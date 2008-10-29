/**
 * This is the pager, and lots more.  Handles everything that deals with
 * "memory" (ELF loading, copyin/copyout, process management, paging) in its
 * own syscall loop.
 *
 * Design is a list of "requests" (aka events, in a way) handled generically -
 * that is, through the request(Start|Continue|Queue|Dequeue) functions. These
 * are OO style, where each request has a three-stage callback system:
 * initialisation, continuation (which will be repeated an arbitrary number of
 * times), and finalisation (which must free resources).  At any point these
 * may be aborted, and the next request immediately started - otherwise each
 * continuation will be triggered by a SOS_REPLY in the pager loop.
 *
 * Everything involving outgoing IPC in any way must go through this request
 * system, for two reasons: safety, and to be non-blocking.
 *
 * Sections:
 * 	- Unfortunately necessary prototypes.
 * 	- Page table
 * 	- Generic (asynchronous) request handling
 * 	- Reading and writing
 * 	- (Demand) paging
 * 	- Copyin/copyout
 * 	- MMap
 * 	- ELF loading
 * 	- Process management
 * 	- Pager (as a thread)
 */
#include <elf/elf.h>
#include <sos/sos.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cache.h"
#include "constants.h"
#include "frames.h"
#include "l4.h"
#include "libsos.h"
#include "list.h"
#include "pager.h"
#include "pair.h"
#include "process.h"
#include "region.h"
#include "swapfile.h"
#include "syscall.h"

#define verbose 3


///////////////////////////////////////////////////////////////////////
// UNFORUNATELY NECESSARY PROTOTYPES
///////////////////////////////////////////////////////////////////////

static void requestStart(int rval);
static void mmapRead(pid_t pid, L4_Word_t addr);


///////////////////////////////////////////////////////////////////////
// PAGE TABLE
///////////////////////////////////////////////////////////////////////

// Masks for page table entries
#define REF_MASK   (1 << 0)
#define MMAP_MASK  (1 << 1)
#define SWAP_MASK  (1 << 2)
#define DIRTY_MASK (1 << 3)
#define ADDRESS_MASK PAGEALIGN

// Kernel always needs frames, so give it some room for things like
// process creation, page table filling, mallocing stuff, etc
// If this is excedded then frames will be swapped out in page faults.
#define FRAMES_ALWAYS_FREE 128

// Two level page table structures
typedef struct Pagetable2_t {
	L4_Word_t pages[PAGEWORDS];
} Pagetable2;

typedef struct Pagetable1_t {
	Pagetable2 *pages2[PAGEWORDS];
} Pagetable1;

// Default file that pages are swapped out to
static Swapfile *defaultSwapfile;

// Allocate a new page table object
Pagetable *pagetable_init(void) {
	assert(sizeof(Pagetable1) == PAGESIZE);
	Pagetable1 *pt = (Pagetable1 *) frame_alloc(FA_PAGETABLE1);

	for (int i = 0; i < PAGEWORDS; i++) {
		pt->pages2[i] = NULL;
	}

	return (Pagetable *) pt;
}

// Look up a virtual address in a page table and return a pointer to its entry.
// This will lazily create the lower levels as necessary
static L4_Word_t *pagetableLookup(Pagetable *pt, L4_Word_t addr) {
	Pagetable1 *level1 = (Pagetable1 *) pt;
	assert(level1 != NULL);
	assert(level1->pages2 != NULL);

	addr /= PAGESIZE;
	int offset1 = addr / PAGEWORDS;
	int offset2 = addr - (offset1 * PAGEWORDS);

	if (level1->pages2[offset1] == NULL) {
		assert(sizeof(Pagetable2) == PAGESIZE);
		level1->pages2[offset1] = (Pagetable2 *) frame_alloc(FA_PAGETABLE2);

		for (int i = 0; i < PAGEWORDS; i++) {
			level1->pages2[offset1]->pages[i] = 0;
		}
	}

	return &level1->pages2[offset1]->pages[offset2];
}

// Free a page table, including all lower levels that have been created
static void pagetableFree(Pagetable *pt) {
	Pagetable1 *pt1 = (Pagetable1 *) pt;
	assert(pt1 != NULL);

	for (int i = 0; i < PAGEWORDS; i++) {
		Pagetable2 *pt2 = pt1->pages2[i];

		if (pt2 != NULL) {
			// Free the swap slots of swapped out pages
			for (int j = 0; j < PAGEWORDS; j++) {
				if (pt2->pages[j] & SWAP_MASK) {
					assert(pt2->pages[j] & MMAP_MASK);
					swapslot_free(defaultSwapfile, pt2->pages[j] & ADDRESS_MASK);
				}
			}

			frame_free((L4_Word_t) pt2);
		}
	}

	frame_free((L4_Word_t) pt1);
}


///////////////////////////////////////////////////////////////////////
// GENERIC (ASYNCHRONOUS) REQUEST HANDLING
///////////////////////////////////////////////////////////////////////

// Generic object for handling asynchronous continuations
typedef struct Request_t {
	// Stage in request continuations, passed to cont.
	int stage;

	// OO-style data associated with the request.
	void *data;

	// Called when this request is first run, returns the stage to start at.
	// rval is the return value from the last request, or UNDEFINED if isolated
	int (*init)(void *data, int rval);

	// Called each time a continuation arrives for this thread with data from
	// the Request and the return value from the IPC.
	// Returns the number of the next stage or SUCCESS/FAILURE to finish.
	int (*cont)(void *data, int stage, int rval);

	// Called when request finishes, the return value passed to the next request
	int (*finish)(void *data, int success);
} Request;

// Some predefined request stages
#define UNDEFINED (-4)
#define ABORT     (-3)
#define FAILURE   (-2)
#define SUCCESS   (-1)

// Requests, in order they are to be processed
static List *requests; // [Request]

// Allocate a request object
static Request *requestAlloc(void *data, int (*init)(void*, int),
		int (*cont)(void*, int, int), int (*finish)(void*, int)) {
	Request *req = (Request *) malloc(sizeof(Request));
	*req = (Request) {UNDEFINED, data, init, cont, finish};
	return req;
}

// Queue a request and immediately start if at the head
static void requestQueue(void *data, int (*init)(void*, int),
		int (*cont)(void*, int, int), int (*finish)(void*, int)) {
	Request *req = requestAlloc(data, init, cont, finish);
	if (list_null(requests)) {
		list_push(requests, req);
		requestStart(UNDEFINED);
	} else {
		list_push(requests, req);
	}
}

// Push a request to the front of the queue and immediately start
static void requestImmediate(void *data, int (*init)(void*, int),
		int (*cont)(void*, int, int), int (*finish)(void*, int)) {
	Request *req = requestAlloc(data, init, cont, finish);
	list_shift(requests, req);
	requestStart(UNDEFINED);
}

// Dequeue a request and start the next one if available
static void requestDequeue(int rval) {
	free(list_unshift(requests));
	if (!list_null(requests)) {
		requestStart(rval);
	}
}

// Continue the current request
static void requestContinue(int rval) {
	Request *req = (Request *) list_peek(requests);
	int newStage = req->cont(req->data, req->stage, rval);
	assert(newStage != UNDEFINED);

	if ((newStage == FAILURE) || (newStage == SUCCESS)) {
		requestDequeue(req->finish(req->data, newStage == SUCCESS));
	} else if (newStage == ABORT) {
		requestDequeue(UNDEFINED);
	} else {
		req->stage = newStage;
	}
}

// Start the next request in the queue, passing through rval from the last one
static void requestStart(int rval) {
	Request *req = (Request *) list_peek(requests);
	req->stage = req->init(req->data, rval);
	if (req->stage == ABORT) {
		requestDequeue(ABORT);
	}
}


///////////////////////////////////////////////////////////////////////
// READING AND WRITING
///////////////////////////////////////////////////////////////////////

// For asynchronous reading and writing of mmapped addresses
typedef struct IORequest_t {
	//pid_t pid;
	L4_Word_t dst;
	L4_Word_t src;
	int size;
	int offset;
	char path[MAX_FILE_NAME];
	fildes_t fd;
	int alwaysOpen;
	int rights;
} IORequest;

// The possible stages of IO (note: don't assume they are sequential)
typedef enum {
	IO_STAGE_OPEN,
	IO_STAGE_STAT,
	IO_STAGE_LSEEK,
	IO_STAGE_READ,
	IO_STAGE_WRITE,
	IO_STAGE_CLOSE
} IOStage;

// Allocate a new IO request object (used by both read and write)
static IORequest *allocIORequest(
		L4_Word_t dst, L4_Word_t src, int size, char *path) {
	IORequest *req = (IORequest *) malloc(sizeof(IORequest));

	req->dst = dst;
	req->src = src;
	req->size = size;
	req->offset = 0;
	strncpy(req->path, path, MAX_FILE_NAME);
	req->fd = VFS_NIL_FILE;
	req->alwaysOpen = FALSE;
	req->rights = FM_READ;

	return req;
}

// Callbacks for reading, to use with requests
static int readInit(void *data, int rval) {
	IORequest *req = (IORequest *) data;
	dprintf(1, "*** %s: path=\"%s\"\n", __FUNCTION__, req->path);

	fildes_t currentFd = vfs_getfd(L4_ThreadNo(sos_my_tid()), req->path);

	if (currentFd != VFS_NIL_FILE) {
		dprintf(2, "%s: already open at %d\n", __FUNCTION__, currentFd);
		assert(req->alwaysOpen);

		req->fd = currentFd;
		lseekNonblocking(req->fd, req->src, SEEK_SET);

		return IO_STAGE_LSEEK;
	} else {
		dprintf(2, "%s: not open\n", __FUNCTION__);

		strncpy(pager_buffer(sos_my_tid()), req->path, MAX_FILE_NAME);
		openNonblocking(NULL, FM_READ);

		return IO_STAGE_OPEN;
	}
}

static int readCont(void *data, int stage, int rval) {
	dprintf(2, "*** %s: rval=%d\n", __FUNCTION__, rval);
	IORequest *req = (IORequest *) data;

	switch (stage) {
		case IO_STAGE_OPEN:
			dprintf(2, "*** %s: IO_STAGE_OPEN\n", __FUNCTION__);

			if (rval < 0) {
				return FAILURE;
			} else {
				req->fd = rval;
				req->offset = 0;
				lseekNonblocking(req->fd, req->src, SEEK_SET);
				return IO_STAGE_LSEEK;
			}

		case IO_STAGE_LSEEK:
			dprintf(2, "*** %s: IO_STAGE_LSEEK\n", __FUNCTION__);
			assert(rval == 0);

			readNonblocking(req->fd, min(IO_MAX_BUFFER, req->size));
			return IO_STAGE_READ;

		case IO_STAGE_READ:
			dprintf(2, "*** %s: IO_STAGE_READ\n", __FUNCTION__);
			assert(rval >= 0);

			memcpy((void *) (req->dst + req->offset),
					pager_buffer(sos_my_tid()), rval);
			req->offset += rval;
			assert(req->offset <= req->size);

			if (req->offset == req->size) {
				if (req->alwaysOpen) {
					return SUCCESS;
				} else {
					closeNonblocking(req->fd);
					return IO_STAGE_CLOSE;
				}
			} else {
				assert(rval > 0);
				readNonblocking(req->fd,
						min(IO_MAX_BUFFER, req->size - req->offset));
				return IO_STAGE_READ;
			}

		case IO_STAGE_CLOSE:
			dprintf(2, "*** %s: STAGE_CLOSE\n", __FUNCTION__);
			assert(rval == 0);
			return SUCCESS;

		default:
			assert(!"default");
			return FAILURE;
	}
}

// Callbacks for writing, to use with requests
static int writeInit(void *data, int rval) {
	IORequest *req = (IORequest *) data;
	dprintf(1, "*** %s: path=\"%s\"\n", __FUNCTION__, req->path);

	fildes_t currentFd = vfs_getfd(L4_ThreadNo(sos_my_tid()), req->path);

	if (currentFd != VFS_NIL_FILE) {
		dprintf(2, "%s: already open at %d\n", __FUNCTION__, currentFd);
		assert(req->alwaysOpen);

		req->fd = currentFd;
		lseekNonblocking(req->fd, req->dst, SEEK_SET);

		return IO_STAGE_LSEEK;
	} else {
		dprintf(2, "%s: not open\n", __FUNCTION__);

		strncpy(pager_buffer(sos_my_tid()), req->path, MAX_FILE_NAME);
		openNonblocking(NULL, FM_WRITE);

		return IO_STAGE_OPEN;
	}
}

static int writeCont(void *data, int stage, int rval) {
	dprintf(2, "*** %s: rval=%d\n", __FUNCTION__, rval);
	IORequest *req = (IORequest *) data;
	size_t size;

	switch (stage) {
		case IO_STAGE_OPEN:
			dprintf(2, "*** %s: IO_STAGE_OPEN\n", __FUNCTION__);

			if (rval < 0) {
				return FAILURE;
			} else {
				req->fd = rval;
				req->offset = 0;
				lseekNonblocking(req->fd, req->dst, SEEK_SET);
				return IO_STAGE_LSEEK;
			}

		case IO_STAGE_LSEEK:
			dprintf(2, "*** %s: IO_STAGE_LSEEK\n", __FUNCTION__);
			assert(rval == 0);

			size = min(COPY_BUFSIZ, req->size);
			memcpy(pager_buffer(sos_my_tid()), (void *) req->src, size);
			writeNonblocking(req->fd, size);

			return IO_STAGE_WRITE;

		case IO_STAGE_WRITE:
			dprintf(2, "*** %s: IO_STAGE_WRITE\n", __FUNCTION__);
			assert(rval >= 0);
			req->offset += rval;
			assert(req->offset <= req->size);

			if (req->offset == req->size) {
				if (req->alwaysOpen) {
					return SUCCESS;
				} else {
					closeNonblocking(req->fd);
					return IO_STAGE_CLOSE;
				}
			} else {
				assert(rval > 0);
				size = min(IO_MAX_BUFFER, req->size - req->offset);
				memcpy(pager_buffer(sos_my_tid()),
						(void *) (req->src + req->offset), size);

				writeNonblocking(req->fd, size);
				return IO_STAGE_WRITE;
			}

		case IO_STAGE_CLOSE:
			dprintf(2, "*** %s: IO_STAGE_CLOSE\n", __FUNCTION__);
			assert(rval == 0);
			return SUCCESS;

		default:
			assert(!"default");
			return FAILURE;
	}
}

// Finishing is the same for either
static int readwriteFinish(void *data, int success) {
	dprintf(2, "*** %s\n", __FUNCTION__);
	free(data);

	if (!success) {
		dprintf(0, "!!! %s: read failed\n", __FUNCTION__);
		return FAILURE;
	} else {
		return SUCCESS;
	}
}


///////////////////////////////////////////////////////////////////////
// (DEMAND) PAGING
///////////////////////////////////////////////////////////////////////

// For storing information about (delayed) page faults
typedef struct Pagefault_t {
	pid_t pid;
	L4_Word_t addr;
	int rights;
} Pagefault;

// Allocate a Pagefault object
static Pagefault *pagefaultAlloc(pid_t pid, L4_Word_t addr, int rights) {
	Pagefault *pf = (Pagefault *) malloc(sizeof(Pagefault));
	*pf = (Pagefault) {pid, addr, rights};
	return pf;
}

// Some requests that pagefaults may as for
typedef enum {
	PAGEFAULT_REQUEST_SWAPOUT,
	PAGEFAULT_REQUEST_MMAP_READ,
	PAGEFAULT_REQUEST_PROCESS_DELETE
} PagerfaultRequest;

// All pages that have been allocated to user processes (for second chance algo)
static List *alloced; // [(pid, word)]

// Map a page to an address space
static int mapPage(
		L4_SpaceId_t sid, L4_Word_t virt, L4_Word_t phys, int rights) {
	assert((virt & ~PAGEALIGN) == 0);
	assert((phys & ~PAGEALIGN) == 0);

	L4_Fpage_t fpage = L4_Fpage(virt, PAGESIZE);
	L4_Set_Rights(&fpage, rights);
	L4_PhysDesc_t ppage = L4_PhysDesc(phys, DEFAULT_MEMORY);

	return L4_MapFpage(sid, fpage, ppage);
}

// Unmap a page from an address space
static int unmapPage(L4_SpaceId_t sid, L4_Word_t virt) {
	(void) unmapPage;

	assert((virt & ~PAGEALIGN) == 0);
	L4_Fpage_t fpage = L4_Fpage(virt, PAGESIZE);
	return L4_UnmapFpage(sid, fpage);
}

// Prepare for some data from a user program to be changed by the pager.
// This involves flushing the user cache, and invalidating ours.
static void prepareDataIn(Process *p, L4_Word_t vaddr) {
	assert((vaddr & ~PAGEALIGN) == 0);

	L4_Word_t *entry = pagetableLookup(process_get_pagetable(p), vaddr);
	L4_Word_t frame = *entry & ADDRESS_MASK;

	dprintf(3, "*** prepareDataIn: p=%d vaddr=%p frame=%p\n",
			process_get_pid(p), (void *) vaddr, (void *) frame);

	please(CACHE_FLUSH_RANGE(process_get_sid(p), vaddr, vaddr + PAGESIZE));
	please(CACHE_FLUSH_RANGE_INVALIDATE(L4_rootspace, frame, frame + PAGESIZE));
}

// Prepare for some data changed by the pager to be seen by the user.
// This involves flushing our cache, and invalidating the users.
static void prepareDataOut(Process *p, L4_Word_t vaddr) {
	assert((vaddr & ~PAGEALIGN) == 0);

	L4_Word_t *entry = pagetableLookup(process_get_pagetable(p), vaddr);
	L4_Word_t frame = *entry & ADDRESS_MASK;

	dprintf(3, "*** prepareDataOut: p=%d vaddr=%p frame=%p\n",
			process_get_pid(p), (void *) vaddr, (void *) frame);

	please(CACHE_FLUSH_RANGE(L4_rootspace, frame, frame + PAGESIZE));
	please(CACHE_FLUSH_RANGE_INVALIDATE(
				process_get_sid(p), vaddr, vaddr + PAGESIZE));
}

// Allocate a frame and add to the list
static L4_Word_t userFrameAlloc(Process *p, L4_Word_t page) {
	L4_Word_t frame = frame_alloc(FA_PAGERALLOC);
	dprintf(1, "*** %s: allocated frame %p\n", __FUNCTION__, (void *) frame);

	list_push(alloced, pair_alloc(process_get_pid(p), page));
	process_get_info(p)->size++;

	return frame;
}

// Set up a page to swap out
static void swapout(void) {
	(void) writeInit;
	(void) writeCont;
	// set the swap mask
}

// Handle a page fault, doing the mapping and all that if it can.
// Will return the type of request needed to complete if there is one.
static int pagefaultHandle(pid_t pid, L4_Word_t addr, int rights) {
	dprintf(2, "*** %s: fault on pid=%d, addr=%p rights=%d\n",
			__FUNCTION__, pid, addr, rights);

	Process *p = process_lookup(pid);
	if ((p == NULL) || (process_get_state(p) == PS_STATE_ZOMBIE)) {
		return FAILURE;
	}

	// Find region it belongs in, and check permissions
	Region *r = list_find(process_get_regions(p), region_find, (void *) addr);

	if (r == NULL) {
		printf("Segmentation fault (%d)\n", process_get_pid(p));
		process_set_state(p, PS_STATE_ZOMBIE);
		return PAGEFAULT_REQUEST_PROCESS_DELETE;
	} else if ((region_get_rights(r) & rights) == 0) {
		printf("Permission fault (%d)\n", process_get_pid(p));
		process_set_state(p, PS_STATE_ZOMBIE);
		return PAGEFAULT_REQUEST_PROCESS_DELETE;
	}

	// Place in, or retrieve from, page table.
	L4_Word_t *entry = pagetableLookup(process_get_pagetable(p), addr);
	L4_Word_t frame = *entry & ADDRESS_MASK;
	dprintf(3, "*** %s: found entry %p\n", __FUNCTION__, (void *) *entry);

	if (*entry & MMAP_MASK) {
		dprintf(2, "*** %s: page %p mmapped\n", __FUNCTION__, (void *) *entry);
		return PAGEFAULT_REQUEST_MMAP_READ;
	} else if (region_map_directly(r)) {
		dprintf(2, "*** %s: mapping directly\n", __FUNCTION__); // bootinfo
		frame = addr & PAGEALIGN;
	} else {
		// Gets a bit tricky, since we may or may not need to allocate a frame.
		// In any case, if there are subsequently none left then we need to
		// start swapping out some frames
		if (frame == 0) {
			assert(*entry == 0);
			frame = userFrameAlloc(p, addr & PAGEALIGN);
		}

		if (frames_free() < FRAMES_ALWAYS_FREE) {
			return PAGEFAULT_REQUEST_SWAPOUT;
		}
	}

	dprintf(3, "*** %s: mapping vaddr=%p pid=%d frame=%p rights=%d\n",
			__FUNCTION__, (void *) (addr & PAGEALIGN), process_get_pid(p),
			(void *) frame, region_get_rights(r));

	// TODO dirty bit optimisation, fun :-)

	*entry = frame | REF_MASK;
	mapPage(process_get_sid(p), addr & PAGEALIGN, frame, region_get_rights(r));
	return SUCCESS;
}

// Continue a pagefault request - rval is ignored since this is a once-only
// deal - if the pagefault fails now, need to do whatever the handler says
static int pagefaultInit(void *data, int rval) {
	dprintf(1, "*** %s\n", __FUNCTION__);

	Pagefault *pf = (Pagefault *) data;
	int request = pagefaultHandle(pf->pid, pf->addr, pf->rights);

	switch (request) {
		case SUCCESS:
			syscall_reply_v(process_get_tid(process_lookup(pf->pid)), 0);
		case FAILURE: // deliberate fall-through
			free(pf);
			return ABORT;

		case PAGEFAULT_REQUEST_PROCESS_DELETE:
			assert(!"process delete");
			return UNDEFINED;

		case PAGEFAULT_REQUEST_MMAP_READ:
			mmapRead(pf->pid, pf->addr);
			return UNDEFINED;

		case PAGEFAULT_REQUEST_SWAPOUT:
			swapout();
			return UNDEFINED;

		default:
			assert("!default");
			return FAILURE;
	}
}


// Try to handle a pagefault immediately, but delay if it fails
static void pagefaultImmediate(pid_t pid, L4_Word_t addr, int rights) {
	int request = pagefaultHandle(pid, addr, rights);
	dprintf(2, "*** %s: handled with %d\n", __FUNCTION__, request);

	if (request == SUCCESS) {
		syscall_reply_v(process_get_tid(process_lookup(pid)), 0);
	} else if (request == FAILURE) {
		; // No point in replying
	} else if (request == PAGEFAULT_REQUEST_PROCESS_DELETE) {
		assert(!"process delete");
	} else {
		Pagefault *pf = pagefaultAlloc(pid, addr, rights);
		requestQueue(pf, pagefaultInit, NULL, NULL);
	}
}


///////////////////////////////////////////////////////////////////////
// COPYIN/COPYOUT
///////////////////////////////////////////////////////////////////////

// The two types of operations
typedef enum {
	COPY_IN,
	COPY_OUT
} CopyOp;

// Non-static data about a copy (in or out)
typedef struct Copy_t {
	Process *p;
	CopyOp op;
	L4_Word_t dst;
	L4_Word_t src;
} Copy;

// Allocate a copy
static Copy *allocCopy(Process *p, CopyOp op, L4_Word_t dst, L4_Word_t src) {
	Copy *req = (Copy*) malloc(sizeof(Copy));
	*req = (Copy) {p, op, dst, src};
	return req;
}

// Will either be the first try, or subsequent delayed requests
typedef enum {
	COPY_STAGE_FIRST,
	COPY_STAGE_DELAYED
} CopyStage;

// Convenience macros
#define LO_HALF(word) ((word) & 0x0000ffff)
#define HI_HALF(word) (((word) >> 16) & 0x0000ffff)
#define HI_SHIFT(word) ((word) << 16)

// Perpetual buffer for communicating between kernel and userspace
static char copyInOutBuffer[MAX_THREADS * COPY_BUFSIZ];

// Retrieve the buffer for a given thread
char *pager_buffer(L4_ThreadId_t tid) {
	return &copyInOutBuffer[L4_ThreadNo(tid) * COPY_BUFSIZ];
}

// Data about the status of the copyin/out operations
static L4_Word_t copyInOutData[MAX_THREADS];

// Like memcpy, but only copies up to the next page boundary.
// Returns 1 if reached the boundary, 0 otherwise.
static int memcpyPage(char *dst, char *src, size_t size) {
	int i = 0;

	while (i < size) {
		*dst = *src;

		dst++;
		src++;
		i++;

		if ((((L4_Word_t) dst) & ~PAGEALIGN) == 0) break;
		if ((((L4_Word_t) src) & ~PAGEALIGN) == 0) break;
	}

	return i;
}

// Finish a copy in or out
static int copyDone(void *data, int success) {
	assert(success);

	Copy *req = (Copy*) data;
	syscall_reply_v(process_get_tid(req->p), 0);
	free(data);

	return SUCCESS;
}

// Continue a copy in or out
static int copyCont(void *data, int stage, int rval) {
	Copy *req = (Copy *) data;

	dprintf(3, "*** %s: op=%d pid=%d dst=%p src=%p\n", __FUNCTION__, req->op,
			process_get_pid(req->p), (void *) req->dst, (void *) req->src);

	/*

	// Most importantly - need to check that this page is available
	int request = pagefaultHandle(process_get_pid(req->pid), req->src, FM_READ);

	if (request == FAILURE) {
	return FAILURE;
	} else if (request != SUCCESS) {
	if (stage == STAGE_FIRST) {
	// Haven't been blocked yet, so try later
	queueRequest(


	// Will need to differentiate between in and out of course

	*/

	// Relevant data about this operation
	size_t size = LO_HALF(copyInOutData[process_get_pid(req->p)]);
	off_t offset = HI_HALF(copyInOutData[process_get_pid(req->p)]);
	L4_Word_t src = req->src;
	L4_Word_t dst = req->dst;
	L4_Word_t *entry;

	// Alignment/cache/vm issues depend on whether it's a copy in or out
	if (req->op == COPY_IN) {
		assert(pagefaultHandle(process_get_pid(req->p), req->src, FM_READ) == SUCCESS);
		entry = pagetableLookup(process_get_pagetable(req->p), req->src);
		src = (*entry & ADDRESS_MASK) + (req->src & ~PAGEALIGN);
		dst += offset;
		prepareDataIn(req->p, *entry & ADDRESS_MASK);
	} else {
		assert(pagefaultHandle(process_get_pid(req->p), req->dst, FM_WRITE) == SUCCESS);
		entry = pagetableLookup(process_get_pagetable(req->p), req->dst);
		src += offset;
		dst = (*entry & ADDRESS_MASK) + (req->dst & ~PAGEALIGN);
	}

	// Copy data up to the next page
	dprintf(3, "*** %s: copying dst=%p src=%p size=%d offset=%d\n",
			__FUNCTION__, (void *) dst, (void *) src, size, offset);
	offset += memcpyPage((char *) dst, (char *) src, size - offset);
	assert(offset <= size);
	copyInOutData[process_get_pid(req->p)] = size | HI_SHIFT(offset);

	// Cache issues if the data is to the user
	if (req->op == COPY_OUT) {
		prepareDataOut(req->p, *entry & ADDRESS_MASK);
	}

	// If we reached a page boundary, have to restart from the next offset
	if (offset != size) {
		dprintf(1, "*** %s: reached page boundary, restaring\n", __FUNCTION__);
		return copyCont(req, stage, rval);
	} else {
		return SUCCESS;
	}
}

// Begin a copy in
static void copyIn(L4_ThreadId_t tid, void *src, size_t size, int append) {
	dprintf(3, "*** %s: tid=%ld src=%p size=%d\n", __FUNCTION__,
			L4_ThreadNo(tid), src, size);
	Process *p = process_lookup(L4_ThreadNo(tid));

	// Retrieve the data from the status buffers
	L4_Word_t newSize = LO_HALF(copyInOutData[process_get_pid(p)]);
	size_t newBase = HI_HALF(copyInOutData[process_get_pid(p)]);

	if (append) {
		newSize += size;
	} else {
		newSize = size;
		newBase = 0;
	}

	copyInOutData[process_get_pid(p)] = LO_HALF(newSize) | HI_SHIFT(newBase);

	// Similar to pagefaulting, we want to try immediately and only delay if
	// something does need to block.  copyCont will handle this based on stage.
	Copy *req = allocCopy(p, COPY_IN,
			(L4_Word_t) pager_buffer(tid), (L4_Word_t) src);
	
	if (copyCont(req, COPY_STAGE_FIRST, UNDEFINED)) {
		copyDone(req, SUCCESS);
	} else {
		// It was delayed
	}
}

// Begin a copy out (almost the same as copy in)
static void copyOut(L4_ThreadId_t tid, void *dst, size_t size, int append) {
	dprintf(3, "*** %s: tid=%ld dst=%p size=%d\n", __FUNCTION__,
			L4_ThreadNo(tid), dst, size);
	Process *p = process_lookup(L4_ThreadNo(tid));

	L4_Word_t newSize = LO_HALF(copyInOutData[process_get_pid(p)]);
	size_t newBase = HI_HALF(copyInOutData[process_get_pid(p)]);

	if (append) {
		newSize += size;
	} else {
		newSize = size;
		newBase = 0;
	}

	copyInOutData[process_get_pid(p)] = LO_HALF(newSize) | HI_SHIFT(newBase);

	Copy *req = allocCopy(p, COPY_OUT,
			(L4_Word_t) dst, (L4_Word_t) pager_buffer(tid));
	
	if (copyCont(req, COPY_STAGE_FIRST, UNDEFINED)) {
		copyDone(req, SUCCESS);
	}
}


///////////////////////////////////////////////////////////////////////
// MMAP
///////////////////////////////////////////////////////////////////////

// For tracking mmapped pages - disk address is stored in page table
typedef struct MMap_t {
	pid_t pid;
	L4_Word_t memAddr;
	size_t size;
	char path[MAX_FILE_NAME];
} MMap;

// Allocate an MMap object
static MMap *allocMMap(pid_t pid, L4_Word_t memAddr, size_t size, char *path) {
	MMap *mmap = (MMap *) malloc(sizeof(MMap));

	mmap->pid = pid;
	mmap->memAddr = memAddr;
	mmap->size = size;
	strncpy(mmap->path, path, MAX_FILE_NAME);

	return mmap;
}

// Print an MMap object (for use with list_iterate)
static void printMMap(void *contents, void *data) {
	MMap *mmap = (MMap*) contents;
	printf("MMap: pid=%d memAddr=%p size=%u path=%s\n",
			mmap->pid, (void *) mmap->memAddr, mmap->size, mmap->path);
}

// All mmapped objects
static List *mmapped; // [MMap]

// Find an mmapped region, for use with list_find
static int findMMap(void *contents, void *data) {
	MMap *mmap = (MMap *) contents;
	Pair *pair = (Pair *) data; // (pid, word)

	printf("mmap: pid=%d memAddr=%p size=%d path=%s\n",
			mmap->pid, (void *) mmap->memAddr, mmap->size, mmap->path);

	if ((mmap->pid == pair->fst) && (pair->snd >= mmap->memAddr) &&
			(pair->snd < (mmap->memAddr + PAGESIZE))) {
		return 1;
	} else {
		return 0;
	}
}

// Read in an mmapped page (by starting a series of callbacks)
static void mmapRead(pid_t pid, L4_Word_t addr) {
	Process *p = process_lookup(pid);
	assert(p != NULL);
	assert(process_get_state(p) != PS_STATE_ZOMBIE);

	Pair pidAddr = PAIR(pid, addr);
	MMap *mmap = list_find(mmapped, findMMap, &pidAddr);
	assert(mmap != NULL);
	assert(mmap->size <= PAGESIZE);

	// Assign the address straight to a frame, if there aren't enough then
	// this will sort itself out with the (eventual) page fault
	L4_Word_t *entry = pagetableLookup(process_get_pagetable(p), addr);
	assert(*entry & MMAP_MASK);
	L4_Word_t dskAddr = *entry & ADDRESS_MASK;

	// This will (rightfully) unset the MMAP/SWAP mask too
	*entry = userFrameAlloc(p, addr & ADDRESS_MASK) | REF_MASK;
	memset((void *) (*entry & ADDRESS_MASK), 0x00, PAGESIZE); // for ELF loading

	IORequest *req = allocIORequest(
			*entry & ADDRESS_MASK, dskAddr, mmap->size, mmap->path);

	// And we don't need the mmap record any more (since it isn't a "real" mmap)
	list_delete_first(mmapped, findMMap, &pidAddr);
	free(mmap);

	requestImmediate(req, readInit, readCont, readwriteFinish);
}


// Swap out a page chosen by second chance



///////////////////////////////////////////////////////////////////////
// ELF LOADING
///////////////////////////////////////////////////////////////////////

// Asynchronous request to read the ELF header and set up mmapped regions
typedef struct ElfloadRequest_t {
	char path[MAX_FILE_NAME];
	pid_t parent;
	pid_t child;
	struct Elf32_Header *header;
	int started;
} ElfloadRequest;

// Allocate a new ELF load request
static ElfloadRequest *allocElfloadRequest(char *path,
		pid_t parent, pid_t child) {
	ElfloadRequest *req = (ElfloadRequest *) malloc(sizeof(ElfloadRequest));

	strncpy(req->path, path, MAX_FILE_NAME);
	req->parent = parent;
	req->child = child;
	req->started = FALSE;

	return req;
}

// Set a single page as mmapped (with appropriate flags)
static void setAddrMMap(Process *p, L4_Word_t memAddr, L4_Word_t dskAddr,
		size_t size, char *path) {
	assert((dskAddr & ~PAGEALIGN) == 0);
	assert((memAddr & ~PAGEALIGN) == 0);
	assert(size <= PAGESIZE);

	L4_Word_t *entry = pagetableLookup(process_get_pagetable(p), memAddr);
	*entry = dskAddr | MMAP_MASK;

	list_push(mmapped, allocMMap(process_get_pid(p), memAddr, size, path));
}

// Set a region as mmaped, will fill the page table (with appropriate flags)
// and add to the mmapped list, once per page
static void setRegionMMap(Region *r, Process *p, L4_Word_t dskAddr,
		size_t dskSize, char *path) {
	assert(dskSize <= region_get_size(r));
	assert((region_get_base(r) & ~PAGEALIGN) == (dskAddr & ~PAGEALIGN));

	// Regions can be nasty and start in the middle of the page, so make
	// it look normal by page aligning it, and extending the size accordingly
	L4_Word_t rBase = region_get_base(r) & PAGEALIGN;
	L4_Word_t rSize = region_get_size(r) + (region_get_base(r) & ~PAGEALIGN);
	L4_Word_t dBase = dskAddr & PAGEALIGN;
	L4_Word_t dSize = dskSize + (dskAddr & ~PAGEALIGN);

	for (size_t size = 0; size < rSize; size += PAGESIZE) {
		setAddrMMap(p, rBase + size, dBase + size,
				min(PAGESIZE, dSize - size), path);
	}
}

// This "init" function actually does everything, since all the blocking work
// is done by the read request.  Otherwise it's just parsing the ELF header.
static int elfloadInit(void *data, int rval) {
	ElfloadRequest *req = (ElfloadRequest *) data;
	dprintf(1, "*** %s: path=\"%s\" child=%d parent=%d started=%d\n",
			__FUNCTION__, req->path, req->child, req->parent, req->started);

	if (!req->started) {
		// Init is being called for the first time, so need to set up the read
		req->started = TRUE;
		req->header = (struct Elf32_Header *) frame_alloc(FA_ELFLOAD);

		requestImmediate(
				allocIORequest((L4_Word_t) req->header, 0, COPY_BUFSIZ, req->path),
				readInit, readCont, readwriteFinish);

		return UNDEFINED;
	} else {
		// Read has returned, so we do the complicated ELF parsing and then finish
		L4_ThreadId_t parentTid = process_get_tid(process_lookup(req->parent));

		if (!rval) {
			dprintf(0, "!!! %s: \"%s\" read failed\n", __FUNCTION__, req->path);
			syscall_reply(parentTid, (-1));
		} else if (elf32_checkFile(req->header) != 0) {
			dprintf(0, "!!! %s: \"%s\" not ELF file\n", __FUNCTION__, req->path);
			syscall_reply(parentTid, (-1));
		} else {
			// We may proceeed... starting with basic process initialisation
			Process *p = process_init(PS_TYPE_PROCESS);
			process_set_name(p, req->path);
			process_get_info(p)->pid = req->child;

			// Set up regions and prefill the page table
			for (int i = 0; i < elf32_getNumProgramHeaders(req->header); i++) {
				Region *r = region_alloc(
						REGION_OTHER,
						elf32_getProgramHeaderVaddr(req->header, i),
						elf32_getProgramHeaderMemorySize(req->header, i),
						elf32_getProgramHeaderFlags(req->header, i), 0);
				L4_Word_t diskAddr = elf32_getProgramHeaderOffset(req->header, i);

				process_add_region(p, r);
				setRegionMMap(r, p, diskAddr,
						elf32_getProgramHeaderFileSize(req->header, i), req->path);
				if (verbose > 1) list_iterate(mmapped, printMMap, NULL);
			}

			process_prepare(p); // note: opens stdout/stderr via IPC
			process_set_ip(p, (void *) elf32_getEntryPoint(req->header));
			if (verbose > 1) process_dump(p);
			process_run(p, YES_TIMESTAMP);

			syscall_reply(parentTid, process_get_pid(p));
		}

		frame_free((L4_Word_t) req->header);
		free(req);
		return ABORT;
	}
}


///////////////////////////////////////////////////////////////////////
// PROCESS MANAGEMENT
///////////////////////////////////////////////////////////////////////

// Request for deleting a process
typedef struct ProcessDelete_t {
	pid_t caller;
	pid_t victim;
} ProcessDelete;

// Allocate a process delete object
static ProcessDelete *allocProcessDelete(pid_t caller, pid_t victim) {
	ProcessDelete *pd = (ProcessDelete *) malloc(sizeof(ProcessDelete));
	*pd = (ProcessDelete) {caller, victim};
	return pd;
}

// Grow the heap region of a process by (at least) the specified amount
static int heapGrow(uintptr_t *base, unsigned int nb) {
	dprintf(2, "*** heapGrow(%p, %lx)\n", base, nb);

	// Find the current heap section.
	Process *p = process_lookup(L4_SpaceNo(L4_SenderSpace()));
	Region *heap = list_find(process_get_regions(p),
			region_find_type, (void *) REGION_HEAP);
	assert(heap != NULL);

	// Top of heap is the (new) start of the free region, this is
	// what morecore/malloc expect.
	dprintf(2, "*** %s: base was %p, now ", __FUNCTION__, (void *) *base);
	*base = region_get_base(heap) + region_get_size(heap);
	dprintf(2, "%p\n", (void *) *base);

	// Move the heap region so SOS knows about it.
	region_set_size(heap, nb + region_get_size(heap));

	// Have the option of returning 0 to signify no more memory.
	return 1;
}

// Free allocated frames (for use with list_delete)
static int framesFree(void *contents, void *data) {
	Pair *curr = (Pair *) contents; // (pid, word)
	Process *p = (Process *) data;

	if ((pid_t) curr->fst == process_get_pid(p)) {
		L4_Word_t *entry = pagetableLookup(process_get_pagetable(p), curr->snd);
		assert((*entry & MMAP_MASK) == 0);
		frame_free(*entry & ADDRESS_MASK);
		free(curr);
		return 1;
	} else {
		return 0;
	}
}

// Free mmapped frames (for use with list_delete)
static int mmappedFree(void *contents, void *data) {
	MMap *curr = (MMap *) contents;
	Process *p = (Process *) data;

	if (curr->pid == process_get_pid(p)) {
		free(curr);
		return 1;
	} else {
		return 0;
	}
}

// Free allocated regions (for use with list_iterate)
static void regionsFree(void *contents, void *data) {
	region_free((Region *) contents);
}

// There is only the init callback for process deletion since the reason
// it needs to go in the queue is for the sequential safety.
static int processDeleteInit(void *data, int rval) {
	dprintf(2, "*** %s\n", __FUNCTION__);
	ProcessDelete *pd = (ProcessDelete *) data;
	Process *p = process_lookup(pd->victim);

	assert(p != NULL);
	assert(process_get_state(p) == PS_STATE_ZOMBIE);
	assert(process_can_kill(p));

	// This will terminate the thread, and free L4's resources
	assert(process_kill(p) == 0);

	// Flush and close open files
	process_close_files(p);
	process_remove(p);

	// Free allocated resources
	Pair args = PAIR(process_get_pid(p), ADDRESS_ALL);
	list_delete(alloced, framesFree, p);
	list_delete(mmapped, mmappedFree, &args);
	pagetableFree(process_get_pagetable(p));
	list_iterate(process_get_regions(p), regionsFree, NULL);
	list_destroy(process_get_regions(p));

	// Wake all waiting processes
	process_wake_all(process_get_pid(p));

	// And done
	free(p);

	return ABORT;
}

// Set up a request to delete a process (yes, a REQUEST, for safety)
static void processDelete(pid_t caller, pid_t victim) {
	// Check we can kill the process
	Process *p = process_lookup(victim);

	if ((p == NULL) || !process_can_kill(p)) {
		syscall_reply(process_get_tid(process_lookup(caller)), (-1));
	}

	// If victim is already a zombie then we don't need to do anything
	// (and in fact can reply with a failure to the caller), otherwise
	// the victim is now a zombie :-)
	if (process_get_state(p) == PS_STATE_ZOMBIE) {
		syscall_reply(process_get_tid(process_lookup(caller)), (-1));
	} else {
		process_set_state(p, PS_STATE_ZOMBIE);
	}

	// Now we must wait until it is actually destroyed and freed
	requestQueue(allocProcessDelete(caller, victim),
			processDeleteInit, NULL, NULL);
}


///////////////////////////////////////////////////////////////////////
// PAGER (AS A THREAD)
///////////////////////////////////////////////////////////////////////

// Thread id of the pager, set once it has started and L4_nilthread otherwise
static L4_ThreadId_t pagerTid;

L4_ThreadId_t pager_get_tid(void) {
	return pagerTid;
}

int pager_is_active(void) {
	return !L4_IsThreadEqual(pagerTid, L4_nilthread);
}

// Indicates whether the pager is busy in an IPC - for debugging only
//static int pagerBusy;

// The big important syscall loop of the pager
static void pager_handler(void) {
	L4_Accept(L4_AddAcceptor(L4_UntypedWordsAcceptor, L4_NotifyMsgAcceptor));

	pagerTid = sos_my_tid();
	dprintf(1, "*** %s: tid=%ld\n", __FUNCTION__, L4_ThreadNo(pagerTid));

	L4_Msg_t msg;
	L4_MsgTag_t tag;
	L4_ThreadId_t tid = L4_nilthread;
	Process *p;
	pid_t pid;
	L4_Word_t tmp;

	for (;;) {
		tag = L4_Wait(&tid);

		tid = sos_sid2tid(L4_SenderSpace());
		p = process_lookup(L4_ThreadNo(tid));
		L4_MsgStore(tag, &msg);

		if (!L4_IsSpaceEqual(L4_SenderSpace(), L4_rootspace)) {
			dprintf(2, "*** %s: tid=%ld tag=%s\n", __FUNCTION__,
					L4_ThreadNo(tid), syscall_show(TAG_SYSLAB(tag)));
		}

		switch (TAG_SYSLAB(tag)) {
			case L4_PAGEFAULT:
				pagefaultImmediate(process_get_pid(p),
						L4_MsgWord(&msg, 0), L4_Label(tag) & 0x7);
				break;

			case SOS_COPYIN:
				copyIn(tid, (void *) L4_MsgWord(&msg, 0),
						(size_t) L4_MsgWord(&msg, 1), (int) L4_MsgWord(&msg, 2));
				break;

			case SOS_COPYOUT:
				copyOut(tid, (void *) L4_MsgWord(&msg, 0),
						(size_t) L4_MsgWord(&msg, 1), (int) L4_MsgWord(&msg, 2));
				break;

			case SOS_REPLY:
				assert(L4_IsSpaceEqual(L4_SenderSpace(), L4_rootspace));
				requestContinue(L4_MsgWord(&msg, 0));
				break;

			case SOS_MOREMEM:
				syscall_reply(tid, heapGrow(
						(uintptr_t *) pager_buffer(tid), L4_MsgWord(&msg, 0)));
				break;

			case SOS_MEMLOC:
				syscall_reply(tid, *(pagetableLookup(process_get_pagetable(p),
								L4_MsgWord(&msg, 0) & PAGEALIGN)));
				break;

			case SOS_MEMFREE:
				frames_print_allocation();
				syscall_reply(tid, frames_free());
				break;

			case SOS_SWAPUSE:
				syscall_reply(tid, swapfile_get_usage(defaultSwapfile));
				break;

			case SOS_PHYSUSE:
				syscall_reply(tid, frames_allocated());
				break;

			case SOS_PROCESS_WAIT:
				tmp = L4_MsgWord(&msg, 0);
				if (tmp == ((L4_Word_t) (-1))) {
					process_wait_any(p);
				} else {
					process_wait_for(process_lookup(tmp), p);
				}
				break;

			case SOS_PROCESS_STATUS:
				syscall_reply(tid, process_write_status(
							(process_t *) pager_buffer(tid), L4_MsgWord(&msg, 0)));
				break;

			case SOS_PROCESS_DELETE:
				processDelete(process_get_pid(p), L4_MsgWord(&msg, 0));
				break;

			case SOS_PROCESS_CREATE:
				pid = reserve_pid();

				if (pid != NIL_PID) {
					ElfloadRequest *req = allocElfloadRequest(
							pager_buffer(tid), process_get_pid(p), pid);
					requestQueue(req, elfloadInit, NULL, NULL);
				} else {
					dprintf(0, "Out of processes\n");
					syscall_reply(tid, -1);
				}

				break;

			case L4_EXCEPTION:
				dprintf(0, "Exception (ip=%p sp=%p id=0x%lx cause=0x%lx pid=%d)\n",
						(void *) L4_MsgWord(&msg, 0), (void *) L4_MsgWord(&msg, 1),
						L4_MsgWord(&msg, 2), L4_MsgWord(&msg, 3), L4_MsgWord(&msg, 4),
						process_get_pid(p));
				processDelete(NIL_PID, process_get_pid(p));
				break;

			default:
				dprintf(0, "!!! pager: unhandled syscall tid=%ld id=%d name=%s\n",
						L4_ThreadNo(tid), TAG_SYSLAB(tag), syscall_show(TAG_SYSLAB(tag)));
		}

		dprintf(3, "*** pager_handler: finished %s from %d\n",
				syscall_show(TAG_SYSLAB(tag)), process_get_pid(p));
	}

	dprintf(0, "!!! pager_handler: loop failed!\n");
}

// Initialise important data structures, and the actual pager thread of course
void pager_init(void) {
	assert(!pager_is_active());

	// Set up lists
	alloced = list_empty();
	mmapped = list_empty();
	requests = list_empty();

	// The default swapfile (.swap)
	defaultSwapfile = swapfile_init(SWAPFILE_FN);

	// Start the real pager process
	Process *p = process_run_rootthread("pager", pager_handler, YES_TIMESTAMP);
	process_set_ipcfilt(p, PS_IPC_NONBLOCKING);

	// Wait until it has actually started
	while (!pager_is_active()) L4_Yield();
}


///////////////////////////////////////////////////////////////////////
// MESSY MESSY MESSY MESSY MESSY MESSY
///////////////////////////////////////////////////////////////////////


#if 0

static void pagerFrameFree(Process *p, L4_Word_t frame) {
	assert((frame & ~PAGEALIGN) == 0);
	frame_free(frame);
	allocLimit++;

	if (p != NULL) process_get_info(p)->size--;
}

static int framesFree(void *contents, void *data) {
	Pair *curr = (Pair *) contents; // (pid, word)
	Process *p = (Process *) data;

	if ((pid_t) curr->fst == process_get_pid(p)) {
		L4_Word_t *entry = pagetableLookup(process_get_pagetable(p), curr->snd);
		pagerFrameFree(p, *entry & ADDRESS_MASK);
		return 1;
	} else {
		return 0;
	}
}

static int mmappedFree(void *contents, void *data) {
	MMap *curr = (MMap *) contents;
	Process *p = (Process *) data;
	return (curr->pid == process_get_pid(p));
}

static Pair *deleteAllocList(void) {
	dprintf(1, "*** deleteAllocList\n");

	assert(!list_null(alloced));

	Process *p;
	Pair *found = NULL; // (pid, word)
	L4_Word_t *entry;

	// Second-chance algorithm
	for (;;) {
		found = (Pair *) list_unshift(alloced);

		p = process_lookup(found->fst);
		assert(p != NULL);
		entry = pagetableLookup(process_get_pagetable(p), found->snd);

		dprintf(3, "*** deleteAllocList: p=%d page=%p frame=%p\n",
				process_get_pid(p), (void *) found->snd, 
				(void *) (*entry & ADDRESS_MASK));

		//if (((*entry & REF_MASK) == 0) && (found->snd < 0x70000000)) {
		if ((*entry & REF_MASK) == 0) {
			// Not been referenced, this is the frame to swap
			break;
		} else {
			// Been referenced: clear refbit, unmap to give it a chance
			// of being reset again, and move to back
			*entry &= ~REF_MASK;
			unmapPage(process_get_sid(p), found->snd);
			list_push(alloced, found);
		}
	}

	return found;
}

static int memoryUsage(void) {
	return FRAME_ALLOC_LIMIT - allocLimit;
}

static ElfloadRequest *allocElfload(char *path, pid_t caller, pid_t child) {
	ElfloadRequest *er = (ElfloadRequest *) malloc(sizeof(ElfloadRequest));

	strncpy(er->path, path, MAX_FILE_NAME);
	er->parent = caller;
	er->child = child;

	return er;
}

static MMap *allocMMap(pid_t pid, L4_Word_t memAddr, size_t size, char *path) {
	MMap *mmap = (MMap *) malloc(sizeof(MMap));

	mmap->pid = pid;
	mmap->memAddr = memAddr;
	mmap->size = size;
	strncpy(mmap->path, path, MAX_FILE_NAME);

	return mmap;
}

static L4_Word_t pagerSwapslotAlloc(Process *p) {
	L4_Word_t diskAddr = swapslot_alloc(defaultSwapfile);

	if (diskAddr == ADDRESS_NONE) {
		dprintf(0, "!!! pagerSwapslotAlloc: none available\n");
		return ADDRESS_NONE;
	} else {
		list_push(swapped, pair_alloc(process_get_pid(p), diskAddr));
		return diskAddr;
	}
}

static int pagerSwapslotFree(void *contents, void *data) {
	Pair *curr = (Pair *) contents; // (pid, word)
	Pair *args = (Pair *) data;     // (pid, word)

	if ((curr->fst == args->fst) &&
			((curr->snd == args->snd) || (curr->snd == ADDRESS_ALL))) {
		swapslot_free(defaultSwapfile, curr->snd);
		return 1;
	} else {
		return 0;
	}
}

static void regionsFree(void *contents, void *data) {
	region_free((Region *) contents);
}

static int processDelete(L4_Word_t pid) {
	Process *p;
	Pair args; // (pid, word)

	p = process_lookup(pid);

	if (p == NULL) {
		// Already killed?
		return 1;
	}
	
	if (process_kill(p) != 0) {
		// Invalid process
		return (-1);
	}
	
	// change the state
	process_set_state(p, PS_STATE_ZOMBIE);

	// flush and close open files
	process_close_files(p);
	process_remove(p);

	// Free all resources
	args = PAIR(process_get_pid(p), ADDRESS_ALL);
	list_delete(alloced, framesFree, p);
	list_delete(swapped, pagerSwapslotFree, &args);
	list_delete(mmapped, mmappedFree, &args);
	pagetableFree(p);
	list_iterate(process_get_regions(p), regionsFree, NULL);

	// Wake all waiting processes
	process_wake_all(process_get_pid(p));

	// And done
	free(p);

	return 0;
}

static void panic2(int success) {
	assert(!"panic");
}

static Request *allocRequest(rtype_t type, void *data) {
	Request *req = (Request *) malloc(sizeof(Request));

	req->type = type;
	req->data = data;
	req->stage = 0;
	req->finish = panic2;

	return req;
}

static void pager2(Pagefault *fault) {
	dprintf(1, "*** %s\n", __FUNCTION__);
	rtype_t requestNeeded = pagefaultHandle(fault);

	if (requestNeeded == REQUEST_NONE) {
		dprintf(3, "*** %s: finished request\n", __FUNCTION__);
		fault->finish(fault);
	} else {
		dprintf(2, "*** %s: delay request\n", __FUNCTION__);
		queueRequest2(allocRequest(REQUEST_PAGEFAULT, fault));
	}
}

static char *wordAlign(char *s) {
	return (char *) round_up((L4_Word_t) s, sizeof(L4_Word_t));
}

static void printRequests2(void *contents, void *data) {
	Request *req = (Request *) contents;

	printf("type: %d, ", req->type);

	switch (req->type) {
		case REQUEST_NONE:
		case REQUEST_SWAPOUT:
		case REQUEST_SWAPIN:
		case REQUEST_ELFLOAD:
		case REQUEST_MMAP_READ:
			assert(0);
			break;

		case REQUEST_PAGEFAULT:
		case REQUEST_WRITE:
		case REQUEST_READ:
			break;

		default:
			assert(! __FUNCTION__);
	}

	printf("\n");
}

static void dequeueRequest2(void) {
	dprintf(1, "*** %s\n", __FUNCTION__);

	printf("WARNING make sure data is freed\n");
	free(list_unshift(requests));

	if (list_null(requests)) {
		dprintf(1, "*** %s: no more items\n", __FUNCTION__);
	} else {
		dprintf(1, "*** %s: running next\n", __FUNCTION__);
		if (verbose > 2) list_iterate(requests, printRequests2, NULL);
		startRequest2();
	}
}

static void queueRequest2(Request *req) {
	queueRequests(1, req);
}

static void queueRequests(int n, ...) {
	if (verbose > 2) list_iterate(requests, printRequests2, NULL);
	int startImmediately = list_null(requests);

	va_list va;
	va_start(va, n);

	for (int i = 0; i < n; i++) {
		list_push(requests, va_arg(va, Request *));
	}

	va_end(va);

	if (startImmediately) {
		startRequest2();
	}
}

static void finishSwapout2(int success) {
	assert(success);
	dprintf(1, "*** %s\n", __FUNCTION__);

	Request *req = (Request *) list_peek(requests);
	assert(req->type == REQUEST_WRITE);
	WriteRequest *writeReq = (WriteRequest *) req->data;

	pagerFrameFree(process_lookup(writeReq->pid), writeReq->src);

	free(writeReq);
	dequeueRequest2();
}

static void finishSwapin2(int success) {
	assert(success);
	dprintf(1, "*** %s\n", __FUNCTION__);

	Request *req = (Request *) list_peek(requests);
	assert(req->type == REQUEST_READ);
	ReadRequest *readReq = (ReadRequest *) req->data;

	free(readReq);
	dequeueRequest2();
}

static void continueRead(int vfsRval) {
	dprintf(2, "*** %s: vfsRval=%d\n", __FUNCTION__, vfsRval);
	Request *req = (Request *) list_peek(requests);
	assert(req->type == REQUEST_READ);
	ReadRequest *readReq = (ReadRequest *) req->data;
	stat_t *readStat;
	char *buf;

	switch (req->stage) {
		case STAGE_OPEN:
			dprintf(2, "*** %s: STAGE_OPEN\n", __FUNCTION__);
			if (vfsRval < 0) {
				req->finish(FALSE);
			} else {
				req->stage++;
				readReq->fd = vfsRval;
				readReq->offset = 0;
				strncpy(pager_buffer(sos_my_tid()), readReq->path, MAX_FILE_NAME);
				statNonblocking();
			}

			break;

		case STAGE_STAT:
			dprintf(2, "*** %s: STAGE_STAT\n", __FUNCTION__);
			assert(vfsRval == 0);

			buf = pager_buffer(sos_my_tid());
			readStat = (stat_t *) wordAlign(buf + strlen(buf) + 1);

			printf("%d vs %d\n", readReq->rights, readStat->st_fmode);

			/*
			if ((readReq->rights & readStat->st_fmode) == 0) {
				req->finish(FALSE);
			} else {
			*/
				dprintf(2, "*** %s: seek %p\n", __FUNCTION__, (void *) readReq->src);
				req->stage++;
				lseekNonblocking(readReq->fd, readReq->src, SEEK_SET);
			//}

			break;

		case STAGE_LSEEK:
			dprintf(2, "*** %s: STAGE_LSEEK\n", __FUNCTION__);
			assert(vfsRval == 0);
			req->stage++;
			readNonblocking(readReq->fd, min(IO_MAX_BUFFER, readReq->size));

			break;

		case STAGE_READ:
			dprintf(2, "*** %s: STAGE_READ\n", __FUNCTION__);
			assert(vfsRval >= 0);
			memcpy((void *) (readReq->dst + readReq->offset),
					pager_buffer(sos_my_tid()), vfsRval);
			readReq->offset += vfsRval;
			assert(readReq->offset <= readReq->size);

			if (readReq->offset == readReq->size) {
				if (readReq->alwaysOpen) {
					req->finish(TRUE);
				} else {
					req->stage++;
					closeNonblocking(readReq->fd);
				}
			} else {
				assert(vfsRval > 0);
				readNonblocking(readReq->fd,
						min(IO_MAX_BUFFER, readReq->size - readReq->offset));
			}

			break;

		case STAGE_CLOSE:
			dprintf(2, "*** %s: STAGE_CLOSE\n", __FUNCTION__);
			req->finish(TRUE);

			break;

		default:
			assert(! __FUNCTION__);
	}
}

static void startRead(void) {
	Request *req = (Request *) list_peek(requests);
	assert(req->type == REQUEST_READ);
	ReadRequest *readReq = (ReadRequest *) req->data;

	dprintf(1, "*** startRead: path=\"%s\"\n", __FUNCTION__, readReq->path);
	fildes_t currentFd = vfs_getfd(L4_ThreadNo(sos_my_tid()), readReq->path);

	if (currentFd != VFS_NIL_FILE) {
		assert(readReq->alwaysOpen);
		dprintf(2, "%s: open at %d\n", __FUNCTION__, currentFd);
		req->stage = STAGE_OPEN + 1;
		readReq->fd = currentFd;
		lseekNonblocking(readReq->fd, readReq->src, SEEK_SET);
	} else {
		dprintf(2, "%s: not open\n", __FUNCTION__);
		req->stage = STAGE_OPEN;
		strncpy(pager_buffer(sos_my_tid()), readReq->path, COPY_BUFSIZ);
		//openNonblocking(NULL, readReq->rights);
		openNonblocking(NULL, FM_READ);
	}
}

static void continueWrite(int vfsRval) {
	dprintf(2, "*** %s, vfsRval=%d\n", __FUNCTION__, vfsRval);
	Request *req = (Request *) list_peek(requests);
	assert(req->type == REQUEST_WRITE);
	WriteRequest *writeReq = (WriteRequest *) req->data;
	stat_t *writeStat;
	char *buf;
	size_t size;

	switch (req->stage) {
		case STAGE_OPEN:
			dprintf(2, "*** %s: STAGE_OPEN\n", __FUNCTION__);
			if (vfsRval < 0) {
				req->finish(FALSE);
			} else {
				req->stage++;
				writeReq->fd = vfsRval;
				dprintf(2, "*** %s: seek %p\n", __FUNCTION__, (void *) writeReq->dst);
				memcpy(pager_buffer(sos_my_tid()), writeReq->path, MAX_FILE_NAME);
				statNonblocking();
			}

			break;

		case STAGE_STAT:
			dprintf(2, "*** %s: STAGE_STAT\n", __FUNCTION__);
			assert(vfsRval == 0);

			buf = pager_buffer(sos_my_tid());
			writeStat = (stat_t *) wordAlign(buf + strlen(buf) + 1);

			printf("%d vs %d\n", writeReq->rights, writeStat->st_fmode);

			/*
			if ((writeReq->rights & writeStat->st_fmode) == 0) {
				req->finish(FALSE);
			} else {
			*/
				dprintf(2, "*** %s: seek %p\n", __FUNCTION__, (void *) writeReq->src);
				req->stage++;
				lseekNonblocking(writeReq->fd, writeReq->dst, SEEK_SET);
			//}

			break;

		case STAGE_LSEEK:
			dprintf(2, "*** %s: STAGE_LSEEK\n", __FUNCTION__);
			assert(vfsRval == 0);
			req->stage++;
			size = min(IO_MAX_BUFFER, writeReq->size);
			memcpy(pager_buffer(sos_my_tid()), (void *) writeReq->src, size);
			writeNonblocking(writeReq->fd, size);

			break;

		case STAGE_WRITE:
			dprintf(2, "*** %s: STAGE_WRITE\n", __FUNCTION__);
			assert(vfsRval >= 0);
			writeReq->offset += vfsRval;
			assert(writeReq->offset <= writeReq->size);

			if (writeReq->offset == writeReq->size) {
				if (writeReq->alwaysOpen) {
					req->finish(TRUE);
				} else {
					req->stage++;
					closeNonblocking(writeReq->fd);
				}
			} else {
				assert(vfsRval > 0);
				size = min(IO_MAX_BUFFER, writeReq->size - writeReq->offset);
				memcpy(pager_buffer(sos_my_tid()),
						(void *) (writeReq->src + writeReq->offset), size);
				writeNonblocking(writeReq->fd, size);
			}

			break;

		case STAGE_CLOSE:
			dprintf(2, "*** %s: STAGE_CLOSE\n", __FUNCTION__);
			req->finish(TRUE);

			break;
	}
}

static void startWrite(void) {
	Request *req = (Request *) list_peek(requests);
	assert(req->type == REQUEST_WRITE);
	WriteRequest *writeReq = (WriteRequest *) req->data;

	dprintf(1, "*** %s: path=\"%s\"\n", __FUNCTION__, writeReq->path);
	fildes_t currentFd = vfs_getfd(L4_ThreadNo(sos_my_tid()), writeReq->path);

	if (currentFd != VFS_NIL_FILE) {
		assert(writeReq->alwaysOpen);
		dprintf(2, "%s: open at %d\n", __FUNCTION__, currentFd);
		req->stage = STAGE_OPEN + 1;
		writeReq->fd = currentFd;
		lseekNonblocking(writeReq->fd, writeReq->dst, SEEK_SET);
	} else {
		dprintf(2, "%s: not open\n", __FUNCTION__);
		req->stage = STAGE_OPEN;
		strncpy(pager_buffer(sos_my_tid()), writeReq->path, COPY_BUFSIZ);
		openNonblocking(NULL, writeReq->rights);
	}
}

static void finishMMapRead(int success) {
	assert(success);
	dprintf(1, "*** %s\n", __FUNCTION__);

	Request *req = (Request *) list_peek(requests);
	assert(req->type == REQUEST_READ);
	ReadRequest *readReq = (ReadRequest *) req->data;

	free(readReq);
	dequeueRequest2();
}

static void startMMapRead(void) {
	dprintf(2, "*** startMMapRead\n");

	Process *p;
	Request *faultReq, *req;
	Pagefault *fault;
	ReadRequest *readReq;
	Pair args;
	L4_Word_t *entry;
	L4_Word_t frame;
	MMap *mmap;

	// The data for the mmap comes from the head of the queue
	faultReq = (Request *) list_peek(requests);
	assert(faultReq->type == REQUEST_PAGEFAULT);
	fault = (Pagefault *) faultReq->data;

	p = process_lookup(fault->pid);

	// Find the actual mmap
	args = PAIR(process_get_pid(p), fault->addr);
	mmap = (MMap *) list_find(mmapped, findMMap, &args);
	assert(mmap != NULL);

	// Reserve a temporary frame to read in to
	frame = frame_alloc(FA_PAGERALLOC);
	dprintf(2, "*** %s: temporary frame is %p\n", __FUNCTION__, (void *) frame);
	memset((void *) frame, 0x00, PAGESIZE);

	// Set up the read request
	entry = pagetableLookup(process_get_pagetable(p), fault->addr);

	dprintf(2, "*** %s: reading from %p in to %p\n", __FUNCTION__,
			(void *) (*entry & ADDRESS_MASK), (void *) frame);

	readReq = allocReadRequest(frame, *entry & ADDRESS_MASK,
			mmap->size, mmap->path);
	readReq->rights = mmap->rights;
	readReq->alwaysOpen = (strcmp(mmap->path, SWAPFILE_FN) == 0); // hack
	req = allocRequest(REQUEST_READ, readReq);
	req->finish = finishMMapRead;

	// Free the swapslot
	if (strcmp(mmap->path, SWAPFILE_FN) == 0) {
		args = PAIR(process_get_pid(p), *entry & ADDRESS_MASK);
		list_delete(swapped, pagerSwapslotFree, &args);
	}

	// Update page table with temporary frame etc
	dprintf(2, "*** %s: entry was %p, now ", __FUNCTION__, *entry);
	*entry &= ~(ADDRESS_MASK | MMAP_MASK);
	*entry |= frame | TEMP_MASK;
	dprintf(2, "%p\n", *entry);

	// Push the swapin to the head of the queue
	list_shift(requests, req);
	startRequest2();
}

static void startSwapin2(void) {
	dprintf(2, "*** %s\n", __FUNCTION__);

	Process *p;
	Request *faultReq, *req;
	Pagefault *fault;
	ReadRequest *readReq;
	Pair args;
	L4_Word_t *entry;
	L4_Word_t frame;

	// The data for the swapin comes from the head of the queue
	faultReq = (Request *) list_peek(requests);
	assert(faultReq->type == REQUEST_PAGEFAULT);
	fault = (Pagefault *) faultReq->data;

	p = process_lookup(fault->pid);

	// Reserve a temporary frame to read in to
	frame = frame_alloc(FA_PAGERALLOC);
	dprintf(2, "*** %s: temporary frame is %p\n", __FUNCTION__, (void *) frame);

	// Set up the read request
	entry = pagetableLookup(process_get_pagetable(p), fault->addr);

	dprintf(2, "*** %s: reading from %p in to %p\n", __FUNCTION__,
			(void *) (*entry & ADDRESS_MASK), (void *) frame);

	readReq = allocReadRequest(frame, *entry & ADDRESS_MASK,
			PAGESIZE, SWAPFILE_FN);
	readReq->alwaysOpen = TRUE;
	req = allocRequest(REQUEST_READ, readReq);
	req->finish = finishSwapin2;

	// Free the swapslot
	args = PAIR(process_get_pid(p), *entry & ADDRESS_MASK);
	list_delete(swapped, pagerSwapslotFree, &args);

	// Update page table with temporary frame etc
	dprintf(2, "*** %s: entry was %p, now ", __FUNCTION__, *entry);
	*entry &= ~(ADDRESS_MASK | SWAP_MASK);
	*entry |= frame | TEMP_MASK;
	dprintf(2, "%p\n", *entry);

	// Push the swapin to the head of the queue
	list_shift(requests, req);
	startRequest2();
}

static void startSwapout2(void) {
	dprintf(2, "*** %s\n", __FUNCTION__);

	Process *p;
	Request *req;
	WriteRequest *writeReq;
	Pair *swapout; // (pid, word)
	L4_Word_t *entry, diskAddr, frame;

	// Choose the next page to swap out
	swapout = deleteAllocList();
	p = process_lookup(swapout->fst);
	assert(p != NULL);

	entry = pagetableLookup(process_get_pagetable(p), swapout->snd);
	frame = *entry & ADDRESS_MASK;

	// Fix caches
	assert((swapout->snd & ~PAGEALIGN) == 0);
	prepareDataIn(p, swapout->snd);

	// The page is no longer backed
	unmapPage(process_get_sid(p), swapout->snd);

	// Set up where on disk to put the page
	diskAddr = pagerSwapslotAlloc(p);
	assert((diskAddr & ~PAGEALIGN) == 0);

	dprintf(1, "*** %s: addr=%p for pid=%d was %p, now %p\n", __FUNCTION__,
			(void *) swapout->snd, process_get_pid(p),
			(void *) frame, (void *) diskAddr);

	pair_free(swapout);

	*entry &= ~ADDRESS_MASK;
	*entry |= diskAddr | SWAP_MASK;

	// Make the request
	writeReq = allocWriteRequest(diskAddr, frame, PAGESIZE, SWAPFILE_FN);
	writeReq->pid = process_get_pid(p);
	writeReq->alwaysOpen = TRUE;
	req = allocRequest(REQUEST_WRITE, writeReq);

	req->finish = finishSwapout2;

	list_shift(requests, req);
	startRequest2();
}

static void startPagefault(void) {
	dprintf(2, "*** %s\n", __FUNCTION__);
	Request *req = (Request *) list_peek(requests);
	Pagefault *fault;
	rtype_t requestNeeded = pagefaultHandle((Pagefault *) req->data);

	switch (requestNeeded) {
		case REQUEST_NONE:
			// Can return now
			fault = (Pagefault *) req->data;
			fault->finish(fault);
			dequeueRequest2();
			break;

		case REQUEST_PAGEFAULT:
			// Need to swap something out
			startSwapout2();
			break;

		case REQUEST_SWAPIN:
			// Need to swap something in
			startSwapin2();
			break;

		case REQUEST_MMAP_READ:
			// Need to read something from disk
			startMMapRead();
			break;

		default:
			assert(!"default");
	}
}

static void startRequest2(void) {
	dprintf(1, "*** %s\n", __FUNCTION__);
	assert(!list_null(requests));

	switch (((Request *) list_peek(requests))->type) {
		case REQUEST_NONE:
			assert(!"REQUEST_NONE");
			break;

		case REQUEST_PAGEFAULT:
			startPagefault();
			break;

		case REQUEST_SWAPOUT:
			assert(!"REQUEST_SWAPOUT");
			break;

		case REQUEST_SWAPIN:
			assert(!"REQUEST_SWAPIN");
			break;

		case REQUEST_MMAP_READ:
			assert(!"REQUEST_MMAP_READ");
			break;

		case REQUEST_ELFLOAD:
			startElfload();
			break;

		case REQUEST_READ:
			startRead();
			break;

		case REQUEST_WRITE:
			startWrite();
			break;

		default:
			assert(!"default");
	}
}

static void vfsHandler(int vfsRval) {
	dprintf(2, "*** vfsHandler: vfsRval=%d\n", vfsRval);

	switch (((Request *) list_peek(requests))->type) {
		case REQUEST_READ:
			continueRead(vfsRval);
			break;

		case REQUEST_WRITE:
			continueWrite(vfsRval);
			break;

		default:
			assert(!"default");
	}
}

#endif


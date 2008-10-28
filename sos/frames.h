#ifndef _SOS_FRAMES_H_
#define _SOS_FRAMES_H_

#include "l4.h"

// Reasons for allocing a frame, used to hel find memory leaks
typedef enum {
	FA_STACK,
	FA_SWAPFILE,
	FA_MORECORE,
	FA_SWAPPIN,
	FA_MMAP_READ,
	FA_PAGETABLE1,
	FA_PAGETABLE2,
	FA_ALLOCFRAMES,
	FA_PAGERALLOC,
} alloc_codes_t;

// Initialise the frame table
void frame_init(L4_Word_t low, L4_Word_t frame);

// Allocate a frame
L4_Word_t frame_alloc(alloc_codes_t reason);

// Get the next frame to swap out
L4_Word_t frame_nextswap(void);

// Free a frame
void frame_free(L4_Word_t frame);

// Query how many frames are in use
int frames_allocated(void);

// Query how many frames are free
int frames_free(void);

// Query how many frames there are in total
int frames_total(void);

#endif // sos/frames.h

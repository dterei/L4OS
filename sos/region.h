#ifndef _SOS_REGION_H_
#define _SOS_REGION_H_

#include "swapfile.h"

#define REGION_READ 0x4
#define REGION_WRITE 0x2
#define REGION_EXECUTE 0x1

typedef enum {
	REGION_STACK,
	REGION_HEAP,
	REGION_OTHER,
	REGION_THREAD_INIT
} region_type;

typedef struct Region_t Region;

// Allocate new region with given properties
Region *region_alloc(region_type type, uintptr_t base,
		uintptr_t size, int rights, int dirmap);

// Free region
void region_free(Region *r);

// Getters
region_type region_get_type(Region *r);
uintptr_t region_get_base(Region *r);
unsigned int region_get_size(Region *r);
int region_get_rights(Region *r);
int region_map_directly(Region *r);
Swapfile *region_get_swapfile(Region *r);

// Setters
void region_set_rights(Region *r, int rights);
void region_set_size(Region *r, unsigned int size);

#endif // sos/region.h

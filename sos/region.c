#include <stdlib.h>

#include "region.h"

struct Region_t {
	region_type type;
	uintptr_t base;
	unsigned int size;
	int rights;
	int mapDirectly;
};

Region *region_alloc(region_type type, uintptr_t base,
		uintptr_t size, int rights, int dirmap) {
	Region *new = (Region*) malloc(sizeof(Region));

	new->type = type;
	new->base = base;
	new->size = size;
	new->rights = rights;
	new->mapDirectly = dirmap;

	return new;
}

void region_free(Region *r) {
	assert(r != NULL);
	free(r);
}

region_type region_get_type(Region *r) {
	assert(r != NULL);
	return r->type;
}

uintptr_t region_get_base(Region *r) {
	assert(r != NULL);
	return r->base;
}

unsigned int region_get_size(Region *r) {
	assert(r != NULL);
	return r->size;
}

int region_get_rights(Region *r) {
	assert(r != NULL);
	return r->rights;
}

int region_map_directly(Region *r) {
	assert(r != NULL);
	return r->mapDirectly;
}

void region_set_rights(Region *r, int rights) {
	assert(r != NULL);
	r->rights = rights;
}

void region_set_size(Region *r, unsigned int size) {
	assert(r != NULL);
	r->size = size;
}

int region_find(void *contents, void *data) {
	Region *r = (Region*) contents;
	assert(r != NULL);
	L4_Word_t addr = (L4_Word_t) data;

	return ((addr >= region_get_base(r)) &&
			(addr < region_get_base(r) + region_get_size(r)));
}

int region_find_type(void *contents, void *data) {
	return (region_get_type((Region *) contents) == (region_type) data);
}


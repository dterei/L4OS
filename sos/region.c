#include <stdlib.h>

#include "region.h"

struct Region_t {
	region_type type;
	uintptr_t base;
	unsigned int size;
	int rights;
	int mapDirectly;
	Swapfile *swapfile;
};

Region *region_alloc(region_type type, uintptr_t base,
		uintptr_t size, int rights, int dirmap) {
	Region *new = (Region*) malloc(sizeof(Region));

	new->type = type;
	new->base = base;
	new->size = size;
	new->rights = rights;
	new->mapDirectly = dirmap;
	new->swapfile = NULL;

	return new;
}

void region_free(Region *r) {
	free(r);
}

region_type region_get_type(Region *r) {
	return r->type;
}

uintptr_t region_get_base(Region *r) {
	return r->base;
}

unsigned int region_get_size(Region *r) {
	return r->size;
}

int region_get_rights(Region *r) {
	return r->rights;
}

Swapfile *region_get_swapfile(Region *r) {
	return r->swapfile;
}

int region_map_directly(Region *r) {
	return r->mapDirectly;
}

void region_set_rights(Region *r, int rights) {
	r->rights = rights;
}

void region_set_size(Region *r, unsigned int size) {
	r->size = size;
}

void region_set_swapfile(Region *r, Swapfile *sf) {
	r->swapfile = sf;
}


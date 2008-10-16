#include <stdlib.h>

#include "region.h"

struct Region_t {
	region_type type;
	uintptr_t base;
	unsigned int size;
	unsigned int filesize;
	int rights;
	int mapDirectly;
	Swapfile *elffile;
};

Region *region_alloc(region_type type, uintptr_t base,
		uintptr_t size, int rights, int dirmap) {
	Region *new = (Region*) malloc(sizeof(Region));

	new->type = type;
	new->base = base;
	new->size = size;
	new->rights = rights;
	new->mapDirectly = dirmap;
	new->elffile = NULL;

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

unsigned int region_get_filesize(Region *r) {
	return r->filesize;
}

int region_get_rights(Region *r) {
	return r->rights;
}

Swapfile *region_get_elffile(Region *r) {
	return r->elffile;
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

void region_set_filesize(Region *r, unsigned int filesize) {
	r->filesize = filesize;
}

void region_set_elffile(Region *r, Swapfile *sf) {
	r->elffile = sf;
}


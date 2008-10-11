#include <stdlib.h>

#include "pair.h"

Pair *pair_alloc(L4_Word_t fst, L4_Word_t snd) {
	Pair *p = (Pair*) malloc(sizeof(Pair));
	p->fst = fst;
	p->snd = snd;
	return p;
}

void pair_free(Pair *p) {
	free(p);
}


#ifndef _SOS_PAIR_H_
#define _SOS_PAIR_H_

#include "l4.h"

/**
 * A pair of L4_Words
 * Non-abstract for optimisation purposes.
 */

typedef struct Pair_t Pair;

struct Pair_t {
	L4_Word_t fst;
	L4_Word_t snd;
};

#define PAIR(fst, snd) ((Pair) {fst, snd})

Pair *pair_alloc(L4_Word_t fst, L4_Word_t snd);
void pair_free(Pair *p);

#endif // sos/pair.h


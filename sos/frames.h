#include <l4/types.h>

void frame_init(L4_Word_t low, L4_Word_t frame);
L4_Word_t frame_alloc(void);
void frame_free(L4_Word_t frame);

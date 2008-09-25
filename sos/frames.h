#ifndef _SOS_FRAMES_H_
#define _SOS_FRAMES_H_

#include "l4.h"

// Initialise the frame table
void frame_init(L4_Word_t low, L4_Word_t frame);

// Allocate a frame
L4_Word_t frame_alloc(void);

// Get the next frame to swap out
L4_Word_t frame_nextswap(void);

// Free a frame
void frame_free(L4_Word_t frame);

#endif // sos/frames.h

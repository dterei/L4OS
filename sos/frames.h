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

// Query how many frames are in use
int frames_allocated(void);

// Query how many frames are free
int frames_free(void);

// Query how many frames there are in total
int frames_total(void);

#endif // sos/frames.h

#ifndef _TIMER__H_
#define _TIMER__H_

// Init the timer
void utimer_init(void);

//
// Put the calling thread to sleep for microseconds.  This function is
// implemented on a temporary L4 debugger api and the timer.c 'timer' driver.
// You will need to reimplment this function when you finish your clock driver.
//
void utimer_sleep(uint32_t microseconds);

#endif

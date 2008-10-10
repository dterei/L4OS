#ifndef _SOS_LIST_H_
#define _SOS_LIST_H_

/**
 * A generic linked list that (most importantly) has the list_delete function
 * which is basically a list iteration function that deletes nodes that it is
 * told to.
 *
 * Note that list_pop is O(n).
 */

typedef struct List_t List;

// Create a new empty list
List *list_empty(void);

// Test if list is empty
int list_null(List *list);

// Peek at the head of the list
void *list_peek(List *list);

// Add a node to the start
void list_shift(List *list, void *contents);

// Remove and return the node at the start
void *list_unshift(List *list);

// Add a node to the end
void list_push(List *list, void *contents);

// Remove the node at the end
void *list_pop(List *list);

// Iterate over a list with a given function, and delete any
// nodes for which the function returns 1
void list_delete(List *list, int (*f)(void *contents, void *data), void *data);

#endif


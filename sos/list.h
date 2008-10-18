#ifndef _SOS_LIST_H_
#define _SOS_LIST_H_

/**
 * A generic linked list with some more interesting functions.
 *
 * Note that list_pop is O(n).
 */

typedef struct List_t List;

// Create a new empty list
List *list_empty(void);

// Destroy a list, returning the number of elements deleted
int list_destroy(List *list);

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

// Iterate over a list with a given function, and return as
// soon as the function returns 1
void *list_find(List *list, int (*f)(void *contents, void *data),
		void *data);

// Iterate over a list with a given function, and delete any
// nodes for which the function returns 1
void list_delete(List *list, int (*f)(void *contents, void *data),
		void *data);

// Iterate over a list with a given function, and delete first
// node for which the function returns 1
void list_delete_first(List *list, int (*f)(void *contents, void *data),
		void *data);

// Reduce a list to a single returned value
void *list_reduce(List *list, void *(*f)(void *contents, void *data),
		void *data);

// Iterate over a list
void list_iterate(List *list, void (*f)(void *contents, void *data),
		void *data);

#endif


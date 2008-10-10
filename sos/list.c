#include <assert.h>
#include <stdlib.h>

#include "list.h"

typedef struct Node_t Node;
struct Node_t {
	void *contents;
	Node *next;
};

struct List_t {
	Node *head;
	Node *last;
};

static Node *nodeAlloc(void *contents) {
	Node *node = (Node*) malloc(sizeof(Node));
	node->contents = contents;
	node->next = NULL;
	return node;
}

List *list_empty(void) {
	List *list = (List*) malloc(sizeof(List));
	list->head = NULL;
	list->last = NULL;
	return list;
}

int list_null(List *list) {
	if (list->head == NULL) {
		assert(list->last == NULL);
		return 1;
	} else {
		return 0;
	}
}

void *list_peek(List *list) {
	return list->head->contents;
}

void list_shift(List *list, void *contents) {
	Node *node = nodeAlloc(contents);
	node->next = list->head;
	list->head = node;

	if (list->last == NULL) {
		list->last = list->head;
	}
}

void *list_unshift(List *list) {
	assert(list->head != NULL);

	Node *tmp = list->head;
	void *contents = tmp->contents;

	list->head = list->head->next;

	if (list->head == NULL) {
		list->last = NULL;
	}

	free(tmp);
	return contents;
}

void list_push(List *list, void *contents) {
	Node *node = nodeAlloc(contents);

	if (list->head == NULL) {
		assert(list->last == NULL);
		list->last = node;
		list->head = node;
	} else {
		assert(list->last->next == NULL);
		list->last->next = node;
		list->last = node;
	}
}

void *list_pop(List *list) {
	assert(list->last != NULL);

	void *contents = list->last->contents;
	Node *prev = NULL;

	for (Node *tmp = list->head; tmp != list->last; tmp = tmp->next) {
		prev = tmp;
	}

	if (prev == NULL) {
		free(list->head);
		list->head = NULL;
		list->last = NULL;
	} else {
		prev->next = NULL;
		free(list->last);
		list->last = prev;
	}

	return contents;
}

void *list_find(List *list, int (*f)(void *contents, void *data),
		void *data) {
	for (Node *curr = list->head; curr != NULL; curr = curr->next) {
		if (f(curr->contents, data)) {
			return curr->contents;
		}
	}

	return NULL;
}

void list_delete(List *list, int (*f)(void *contents, void *data),
		void *data) {
	Node *curr, *prev, *tmp;

	curr = list->head;
	prev = NULL;

	while (curr != NULL) {
		if (f(curr->contents, data)) {
			tmp = curr;

			if (prev == NULL) {
				list->head = curr->next;
			} else {
				prev->next = curr->next;
			}

			curr = curr->next;
			free(tmp);
		} else {
			prev = curr;
			curr = curr->next;
		}
	}

	if (list->head == NULL) {
		list->last = NULL;
	}
}

void *list_reduce(List *list, void *(*f)(void *contents, void *data),
		void *data) {

	for (Node *curr = list->head; curr != NULL; curr = curr->next) {
		data = f(curr->contents, data);
	}

	return data;
}

void list_iterate(List *list, void (*f)(void *contents, void *data),
		void *data) {
	for (Node *curr = list->head; curr != NULL; curr = curr->next) {
		f(curr->contents, data);
	}
}


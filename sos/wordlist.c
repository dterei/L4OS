#include "l4.h"
#include "process.h"
#include "wordlist.h"

// Create a new empty wordlist
WordList *wordlist_empty(void) {
	WordList *list = (WordList*) malloc(sizeof(WordList));

	list->head = NULL;
	list->last = NULL;

	return list;
}

static WordNode *nodeAlloc(L4_Word_t word, pid_t pid) {
	WordNode *node = (WordNode*) malloc(sizeof(WordNode));

	node->word = word;
	node->pid = pid;
	node->next = NULL;

	return node;
}

// Add a node to the start
void wordlist_shift(WordList *list, L4_Word_t word, pid_t pid) {
	WordNode *node = nodeAlloc(word, pid);
	node->next = list->head;
	list->head = node;

	if (list->last == NULL) {
		list->last = list->head;
	}
}

// Remove the node at the start
void wordlist_unshift(WordList *list) {
	assert(list->head != NULL);

	WordNode *tmp = list->head;
	list->head = list->head->next;

	if (list->head == NULL) {
		list->last = NULL;
	}

	free(tmp);
}

// Add a node to the end
void wordlist_push(WordList *list, L4_Word_t word, pid_t pid) {
	WordNode *node = nodeAlloc(word, p);

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

// Remove the node at the end
void wordlist_pop(WordList *list) {
	assert(list->last != NULL);

	WordNode *prev = NULL;

	for (WordNode *tmp = list->head; tmp != list->last; tmp = tmp->next) {
		prev = tmp;
	}

	if (prev == NULL) {
		free(list->head);
		list->head = NULL;
		list->tail = NULL;
	} else {
		prev->next = list->last->next;
		free(list->last);
		list->last = prev;
	}
}


#ifndef _SOS_WORDLIST_H_
#define _SOS_WORDLIST_H_

/**
 * A note-so-abstract list of words associated with a process.
 *
 * Exists in order to decouple the linked list code from the pager.
 */

#include "l4.h"
#include "process.h"

typedef struct WordNode_t WordNode;
struct WordNode_t {
	L4_Word_t word;
	pid_t pid;
	WordNode *next;
};

typedef struct WordList_t WordList;
struct WordList_t {
	WordNode *head;
	WordNode *last;
};

// Create a new empty wordlist
WordList *wordlist_empty(void);

// Add a node to the start
void wordlist_shift(WordList *list, L4_Word_t word, pid_t pid);

// Remove the node at the start
void wordlist_unshift(WordList *list);

// Add a node to the end
void wordlist_push(WordList *list, L4_Word_t word, pid_t pid);

// Remove the node at the end
void wordlist_pop(WordList *list);

#endif


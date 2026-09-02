/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/*
 * list.h - List-type data structures
 */

#ifndef _AV6_LIST_H_
#define _AV6_LIST_H_

#include "sys/types.h"

/*
 * LIST - intrusive doubly-linked lists.
 *
 * "Intrusive" = the link pointers live inside the element via a
 * LIST_ENTRY field.
 */

#define LIST_HEAD(name, type)       struct name {struct type *lh_first;}
#define LIST_ENTRY(type)            struct {struct type *le_next; struct type **le_prev;}
#define LIST_HEAD_INIT(head)        {NULL}
#define LIST_INIT(head)             do {(head)->lh_first = NULL;} while (false)

#define LIST_FIRST(head)            ((head)->lh_first)
#define LIST_NEXT(elm, field)       ((elm)->field.le_next)
#define LIST_EMPTY(head)            ((head)->lh_first == NULL)

#define LIST_INSERT_HEAD(head, elm, field)                              \
    do {                                                                \
        if (((elm)->field.le_next = (head)->lh_first) != NULL) {        \
            (head)->lh_first->field.le_prev = &(elm)->field.le_next;    \
        }                                                               \
        (head)->lh_first = (elm);                                       \
        (elm)->field.le_prev = &(head)->lh_first;                       \
    } while (false)

#define LIST_REMOVE(elm, field)                                         \
    do {                                                                \
        if ((elm)->field.le_next != NULL) {                             \
            (elm)->field.le_next->field.le_prev = (elm)->field.le_prev; \
        }                                                               \
        *(elm)->field.le_prev = (elm)->field.le_next;                   \
    } while (false)

#define LIST_FOREACH(var, head, field)                                  \
    for ((var) = LIST_FIRST(head); (var) != NULL;                       \
        (var) = LIST_NEXT(var, field))

/* safe against freeing/removing (var) inside the loop body */
#define LIST_FOREACH_SAFE(var, head, field, tvar)                       \
    for ((var) = LIST_FIRST(head);                                      \
        (var) != NULL && ((tvar) = LIST_NEXT(var, field), 1);           \
        (var) = (tvar))

/*
 * TAILQ - doubly-linked FIFO queue: O(1) insert-at-tail and O(1) remove
 *
 * Same intrusive idea as LIST, but the head also holds tqh_last -- a pointer to
 * the last element's tqe_next slot (or to tqh_first when empty) -- so appending
 * at the tail is O(1). Used for the scheduler run queue: enqueue at the tail,
 * dequeue from the head = FIFO round-robin.
 */
#define TAILQ_HEAD(name, type)      struct name {struct type *tqh_first; struct type **tqh_last;}
#define TAILQ_ENTRY(type)           struct {struct type *tqe_next; struct type **tqe_prev;}
#define TAILQ_INIT(head)                                            \
    do {                                                            \
        (head)->tqh_first = NULL;                                   \
        (head)->tqh_last = &(head)->tqh_first;                      \
    } while (false)

#define TAILQ_FIRST(head)           ((head)->tqh_first)
#define TAILQ_EMPTY(head)           ((head)->tqh_first == NULL)

#define TAILQ_INSERT_TAIL(head, elm, field)                         \
    do {                                                            \
        (elm)->field.tqe_next = NULL;                               \
        (elm)->field.tqe_prev = (head)->tqh_last;                   \
        *(head)->tqh_last = (elm);                                  \
        (head)->tqh_last = &(elm)->field.tqe_next;                  \
    } while (false)

#define TAILQ_REMOVE(head, elm, field)                                         \
    do {                                                                        \
        if ((elm)->field.tqe_next != NULL) {                                    \
            (elm)->field.tqe_next->field.tqe_prev = (elm)->field.tqe_prev;      \
        } else {                                                                \
            (head)->tqh_last = (elm)->field.tqe_prev;                           \
        }                                                                       \
        *(elm)->field.tqe_prev = (elm)->field.tqe_next;                         \
    } while (false)

#endif  /* _AV6_LIST_H_ */

/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
 * Copyright IBM Corporation, 2010
 *  Contributor: Aneesh Kumar K.v  <aneesh.kumar@linux.vnet.ibm.com>
 *
 *
 * This software is a server that implements the NFS protocol.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * ---------------------------------------
 *
 *
 */

#ifndef RGW_LIST_H
#define RGW_LIST_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct rgw_list_head {
	struct rgw_list_head *next;
	struct rgw_list_head *prev;
};

/*
 * @brief Compare routine that is used by rgw_list_insert_sorted
 *
 * This routine can be defined by the calling function
 * to enable a sorted insert
 *
 * @param struct rgw_list_head *: The first element to compare
 * @param struct rgw_list_head *: The second element to compare
 *
 * @return  negative if the 1st element should appear before the 2nd element
 *          0 if the 1st and 2nd element are equal
 *          positive if the 1st element should appear after the 2nd element
 */
typedef int (*rgw_list_compare)(struct rgw_list_head *, struct rgw_list_head *);

/**
 * @brief List head initialization
 *
 * These macros and functions are only for list heads,
 * not nodes.  The head always points to something and
 * if the list is empty, it points to itself.
 */

#define GLIST_HEAD_INIT(name) { &(name), &(name) }

#define GLIST_HEAD(name) struct rgw_list_head name = GLIST_HEAD_INIT(name)

static inline void rgw_list_init(struct rgw_list_head *head)
{ /* XXX rgw_list_init? */
	head->next = head;
	head->prev = head;
}

/* Add the new element between left and right */
static inline void __rgw_list_add(struct rgw_list_head *left,
			       struct rgw_list_head *right, struct rgw_list_head *elt)
{
	elt->prev = left;
	elt->next = right;
	left->next = elt;
	right->prev = elt;
}

static inline void rgw_list_add_tail(struct rgw_list_head *head,
				  struct rgw_list_head *elt)
{
	__rgw_list_add(head->prev, head, elt);
}

/* add after the specified entry*/
static inline void rgw_list_add(struct rgw_list_head *head, struct rgw_list_head *elt)
{
	__rgw_list_add(head, head->next, elt);
}

static inline void rgw_list_del(struct rgw_list_head *node)
{
	struct rgw_list_head *left = node->prev;
	struct rgw_list_head *right = node->next;

	if (left != NULL)
		left->next = right;
	if (right != NULL)
		right->prev = left;
	node->next = NULL;
	node->prev = NULL;
}

/*
 * @brief Move the node to list tail
 *
 * This function would help to move node to the tail of list.
 * Calling `rgw_list_del` and `rgw_list_add_tail` would do extra
 * operations when the node is already tail.
 * This function does pre-check for the situation that node is
 * already tail so that would reduce some operations.
 *
 * @param struct rgw_list_head *head: The first element of list
 * @param struct rgw_list_head *node: The element that want to move to tail
 *
 * @return Nothing; The operation is done w/o return value.
 */
static inline void rgw_list_move_tail(struct rgw_list_head *head,
				   struct rgw_list_head *node)
{
	/* already tail */
	if (node == head->prev)
		return;

	rgw_list_del(node);
	__rgw_list_add(head->prev, head, node);
}

/**
 * @brief Test if the list in this head is empty
 */
static inline int rgw_list_empty(struct rgw_list_head *head)
{
	return head->next == head;
}

/**
 * @brief Test if this node is not on a list.
 *
 * NOT to be confused with rgw_list_empty which is just
 * for heads.  We poison with NULL for disconnected nodes.
 */

static inline int rgw_list_null(struct rgw_list_head *head)
{
	return (head->next == NULL) && (head->prev == NULL);
}

static inline void rgw_list_add_list_tail(struct rgw_list_head *list,
				       struct rgw_list_head *elt)
{
	struct rgw_list_head *first = elt->next;
	struct rgw_list_head *last = elt->prev;

	if (rgw_list_empty(elt)) {
		/* nothing to add */
		return;
	}

	first->prev = list->prev;
	list->prev->next = first;

	last->next = list;
	list->prev = last;
}

/* Move all of src onto the tail of tgt.  Clears src. */
static inline void rgw_list_splice_tail(struct rgw_list_head *tgt,
				     struct rgw_list_head *src)
{
	if (rgw_list_empty(src))
		return;

	src->next->prev = tgt->prev;
	tgt->prev->next = src->next;
	src->prev->next = tgt;
	tgt->prev = src->prev;

	rgw_list_init(src);
}

static inline void rgw_list_swap_lists(struct rgw_list_head *l1,
				    struct rgw_list_head *l2)
{
	struct rgw_list_head temp;

	if (rgw_list_empty(l1)) {
		/* l1 was empty, so splice tail will accomplish swap. */
		rgw_list_splice_tail(l1, l2);
		return;
	}

	if (rgw_list_empty(l2)) {
		/* l2 was empty, so reverse splice tail will accomplish swap. */
		rgw_list_splice_tail(l2, l1);
		return;
	}

	/* Both lists are non-empty */

	/* First swap the list pointers. */
	temp = *l1;
	*l1 = *l2;
	*l2 = temp;

	/* Then fixup first entry in each list prev to point to it's new head */
	l1->next->prev = l1;
	l2->next->prev = l2;

	/* And fixup the last entry in each list next to point to it's new head
	 */
	l1->prev->next = l1;
	l2->prev->next = l2;
}

/**
 * @brief Split list list1 into list2 at element.
 *
 * @note list2 is expected to be empty. list1 is expected to be non-empty (i.e.
 * element is NOT list1).
 *
 * @param[in,out] list1    Source list.
 * @param[in,out] list2    Destination list.
 * @param[in,out] element  List element to become first element in list2.
 *
 */
static inline void rgw_list_split(struct rgw_list_head *list1,
			       struct rgw_list_head *list2,
			       struct rgw_list_head *element)
{
	/* Set up list2 to contain element to the end. */
	list2->next = element;
	list2->prev = list1->prev;

	/* Fixup the last element of list1 to be the last element of list2, even
	 * if it was element.
	 */
	list2->prev->next = list2;

	/* Now fixup list1 even if element was first element of list1. */
	list1->prev = element->prev;

	/* Now fixup prev of element, even if element was first element of
	 * list1.
	 */
	element->prev->next = list1;

	/* Now fixup element */
	element->prev = list2;
}

#define rgw_list_for_each(node, head) \
	for (node = (head)->next; node != head; node = node->next)

#define rgw_list_for_each_next(start, node, head) \
	for (node = (start)->next; node != head; node = node->next)

static inline size_t rgw_list_length(struct rgw_list_head *head)
{
	size_t length = 0;
	struct rgw_list_head *dummy = NULL;

	rgw_list_for_each(dummy, head) {
		++length;
	}
	return length;
}

#define container_of(addr, type, member)                            \
	({                                                          \
		const typeof(((type *)0)->member) *__mptr = (addr); \
		(type *)((char *)__mptr - offsetof(type, member));  \
	})

#define rgw_list_first_entry(head, type, member)                              \
	((head)->next != (head) ? container_of((head)->next, type, member) \
				: NULL)

#define rgw_list_last_entry(head, type, member)                               \
	((head)->prev != (head) ? container_of((head)->prev, type, member) \
				: NULL)

#define rgw_list_entry(node, type, member) container_of(node, type, member)

#define rgw_list_for_each_safe(node, noden, head)                        \
	for (node = (head)->next, noden = node->next; node != (head); \
	     node = noden, noden = node->next)

#define rgw_list_for_each_next_safe(start, node, noden, head)             \
	for (node = (start)->next, noden = node->next; node != (head); \
	     node = noden, noden = node->next)

/* Return the next entry in the list after node if any. */
#define rgw_list_next_entry(head, type, member, node)                         \
	((node)->next != (head) ? container_of((node)->next, type, member) \
				: NULL)

/* Return the previous entry in the list after node if any. */
#define rgw_list_prev_entry(head, type, member, node)                         \
	((node)->prev != (head) ? container_of((node)->prev, type, member) \
				: NULL)

static inline void rgw_list_insert_sorted(struct rgw_list_head *head,
				       struct rgw_list_head *elt,
				       rgw_list_compare compare)
{
	struct rgw_list_head *next = NULL;

	if (rgw_list_empty(head)) {
		rgw_list_add_tail(head, elt);
		return;
	}
	rgw_list_for_each(next, head) {
		if (compare(next, elt) > 0)
			break;
	}

	__rgw_list_add(next->prev, next, elt);
}

#ifdef __cplusplus
}
#endif

#endif /* _GANESHA_LIST_H */

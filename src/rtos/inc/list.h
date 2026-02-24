//-----------------------------------------------------------------------------
//                 Copyright(c) 2016-2019 Innogrit Corporation
//                             All Rights reserved.
//
// The confidential and proprietary information contained in this file may
// only be used by a person authorized under and to the extent permitted
// by a subsisting licensing agreement from Innogrit Corporation.
// Dissemination of this information or reproduction of this material
// is strictly forbidden unless prior written permission is obtained
// from Innogrit Corporation.
//-----------------------------------------------------------------------------

#pragma once

/* Double linked lists. Same api as the linux kernel */
struct list_head {
	struct list_head *next, *prev;
};

static inline int list_empty(struct list_head *head)
{
    return head->next == head;
}

static inline void __list_add(struct list_head *elem,
			      struct list_head *prev, struct list_head *next)
{
    next->prev = elem;
    elem->next = next;
    prev->next = elem;
    elem->prev = prev;
}

static inline void __list_del(struct list_head *prev, struct list_head *next)
{
    prev->next = next;
    next->prev = prev;
}

#define LIST_HEAD(name) struct list_head name = { &name, &name }

/* add at the head */
#define list_add(elem, head) \
   __list_add((struct list_head *)elem, head, (head)->next);

/* add at tail */
#define list_add_tail(elem, head) \
   __list_add((struct list_head *)elem, (head)->prev, head)

/* delete */
#define list_del(elem) __list_del(((struct list_head *)elem)->prev,  \
				  ((struct list_head *)elem)->next)

// Traverse each entry in the list, the entry must NOT be modified (e.g. deleted) during this
#define list_for_each(elem, head) \
   for (elem = (void *)(head)->next; elem != (void *)(head); elem = elem->next)

// Traverse each entry in the list safely, the entry may be modified
#define list_for_each_safe(elem, elem1, head) \
   for (elem = (void *)(head)->next, elem1 = elem->next; elem != (void *)(head); \
		elem = elem1, elem1 = elem->next)

// Traverse each entry in the list in reverse order, the entry must NOT be modified (e.g. deleted) during this
#define list_for_each_prev(elem, head) \
   for (elem = (void *)(head)->prev; elem != (void *)(head); elem = elem->prev)

/*!
 * @brief Find the first structure in the link list
 *
 * @param list	double link list head
 * @param struct_name	Data structure type
 * @param member_name	the name in the structure definition that used to form a link list
 *
 * @return pointer to the first structure
 */
#define list_first_entry(ptr, type, member) \
		container_of((ptr)->next, type, member)

/*!
 * @brief Find the last structure in the link list
 *
 * @param head	double link list head
 * @param struct_name	Data structure type
 * @param member_name	the name in the structure definition that used to form a link list
 *
 * @return pointer to the first structure
 */
#define list_last_entry(ptr, type, member) \
		container_of((ptr)->prev, type, member)

#define LIST_HEAD_INIT(name) { &(name), &(name) }

/*!
 * @brief Init double link list
 *
 * @param head	double link list head
 *
 * @return not used
 */
static void inline INIT_LIST_HEAD(struct list_head *list)
{
	list->next = list;
	list->prev = list;
}

/*!
 * @brief Clear and reinit the link list
 *
 * @param list	double link list head
 *
 * @return not used
 */
static __attribute__((always_inline)) void inline list_del_init(struct list_head *list)
{
	list_del(list);
	INIT_LIST_HEAD(list);
}

static inline void __list_splice(struct list_head *list,
		struct list_head *prev,
		struct list_head *next)
{
	struct list_head *first = list->next;
	struct list_head *last = list->prev;

	first->prev = prev;
	prev->next = first;

	last->next = next;
	next->prev = last;
}

/*!
 * @brief Add whole list(1) into another list(2) (to the beginning)
 *
 * @param list1	double link list head 1
 * @param list1	double link list head 2
 *
 * @return not used
 */
static inline void list_splice(struct list_head *list,
		struct list_head *head)
{
	__list_splice(list, head, head->next);
}

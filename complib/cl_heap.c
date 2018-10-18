/*
 * Copyright (c) 2009-2015 ZIH, TU Dresden, Federal Republic of Germany. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

/*
 * Abstract:
 *	This file contains a d-ary heap implementation.
 *	The default is a minimum heap, however the caller can overwrite
 *	the compare function for the keys of the heap.
 *
 */

#if HAVE_CONFIG_H
#include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <stdlib.h>
#include <string.h>
#include <complib/cl_heap.h>

typedef struct _cl_heap_elem {
	uint64_t key;
	void *context;
} cl_heap_elem_t;

static int compare_keys(IN const void *p_key_1, IN const void *p_key_2)
{
	uint64_t key1, key2;

	CL_ASSERT(p_key_1);
	CL_ASSERT(p_key_2);

	key1 = *((uint64_t *) p_key_1);
	key2 = *((uint64_t *) p_key_2);

	return ((key1 < key2) ? -1 : ((key1 > key2) ? 1 : 0));
}

void cl_heap_construct(IN cl_heap_t * const p_heap)
{
	CL_ASSERT(p_heap);

	memset(p_heap, 0, sizeof(cl_heap_t));

	p_heap->state = CL_UNINITIALIZED;
}

cl_status_t cl_heap_init(IN cl_heap_t * const p_heap, IN const size_t max_size,
			 IN const uint8_t d,
			 IN cl_pfn_heap_apply_index_update_t pfn_index_update,
			 IN cl_pfn_heap_compare_keys_t pfn_compare OPTIONAL)
{
	CL_ASSERT(p_heap);

	if (!cl_is_state_valid(p_heap->state))
		cl_heap_construct(p_heap);

	if (max_size <= 0 || !d || !pfn_index_update)
		return (CL_INVALID_PARAMETER);

	if (cl_is_heap_inited(p_heap))
		cl_heap_destroy(p_heap);

	p_heap->branching_factor = d;
	p_heap->size = 0;
	p_heap->capacity = max_size;
	p_heap->pfn_index_update = pfn_index_update;

	if (pfn_compare)
		p_heap->pfn_compare = pfn_compare;
	else
		p_heap->pfn_compare = &compare_keys;

	p_heap->element_array =
	    (cl_heap_elem_t *) malloc(max_size * sizeof(cl_heap_elem_t));
	if (!p_heap->element_array)
		return (CL_INSUFFICIENT_MEMORY);
	memset(p_heap->element_array, 0, max_size * sizeof(cl_heap_elem_t));

	p_heap->state = CL_INITIALIZED;

	return (CL_SUCCESS);
}

void cl_heap_destroy(IN cl_heap_t * const p_heap)
{
	CL_ASSERT(p_heap);
	CL_ASSERT(cl_is_state_valid(p_heap->state));

	if (p_heap->element_array)
		free(p_heap->element_array);

	cl_heap_construct(p_heap);
}

cl_status_t cl_heap_resize(IN cl_heap_t * const p_heap,
			   IN const size_t new_size)
{
	cl_heap_elem_t *realloc_element_array = NULL;

	CL_ASSERT(p_heap);
	CL_ASSERT(cl_is_heap_inited(p_heap));

	if (new_size <= 0 || new_size < p_heap->size)
		return (CL_INVALID_PARAMETER);

	if (new_size == p_heap->capacity)
		return (CL_SUCCESS);

	realloc_element_array =
	    (cl_heap_elem_t *) realloc(p_heap->element_array,
				       new_size * sizeof(cl_heap_elem_t));
	if (!realloc_element_array)
		return (CL_INSUFFICIENT_MEMORY);

	p_heap->element_array = realloc_element_array;
	memset(p_heap->element_array + p_heap->size, 0,
	       (new_size - p_heap->size) * sizeof(cl_heap_elem_t));

	p_heap->capacity = new_size;

	return (CL_SUCCESS);
}

static void heap_down(IN cl_heap_t * const p_heap, IN const size_t index)
{
	int64_t first_child, swap_child, child, parent, d;
	cl_heap_elem_t tmp = p_heap->element_array[index];
	boolean_t swapped = FALSE;

	d = (int64_t) p_heap->branching_factor;
	parent = index;

	while (parent * d + 1 < p_heap->size) {
		swap_child = first_child = parent * d + 1;
		/* find the min (or max) child among the children */
		for (child = first_child + 1;
		     child < first_child + d && child < p_heap->size; child++)
			if (p_heap->
			    pfn_compare(&(p_heap->element_array[child].key),
					&(p_heap->element_array[swap_child].
					  key)) <= 0)
				swap_child = child;

		/* exchange parent and one child */
		if (p_heap->
		    pfn_compare(&(tmp.key),
				&(p_heap->element_array[swap_child].key)) > 0) {
			p_heap->element_array[parent] =
			    p_heap->element_array[swap_child];
			p_heap->pfn_index_update(p_heap->element_array[parent].
						 context, parent);
			parent = swap_child;
			swapped = TRUE;
		} else
			break;
	}

	/* move the original element down in the heap */
	if (swapped) {
		p_heap->element_array[parent] = tmp;
		p_heap->pfn_index_update(p_heap->element_array[parent].context,
					 parent);
	}
}

static void heap_up(IN cl_heap_t * const p_heap, IN const size_t index)
{
	int64_t parent, child, swap_child = 0, d;
	boolean_t swapped = FALSE;

	if (!index)
		return;

	cl_heap_elem_t tmp = p_heap->element_array[index];

	d = (int64_t) p_heap->branching_factor;
	parent = index;
	do {
		child = parent;
		parent = (child - 1) / d;
		if (p_heap->
		    pfn_compare(&(tmp.key),
				&(p_heap->element_array[parent].key)) < 0) {
			/* move the parent down and notify the user context about the change */
			p_heap->element_array[child] =
			    p_heap->element_array[parent];
			p_heap->pfn_index_update(p_heap->element_array[child].
						 context, child);
			swap_child = parent;
			swapped = TRUE;
		} else
			break;
	} while (parent > 0);

	/* write original heap element to the correct position */
	if (swapped) {
		p_heap->element_array[swap_child] = tmp;
		p_heap->pfn_index_update(p_heap->element_array[swap_child].
					 context, swap_child);
	}
}

cl_status_t cl_heap_modify_key(IN cl_heap_t * const p_heap,
			       IN const uint64_t key, IN const size_t index)
{
	uint64_t old_key;
	int compare_result;

	CL_ASSERT(p_heap);
	CL_ASSERT(cl_is_heap_inited(p_heap));

	if (index < 0 || index >= p_heap->size)
		return (CL_INVALID_PARAMETER);

	old_key = p_heap->element_array[index].key;
	p_heap->element_array[index].key = key;

	compare_result = p_heap->pfn_compare(&key, &old_key);
	if (compare_result < 0)
		heap_up(p_heap, index);
	else if (compare_result > 0)
		heap_down(p_heap, index);

	return (CL_SUCCESS);
}

cl_status_t cl_heap_insert(IN cl_heap_t * const p_heap, IN const uint64_t key,
			   IN const void *const context)
{
	CL_ASSERT(p_heap);
	CL_ASSERT(cl_is_heap_inited(p_heap));

	if (!context)
		return (CL_INVALID_PARAMETER);

	if (p_heap->size == p_heap->capacity)
		return (CL_INSUFFICIENT_RESOURCES);

	p_heap->element_array[p_heap->size].key = key;
	p_heap->element_array[p_heap->size].context = (void *) context;
	p_heap->pfn_index_update(context, p_heap->size);

	heap_up(p_heap, p_heap->size++);

	return (CL_SUCCESS);
}

void *cl_heap_delete(IN cl_heap_t * const p_heap, IN const size_t index)
{
	int64_t parent, d;
	int compare_result;
	cl_heap_elem_t tmp;

	CL_ASSERT(p_heap);
	CL_ASSERT(cl_is_heap_inited(p_heap));

	if (!p_heap->size)
		return NULL;
	if (index < 0 || index >= p_heap->size)
		return NULL;
	if (p_heap->size == 1)
		return p_heap->element_array[--(p_heap->size)].context;

	tmp = p_heap->element_array[--(p_heap->size)];

	p_heap->element_array[p_heap->size] = p_heap->element_array[index];
	p_heap->pfn_index_update(p_heap->element_array[p_heap->size].context,
				 p_heap->size);

	p_heap->element_array[index] = tmp;
	p_heap->pfn_index_update(p_heap->element_array[index].context, index);

	if (0 == index)
		heap_down(p_heap, index);
	else {
		d = (int64_t) p_heap->branching_factor;
		parent = (index - 1) / d;
		compare_result =
		    p_heap->pfn_compare(&(p_heap->element_array[parent].key),
					&(p_heap->element_array[index].key));

		/* if the parent is smaller than tmp (which we moved within
		 * the head), then we have to attempt a heap_down
		 */
		if (compare_result < 0)
			heap_down(p_heap, index);
		/* otherwise heap_up is needed to restore the heap property */
		else if (compare_result > 0)
			heap_up(p_heap, index);
	}

	return p_heap->element_array[p_heap->size].context;
}

void *cl_heap_extract_root(IN cl_heap_t * const p_heap)
{
	return cl_heap_delete(p_heap, 0);
}

boolean_t cl_is_stored_in_heap(IN const cl_heap_t * const p_heap,
			       IN const void *const ctx, IN const size_t index)
{
	CL_ASSERT(p_heap);
	CL_ASSERT(cl_is_heap_inited(p_heap));

	return ((index < 0 || index >= p_heap->size ||
		 p_heap->element_array[index].context != ctx) ? FALSE : TRUE);
}

boolean_t cl_verify_heap_property(IN const cl_heap_t * const p_heap)
{
	int64_t first_child, child, parent, d;

	CL_ASSERT(p_heap);
	CL_ASSERT(cl_is_heap_inited(p_heap));

	d = (int64_t) p_heap->branching_factor;
	parent = 0;

	while (parent < p_heap->size) {
		first_child = parent * d + 1;
		/* find the min (or max) child among the children */
		for (child = first_child;
		     child < first_child + d && child < p_heap->size; child++)
			if (p_heap->
			    pfn_compare(&(p_heap->element_array[parent].key),
					&(p_heap->element_array[child].key)) >
			    0)
				return FALSE;
		parent++;
	}

	return TRUE;
}

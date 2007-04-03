/*
 * Copyright (c) 2004, 2005 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2005 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
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
 *	Definitions of Data-Structures for memory allocation tracking functions.
 *
 * Environment:
 *	All
 *
 * $Revision: 1.3 $
 */


#ifndef _CL_MEMTRACK_H_
#define _CL_MEMTRACK_H_

#include <complib/cl_types.h>
#include <complib/cl_memory.h>
#include <complib/cl_debug.h>
#include <complib/cl_qlist.h>
#include <complib/cl_spinlock.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else /* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif /* __cplusplus */

BEGIN_C_DECLS

/* Structure to track memory allocations. */
typedef struct _cl_mem_tracker
{
	/* List for tracking memory allocations. */
	cl_qlist_t		alloc_list;

	/* Lock for synchronization. */
	cl_spinlock_t	lock;

	/* List to manage free headers. */
	cl_qlist_t		free_hdr_list;

} cl_mem_tracker_t __attribute__((deprecated));

#define FILE_NAME_LENGTH	64

/* Header for all memory allocations. */
typedef struct _cl_malloc_hdr
{
	cl_list_item_t		list_item;
	void				*p_mem;
	char				file_name[FILE_NAME_LENGTH];
	int32_t				line_num;

} cl_malloc_hdr_t __attribute__((deprecated));

extern cl_mem_tracker_t		*gp_mem_tracker;

END_C_DECLS

#endif	/* _CL_MEMTRACK_H_ */

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
 *	Declaration of the asynchronous processing module.
 *
 * Environment:
 *	All
 *
 * $Revision: 1.3 $
 */

#ifndef _CL_ASYNC_PROC_H_
#define _CL_ASYNC_PROC_H_

#include <complib/cl_qlist.h>
#include <complib/cl_qpool.h>
#include <complib/cl_threadpool.h>
#include <complib/cl_spinlock.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else /* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif /* __cplusplus */

BEGIN_C_DECLS

/****h* Component Library/Asynchronous Processor
* NAME
*	Asynchronous Processor
*
* DESCRIPTION
*	The asynchronous processor provides threads for executing queued callbacks.
*
*	The threads in the asynchronous processor wait for callbacks to be queued.
*
*	The asynchronous processor functions operate on a cl_async_proc_t structure
*	which should be treated as opaque and manipulated only through the provided
*	functions.
*
* SEE ALSO
*	Structures:
*		cl_async_proc_t, cl_async_proc_item_t
*
*	Initialization:
*		cl_async_proc_construct, cl_async_proc_init, cl_async_proc_destroy
*
*	Manipulation:
*		cl_async_proc_queue
*********/

/****s* Component Library: Asynchronous Processor/cl_async_proc_t
* NAME
*	cl_async_proc_t
*
* DESCRIPTION
*	Asynchronous processor structure.
*
*	The cl_async_proc_t structure should be treated as opaque, and should be
*	manipulated only through the provided functions.
*
* SYNOPSIS
*/
typedef struct _cl_async_proc
{
	cl_thread_pool_t	thread_pool;
	cl_qlist_t			item_queue;
	cl_spinlock_t		lock;

} cl_async_proc_t;
/*
* FIELDS
*	item_pool
*		Pool of items storing the callback function and contexts to be invoked
*		by the asynchronous processor's threads.
*
*	thread_pool
*		Thread pool that will invoke the callbacks.
*
*	item_queue
*		Queue of items that the threads should process.
*
*	lock
*		Lock used to synchronize access to the item pool and queue.
*
* SEE ALSO
*	Asynchronous Processor
*********/

/*
 * Declare the structure so we can reference it in the following function
 * prototype.
 */
typedef struct _cl_async_proc_item	*__p_cl_async_proc_item_t;

/****d* Component Library: Asynchronous Processor/cl_pfn_async_proc_cb_t
* NAME
*	cl_pfn_async_proc_cb_t
*
* DESCRIPTION
*	The cl_pfn_async_proc_cb_t function type defines the prototype for
*	callbacks queued to and invoked by the asynchronous processor.
*
* SYNOPSIS
*/
typedef void
(*cl_pfn_async_proc_cb_t)(
	IN	struct _cl_async_proc_item	*p_item );
/*
* PARAMETERS
*	p_item
*		Pointer to the cl_async_proc_item_t structure that was queued in
*		a call to cl_async_proc_queue.
*
* NOTES
*	This function type is provided as function prototype reference for the
*	function provided by users as a parameter to the cl_async_proc_queue
*	function.
*
* SEE ALSO
*	Asynchronous Processor, cl_async_proc_item_t
*********/

/****s* Component Library: Asynchronous Processor/cl_async_proc_item_t
* NAME
*	cl_async_proc_item_t
*
* DESCRIPTION
*	Asynchronous processor item structure passed to the cl_async_proc_queue
*	function to queue a callback for execution.
*
* SYNOPSIS
*/
typedef struct _cl_async_proc_item
{
	cl_pool_item_t			pool_item;
	cl_pfn_async_proc_cb_t	pfn_callback;

} cl_async_proc_item_t;
/*
* FIELDS
*	pool_item
*		Pool item for queuing the item to be invoked by the asynchronous
*		processor's threads.  This field is defined as a pool item to
*		allow items to be managed by a pool.
*
*	pfn_callback
*		Pointer to a callback function to invoke when the item is dequeued.
*
* SEE ALSO
*	Asynchronous Processor, cl_async_proc_queue, cl_pfn_async_proc_cb_t
*********/

/****f* Component Library: Asynchronous Processor/cl_async_proc_construct
* NAME
*	cl_async_proc_construct
*
* DESCRIPTION
*	The cl_async_proc_construct function initializes the state of a
*	thread pool.
*
* SYNOPSIS
*/
void
cl_async_proc_construct(
	IN	cl_async_proc_t* const	p_async_proc );
/*
* PARAMETERS
*	p_async_proc
*		[in] Pointer to an asynchronous processor structure.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Allows calling cl_async_proc_destroy without first calling
*	cl_async_proc_init.
*
*	Calling cl_async_proc_construct is a prerequisite to calling any other
*	thread pool function except cl_async_proc_init.
*
* SEE ALSO
*	Asynchronous Processor, cl_async_proc_init, cl_async_proc_destroy
*********/

/****f* Component Library: Asynchronous Processor/cl_async_proc_init
* NAME
*	cl_async_proc_init
*
* DESCRIPTION
*	The cl_async_proc_init function initialized an asynchronous processor
*	for use.
*
* SYNOPSIS
*/
cl_status_t
cl_async_proc_init(
	IN	cl_async_proc_t* const	p_async_proc,
	IN	const uint32_t			thread_count,
	IN	const char* const		name );
/*
* PARAMETERS
*	p_async_proc
*		[in] Pointer to an asynchronous processor structure to initialize.
*
*	thread_count
*		[in] Number of threads to be managed by the asynchronous processor.
*
*	name
*		[in] Name to associate with the threads.  The name may be up to 16
*		characters, including a terminating null character.  All threads
*		created in the asynchronous processor have the same name.
*
* RETURN VALUES
*	CL_SUCCESS if the asynchronous processor creation succeeded.
*
*	CL_INSUFFICIENT_MEMORY if there was not enough memory to inititalize
*	the asynchronous processor.
*
*	CL_ERROR if the threads could not be created.
*
* NOTES
*	cl_async_proc_init creates and starts the specified number of threads.
*	If thread_count is zero, the asynchronous processor creates as many
*	threads as there are processors in the system.
*
* SEE ALSO
*	Asynchronous Processor, cl_async_proc_construct, cl_async_proc_destroy,
*	cl_async_proc_queue
*********/

/****f* Component Library: Asynchronous Processor/cl_async_proc_destroy
* NAME
*	cl_async_proc_destroy
*
* DESCRIPTION
*	The cl_async_proc_destroy function performs any necessary cleanup
*	for a thread pool.
*
* SYNOPSIS
*/
void
cl_async_proc_destroy(
	IN	cl_async_proc_t* const	p_async_proc );
/*
* PARAMETERS
*	p_async_proc
*		[in] Pointer to an asynchronous processor structure to destroy.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	This function blocks until all threads exit, and must therefore not
*	be called from any of the asynchronous processor's threads. Because of
*	its blocking nature, callers of cl_async_proc_destroy must ensure that
*	entering a wait state is valid from the calling thread context.
*
*	This function should only be called after a call to
*	cl_async_proc_construct or cl_async_proc_init.
*
* SEE ALSO
*	Asynchronous Processor, cl_async_proc_construct, cl_async_proc_init
*********/

/****f* Component Library: Asynchronous Processor/cl_async_proc_queue
* NAME
*	cl_async_proc_queue
*
* DESCRIPTION
*	The cl_async_proc_queue function queues a callback to an asynchronous
*	processor.
*
* SYNOPSIS
*/
void
cl_async_proc_queue(
	IN	cl_async_proc_t* const		p_async_proc,
	IN	cl_async_proc_item_t* const	p_item );
/*
* PARAMETERS
*	p_async_proc
*		[in] Pointer to an asynchronous processor structure to initialize.
*
*	p_item
*		[in] Pointer to an asynchronous processor item to queue for execution.
*		The callback and context fields of the item must be valid.
*
* RETURN VALUES
*	This function does not return a value.
*
* SEE ALSO
*	Asynchronous Processor, cl_async_proc_init, cl_pfn_async_proc_cb_t
*********/

END_C_DECLS

#endif	/* !defined(_CL_ASYNC_PROC_H_) */

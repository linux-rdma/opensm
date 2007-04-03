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
 *	Declaration of cl_qlock_pool_t.
 *	This object represents a threadsafe quick-pool of objects.
 *
 * Environment:
 *	All
 *
 * $Revision: 1.3 $
 */

#ifndef _CL_QLOCKPOOL_H_
#define _CL_QLOCKPOOL_H_

#include <complib/cl_qpool.h>
#include <complib/cl_spinlock.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else /* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif /* __cplusplus */

BEGIN_C_DECLS

/****h* Component Library/Quick Locking Pool
* NAME
*	Quick Locking Pool
*
* DESCRIPTION
*	The Quick Locking Pool represents a thread-safe quick pool.
*
*	This object should be treated as opaque and should be
*	manipulated only through the provided functions.
*
* SEE ALSO
*	Structures:
*		cl_qlock_pool_t
*
*	Initialization:
*		cl_qlock_pool_construct, cl_qlock_pool_init, cl_qlock_pool_destroy
*
*	Manipulation
*		cl_qlock_pool_get, cl_qlock_pool_put
*********/

/****s* Component Library: Quick Locking Pool/cl_qlock_pool_t
* NAME
*	cl_qlock_pool_t
*
* DESCRIPTION
*	Quick Locking Pool structure.
*
*	This object should be treated as opaque and should
*	be manipulated only through the provided functions.
*
* SYNOPSIS
*/
typedef struct _cl_qlock_pool
{
	cl_spinlock_t				lock;
	cl_qpool_t					pool;

} cl_qlock_pool_t;
/*
* FIELDS
*	lock
*		Spinlock guarding the pool.
*
*	pool
*		quick_pool of user objects.
*
* SEE ALSO
*	Quick Locking Pool
*********/

/****f* Component Library: Quick Locking Pool/cl_qlock_pool_construct
* NAME
*	cl_qlock_pool_construct
*
* DESCRIPTION
*	This function constructs a Quick Locking Pool.
*
* SYNOPSIS
*/
static inline void
cl_qlock_pool_construct(
	IN cl_qlock_pool_t* const p_pool )
{
	cl_qpool_construct( &p_pool->pool );
	cl_spinlock_construct( &p_pool->lock );
}

/*
* PARAMETERS
*	p_pool
*		[in] Pointer to a Quick Locking Pool to construct.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Allows calling cl_qlock_pool_init, cl_qlock_pool_destroy
*
*	Calling cl_qlock_pool_construct is a prerequisite to calling any other
*	method except cl_qlock_pool_init.
*
* SEE ALSO
*	Quick Locking Pool, cl_qlock_pool_init, cl_qlock_pool_destroy
*********/

/****f* Component Library: Quick Locking Pool/cl_qlock_pool_destroy
* NAME
*	cl_qlock_pool_destroy
*
* DESCRIPTION
*	The cl_qlock_pool_destroy function destroys a node, releasing
*	all resources.
*
* SYNOPSIS
*/
static inline void
cl_qlock_pool_destroy(
	IN cl_qlock_pool_t* const p_pool )
{
	/*
		If the pool has already been put into use, grab the lock
		to sync with other threads before we blow everything away.
	*/
	if( cl_is_qpool_inited( &p_pool->pool ) )
	{
		cl_spinlock_acquire( &p_pool->lock );
		cl_qpool_destroy( &p_pool->pool );
		cl_spinlock_release( &p_pool->lock );
	}
	else
		cl_qpool_destroy( &p_pool->pool );

	cl_spinlock_destroy( &p_pool->lock );
}
/*
* PARAMETERS
*	p_pool
*		[in] Pointer to a Quick Locking Pool to destroy.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Performs any necessary cleanup of the specified Quick Locking Pool.
*	Further operations should not be attempted on the destroyed object.
*	This function should only be called after a call to
*	cl_qlock_pool_construct or cl_qlock_pool_init.
*
* SEE ALSO
*	Quick Locking Pool, cl_qlock_pool_construct, cl_qlock_pool_init
*********/

/****f* Component Library: Quick Locking Pool/cl_qlock_pool_init
* NAME
*	cl_qlock_pool_init
*
* DESCRIPTION
*	The cl_qlock_pool_init function initializes a Quick Locking Pool for use.
*
* SYNOPSIS
*/
static inline cl_status_t
cl_qlock_pool_init(
	IN cl_qlock_pool_t*			const p_pool,
	IN	const size_t			min_size,
	IN	const size_t			max_size,
	IN	const size_t			grow_size,
	IN	const size_t			object_size,
	IN	cl_pfn_qpool_init_t		pfn_initializer OPTIONAL,
	IN	cl_pfn_qpool_dtor_t		pfn_destructor OPTIONAL,
	IN	const void* const		context )
{
	cl_status_t status;

	cl_qlock_pool_construct( p_pool );

	status = cl_spinlock_init( &p_pool->lock );
	if( status )
		return( status );

	status = cl_qpool_init( &p_pool->pool, min_size, max_size, grow_size,
			object_size, pfn_initializer, pfn_destructor, context );

	return( status );
}
/*
* PARAMETERS
*	p_pool
*		[in] Pointer to an cl_qlock_pool_t object to initialize.
*
*	min_size
*		[in] Minimum number of objects that the pool should support. All
*		necessary allocations to allow storing the minimum number of items
*		are performed at initialization time, and all necessary callbacks
*		successfully invoked.
*
*	max_size
*		[in] Maximum number of objects to which the pool is allowed to grow.
*		A value of zero specifies no maximum.
*
*	grow_size
*		[in] Number of objects to allocate when incrementally growing the pool.
*		A value of zero disables automatic growth.
*
*	object_size
*		[in] Size, in bytes, of each object.
*
*	pfn_initializer
*		[in] Initialization callback to invoke for every new object when
*		growing the pool. This parameter is optional and may be NULL. If NULL,
*		the pool assumes the cl_pool_item_t structure describing objects is
*		located at the head of each object. See the cl_pfn_qpool_init_t
*		function type declaration for details about the callback function.
*
*	pfn_destructor
*		[in] Destructor callback to invoke for every object before memory for
*		that object is freed. This parameter is optional and may be NULL.
*		See the cl_pfn_qpool_dtor_t function type declaration for details
*		about the callback function.
*
*	context
*		[in] Value to pass to the callback functions to provide context.
*
* RETURN VALUES
*	CL_SUCCESS if the quick pool was initialized successfully.
*
*	CL_INSUFFICIENT_MEMORY if there was not enough memory to initialize the
*	quick pool.
*
*	CL_INVALID_SETTING if a the maximum size is non-zero and less than the
*	minimum size.
*
*	Other cl_status_t value returned by optional initialization callback function
*	specified by the pfn_initializer parameter.
*
* NOTES
*	Allows calling other Quick Locking Pool methods.
*
* SEE ALSO
*	Quick Locking Pool, cl_qlock_pool_construct, cl_qlock_pool_destroy
*********/

/****f* Component Library: Quick Locking Pool/cl_qlock_pool_get
* NAME
*	cl_qlock_pool_get
*
* DESCRIPTION
*	Gets an object wrapper and wire MAD from the pool.
*
* SYNOPSIS
*/
static inline cl_pool_item_t*
cl_qlock_pool_get(
	IN cl_qlock_pool_t* const p_pool )
{
	cl_pool_item_t* p_item;
	cl_spinlock_acquire( &p_pool->lock );
	p_item = cl_qpool_get( &p_pool->pool );
	cl_spinlock_release( &p_pool->lock );
	return( p_item );
}

/*
* PARAMETERS
*	p_pool
*		[in] Pointer to an cl_qlock_pool_t object.
*
* RETURN VALUES
*	Returns a pointer to a cl_pool_item_t contained in the user object.
*
* NOTES
*	The object must eventually be returned to the pool with a call to
*	cl_qlock_pool_put.
*
*	The cl_qlock_pool_construct or cl_qlock_pool_init must be called before
*	using this function.
*
* SEE ALSO
*	Quick Locking Pool, cl_qlock_pool_put
*********/

/****f* Component Library: Quick Locking Pool/cl_qlock_pool_put
* NAME
*	cl_qlock_pool_put
*
* DESCRIPTION
*	Returns an object to the pool.
*
* SYNOPSIS
*/
static inline void
cl_qlock_pool_put(
	IN cl_qlock_pool_t* const p_pool,
	IN cl_pool_item_t* const p_item )
{
	cl_spinlock_acquire( &p_pool->lock );
	cl_qpool_put( &p_pool->pool, p_item );
	cl_spinlock_release( &p_pool->lock );
}
/*
* PARAMETERS
*	p_pool
*		[in] Pointer to an cl_qlock_pool_t object.
*
*	p_item
*		[in] Pointer to the cl_pool_item_t in an object that was previously
*		retrieved from the pool.
*
* RETURN VALUES
*	This function does not return a value.
*
* NOTES
*	The cl_qlock_pool_construct or cl_qlock_pool_init must be called before
*	using this function.
*
* SEE ALSO
*	Quick Locking Pool, cl_qlock_pool_get
*********/

END_C_DECLS

#endif	/* _CL_QLOCKPOOL_H_ */

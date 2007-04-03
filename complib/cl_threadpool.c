/*
 * Copyright (c) 2004-2006 Voltaire, Inc. All rights reserved.
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
 *	Implementation of thread pool.
 *
 * Environment:
 *	All
 *
 * $Revision: 1.4 $
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <stdlib.h>
#include <string.h>
#include <complib/cl_threadpool.h>
#include <complib/cl_atomic.h>

void
__cl_thread_pool_routine(
	IN	void* const	context )
{
	cl_status_t			status = CL_SUCCESS;
	cl_thread_pool_t	*p_thread_pool = (cl_thread_pool_t*)context;

	/* Continue looping until signalled to end. */
	while( !p_thread_pool->exit )
	{
		/* Wait for the specified event to occur. */
		status = cl_event_wait_on( &p_thread_pool->wakeup_event, 
							EVENT_NO_TIMEOUT, TRUE );

		/* See if we've been signalled to end execution. */
		if( (p_thread_pool->exit) || (status == CL_NOT_DONE) )
			break;

		/* The event has been signalled.  Invoke the callback. */
		(*p_thread_pool->pfn_callback)( (void*)p_thread_pool->context );
	}

	/*
	 * Decrement the running count to notify the destroying thread
	 * that the event was received and processed.
	 */
	cl_atomic_dec( &p_thread_pool->running_count );
	cl_event_signal( &p_thread_pool->destroy_event );
}

void
cl_thread_pool_construct(
	IN	cl_thread_pool_t* const	p_thread_pool )
{
	CL_ASSERT( p_thread_pool);

	memset( p_thread_pool, 0, sizeof(cl_thread_pool_t) );
	cl_event_construct( &p_thread_pool->wakeup_event );
	cl_event_construct( &p_thread_pool->destroy_event );
	cl_list_construct( &p_thread_pool->thread_list );
	p_thread_pool->state = CL_UNINITIALIZED;
}

cl_status_t
cl_thread_pool_init(
	IN	cl_thread_pool_t* const		p_thread_pool,
	IN	uint32_t					count,
	IN	cl_pfn_thread_callback_t	pfn_callback,
	IN	const void* const			context,
	IN	const char* const			name )
{
	cl_status_t	status;
	cl_thread_t	*p_thread;
	uint32_t	i;

	CL_ASSERT( p_thread_pool );
	CL_ASSERT( pfn_callback );

	cl_thread_pool_construct( p_thread_pool );

	if( !count )
		count = cl_proc_count();

	status = cl_list_init( &p_thread_pool->thread_list, count );
	if( status != CL_SUCCESS )
	{
		cl_thread_pool_destroy( p_thread_pool );
		return( status );
	}

	/* Initialize the event that the threads wait on. */
	status = cl_event_init( &p_thread_pool->wakeup_event, FALSE );
	if( status != CL_SUCCESS )
	{
		cl_thread_pool_destroy( p_thread_pool );
		return( status );
	}

	/* Initialize the event used to destroy the threadpool. */
	status = cl_event_init( &p_thread_pool->destroy_event, FALSE );
	if( status != CL_SUCCESS )
	{
		cl_thread_pool_destroy( p_thread_pool );
		return( status );
	}

	p_thread_pool->pfn_callback = pfn_callback;
	p_thread_pool->context = context;

	for( i = 0; i < count; i++ )
	{
		/* Create a new thread. */
		p_thread = (cl_thread_t*)malloc( sizeof(cl_thread_t) );
		if( !p_thread )
		{
			cl_thread_pool_destroy( p_thread_pool );
			return( CL_INSUFFICIENT_MEMORY );
		}

		cl_thread_construct( p_thread );

		/*
		 * Add it to the list.  This is guaranteed to work since we
		 * initialized the list to hold at least the number of threads we want
		 * to store there.
		 */
		status = cl_list_insert_head( &p_thread_pool->thread_list, p_thread );
		CL_ASSERT( status == CL_SUCCESS );

		/* Start the thread. */
		status = cl_thread_init( p_thread, __cl_thread_pool_routine,
			p_thread_pool, name );
		if( status != CL_SUCCESS )
		{
			cl_thread_pool_destroy( p_thread_pool );
			return( status );
		}

		/*
		 * Increment the running count to insure that a destroying thread
		 * will signal all the threads.
		 */
		cl_atomic_inc( &p_thread_pool->running_count );
	}
	p_thread_pool->state = CL_INITIALIZED;
	return( CL_SUCCESS );
}

void
cl_thread_pool_destroy(
	IN	cl_thread_pool_t* const	p_thread_pool )
{
	cl_thread_t		*p_thread;

	CL_ASSERT( p_thread_pool );
	CL_ASSERT( cl_is_state_valid( p_thread_pool->state ) );

	/* Indicate to all threads that they need to exit. */
	p_thread_pool->exit = TRUE;

	/*
	 * Signal the threads until they have all exited.  Signalling
	 * once for each thread is not guaranteed to work since two events
	 * could release only a single thread, depending on the rate at which
	 * the events are set and how the thread scheduler processes notifications.
	 */

	while( p_thread_pool->running_count )
	{
     cl_event_signal( &p_thread_pool->wakeup_event );
     /*
      * Wait for the destroy event to occur, indicating that the thread
      * has exited.
      */
     cl_event_wait_on( &p_thread_pool->destroy_event,
                       EVENT_NO_TIMEOUT, TRUE );
   }

	/*
	 * Stop each thread one at a time.  Note that this cannot be done in the
	 * above for loop because signal will wake up an unknown thread.
	 */
	if( cl_is_list_inited( &p_thread_pool->thread_list ) )
	{
		while( !cl_is_list_empty( &p_thread_pool->thread_list ) )
		{
			p_thread =
				(cl_thread_t*)cl_list_remove_head( &p_thread_pool->thread_list );
			cl_thread_destroy( p_thread );
			free( p_thread );
		}
	}

	cl_event_destroy( &p_thread_pool->destroy_event );
	cl_event_destroy( &p_thread_pool->wakeup_event );
	cl_list_destroy( &p_thread_pool->thread_list );
	p_thread_pool->state = CL_UNINITIALIZED;
}

cl_status_t
cl_thread_pool_signal(
	IN	cl_thread_pool_t* const	p_thread_pool )
{
	CL_ASSERT( p_thread_pool );
	CL_ASSERT( p_thread_pool->state == CL_INITIALIZED );

	return( cl_event_signal( &p_thread_pool->wakeup_event ) );
}

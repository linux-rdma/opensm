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
 *	Implementation of performance tracking.
 *
 * Environment:
 *	All supported environments.
 *
 * $Revision: 1.3 $
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <stdlib.h>
#include <string.h>

/*
 * Always turn on performance tracking when building this file to allow the
 * performance counter functions to be built into the component library.
 * Users control their use of the functions by defining the PERF_TRACK_ON
 * keyword themselves before including cl_perf.h to enable the macros to
 * resolve to the internal functions.
 */
#define PERF_TRACK_ON

#include <complib/cl_perf.h>
#include <complib/cl_debug.h>

uint64_t
__cl_perf_run_calibration(
	IN	cl_perf_t* const p_perf );

/*
 * Initialize the state of the performance tracker.
 */
void
__cl_perf_construct(
	IN	cl_perf_t* const	p_perf )
{
	memset( p_perf, 0, sizeof(cl_perf_t) );
	p_perf->state = CL_UNINITIALIZED;
}

/*
 * Initialize the performance tracker.
 */
cl_status_t
__cl_perf_init(
	IN	cl_perf_t* const	p_perf,
	IN	const uintn_t		num_counters )
{
	cl_status_t		status;
	cl_spinlock_t	lock;
	uintn_t			i;
	static uint64_t	locked_calibration_time = 0;
	static uint64_t	normal_calibration_time;

	CL_ASSERT( p_perf );
	CL_ASSERT( !p_perf->size && num_counters );

	/* Construct the performance tracker. */
	__cl_perf_construct( p_perf );

	/* Allocate an array of counters. */
	p_perf->size = num_counters;
	p_perf->data_array = (cl_perf_data_t*)
		malloc( sizeof(cl_perf_data_t) * num_counters );

	if( !p_perf->data_array )
		return( CL_INSUFFICIENT_MEMORY );
	else
		memset( p_perf->data_array, 0,
			sizeof(cl_perf_data_t) * num_counters );

	/* Initialize the user's counters. */
	for( i = 0; i < num_counters; i++ )
	{
		p_perf->data_array[i].min_time = ((uint64_t)~0);
		cl_spinlock_construct( &p_perf->data_array[i].lock );
	}

	for( i = 0; i < num_counters; i++ )
	{
		status = cl_spinlock_init( &p_perf->data_array[i].lock );
		if( status != CL_SUCCESS )
		{
			__cl_perf_destroy( p_perf, FALSE );
			return( status );
		}
	}

	/*
	 * Run the calibration only if it has not been run yet.  Subsequent
	 * calls will use the results from the first calibration.
	 */
	if( !locked_calibration_time )
	{
		/*
		 * Perform the calibration under lock to prevent thread context
		 * switches.
		 */
		cl_spinlock_construct( &lock );
		status = cl_spinlock_init( &lock );
		if( status != CL_SUCCESS )
		{
			__cl_perf_destroy( p_perf, FALSE );
			return( status );
		}

		/* Measure the impact when running at elevated thread priority. */
		cl_spinlock_acquire( &lock );
		locked_calibration_time = __cl_perf_run_calibration( p_perf );
		cl_spinlock_release( &lock );
		cl_spinlock_destroy( &lock );

		/* Measure the impact when runnin at normal thread priority. */
		normal_calibration_time = __cl_perf_run_calibration( p_perf );
	}

	/* Reset the user's performance counter. */
	p_perf->normal_calibration_time = locked_calibration_time;
	p_perf->locked_calibration_time = normal_calibration_time;
	p_perf->data_array[0].count = 0;
	p_perf->data_array[0].total_time = 0;
	p_perf->data_array[0].min_time = ((uint64_t)~0);

	p_perf->state = CL_INITIALIZED;

	return( CL_SUCCESS );
}

/*
 * Measure the time to take performance counters.
 */
uint64_t
__cl_perf_run_calibration(
	IN	cl_perf_t* const	p_perf )
{
	uint64_t		start_time;
	uintn_t			i;
	PERF_DECLARE( 0 );

	/* Start timing. */
	start_time = cl_get_time_stamp();

	/*
	 * Get the performance counter repeatedly in a loop.  Use the first
	 * user counter as our test counter.
	 */
	for( i = 0; i < PERF_CALIBRATION_TESTS; i++ )
	{
		cl_perf_start( 0 );
		cl_perf_stop( p_perf, 0 );
	}

	/* Calculate the total time for the calibration. */
	return( cl_get_time_stamp() - start_time );
}

/*
 * Destroy the performance tracker.
 */
void
__cl_perf_destroy(
	IN	cl_perf_t* const	p_perf,
	IN	const boolean_t		display )
{
	uintn_t	i;

	CL_ASSERT( cl_is_state_valid( p_perf->state ) );

	if( !p_perf->data_array )
		return;

	/* Display the performance data as requested. */
	if( display && p_perf->state == CL_INITIALIZED )
		__cl_perf_display( p_perf );

	/* Destroy the user's counters. */
	for( i = 0; i < p_perf->size; i++ )
		cl_spinlock_destroy( &p_perf->data_array[i].lock );

	free( p_perf->data_array );
	p_perf->data_array = NULL;

	p_perf->state = CL_UNINITIALIZED;
}

/*
 * Reset the performance counters.
 */
void
__cl_perf_reset(
	IN	cl_perf_t* const		p_perf )
{
	uintn_t	i;

	for( i = 0; i < p_perf->size; i++ )
	{
		cl_spinlock_acquire( &p_perf->data_array[i].lock );
		p_perf->data_array[i].min_time = ((uint64_t)~0);
		p_perf->data_array[i].total_time = 0;
		p_perf->data_array[i].count = 0;
		cl_spinlock_release( &p_perf->data_array[i].lock );
	}
}

/*
 * Display the captured performance data.
 */
void
__cl_perf_display(
	IN	const cl_perf_t* const	p_perf )
{
	uintn_t	i;

	CL_ASSERT( p_perf );
	CL_ASSERT( p_perf->state == CL_INITIALIZED );

	cl_msg_out( "\n\n\nCL Perf:\tPerformance Data\n" );

	cl_msg_out( "CL Perf:\tCounter Calibration Time\n" );
	cl_msg_out( "CL Perf:\tLocked TotalTime\tNormal TotalTime\tTest Count\n" );
	cl_msg_out( "CL Perf:\t%"PRIu64"\t%"PRIu64"\t%u\n",
		p_perf->locked_calibration_time, p_perf->normal_calibration_time,
		PERF_CALIBRATION_TESTS );

	cl_msg_out( "CL Perf:\tUser Performance Counters\n" );
	cl_msg_out( "CL Perf:\tIndex\tTotalTime\tMinTime\tCount\n" );
	for( i = 0; i < p_perf->size; i++ )
	{
		cl_msg_out( "CL Perf:\t%lu\t%"PRIu64"\t%"PRIu64"\t%"PRIu64"\n",
			i, p_perf->data_array[i].total_time,
			p_perf->data_array[i].min_time, p_perf->data_array[i].count );
	}
	cl_msg_out( "CL Perf:\tEnd of User Performance Counters\n" );
}

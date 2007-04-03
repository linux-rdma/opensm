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
 *	Declaration of performance tracking.
 *
 * Environment:
 *	All
 *
 * $Revision: 1.3 $
 */

#ifndef _CL_PERF_H_
#define _CL_PERF_H_

#include <complib/cl_types.h>
#include <complib/cl_spinlock.h>
#include <complib/cl_timer.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else /* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif /* __cplusplus */

BEGIN_C_DECLS

/****h* Component Library/Performance Counters
* NAME
*	Performance Counters
*
* DESCRIPTION
*	The performance counters allows timing operations to benchmark
*	software performance and help identify potential bottlenecks.
*
*	All performance counters are NULL macros when disabled, preventing them
*	from adversly affecting performance in builds where the counters are not
*	used.
*
*	Each counter records elapsed time in micro-seconds, minimum time elapsed,
*	and total number of samples.
*
*	Each counter is independently protected by a spinlock, allowing use of
*	the counters in multi-processor environments.
*
*	The impact of serializing access to performance counters is measured,
*	allowing measurements to be corrected as necessary.
*
* NOTES
*	Performance counters do impact performance, and should only be enabled
*	when gathering data.  Counters can be enabled or disabled on a per-user
*	basis at compile time.  To enable the counters, users should define
*	the PERF_TRACK_ON keyword before including the cl_perf.h file.
*	Undefining the PERF_TRACK_ON keyword disables the performance counters.
*	When disabled, all performance tracking calls resolve to no-ops.
*
*	When using performance counters, it is the user's responsibility to
*	maintain the counter indexes.  It is recomended that users define an
*	enumerated type to use for counter indexes.  It improves readability
*	and simplifies maintenance by reducing the work necessary in managing
*	the counter indexes.
*
* SEE ALSO
*	Structures:
*		cl_perf_t
*
*	Initialization:
*		cl_perf_construct, cl_perf_init, cl_perf_destroy
*
*	Manipulation
*		cl_perf_reset, cl_perf_display, cl_perf_start, cl_perf_update,
*		cl_perf_log, cl_perf_stop
*
*	Macros:
*		PERF_DECLARE, PERF_DECLARE_START
*********/

/*
 * Number of times the counter calibration test is executed.  This is used
 * to determine the average time to use a performance counter.
 */
#define PERF_CALIBRATION_TESTS		100000

/****i* Component Library: Performance Counters/cl_perf_data_t
* NAME
*	cl_perf_data_t
*
* DESCRIPTION
*	The cl_perf_data_t structure is used to tracking information
*	for a single counter.
*
* SYNOPSIS
*/
typedef struct _cl_perf_data
{
	uint64_t		count;
	uint64_t		total_time;
	uint64_t		min_time;
	cl_spinlock_t	lock;

} cl_perf_data_t;
/*
* FIELDS
*	count
*		Number of samples in the counter.
*
*	total_time
*		Total time for all samples, in microseconds.
*
*	min_time
*		Minimum time for any sample in the counter, in microseconds.
*
*	lock
*		Spinlock to serialize counter updates.
*
* SEE ALSO
*	Performance Counters
*********/

/****i* Component Library: Performance Counters/cl_perf_t
* NAME
*	cl_perf_t
*
* DESCRIPTION
*	The cl_perf_t structure serves as a container for a group of performance
*	counters and related calibration data.
*
*	This structure should be treated as opaque and be manipulated only through
*	the provided functions.
*
* SYNOPSIS
*/
typedef struct _cl_perf
{
	cl_perf_data_t	*data_array;
	uintn_t			size;
	uint64_t		locked_calibration_time;
	uint64_t		normal_calibration_time;
	cl_state_t		state;

} cl_perf_t;
/*
* FIELDS
*	data_array
*		Pointer to the array of performance counters.
*
*	size
*		Number of counters in the counter array.
*
*	locked_calibration_time
*		Time needed to update counters while holding a spinlock.
*
*	normal_calibration_time
*		Time needed to update counters while not holding a spinlock.
*
*	state
*		State of the performance counter provider.
*
* SEE ALSO
*	Performance Counters, cl_perf_data_t
*********/

/****f* Component Library: Performance Counters/cl_perf_construct
* NAME
*	cl_perf_construct
*
* DESCRIPTION
*	The cl_perf_construct macro constructs a performance
*	tracking container.
*
* SYNOPSIS
*/
void
cl_perf_construct(
	IN	cl_perf_t* const	p_perf );
/*
* PARAMETERS
*	p_perf
*		[in] Pointer to a performance counter container to construct.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	cl_perf_construct allows calling cl_perf_destroy without first calling
*	cl_perf_init.
*
*	Calling cl_perf_construct is a prerequisite to calling any other
*	perfromance counter function except cl_perf_init.
*
*	This function is implemented as a macro and has no effect when
*	performance counters are disabled.
*
* SEE ALSO
*	Performance Counters, cl_perf_init, cl_perf_destroy
*********/

/****f* Component Library: Performance Counters/cl_perf_init
* NAME
*	cl_perf_init
*
* DESCRIPTION
*	The cl_perf_init function initializes a performance counter container
*	for use.
*
* SYNOPSIS
*/
cl_status_t
cl_perf_init(
	IN	cl_perf_t* const	p_perf,
	IN	const uintn_t		num_counters );
/*
* PARAMETERS
*	p_perf
*		[in] Pointer to a performance counter container to initalize.
*
*	num_cntrs
*		[in] Number of counters to allocate in the container.
*
* RETURN VALUES
*	CL_SUCCESS if initialization was successful.
*
*	CL_INSUFFICIENT_MEMORY if there was not enough memory to initialize
*	the container.
*
*	CL_ERROR if an error was encountered initializing the locks for the
*	performance counters.
*
* NOTES
*	This function allocates all memory required for the requested number of
*	counters and initializes all locks protecting those counters.  After a
*	successful initialization, cl_perf_init calibrates the counters and
*	resets their value.
*
*	This function is implemented as a macro and has no effect when
*	performance counters are disabled.
*
* SEE ALSO
*	Performance Counters, cl_perf_construct, cl_perf_destroy, cl_perf_display
*********/

/****f* Component Library: Performance Counters/cl_perf_destroy
* NAME
*	cl_perf_destroy
*
* DESCRIPTION
*	The cl_perf_destroy function destroys a performance tracking container.
*
* SYNOPSIS
*/
void
cl_perf_destroy(
	IN	cl_perf_t* const	p_perf,
	IN	const boolean_t		display );
/*
* PARAMETERS
*	p_perf
*		[in] Pointer to a performance counter container to destroy.
*
*	display
*		[in] If TRUE, causes the performance counters to be displayed.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	cl_perf_destroy frees all resources allocated in a call to cl_perf_init.
*	If the display parameter is set to TRUE, displays all counter values
*	before deallocating resources.
*
*	This function should only be called after a call to cl_perf_construct
*	or cl_perf_init.
*
*	This function is implemented as a macro and has no effect when
*	performance counters are disabled.
*
* SEE ALSO
*	Performance Counters, cl_perf_construct, cl_perf_init
*********/

/****f* Component Library: Performance Counters/cl_perf_reset
* NAME
*	cl_perf_reset
*
* DESCRIPTION
*	The cl_perf_reset function resets the counters contained in
*	a performance tracking container.
*
* SYNOPSIS
*/
void
cl_perf_reset(
	IN	cl_perf_t* const	p_perf );
/*
* PARAMETERS
*	p_perf
*		[in] Pointer to a performance counter container whose counters
*		to reset.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	This function is implemented as a macro and has no effect when
*	performance counters are disabled.
*
* SEE ALSO
*	Performance Counters
*********/

/****f* Component Library: Performance Counters/cl_perf_display
* NAME
*	cl_perf_display
*
* DESCRIPTION
*	The cl_perf_display function displays the current performance
*	counter values.
*
* SYNOPSIS
*/
void
cl_perf_display(
	IN	const cl_perf_t* const	p_perf );
/*
* PARAMETERS
*	p_perf
*		[in] Pointer to a performance counter container whose counter
*		values to display.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	This function is implemented as a macro and has no effect when
*	performance counters are disabled.
*
* SEE ALSO
*	Performance Counters, cl_perf_init
*********/

/****d* Component Library: Performance Counters/PERF_DECLARE
* NAME
*	PERF_DECLARE
*
* DESCRIPTION
*	The PERF_DECLARE macro declares a performance counter variable used
*	to store the starting time of a timing sequence.
*
* SYNOPSIS
*	PERF_DECLARE( index )
*
* PARAMETERS
*	index
*		[in] Index of the performance counter for which to use this
*		variable.
*
* NOTES
*	Variables should generally be declared on the stack to support
*	multi-threading.  In cases where a counter needs to be used to
*	time operations accross multiple functions, care must be taken to
*	ensure that the start time stored in this variable is not overwritten
*	before the related performance counter has been updated.
*
*	This macro has no effect when performance counters are disabled.
*
* SEE ALSO
*	Performance Counters, PERF_DECLARE_START, cl_perf_start, cl_perf_log,
*	cl_perf_stop
*********/

/****d* Component Library: Performance Counters/PERF_DECLARE_START
* NAME
*	PERF_DECLARE_START
*
* DESCRIPTION
*	The PERF_DECLARE_START macro declares a performance counter variable
*	and sets it to the starting time of a timed sequence.
*
* SYNOPSIS
*	PERF_DECLARE_START( index )
*
* PARAMETERS
*	index
*		[in] Index of the performance counter for which to use this
*		variable.
*
* NOTES
*	Variables should generally be declared on the stack to support
*	multi-threading.
*
*	This macro has no effect when performance counters are disabled.
*
* SEE ALSO
*	Performance Counters, PERF_DECLARE, cl_perf_start, cl_perf_log,
*	cl_perf_stop
*********/

/****d* Component Library: Performance Counters/cl_perf_start
* NAME
*	cl_perf_start
*
* DESCRIPTION
*	The cl_perf_start macro sets the starting value of a timed sequence.
*
* SYNOPSIS
*/
void
cl_perf_start(
	IN	const uintn_t index );
/*
* PARAMETERS
*	index
*		[in] Index of the performance counter to set.
*
* NOTES
*	This macro has no effect when performance counters are disabled.
*
* SEE ALSO
*	Performance Counters, PERF_DECLARE, PERF_DECLARE_START, cl_perf_log,
*	cl_perf_update, cl_perf_stop
*********/

/****d* Component Library: Performance Counters/cl_perf_update
* NAME
*	cl_perf_update
*
* DESCRIPTION
*	The cl_perf_update macro adds a timing sample based on a provided start
*	time to a counter in a performance counter container.
*
* SYNOPSIS
*/
void
cl_perf_update(
	IN	cl_perf_t* const	p_perf,
	IN	const uintn_t		index,
	IN	const uint64_t		start_time );
/*
* PARAMETERS
*	p_perf
*		[in] Pointer to a performance counter container to whose counter
*		the sample should be added.
*
*	index
*		[in] Number of the performance counter to update with a new sample.
*
*	start_time
*		[in] Timestamp to use as the start time for the timing sample.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	This macro has no effect when performance counters are disabled.
*
* SEE ALSO
*	Performance Counters, PERF_DECLARE, PERF_DECLARE_START, cl_perf_start,
*	cl_perf_lob, cl_perf_stop
*********/

/****d* Component Library: Performance Counters/cl_perf_log
* NAME
*	cl_perf_log
*
* DESCRIPTION
*	The cl_perf_log macro adds a given timing sample to a
*	counter in a performance counter container.
*
* SYNOPSIS
*/
void
cl_perf_log(
	IN	cl_perf_t* const	p_perf,
	IN	const uintn_t		index,
	IN	const uint64_t		pc_total_time );
/*
* PARAMETERS
*	p_perf
*		[in] Pointer to a performance counter container to whose counter
*		the sample should be added.
*
*	index
*		[in] Number of the performance counter to update with a new sample.
*
*	pc_total_time
*		[in] Total elapsed time for the sample being added.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	This macro has no effect when performance counters are disabled.
*
* SEE ALSO
*	Performance Counters, PERF_DECLARE, PERF_DECLARE_START, cl_perf_start,
*	cl_perf_update, cl_perf_stop
*********/

/****d* Component Library: Performance Counters/cl_perf_stop
* NAME
*	cl_perf_stop
*
* DESCRIPTION
*	The cl_perf_log macro updates a counter in a performance counter
*	container with a new timing sample.
*
* SYNOPSIS
*/
void
cl_perf_stop(
	IN	cl_perf_t* const	p_perf,
	IN	const uintn_t		index );
/*
* PARAMETERS
*	p_perf
*		[in] Pointer to a performance counter container to whose counter
*		a sample should be added.
*
*	index
*		[in] Number of the performance counter to update with a new sample.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	The ending time stamp is taken and elapsed time calculated before updating
*	the specified counter.
*
*	This macro has no effect when performance counters are disabled.
*
* SEE ALSO
*	Performance Counters, PERF_DECLARE, PERF_DECLARE_START, cl_perf_start,
*	cl_perf_log
*********/

/*
 * PERF_TRACK_ON must be defined by the user before including this file to
 * enable performance tracking.  To disable tracking, users should undefine
 * PERF_TRACK_ON.
 */
#if defined( PERF_TRACK_ON )
/*
 * Enable performance tracking.
 */

#define cl_perf_construct( p_perf ) \
	__cl_perf_construct( p_perf )
#define cl_perf_init( p_perf, num_counters ) \
	__cl_perf_init( p_perf, num_counters )
#define cl_perf_destroy( p_perf, display ) \
	__cl_perf_destroy( p_perf, display )
#define cl_perf_reset( p_perf ) \
	__cl_perf_reset( p_perf )
#define cl_perf_display( p_perf ) \
	__cl_perf_display( p_perf )
#define PERF_DECLARE( index ) \
	uint64_t Pc##index
#define PERF_DECLARE_START( index ) \
	uint64 Pc##index = cl_get_time_stamp()
#define cl_perf_start( index ) \
	(Pc##index = cl_get_time_stamp())
#define cl_perf_log( p_perf, index, pc_total_time ) \
{\
	/* Update the performance data.  This requires synchronization. */ \
	cl_spinlock_acquire( &((cl_perf_t*)p_perf)->data_array[index].lock ); \
	\
	((cl_perf_t*)p_perf)->data_array[index].total_time += pc_total_time; \
	((cl_perf_t*)p_perf)->data_array[index].count++; \
	if( pc_total_time < ((cl_perf_t*)p_perf)->data_array[index].min_time ) \
		((cl_perf_t*)p_perf)->data_array[index].min_time = pc_total_time; \
	\
	cl_spinlock_release( &((cl_perf_t*)p_perf)->data_array[index].lock );  \
}
#define cl_perf_update( p_perf, index, start_time )	\
{\
	/* Get the ending time stamp, and calculate the total time. */ \
	uint64_t pc_total_time = cl_get_time_stamp() - start_time;\
	/* Using stack variable for start time, stop and log  */ \
	cl_perf_log( p_perf, index, pc_total_time ); \
}

#define cl_perf_stop( p_perf, index ) \
{\
	cl_perf_update( p_perf, index, Pc##index );\
}

#define cl_get_perf_values( p_perf, index, p_total, p_min, p_count )	\
{\
	*p_total = p_perf->data_array[index].total_time;	\
	*p_min = p_perf->data_array[index].min_time;		\
	*p_count = p_perf->data_array[index].count;			\
}

#define cl_get_perf_calibration( p_perf, p_locked_time, p_normal_time )	\
{\
	*p_locked_time = p_perf->locked_calibration_time;	\
	*p_normal_time = p_perf->normal_calibration_time;	\
}

#define cl_get_perf_string( p_perf, i )	\
"CL Perf:\t%lu\t%"PRIu64"\t%"PRIu64"\t%"PRIu64"\n",	\
			i, p_perf->data_array[i].total_time,	\
			p_perf->data_array[i].min_time, p_perf->data_array[i].count

#else	/* PERF_TRACK_ON */
/*
 * Disable performance tracking.
 */

#define cl_perf_construct( p_perf )
#define cl_perf_init( p_perf, num_cntrs )		CL_SUCCESS
#define cl_perf_destroy( p_perf, display )
#define cl_perf_reset( p_perf )
#define cl_perf_display( p_perf )
#define PERF_DECLARE( index )
#define PERF_DECLARE_START( index )
#define cl_perf_start( index )
#define cl_perf_log( p_perf, index, pc_total_time )
#define cl_perf_upadate( p_perf, index, start_time )
#define cl_perf_stop( p_perf, index )
#define cl_get_perf_values( p_perf, index, p_total, p_min, p_count )
#define cl_get_perf_calibration( p_perf, p_locked_time, p_normal_time )
#endif	/* PERF_TRACK_ON */

/*
 * Internal performance tracking functions.  Users should never call these
 * functions directly.  Instead, use the macros defined above to resolve
 * to these functions when PERF_TRACK_ON is defined, which allows disabling
 * performance tracking.
 */

/*
 * Initialize the state of the performance tracking structure.
 */
void
__cl_perf_construct(
	IN	cl_perf_t* const		p_perf );

/*
 * Size the performance tracking information and initialize all
 * related structures.
 */
cl_status_t
__cl_perf_init(
	IN	cl_perf_t* const		p_perf,
	IN	const uintn_t			num_counters );

/*
 * Destroy the performance tracking data.
 */
void
__cl_perf_destroy(
	IN	cl_perf_t* const		p_perf,
	IN	const boolean_t			display );

/*
 * Reset the performance tracking data.
 */
void
__cl_perf_reset(
	IN	cl_perf_t* const		p_perf );

/*
 * Display the current performance tracking data.
 */
void
__cl_perf_display(
	IN	const cl_perf_t* const	p_perf );


END_C_DECLS

#endif	/* _CL_PERF_H_ */

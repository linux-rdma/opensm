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
 * 	Declaration of osm_trap_rcv_t.
 *	This object represents the Trap Receiver object.
 *	This object is part of the OpenSM family of objects.
 *
 * Environment:
 * 	Linux User Mode
 *
 * $Revision: 1.3 $
 */

#ifndef _OSM_TRAP_RCV_H_
#define _OSM_TRAP_RCV_H_

#include <complib/cl_passivelock.h>
#include <complib/cl_event_wheel.h>
#include <opensm/osm_base.h>
#include <opensm/osm_madw.h>
#include <opensm/osm_stats.h>
#include <opensm/osm_subnet.h>
#include <opensm/osm_resp.h>
#include <opensm/osm_remote_sm.h>
#include <opensm/osm_log.h>
#include <opensm/osm_state_mgr.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else /* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif /* __cplusplus */

BEGIN_C_DECLS

/****h* OpenSM/Trap Receiver
* NAME
*	Trap Receiver
*
* DESCRIPTION
*	The Trap Receiver object encapsulates the information
*	needed to receive the Trap attribute from a node.
*
*	The Trap Receiver object is thread safe.
*
*	This object should be treated as opaque and should be
*	manipulated only through the provided functions.
*
* AUTHOR
*	Steve King, Intel
*
*********/

/****s* OpenSM: Trap Receiver/osm_trap_rcv_t
* NAME
*	osm_trap_rcv_t
*
* DESCRIPTION
*	Trap Receiver structure.
*
*	This object should be treated as opaque and should
*	be manipulated only through the provided functions.
*
* SYNOPSIS
*/
typedef struct _osm_trap_rcv
{
	osm_subn_t		*p_subn;
	osm_stats_t		*p_stats;
	osm_log_t	       	*p_log;
	osm_resp_t		*p_resp;
	osm_state_mgr_t		*p_state_mgr;
	cl_plock_t		*p_lock;
	cl_event_wheel_t	trap_aging_tracker;
} osm_trap_rcv_t;
/*
* FIELDS
*	p_subn
*		Pointer to the Subnet object for this subnet.
*
*	p_stats
*		Pointer to the OpenSM statistics block.
*
*	p_log
*		Pointer to the log object.
*
*	p_resp
*		Pointer to the generic MAD responder object.
*
*	p_state_mgr
*		Pointer to the State Manager object.
*
*	p_lock
*		Pointer to the serializing lock.
*
*	trap_aging_tracker
*		An event wheel tracking erceived traps and their aging.
*		Basically we can start a timer every time we receive a specific 
*		trap and check to seee if not expired next time it is received.
*
* SEE ALSO
*	Trap Receiver object
*********/

/****f* OpenSM: Trap Receiver/osm_trap_rcv_construct
* NAME
*	osm_trap_rcv_construct
*
* DESCRIPTION
*	This function constructs a Trap Receiver object.
*
* SYNOPSIS
*/
void osm_trap_rcv_construct(
	IN osm_trap_rcv_t* const p_rcv );
/*
* PARAMETERS
*	p_rcv
*		[in] Pointer to a Trap Receiver object to construct.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Allows calling osm_trap_rcv_init, osm_trap_rcv_destroy
*
*	Calling osm_trap_rcv_construct is a prerequisite to calling any other
*	method except osm_trap_rcv_init.
*
* SEE ALSO
*	Trap Receiver object, osm_trap_rcv_init,
*	osm_trap_rcv_destroy
*********/

/****f* OpenSM: Trap Receiver/osm_trap_rcv_destroy
* NAME
*	osm_trap_rcv_destroy
*
* DESCRIPTION
*	The osm_trap_rcv_destroy function destroys the object, releasing
*	all resources.
*
* SYNOPSIS
*/
void osm_trap_rcv_destroy(
	IN osm_trap_rcv_t* const p_rcv );
/*
* PARAMETERS
*	p_rcv
*		[in] Pointer to the object to destroy.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Performs any necessary cleanup of the specified
*	Trap Receiver object.
*	Further operations should not be attempted on the destroyed object.
*	This function should only be called after a call to
*	osm_trap_rcv_construct or osm_trap_rcv_init.
*
* SEE ALSO
*	Trap Receiver object, osm_trap_rcv_construct,
*	osm_trap_rcv_init
*********/

/****f* OpenSM: Trap Receiver/osm_trap_rcv_init
* NAME
*	osm_trap_rcv_init
*
* DESCRIPTION
*	The osm_trap_rcv_init function initializes a
*	Trap Receiver object for use.
*
* SYNOPSIS
*/
ib_api_status_t osm_trap_rcv_init(
	IN osm_trap_rcv_t* const p_rcv,
	IN osm_subn_t* const p_subn,
	IN osm_stats_t* const p_stats,
	IN osm_resp_t* const p_resp,
	IN osm_log_t* const p_log,
	IN osm_state_mgr_t* const p_state_mgr,
	IN cl_plock_t* const p_lock );
/*
* PARAMETERS
*	p_rcv
*		[in] Pointer to an osm_trap_rcv_t object to initialize.
*
*	p_subn
*		[in] Pointer to the Subnet object for this subnet.
*
*	p_stats
*		[in] Pointer to the OpenSM statistics block.
*
*	p_resp
*		[in] Pointer to the generic MAD Responder object.
*
*	p_log
*		[in] Pointer to the log object.
*
*	p_state_mgr
*		[in] Pointer to the State Manager object.
*
*	p_lock
*		[in] Pointer to the OpenSM serializing lock.
*
* RETURN VALUES
*	IB_SUCCESS if the Trap Receiver object was initialized
*	successfully.
*
* NOTES
*	Allows calling other Trap Receiver methods.
*
* SEE ALSO
*	Trap Receiver object, osm_trap_rcv_construct,
*	osm_trap_rcv_destroy
*********/

/****f* OpenSM: Trap Receiver/osm_trap_rcv_process
* NAME
*	osm_trap_rcv_process
*
* DESCRIPTION
*	Process the Trap attribute.
*
* SYNOPSIS
*/
void osm_trap_rcv_process(
	IN void *context,
	IN void *data );
/*
* PARAMETERS
*	context
*		[in] Pointer to an osm_trap_rcv_t object.
*
*	data
*		[in] Pointer to the MAD Wrapper containing the MAD
*		that contains the node's Trap attribute.
*
* RETURN VALUES
*	IB_SUCCESS if the Trap processing was successful.
*
* NOTES
*	This function processes a Trap attribute.
*
* SEE ALSO
*	Trap Receiver, Trap Response Controller
*********/

/****f* OpenSM: Trap Receiver/osm_trap_rcv_aging_tracker_callback
* NAME
*	osm_trap_rcv_aging_tracker_callback
*
* DESCRIPTION
*	Callback function called by the aging tracker mechanism.
*
* SYNOPSIS
*/
uint64_t
osm_trap_rcv_aging_tracker_callback(
  IN uint64_t key,
  IN uint32_t num_regs,
  IN void*    context );

/*
* PARAMETERS
*	key
*		[in] The key by which the event was inserted.
*
*	num_regs
*		[in] The number of times the same event (key)  was registered.
*
*	context
*		[in] Pointer to the context given in the registering of the event.
*
* RETURN VALUES
*	None.
*
* NOTES
*	This function is called by the cl_event_wheel when the aging tracker
*  event has ended.
*
* SEE ALSO
*	Trap Receiver, Trap Response Controller
*********/

END_C_DECLS

#endif	/* _OSM_TRAP_RCV_H_ */

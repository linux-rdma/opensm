/*
 * Copyright (c) 2004-2007 Voltaire, Inc. All rights reserved.
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
 * 	Declaration of osm_sm_state_mgr_t.
 *	This object represents the SM State Manager object.
 *	This object is part of the OpenSM family of objects.
 *
 * Environment:
 * 	Linux User Mode
 *
 * $Revision: 1.2 $
 */

#ifndef _OSM_SM_STATE_MGR_H_
#define _OSM_SM_STATE_MGR_H_

#include <complib/cl_passivelock.h>
#include <complib/cl_timer.h>
#include <opensm/osm_base.h>
#include <opensm/osm_subnet.h>
#include <opensm/osm_lid_mgr.h>
#include <opensm/osm_ucast_mgr.h>
#include <opensm/osm_mcast_mgr.h>
#include <opensm/osm_link_mgr.h>
#include <opensm/osm_drop_mgr.h>
#include <opensm/osm_sm_mad_ctrl.h>
#include <opensm/osm_log.h>
#include <opensm/osm_remote_sm.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/****h* OpenSM/SM State Manager
* NAME
*	SM State Manager
*
* DESCRIPTION
*	The SM State Manager object encapsulates the information
*	needed to control the state of the SM.
*
*	The SM State Manager object is thread safe.
*
*	This object should be treated as opaque and should be
*	manipulated only through the provided functions.
*
* AUTHOR
*	Yael Kalka, Mellanox
*
*********/
/****s* OpenSM: SM State Manager/osm_sm_state_mgr_t
* NAME
*	osm_sm_state_mgr_t
*
* DESCRIPTION
*	SM State Manager structure.
*
*	This object should be treated as opaque and should
*	be manipulated only through the provided functions.
*
* SYNOPSIS
*/
typedef struct _osm_sm_state_mgr {
	cl_spinlock_t state_lock;
	cl_timer_t polling_timer;
	uint32_t retry_number;
	ib_net64_t master_guid;
	osm_state_mgr_t *p_state_mgr;
	osm_subn_t *p_subn;
	osm_req_t *p_req;
	osm_log_t *p_log;
	osm_remote_sm_t *p_polling_sm;
} osm_sm_state_mgr_t;

/*
* FIELDS
*	state_lock
*		Spinlock guarding the state and processes.
*
*	polling_timer
*		Timer for polling.
*
*	retry_number
*		Used in Standby state - to count the number of retries
*		of queries to the master SM.
*
*	master_guid
*		Port GUID of master SM.
*
*	p_state_mgr
*		Pointer to the state manager object.
*
*	p_subn
*		Pointer to the Subnet object for this subnet.
*
*	p_req
*		Pointer to the generic attribute request object.
*
*	p_log
*		Pointer to the log object.
*
*	p_polling_sm
*		Pointer to a osm_remote_sm_t object. When our SM needs
*		to poll on a remote sm, this will be the pointer of the
*		polled SM.
*
* SEE ALSO
*	SM State Manager object
*********/

/****f* OpenSM: SM State Manager/osm_sm_state_mgr_construct
* NAME
*	osm_sm_state_mgr_construct
*
* DESCRIPTION
*	This function constructs a SM State Manager object.
*
* SYNOPSIS
*/
void osm_sm_state_mgr_construct(IN osm_sm_state_mgr_t * const p_sm_mgr);
/*
* PARAMETERS
*	p_sm_mgr
*		[in] Pointer to a SM State Manager object to construct.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Allows osm_sm_state_mgr_destroy
*
*	Calling osm_sm_state_mgr_construct is a prerequisite to calling any other
*	method except osm_sm_state_mgr_init.
*
* SEE ALSO
*	SM State Manager object, osm_sm_state_mgr_init,
*	osm_sm_state_mgr_destroy
*********/

/****f* OpenSM: SM State Manager/osm_sm_state_mgr_destroy
* NAME
*	osm_sm_state_mgr_destroy
*
* DESCRIPTION
*	The osm_sm_state_mgr_destroy function destroys the object, releasing
*	all resources.
*
* SYNOPSIS
*/
void osm_sm_state_mgr_destroy(IN osm_sm_state_mgr_t * const p_sm_mgr);
/*
* PARAMETERS
*	p_sm_mgr
*		[in] Pointer to the object to destroy.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Performs any necessary cleanup of the specified
*	SM State Manager object.
*	Further operations should not be attempted on the destroyed object.
*	This function should only be called after a call to
*	osm_sm_state_mgr_construct or osm_sm_state_mgr_init.
*
* SEE ALSO
*	SM State Manager object, osm_sm_state_mgr_construct,
*	osm_sm_state_mgr_init
*********/

/****f* OpenSM: SM State Manager/osm_sm_state_mgr_init
* NAME
*	osm_sm_state_mgr_init
*
* DESCRIPTION
*	The osm_sm_state_mgr_init function initializes a
*	SM State Manager object for use.
*
* SYNOPSIS
*/
ib_api_status_t
osm_sm_state_mgr_init(IN osm_sm_state_mgr_t * const p_sm_mgr,
		      IN osm_state_mgr_t * const p_state_mgr,
		      IN osm_subn_t * const p_subn,
		      IN osm_req_t * const p_req, IN osm_log_t * const p_log);
/*
* PARAMETERS
*	p_sm_mgr
*		[in] Pointer to an osm_sm_state_mgr_t object to initialize.
*
*
*  p_state_mgr
*     [in] Pointer to the State Manager object.
*
*	p_subn
*		[in] Pointer to the Subnet object for this subnet.
*
*  p_req
*		[in] Pointer to an osm_req_t object.
*
*	p_log
*		[in] Pointer to the log object.
*
* RETURN VALUES
*	IB_SUCCESS if the SM State Manager object was initialized
*	successfully.
*
* NOTES
*	Allows calling other SM State Manager methods.
*
* SEE ALSO
*	SM State Manager object, osm_sm_state_mgr_construct,
*	osm_sm_state_mgr_destroy
*********/

/****f* OpenSM: SM State Manager/osm_sm_state_mgr_process
* NAME
*	osm_sm_state_mgr_process
*
* DESCRIPTION
*	Processes and maintains the states of the SM.
*
* SYNOPSIS
*/
ib_api_status_t
osm_sm_state_mgr_process(IN osm_sm_state_mgr_t * const p_sm_mgr,
			 IN osm_sm_signal_t signal);
/*
* PARAMETERS
*	p_sm_mgr
*		[in] Pointer to an osm_sm_state_mgr_t object.
*
*	signal
*		[in] Signal to the state SM engine.
*
* RETURN VALUES
*	None.
*
* NOTES
*
* SEE ALSO
*	State Manager
*********/

/****f* OpenSM: SM State Manager/osm_sm_state_mgr_signal_master_is_alive
* NAME
*	osm_sm_state_mgr_signal_master_is_alive
*
* DESCRIPTION
*	Signals that the remote Master SM is alive.
*	Need to clear the retry_number variable.
*
* SYNOPSIS
*/
void
osm_sm_state_mgr_signal_master_is_alive(IN osm_sm_state_mgr_t * const p_sm_mgr);
/*
* PARAMETERS
*	p_sm_mgr
*		[in] Pointer to an osm_sm_state_mgr_t object.
*
* RETURN VALUES
*	None.
*
* NOTES
*
* SEE ALSO
*	State Manager
*********/

/****f* OpenSM: SM State Manager/osm_sm_state_mgr_check_legality
* NAME
*	osm_sm_state_mgr_check_legality
*
* DESCRIPTION
*	Checks the legality of the signal received, according to the
*  current state of the SM state machine.
*
* SYNOPSIS
*/
ib_api_status_t
osm_sm_state_mgr_check_legality(IN osm_sm_state_mgr_t * const p_sm_mgr,
				IN osm_sm_signal_t signal);
/*
* PARAMETERS
*	p_sm_mgr
*		[in] Pointer to an osm_sm_state_mgr_t object.
*
*	signal
*		[in] Signal to the state SM engine.
*
* RETURN VALUES
*	None.
*
* NOTES
*
* SEE ALSO
*	State Manager
*********/

END_C_DECLS
#endif				/* _OSM_SM_STATE_MGR_H_ */

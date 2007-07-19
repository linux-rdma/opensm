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
 * 	Declaration of osm_sm_t.
 *	This object represents an IBA subnet.
 *	This object is part of the OpenSM family of objects.
 *
 * Environment:
 * 	Linux User Mode
 *
 * $Revision: 1.5 $
 */

#ifndef _OSM_SM_H_
#define _OSM_SM_H_

#include <iba/ib_types.h>
#include <complib/cl_passivelock.h>
#include <complib/cl_event.h>
#include <complib/cl_thread.h>
#include <opensm/osm_stats.h>
#include <complib/cl_dispatcher.h>
#include <opensm/osm_subnet.h>
#include <opensm/osm_vl15intf.h>
#include <vendor/osm_vendor.h>
#include <opensm/osm_mad_pool.h>
#include <opensm/osm_req.h>
#include <opensm/osm_resp.h>
#include <opensm/osm_log.h>
#include <opensm/osm_node_info_rcv.h>
#include <opensm/osm_port_info_rcv.h>
#include <opensm/osm_sw_info_rcv.h>
#include <opensm/osm_node_desc_rcv.h>
#include <opensm/osm_sm_mad_ctrl.h>
#include <opensm/osm_state_mgr_ctrl.h>
#include <opensm/osm_lid_mgr.h>
#include <opensm/osm_ucast_mgr.h>
#include <opensm/osm_link_mgr.h>
#include <opensm/osm_drop_mgr.h>
#include <opensm/osm_lin_fwd_rcv.h>
#include <opensm/osm_mcast_fwd_rcv.h>
#include <opensm/osm_sweep_fail_ctrl.h>
#include <opensm/osm_sminfo_rcv.h>
#include <opensm/osm_trap_rcv.h>
#include <opensm/osm_sm_state_mgr.h>
#include <opensm/osm_slvl_map_rcv.h>
#include <opensm/osm_vl_arb_rcv.h>
#include <opensm/osm_pkey_rcv.h>
#include <opensm/osm_port.h>
#include <opensm/osm_mcast_mgr.h>
#include <opensm/osm_db.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else /* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif /* __cplusplus */

BEGIN_C_DECLS

/****h* OpenSM/SM
* NAME
*	SM
*
* DESCRIPTION
*	The SM object encapsulates the information needed by the
*	OpenSM to instantiate a subnet manager.  The OpenSM allocates
*	one SM object per subnet manager.
*
*	The SM object is thread safe.
*
*	This object should be treated as opaque and should
*	be manipulated only through the provided functions.
*
* AUTHOR
*	Steve King, Intel
*
*********/

/****s* OpenSM: SM/osm_sm_t
* NAME
*  osm_sm_t
*
* DESCRIPTION
*  Subnet Manager structure.
*
*  This object should be treated as opaque and should
*  be manipulated only through the provided functions.
*
* SYNOPSIS
*/
typedef struct _osm_sm
{
  osm_thread_state_t       thread_state;
  cl_event_t               signal;
  cl_event_t               subnet_up_event;
  cl_thread_t              sweeper;
  osm_subn_t               *p_subn;
  osm_db_t                 *p_db;
  osm_vendor_t             *p_vendor;
  osm_log_t                *p_log;
  osm_mad_pool_t           *p_mad_pool;
  osm_vl15_t               *p_vl15;
  cl_dispatcher_t          *p_disp;
  cl_plock_t               *p_lock;
  atomic32_t               sm_trans_id;
  osm_req_t                req;
  osm_resp_t               resp;
  osm_ni_rcv_t             ni_rcv;
  osm_pi_rcv_t             pi_rcv;
  osm_nd_rcv_t             nd_rcv;
  osm_sm_mad_ctrl_t        mad_ctrl;
  osm_si_rcv_t             si_rcv;
  osm_state_mgr_ctrl_t     state_mgr_ctrl;
  osm_lid_mgr_t            lid_mgr;
  osm_ucast_mgr_t          ucast_mgr;
  osm_link_mgr_t           link_mgr;
  osm_state_mgr_t          state_mgr;
  osm_drop_mgr_t           drop_mgr;
  osm_lft_rcv_t            lft_rcv;
  osm_mft_rcv_t            mft_rcv;
  osm_sweep_fail_ctrl_t    sweep_fail_ctrl;
  osm_sminfo_rcv_t         sm_info_rcv;
  osm_trap_rcv_t           trap_rcv;
  osm_sm_state_mgr_t       sm_state_mgr;
  osm_mcast_mgr_t          mcast_mgr;
  osm_slvl_rcv_t           slvl_rcv;
  osm_vla_rcv_t            vla_rcv;
  osm_pkey_rcv_t           pkey_rcv;
  cl_disp_reg_handle_t     ni_disp_h;
  cl_disp_reg_handle_t     pi_disp_h;
  cl_disp_reg_handle_t     nd_disp_h;
  cl_disp_reg_handle_t     si_disp_h;
  cl_disp_reg_handle_t     lft_disp_h;
  cl_disp_reg_handle_t     mft_disp_h;
  cl_disp_reg_handle_t     sm_info_disp_h;
  cl_disp_reg_handle_t     trap_disp_h;
  cl_disp_reg_handle_t     slvl_disp_h;
  cl_disp_reg_handle_t     vla_disp_h;
  cl_disp_reg_handle_t     pkey_disp_h;
} osm_sm_t;
/*
* FIELDS
*	p_subn
*		Pointer to the Subnet object for this subnet.
*
*  p_db
*     Pointer to the database (persistency) object
*
*	p_vendor
*		Pointer to the vendor specific interfaces object.
*
*	p_log
*		Pointer to the log object.
*
*	p_mad_pool
*		Pointer to the MAD pool.
*
*	p_vl15
*		Pointer to the VL15 interface.
*
*	req
*		Generic MAD attribute requester.
*
*	resp
*		MAD attribute responder.
*
*	nd_rcv_ctrl
*		Node Description Receive Controller.
*
*	ni_rcv_ctrl
*		Node Info Receive Controller.
*
*	pi_rcv_ctrl
*		Port Info Receive Controller.
*
*	si_rcv_ctrl
*		Switch Info Receive Controller.
*
*	nd_rcv_ctrl
*		Node Description Receive Controller.
*
*	mad_ctrl
*		MAD Controller.
*
*	smi_get_ctrl
*		SM Info Get Controller.
*
*	p_disp
*		Pointer to the Dispatcher.
*
*	p_lock
*		Pointer to the serializing lock.
*
* SEE ALSO
*	SM object
*********/

/****f* OpenSM: SM/osm_sm_construct
* NAME
*	osm_sm_construct
*
* DESCRIPTION
*	This function constructs an SM object.
*
* SYNOPSIS
*/
void
osm_sm_construct(
	IN osm_sm_t* const p_sm );
/*
* PARAMETERS
*	p_sm
*		[in] Pointer to a SM object to construct.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Allows calling osm_sm_init, osm_sm_destroy
*
*	Calling osm_sm_construct is a prerequisite to calling any other
*	method except osm_sm_init.
*
* SEE ALSO
*	SM object, osm_sm_init, osm_sm_destroy
*********/

/****f* OpenSM: SM/osm_sm_shutdown
* NAME
*	osm_sm_shutdown
*
* DESCRIPTION
*	The osm_sm_shutdown function shutdowns an SM, stopping the sweeper
*	and unregistering all messages from the dispatcher
*
* SYNOPSIS
*/
void
osm_sm_shutdown(
	IN osm_sm_t* const p_sm );
/*
* PARAMETERS
*	p_sm
*		[in] Pointer to a SM object to shutdown.
*
* RETURN VALUE
*	This function does not return a value.
*
* SEE ALSO
*	SM object, osm_sm_construct, osm_sm_init
*********/

/****f* OpenSM: SM/osm_sm_destroy
* NAME
*	osm_sm_destroy
*
* DESCRIPTION
*	The osm_sm_destroy function destroys an SM, releasing
*	all resources.
*
* SYNOPSIS
*/
void
osm_sm_destroy(
	IN osm_sm_t* const p_sm );
/*
* PARAMETERS
*	p_sm
*		[in] Pointer to a SM object to destroy.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Performs any necessary cleanup of the specified SM object.
*	Further operations should not be attempted on the destroyed object.
*	This function should only be called after a call to osm_sm_construct or
*	osm_sm_init.
*
* SEE ALSO
*	SM object, osm_sm_construct, osm_sm_init
*********/

/****f* OpenSM: SM/osm_sm_init
* NAME
*	osm_sm_init
*
* DESCRIPTION
*	The osm_sm_init function initializes a SM object for use.
*
* SYNOPSIS
*/
ib_api_status_t
osm_sm_init(
	IN osm_sm_t* const p_sm,
	IN osm_subn_t* const p_subn,
   IN osm_db_t* const p_db,
	IN osm_vendor_t* const p_vendor,
	IN osm_mad_pool_t* const p_mad_pool,
	IN osm_vl15_t* const p_vl15,
	IN osm_log_t* const p_log,
	IN osm_stats_t*	const p_stats,
	IN cl_dispatcher_t* const p_disp,
	IN cl_plock_t* const p_lock );
/*
* PARAMETERS
*	p_sm
*		[in] Pointer to an osm_sm_t object to initialize.
*
*	p_subn
*		[in] Pointer to the Subnet object for this subnet.
*
*	p_vendor
*		[in] Pointer to the vendor specific interfaces object.
*
*	p_mad_pool
*		[in] Pointer to the MAD pool.
*
*	p_vl15
*		[in] Pointer to the VL15 interface.
*
*	p_log
*		[in] Pointer to the log object.
*
*	p_stats
*		[in] Pointer to the statistics object.
*
*	p_disp
*		[in] Pointer to the OpenSM central Dispatcher.
*
*	p_lock
*		[in] Pointer to the OpenSM serializing lock.
*
* RETURN VALUES
*	IB_SUCCESS if the SM object was initialized successfully.
*
* NOTES
*	Allows calling other SM methods.
*
* SEE ALSO
*	SM object, osm_sm_construct, osm_sm_destroy
*********/

/****f* OpenSM: SM/osm_sm_sweep
* NAME
*	osm_sm_sweep
*
* DESCRIPTION
*	Initiates a subnet sweep.
*
* SYNOPSIS
*/
void
osm_sm_sweep(
	IN osm_sm_t* const p_sm );
/*
* PARAMETERS
*	p_sm
*		[in] Pointer to an osm_sm_t object.
*
* RETURN VALUES
*	IB_SUCCESS if the sweep completed successfully.
*
* NOTES
*
* SEE ALSO
*	SM object
*********/

/****f* OpenSM: SM/osm_sm_bind
* NAME
*	osm_sm_bind
*
* DESCRIPTION
*	Binds the sm object to a port guid.
*
* SYNOPSIS
*/
ib_api_status_t
osm_sm_bind(
	IN osm_sm_t* const p_sm,
	IN const ib_net64_t port_guid );
/*
* PARAMETERS
*	p_sm
*		[in] Pointer to an osm_sm_t object to bind.
*
*	port_guid
*		[in] Local port GUID with which to bind.
*
*
* RETURN VALUES
*	None
*
* NOTES
*	A given SM object can only be bound to one port at a time.
*
* SEE ALSO
*********/

/****f* OpenSM: SM/osm_sm_mcgrp_join
* NAME
*	osm_sm_mcgrp_join
*
* DESCRIPTION
*	Adds a port to the multicast group.  Creates the multicast group
*	if necessary.
*
*	This function is called by the SA.
*
* SYNOPSIS
*/
ib_api_status_t
osm_sm_mcgrp_join(
	IN osm_sm_t* const p_sm,
	IN const ib_net16_t mlid,
	IN const ib_net64_t port_guid,
   IN osm_mcast_req_type_t req_type );
/*
* PARAMETERS
*	p_sm
*		[in] Pointer to an osm_sm_t object.
*
*	mlid
*		[in] Multicast LID
*
*	port_guid
*		[in] Port GUID to add to the group.
*
*  req_type
*     [in] Type of the MC request that caused this join
*          (MC create/join).
*
* RETURN VALUES
*	None
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: SM/osm_sm_mcgrp_leave
* NAME
*	osm_sm_mcgrp_leave
*
* DESCRIPTION
*	Removes a port from the multicast group.
*
*	This function is called by the SA.
*
* SYNOPSIS
*/
ib_api_status_t
osm_sm_mcgrp_leave(
	IN osm_sm_t* const p_sm,
	IN const ib_net16_t mlid,
	IN const ib_net64_t port_guid );
/*
* PARAMETERS
*	p_sm
*		[in] Pointer to an osm_sm_t object.
*
*	mlid
*		[in] Multicast LID
*
*	port_guid
*		[in] Port GUID to remove from the group.
*
* RETURN VALUES
*	None
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: OpenSM/osm_sm_wait_for_subnet_up
* NAME
*	osm_sm_wait_for_subnet_up
*
* DESCRIPTION
*	Blocks the calling thread until the subnet is up.
*
* SYNOPSIS
*/
static inline cl_status_t
osm_sm_wait_for_subnet_up(
	IN osm_sm_t*				const p_sm,
	IN uint32_t				const wait_us,
	IN boolean_t				const interruptible )
{
	return( cl_event_wait_on( &p_sm->subnet_up_event,
			wait_us, interruptible ) );
}
/*
* PARAMETERS
*	p_sm
*		[in] Pointer to an osm_sm_t object.
*
*	wait_us
*		[in] Number of microseconds to wait.
*
*	interruptible
*		[in] Indicates whether the wait operation can be interrupted
*		by external signals.
*
* RETURN VALUES
*	CL_SUCCESS if the wait operation succeeded in response to the event
*	being set.
*
*	CL_TIMEOUT if the specified time period elapses.
*
*	CL_NOT_DONE if the wait was interrupted by an external signal.
*
*	CL_ERROR if the wait operation failed.
*
* NOTES
*
* SEE ALSO
*********/

END_C_DECLS

#endif		/* _OSM_SM_H_ */

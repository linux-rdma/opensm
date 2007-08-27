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
 *    Implementation of osm_state_mgr_t.
 * This file implements the State Manager object.
 *
 * Environment:
 *    Linux User Mode
 *
 * $Revision: 1.13 $
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <iba/ib_types.h>
#include <complib/cl_passivelock.h>
#include <complib/cl_debug.h>
#include <opensm/osm_state_mgr.h>
#include <opensm/osm_madw.h>
#include <opensm/osm_switch.h>
#include <opensm/osm_log.h>
#include <opensm/osm_subnet.h>
#include <opensm/osm_helper.h>
#include <opensm/osm_msgdef.h>
#include <opensm/osm_node.h>
#include <opensm/osm_port.h>
#include <opensm/osm_pkey_mgr.h>
#include <vendor/osm_vendor_api.h>
#include <opensm/osm_sm_state_mgr.h>
#include <opensm/osm_inform.h>
#include <opensm/osm_opensm.h>

#define SUBNET_LIST_FILENAME "/opensm-subnet.lst"

osm_signal_t osm_qos_setup(IN osm_opensm_t * p_osm);

/**********************************************************************
 **********************************************************************/
void osm_state_mgr_construct(IN osm_state_mgr_t * const p_mgr)
{
	memset(p_mgr, 0, sizeof(*p_mgr));
	cl_spinlock_construct(&p_mgr->state_lock);
	cl_spinlock_construct(&p_mgr->idle_lock);
	p_mgr->state = OSM_SM_STATE_INIT;
}

/**********************************************************************
 **********************************************************************/
void osm_state_mgr_destroy(IN osm_state_mgr_t * const p_mgr)
{
	CL_ASSERT(p_mgr);

	OSM_LOG_ENTER(p_mgr->p_log, osm_state_mgr_destroy);

	/* destroy the locks */
	cl_spinlock_destroy(&p_mgr->state_lock);
	cl_spinlock_destroy(&p_mgr->idle_lock);

	OSM_LOG_EXIT(p_mgr->p_log);
}

/**********************************************************************
 **********************************************************************/
ib_api_status_t
osm_state_mgr_init(IN osm_state_mgr_t * const p_mgr,
		   IN osm_subn_t * const p_subn,
		   IN osm_lid_mgr_t * const p_lid_mgr,
		   IN osm_ucast_mgr_t * const p_ucast_mgr,
		   IN osm_mcast_mgr_t * const p_mcast_mgr,
		   IN osm_link_mgr_t * const p_link_mgr,
		   IN osm_drop_mgr_t * const p_drop_mgr,
		   IN osm_req_t * const p_req,
		   IN osm_stats_t * const p_stats,
		   IN osm_sm_state_mgr_t * const p_sm_state_mgr,
		   IN const osm_sm_mad_ctrl_t * const p_mad_ctrl,
		   IN cl_plock_t * const p_lock,
		   IN cl_event_t * const p_subnet_up_event,
		   IN osm_log_t * const p_log)
{
	cl_status_t status;

	OSM_LOG_ENTER(p_log, osm_state_mgr_init);

	CL_ASSERT(p_subn);
	CL_ASSERT(p_lid_mgr);
	CL_ASSERT(p_ucast_mgr);
	CL_ASSERT(p_mcast_mgr);
	CL_ASSERT(p_link_mgr);
	CL_ASSERT(p_drop_mgr);
	CL_ASSERT(p_req);
	CL_ASSERT(p_stats);
	CL_ASSERT(p_sm_state_mgr);
	CL_ASSERT(p_mad_ctrl);
	CL_ASSERT(p_lock);

	osm_state_mgr_construct(p_mgr);

	p_mgr->p_log = p_log;
	p_mgr->p_subn = p_subn;
	p_mgr->p_lid_mgr = p_lid_mgr;
	p_mgr->p_ucast_mgr = p_ucast_mgr;
	p_mgr->p_mcast_mgr = p_mcast_mgr;
	p_mgr->p_link_mgr = p_link_mgr;
	p_mgr->p_drop_mgr = p_drop_mgr;
	p_mgr->p_mad_ctrl = p_mad_ctrl;
	p_mgr->p_req = p_req;
	p_mgr->p_stats = p_stats;
	p_mgr->p_sm_state_mgr = p_sm_state_mgr;
	p_mgr->state = OSM_SM_STATE_IDLE;
	p_mgr->p_lock = p_lock;
	p_mgr->p_subnet_up_event = p_subnet_up_event;

	status = cl_spinlock_init(&p_mgr->state_lock);
	if (status != CL_SUCCESS) {
		osm_log(p_mgr->p_log, OSM_LOG_ERROR,
			"osm_state_mgr_init: ERR 3301: "
			"Spinlock init failed (%s)\n", CL_STATUS_MSG(status));
	}

	cl_qlist_init(&p_mgr->idle_time_list);

	status = cl_spinlock_init(&p_mgr->idle_lock);
	if (status != CL_SUCCESS) {
		osm_log(p_mgr->p_log, OSM_LOG_ERROR,
			"osm_state_mgr_init: ERR 3302: "
			"Spinlock init failed (%s)\n", CL_STATUS_MSG(status));
	}

	OSM_LOG_EXIT(p_mgr->p_log);
	return (status);
}

/**********************************************************************
 **********************************************************************/
static void __osm_state_mgr_up_msg(IN const osm_state_mgr_t * p_mgr)
{
	/*
	 * This message should be written only once - when the
	 * SM moves to Master state and the subnet is up for
	 * the first time. The change of state is marked with
	 * the subnet flag moved_to_master_state
	 */
	if (p_mgr->p_subn->moved_to_master_state == TRUE) {
		osm_log(p_mgr->p_log, OSM_LOG_SYS, "SUBNET UP\n");	/* Format Waived */
		/* clear the signal */
		p_mgr->p_subn->moved_to_master_state = FALSE;
	} else {
		osm_log(p_mgr->p_log, OSM_LOG_INFO, "SUBNET UP\n");	/* Format Waived */
	}

	if (p_mgr->p_subn->opt.sweep_interval) {
		osm_log(p_mgr->p_log, OSM_LOG_VERBOSE,
			"__osm_state_mgr_up_msg: "
			"\n\n\n********************************"
			"**********************************\n"
			"**************************** SUBNET UP "
			"***************************\n"
			"**************************************"
			"****************************\n\n\n");
	} else {
		osm_log(p_mgr->p_log, OSM_LOG_VERBOSE,
			"__osm_state_mgr_up_msg: "
			"\n\n\n********************************"
			"**********************************\n"
			"******************* SUBNET UP "
			"(sweep disabled) *******************\n"
			"**************************************"
			"****************************\n\n\n");
	}
}

/**********************************************************************
 **********************************************************************/
static void __osm_state_mgr_init_errors_msg(IN const osm_state_mgr_t * p_mgr)
{
	osm_log(p_mgr->p_log, OSM_LOG_SYS, "Errors during initialization\n");	/* Format Waived */

	osm_log(p_mgr->p_log, OSM_LOG_ERROR,
		"__osm_state_mgr_init_errors_msg: "
		"\n\n\n********************************"
		"**********************************\n"
		"****************** ERRORS DURING INITI"
		"ALIZATION ******************\n"
		"**************************************"
		"****************************\n\n\n");
}

/**********************************************************************
 **********************************************************************/
static void __osm_state_mgr_light_sweep_done_msg(IN const osm_state_mgr_t *
						 p_mgr)
{
	if (osm_log_is_active(p_mgr->p_log, OSM_LOG_VERBOSE)) {
		osm_log(p_mgr->p_log, OSM_LOG_VERBOSE,
			"__osm_state_mgr_light_sweep_done_msg: "
			"\n\n\n********************************"
			"**********************************\n"
			"********************** LIGHT SWEEP "
			"COMPLETE **********************\n"
			"**************************************"
			"****************************\n\n\n");
	}
}

/**********************************************************************
 **********************************************************************/
static void __osm_state_mgr_standby_msg(IN const osm_state_mgr_t * p_mgr)
{
	if (osm_log_is_active(p_mgr->p_log, OSM_LOG_VERBOSE)) {
		osm_log(p_mgr->p_log, OSM_LOG_VERBOSE,
			"__osm_state_mgr_standby_msg: "
			"\n\n\n********************************"
			"**********************************\n"
			"******************** ENTERING STANDBY"
			" STATE **********************\n"
			"**************************************"
			"****************************\n\n\n");
	}
}

/**********************************************************************
 **********************************************************************/
static void __osm_state_mgr_sm_port_down_msg(IN const osm_state_mgr_t * p_mgr)
{
	osm_log(p_mgr->p_log, OSM_LOG_SYS, "SM port is down\n");	/* Format Waived */

	if (osm_log_is_active(p_mgr->p_log, OSM_LOG_VERBOSE)) {
		osm_log(p_mgr->p_log, OSM_LOG_VERBOSE,
			"__osm_state_mgr_sm_port_down_msg: "
			"\n\n\n********************************"
			"**********************************\n"
			"************************** SM PORT DOWN "
			"**************************\n"
			"**************************************"
			"****************************\n\n\n");
	}
}

/**********************************************************************
 **********************************************************************/
static void __osm_state_mgr_lid_assign_msg(IN const osm_state_mgr_t * p_mgr)
{
	if (osm_log_is_active(p_mgr->p_log, OSM_LOG_VERBOSE)) {
		osm_log(p_mgr->p_log, OSM_LOG_VERBOSE,
			"__osm_state_mgr_lid_assign_msg: "
			"\n\n\n**************************************"
			"****************************\n"
			"***** LID ASSIGNMENT COMPLETE - STARTING SWITC"
			"H TABLE CONFIG *****\n"
			"*********************************************"
			"*********************\n\n\n");
	}
}

/**********************************************************************
 **********************************************************************/
static void __osm_state_mgr_set_sm_lid_done_msg(IN const osm_state_mgr_t *
						p_mgr)
{
	if (osm_log_is_active(p_mgr->p_log, OSM_LOG_VERBOSE)) {
		osm_log(p_mgr->p_log, OSM_LOG_VERBOSE,
			"__osm_state_mgr_set_sm_lid_done_msg: "
			"\n\n\n**************************************"
			"****************************\n"
			"**** SM LID ASSIGNMENT COMPLETE - STARTING SUBN"
			"ET LID CONFIG *****\n"
			"*********************************************"
			"*********************\n\n\n");
	}
}

/**********************************************************************
 **********************************************************************/
static void __osm_state_mgr_switch_config_msg(IN const osm_state_mgr_t * p_mgr)
{
	if (osm_log_is_active(p_mgr->p_log, OSM_LOG_VERBOSE)) {
		osm_log(p_mgr->p_log, OSM_LOG_VERBOSE,
			"__osm_state_mgr_switch_config_msg: "
			"\n\n\n**************************************"
			"****************************\n"
			"***************** SWITCHES CONFIGURED FOR UNICAST "
			"****************\n"
			"*********************************************"
			"*********************\n\n\n");
	}
}

/**********************************************************************
 **********************************************************************/
static void __osm_state_mgr_multicast_config_msg(IN const osm_state_mgr_t *
						 p_mgr)
{
	if (osm_log_is_active(p_mgr->p_log, OSM_LOG_VERBOSE)) {
		osm_log(p_mgr->p_log, OSM_LOG_VERBOSE,
			"__osm_state_mgr_multicast_config_msg: "
			"\n\n\n**************************************"
			"****************************\n"
			"**************** SWITCHES CONFIGURED FOR MULTICAST "
			"***************\n"
			"*********************************************"
			"*********************\n\n\n");
	}
}

/**********************************************************************
 **********************************************************************/
static void __osm_state_mgr_links_ports_msg(IN const osm_state_mgr_t * p_mgr)
{
	if (osm_log_is_active(p_mgr->p_log, OSM_LOG_VERBOSE)) {
		osm_log(p_mgr->p_log, OSM_LOG_VERBOSE,
			"__osm_state_mgr_links_ports_msg: "
			"\n\n\n**************************************"
			"****************************\n"
			"******* LINKS PORTS CONFIGURED - SET LINKS TO ARMED "
			"STATE ********\n"
			"*********************************************"
			"*********************\n\n\n");
	}
}

/**********************************************************************
 **********************************************************************/
static void __osm_state_mgr_links_armed_msg(IN const osm_state_mgr_t * p_mgr)
{
	if (osm_log_is_active(p_mgr->p_log, OSM_LOG_VERBOSE)) {
		osm_log(p_mgr->p_log, OSM_LOG_VERBOSE,
			"__osm_state_mgr_links_armed_msg: "
			"\n\n\n**************************************"
			"****************************\n"
			"************* LINKS ARMED - SET LINKS TO ACTIVE "
			"STATE ************\n"
			"*********************************************"
			"*********************\n\n\n");
	}
}

/**********************************************************************
 **********************************************************************/
static void __osm_state_mgr_sweep_heavy_msg(IN const osm_state_mgr_t * p_mgr)
{
	if (osm_log_is_active(p_mgr->p_log, OSM_LOG_VERBOSE)) {
		osm_log(p_mgr->p_log, OSM_LOG_VERBOSE,
			"__osm_state_mgr_sweep_heavy_msg: "
			"\n\n\n**************************************"
			"****************************\n"
			"******************** INITIATING HEAVY SWEEP "
			"**********************\n"
			"*********************************************"
			"*********************\n\n\n");
	}
}

/**********************************************************************
 **********************************************************************/
static void __osm_state_mgr_sweep_heavy_done_msg(IN const osm_state_mgr_t *
						 p_mgr)
{
	if (osm_log_is_active(p_mgr->p_log, OSM_LOG_VERBOSE)) {
		osm_log(p_mgr->p_log, OSM_LOG_VERBOSE,
			"__osm_state_mgr_sweep_heavy_done_msg: "
			"\n\n\n**************************************"
			"****************************\n"
			"********************* HEAVY SWEEP COMPLETE "
			"***********************\n"
			"*********************************************"
			"*********************\n\n\n");
	}
}

/**********************************************************************
 **********************************************************************/
static void __osm_state_mgr_sweep_light_msg(IN const osm_state_mgr_t * p_mgr)
{
	if (osm_log_is_active(p_mgr->p_log, OSM_LOG_VERBOSE)) {
		osm_log(p_mgr->p_log, OSM_LOG_VERBOSE,
			"__osm_state_mgr_sweep_light_msg: "
			"\n\n\n**************************************"
			"****************************\n"
			"******************** INITIATING LIGHT SWEEP "
			"**********************\n"
			"*********************************************"
			"*********************\n\n\n");
	}
}

/**********************************************************************
 **********************************************************************/
static void
__osm_state_mgr_signal_warning(IN const osm_state_mgr_t * const p_mgr,
			       IN const osm_signal_t signal)
{
	osm_log(p_mgr->p_log, OSM_LOG_VERBOSE,
		"__osm_state_mgr_signal_warning: "
		"Invalid signal %s(%lu) in state %s\n",
		osm_get_sm_signal_str(signal), signal,
		osm_get_sm_state_str(p_mgr->state));
}

/**********************************************************************
 **********************************************************************/
static void
__osm_state_mgr_signal_error(IN const osm_state_mgr_t * const p_mgr,
			     IN const osm_signal_t signal)
{
	/* the Request for IDLE processing can come async to the state so it
	 * really is just verbose ... */
	if (signal == OSM_SIGNAL_IDLE_TIME_PROCESS_REQUEST)
		__osm_state_mgr_signal_warning(p_mgr, signal);
	else
		osm_log(p_mgr->p_log, OSM_LOG_ERROR,
			"__osm_state_mgr_signal_error: ERR 3303: "
			"Invalid signal %s(%lu) in state %s\n",
			osm_get_sm_signal_str(signal), signal,
			osm_get_sm_state_str(p_mgr->state));
}

/**********************************************************************
 **********************************************************************/
static void __osm_state_mgr_reset_node_count(IN cl_map_item_t *
					     const p_map_item, IN void *context)
{
	osm_node_t *p_node = (osm_node_t *) p_map_item;
	osm_state_mgr_t *const p_mgr = (osm_state_mgr_t *) context;

	if (osm_log_is_active(p_mgr->p_log, OSM_LOG_DEBUG)) {
		osm_log(p_mgr->p_log, OSM_LOG_DEBUG,
			"__osm_state_mgr_reset_node_count: "
			"Resetting discovery count for node 0x%" PRIx64 "\n",
			cl_ntoh64(osm_node_get_node_guid(p_node)));
	}

	p_node->discovery_count = 0;
}

/**********************************************************************
 **********************************************************************/
static void __osm_state_mgr_reset_port_count(IN cl_map_item_t *
					     const p_map_item, IN void *context)
{
	osm_port_t *p_port = (osm_port_t *) p_map_item;
	osm_state_mgr_t *const p_mgr = (osm_state_mgr_t *) context;

	if (osm_log_is_active(p_mgr->p_log, OSM_LOG_DEBUG)) {
		osm_log(p_mgr->p_log, OSM_LOG_DEBUG,
			"__osm_state_mgr_reset_port_count: "
			"Resetting discovery count for port 0x%" PRIx64 "\n",
			cl_ntoh64(osm_port_get_guid(p_port)));
	}

	p_port->discovery_count = 0;
}

/**********************************************************************
 **********************************************************************/
static void
__osm_state_mgr_reset_switch_count(IN cl_map_item_t * const p_map_item,
				   IN void *context)
{
	osm_switch_t *p_sw = (osm_switch_t *) p_map_item;
	osm_state_mgr_t *const p_mgr = (osm_state_mgr_t *) context;

	if (osm_log_is_active(p_mgr->p_log, OSM_LOG_DEBUG)) {
		osm_log(p_mgr->p_log, OSM_LOG_DEBUG,
			"__osm_state_mgr_reset_switch_count: "
			"Resetting discovery count for switch 0x%" PRIx64 "\n",
			cl_ntoh64(osm_node_get_node_guid(p_sw->p_node)));
	}

	p_sw->discovery_count = 0;
	p_sw->need_update = 1;
}

/**********************************************************************
 **********************************************************************/
static void __osm_state_mgr_get_sw_info(IN cl_map_item_t * const p_object,
					IN void *context)
{
	osm_node_t *p_node;
	osm_dr_path_t *p_dr_path;
	osm_madw_context_t mad_context;
	osm_switch_t *const p_sw = (osm_switch_t *) p_object;
	osm_state_mgr_t *const p_mgr = (osm_state_mgr_t *) context;
	ib_api_status_t status;

	OSM_LOG_ENTER(p_mgr->p_log, __osm_state_mgr_get_sw_info);

	p_node = p_sw->p_node;
	p_dr_path = osm_node_get_any_dr_path_ptr(p_node);

	memset(&mad_context, 0, sizeof(mad_context));

	mad_context.si_context.node_guid = osm_node_get_node_guid(p_node);
	mad_context.si_context.set_method = FALSE;
	mad_context.si_context.light_sweep = TRUE;

	status = osm_req_get(p_mgr->p_req,
			     p_dr_path,
			     IB_MAD_ATTR_SWITCH_INFO, 0,
			     OSM_MSG_LIGHT_SWEEP_FAIL, &mad_context);

	if (status != IB_SUCCESS) {
		osm_log(p_mgr->p_log, OSM_LOG_ERROR,
			"__osm_state_mgr_get_sw_info: ERR 3304: "
			"Request for SwitchInfo failed\n");
	}

	OSM_LOG_EXIT(p_mgr->p_log);
}

/**********************************************************************
 Initiate a remote port info request for the given physical port
 **********************************************************************/
static void
__osm_state_mgr_get_remote_port_info(IN osm_state_mgr_t * const p_mgr,
				     IN osm_physp_t * const p_physp)
{
	osm_dr_path_t *p_dr_path;
	osm_dr_path_t rem_node_dr_path;
	osm_madw_context_t mad_context;
	ib_api_status_t status;

	OSM_LOG_ENTER(p_mgr->p_log, __osm_state_mgr_get_remote_port_info);

	/* generate a dr path leaving on the physp to the remote node */
	p_dr_path = osm_physp_get_dr_path_ptr(p_physp);
	memcpy(&rem_node_dr_path, p_dr_path, sizeof(osm_dr_path_t));
	osm_dr_path_extend(&rem_node_dr_path, osm_physp_get_port_num(p_physp));

	memset(&mad_context, 0, sizeof(mad_context));

	mad_context.pi_context.node_guid =
	    osm_node_get_node_guid(osm_physp_get_node_ptr(p_physp));
	mad_context.pi_context.port_guid =
	    cl_hton64(osm_physp_get_port_num(p_physp));
	mad_context.pi_context.set_method = FALSE;
	mad_context.pi_context.light_sweep = TRUE;
	mad_context.pi_context.ignore_errors = FALSE;
	mad_context.pi_context.update_master_sm_base_lid = FALSE;
	mad_context.pi_context.active_transition = FALSE;

	/* note that with some negative logic - if the query failed it means that
	 * there is no point in going to heavy sweep */
	status = osm_req_get(p_mgr->p_req,
			     &rem_node_dr_path,
			     IB_MAD_ATTR_PORT_INFO, 0, CL_DISP_MSGID_NONE,
			     &mad_context);

	if (status != IB_SUCCESS) {
		osm_log(p_mgr->p_log, OSM_LOG_ERROR,
			"__osm_state_mgr_get_remote_port_info: ERR 332E: "
			"Request for PortInfo failed\n");
	}

	OSM_LOG_EXIT(p_mgr->p_log);
}

/**********************************************************************
 Initiates a thorough sweep of the subnet.
 Used when there is suspicion that something on the subnet has changed.
**********************************************************************/
static ib_api_status_t __osm_state_mgr_sweep_hop_0(IN osm_state_mgr_t *
						   const p_mgr)
{
	ib_api_status_t status;
	osm_dr_path_t dr_path;
	osm_bind_handle_t h_bind;
	uint8_t path_array[IB_SUBNET_PATH_HOPS_MAX];

	OSM_LOG_ENTER(p_mgr->p_log, __osm_state_mgr_sweep_hop_0);

	memset(path_array, 0, sizeof(path_array));

	/*
	 * First, get the bind handle.
	 */
	h_bind = osm_sm_mad_ctrl_get_bind_handle(p_mgr->p_mad_ctrl);
	if (h_bind != OSM_BIND_INVALID_HANDLE) {
		__osm_state_mgr_sweep_heavy_msg(p_mgr);

		/*
		 * Start the sweep by clearing the port counts, then
		 * get our own NodeInfo at 0 hops.
		 */
		CL_PLOCK_ACQUIRE(p_mgr->p_lock);

		cl_qmap_apply_func(&p_mgr->p_subn->node_guid_tbl,
				   __osm_state_mgr_reset_node_count, p_mgr);

		cl_qmap_apply_func(&p_mgr->p_subn->port_guid_tbl,
				   __osm_state_mgr_reset_port_count, p_mgr);

		cl_qmap_apply_func(&p_mgr->p_subn->sw_guid_tbl,
				   __osm_state_mgr_reset_switch_count, p_mgr);

		/* Set the in_sweep_hop_0 flag in subn to be TRUE.
		 * This will indicate the sweeping not to continue beyond the
		 * the current node.
		 * This is relevant for the case of SM on switch, since in the
		 * switch info we need to signal somehow not to continue
		 * the sweeping. */
		p_mgr->p_subn->in_sweep_hop_0 = TRUE;

		CL_PLOCK_RELEASE(p_mgr->p_lock);

		osm_dr_path_init(&dr_path, h_bind, 0, path_array);
		status = osm_req_get(p_mgr->p_req,
				     &dr_path, IB_MAD_ATTR_NODE_INFO, 0,
				     CL_DISP_MSGID_NONE, NULL);

		if (status != IB_SUCCESS) {
			osm_log(p_mgr->p_log, OSM_LOG_ERROR,
				"__osm_state_mgr_sweep_hop_0: ERR 3305: "
				"Request for NodeInfo failed\n");
		}
	} else {
		osm_log(p_mgr->p_log, OSM_LOG_DEBUG,
			"__osm_state_mgr_sweep_hop_0: "
			"No bound ports. Deferring sweep...\n");
		status = IB_INVALID_STATE;
	}

	OSM_LOG_EXIT(p_mgr->p_log);
	return (status);
}

/**********************************************************************
 Clear out all existing port lid assignments
**********************************************************************/
static ib_api_status_t __osm_state_mgr_clean_known_lids(IN osm_state_mgr_t *
							const p_mgr)
{
	ib_api_status_t status = IB_SUCCESS;
	cl_ptr_vector_t *p_vec = &(p_mgr->p_subn->port_lid_tbl);
	uint32_t i;

	OSM_LOG_ENTER(p_mgr->p_log, __osm_state_mgr_clean_known_lids);

	/* we need a lock here! */
	CL_PLOCK_ACQUIRE(p_mgr->p_lock);

	for (i = 0; i < cl_ptr_vector_get_size(p_vec); i++)
		cl_ptr_vector_set(p_vec, i, NULL);

	CL_PLOCK_RELEASE(p_mgr->p_lock);

	OSM_LOG_EXIT(p_mgr->p_log);
	return (status);
}

/**********************************************************************
 Notifies the transport layer that the local LID has changed,
 which give it a chance to update address vectors, etc..
**********************************************************************/
static ib_api_status_t __osm_state_mgr_notify_lid_change(IN osm_state_mgr_t *
							 const p_mgr)
{
	ib_api_status_t status;
	osm_bind_handle_t h_bind;

	OSM_LOG_ENTER(p_mgr->p_log, __osm_state_mgr_notify_lid_change);

	/*
	 * First, get the bind handle.
	 */
	h_bind = osm_sm_mad_ctrl_get_bind_handle(p_mgr->p_mad_ctrl);
	if (h_bind == OSM_BIND_INVALID_HANDLE) {
		osm_log(p_mgr->p_log, OSM_LOG_ERROR,
			"__osm_state_mgr_notify_lid_change: ERR 3306: "
			"No bound ports\n");
		status = IB_ERROR;
		goto Exit;
	}

	/*
	 * Notify the transport layer that we changed the local LID.
	 */
	status = osm_vendor_local_lid_change(h_bind);
	if (status != IB_SUCCESS) {
		osm_log(p_mgr->p_log, OSM_LOG_ERROR,
			"__osm_state_mgr_notify_lid_change: ERR 3307: "
			"Vendor LID update failed (%s)\n",
			ib_get_err_str(status));
	}

      Exit:
	OSM_LOG_EXIT(p_mgr->p_log);
	return (status);
}

/**********************************************************************
 Returns true if the SM port is down.
 The SM's port object must exist in the port_guid table.
**********************************************************************/
static boolean_t __osm_state_mgr_is_sm_port_down(IN osm_state_mgr_t *
						 const p_mgr)
{
	ib_net64_t port_guid;
	osm_port_t *p_port;
	osm_physp_t *p_physp;
	uint8_t state;

	OSM_LOG_ENTER(p_mgr->p_log, __osm_state_mgr_is_sm_port_down);

	port_guid = p_mgr->p_subn->sm_port_guid;

	/*
	 * If we don't know our own port guid yet, assume the port is down.
	 */
	if (port_guid == 0) {
		osm_log(p_mgr->p_log, OSM_LOG_ERROR,
			"__osm_state_mgr_is_sm_port_down: ERR 3308: "
			"SM port GUID unknown\n");
		state = IB_LINK_DOWN;
		goto Exit;
	}

	CL_ASSERT(port_guid);

	CL_PLOCK_ACQUIRE(p_mgr->p_lock);
	p_port = osm_get_port_by_guid(p_mgr->p_subn, port_guid);
	if (!p_port) {
		osm_log(p_mgr->p_log, OSM_LOG_ERROR,
			"__osm_state_mgr_is_sm_port_down: ERR 3309: "
			"SM port with GUID:%016" PRIx64 " is unknown\n",
			cl_ntoh64(port_guid));
		state = IB_LINK_DOWN;
		CL_PLOCK_RELEASE(p_mgr->p_lock);
		goto Exit;
	}

	p_physp = p_port->p_physp;

	CL_ASSERT(p_physp);
	CL_ASSERT(osm_physp_is_valid(p_physp));

	state = osm_physp_get_port_state(p_physp);
	CL_PLOCK_RELEASE(p_mgr->p_lock);

      Exit:
	OSM_LOG_EXIT(p_mgr->p_log);
	return (state == IB_LINK_DOWN);
}

/**********************************************************************
 Sweeps the node 1 hop away.
 This sets off a "chain reaction" that causes discovery of the subnet.
 Used when there is suspicion that something on the subnet has changed.
**********************************************************************/
static ib_api_status_t __osm_state_mgr_sweep_hop_1(IN osm_state_mgr_t *
						   const p_mgr)
{
	ib_api_status_t status = IB_SUCCESS;
	osm_bind_handle_t h_bind;
	osm_madw_context_t context;
	osm_node_t *p_node;
	osm_port_t *p_port;
	osm_physp_t *p_physp;
	osm_dr_path_t *p_dr_path;
	osm_dr_path_t hop_1_path;
	ib_net64_t port_guid;
	uint8_t port_num;
	uint8_t path_array[IB_SUBNET_PATH_HOPS_MAX];
	uint8_t num_ports;
	osm_physp_t *p_ext_physp;

	OSM_LOG_ENTER(p_mgr->p_log, __osm_state_mgr_sweep_hop_1);

	/*
	 * First, get our own port and node objects.
	 */
	port_guid = p_mgr->p_subn->sm_port_guid;

	CL_ASSERT(port_guid);

	/* Set the in_sweep_hop_0 flag in subn to be FALSE.
	 * This will indicate the sweeping to continue beyond the
	 * the current node.
	 * This is relevant for the case of SM on switch, since in the
	 * switch info we need to signal that the sweeping should
	 * continue through the switch. */
	p_mgr->p_subn->in_sweep_hop_0 = FALSE;

	p_port = osm_get_port_by_guid(p_mgr->p_subn, port_guid);
	if (!p_port) {
		osm_log(p_mgr->p_log, OSM_LOG_ERROR,
			"__osm_state_mgr_sweep_hop_1: ERR 3310: "
			"No SM port object\n");
		status = IB_ERROR;
		goto Exit;
	}

	p_node = p_port->p_node;
	CL_ASSERT(p_node);

	port_num = ib_node_info_get_local_port_num(&p_node->node_info);

	osm_log(p_mgr->p_log, OSM_LOG_DEBUG,
		"__osm_state_mgr_sweep_hop_1: "
		"Probing hop 1 on local port %u\n", port_num);

	p_physp = osm_node_get_physp_ptr(p_node, port_num);

	CL_ASSERT(osm_physp_is_valid(p_physp));

	p_dr_path = osm_physp_get_dr_path_ptr(p_physp);
	h_bind = osm_dr_path_get_bind_handle(p_dr_path);

	CL_ASSERT(h_bind != OSM_BIND_INVALID_HANDLE);

	memset(path_array, 0, sizeof(path_array));
	/* the hop_1 operations depend on the type of our node.
	 * Currently - legal nodes that can host SM are SW and CA */
	switch (osm_node_get_type(p_node)) {
	case IB_NODE_TYPE_CA:
	case IB_NODE_TYPE_ROUTER:
		memset(&context, 0, sizeof(context));
		context.ni_context.node_guid = osm_node_get_node_guid(p_node);
		context.ni_context.port_num = port_num;

		path_array[1] = port_num;

		osm_dr_path_init(&hop_1_path, h_bind, 1, path_array);
		status = osm_req_get(p_mgr->p_req,
				     &hop_1_path,
				     IB_MAD_ATTR_NODE_INFO, 0,
				     CL_DISP_MSGID_NONE, &context);

		if (status != IB_SUCCESS) {
			osm_log(p_mgr->p_log, OSM_LOG_ERROR,
				"__osm_state_mgr_sweep_hop_1: ERR 3311: "
				"Request for NodeInfo failed\n");
		}
		break;

	case IB_NODE_TYPE_SWITCH:
		/* Need to go over all the ports of the switch, and send a node_info
		 * from them. This doesn't include the port 0 of the switch, which
		 * hosts the SM.
		 * Note: We'll send another switchInfo on port 0, since if no ports
		 * are connected, we still want to get some response, and have the
		 * subnet come up.
		 */
		num_ports = osm_node_get_num_physp(p_node);
		for (port_num = 0; port_num < num_ports; port_num++) {
			/* go through the port only if the port is not DOWN */
			p_ext_physp = osm_node_get_physp_ptr(p_node, port_num);
			if (ib_port_info_get_port_state
			    (&(p_ext_physp->port_info)) > IB_LINK_DOWN) {
				memset(&context, 0, sizeof(context));
				context.ni_context.node_guid =
				    osm_node_get_node_guid(p_node);
				context.ni_context.port_num = port_num;

				path_array[1] = port_num;

				osm_dr_path_init(&hop_1_path, h_bind, 1,
						 path_array);
				status =
				    osm_req_get(p_mgr->p_req, &hop_1_path,
						IB_MAD_ATTR_NODE_INFO, 0,
						CL_DISP_MSGID_NONE, &context);

				if (status != IB_SUCCESS) {
					osm_log(p_mgr->p_log, OSM_LOG_ERROR,
						"__osm_state_mgr_sweep_hop_1: ERR 3312: "
						"Request for NodeInfo failed\n");
				}
			}
		}
		break;

	default:
		osm_log(p_mgr->p_log, OSM_LOG_ERROR,
			"__osm_state_mgr_sweep_hop_1: ERR 3313: Unknown node type %d\n",
			osm_node_get_type(p_node));
	}

      Exit:
	OSM_LOG_EXIT(p_mgr->p_log);
	return (status);
}

/**********************************************************************
 Initiates a lightweight sweep of the subnet.
 Used during normal sweeps after the subnet is up.
**********************************************************************/
static ib_api_status_t __osm_state_mgr_light_sweep_start(IN osm_state_mgr_t *
							 const p_mgr)
{
	ib_api_status_t status = IB_SUCCESS;
	osm_bind_handle_t h_bind;
	cl_qmap_t *p_sw_tbl;
	cl_map_item_t *p_next;
	osm_node_t *p_node;
	osm_physp_t *p_physp;
	uint8_t port_num;

	OSM_LOG_ENTER(p_mgr->p_log, __osm_state_mgr_light_sweep_start);

	p_sw_tbl = &p_mgr->p_subn->sw_guid_tbl;

	/*
	 * First, get the bind handle.
	 */
	h_bind = osm_sm_mad_ctrl_get_bind_handle(p_mgr->p_mad_ctrl);
	if (h_bind != OSM_BIND_INVALID_HANDLE) {
		__osm_state_mgr_sweep_light_msg(p_mgr);
		CL_PLOCK_ACQUIRE(p_mgr->p_lock);
		cl_qmap_apply_func(p_sw_tbl, __osm_state_mgr_get_sw_info,
				   p_mgr);
		CL_PLOCK_RELEASE(p_mgr->p_lock);

		/* now scan the list of physical ports that were not down but have no remote port */
		CL_PLOCK_ACQUIRE(p_mgr->p_lock);
		p_next = cl_qmap_head(&p_mgr->p_subn->node_guid_tbl);
		while (p_next != cl_qmap_end(&p_mgr->p_subn->node_guid_tbl)) {
			p_node = (osm_node_t *) p_next;
			p_next = cl_qmap_next(p_next);

			for (port_num = 1;
			     port_num < osm_node_get_num_physp(p_node);
			     port_num++) {
				p_physp =
				    osm_node_get_physp_ptr(p_node, port_num);
				if (osm_physp_is_valid(p_physp)
				    && (osm_physp_get_port_state(p_physp) !=
					IB_LINK_DOWN)
				    && !osm_physp_get_remote(p_physp)) {
					osm_log(p_mgr->p_log, OSM_LOG_ERROR,
						"__osm_state_mgr_light_sweep_start: ERR 0108: "
						"Unknown remote side for node 0x%016"
						PRIx64
						" port %u. Adding to light sweep sampling list\n",
						cl_ntoh64(osm_node_get_node_guid
							  (p_node)), port_num);

					osm_dump_dr_path(p_mgr->p_log,
							 osm_physp_get_dr_path_ptr
							 (p_physp),
							 OSM_LOG_ERROR);

					__osm_state_mgr_get_remote_port_info
					    (p_mgr, p_physp);
				}
			}
		}
		CL_PLOCK_RELEASE(p_mgr->p_lock);
	} else {
		osm_log(p_mgr->p_log, OSM_LOG_DEBUG,
			"__osm_state_mgr_light_sweep_start: "
			"No bound ports. Deferring sweep...\n");
		status = IB_INVALID_STATE;
	}

	OSM_LOG_EXIT(p_mgr->p_log);
	return (status);
}

/**********************************************************************
 **********************************************************************/
static void __osm_topology_file_create(IN osm_state_mgr_t * const p_mgr)
{
	const osm_node_t *p_node;
	char *file_name;
	FILE *rc;

	OSM_LOG_ENTER(p_mgr->p_log, __osm_topology_file_create);

	CL_PLOCK_ACQUIRE(p_mgr->p_lock);

	file_name = (char *)malloc(strlen(p_mgr->p_subn->opt.dump_files_dir)
				   + strlen(SUBNET_LIST_FILENAME) + 1);

	CL_ASSERT(file_name);

	strcpy(file_name, p_mgr->p_subn->opt.dump_files_dir);
	strcat(file_name, SUBNET_LIST_FILENAME);

	if ((rc = fopen(file_name, "w")) == NULL) {
		osm_log(p_mgr->p_log, OSM_LOG_DEBUG,
			"__osm_topology_file_create: "
			"fopen failed for file:%s\n", file_name);

		CL_PLOCK_RELEASE(p_mgr->p_lock);
		goto Exit;
	}

	p_node = (osm_node_t *) cl_qmap_head(&p_mgr->p_subn->node_guid_tbl);
	while (p_node !=
	       (osm_node_t *) cl_qmap_end(&p_mgr->p_subn->node_guid_tbl)) {
		if (p_node->node_info.num_ports) {
			uint32_t cPort;
			osm_node_t *p_nbnode;
			osm_physp_t *p_physp;
			osm_physp_t *p_default_physp;
			osm_physp_t *p_rphysp;
			uint8_t link_speed_act;

			for (cPort = 1; cPort < osm_node_get_num_physp(p_node);
			     cPort++) {
				uint8_t port_state;

				p_physp = osm_node_get_physp_ptr(p_node, cPort);

				if (!osm_physp_is_valid(p_physp))
					continue;

				p_rphysp = p_physp->p_remote_physp;

				if ((p_rphysp == NULL)
				    || (!osm_physp_is_valid(p_rphysp)))
					continue;

				CL_ASSERT(cPort == p_physp->port_num);

				if (p_node->node_info.node_type ==
				    IB_NODE_TYPE_SWITCH) {
					p_default_physp =
					    osm_node_get_physp_ptr(p_node, 0);
				} else {
					p_default_physp = p_physp;
				}

				fprintf(rc, "{ %s%s Ports:%02X"
					" SystemGUID:%016" PRIx64
					" NodeGUID:%016" PRIx64
					" PortGUID:%016" PRIx64
					" VenID:%06X DevID:%04X Rev:%08X {%s} LID:%04X PN:%02X } ",
					(p_node->node_info.node_type ==
					 IB_NODE_TYPE_SWITCH) ? "SW" : (p_node->
									node_info.
									node_type
									==
									IB_NODE_TYPE_CA)
					? "CA" : (p_node->node_info.node_type ==
						  IB_NODE_TYPE_ROUTER) ? "Rt" :
					"**",
					(p_default_physp->port_info.base_lid ==
					 p_default_physp->port_info.
					 master_sm_base_lid) ? "-SM" : "",
					p_node->node_info.num_ports,
					cl_ntoh64(p_node->node_info.sys_guid),
					cl_ntoh64(p_node->node_info.node_guid),
					cl_ntoh64(p_physp->port_guid),
					cl_ntoh32(ib_node_info_get_vendor_id
						  (&p_node->node_info)),
					cl_ntoh16(p_node->node_info.device_id),
					cl_ntoh32(p_node->node_info.revision),
					p_node->print_desc,
					cl_ntoh16(p_default_physp->port_info.
						  base_lid), cPort);

				p_nbnode = p_rphysp->p_node;

				if (p_nbnode->node_info.node_type ==
				    IB_NODE_TYPE_SWITCH) {
					p_default_physp =
					    osm_node_get_physp_ptr(p_nbnode, 0);
				} else {
					p_default_physp = p_rphysp;
				}

				fprintf(rc, "{ %s%s Ports:%02X"
					" SystemGUID:%016" PRIx64
					" NodeGUID:%016" PRIx64
					" PortGUID:%016" PRIx64
					" VenID:%08X DevID:%04X Rev:%08X {%s} LID:%04X PN:%02X } ",
					(p_nbnode->node_info.node_type ==
					 IB_NODE_TYPE_SWITCH) ? "SW"
					: (p_nbnode->node_info.node_type ==
					   IB_NODE_TYPE_CA) ? "CA" : (p_nbnode->
								      node_info.
								      node_type
								      ==
								      IB_NODE_TYPE_ROUTER)
					? "Rt" : "**",
					(p_default_physp->port_info.base_lid ==
					 p_default_physp->port_info.
					 master_sm_base_lid) ? "-SM" : "",
					p_nbnode->node_info.num_ports,
					cl_ntoh64(p_nbnode->node_info.sys_guid),
					cl_ntoh64(p_nbnode->node_info.
						  node_guid),
					cl_ntoh64(p_rphysp->port_guid),
					cl_ntoh32(ib_node_info_get_vendor_id
						  (&p_nbnode->node_info)),
					cl_ntoh32(p_nbnode->node_info.
						  device_id),
					cl_ntoh32(p_nbnode->node_info.revision),
					p_nbnode->print_desc,
					cl_ntoh16(p_default_physp->port_info.
						  base_lid),
					p_rphysp->port_num);

				port_state =
				    ib_port_info_get_port_state(&p_physp->
								port_info);
				link_speed_act =
				    ib_port_info_get_link_speed_active
				    (&p_physp->port_info);

				fprintf(rc, "PHY=%s LOG=%s SPD=%s\n",
					(p_physp->port_info.link_width_active ==
					 1) ? "1x" : (p_physp->port_info.
						      link_width_active ==
						      2) ? "4x" : (p_physp->
								   port_info.
								   link_width_active
								   ==
								   8) ? "12x" :
					"??",
					((port_state ==
					  IB_LINK_ACTIVE) ? "ACT" : (port_state
								     ==
								     IB_LINK_ARMED)
					 ? "ARM" : (port_state ==
						    IB_LINK_INIT) ? "INI" :
					 "DWN"),
					(link_speed_act ==
					 1) ? "2.5" : (link_speed_act ==
						       2) ? "5"
					: (link_speed_act == 4) ? "10" : "??");
			}
		}
		p_node = (osm_node_t *) cl_qmap_next(&p_node->map_item);
	}

	CL_PLOCK_RELEASE(p_mgr->p_lock);

	fclose(rc);

      Exit:
	free(file_name);
	OSM_LOG_EXIT(p_mgr->p_log);
}

/**********************************************************************
 **********************************************************************/
static void __osm_state_mgr_report(IN osm_state_mgr_t * const p_mgr)
{
	const cl_qmap_t *p_tbl;
	const osm_port_t *p_port;
	const osm_node_t *p_node;
	const osm_physp_t *p_physp;
	const osm_physp_t *p_remote_physp;
	const ib_port_info_t *p_pi;
	uint8_t port_num;
	uint8_t start_port;
	uint32_t num_ports;
	uint8_t node_type;

	if (!osm_log_is_active(p_mgr->p_log, OSM_LOG_VERBOSE))
		return;

	OSM_LOG_ENTER(p_mgr->p_log, __osm_state_mgr_report);

	osm_log_printf(p_mgr->p_log, OSM_LOG_VERBOSE,
		       "\n==================================================="
		       "===================================================="
		       "\nVendor      : Ty "
		       ": #  : Sta : LID  : LMC : MTU  : LWA : LSA : Port GUID    "
		       "    : Neighbor Port (Port #)\n");

	p_tbl = &p_mgr->p_subn->port_guid_tbl;

	/*
	 * Hold lock non-exclusively while we perform these read-only operations.
	 */

	CL_PLOCK_ACQUIRE(p_mgr->p_lock);
	p_port = (osm_port_t *) cl_qmap_head(p_tbl);
	while (p_port != (osm_port_t *) cl_qmap_end(p_tbl)) {
		if (osm_log_is_active(p_mgr->p_log, OSM_LOG_DEBUG)) {
			osm_log(p_mgr->p_log, OSM_LOG_DEBUG,
				"__osm_state_mgr_report: "
				"Processing port 0x%016" PRIx64 "\n",
				cl_ntoh64(osm_port_get_guid(p_port)));
		}

		p_node = p_port->p_node;
		node_type = osm_node_get_type(p_node);
		if (node_type == IB_NODE_TYPE_SWITCH)
			start_port = 0;
		else
			start_port = 1;

		num_ports = osm_node_get_num_physp(p_node);
		for (port_num = start_port; port_num < num_ports; port_num++) {
			p_physp = osm_node_get_physp_ptr(p_node, port_num);
			if (!osm_physp_is_valid(p_physp))
				continue;

			osm_log_printf(p_mgr->p_log, OSM_LOG_VERBOSE,
				       "%-11s : %s : %02X :",
				       osm_get_manufacturer_str(cl_ntoh64
								(osm_node_get_node_guid
								 (p_node))),
				       osm_get_node_type_str_fixed_width
				       (node_type), port_num);

			p_pi = &p_physp->port_info;

			/*
			 * Port state is not defined for switch port 0
			 */
			if (port_num == 0)
				osm_log_printf(p_mgr->p_log, OSM_LOG_VERBOSE,
					       "     :");
			else
				osm_log_printf(p_mgr->p_log, OSM_LOG_VERBOSE,
					       " %s :",
					       osm_get_port_state_str_fixed_width
					       (ib_port_info_get_port_state
						(p_pi)));

			/*
			 * LID values are only meaningful in select cases.
			 */
			if (ib_port_info_get_port_state(p_pi) != IB_LINK_DOWN
			    &&
			    ((node_type == IB_NODE_TYPE_SWITCH && port_num == 0)
			     || node_type != IB_NODE_TYPE_SWITCH))
				osm_log_printf(p_mgr->p_log, OSM_LOG_VERBOSE,
					       " %04X :  %01X  :",
					       cl_ntoh16(p_pi->base_lid),
					       ib_port_info_get_lmc(p_pi));
			else
				osm_log_printf(p_mgr->p_log, OSM_LOG_VERBOSE,
					       "      :     :");

			if (port_num != 0)
				osm_log_printf(p_mgr->p_log, OSM_LOG_VERBOSE,
					       " %s : %s : %s ",
					       osm_get_mtu_str
					       (ib_port_info_get_neighbor_mtu
						(p_pi)),
					       osm_get_lwa_str(p_pi->
							       link_width_active),
					       osm_get_lsa_str
					       (ib_port_info_get_link_speed_active
						(p_pi)));
			else
				osm_log_printf(p_mgr->p_log, OSM_LOG_VERBOSE,
					       "      :     :     ");

			if (osm_physp_get_port_guid(p_physp) ==
			    p_mgr->p_subn->sm_port_guid)
				osm_log_printf(p_mgr->p_log, OSM_LOG_VERBOSE,
					       "* %016" PRIx64 " *",
					       cl_ntoh64(osm_physp_get_port_guid
							 (p_physp)));
			else
				osm_log_printf(p_mgr->p_log, OSM_LOG_VERBOSE,
					       ": %016" PRIx64 " :",
					       cl_ntoh64(osm_physp_get_port_guid
							 (p_physp)));

			if (port_num
			    && (ib_port_info_get_port_state(p_pi) !=
				IB_LINK_DOWN)) {
				p_remote_physp = osm_physp_get_remote(p_physp);
				if (p_remote_physp
				    && osm_physp_is_valid(p_remote_physp)) {
					osm_log_printf(p_mgr->p_log,
						       OSM_LOG_VERBOSE,
						       " %016" PRIx64 " (%02X)",
						       cl_ntoh64
						       (osm_physp_get_port_guid
							(p_remote_physp)),
						       osm_physp_get_port_num
						       (p_remote_physp));
				} else
					osm_log_printf(p_mgr->p_log,
						       OSM_LOG_VERBOSE,
						       " UNKNOWN");
			}

			osm_log_printf(p_mgr->p_log, OSM_LOG_VERBOSE, "\n");
		}

		osm_log_printf(p_mgr->p_log, OSM_LOG_VERBOSE,
			       "------------------------------------------------------"
			       "------------------------------------------------\n");
		p_port = (osm_port_t *) cl_qmap_next(&p_port->map_item);
	}

	CL_PLOCK_RELEASE(p_mgr->p_lock);
	OSM_LOG_EXIT(p_mgr->p_log);
}

/**********************************************************************
 **********************************************************************/
static void __process_idle_time_queue_done(IN osm_state_mgr_t * const p_mgr)
{
	cl_qlist_t *p_list = &p_mgr->idle_time_list;
	cl_list_item_t *p_list_item;
	osm_idle_item_t *p_process_item;

	OSM_LOG_ENTER(p_mgr->p_log, __process_idle_time_queue_done);

	cl_spinlock_acquire(&p_mgr->idle_lock);
	p_list_item = cl_qlist_remove_head(p_list);

	if (p_list_item == cl_qlist_end(p_list)) {
		cl_spinlock_release(&p_mgr->idle_lock);
		osm_log(p_mgr->p_log, OSM_LOG_ERROR,
			"__process_idle_time_queue_done: ERR 3314: "
			"Idle time queue is empty\n");
		return;
	}
	cl_spinlock_release(&p_mgr->idle_lock);

	p_process_item = (osm_idle_item_t *) p_list_item;

	if (p_process_item->pfn_done) {

		p_process_item->pfn_done(p_process_item->context1,
					 p_process_item->context2);
	}

	free(p_process_item);

	OSM_LOG_EXIT(p_mgr->p_log);
	return;
}

/**********************************************************************
 **********************************************************************/
static osm_signal_t __process_idle_time_queue_start(IN osm_state_mgr_t *
						    const p_mgr)
{
	cl_qlist_t *p_list = &p_mgr->idle_time_list;
	cl_list_item_t *p_list_item;
	osm_idle_item_t *p_process_item;
	osm_signal_t signal;

	OSM_LOG_ENTER(p_mgr->p_log, __process_idle_time_queue_start);

	cl_spinlock_acquire(&p_mgr->idle_lock);

	p_list_item = cl_qlist_head(p_list);
	if (p_list_item == cl_qlist_end(p_list)) {
		cl_spinlock_release(&p_mgr->idle_lock);
		OSM_LOG_EXIT(p_mgr->p_log);
		return OSM_SIGNAL_NONE;
	}

	cl_spinlock_release(&p_mgr->idle_lock);

	p_process_item = (osm_idle_item_t *) p_list_item;

	CL_ASSERT(p_process_item->pfn_start);

	signal =
	    p_process_item->pfn_start(p_process_item->context1,
				      p_process_item->context2);

	CL_ASSERT(signal != OSM_SIGNAL_NONE);

	OSM_LOG_EXIT(p_mgr->p_log);
	return signal;
}

/**********************************************************************
 * Go over all the remote SMs (as updated in the sm_guid_tbl).
 * Find if there is a remote sm that is a master SM.
 * If there is a remote master SM - return a pointer to it,
 * else - return NULL.
 **********************************************************************/
static osm_remote_sm_t *__osm_state_mgr_exists_other_master_sm(IN
							       osm_state_mgr_t *
							       const p_mgr)
{
	cl_qmap_t *p_sm_tbl;
	osm_remote_sm_t *p_sm;
	osm_remote_sm_t *p_sm_res = NULL;

	OSM_LOG_ENTER(p_mgr->p_log, __osm_state_mgr_exists_other_master_sm);

	p_sm_tbl = &p_mgr->p_subn->sm_guid_tbl;

	/* go over all the remote SMs */
	for (p_sm = (osm_remote_sm_t *) cl_qmap_head(p_sm_tbl);
	     p_sm != (osm_remote_sm_t *) cl_qmap_end(p_sm_tbl);
	     p_sm = (osm_remote_sm_t *) cl_qmap_next(&p_sm->map_item)) {
		/* If the sm is in MASTER state - return a pointer to it */
		if (ib_sminfo_get_state(&p_sm->smi) == IB_SMINFO_STATE_MASTER) {
			osm_log(p_mgr->p_log, OSM_LOG_VERBOSE,
				"__osm_state_mgr_exists_other_master_sm: "
				"Found remote master SM with guid:0x%016" PRIx64
				"\n", cl_ntoh64(p_sm->smi.guid));
			p_sm_res = p_sm;
			goto Exit;
		}
	}

      Exit:
	OSM_LOG_EXIT(p_mgr->p_log);
	return (p_sm_res);
}

/**********************************************************************
 * Go over all remote SMs (as updated in the sm_guid_tbl).
 * Find the one with the highest priority and lowest guid.
 * Compare this SM to the local SM. If the local SM is higher -
 * return NULL, if the remote SM is higher - return a pointer to it.
 **********************************************************************/
static osm_remote_sm_t *__osm_state_mgr_get_highest_sm(IN osm_state_mgr_t *
						       const p_mgr)
{
	cl_qmap_t *p_sm_tbl;
	osm_remote_sm_t *p_sm = NULL;
	osm_remote_sm_t *p_highest_sm;
	uint8_t highest_sm_priority;
	ib_net64_t highest_sm_guid;

	OSM_LOG_ENTER(p_mgr->p_log, __osm_state_mgr_get_highest_sm);

	p_sm_tbl = &p_mgr->p_subn->sm_guid_tbl;

	/* Start with the local sm as the standard */
	p_highest_sm = NULL;
	highest_sm_priority = p_mgr->p_subn->opt.sm_priority;
	highest_sm_guid = p_mgr->p_subn->sm_port_guid;

	/* go over all the remote SMs */
	for (p_sm = (osm_remote_sm_t *) cl_qmap_head(p_sm_tbl);
	     p_sm != (osm_remote_sm_t *) cl_qmap_end(p_sm_tbl);
	     p_sm = (osm_remote_sm_t *) cl_qmap_next(&p_sm->map_item)) {

		/* If the sm is in NOTACTIVE state - continue */
		if (ib_sminfo_get_state(&p_sm->smi) ==
		    IB_SMINFO_STATE_NOTACTIVE)
			continue;

		if (osm_sm_is_greater_than(ib_sminfo_get_priority(&p_sm->smi),
					   p_sm->smi.guid, highest_sm_priority,
					   highest_sm_guid)) {
			/* the new p_sm is with higher priority - update the highest_sm */
			/* to this sm */
			p_highest_sm = p_sm;
			highest_sm_priority =
			    ib_sminfo_get_priority(&p_sm->smi);
			highest_sm_guid = p_sm->smi.guid;
		}
	}

	if (p_highest_sm != NULL) {
		osm_log(p_mgr->p_log, OSM_LOG_DEBUG,
			"__osm_state_mgr_get_highest_sm: "
			"Found higher SM with guid: %016" PRIx64 "\n",
			cl_ntoh64(p_highest_sm->smi.guid));
	}

	OSM_LOG_EXIT(p_mgr->p_log);
	return (p_highest_sm);
}

/**********************************************************************
 * Send SubnSet(SMInfo) SMP with HANDOVER attribute to the
 * remote_sm indicated.
 **********************************************************************/
static void
__osm_state_mgr_send_handover(IN osm_state_mgr_t * const p_mgr,
			      IN osm_remote_sm_t * const p_sm)
{
	uint8_t payload[IB_SMP_DATA_SIZE];
	ib_sm_info_t *p_smi = (ib_sm_info_t *) payload;
	osm_madw_context_t context;
	const osm_port_t *p_port;
	ib_api_status_t status;

	OSM_LOG_ENTER(p_mgr->p_log, __osm_state_mgr_send_handover);

	if (p_mgr->p_subn->opt.testability_mode ==
	    OSM_TEST_MODE_EXIT_BEFORE_SEND_HANDOVER) {
		osm_log(p_mgr->p_log, OSM_LOG_ERROR,
			"__osm_state_mgr_send_handover: ERR 3315: "
			"Exit on testability mode OSM_TEST_MODE_EXIT_BEFORE_SEND_HANDOVER\n");
		osm_exit_flag = TRUE;
		sleep(3);
		exit(1);
	}

	/*
	 * Send a query of SubnSet(SMInfo) HANDOVER to the remote sm given.
	 */

	memset(&context, 0, sizeof(context));
	p_port = p_sm->p_port;
	if (p_port == NULL) {
		osm_log(p_mgr->p_log, OSM_LOG_ERROR,
			"__osm_state_mgr_send_handover: ERR 3316: "
			"No port object on given remote_sm object\n");
		goto Exit;
	}

	/* update the master_guid in the p_sm_state_mgr object according to */
	/* the guid of the port where the new Master SM should reside. */
	osm_log(p_mgr->p_log, OSM_LOG_VERBOSE,
		"__osm_state_mgr_send_handover: "
		"Handing over mastership. Updating sm_state_mgr master_guid: %016"
		PRIx64 "\n", cl_ntoh64(p_port->guid));
	p_mgr->p_sm_state_mgr->master_guid = p_port->guid;

	context.smi_context.port_guid = p_port->guid;
	context.smi_context.set_method = TRUE;

	p_smi->guid = p_mgr->p_subn->sm_port_guid;
	p_smi->act_count = cl_hton32(p_mgr->p_stats->qp0_mads_sent);
	p_smi->pri_state = (uint8_t) (p_mgr->p_subn->sm_state |
				      p_mgr->p_subn->opt.sm_priority << 4);
	/*
	 * Return 0 for the SM key unless we authenticate the requester
	 * as the master SM.
	 */
	if (ib_sminfo_get_state(&p_sm->smi) == IB_SMINFO_STATE_MASTER) {
		osm_log(p_mgr->p_log, OSM_LOG_DEBUG,
			"__osm_state_mgr_send_handover: "
			"Responding to master SM with real sm_key\n");
		p_smi->sm_key = p_mgr->p_subn->opt.sm_key;
	} else {
		/* The requester is not authenticated as master - set sm_key to zero */
		osm_log(p_mgr->p_log, OSM_LOG_DEBUG,
			"__osm_state_mgr_send_handover: "
			"Responding to SM not master with zero sm_key\n");
		p_smi->sm_key = 0;
	}

	status = osm_req_set(p_mgr->p_req,
			     osm_physp_get_dr_path_ptr(p_port->p_physp),
			     payload, sizeof(payload),
			     IB_MAD_ATTR_SM_INFO, IB_SMINFO_ATTR_MOD_HANDOVER,
			     CL_DISP_MSGID_NONE, &context);

	if (status != IB_SUCCESS) {
		osm_log(p_mgr->p_log, OSM_LOG_ERROR,
			"__osm_state_mgr_send_handover: ERR 3317: "
			"Failure requesting SMInfo (%s)\n",
			ib_get_err_str(status));
	}

      Exit:
	OSM_LOG_EXIT(p_mgr->p_log);
}

/**********************************************************************
 * Send Trap 64 on all new ports.
 **********************************************************************/
static void __osm_state_mgr_report_new_ports(IN osm_state_mgr_t * const p_mgr)
{
	ib_gid_t port_gid;
	ib_mad_notice_attr_t notice;
	ib_api_status_t status;
	ib_net64_t port_guid;
	cl_map_item_t *p_next;
	osm_port_t *p_port;
	uint16_t min_lid_ho;
	uint16_t max_lid_ho;

	OSM_LOG_ENTER(p_mgr->p_log, __osm_state_mgr_report_new_ports);

	CL_PLOCK_ACQUIRE(p_mgr->p_lock);
	p_next = cl_qmap_head(&p_mgr->p_subn->port_guid_tbl);
	while (p_next != cl_qmap_end(&p_mgr->p_subn->port_guid_tbl)) {
		p_port = (osm_port_t *) p_next;
		p_next = cl_qmap_next(p_next);

		if (!p_port->is_new)
			continue;

		port_guid = osm_port_get_guid(p_port);
		/* issue a notice - trap 64 */

		/* details of the notice */
		notice.generic_type = 0x83;	/* is generic subn mgt type */
		ib_notice_set_prod_type_ho(&notice, 4);	/* A Class Manager generator */
		/* endport becomes to be reachable */
		notice.g_or_v.generic.trap_num = CL_HTON16(64);
		/* The sm_base_lid is saved in network order already. */
		notice.issuer_lid = p_mgr->p_subn->sm_base_lid;
		/* following C14-72.1.1 and table 119 p739 */
		/* we need to provide the GID */
		port_gid.unicast.prefix = p_mgr->p_subn->opt.subnet_prefix;
		port_gid.unicast.interface_id = port_guid;
		memcpy(&(notice.data_details.ntc_64_67.gid), &(port_gid),
		       sizeof(ib_gid_t));

		/* According to page 653 - the issuer gid in this case of trap
		 * is the SM gid, since the SM is the initiator of this trap. */
		notice.issuer_gid.unicast.prefix =
		    p_mgr->p_subn->opt.subnet_prefix;
		notice.issuer_gid.unicast.interface_id =
		    p_mgr->p_subn->sm_port_guid;

		status =
		    osm_report_notice(p_mgr->p_log, p_mgr->p_subn, &notice);
		if (status != IB_SUCCESS) {
			osm_log(p_mgr->p_log, OSM_LOG_ERROR,
				"__osm_state_mgr_report_new_ports: ERR 3318: "
				"Error sending trap reports on GUID:0x%016"
				PRIx64 " (%s)\n", port_gid.unicast.interface_id,
				ib_get_err_str(status));
		}
		osm_port_get_lid_range_ho(p_port, &min_lid_ho, &max_lid_ho);
		osm_log(p_mgr->p_log, OSM_LOG_INFO,
			"__osm_state_mgr_report_new_ports: "
			"Discovered new port with GUID:0x%016" PRIx64
			" LID range [0x%X,0x%X] of node:%s\n",
			cl_ntoh64(port_gid.unicast.interface_id),
			min_lid_ho, max_lid_ho,
			p_port->p_node ? p_port->p_node->
			print_desc : "UNKNOWN");

		p_port->is_new = 0;
	}
	CL_PLOCK_RELEASE(p_mgr->p_lock);

	OSM_LOG_EXIT(p_mgr->p_log);
}

/**********************************************************************
 * Make sure that the lid_port_tbl of the subnet has only the ports
 * that are recognized, and in the correct lid place. There could be
 * errors if we wanted to assign a certain port with lid X, but that
 * request didn't reach the port. In this case port_lid_tbl will have
 * the port under lid X, though the port isn't updated with this lid.
 * We will run a new heavy sweep (since there were errors in the
 * initialization), but here we'll clean the database from incorrect
 * information.
 **********************************************************************/
static void __osm_state_mgr_check_tbl_consistency(IN osm_state_mgr_t *
						  const p_mgr)
{
	cl_qmap_t *p_port_guid_tbl;
	osm_port_t *p_port;
	osm_port_t *p_next_port;
	cl_ptr_vector_t *p_port_lid_tbl;
	size_t max_lid, ref_size, curr_size, lid;
	osm_port_t *p_port_ref, *p_port_stored;
	cl_ptr_vector_t ref_port_lid_tbl;
	uint16_t min_lid_ho;
	uint16_t max_lid_ho;
	uint16_t lid_ho;

	OSM_LOG_ENTER(p_mgr->p_log, __osm_state_mgr_check_tbl_consistency);

	cl_ptr_vector_construct(&ref_port_lid_tbl);
	cl_ptr_vector_init(&ref_port_lid_tbl,
			   cl_ptr_vector_get_size(&p_mgr->p_subn->port_lid_tbl),
			   OSM_SUBNET_VECTOR_GROW_SIZE);

	p_port_guid_tbl = &p_mgr->p_subn->port_guid_tbl;

	/* Let's go over all the ports according to port_guid_tbl,
	 * and add the port to a reference port_lid_tbl. */
	p_next_port = (osm_port_t *) cl_qmap_head(p_port_guid_tbl);
	while (p_next_port != (osm_port_t *) cl_qmap_end(p_port_guid_tbl)) {
		p_port = p_next_port;
		p_next_port =
		    (osm_port_t *) cl_qmap_next(&p_next_port->map_item);

		osm_port_get_lid_range_ho(p_port, &min_lid_ho, &max_lid_ho);
		for (lid_ho = min_lid_ho; lid_ho <= max_lid_ho; lid_ho++)
			cl_ptr_vector_set(&ref_port_lid_tbl, lid_ho, p_port);
	}

	p_port_lid_tbl = &p_mgr->p_subn->port_lid_tbl;

	ref_size = cl_ptr_vector_get_size(&ref_port_lid_tbl);
	curr_size = cl_ptr_vector_get_size(p_port_lid_tbl);
	/* They should be the same, but compare it anyway */
	max_lid = (ref_size > curr_size) ? ref_size : curr_size;

	for (lid = 1; lid <= max_lid; lid++) {
		p_port_ref = NULL;
		p_port_stored = NULL;
		cl_ptr_vector_at(p_port_lid_tbl, lid, (void *)&p_port_stored);
		cl_ptr_vector_at(&ref_port_lid_tbl, lid, (void *)&p_port_ref);

		if (p_port_stored == p_port_ref)
			/* This is the "good" case - both entries are the same for this lid.
			 * Nothing to do. */
			continue;

		if (p_port_ref == NULL) {
			/* There is an object in the subnet database for this lid,
			 * but no such object exists in the reference port_list_tbl.
			 * This can occur if we wanted to assign a certain port with some
			 * lid (different than the one pre-assigned to it), and the port
			 * didn't get the PortInfo Set request. Due to this, the port
			 * is updated with its original lid in our database, but with the
			 * new lid we wanted to give it in our port_lid_tbl. */
			osm_log(p_mgr->p_log, OSM_LOG_ERROR,
				"__osm_state_mgr_check_tbl_consistency: ERR 3322: "
				"lid 0x%zX is wrongly assigned to port 0x%016"
				PRIx64 " in port_lid_tbl\n", lid,
				cl_ntoh64(osm_port_get_guid(p_port_stored)));
		} else {
			if (p_port_stored == NULL) {
				/* There is an object in the new database, but no object in our subnet
				 * database. This is the matching case of the prior check - the port
				 * still has its original lid. */
				osm_log(p_mgr->p_log, OSM_LOG_ERROR,
					"__osm_state_mgr_check_tbl_consistency: ERR 3323: "
					"port 0x%016" PRIx64
					" exists in new port_lid_tbl under "
					"lid 0x%zX, but missing in subnet port_lid_tbl db\n",
					cl_ntoh64(osm_port_get_guid
						  (p_port_ref)), lid);
			} else {

				/* if we reached here then p_port_stored != p_port_ref.
				 * We were trying to set a lid to p_port_stored, but it didn't reach it,
				 * and p_port_ref also didn't get the lid update. */
				osm_log(p_mgr->p_log, OSM_LOG_ERROR,
					"__osm_state_mgr_check_tbl_consistency: ERR 3324: "
					"lid 0x%zX has port 0x%016" PRIx64
					" in new port_lid_tbl db, "
					"and port 0x%016" PRIx64
					" in subnet port_lid_tbl db\n", lid,
					cl_ntoh64(osm_port_get_guid
						  (p_port_ref)),
					cl_ntoh64(osm_port_get_guid
						  (p_port_stored)));
			}
		}
		/* In any of these cases we want to set NULL in the port_lid_tbl, since this
		 * entry is invalid. Also, make sure we'll do another heavy sweep. */
		cl_ptr_vector_set(p_port_lid_tbl, lid, NULL);
		p_mgr->p_subn->subnet_initialization_error = TRUE;
	}

	cl_ptr_vector_destroy(&ref_port_lid_tbl);
	OSM_LOG_EXIT(p_mgr->p_log);
}

/**********************************************************************
 **********************************************************************/
void osm_state_mgr_process(IN osm_state_mgr_t * const p_mgr,
			   IN osm_signal_t signal)
{
	ib_api_status_t status;
	osm_remote_sm_t *p_remote_sm;
	osm_signal_t tmp_signal;

	CL_ASSERT(p_mgr);

	OSM_LOG_ENTER(p_mgr->p_log, osm_state_mgr_process);

	/* if we are exiting do nothing */
	if (osm_exit_flag)
		signal = OSM_SIGNAL_NONE;

	/*
	 * The state lock prevents many race conditions from screwing
	 * up the state transition process.  For example, if an function
	 * puts transactions on the wire, the state lock guarantees this
	 * loop will see the return code ("DONE PENDING") of the function
	 * before the "NO OUTSTANDING TRANSACTIONS" signal is asynchronously
	 * received.
	 */
	cl_spinlock_acquire(&p_mgr->state_lock);

	while (signal != OSM_SIGNAL_NONE) {
		if (osm_log_is_active(p_mgr->p_log, OSM_LOG_DEBUG)) {
			osm_log(p_mgr->p_log, OSM_LOG_DEBUG,
				"osm_state_mgr_process: "
				"Received signal %s in state %s\n",
				osm_get_sm_signal_str(signal),
				osm_get_sm_state_str(p_mgr->state));
		}

		/*
		 * If we're already sweeping and we get the signal to sweep,
		 * just ignore it harmlessly.
		 */
		if ((p_mgr->state != OSM_SM_STATE_IDLE)
		    && (p_mgr->state != OSM_SM_STATE_STANDBY)
		    && (signal == OSM_SIGNAL_SWEEP)) {
			break;
		}

		switch (p_mgr->state) {
		case OSM_SM_STATE_IDLE:
			switch (signal) {
			case OSM_SIGNAL_SWEEP:
				/*
				 * If the osm_sm_state_mgr is in NOT-ACTIVE state -
				 * stay in IDLE
				 */
				if (p_mgr->p_subn->sm_state == IB_SMINFO_STATE_NOTACTIVE) {
					osm_vendor_set_sm(p_mgr->p_mad_ctrl->h_bind, FALSE);
					goto Idle;
				}

				/*
				 * If the osm_sm_state_mgr is in INIT state - signal
				 * it with a INIT signal to move it to DISCOVERY state.
				 */
				if (p_mgr->p_subn->sm_state == IB_SMINFO_STATE_INIT)
					osm_sm_state_mgr_process(p_mgr->
								 p_sm_state_mgr,
								 OSM_SM_SIGNAL_INIT);

				/*
				 * If we already have switches, then try a light sweep.
				 * Otherwise, this is probably our first discovery pass
				 * or we are connected in loopback. In both cases do a
				 * heavy sweep.
				 * Note: If we are connected in loopback we want a heavy
				 * sweep, since we will not be getting any traps if there is
				 * a lost connection.
				 */
				/*  if we are in DISCOVERING state - this means it is either in
				 *  initializing or wake up from STANDBY - run the heavy sweep */
				if (cl_qmap_count(&p_mgr->p_subn->sw_guid_tbl)
				    && p_mgr->p_subn->sm_state !=
				    IB_SMINFO_STATE_DISCOVERING
				    && p_mgr->p_subn->opt.force_heavy_sweep ==
				    FALSE
				    && p_mgr->p_subn->
				    force_immediate_heavy_sweep == FALSE
				    && p_mgr->p_subn->
				    force_delayed_heavy_sweep == FALSE
				    && p_mgr->p_subn->subnet_initialization_error == FALSE) {
					if (__osm_state_mgr_light_sweep_start(p_mgr) == IB_SUCCESS) {
						p_mgr->state = OSM_SM_STATE_SWEEP_LIGHT;
					}
				} else {
					/* First of all - if force_immediate_heavy_sweep is TRUE then
					 * need to unset it */
					p_mgr->p_subn->force_immediate_heavy_sweep = FALSE;
					/* If force_delayed_heavy_sweep is TRUE then
					 * need to unset it */
					p_mgr->p_subn->force_delayed_heavy_sweep = FALSE;
					/* If subnet_initialization_error is TRUE then
					 * need to unset it. */
					p_mgr->p_subn->subnet_initialization_error = FALSE;

					/* rescan configuration updates */
					status = osm_subn_rescan_conf_files(p_mgr->p_subn);
					if (status != IB_SUCCESS) {
						osm_log(p_mgr->p_log,
							OSM_LOG_ERROR,
							"osm_state_mgr_process: ERR 331A: "
							"osm_subn_rescan_conf_file failed\n");
					}

					if (p_mgr->p_subn->sm_state != IB_SMINFO_STATE_MASTER)
						p_mgr->p_subn->need_update = 1;

					status = __osm_state_mgr_sweep_hop_0(p_mgr);
					if (status == IB_SUCCESS) {
						p_mgr->state = OSM_SM_STATE_SWEEP_HEAVY_SELF;
					}
				}
			      Idle:
				signal = OSM_SIGNAL_NONE;
				break;

			case OSM_SIGNAL_IDLE_TIME_PROCESS_REQUEST:
				p_mgr->state = OSM_SM_STATE_PROCESS_REQUEST;
				signal = OSM_SIGNAL_IDLE_TIME_PROCESS;
				break;

			default:
				__osm_state_mgr_signal_error(p_mgr, signal);
				signal = OSM_SIGNAL_NONE;
				break;
			}
			break;

		case OSM_SM_STATE_PROCESS_REQUEST:
			switch (signal) {
			case OSM_SIGNAL_IDLE_TIME_PROCESS:
				signal = __process_idle_time_queue_start(p_mgr);
				switch (signal) {
				case OSM_SIGNAL_NONE:
					p_mgr->state = OSM_SM_STATE_IDLE;
					break;

				case OSM_SIGNAL_DONE_PENDING:
					p_mgr->state = OSM_SM_STATE_PROCESS_REQUEST_WAIT;
					signal = OSM_SIGNAL_NONE;
					break;

				case OSM_SIGNAL_DONE:
					p_mgr->state = OSM_SM_STATE_PROCESS_REQUEST_DONE;
					break;

				default:
					__osm_state_mgr_signal_error(p_mgr, signal);
					signal = OSM_SIGNAL_NONE;
					break;
				}
				break;

			default:
				__osm_state_mgr_signal_error(p_mgr, signal);
				signal = OSM_SIGNAL_NONE;
				break;
			}
			break;

		case OSM_SM_STATE_PROCESS_REQUEST_WAIT:
			switch (signal) {
			case OSM_SIGNAL_NO_PENDING_TRANSACTIONS:
				p_mgr->state = OSM_SM_STATE_PROCESS_REQUEST_DONE;
				break;

			default:
				__osm_state_mgr_signal_error(p_mgr, signal);
				signal = OSM_SIGNAL_NONE;
				break;
			}
			break;

		case OSM_SM_STATE_PROCESS_REQUEST_DONE:
			switch (signal) {
			case OSM_SIGNAL_NO_PENDING_TRANSACTIONS:
			case OSM_SIGNAL_DONE:
				/* CALL the done function */
				__process_idle_time_queue_done(p_mgr);

				/*
				 * Set the signal to OSM_SIGNAL_IDLE_TIME_PROCESS
				 * so that the next element in the queue gets processed
				 */

				signal = OSM_SIGNAL_IDLE_TIME_PROCESS;
				p_mgr->state = OSM_SM_STATE_PROCESS_REQUEST;
				break;

			default:
				__osm_state_mgr_signal_error(p_mgr, signal);
				signal = OSM_SIGNAL_NONE;
				break;
			}
			break;

		case OSM_SM_STATE_SWEEP_LIGHT:
			switch (signal) {
			case OSM_SIGNAL_LIGHT_SWEEP_FAIL:
			case OSM_SIGNAL_CHANGE_DETECTED:
				/*
				 * Nothing else to do yet except change state.
				 */
				p_mgr->state = OSM_SM_STATE_SWEEP_LIGHT_WAIT;
				signal = OSM_SIGNAL_NONE;
				break;

			case OSM_SIGNAL_NO_PENDING_TRANSACTIONS:
				/*
				 * No change was detected on the subnet.
				 * We can return to the idle state.
				 */
				__osm_state_mgr_light_sweep_done_msg(p_mgr);
				p_mgr->state = OSM_SM_STATE_PROCESS_REQUEST;
				signal = OSM_SIGNAL_IDLE_TIME_PROCESS;
				break;

			default:
				__osm_state_mgr_signal_error(p_mgr, signal);
				signal = OSM_SIGNAL_NONE;
				break;
			}
			break;

		case OSM_SM_STATE_SWEEP_LIGHT_WAIT:
			switch (signal) {
			case OSM_SIGNAL_LIGHT_SWEEP_FAIL:
			case OSM_SIGNAL_CHANGE_DETECTED:
				/*
				 * Nothing to do here. One subnet change typcially
				 * begets another.... But need to wait for all transactions to
				 * complete
				 */
				break;

			case OSM_SIGNAL_NO_PENDING_TRANSACTIONS:
				/*
				 * A change was detected on the subnet.
				 * Initiate a heavy sweep.
				 */
				if (__osm_state_mgr_sweep_hop_0(p_mgr) == IB_SUCCESS) {
					p_mgr->state = OSM_SM_STATE_SWEEP_HEAVY_SELF;
				}
				break;

			default:
				__osm_state_mgr_signal_error(p_mgr, signal);
				break;
			}
			signal = OSM_SIGNAL_NONE;
			break;

		case OSM_SM_STATE_SWEEP_HEAVY_SELF:
			switch (signal) {
			case OSM_SIGNAL_CHANGE_DETECTED:
				/*
				 * Nothing to do here. One subnet change typcially
				 * begets another.... But need to wait for all transactions
				 */
				signal = OSM_SIGNAL_NONE;
				break;

			case OSM_SIGNAL_NO_PENDING_TRANSACTIONS:
				if (__osm_state_mgr_is_sm_port_down(p_mgr) == TRUE) {
					__osm_state_mgr_sm_port_down_msg(p_mgr);

					/* Run the drop manager - we want to clear all records */
					osm_drop_mgr_process(p_mgr->p_drop_mgr);

					/* Move to DISCOVERING state */
					osm_sm_state_mgr_process(p_mgr->
								 p_sm_state_mgr,
								 OSM_SM_SIGNAL_DISCOVER);

					p_mgr->state = OSM_SM_STATE_PROCESS_REQUEST;
					signal = OSM_SIGNAL_IDLE_TIME_PROCESS;
				} else {
					if (__osm_state_mgr_sweep_hop_1(p_mgr)
					    == IB_SUCCESS) {
						p_mgr->state = OSM_SM_STATE_SWEEP_HEAVY_SUBNET;
					}
					signal = OSM_SIGNAL_NONE;
				}
				break;

			default:
				__osm_state_mgr_signal_error(p_mgr, signal);
				signal = OSM_SIGNAL_NONE;
				break;
			}
			break;

			/*
			 * There is no 'OSM_SM_STATE_SWEEP_HEAVY_WAIT' state since we
			 * know that there are outstanding transactions on the wire already...
			 */
		case OSM_SM_STATE_SWEEP_HEAVY_SUBNET:
			switch (signal) {
			case OSM_SIGNAL_CHANGE_DETECTED:
				/*
				 * Nothing to do here. One subnet change typically
				 * begets another....
				 */
				signal = OSM_SIGNAL_NONE;
				break;

			case OSM_SIGNAL_MASTER_OR_HIGHER_SM_DETECTED:
				p_mgr->state = OSM_SM_STATE_MASTER_OR_HIGHER_SM_DETECTED;
				break;

			case OSM_SIGNAL_NO_PENDING_TRANSACTIONS:
				/* if new sweep requiested - don't bother with the rest */
				if (p_mgr->p_subn->force_immediate_heavy_sweep) {
					p_mgr->state = OSM_SM_STATE_IDLE;
					signal = OSM_SIGNAL_SWEEP;
					break;
				}

				__osm_state_mgr_sweep_heavy_done_msg(p_mgr);

				/* If we are MASTER - get the highest remote_sm, and
				 * see if it is higher than our local sm. If
				 */
				if (p_mgr->p_subn->sm_state == IB_SMINFO_STATE_MASTER) {
					p_remote_sm = __osm_state_mgr_get_highest_sm(p_mgr);
					if (p_remote_sm != NULL) {
						/* report new ports (trap 64) before leaving MASTER */
						__osm_state_mgr_report_new_ports(p_mgr);

						/* need to handover the mastership
						 * to the remote sm, and move to standby */
						__osm_state_mgr_send_handover(p_mgr, p_remote_sm);
						osm_sm_state_mgr_process(p_mgr->
									 p_sm_state_mgr,
									 OSM_SM_SIGNAL_HANDOVER_SENT);
						p_mgr->state = OSM_SM_STATE_STANDBY;
						signal = OSM_SIGNAL_NONE;
						break;
					} else {
						/* We are the highest sm - check to see if there is
						 * a remote SM that is in master state. */
						p_remote_sm =
						    __osm_state_mgr_exists_other_master_sm(p_mgr);
						if (p_remote_sm != NULL) {
							/* There is a remote SM that is master.
							 * need to wait for that SM to relinquish control
							 * of its portion of the subnet. C14-60.2.1.
							 * Also - need to start polling on that SM. */
							p_mgr->p_sm_state_mgr->
							    p_polling_sm = p_remote_sm;
							osm_sm_state_mgr_process
							    (p_mgr->
							     p_sm_state_mgr,
							     OSM_SM_SIGNAL_WAIT_FOR_HANDOVER);
							p_mgr->state = OSM_SM_STATE_PROCESS_REQUEST;
							signal = OSM_SIGNAL_IDLE_TIME_PROCESS;
							break;
						}
					}
				}

				/* Need to continue with lid assignment */
				osm_drop_mgr_process(p_mgr->p_drop_mgr);

				p_mgr->state = OSM_SM_STATE_SET_PKEY;

				/*
				 * If we are not MASTER already - this means that we are
				 * in discovery state. call osm_sm_state_mgr with signal
				 * DISCOVERY_COMPLETED
				 */
				if (p_mgr->p_subn->sm_state == IB_SMINFO_STATE_DISCOVERING)
					osm_sm_state_mgr_process(p_mgr->
								 p_sm_state_mgr,
								 OSM_SM_SIGNAL_DISCOVERY_COMPLETED);

				/* the returned signal might be DONE or DONE_PENDING */
				signal = osm_pkey_mgr_process(p_mgr->p_subn->p_osm);

				/* the returned signal is always DONE */
				tmp_signal = osm_qos_setup(p_mgr->p_subn->p_osm);

				if (tmp_signal == OSM_SIGNAL_DONE_PENDING)
					signal = OSM_SIGNAL_DONE_PENDING;

				/* try to restore SA DB (this should be before lid_mgr
				   because we may want to disable clients reregistration
				   when SA DB is restored) */
				osm_sa_db_file_load(p_mgr->p_subn->p_osm);

				break;

			default:
				__osm_state_mgr_signal_error(p_mgr, signal);
				signal = OSM_SIGNAL_NONE;
				break;
			}
			break;

		case OSM_SM_STATE_SET_PKEY:
			switch (signal) {
			case OSM_SIGNAL_DONE:
				p_mgr->state = OSM_SM_STATE_SET_PKEY_DONE;
				break;

			case OSM_SIGNAL_DONE_PENDING:
				/*
				 * There are outstanding transactions, so we
				 * must wait for the wire to clear.
				 */
				p_mgr->state = OSM_SM_STATE_SET_PKEY_WAIT;
				signal = OSM_SIGNAL_NONE;
				break;

			default:
				__osm_state_mgr_signal_error(p_mgr, signal);
				signal = OSM_SIGNAL_NONE;
				break;
			}
			break;

		case OSM_SM_STATE_SET_PKEY_WAIT:
			switch (signal) {
			case OSM_SIGNAL_NO_PENDING_TRANSACTIONS:
				p_mgr->state = OSM_SM_STATE_SET_PKEY_DONE;
				break;

			default:
				__osm_state_mgr_signal_error(p_mgr, signal);
				signal = OSM_SIGNAL_NONE;
				break;
			}
			break;

		case OSM_SM_STATE_SET_PKEY_DONE:
			switch (signal) {
			case OSM_SIGNAL_NO_PENDING_TRANSACTIONS:
			case OSM_SIGNAL_DONE:
				p_mgr->state = OSM_SM_STATE_SET_SM_UCAST_LID;
				signal = osm_lid_mgr_process_sm(p_mgr->p_lid_mgr);
				break;

			default:
				__osm_state_mgr_signal_error(p_mgr, signal);
				signal = OSM_SIGNAL_NONE;
				break;
			}
			break;

		case OSM_SM_STATE_SET_SM_UCAST_LID:
			switch (signal) {
			case OSM_SIGNAL_DONE:
				p_mgr->state = OSM_SM_STATE_SET_SM_UCAST_LID_DONE;
				break;

			case OSM_SIGNAL_DONE_PENDING:
				/*
				 * There are outstanding transactions, so we
				 * must wait for the wire to clear.
				 */
				p_mgr->state = OSM_SM_STATE_SET_SM_UCAST_LID_WAIT;
				signal = OSM_SIGNAL_NONE;
				break;

			default:
				__osm_state_mgr_signal_error(p_mgr, signal);
				signal = OSM_SIGNAL_NONE;
				break;
			}
			break;

		case OSM_SM_STATE_SET_SM_UCAST_LID_WAIT:
			switch (signal) {
			case OSM_SIGNAL_NO_PENDING_TRANSACTIONS:
				p_mgr->state = OSM_SM_STATE_SET_SM_UCAST_LID_DONE;
				break;

			default:
				__osm_state_mgr_signal_error(p_mgr, signal);
				signal = OSM_SIGNAL_NONE;
				break;
			}
			break;

		case OSM_SM_STATE_SET_SM_UCAST_LID_DONE:
			switch (signal) {
			case OSM_SIGNAL_NO_PENDING_TRANSACTIONS:
			case OSM_SIGNAL_DONE:
				__osm_state_mgr_set_sm_lid_done_msg(p_mgr);
				__osm_state_mgr_notify_lid_change(p_mgr);
				p_mgr->state = OSM_SM_STATE_SET_SUBNET_UCAST_LIDS;
				signal = osm_lid_mgr_process_subnet(p_mgr->p_lid_mgr);
				break;

			default:
				__osm_state_mgr_signal_error(p_mgr, signal);
				signal = OSM_SIGNAL_NONE;
				break;
			}
			break;

		case OSM_SM_STATE_SET_SUBNET_UCAST_LIDS:
			switch (signal) {
			case OSM_SIGNAL_DONE:
				/*
				 * The LID Manager is done processing.
				 * There are no outstanding transactions, so we
				 * can move on to configuring the forwarding tables.
				 */
				p_mgr->state = OSM_SM_STATE_SET_SUBNET_UCAST_LIDS_DONE;
				break;

			case OSM_SIGNAL_DONE_PENDING:
				/*
				 * The LID Manager is done processing.
				 * There are outstanding transactions, so we
				 * must wait for the wire to clear.
				 */
				p_mgr->state = OSM_SM_STATE_SET_SUBNET_UCAST_LIDS_WAIT;
				signal = OSM_SIGNAL_NONE;
				break;

			default:
				__osm_state_mgr_signal_error(p_mgr, signal);
				signal = OSM_SIGNAL_NONE;
				break;
			}
			break;

			/*
			 * In this state, the Unicast Manager has completed processing,
			 * but there are still transactions on the wire.  Therefore,
			 * wait here until the wire clears.
			 */
		case OSM_SM_STATE_SET_SUBNET_UCAST_LIDS_WAIT:
			switch (signal) {
			case OSM_SIGNAL_NO_PENDING_TRANSACTIONS:
				/*
				 * The LID Manager is done processing.
				 * There are no outstanding transactions, so we
				 * can move on to configuring the forwarding tables.
				 */
				p_mgr->state = OSM_SM_STATE_SET_SUBNET_UCAST_LIDS_DONE;
				break;

			default:
				__osm_state_mgr_signal_error(p_mgr, signal);
				signal = OSM_SIGNAL_NONE;
				break;
			}
			break;

		case OSM_SM_STATE_SET_SUBNET_UCAST_LIDS_DONE:
			switch (signal) {
			case OSM_SIGNAL_DONE:
			case OSM_SIGNAL_NO_PENDING_TRANSACTIONS:
				/* At this point we need to check the consistency of
				 * the port_lid_tbl under the subnet. There might be
				 * errors in it if PortInfo Set reqeusts didn't reach
				 * their destination. */
				__osm_state_mgr_check_tbl_consistency(p_mgr);

				__osm_state_mgr_lid_assign_msg(p_mgr);

				/*
				 * OK, the wire is clear, so proceed with
				 * unicast forwarding table configuration.
				 * First - send trap 64 on newly discovered endports
				 */
				__osm_state_mgr_report_new_ports(p_mgr);

				p_mgr->state = OSM_SM_STATE_SET_UCAST_TABLES;
				signal = osm_ucast_mgr_process(p_mgr->p_ucast_mgr);

				break;

			default:
				__osm_state_mgr_signal_error(p_mgr, signal);
				signal = OSM_SIGNAL_NONE;
				break;
			}
			break;

		case OSM_SM_STATE_SET_UCAST_TABLES:
			switch (signal) {
			case OSM_SIGNAL_DONE:
				p_mgr->state = OSM_SM_STATE_SET_UCAST_TABLES_DONE;
				break;

			case OSM_SIGNAL_DONE_PENDING:
				/*
				 * The Unicast Manager is done processing.
				 * There are outstanding transactions, so we
				 * must wait for the wire to clear.
				 */
				p_mgr->state = OSM_SM_STATE_SET_UCAST_TABLES_WAIT;
				signal = OSM_SIGNAL_NONE;
				break;

			default:
				__osm_state_mgr_signal_error(p_mgr, signal);
				signal = OSM_SIGNAL_NONE;
				break;
			}
			break;

		case OSM_SM_STATE_SET_UCAST_TABLES_WAIT:
			switch (signal) {
			case OSM_SIGNAL_NO_PENDING_TRANSACTIONS:
				p_mgr->state = OSM_SM_STATE_SET_UCAST_TABLES_DONE;
				break;

			default:
				__osm_state_mgr_signal_error(p_mgr, signal);
				signal = OSM_SIGNAL_NONE;
				break;
			}
			break;

		case OSM_SM_STATE_SET_UCAST_TABLES_DONE:
			switch (signal) {
			case OSM_SIGNAL_NO_PENDING_TRANSACTIONS:
			case OSM_SIGNAL_DONE:
				/* We are done setting all LFTs so clear the ignore existing.
				 * From now on, as long as we are still master, we want to
				 * take into account these lfts. */
				p_mgr->p_subn->ignore_existing_lfts = FALSE;

				__osm_state_mgr_switch_config_msg(p_mgr);

				if (!p_mgr->p_subn->opt.disable_multicast) {
					p_mgr->state = OSM_SM_STATE_SET_MCAST_TABLES;
					signal = osm_mcast_mgr_process(p_mgr->p_mcast_mgr);
				} else {
					p_mgr->state = OSM_SM_STATE_SET_LINK_PORTS;
					signal =
					    osm_link_mgr_process(p_mgr->
								 p_link_mgr, IB_LINK_NO_CHANGE);
				}
				break;

			default:
				__osm_state_mgr_signal_error(p_mgr, signal);
				signal = OSM_SIGNAL_NONE;
				break;
			}
			break;

		case OSM_SM_STATE_SET_MCAST_TABLES:
			switch (signal) {
			case OSM_SIGNAL_DONE:
				p_mgr->state = OSM_SM_STATE_SET_MCAST_TABLES_DONE;
				break;

			case OSM_SIGNAL_DONE_PENDING:
				/*
				 * The Multicast Manager is done processing.
				 * There are outstanding transactions, so we
				 * must wait for the wire to clear.
				 */
				p_mgr->state = OSM_SM_STATE_SET_MCAST_TABLES_WAIT;
				signal = OSM_SIGNAL_NONE;
				break;

			default:
				__osm_state_mgr_signal_error(p_mgr, signal);
				signal = OSM_SIGNAL_NONE;
				break;
			}
			break;

		case OSM_SM_STATE_SET_MCAST_TABLES_WAIT:
			switch (signal) {
			case OSM_SIGNAL_NO_PENDING_TRANSACTIONS:
				p_mgr->state = OSM_SM_STATE_SET_MCAST_TABLES_DONE;
				break;

			default:
				__osm_state_mgr_signal_error(p_mgr, signal);
				signal = OSM_SIGNAL_NONE;
				break;
			}
			break;

		case OSM_SM_STATE_SET_MCAST_TABLES_DONE:
			switch (signal) {
			case OSM_SIGNAL_NO_PENDING_TRANSACTIONS:
			case OSM_SIGNAL_DONE:
				__osm_state_mgr_multicast_config_msg(p_mgr);

				p_mgr->state = OSM_SM_STATE_SET_LINK_PORTS;
				signal = osm_link_mgr_process(p_mgr->p_link_mgr, IB_LINK_NO_CHANGE);
				break;

			default:
				__osm_state_mgr_signal_error(p_mgr, signal);
				signal = OSM_SIGNAL_NONE;
				break;
			}
			break;

			/*
			 * The LINK_PORTS state is required since we can not count on
			 * the port state change MADs to succeed. This is an artifact
			 * of the spec defining state change from state X to state X
			 * as an error. The hardware then is not required to process
			 * other parameters provided by the Set(PortInfo) Packet.
			 */
		case OSM_SM_STATE_SET_LINK_PORTS:
			switch (signal) {
			case OSM_SIGNAL_DONE:
				p_mgr->state = OSM_SM_STATE_SET_LINK_PORTS_DONE;
				break;

			case OSM_SIGNAL_DONE_PENDING:
				/*
				 * The Link Manager is done processing.
				 * There are outstanding transactions, so we
				 * must wait for the wire to clear.
				 */
				p_mgr->state = OSM_SM_STATE_SET_LINK_PORTS_WAIT;
				signal = OSM_SIGNAL_NONE;
				break;

			default:
				__osm_state_mgr_signal_error(p_mgr, signal);
				signal = OSM_SIGNAL_NONE;
				break;
			}
			break;

		case OSM_SM_STATE_SET_LINK_PORTS_WAIT:
			switch (signal) {
			case OSM_SIGNAL_NO_PENDING_TRANSACTIONS:
				p_mgr->state = OSM_SM_STATE_SET_LINK_PORTS_DONE;
				break;

			default:
				__osm_state_mgr_signal_error(p_mgr, signal);
				signal = OSM_SIGNAL_NONE;
				break;
			}
			break;

		case OSM_SM_STATE_SET_LINK_PORTS_DONE:
			switch (signal) {
			case OSM_SIGNAL_NO_PENDING_TRANSACTIONS:
			case OSM_SIGNAL_DONE:

				__osm_state_mgr_links_ports_msg(p_mgr);

				p_mgr->state = OSM_SM_STATE_SET_ARMED;
				signal = osm_link_mgr_process(p_mgr->p_link_mgr, IB_LINK_ARMED);
				break;

			default:
				__osm_state_mgr_signal_error(p_mgr, signal);
				signal = OSM_SIGNAL_NONE;
				break;
			}
			break;

		case OSM_SM_STATE_SET_ARMED:
			switch (signal) {
			case OSM_SIGNAL_DONE:
				p_mgr->state = OSM_SM_STATE_SET_ARMED_DONE;
				break;

			case OSM_SIGNAL_DONE_PENDING:
				/*
				 * The Link Manager is done processing.
				 * There are outstanding transactions, so we
				 * must wait for the wire to clear.
				 */
				p_mgr->state = OSM_SM_STATE_SET_ARMED_WAIT;
				signal = OSM_SIGNAL_NONE;
				break;

			default:
				__osm_state_mgr_signal_error(p_mgr, signal);
				signal = OSM_SIGNAL_NONE;
				break;
			}
			break;

		case OSM_SM_STATE_SET_ARMED_WAIT:
			switch (signal) {
			case OSM_SIGNAL_NO_PENDING_TRANSACTIONS:
				p_mgr->state = OSM_SM_STATE_SET_ARMED_DONE;
				break;

			default:
				__osm_state_mgr_signal_error(p_mgr, signal);
				signal = OSM_SIGNAL_NONE;
				break;
			}
			break;

		case OSM_SM_STATE_SET_ARMED_DONE:
			switch (signal) {
			case OSM_SIGNAL_NO_PENDING_TRANSACTIONS:
			case OSM_SIGNAL_DONE:

				__osm_state_mgr_links_armed_msg(p_mgr);

				p_mgr->state = OSM_SM_STATE_SET_ACTIVE;
				signal = osm_link_mgr_process(p_mgr->p_link_mgr, IB_LINK_ACTIVE);
				break;

			default:
				__osm_state_mgr_signal_error(p_mgr, signal);
				signal = OSM_SIGNAL_NONE;
				break;
			}
			break;

		case OSM_SM_STATE_SET_ACTIVE:
			switch (signal) {
			case OSM_SIGNAL_DONE:
				/*
				 * Don't change the signal, just the state.
				 */
				p_mgr->state = OSM_SM_STATE_SUBNET_UP;
				break;

			case OSM_SIGNAL_DONE_PENDING:
				/*
				 * The Link Manager is done processing.
				 * There are outstanding transactions, so we
				 * must wait for the wire to clear.
				 */
				p_mgr->state = OSM_SM_STATE_SET_ACTIVE_WAIT;
				signal = OSM_SIGNAL_NONE;
				break;

			default:
				__osm_state_mgr_signal_error(p_mgr, signal);
				signal = OSM_SIGNAL_NONE;
				break;
			}
			break;

		case OSM_SM_STATE_SET_ACTIVE_WAIT:
			switch (signal) {
			case OSM_SIGNAL_NO_PENDING_TRANSACTIONS:
				/*
				 * Don't change the signal, just the state.
				 */
				p_mgr->state = OSM_SM_STATE_SUBNET_UP;
				break;

			default:
				__osm_state_mgr_signal_error(p_mgr, signal);
				signal = OSM_SIGNAL_NONE;
				break;
			}
			break;

		case OSM_SM_STATE_SUBNET_UP:
			switch (signal) {
			case OSM_SIGNAL_NO_PENDING_TRANSACTIONS:
			case OSM_SIGNAL_DONE:
				/*
				 * The sweep completed!
				 */

				/* in any case we zero this flag */
				p_mgr->p_subn->coming_out_of_standby = FALSE;

				/* If there were errors - then the subnet is not really up */
				if (p_mgr->p_subn->subnet_initialization_error == TRUE) {
					__osm_state_mgr_init_errors_msg(p_mgr);
				} else {
					/* The subnet is up correctly - set the first_time_master_sweep flag
					 * (if it is on) to FALSE. */
					if (p_mgr->p_subn->first_time_master_sweep == TRUE) {
						p_mgr->p_subn->first_time_master_sweep = FALSE;
					}
					p_mgr->p_subn->need_update = 0;

					__osm_topology_file_create(p_mgr);
					osm_dump_all(p_mgr->p_subn->p_osm);
					__osm_state_mgr_report(p_mgr);
					__osm_state_mgr_up_msg(p_mgr);

					if (osm_log_is_active(p_mgr->p_log, OSM_LOG_VERBOSE))
						osm_sa_db_file_dump(p_mgr->p_subn->p_osm);
				}
				p_mgr->state = OSM_SM_STATE_PROCESS_REQUEST;
				signal = OSM_SIGNAL_IDLE_TIME_PROCESS;

				/*
				 * Finally signal the subnet up event
				 */
				status =
				    cl_event_signal(p_mgr->p_subnet_up_event);
				if (status != IB_SUCCESS) {
					osm_log(p_mgr->p_log, OSM_LOG_ERROR,
						"osm_state_mgr_process: ERR 3319: "
						"Invalid SM state %u\n",
						p_mgr->state);
				}
				break;

			default:
				__osm_state_mgr_signal_error(p_mgr, signal);
				signal = OSM_SIGNAL_NONE;
				break;
			}
			break;

		case OSM_SM_STATE_MASTER_OR_HIGHER_SM_DETECTED:
			switch (signal) {
			case OSM_SIGNAL_CHANGE_DETECTED:
				/*
				 * Nothing to do here. One subnet change typically
				 * begets another....
				 */
				break;

			case OSM_SIGNAL_MASTER_OR_HIGHER_SM_DETECTED:
				/*
				 * If we lost once, we might lose again. Nothing to do.
				 */
				break;

			case OSM_SIGNAL_NO_PENDING_TRANSACTIONS:
				p_mgr->state = OSM_SM_STATE_STANDBY;
				/*
				 * Call the sm_state_mgr with signal
				 * MASTER_OR_HIGHER_SM_DETECTED_DONE
				 */
				osm_sm_state_mgr_process(p_mgr->p_sm_state_mgr,
							 OSM_SM_SIGNAL_MASTER_OR_HIGHER_SM_DETECTED_DONE);
				__osm_state_mgr_standby_msg(p_mgr);
				break;

			default:
				__osm_state_mgr_signal_error(p_mgr, signal);
				break;
			}
			signal = OSM_SIGNAL_NONE;
			break;

		case OSM_SM_STATE_STANDBY:
			switch (signal) {
			case OSM_SIGNAL_EXIT_STBY:
				/*
				 * Need to force re-write of sm_base_lid to all ports
				 * to do that we want all the ports to be considered
				 * foriegn
				 */
				signal = OSM_SIGNAL_SWEEP;
				__osm_state_mgr_clean_known_lids(p_mgr);
				p_mgr->state = OSM_SM_STATE_IDLE;
				break;

			case OSM_SIGNAL_NO_PENDING_TRANSACTIONS:
				/*
				 * Nothing to do here - need to stay at this state
				 */
				signal = OSM_SIGNAL_NONE;
				break;

			default:
				__osm_state_mgr_signal_error(p_mgr, signal);
				signal = OSM_SIGNAL_NONE;
				break;
			}
			/* stay with the same signal - so we can start the sweep */
			break;

		default:
			CL_ASSERT(FALSE);
			osm_log(p_mgr->p_log, OSM_LOG_ERROR,
				"osm_state_mgr_process: ERR 3320: "
				"Invalid SM state %u\n", p_mgr->state);
			p_mgr->state = OSM_SM_STATE_IDLE;
			signal = OSM_SIGNAL_NONE;
			break;
		}

		/* if we got a signal to force immediate heavy sweep in the middle of the sweep -
		 * try another sweep. */
		if ((p_mgr->p_subn->force_immediate_heavy_sweep) &&
		    (p_mgr->state == OSM_SM_STATE_IDLE)) {
			signal = OSM_SIGNAL_SWEEP;
		}
		/* if we got errors during the initialization in the middle of the sweep -
		 * try another sweep. */
		if ((p_mgr->p_subn->subnet_initialization_error) &&
		    (p_mgr->state == OSM_SM_STATE_IDLE)) {
			signal = OSM_SIGNAL_SWEEP;
		}

	}

	cl_spinlock_release(&p_mgr->state_lock);

	OSM_LOG_EXIT(p_mgr->p_log);
}

/**********************************************************************
 **********************************************************************/
ib_api_status_t
osm_state_mgr_process_idle(IN osm_state_mgr_t * const p_mgr,
			   IN osm_pfn_start_t pfn_start,
			   IN osm_pfn_done_t pfn_done, void *context1,
			   void *context2)
{
	osm_idle_item_t *p_idle_item;

	OSM_LOG_ENTER(p_mgr->p_log, osm_state_mgr_process_idle);

	p_idle_item = malloc(sizeof(osm_idle_item_t));
	if (p_idle_item == NULL) {
		osm_log(p_mgr->p_log, OSM_LOG_ERROR,
			"osm_state_mgr_process_idle: ERR 3321: "
			"insufficient memory\n");
		return IB_ERROR;
	}

	memset(p_idle_item, 0, sizeof(osm_idle_item_t));
	p_idle_item->pfn_start = pfn_start;
	p_idle_item->pfn_done = pfn_done;
	p_idle_item->context1 = context1;
	p_idle_item->context2 = context2;

	cl_spinlock_acquire(&p_mgr->idle_lock);
	cl_qlist_insert_tail(&p_mgr->idle_time_list, &p_idle_item->list_item);
	cl_spinlock_release(&p_mgr->idle_lock);

	osm_state_mgr_process(p_mgr, OSM_SIGNAL_IDLE_TIME_PROCESS_REQUEST);

	OSM_LOG_EXIT(p_mgr->p_log);

	return IB_SUCCESS;
}

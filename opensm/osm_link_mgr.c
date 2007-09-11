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
 *    Implementation of osm_link_mgr_t.
 * This file implements the Link Manager object.
 *
 * Environment:
 *    Linux User Mode
 *
 * $Revision: 1.15 $
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <string.h>
#include <iba/ib_types.h>
#include <complib/cl_debug.h>
#include <opensm/osm_link_mgr.h>
#include <opensm/osm_node.h>
#include <opensm/osm_switch.h>
#include <opensm/osm_helper.h>
#include <opensm/osm_msgdef.h>

/**********************************************************************
 **********************************************************************/
void osm_link_mgr_construct(IN osm_link_mgr_t * const p_mgr)
{
	memset(p_mgr, 0, sizeof(*p_mgr));
}

/**********************************************************************
 **********************************************************************/
void osm_link_mgr_destroy(IN osm_link_mgr_t * const p_mgr)
{
	OSM_LOG_ENTER(p_mgr->p_log, osm_link_mgr_destroy);

	OSM_LOG_EXIT(p_mgr->p_log);
}

/**********************************************************************
 **********************************************************************/
ib_api_status_t
osm_link_mgr_init(IN osm_link_mgr_t * const p_mgr,
		  IN osm_req_t * const p_req,
		  IN osm_subn_t * const p_subn,
		  IN osm_log_t * const p_log, IN cl_plock_t * const p_lock)
{
	ib_api_status_t status = IB_SUCCESS;

	OSM_LOG_ENTER(p_log, osm_link_mgr_init);

	CL_ASSERT(p_req);
	CL_ASSERT(p_subn);
	CL_ASSERT(p_lock);

	osm_link_mgr_construct(p_mgr);

	p_mgr->p_log = p_log;
	p_mgr->p_subn = p_subn;
	p_mgr->p_lock = p_lock;
	p_mgr->p_req = p_req;

	OSM_LOG_EXIT(p_mgr->p_log);
	return (status);
}

/**********************************************************************
 **********************************************************************/
static void
__osm_link_mgr_set_physp_pi(IN osm_link_mgr_t * const p_mgr,
			    IN osm_physp_t * const p_physp,
			    IN uint8_t const port_state)
{
	uint8_t payload[IB_SMP_DATA_SIZE];
	ib_port_info_t *const p_pi = (ib_port_info_t *) payload;
	const ib_port_info_t *p_old_pi;
	osm_madw_context_t context;
	osm_node_t *p_node;
	ib_api_status_t status;
	uint8_t port_num;
	uint8_t mtu;
	uint8_t op_vls;
	boolean_t esp0 = FALSE;
	boolean_t send_set = FALSE;
	osm_physp_t *p_remote_physp;

	OSM_LOG_ENTER(p_mgr->p_log, __osm_link_mgr_set_physp_pi);

	CL_ASSERT(p_physp);
	CL_ASSERT(osm_physp_is_valid(p_physp));

	p_node = osm_physp_get_node_ptr(p_physp);

	port_num = osm_physp_get_port_num(p_physp);

	if (port_num == 0) {
		/*
		   CAs don't have a port 0, and for switch port 0,
		   we need to check if this is enhanced or base port 0.
		   For base port 0 the following parameters are not valid (p822, table 145).
		 */
		if (!p_node->sw) {
			osm_log(p_mgr->p_log, OSM_LOG_ERROR,
				"__osm_link_mgr_set_physp_pi: ERR 4201: "
				"Cannot find switch by guid: 0x%" PRIx64 "\n",
				cl_ntoh64(p_node->node_info.node_guid));
			goto Exit;
		}

		if (ib_switch_info_is_enhanced_port0(&p_node->sw->switch_info)
		    == FALSE) {
			/* This means the switch doesn't support enhanced port 0.
			   Can skip it. */
			if (osm_log_is_active(p_mgr->p_log, OSM_LOG_DEBUG))
				osm_log(p_mgr->p_log, OSM_LOG_DEBUG,
					"__osm_link_mgr_set_physp_pi: "
					"Skipping port 0, GUID 0x%016" PRIx64
					"\n",
					cl_ntoh64(osm_physp_get_port_guid
						  (p_physp)));
			goto Exit;
		}
		esp0 = TRUE;
	}

	/*
	   PAST THIS POINT WE ARE HANDLING EITHER A NON PORT 0 OR ENHANCED PORT 0
	 */

	p_old_pi = &p_physp->port_info;

	memset(payload, 0, IB_SMP_DATA_SIZE);

	/* Correction by FUJITSU */
	memcpy(payload, p_old_pi, sizeof(ib_port_info_t));

	/*
	   Correction following a bug injected by the previous
	   FUJITSU line:

	   Should never write back a value that is bigger then 3 in
	   the PortPhysicalState field - so can not simply copy!

	   Actually we want to write there:
	   port physical state - no change,
	   link down default state = polling
	   port state - no change
	 */
	p_pi->state_info2 = 0x02;
	ib_port_info_set_port_state(p_pi, IB_LINK_NO_CHANGE);
	if (ib_port_info_get_link_down_def_state(p_pi) !=
	    ib_port_info_get_link_down_def_state(p_old_pi))
		send_set = TRUE;

	/* we only change port fields if we do not change state */
	if (port_state == IB_LINK_NO_CHANGE) {
		/* The following fields are relevant only for CA port, router, or Enh. SP0 */
		if (osm_node_get_type(p_node) != IB_NODE_TYPE_SWITCH ||
		    port_num == 0) {
			p_pi->m_key = p_mgr->p_subn->opt.m_key;
			if (memcmp(&p_pi->m_key, &p_old_pi->m_key,
				   sizeof(p_pi->m_key)))
				send_set = TRUE;

			p_pi->subnet_prefix = p_mgr->p_subn->opt.subnet_prefix;
			if (memcmp(&p_pi->subnet_prefix,
				   &p_old_pi->subnet_prefix,
				   sizeof(p_pi->subnet_prefix)))
				send_set = TRUE;

			p_pi->base_lid = osm_physp_get_base_lid(p_physp);
			if (memcmp(&p_pi->base_lid, &p_old_pi->base_lid,
				   sizeof(p_pi->base_lid)))
				send_set = TRUE;

			/* we are initializing the ports with our local sm_base_lid */
			p_pi->master_sm_base_lid = p_mgr->p_subn->sm_base_lid;
			if (memcmp(&p_pi->master_sm_base_lid,
				   &p_old_pi->master_sm_base_lid,
				   sizeof(p_pi->master_sm_base_lid)))
				send_set = TRUE;

			p_pi->m_key_lease_period =
			    p_mgr->p_subn->opt.m_key_lease_period;
			if (memcmp(&p_pi->m_key_lease_period,
				   &p_old_pi->m_key_lease_period,
				   sizeof(p_pi->m_key_lease_period)))
				send_set = TRUE;

			if (esp0 == FALSE)
				p_pi->mkey_lmc = p_mgr->p_subn->opt.lmc;
			else {
				if (p_mgr->p_subn->opt.lmc_esp0)
					p_pi->mkey_lmc = p_mgr->p_subn->opt.lmc;
				else
					p_pi->mkey_lmc = 0;
			}
			if (memcmp(&p_pi->mkey_lmc, &p_old_pi->mkey_lmc,
				   sizeof(p_pi->mkey_lmc)))
				send_set = TRUE;

			ib_port_info_set_timeout(p_pi,
						 p_mgr->p_subn->opt.
						 subnet_timeout);
			if (ib_port_info_get_timeout(p_pi) !=
			    ib_port_info_get_timeout(p_old_pi))
				send_set = TRUE;
		}

		/*
		   Several timeout mechanisms:
		 */
		p_remote_physp = osm_physp_get_remote(p_physp);
		if (port_num != 0 && p_remote_physp &&
		    osm_physp_is_valid(p_remote_physp)) {
			if (osm_node_get_type(osm_physp_get_node_ptr(p_physp))
			    == IB_NODE_TYPE_ROUTER) {
				ib_port_info_set_hoq_lifetime(p_pi,
							      p_mgr->p_subn->
							      opt.
							      leaf_head_of_queue_lifetime);
			} else
			    if (osm_node_get_type
				(osm_physp_get_node_ptr(p_physp)) ==
				IB_NODE_TYPE_SWITCH) {
				/* Is remote end CA or router (a leaf port) ? */
				if (osm_node_get_type
				    (osm_physp_get_node_ptr(p_remote_physp)) !=
				    IB_NODE_TYPE_SWITCH) {
					ib_port_info_set_hoq_lifetime(p_pi,
								      p_mgr->
								      p_subn->
								      opt.
								      leaf_head_of_queue_lifetime);
					ib_port_info_set_vl_stall_count(p_pi,
									p_mgr->
									p_subn->
									opt.
									leaf_vl_stall_count);
				} else {
					ib_port_info_set_hoq_lifetime(p_pi,
								      p_mgr->
								      p_subn->
								      opt.
								      head_of_queue_lifetime);
					ib_port_info_set_vl_stall_count(p_pi,
									p_mgr->
									p_subn->
									opt.
									vl_stall_count);
				}
			}
			if (ib_port_info_get_hoq_lifetime(p_pi) !=
			    ib_port_info_get_hoq_lifetime(p_old_pi) ||
			    ib_port_info_get_vl_stall_count(p_pi) !=
			    ib_port_info_get_vl_stall_count(p_old_pi))
				send_set = TRUE;
		}

		ib_port_info_set_phy_and_overrun_err_thd(p_pi,
							 p_mgr->p_subn->opt.
							 local_phy_errors_threshold,
							 p_mgr->p_subn->opt.
							 overrun_errors_threshold);
		if (memcmp(&p_pi->error_threshold, &p_old_pi->error_threshold,
			   sizeof(p_pi->error_threshold)))
			send_set = TRUE;

		/*
		   Set the easy common parameters for all port types,
		   then determine the neighbor MTU.
		 */
		p_pi->link_width_enabled = p_old_pi->link_width_supported;
		if (memcmp(&p_pi->link_width_enabled,
			   &p_old_pi->link_width_enabled,
			   sizeof(p_pi->link_width_enabled)))
			send_set = TRUE;

		if (p_mgr->p_subn->opt.force_link_speed) {
			if (p_mgr->p_subn->opt.force_link_speed == 15) {	/* LinkSpeedSupported */
				if (ib_port_info_get_link_speed_enabled
				    (p_old_pi) !=
				    ib_port_info_get_link_speed_sup(p_pi))
					ib_port_info_set_link_speed_enabled
					    (p_pi,
					     IB_PORT_LINK_SPEED_ENABLED_MASK);
				else
					ib_port_info_set_link_speed_enabled
					    (p_pi,
					     ib_port_info_get_link_speed_enabled
					     (p_old_pi));
			} else
				ib_port_info_set_link_speed_enabled(p_pi,
								    p_mgr->
								    p_subn->opt.
								    force_link_speed);
			if (memcmp(&p_pi->link_speed, &p_old_pi->link_speed,
				   sizeof(p_pi->link_speed)))
				send_set = TRUE;
		}

		/* calc new op_vls and mtu */
		op_vls =
		    osm_physp_calc_link_op_vls(p_mgr->p_log, p_mgr->p_subn,
					       p_physp);
		mtu = osm_physp_calc_link_mtu(p_mgr->p_log, p_physp);

		ib_port_info_set_neighbor_mtu(p_pi, mtu);
		if (ib_port_info_get_neighbor_mtu(p_pi) !=
		    ib_port_info_get_neighbor_mtu(p_old_pi))
			send_set = TRUE;

		ib_port_info_set_op_vls(p_pi, op_vls);
		if (ib_port_info_get_op_vls(p_pi) !=
		    ib_port_info_get_op_vls(p_old_pi))
			send_set = TRUE;

		/* provide the vl_high_limit from the qos mgr */
		if (p_mgr->p_subn->opt.qos &&
		    p_physp->vl_high_limit != p_old_pi->vl_high_limit) {
			send_set = TRUE;
			p_pi->vl_high_limit = p_physp->vl_high_limit;
		}

		/* also the context can flag the need to check for errors. */
		context.pi_context.ignore_errors = FALSE;
	} else
		/*
		   Since the only change we try to do is to modify the port
		   state we can ignore the errors that might be caused by a
		   race in setting the state and the actual state the port is
		   in.
		 */
		context.pi_context.ignore_errors = FALSE;

	ib_port_info_set_port_state(p_pi, port_state);
	if (port_state != IB_LINK_NO_CHANGE &&
	    ib_port_info_get_port_state(p_pi) !=
	    ib_port_info_get_port_state(p_old_pi)) {
		send_set = TRUE;
		if (port_state == IB_LINK_ACTIVE)
			context.pi_context.active_transition = TRUE;
		else
			context.pi_context.active_transition = FALSE;
	}

	context.pi_context.node_guid = osm_node_get_node_guid(p_node);
	context.pi_context.port_guid = osm_physp_get_port_guid(p_physp);
	context.pi_context.set_method = TRUE;
	context.pi_context.update_master_sm_base_lid = FALSE;
	context.pi_context.light_sweep = FALSE;

	/* We need to send the PortInfoSet request with the new sm_lid
	   in the following cases:
	   1. There is a change in the values (send_set == TRUE)
	   2. This is an ca port or a switch port 0 and got_set_resp is FALSE
	   (in this case we sent a PortInfoSet in the osm_lid_mgr, but for some
	   reason we didn't get a response) - try and re-send.
	   3. This is a switch port and:
	   a. first_time_master_sweep flag on the subnet is TRUE. This means the
	   SM just became master, and it then needs to send at PortInfoSet to
	   every port (and this is the first time we can send a PortInfoSet to
	   switch external ports).
	   b. got_set_resp on the physical port is FALSE. This means we haven't
	   seen this port before - need to send PortInfoSet to it.
	 */
	if (send_set ||
	    (osm_node_get_type(p_node) != IB_NODE_TYPE_SWITCH
	     && p_physp->got_set_resp == FALSE)
	    || (osm_node_get_type(p_node) == IB_NODE_TYPE_SWITCH
		&& port_num == 0 && p_physp->got_set_resp == FALSE)
	    || (osm_node_get_type(p_node) == IB_NODE_TYPE_SWITCH
		&& port_num != 0
		&& (p_mgr->p_subn->first_time_master_sweep == TRUE
		    || p_physp->got_set_resp == FALSE))) {
		p_mgr->send_set_reqs = TRUE;
		status = osm_req_set(p_mgr->p_req,
				     osm_physp_get_dr_path_ptr(p_physp),
				     payload,
				     sizeof(payload),
				     IB_MAD_ATTR_PORT_INFO,
				     cl_hton32(port_num),
				     CL_DISP_MSGID_NONE, &context);
	}

      Exit:
	OSM_LOG_EXIT(p_mgr->p_log);
}

/**********************************************************************
 **********************************************************************/
static osm_signal_t
__osm_link_mgr_process_node(IN osm_link_mgr_t * const p_mgr,
			    IN osm_node_t * const p_node,
			    IN const uint8_t link_state)
{
	uint32_t i;
	uint32_t num_physp;
	osm_physp_t *p_physp;
	uint8_t current_state;
	osm_signal_t signal = OSM_SIGNAL_DONE;

	OSM_LOG_ENTER(p_mgr->p_log, __osm_link_mgr_process_node);

	if (osm_log_is_active(p_mgr->p_log, OSM_LOG_DEBUG))
		osm_log(p_mgr->p_log, OSM_LOG_DEBUG,
			"__osm_link_mgr_process_node: "
			"Node 0x%" PRIx64 " going to %s\n",
			cl_ntoh64(osm_node_get_node_guid(p_node)),
			ib_get_port_state_str(link_state));

	/*
	   Set the PortInfo for every Physical Port associated
	   with this Port.  Start iterating with port 1, since the linkstate
	   is not applicable to the management port on switches.
	 */
	num_physp = osm_node_get_num_physp(p_node);
	for (i = 0; i < num_physp; i++) {
		/*
		   Don't bother doing anything if this Physical Port is not valid.
		   or if the state of the port is already better then the
		   specified state.
		 */
		p_physp = osm_node_get_physp_ptr(p_node, (uint8_t) i);
		if (!osm_physp_is_valid(p_physp))
			continue;

		current_state = osm_physp_get_port_state(p_physp);

		if (current_state == IB_LINK_DOWN)
			continue;

		/*
		   Normally we only send state update if state is lower
		   then required state. However, we need to send update if
		   no state change required.
		 */
		if ((link_state == IB_LINK_NO_CHANGE) ||
		    (current_state < link_state)) {
			p_mgr->send_set_reqs = FALSE;
			__osm_link_mgr_set_physp_pi(p_mgr, p_physp, link_state);

			if (p_mgr->send_set_reqs == TRUE)
				signal = OSM_SIGNAL_DONE_PENDING;
		} else if (osm_log_is_active(p_mgr->p_log, OSM_LOG_DEBUG))
			osm_log(p_mgr->p_log, OSM_LOG_DEBUG,
				"__osm_link_mgr_process_node: "
				"Physical port 0x%X already %s. Skipping\n",
				p_physp->port_num,
				ib_get_port_state_str(current_state));
	}

	OSM_LOG_EXIT(p_mgr->p_log);
	return (signal);
}

/**********************************************************************
 **********************************************************************/
osm_signal_t
osm_link_mgr_process(IN osm_link_mgr_t * const p_mgr,
		     IN const uint8_t link_state)
{
	cl_qmap_t *p_node_guid_tbl;
	osm_node_t *p_node;
	osm_signal_t signal = OSM_SIGNAL_DONE;

	OSM_LOG_ENTER(p_mgr->p_log, osm_link_mgr_process);

	p_node_guid_tbl = &p_mgr->p_subn->node_guid_tbl;

	CL_PLOCK_EXCL_ACQUIRE(p_mgr->p_lock);

	for (p_node = (osm_node_t *) cl_qmap_head(p_node_guid_tbl);
	     p_node != (osm_node_t *) cl_qmap_end(p_node_guid_tbl);
	     p_node = (osm_node_t *) cl_qmap_next(&p_node->map_item)) {
		if (__osm_link_mgr_process_node(p_mgr, p_node, link_state) ==
		    OSM_SIGNAL_DONE_PENDING)
			signal = OSM_SIGNAL_DONE_PENDING;
	}

	CL_PLOCK_RELEASE(p_mgr->p_lock);

	OSM_LOG_EXIT(p_mgr->p_log);
	return (signal);
}

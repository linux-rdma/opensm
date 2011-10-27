/*
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2011 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
 * Copyright (c) 2009 Sun Microsystems, Inc. All rights reserved.
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
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <string.h>
#include <iba/ib_types.h>
#include <complib/cl_debug.h>
#include <opensm/osm_sm.h>
#include <opensm/osm_node.h>
#include <opensm/osm_switch.h>
#include <opensm/osm_helper.h>
#include <opensm/osm_msgdef.h>
#include <opensm/osm_opensm.h>

static uint8_t link_mgr_get_smsl(IN osm_sm_t * sm, IN osm_physp_t * p_physp)
{
	osm_opensm_t *p_osm = sm->p_subn->p_osm;
	struct osm_routing_engine *re = p_osm->routing_engine_used;
	const osm_port_t *p_sm_port, *p_src_port;
	ib_net16_t slid;
	uint8_t sl;

	OSM_LOG_ENTER(sm->p_log);

	if (!(re && re->path_sl &&
	      (slid = osm_physp_get_base_lid(p_physp)))) {
		/*
		 * Use default SL if routing engine does not provide a
		 * path SL lookup callback.
		 */
		OSM_LOG_EXIT(sm->p_log);
		return sm->p_subn->opt.sm_sl;
	}

	/* Find osm_port of the SM itself = dest_port */
	p_sm_port = osm_get_port_by_lid(sm->p_subn, sm->p_subn->sm_base_lid);

	/* Find osm_port of the source = p_physp */
	p_src_port = osm_get_port_by_lid(sm->p_subn, slid);

	/* Call into routing engine to find proper SL */
	sl = re->path_sl(re->context, sm->p_subn->opt.sm_sl,
			 p_src_port, p_sm_port);

	OSM_LOG_EXIT(sm->p_log);
	return sl;
}

static int link_mgr_set_physp_pi(osm_sm_t * sm, IN osm_physp_t * p_physp,
				 IN uint8_t port_state)
{
	uint8_t payload[IB_SMP_DATA_SIZE], payload2[IB_SMP_DATA_SIZE];
	ib_port_info_t *p_pi = (ib_port_info_t *) payload;
	ib_mlnx_ext_port_info_t *p_epi = (ib_mlnx_ext_port_info_t *) payload2;
	const ib_port_info_t *p_old_pi;
	const ib_mlnx_ext_port_info_t *p_old_epi;
	osm_madw_context_t context;
	osm_node_t *p_node;
	ib_api_status_t status;
	uint8_t port_num, mtu, op_vls, smsl = OSM_DEFAULT_SL;
	boolean_t esp0 = FALSE, send_set = FALSE, send_set2 = FALSE;
	osm_physp_t *p_remote_physp, *physp0;
	int qdr_change = 0, fdr10_change = 0;
	int ret = 0;
	ib_net32_t attr_mod, cap_mask;

	OSM_LOG_ENTER(sm->p_log);

	p_node = osm_physp_get_node_ptr(p_physp);

	p_old_pi = &p_physp->port_info;

	port_num = osm_physp_get_port_num(p_physp);

	if (port_num == 0) {
		/*
		   CAs don't have a port 0, and for switch port 0,
		   we need to check if this is enhanced or base port 0.
		   For base port 0 the following parameters are not valid
		   (IBA 1.2.1 p.830 table 146).
		 */
		if (!p_node->sw) {
			OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 4201: "
				"Cannot find switch by guid: 0x%" PRIx64 "\n",
				cl_ntoh64(p_node->node_info.node_guid));
			goto Exit;
		}

		if (ib_switch_info_is_enhanced_port0(&p_node->sw->switch_info)
		    == FALSE) {

			/* Even for base port 0 we might have to set smsl
			   (if we are using lash routing) */
			smsl = link_mgr_get_smsl(sm, p_physp);
			if (smsl != ib_port_info_get_master_smsl(p_old_pi)) {
				send_set = TRUE;
				OSM_LOG(sm->p_log, OSM_LOG_DEBUG,
					"Setting SMSL to %d on port 0 GUID 0x%016"
					PRIx64 "\n", smsl,
					cl_ntoh64(osm_physp_get_port_guid
						  (p_physp)));
			} else {
				/* This means the switch doesn't support
				   enhanced port 0 and we don't need to
				   change SMSL. Can skip it. */
				OSM_LOG(sm->p_log, OSM_LOG_DEBUG,
					"Skipping port 0, GUID 0x%016" PRIx64
					"\n",
					cl_ntoh64(osm_physp_get_port_guid
						  (p_physp)));
				goto Exit;
			}
		} else
			esp0 = TRUE;
	}

	memcpy(payload, p_old_pi, sizeof(ib_port_info_t));

	/*
	   Should never write back a value that is bigger then 3 in
	   the PortPhysicalState field - so can not simply copy!

	   Actually we want to write there:
	   port physical state - no change,
	   link down default state = polling
	   port state - as requested.
	 */
	p_pi->state_info2 = 0x02;
	ib_port_info_set_port_state(p_pi, port_state);

	/* Check whether this is base port0 smsl handling only */
	if (port_num == 0 && esp0 == FALSE) {
		ib_port_info_set_master_smsl(p_pi, smsl);
		goto Send;
	}

	/*
	   PAST THIS POINT WE ARE HANDLING EITHER A NON PORT 0 OR ENHANCED PORT 0
	 */

	if (ib_port_info_get_link_down_def_state(p_pi) !=
	    ib_port_info_get_link_down_def_state(p_old_pi))
		send_set = TRUE;

	/* didn't get PortInfo before */
	if (!ib_port_info_get_port_state(p_old_pi))
		send_set = TRUE;

	/* we only change port fields if we do not change state */
	if (port_state == IB_LINK_NO_CHANGE) {
		/* The following fields are relevant only for CA port, router, or Enh. SP0 */
		if (osm_node_get_type(p_node) != IB_NODE_TYPE_SWITCH ||
		    port_num == 0) {
			p_pi->m_key = sm->p_subn->opt.m_key;
			if (memcmp(&p_pi->m_key, &p_old_pi->m_key,
				   sizeof(p_pi->m_key)))
				send_set = TRUE;

			p_pi->subnet_prefix = sm->p_subn->opt.subnet_prefix;
			if (memcmp(&p_pi->subnet_prefix,
				   &p_old_pi->subnet_prefix,
				   sizeof(p_pi->subnet_prefix)))
				send_set = TRUE;

			p_pi->base_lid = osm_physp_get_base_lid(p_physp);
			if (memcmp(&p_pi->base_lid, &p_old_pi->base_lid,
				   sizeof(p_pi->base_lid)))
				send_set = TRUE;

			/* we are initializing the ports with our local sm_base_lid */
			p_pi->master_sm_base_lid = sm->p_subn->sm_base_lid;
			if (memcmp(&p_pi->master_sm_base_lid,
				   &p_old_pi->master_sm_base_lid,
				   sizeof(p_pi->master_sm_base_lid)))
				send_set = TRUE;

			smsl = link_mgr_get_smsl(sm, p_physp);
			if (smsl != ib_port_info_get_master_smsl(p_old_pi)) {

				ib_port_info_set_master_smsl(p_pi, smsl);

				OSM_LOG(sm->p_log, OSM_LOG_DEBUG,
					"Setting SMSL to %d on GUID 0x%016"
					PRIx64 ", port %d\n", smsl,
					cl_ntoh64(osm_physp_get_port_guid
						  (p_physp)), port_num);

				send_set = TRUE;
			}

			p_pi->m_key_lease_period =
			    sm->p_subn->opt.m_key_lease_period;
			if (memcmp(&p_pi->m_key_lease_period,
				   &p_old_pi->m_key_lease_period,
				   sizeof(p_pi->m_key_lease_period)))
				send_set = TRUE;

			/* M_KeyProtectBits are currently always zero */
			p_pi->mkey_lmc = 0;
			if (esp0 == FALSE || sm->p_subn->opt.lmc_esp0)
				ib_port_info_set_lmc(p_pi, sm->p_subn->opt.lmc);
			if (ib_port_info_get_lmc(p_old_pi) !=
			    ib_port_info_get_lmc(p_pi) ||
			    ib_port_info_get_mpb(p_old_pi) !=
			    ib_port_info_get_mpb(p_pi))
				send_set = TRUE;

			ib_port_info_set_timeout(p_pi,
						 sm->p_subn->opt.
						 subnet_timeout);
			if (ib_port_info_get_timeout(p_pi) !=
			    ib_port_info_get_timeout(p_old_pi))
				send_set = TRUE;
		}

		/*
		   Several timeout mechanisms:
		 */
		p_remote_physp = osm_physp_get_remote(p_physp);
		if (port_num != 0 && p_remote_physp) {
			if (osm_node_get_type(osm_physp_get_node_ptr(p_physp))
			    == IB_NODE_TYPE_ROUTER) {
				ib_port_info_set_hoq_lifetime(p_pi,
							      sm->p_subn->
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
								      sm->
								      p_subn->
								      opt.
								      leaf_head_of_queue_lifetime);
					ib_port_info_set_vl_stall_count(p_pi,
									sm->
									p_subn->
									opt.
									leaf_vl_stall_count);
				} else {
					ib_port_info_set_hoq_lifetime(p_pi,
								      sm->
								      p_subn->
								      opt.
								      head_of_queue_lifetime);
					ib_port_info_set_vl_stall_count(p_pi,
									sm->
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
							 sm->p_subn->opt.
							 local_phy_errors_threshold,
							 sm->p_subn->opt.
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

		if (sm->p_subn->opt.force_link_speed &&
		    (sm->p_subn->opt.force_link_speed != 15 ||
		     ib_port_info_get_link_speed_enabled(p_pi) !=
		     ib_port_info_get_link_speed_sup(p_pi))) {
			ib_port_info_set_link_speed_enabled(p_pi,
							    sm->p_subn->opt.
							    force_link_speed);
			if (memcmp(&p_pi->link_speed, &p_old_pi->link_speed,
				   sizeof(p_pi->link_speed))) {
				send_set = TRUE;
				/* Determine whether QDR in LSE is being changed */
				if ((ib_port_info_get_link_speed_enabled(p_pi) &
				     IB_LINK_SPEED_ACTIVE_10 &&
				     !(ib_port_info_get_link_speed_enabled(p_old_pi) &
				      IB_LINK_SPEED_ACTIVE_10)) ||
				    ((!(ib_port_info_get_link_speed_enabled(p_pi) &
				       IB_LINK_SPEED_ACTIVE_10) &&
				      ib_port_info_get_link_speed_enabled(p_old_pi) &
				      IB_LINK_SPEED_ACTIVE_10)))
				qdr_change = 1;
			}
		}

		if (sm->p_subn->opt.fdr10 &&
		    p_physp->ext_port_info.link_speed_supported & FDR10) {
			if (sm->p_subn->opt.fdr10 == 1) { /* enable */
				if (!(p_physp->ext_port_info.link_speed_enabled & FDR10))
					fdr10_change = 1;
			} else {	/* disable */
				if (p_physp->ext_port_info.link_speed_enabled & FDR10)
					fdr10_change = 1;
			}
			if (fdr10_change) {
				p_old_epi = &p_physp->ext_port_info;
				memcpy(payload2, p_old_epi,
				       sizeof(ib_mlnx_ext_port_info_t));
				p_epi->state_change_enable = 0x01;
				if (sm->p_subn->opt.fdr10 == 1)
					p_epi->link_speed_enabled = FDR10;
				else
					p_epi->link_speed_enabled = 0;
				send_set2 = TRUE;
			}
		}

		if (osm_node_get_type(p_physp->p_node) == IB_NODE_TYPE_SWITCH) {
			physp0 = osm_node_get_physp_ptr(p_physp->p_node, 0);
			cap_mask = physp0->port_info.capability_mask;
		} else
			cap_mask = p_pi->capability_mask;
		if (!(cap_mask & IB_PORT_CAP_HAS_EXT_SPEEDS))
			qdr_change = 0;

		/* Do peer ports support extended link speeds ? */
		if (port_num != 0 && p_remote_physp) {
			osm_physp_t *rphysp0;
			ib_net32_t rem_cap_mask;

			if (osm_node_get_type(p_remote_physp->p_node) ==
			    IB_NODE_TYPE_SWITCH) {
				rphysp0 = osm_node_get_physp_ptr(p_remote_physp->p_node, 0);
				rem_cap_mask = rphysp0->port_info.capability_mask;
			} else
				rem_cap_mask = p_remote_physp->port_info.capability_mask;

			if (cap_mask & IB_PORT_CAP_HAS_EXT_SPEEDS &&
			    rem_cap_mask & IB_PORT_CAP_HAS_EXT_SPEEDS) {
				if (sm->p_subn->opt.force_link_speed_ext &&
				    (sm->p_subn->opt.force_link_speed_ext != IB_LINK_SPEED_EXT_SET_LSES ||
				     p_pi->link_speed_ext_enabled !=
				     ib_port_info_get_link_speed_sup(p_pi))) {
					p_pi->link_speed_ext_enabled = sm->p_subn->opt.force_link_speed_ext;
					if (memcmp(&p_pi->link_speed_ext_enabled,
						   &p_old_pi->link_speed_ext_enabled,
						   sizeof(p_pi->link_speed_ext_enabled)))
						send_set = TRUE;
				}
			}
		}

		/* calc new op_vls and mtu */
		op_vls =
		    osm_physp_calc_link_op_vls(sm->p_log, sm->p_subn, p_physp);
		mtu = osm_physp_calc_link_mtu(sm->p_log, p_physp);

		ib_port_info_set_neighbor_mtu(p_pi, mtu);
		if (ib_port_info_get_neighbor_mtu(p_pi) !=
		    ib_port_info_get_neighbor_mtu(p_old_pi))
			send_set = TRUE;

		ib_port_info_set_op_vls(p_pi, op_vls);
		if (ib_port_info_get_op_vls(p_pi) !=
		    ib_port_info_get_op_vls(p_old_pi))
			send_set = TRUE;

		/* provide the vl_high_limit from the qos mgr */
		if (sm->p_subn->opt.qos &&
		    p_physp->vl_high_limit != p_old_pi->vl_high_limit) {
			send_set = TRUE;
			p_pi->vl_high_limit = p_physp->vl_high_limit;
		}
	}

Send:
	if (port_state != IB_LINK_NO_CHANGE &&
	    port_state != ib_port_info_get_port_state(p_old_pi)) {
		send_set = TRUE;
		if (port_state == IB_LINK_ACTIVE)
			context.pi_context.active_transition = TRUE;
		else
			context.pi_context.active_transition = FALSE;
	}

	context.pi_context.node_guid = osm_node_get_node_guid(p_node);
	context.pi_context.port_guid = osm_physp_get_port_guid(p_physp);
	context.pi_context.set_method = TRUE;
	context.pi_context.light_sweep = FALSE;

	/* We need to send the PortInfoSet request with the new sm_lid
	   in the following cases:
	   1. There is a change in the values (send_set == TRUE)
	   2. This is a switch external port (so it wasn't handled yet by
	   osm_lid_mgr) and first_time_master_sweep flag on the subnet is TRUE,
	   which means the SM just became master, and it then needs to send at
	   PortInfoSet to every port.
	 */
	if (osm_node_get_type(p_node) == IB_NODE_TYPE_SWITCH && port_num
	    && sm->p_subn->first_time_master_sweep == TRUE)
		send_set = TRUE;

	if (!send_set)
		goto Exit;

	attr_mod = cl_hton32(port_num);
	if (qdr_change)
		attr_mod |= cl_hton32(1 << 31);	/* AM SMSupportExtendedSpeeds */
	status = osm_req_set(sm, osm_physp_get_dr_path_ptr(p_physp),
			     payload, sizeof(payload), IB_MAD_ATTR_PORT_INFO,
			     attr_mod, CL_DISP_MSGID_NONE, &context);
	if (status)
		ret = -1;

	if (send_set2) {
		status = osm_req_set(sm, osm_physp_get_dr_path_ptr(p_physp),
				     payload2, sizeof(payload2),
				     IB_MAD_ATTR_MLNX_EXTENDED_PORT_INFO,
				     cl_hton32(port_num),
				     CL_DISP_MSGID_NONE, &context);
		if (status)
			ret = -1;
	}

Exit:
	OSM_LOG_EXIT(sm->p_log);
	return ret;
}

static int link_mgr_process_node(osm_sm_t * sm, IN osm_node_t * p_node,
				 IN const uint8_t link_state)
{
	osm_physp_t *p_physp;
	uint32_t i, num_physp;
	int ret = 0;
	uint8_t current_state;

	OSM_LOG_ENTER(sm->p_log);

	OSM_LOG(sm->p_log, OSM_LOG_DEBUG,
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
		if (!p_physp)
			continue;

		current_state = osm_physp_get_port_state(p_physp);
		if (current_state == IB_LINK_DOWN)
			continue;

		/*
		   Normally we only send state update if state is lower
		   then required state. However, we need to send update if
		   no state change required.
		 */
		if (link_state != IB_LINK_NO_CHANGE &&
		    link_state <= current_state)
			OSM_LOG(sm->p_log, OSM_LOG_DEBUG,
				"Physical port %u already %s. Skipping\n",
				p_physp->port_num,
				ib_get_port_state_str(current_state));
		else if (link_mgr_set_physp_pi(sm, p_physp, link_state))
			ret = -1;
	}

	OSM_LOG_EXIT(sm->p_log);
	return ret;
}

int osm_link_mgr_process(osm_sm_t * sm, IN const uint8_t link_state)
{
	cl_qmap_t *p_node_guid_tbl;
	osm_node_t *p_node;
	int ret = 0;

	OSM_LOG_ENTER(sm->p_log);

	p_node_guid_tbl = &sm->p_subn->node_guid_tbl;

	CL_PLOCK_EXCL_ACQUIRE(sm->p_lock);

	for (p_node = (osm_node_t *) cl_qmap_head(p_node_guid_tbl);
	     p_node != (osm_node_t *) cl_qmap_end(p_node_guid_tbl);
	     p_node = (osm_node_t *) cl_qmap_next(&p_node->map_item))
		if (link_mgr_process_node(sm, p_node, link_state))
			ret = -1;

	CL_PLOCK_RELEASE(sm->p_lock);

	OSM_LOG_EXIT(sm->p_log);
	return ret;
}

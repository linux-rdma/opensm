/*
 * Copyright (c) 2004-2007 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2006 Mellanox Technologies LTD. All rights reserved.
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
 *    Implementation of osm_ucast_mgr_t.
 * This file implements the Unicast Manager object.
 *
 * Environment:
 *    Linux User Mode
 *
 * $Revision: 1.14 $
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <iba/ib_types.h>
#include <complib/cl_qmap.h>
#include <complib/cl_debug.h>
#include <opensm/osm_ucast_mgr.h>
#include <opensm/osm_log.h>
#include <opensm/osm_node.h>
#include <opensm/osm_switch.h>
#include <opensm/osm_helper.h>
#include <opensm/osm_msgdef.h>
#include <opensm/osm_opensm.h>

/**********************************************************************
 **********************************************************************/
void osm_ucast_mgr_construct(IN osm_ucast_mgr_t * const p_mgr)
{
	memset(p_mgr, 0, sizeof(*p_mgr));
}

/**********************************************************************
 **********************************************************************/
void osm_ucast_mgr_destroy(IN osm_ucast_mgr_t * const p_mgr)
{
	CL_ASSERT(p_mgr);

	OSM_LOG_ENTER(p_mgr->p_log, osm_ucast_mgr_destroy);

	if (p_mgr->lft_buf)
		free(p_mgr->lft_buf);

	OSM_LOG_EXIT(p_mgr->p_log);
}

/**********************************************************************
 **********************************************************************/
ib_api_status_t
osm_ucast_mgr_init(IN osm_ucast_mgr_t * const p_mgr,
		   IN osm_req_t * const p_req,
		   IN osm_subn_t * const p_subn,
		   IN osm_log_t * const p_log, IN cl_plock_t * const p_lock)
{
	ib_api_status_t status = IB_SUCCESS;

	OSM_LOG_ENTER(p_log, osm_ucast_mgr_init);

	CL_ASSERT(p_req);
	CL_ASSERT(p_subn);
	CL_ASSERT(p_lock);

	osm_ucast_mgr_construct(p_mgr);

	p_mgr->p_log = p_log;
	p_mgr->p_subn = p_subn;
	p_mgr->p_lock = p_lock;
	p_mgr->p_req = p_req;

	p_mgr->lft_buf = malloc(IB_LID_UCAST_END_HO + 1);
	if (!p_mgr->lft_buf)
		return IB_INSUFFICIENT_MEMORY;

	OSM_LOG_EXIT(p_mgr->p_log);
	return (status);
}

/**********************************************************************
 Add each switch's own and neighbor LIDs to its LID matrix
**********************************************************************/
static void
__osm_ucast_mgr_process_hop_0_1(IN cl_map_item_t * const p_map_item,
				IN void *context)
{
	osm_switch_t *const p_sw = (osm_switch_t *) p_map_item;
	osm_node_t *p_remote_node;
	uint16_t lid, remote_lid;
	uint8_t i, remote_port;

	lid = osm_node_get_base_lid(p_sw->p_node, 0);
	lid = cl_ntoh16(lid);
	osm_switch_set_hops(p_sw, lid, 0, 0);

	for (i = 1; i < p_sw->num_ports; i++) {
		p_remote_node =
		    osm_node_get_remote_node(p_sw->p_node, i, &remote_port);

		if (p_remote_node && p_remote_node->sw &&
		    (p_remote_node != p_sw->p_node)) {
			remote_lid = osm_node_get_base_lid(p_remote_node, 0);
			remote_lid = cl_ntoh16(remote_lid);
			osm_switch_set_hops(p_sw, remote_lid, i, 1);
			osm_switch_set_hops(p_remote_node->sw, lid, remote_port,
					    1);
		}
	}
}

/**********************************************************************
 **********************************************************************/
static void
__osm_ucast_mgr_process_neighbor(IN osm_ucast_mgr_t * const p_mgr,
				 IN osm_switch_t * const p_this_sw,
				 IN osm_switch_t * const p_remote_sw,
				 IN const uint8_t port_num,
				 IN const uint8_t remote_port_num)
{
	osm_switch_t *p_sw, *p_next_sw;
	uint16_t lid_ho;
	uint8_t hops;

	OSM_LOG_ENTER(p_mgr->p_log, __osm_ucast_mgr_process_neighbor);

	if (osm_log_is_active(p_mgr->p_log, OSM_LOG_DEBUG)) {
		osm_log(p_mgr->p_log, OSM_LOG_DEBUG,
			"__osm_ucast_mgr_process_neighbor: "
			"Node 0x%" PRIx64 ", remote node 0x%" PRIx64
			", port 0x%X, remote port 0x%X\n",
			cl_ntoh64(osm_node_get_node_guid(p_this_sw->p_node)),
			cl_ntoh64(osm_node_get_node_guid(p_remote_sw->p_node)),
			port_num, remote_port_num);
	}

	p_next_sw = (osm_switch_t *) cl_qmap_head(&p_mgr->p_subn->sw_guid_tbl);
	while (p_next_sw !=
	       (osm_switch_t *) cl_qmap_end(&p_mgr->p_subn->sw_guid_tbl)) {
		p_sw = p_next_sw;
		p_next_sw = (osm_switch_t *) cl_qmap_next(&p_sw->map_item);
		lid_ho = osm_node_get_base_lid(p_sw->p_node, 0);
		lid_ho = cl_ntoh16(lid_ho);
		hops = osm_switch_get_least_hops(p_remote_sw, lid_ho);
		if (hops == OSM_NO_PATH)
			continue;
		hops++;
		if (hops <
		    osm_switch_get_hop_count(p_this_sw, lid_ho, port_num)) {
			if (osm_switch_set_hops
			    (p_this_sw, lid_ho, port_num, hops) != 0)
				osm_log(p_mgr->p_log, OSM_LOG_ERROR,
					"__osm_ucast_mgr_process_neighbor: "
					"cannot set hops for lid %u at switch 0x%"
					PRIx64 "\n", lid_ho,
					cl_ntoh64(osm_node_get_node_guid
						  (p_this_sw->p_node)));
			p_mgr->some_hop_count_set = TRUE;
		}
	}

	OSM_LOG_EXIT(p_mgr->p_log);
}

/**********************************************************************
 **********************************************************************/
static void
__osm_ucast_mgr_process_port(IN osm_ucast_mgr_t * const p_mgr,
			     IN osm_switch_t * const p_sw,
			     IN osm_port_t * const p_port)
{
	uint16_t min_lid_ho;
	uint16_t max_lid_ho;
	uint16_t lid_ho;
	uint8_t port;
	boolean_t is_ignored_by_port_prof;
	ib_net64_t node_guid;
	struct osm_routing_engine *p_routing_eng;
	boolean_t dor;
	/*
	   The following are temporary structures that will aid
	   in providing better routing in LMC > 0 situations
	 */
	uint16_t lids_per_port = 1 << p_mgr->p_subn->opt.lmc;
	uint64_t *remote_sys_guids = NULL;
	uint64_t *remote_node_guids = NULL;
	uint16_t num_used_sys = 0;
	uint16_t num_used_nodes = 0;

	OSM_LOG_ENTER(p_mgr->p_log, __osm_ucast_mgr_process_port);

	if (lids_per_port > 1) {
		remote_sys_guids = malloc(sizeof(uint64_t) * lids_per_port);
		if (remote_sys_guids == NULL) {
			osm_log(p_mgr->p_log, OSM_LOG_ERROR,
				"__osm_ucast_mgr_process_port: ERR 3A09: "
				"Cannot allocate array. Insufficient memory\n");
			goto Exit;
		}

		memset(remote_sys_guids, 0, sizeof(uint64_t) * lids_per_port);

		remote_node_guids = malloc(sizeof(uint64_t) * lids_per_port);
		if (remote_node_guids == NULL) {
			osm_log(p_mgr->p_log, OSM_LOG_ERROR,
				"__osm_ucast_mgr_process_port: ERR 3A0A: "
				"Cannot allocate array. Insufficient memory\n");
			goto Exit;
		}

		memset(remote_node_guids, 0, sizeof(uint64_t) * lids_per_port);
	}

	osm_port_get_lid_range_ho(p_port, &min_lid_ho, &max_lid_ho);

	/* If the lids are zero - then there was some problem with the initialization.
	   Don't handle this port. */
	if (min_lid_ho == 0 || max_lid_ho == 0) {
		osm_log(p_mgr->p_log, OSM_LOG_ERROR,
			"__osm_ucast_mgr_process_port: ERR 3A04: "
			"Port 0x%" PRIx64 " has LID 0. An initialization "
			"error occurred. Ignoring port\n",
			cl_ntoh64(osm_port_get_guid(p_port)));
		goto Exit;
	}

	if (osm_log_is_active(p_mgr->p_log, OSM_LOG_DEBUG)) {
		osm_log(p_mgr->p_log, OSM_LOG_DEBUG,
			"__osm_ucast_mgr_process_port: "
			"Processing port 0x%" PRIx64
			", LIDs [0x%X,0x%X]\n",
			cl_ntoh64(osm_port_get_guid(p_port)),
			min_lid_ho, max_lid_ho);
	}

	/*
	   TO DO - This should be runtime error, not a CL_ASSERT()
	 */
	CL_ASSERT(max_lid_ho < osm_switch_get_fwd_tbl_size(p_sw));

	node_guid = osm_node_get_node_guid(p_sw->p_node);

	p_routing_eng = &p_mgr->p_subn->p_osm->routing_engine;
	dor = p_routing_eng->name && (strcmp(p_routing_eng->name, "dor") == 0);

	/*
	   The lid matrix contains the number of hops to each
	   lid from each port.  From this information we determine
	   how best to distribute the LID range across the ports
	   that can reach those LIDs.
	 */
	for (lid_ho = min_lid_ho; lid_ho <= max_lid_ho; lid_ho++) {
		/* Use the enhanced algorithm only for LMC > 0 */
		if (lids_per_port > 1)
			port = osm_switch_recommend_path(p_sw, p_port, lid_ho,
							 p_mgr->p_subn->
							 ignore_existing_lfts,
							 dor,
							 remote_sys_guids,
							 &num_used_sys,
							 remote_node_guids,
							 &num_used_nodes);
		else
			port = osm_switch_recommend_path(p_sw, p_port, lid_ho,
							 p_mgr->p_subn->
							 ignore_existing_lfts,
							 dor,
							 NULL, NULL, NULL,
							 NULL);

		/*
		   There might be no path to the target
		 */
		if (port == OSM_NO_PATH) {
			/* do not try to overwrite the ppro of non existing port ... */
			is_ignored_by_port_prof = TRUE;

			/* Up/Down routing can cause unreachable routes between some
			   switches so we do not report that as an error in that case */
			if (!p_routing_eng->build_lid_matrices) {
				osm_log(p_mgr->p_log, OSM_LOG_ERROR,
					"__osm_ucast_mgr_process_port: ERR 3A08: "
					"No path to get to LID 0x%X from switch 0x%"
					PRIx64 "\n", lid_ho,
					cl_ntoh64(node_guid));
				/* trigger a new sweep - try again ... */
				p_mgr->p_subn->subnet_initialization_error =
				    TRUE;
			} else
				osm_log(p_mgr->p_log, OSM_LOG_DEBUG,
					"__osm_ucast_mgr_process_port: "
					"No path to get to LID 0x%X from switch 0x%"
					PRIx64 "\n", lid_ho,
					cl_ntoh64(node_guid));
		} else {
			osm_log(p_mgr->p_log, OSM_LOG_DEBUG,
				"__osm_ucast_mgr_process_port: "
				"Routing LID 0x%X to port 0x%X"
				" for switch 0x%" PRIx64 "\n",
				lid_ho, port, cl_ntoh64(node_guid));

			/*
			   we would like to optionally ignore this port in equalization
			   as in the case of the Mellanox Anafa Internal PCI TCA port
			 */
			is_ignored_by_port_prof =
			    osm_port_prof_is_ignored_port(p_mgr->p_subn,
							  node_guid, port);

			/*
			   We also would ignore this route if the target lid is of a switch
			   and the port_profile_switch_node is not TRUE
			 */
			if (!p_mgr->p_subn->opt.port_profile_switch_nodes) {
				is_ignored_by_port_prof |=
				    (osm_node_get_type(p_port->p_node) ==
				     IB_NODE_TYPE_SWITCH);
			}
		}

		/*
		   We have selected the port for this LID.
		   Write it to the forwarding tables.
		 */
		p_mgr->lft_buf[lid_ho] = port;
		if (!is_ignored_by_port_prof)
			osm_switch_count_path(p_sw, port);
	}

      Exit:
	if (remote_sys_guids)
		free(remote_sys_guids);
	if (remote_node_guids)
		free(remote_node_guids);
	OSM_LOG_EXIT(p_mgr->p_log);
}

/**********************************************************************
 **********************************************************************/
void
osm_ucast_mgr_set_fwd_table(IN osm_ucast_mgr_t * const p_mgr,
			    IN osm_switch_t * const p_sw)
{
	osm_node_t *p_node;
	osm_dr_path_t *p_path;
	osm_madw_context_t context;
	ib_api_status_t status;
	ib_switch_info_t si;
	uint32_t block_id_ho = 0;
	uint8_t block[IB_SMP_DATA_SIZE];
	boolean_t set_swinfo_require = FALSE;
	uint16_t lin_top;
	uint8_t life_state;

	CL_ASSERT(p_mgr);

	OSM_LOG_ENTER(p_mgr->p_log, osm_ucast_mgr_set_fwd_table);

	CL_ASSERT(p_sw);

	p_node = p_sw->p_node;

	CL_ASSERT(p_node);

	p_path = osm_node_get_any_dr_path_ptr(p_node);

	CL_ASSERT(p_path);

	/*
	   Set the top of the unicast forwarding table.
	 */
	si = p_sw->switch_info;
	lin_top = cl_hton16(p_sw->max_lid_ho);
	if (lin_top != si.lin_top) {
		set_swinfo_require = TRUE;
		si.lin_top = lin_top;
	}

	/* check to see if the change state bit is on. If it is - then we
	   need to clear it. */
	if (ib_switch_info_get_state_change(&si))
		life_state = ((p_mgr->p_subn->opt.packet_life_time << 3)
			      | (si.life_state & IB_SWITCH_PSC)) & 0xfc;
	else
		life_state = (p_mgr->p_subn->opt.packet_life_time << 3) & 0xf8;

	if ((life_state != si.life_state)
	    || ib_switch_info_get_state_change(&si)) {
		set_swinfo_require = TRUE;
		si.life_state = life_state;
	}

	if (set_swinfo_require) {
		if (osm_log_is_active(p_mgr->p_log, OSM_LOG_DEBUG)) {
			osm_log(p_mgr->p_log, OSM_LOG_DEBUG,
				"osm_ucast_mgr_set_fwd_table: "
				"Setting switch FT top to LID 0x%X\n",
				p_sw->max_lid_ho);
		}

		context.si_context.light_sweep = FALSE;
		context.si_context.node_guid = osm_node_get_node_guid(p_node);
		context.si_context.set_method = TRUE;

		status = osm_req_set(p_mgr->p_req,
				     p_path,
				     (uint8_t *) & si,
				     sizeof(si),
				     IB_MAD_ATTR_SWITCH_INFO,
				     0, CL_DISP_MSGID_NONE, &context);

		if (status != IB_SUCCESS) {
			osm_log(p_mgr->p_log, OSM_LOG_ERROR,
				"osm_ucast_mgr_set_fwd_table: ERR 3A06: "
				"Sending SwitchInfo attribute failed (%s)\n",
				ib_get_err_str(status));
		} else
			p_mgr->any_change = TRUE;
	}

	/*
	   Send linear forwarding table blocks to the switch
	   as long as the switch indicates it has blocks needing
	   configuration.
	 */

	context.lft_context.node_guid = osm_node_get_node_guid(p_node);
	context.lft_context.set_method = TRUE;

	for (block_id_ho = 0;
	     osm_switch_get_fwd_tbl_block(p_sw, block_id_ho, block);
	     block_id_ho++) {
		if (!p_sw->need_update &&
		    !memcmp(block, p_mgr->lft_buf + block_id_ho * 64, 64))
			continue;

		if (osm_log_is_active(p_mgr->p_log, OSM_LOG_DEBUG)) {
			osm_log(p_mgr->p_log, OSM_LOG_DEBUG,
				"osm_ucast_mgr_set_fwd_table: "
				"Writing FT block %u\n", block_id_ho);
		}

		status = osm_req_set(p_mgr->p_req,
				     p_path,
				     p_mgr->lft_buf + block_id_ho * 64,
				     sizeof(block),
				     IB_MAD_ATTR_LIN_FWD_TBL,
				     cl_hton32(block_id_ho),
				     CL_DISP_MSGID_NONE, &context);

		if (status != IB_SUCCESS) {
			osm_log(p_mgr->p_log, OSM_LOG_ERROR,
				"osm_ucast_mgr_set_fwd_table: ERR 3A05: "
				"Sending linear fwd. tbl. block failed (%s)\n",
				ib_get_err_str(status));
		} else {
			p_mgr->any_change = TRUE;
		}
	}

	OSM_LOG_EXIT(p_mgr->p_log);
}

/**********************************************************************
 **********************************************************************/
static void
__osm_ucast_mgr_process_tbl(IN cl_map_item_t * const p_map_item,
			    IN void *context)
{
	osm_switch_t *const p_sw = (osm_switch_t *) p_map_item;
	osm_ucast_mgr_t *const p_mgr = (osm_ucast_mgr_t *) context;
	osm_node_t *p_node;
	osm_port_t *p_port;
	const cl_qmap_t *p_port_tbl;

	OSM_LOG_ENTER(p_mgr->p_log, __osm_ucast_mgr_process_tbl);

	p_node = p_sw->p_node;

	CL_ASSERT(p_node);
	CL_ASSERT(osm_node_get_type(p_node) == IB_NODE_TYPE_SWITCH);

	if (osm_log_is_active(p_mgr->p_log, OSM_LOG_DEBUG)) {
		osm_log(p_mgr->p_log, OSM_LOG_DEBUG,
			"__osm_ucast_mgr_process_tbl: "
			"Processing switch 0x%" PRIx64 "\n",
			cl_ntoh64(osm_node_get_node_guid(p_node)));
	}

	/* Initialize LIDs in buffer to invalid port number. */
	memset(p_mgr->lft_buf, 0xff, IB_LID_UCAST_END_HO + 1);

	p_port_tbl = &p_mgr->p_subn->port_guid_tbl;

	/*
	   Iterate through every port setting LID routes for each
	   port based on base LID and LMC value.
	 */

	for (p_port = (osm_port_t *) cl_qmap_head(p_port_tbl);
	     p_port != (osm_port_t *) cl_qmap_end(p_port_tbl);
	     p_port = (osm_port_t *) cl_qmap_next(&p_port->map_item)) {
		__osm_ucast_mgr_process_port(p_mgr, p_sw, p_port);
	}

	osm_ucast_mgr_set_fwd_table(p_mgr, p_sw);

	OSM_LOG_EXIT(p_mgr->p_log);
}

/**********************************************************************
 **********************************************************************/
static void
__osm_ucast_mgr_process_neighbors(IN cl_map_item_t * const p_map_item,
				  IN void *context)
{
	osm_switch_t *const p_sw = (osm_switch_t *) p_map_item;
	osm_ucast_mgr_t *const p_mgr = (osm_ucast_mgr_t *) context;
	osm_node_t *p_node;
	osm_node_t *p_remote_node;
	uint32_t port_num;
	uint8_t remote_port_num;
	uint32_t num_ports;
	osm_physp_t *p_physp;

	OSM_LOG_ENTER(p_mgr->p_log, __osm_ucast_mgr_process_neighbors);

	p_node = p_sw->p_node;

	CL_ASSERT(p_node);
	CL_ASSERT(osm_node_get_type(p_node) == IB_NODE_TYPE_SWITCH);

	if (osm_log_is_active(p_mgr->p_log, OSM_LOG_DEBUG)) {
		osm_log(p_mgr->p_log, OSM_LOG_DEBUG,
			"__osm_ucast_mgr_process_neighbors: "
			"Processing switch with GUID 0x%" PRIx64 "\n",
			cl_ntoh64(osm_node_get_node_guid(p_node)));
	}

	num_ports = osm_node_get_num_physp(p_node);

	/*
	   Start with port 1 to skip the switch's management port.
	 */
	for (port_num = 1; port_num < num_ports; port_num++) {
		p_remote_node = osm_node_get_remote_node(p_node,
							 (uint8_t) port_num,
							 &remote_port_num);

		if (p_remote_node && p_remote_node->sw
		    && (p_remote_node != p_node)) {
			/* make sure the link is healthy. If it is not - don't
			   propagate through it. */
			p_physp = osm_node_get_physp_ptr(p_node, port_num);
			if (!osm_link_is_healthy(p_physp))
				continue;

			__osm_ucast_mgr_process_neighbor(p_mgr, p_sw,
							 p_remote_node->sw,
							 (uint8_t) port_num,
							 remote_port_num);

		}
	}

	OSM_LOG_EXIT(p_mgr->p_log);
}

/**********************************************************************
 **********************************************************************/
void osm_ucast_mgr_build_lid_matrices(IN osm_ucast_mgr_t * const p_mgr)
{
	uint32_t i;
	uint32_t iteration_max;
	cl_qmap_t *p_sw_guid_tbl;

	p_sw_guid_tbl = &p_mgr->p_subn->sw_guid_tbl;

	osm_log(p_mgr->p_log, OSM_LOG_VERBOSE,
		"osm_ucast_mgr_build_lid_matrices: "
		"Starting switches' Min Hop Table Assignment\n");

	/*
	   Set the switch matrices for each switch's own port 0 LID(s)
	   then set the lid matrices for the each switch's leaf nodes.
	 */
	cl_qmap_apply_func(p_sw_guid_tbl,
			   __osm_ucast_mgr_process_hop_0_1, p_mgr);

	/*
	   Get the switch matrices for each switch's neighbors.
	   This process requires a number of iterations equal to
	   the number of switches in the subnet minus 1.

	   In each iteration, a switch learns the lid/port/hop
	   information (as contained by a switch's lid matrix) from
	   its immediate neighbors.  After each iteration, a switch
	   (and it's neighbors) know more routing information than
	   it did on the previous iteration.
	   Thus, by repeatedly absorbing the routing information of
	   neighbor switches, every switch eventually learns how to
	   route all LIDs on the subnet.

	   Note that there may not be any switches in the subnet if
	   we are in simple p2p configuration.
	 */
	iteration_max = cl_qmap_count(p_sw_guid_tbl);

	/*
	   If there are switches in the subnet, iterate until the lid
	   matrix has been constructed.  Otherwise, just immediately
	   indicate we're done if no switches exist.
	 */
	if (iteration_max) {
		iteration_max--;

		/*
		   we need to find out when the propagation of
		   hop counts has relaxed. So this global variable
		   is preset to 0 on each iteration and if
		   if non of the switches was set will exit the
		   while loop
		 */
		p_mgr->some_hop_count_set = TRUE;
		for (i = 0; (i < iteration_max) && p_mgr->some_hop_count_set;
		     i++) {
			p_mgr->some_hop_count_set = FALSE;
			cl_qmap_apply_func(p_sw_guid_tbl,
					   __osm_ucast_mgr_process_neighbors,
					   p_mgr);
		}
		osm_log(p_mgr->p_log, OSM_LOG_DEBUG,
			"osm_ucast_mgr_build_lid_matrices: "
			"Min-hop propagated in %d steps\n", i);
	}
}

/**********************************************************************
 **********************************************************************/
static int ucast_mgr_setup_all_switches(osm_subn_t * p_subn)
{
	osm_switch_t *p_sw;
	uint16_t lids;

	lids = (uint16_t) cl_ptr_vector_get_size(&p_subn->port_lid_tbl);
	lids = lids ? lids - 1 : 0;

	for (p_sw = (osm_switch_t *) cl_qmap_head(&p_subn->sw_guid_tbl);
	     p_sw != (osm_switch_t *) cl_qmap_end(&p_subn->sw_guid_tbl);
	     p_sw = (osm_switch_t *) cl_qmap_next(&p_sw->map_item))
		if (osm_switch_prepare_path_rebuild(p_sw, lids)) {
			osm_log(&p_subn->p_osm->log, OSM_LOG_ERROR,
				"ucast_mgr_setup_all_switches: ERR 3A0B: "
				"cannot setup switch 0x%016" PRIx64 "\n",
				cl_ntoh64(osm_node_get_node_guid
					  (p_sw->p_node)));
			return -1;
		}

	return 0;
}

/**********************************************************************
 **********************************************************************/

cl_status_t
osm_ucast_mgr_read_guid_file(IN osm_ucast_mgr_t * const p_mgr,
			     IN const char *guid_file_name,
			     IN cl_list_t * p_list)
{
	cl_status_t status = IB_SUCCESS;
	FILE *guid_file;
	char line[MAX_GUID_FILE_LINE_LENGTH];
	char *endptr;
	uint64_t *p_guid;

	OSM_LOG_ENTER(p_mgr->p_log, osm_ucast_mgr_read_guid_file);

	guid_file = fopen(guid_file_name, "r");
	if (guid_file == NULL) {
		osm_log(p_mgr->p_log, OSM_LOG_ERROR,
			"osm_ucast_mgr_read_guid_file: ERR 3A13: "
			"Failed to open guid list file (%s)\n", guid_file_name);
		status = IB_NOT_FOUND;
		goto Exit;
	}

	while (fgets(line, MAX_GUID_FILE_LINE_LENGTH, guid_file)) {
		if (strcspn(line, " ,;.") != strlen(line)) {
			osm_log(p_mgr->p_log, OSM_LOG_ERROR,
				"osm_ucast_mgr_read_guid_file: ERR 3A14: "
				"Poorly formatted guid in file (%s): %s\n",
				guid_file_name, line);
			status = IB_NOT_FOUND;
			break;
		}

		/* Skip empty lines anywhere in the file - only one
		   char means the null termination */
		if (strlen(line) <= 1)
			continue;

		p_guid = malloc(sizeof(uint64_t));
		if (!p_guid) {
			status = IB_ERROR;
			goto Exit;
		}

		*p_guid = strtoull(line, &endptr, 16);

		/* check that the string is a number */
		if (!(*p_guid) && (*endptr != '\0')) {
			osm_log(p_mgr->p_log, OSM_LOG_ERROR,
				"osm_ucast_mgr_read_guid_file: ERR 3A15: "
				"Poorly formatted guid in file (%s): %s\n",
				guid_file_name, line);
			status = IB_NOT_FOUND;
			break;
		}

		/* store the parsed guid */
		cl_list_insert_tail(p_list, p_guid);
	}

      Exit:
	if (guid_file)
		fclose(guid_file);
	OSM_LOG_EXIT(p_mgr->p_log);
	return (status);
}

/**********************************************************************
 **********************************************************************/
osm_signal_t osm_ucast_mgr_process(IN osm_ucast_mgr_t * const p_mgr)
{
	struct osm_routing_engine *p_routing_eng;
	osm_signal_t signal = OSM_SIGNAL_DONE;
	cl_qmap_t *p_sw_guid_tbl;
	boolean_t default_routing = TRUE;

	OSM_LOG_ENTER(p_mgr->p_log, osm_ucast_mgr_process);

	p_sw_guid_tbl = &p_mgr->p_subn->sw_guid_tbl;
	p_routing_eng = &p_mgr->p_subn->p_osm->routing_engine;

	CL_PLOCK_EXCL_ACQUIRE(p_mgr->p_lock);

	/*
	   If there are no switches in the subnet, we are done.
	 */
	if (cl_qmap_count(p_sw_guid_tbl) == 0 ||
	    ucast_mgr_setup_all_switches(p_mgr->p_subn) < 0)
		goto Exit;

	p_mgr->any_change = FALSE;

	if (!p_routing_eng->build_lid_matrices ||
	    p_routing_eng->build_lid_matrices(p_routing_eng->context) != 0)
		osm_ucast_mgr_build_lid_matrices(p_mgr);

	osm_log(p_mgr->p_log, OSM_LOG_INFO,
		"osm_ucast_mgr_process: "
		"%s tables configured on all switches\n",
		p_routing_eng->name ? p_routing_eng->name : "null (min-hop)");

	/*
	   Now that the lid matrices have been built, we can
	   build and download the switch forwarding tables.
	 */
	if (p_routing_eng->ucast_build_fwd_tables &&
	    (p_routing_eng->ucast_build_fwd_tables(p_routing_eng->context) ==
	     0))
		default_routing = FALSE;
	else
		cl_qmap_apply_func(p_sw_guid_tbl, __osm_ucast_mgr_process_tbl,
				   p_mgr);

	if (p_mgr->any_change) {
		signal = OSM_SIGNAL_DONE_PENDING;
		osm_log(p_mgr->p_log, OSM_LOG_VERBOSE,
			"osm_ucast_mgr_process: "
			"LFT Tables configured on all switches\n");
	} else {
		signal = OSM_SIGNAL_DONE;
		osm_log(p_mgr->p_log, OSM_LOG_VERBOSE,
			"osm_ucast_mgr_process: "
			"No need to set any LFT Tables on any switches\n");
	}

      Exit:
	CL_PLOCK_RELEASE(p_mgr->p_lock);
	OSM_LOG_EXIT(p_mgr->p_log);
	return (signal);
}

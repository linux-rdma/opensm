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
 *    Implementation of osm_ni_rcv_t.
 * This object represents the NodeInfo Receiver object.
 * This object is part of the opensm family of objects.
 *
 * Environment:
 *    Linux User Mode
 *
 * $Revision: 1.9 $
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <stdlib.h>
#include <string.h>
#include <iba/ib_types.h>
#include <complib/cl_qmap.h>
#include <complib/cl_passivelock.h>
#include <complib/cl_debug.h>
#include <opensm/osm_node_info_rcv.h>
#include <opensm/osm_req.h>
#include <opensm/osm_madw.h>
#include <opensm/osm_log.h>
#include <opensm/osm_node.h>
#include <opensm/osm_subnet.h>
#include <opensm/osm_router.h>
#include <opensm/osm_mad_pool.h>
#include <opensm/osm_helper.h>
#include <opensm/osm_msgdef.h>
#include <opensm/osm_opensm.h>

static void
report_duplicated_guid(IN const osm_ni_rcv_t * const p_rcv,
		       osm_physp_t * p_physp,
		       osm_node_t * p_neighbor_node, const uint8_t port_num)
{
	osm_physp_t *p_old, *p_new;
	osm_dr_path_t path;

	p_old = p_physp->p_remote_physp;
	p_new = osm_node_get_physp_ptr(p_neighbor_node, port_num);

	osm_log(p_rcv->p_log, OSM_LOG_ERROR,
		"report_duplicated_guid: ERR 0D01: "
		"Found duplicated node.\n"
		"Node 0x%" PRIx64 " port %u is reachable from remote node "
		"0x%" PRIx64 " port %u and remote node 0x%" PRIx64 " port %u.\n"
		"Paths are:\n",
		cl_ntoh64(p_physp->p_node->node_info.node_guid),
		p_physp->port_num,
		cl_ntoh64(p_old->p_node->node_info.node_guid), p_old->port_num,
		cl_ntoh64(p_new->p_node->node_info.node_guid), p_new->port_num);

	osm_dump_dr_path(p_rcv->p_log, osm_physp_get_dr_path_ptr(p_physp),
			 OSM_LOG_ERROR);

	path = *osm_physp_get_dr_path_ptr(p_new);
	osm_dr_path_extend(&path, port_num);
	osm_dump_dr_path(p_rcv->p_log, &path, OSM_LOG_ERROR);

	osm_log(p_rcv->p_log, OSM_LOG_SYS,
		"FATAL: duplicated guids or 12x lane reversal\n");
}

static void requery_dup_node_info(IN const osm_ni_rcv_t * const p_rcv,
				  osm_physp_t * p_physp, unsigned count)
{
	osm_madw_context_t context;
	osm_dr_path_t path;
	cl_status_t status;

	path = *osm_physp_get_dr_path_ptr(p_physp->p_remote_physp);
	osm_dr_path_extend(&path, p_physp->p_remote_physp->port_num);

	context.ni_context.node_guid =
	    p_physp->p_remote_physp->p_node->node_info.port_guid;
	context.ni_context.port_num = p_physp->p_remote_physp->port_num;
	context.ni_context.dup_node_guid = p_physp->p_node->node_info.node_guid;
	context.ni_context.dup_port_num = p_physp->port_num;
	context.ni_context.dup_count = count;

	status = osm_req_get(p_rcv->p_gen_req,
			     &path,
			     IB_MAD_ATTR_NODE_INFO,
			     0, CL_DISP_MSGID_NONE, &context);

	if (status != IB_SUCCESS)
		osm_log(p_rcv->p_log, OSM_LOG_ERROR,
			"requery_dup_node_info: ERR 0D02: "
			"Failure initiating NodeInfo request (%s)\n",
			ib_get_err_str(status));
}

/**********************************************************************
 The plock must be held before calling this function.
**********************************************************************/
static void
__osm_ni_rcv_set_links(IN const osm_ni_rcv_t * const p_rcv,
		       osm_node_t * p_node,
		       const uint8_t port_num,
		       const osm_ni_context_t * const p_ni_context)
{
	osm_node_t *p_neighbor_node;
	osm_physp_t *p_physp;

	OSM_LOG_ENTER(p_rcv->p_log, __osm_ni_rcv_set_links);

	/*
	   A special case exists in which the node we're trying to
	   link is our own node.  In this case, the guid value in
	   the ni_context will be zero.
	 */
	if (p_ni_context->node_guid == 0) {
		osm_log(p_rcv->p_log, OSM_LOG_DEBUG,
			"__osm_ni_rcv_set_links: "
			"Nothing to link for our own node 0x%" PRIx64 "\n",
			cl_ntoh64(osm_node_get_node_guid(p_node)));
		goto _exit;
	}

	p_neighbor_node = osm_get_node_by_guid(p_rcv->p_subn,
					       p_ni_context->node_guid);
	if (!p_neighbor_node) {
		osm_log(p_rcv->p_log, OSM_LOG_ERROR,
			"__osm_ni_rcv_set_links: ERR 0D10: "
			"Unexpected removal of neighbor node "
			"0x%" PRIx64 "\n", cl_ntoh64(p_ni_context->node_guid));
		goto _exit;
	}

	/*
	   We have seen this neighbor node before, but we might
	   not have seen this port on the neighbor node before.
	   We should not set links to an uninitialized port on the
	   neighbor, so check validity up front.  If it's not
	   valid, do nothing, since we'll see this link again
	   when we probe the neighbor.
	 */
	if (!osm_node_link_has_valid_ports(p_node, port_num,
					   p_neighbor_node,
					   p_ni_context->port_num))
		goto _exit;

	if (osm_node_link_exists(p_node, port_num,
				 p_neighbor_node, p_ni_context->port_num)) {
		osm_log(p_rcv->p_log, OSM_LOG_DEBUG,
			"__osm_ni_rcv_set_links: " "Link already exists\n");
		goto _exit;
	}

	if (osm_node_has_any_link(p_node, port_num) &&
	    p_rcv->p_subn->force_immediate_heavy_sweep == FALSE &&
	    (!p_ni_context->dup_count ||
	     (p_ni_context->dup_node_guid == osm_node_get_node_guid(p_node) &&
	      p_ni_context->dup_port_num == port_num))) {
		/*
		   Uh oh...
		   This could be reconnected ports, but also duplicated GUID
		   (2 nodes have the same guid) or a 12x link with lane reversal
		   that is not configured correctly.
		   We will try to recover by querying NodeInfo again.
		   In order to catch even fast port moving to new location(s) and
		   back we will count up to 5.
		   Some crazy reconnections (newly created switch loop right before
		   targeted CA) will not be catched this way. So in worst case -
		   report GUID duplication and request new discovery.
		   When switch node is targeted NodeInfo querying will be done in
		   opposite order, this is much stronger check, unfortunately it is
		   impossible with CAs.
		 */
		p_physp = osm_node_get_physp_ptr(p_node, port_num);
		if (p_ni_context->dup_count > 5) {
			report_duplicated_guid(p_rcv, p_physp,
					       p_neighbor_node,
					       p_ni_context->port_num);
			p_rcv->p_subn->force_immediate_heavy_sweep = TRUE;
		} else if (p_node->sw)
			requery_dup_node_info(p_rcv, p_physp->p_remote_physp,
					      p_ni_context->dup_count + 1);
		else
			requery_dup_node_info(p_rcv, p_physp,
					      p_ni_context->dup_count + 1);
	}

	/*
	   When there are only two nodes with exact same guids (connected back
	   to back) - the previous check for duplicated guid will not catch
	   them. But the link will be from the port to itself...
	   Enhanced Port 0 is an exception to this
	 */
	if ((osm_node_get_node_guid(p_node) == p_ni_context->node_guid) &&
	    (port_num == p_ni_context->port_num) &&
	    port_num != 0 && cl_qmap_count(&p_rcv->p_subn->sw_guid_tbl) == 0) {
		osm_log(p_rcv->p_log, OSM_LOG_VERBOSE,
			"__osm_ni_rcv_set_links: "
			"Duplicate GUID found by link from a port to itself:"
			"node 0x%" PRIx64 ", port number 0x%X\n",
			cl_ntoh64(osm_node_get_node_guid(p_node)), port_num);
		p_physp = osm_node_get_physp_ptr(p_node, port_num);
		osm_dump_dr_path(p_rcv->p_log,
				 osm_physp_get_dr_path_ptr(p_physp),
				 OSM_LOG_VERBOSE);

		if (p_rcv->p_subn->opt.exit_on_fatal == TRUE) {
			osm_log(p_rcv->p_log, OSM_LOG_SYS,
				"Errors on subnet. Duplicate GUID found "
				"by link from a port to itself. "
				"See verbose opensm.log for more details\n");
			exit(1);
		}
	}

	if (osm_log_is_active(p_rcv->p_log, OSM_LOG_DEBUG))
		osm_log(p_rcv->p_log, OSM_LOG_DEBUG,
			"__osm_ni_rcv_set_links: "
			"Creating new link between: "
			"\n\t\t\t\tnode 0x%" PRIx64 ", "
			"port number 0x%X and"
			"\n\t\t\t\tnode 0x%" PRIx64 ", "
			"port number 0x%X\n",
			cl_ntoh64(osm_node_get_node_guid(p_node)),
			port_num,
			cl_ntoh64(p_ni_context->node_guid),
			p_ni_context->port_num);

	osm_node_link(p_node, port_num, p_neighbor_node,
		      p_ni_context->port_num);

      _exit:
	OSM_LOG_EXIT(p_rcv->p_log);
}

/**********************************************************************
 The plock must be held before calling this function.
**********************************************************************/
static void
__osm_ni_rcv_process_new_node(IN const osm_ni_rcv_t * const p_rcv,
			      IN osm_node_t * const p_node,
			      IN const osm_madw_t * const p_madw)
{
	ib_api_status_t status = IB_SUCCESS;
	osm_madw_context_t context;
	osm_physp_t *p_physp;
	ib_node_info_t *p_ni;
	ib_smp_t *p_smp;
	uint8_t port_num;

	OSM_LOG_ENTER(p_rcv->p_log, __osm_ni_rcv_process_new_node);

	CL_ASSERT(p_node);
	CL_ASSERT(p_madw);

	p_smp = osm_madw_get_smp_ptr(p_madw);
	p_ni = (ib_node_info_t *) ib_smp_get_payload_ptr(p_smp);
	port_num = ib_node_info_get_local_port_num(p_ni);

	/*
	   Request PortInfo & NodeDescription attributes for the port
	   that responded to the NodeInfo attribute.
	   Because this is a channel adapter or router, we are
	   not allowed to request PortInfo for the other ports.
	   Set the context union properly, so the recipient
	   knows which node & port are relevant.
	 */
	p_physp = osm_node_get_physp_ptr(p_node, port_num);

	CL_ASSERT(osm_physp_is_valid(p_physp));
	CL_ASSERT(osm_madw_get_bind_handle(p_madw) ==
		  osm_dr_path_get_bind_handle(osm_physp_get_dr_path_ptr
					      (p_physp)));

	context.pi_context.node_guid = p_ni->node_guid;
	context.pi_context.port_guid = p_ni->port_guid;
	context.pi_context.set_method = FALSE;
	context.pi_context.update_master_sm_base_lid = FALSE;
	context.pi_context.ignore_errors = FALSE;
	context.pi_context.light_sweep = FALSE;
	context.pi_context.active_transition = FALSE;

	status = osm_req_get(p_rcv->p_gen_req,
			     osm_physp_get_dr_path_ptr(p_physp),
			     IB_MAD_ATTR_PORT_INFO,
			     cl_hton32(port_num), CL_DISP_MSGID_NONE, &context);
	if (status != IB_SUCCESS)
		osm_log(p_rcv->p_log, OSM_LOG_ERROR,
			"__osm_ni_rcv_process_new_node: ERR 0D02: "
			"Failure initiating PortInfo request (%s)\n",
			ib_get_err_str(status));

	OSM_LOG_EXIT(p_rcv->p_log);
}

/**********************************************************************
 The plock must be held before calling this function.
**********************************************************************/
static void
__osm_ni_rcv_get_node_desc(IN const osm_ni_rcv_t * const p_rcv,
			   IN osm_node_t * const p_node,
			   IN const osm_madw_t * const p_madw)
{
	ib_api_status_t status = IB_SUCCESS;
	osm_madw_context_t context;
	osm_physp_t *p_physp;
	ib_node_info_t *p_ni;
	ib_smp_t *p_smp;
	uint8_t port_num;

	OSM_LOG_ENTER(p_rcv->p_log, __osm_ni_rcv_get_node_desc);

	CL_ASSERT(p_node);
	CL_ASSERT(p_madw);

	p_smp = osm_madw_get_smp_ptr(p_madw);
	p_ni = (ib_node_info_t *) ib_smp_get_payload_ptr(p_smp);
	port_num = ib_node_info_get_local_port_num(p_ni);

	/*
	   Request PortInfo & NodeDescription attributes for the port
	   that responded to the NodeInfo attribute.
	   Because this is a channel adapter or router, we are
	   not allowed to request PortInfo for the other ports.
	   Set the context union properly, so the recipient
	   knows which node & port are relevant.
	 */
	p_physp = osm_node_get_physp_ptr(p_node, port_num);

	CL_ASSERT(osm_physp_is_valid(p_physp));
	CL_ASSERT(osm_madw_get_bind_handle(p_madw) ==
		  osm_dr_path_get_bind_handle(osm_physp_get_dr_path_ptr
					      (p_physp)));

	context.nd_context.node_guid = osm_node_get_node_guid(p_node);

	status = osm_req_get(p_rcv->p_gen_req,
			     osm_physp_get_dr_path_ptr(p_physp),
			     IB_MAD_ATTR_NODE_DESC,
			     0, CL_DISP_MSGID_NONE, &context);
	if (status != IB_SUCCESS)
		osm_log(p_rcv->p_log, OSM_LOG_ERROR,
			"__osm_ni_rcv_get_node_desc: ERR 0D03: "
			"Failure initiating NodeDescription request (%s)\n",
			ib_get_err_str(status));

	OSM_LOG_EXIT(p_rcv->p_log);
}

/**********************************************************************
 The plock must be held before calling this function.
**********************************************************************/
static void
__osm_ni_rcv_process_new_ca_or_router(IN const osm_ni_rcv_t * const p_rcv,
				      IN osm_node_t * const p_node,
				      IN const osm_madw_t * const p_madw)
{
	OSM_LOG_ENTER(p_rcv->p_log, __osm_ni_rcv_process_new_ca_or_router);

	__osm_ni_rcv_process_new_node(p_rcv, p_node, p_madw);

	/*
	   A node guid of 0 is the corner case that indicates
	   we discovered our own node.  Initialize the subnet
	   object with the SM's own port guid.
	 */
	if (osm_madw_get_ni_context_ptr(p_madw)->node_guid == 0)
		p_rcv->p_subn->sm_port_guid = p_node->node_info.port_guid;

	OSM_LOG_EXIT(p_rcv->p_log);
}

/**********************************************************************
 The plock must be held before calling this function.
**********************************************************************/
static void
__osm_ni_rcv_process_existing_ca_or_router(IN const osm_ni_rcv_t * const p_rcv,
					   IN osm_node_t * const p_node,
					   IN const osm_madw_t * const p_madw)
{
	ib_node_info_t *p_ni;
	ib_smp_t *p_smp;
	osm_port_t *p_port;
	osm_port_t *p_port_check;
	osm_madw_context_t context;
	uint8_t port_num;
	osm_physp_t *p_physp;
	ib_api_status_t status;
	osm_dr_path_t *p_dr_path;
	osm_bind_handle_t h_bind;

	OSM_LOG_ENTER(p_rcv->p_log, __osm_ni_rcv_process_existing_ca_or_router);

	p_smp = osm_madw_get_smp_ptr(p_madw);
	p_ni = (ib_node_info_t *) ib_smp_get_payload_ptr(p_smp);
	port_num = ib_node_info_get_local_port_num(p_ni);
	h_bind = osm_madw_get_bind_handle(p_madw);

	/*
	   Determine if we have encountered this node through a
	   previously undiscovered port.  If so, build the new
	   port object.
	 */
	p_port = osm_get_port_by_guid(p_rcv->p_subn, p_ni->port_guid);
	if (!p_port) {
		osm_log(p_rcv->p_log, OSM_LOG_VERBOSE,
			"__osm_ni_rcv_process_existing_ca_or_router: "
			"Creating new port object with GUID 0x%" PRIx64 "\n",
			cl_ntoh64(p_ni->port_guid));

		osm_node_init_physp(p_node, p_madw);

		p_port = osm_port_new(p_ni, p_node);
		if (p_port == NULL) {
			osm_log(p_rcv->p_log, OSM_LOG_ERROR,
				"__osm_ni_rcv_process_existing_ca_or_router: ERR 0D04: "
				"Unable to create new port object\n");
			goto Exit;
		}

		/*
		   Add the new port object to the database.
		 */
		p_port_check =
		    (osm_port_t *) cl_qmap_insert(&p_rcv->p_subn->port_guid_tbl,
						  p_ni->port_guid,
						  &p_port->map_item);
		if (p_port_check != p_port) {
			/*
			   We should never be here!
			   Somehow, this port GUID already exists in the table.
			 */
			osm_log(p_rcv->p_log, OSM_LOG_ERROR,
				"__osm_ni_rcv_process_existing_ca_or_router: ERR 0D12: "
				"Port 0x%" PRIx64 " already in the database!\n",
				cl_ntoh64(p_ni->port_guid));

			osm_port_delete(&p_port);
			goto Exit;
		}

		/* If we are a master, then this means the port is new on the subnet.
		   Mark it as new - need to send trap 64 on these ports.
		   The condition that we are master is true, since if we are in discovering
		   state (meaning we woke up from standby or we are just initializing),
		   then these ports may be new to us, but are not new on the subnet.
		   If we are master, then the subnet as we know it is the updated one,
		   and any new ports we encounter should cause trap 64. C14-72.1.1 */
		if (p_rcv->p_subn->sm_state == IB_SMINFO_STATE_MASTER)
			p_port->is_new = 1;

		p_physp = osm_node_get_physp_ptr(p_node, port_num);
	} else {
		p_physp = osm_node_get_physp_ptr(p_node, port_num);

		if (!osm_physp_is_valid(p_physp)) {
			osm_log(p_rcv->p_log, OSM_LOG_ERROR,
				"__osm_ni_rcv_process_existing_ca_or_router: ERR 0D19: "
				"Invalid physical port. Aborting discovery\n");
			goto Exit;
		}

		/*
		   Update the DR Path to the port,
		   in case the old one is no longer available.
		 */
		p_dr_path = osm_physp_get_dr_path_ptr(p_physp);

		osm_dr_path_init(p_dr_path, h_bind, p_smp->hop_count,
				 p_smp->initial_path);
	}

	context.pi_context.node_guid = p_ni->node_guid;
	context.pi_context.port_guid = p_ni->port_guid;
	context.pi_context.set_method = FALSE;
	context.pi_context.update_master_sm_base_lid = FALSE;
	context.pi_context.ignore_errors = FALSE;
	context.pi_context.light_sweep = FALSE;

	status = osm_req_get(p_rcv->p_gen_req,
			     osm_physp_get_dr_path_ptr(p_physp),
			     IB_MAD_ATTR_PORT_INFO,
			     cl_hton32(port_num), CL_DISP_MSGID_NONE, &context);

	if (status != IB_SUCCESS)
		osm_log(p_rcv->p_log, OSM_LOG_ERROR,
			"__osm_ni_rcv_process_existing_ca_or_router: ERR 0D13: "
			"Failure initiating PortInfo request (%s)\n",
			ib_get_err_str(status));

      Exit:
	OSM_LOG_EXIT(p_rcv->p_log);
}

/**********************************************************************
 **********************************************************************/
static void
__osm_ni_rcv_process_switch(IN const osm_ni_rcv_t * const p_rcv,
			    IN osm_node_t * const p_node,
			    IN const osm_madw_t * const p_madw)
{
	ib_api_status_t status = IB_SUCCESS;
	osm_madw_context_t context;
	osm_dr_path_t dr_path;
	ib_smp_t *p_smp;

	OSM_LOG_ENTER(p_rcv->p_log, __osm_ni_rcv_process_switch);

	CL_ASSERT(p_node);
	CL_ASSERT(p_madw);

	p_smp = osm_madw_get_smp_ptr(p_madw);

	osm_dr_path_init(&dr_path,
			 osm_madw_get_bind_handle(p_madw),
			 p_smp->hop_count, p_smp->initial_path);

	context.si_context.node_guid = osm_node_get_node_guid(p_node);
	context.si_context.set_method = FALSE;
	context.si_context.light_sweep = FALSE;

	/* Request a SwitchInfo attribute */
	status = osm_req_get(p_rcv->p_gen_req,
			     &dr_path,
			     IB_MAD_ATTR_SWITCH_INFO,
			     0, CL_DISP_MSGID_NONE, &context);
	if (status != IB_SUCCESS)
		/* continue despite error */
		osm_log(p_rcv->p_log, OSM_LOG_ERROR,
			"__osm_ni_rcv_process_switch: ERR 0D06: "
			"Failure initiating SwitchInfo request (%s)\n",
			ib_get_err_str(status));

	OSM_LOG_EXIT(p_rcv->p_log);
}

/**********************************************************************
 The plock must be held before calling this function.
**********************************************************************/
static void
__osm_ni_rcv_process_existing_switch(IN const osm_ni_rcv_t * const p_rcv,
				     IN osm_node_t * const p_node,
				     IN const osm_madw_t * const p_madw)
{
	OSM_LOG_ENTER(p_rcv->p_log, __osm_ni_rcv_process_existing_switch);

	/*
	   If this switch has already been probed during this sweep,
	   then don't bother reprobing it.
	   There is one exception - if the node has been visited, but
	   for some reason we don't have the switch object (this can happen
	   if the SwitchInfo mad didn't reach the SM) then we want
	   to retry to probe the switch.
	 */
	if (p_node->discovery_count == 1)
		__osm_ni_rcv_process_switch(p_rcv, p_node, p_madw);
	else if (!p_node->sw || p_node->sw->discovery_count == 0) {
		/* we don't have the SwitchInfo - retry to get it */
		osm_log(p_rcv->p_log, OSM_LOG_DEBUG,
			"__osm_ni_rcv_process_existing_switch: "
			"Retry to get SwitchInfo on node GUID:0x%"
			PRIx64 "\n", cl_ntoh64(osm_node_get_node_guid(p_node)));
		__osm_ni_rcv_process_switch(p_rcv, p_node, p_madw);
	}

	OSM_LOG_EXIT(p_rcv->p_log);
}

/**********************************************************************
 The plock must be held before calling this function.
**********************************************************************/
static void
__osm_ni_rcv_process_new_switch(IN const osm_ni_rcv_t * const p_rcv,
				IN osm_node_t * const p_node,
				IN const osm_madw_t * const p_madw)
{
	OSM_LOG_ENTER(p_rcv->p_log, __osm_ni_rcv_process_new_switch);

	__osm_ni_rcv_process_switch(p_rcv, p_node, p_madw);

	/*
	   A node guid of 0 is the corner case that indicates
	   we discovered our own node.  Initialize the subnet
	   object with the SM's own port guid.
	 */
	if (osm_madw_get_ni_context_ptr(p_madw)->node_guid == 0)
		p_rcv->p_subn->sm_port_guid = p_node->node_info.port_guid;

	OSM_LOG_EXIT(p_rcv->p_log);
}

/**********************************************************************
 The plock must NOT be held before calling this function.
**********************************************************************/
static void
__osm_ni_rcv_process_new(IN const osm_ni_rcv_t * const p_rcv,
			 IN const osm_madw_t * const p_madw)
{
	osm_node_t *p_node;
	osm_node_t *p_node_check;
	osm_port_t *p_port;
	osm_port_t *p_port_check;
	osm_router_t *p_rtr = NULL;
	osm_router_t *p_rtr_check;
	cl_qmap_t *p_rtr_guid_tbl;
	ib_node_info_t *p_ni;
	ib_smp_t *p_smp;
	osm_ni_context_t *p_ni_context;
	uint8_t port_num;

	OSM_LOG_ENTER(p_rcv->p_log, __osm_ni_rcv_process_new);

	p_smp = osm_madw_get_smp_ptr(p_madw);
	p_ni = (ib_node_info_t *) ib_smp_get_payload_ptr(p_smp);
	p_ni_context = osm_madw_get_ni_context_ptr(p_madw);
	port_num = ib_node_info_get_local_port_num(p_ni);

	osm_dump_smp_dr_path(p_rcv->p_log, p_smp, OSM_LOG_VERBOSE);

	osm_log(p_rcv->p_log, OSM_LOG_VERBOSE,
		"__osm_ni_rcv_process_new: "
		"Discovered new %s node,"
		"\n\t\t\t\tGUID 0x%" PRIx64 ", TID 0x%" PRIx64 "\n",
		ib_get_node_type_str(p_ni->node_type),
		cl_ntoh64(p_ni->node_guid), cl_ntoh64(p_smp->trans_id));

	p_node = osm_node_new(p_madw);
	if (p_node == NULL) {
		osm_log(p_rcv->p_log, OSM_LOG_ERROR,
			"__osm_ni_rcv_process_new: ERR 0D07: "
			"Unable to create new node object\n");
		goto Exit;
	}

	/*
	   Create a new port object to represent this node's physical
	   ports in the port table.
	 */
	p_port = osm_port_new(p_ni, p_node);
	if (p_port == NULL) {
		osm_log(p_rcv->p_log, OSM_LOG_ERROR,
			"__osm_ni_rcv_process_new: ERR 0D14: "
			"Unable to create new port object\n");
		osm_node_delete(&p_node);
		goto Exit;
	}

	/*
	   Add the new port object to the database.
	 */
	p_port_check =
	    (osm_port_t *) cl_qmap_insert(&p_rcv->p_subn->port_guid_tbl,
					  p_ni->port_guid, &p_port->map_item);
	if (p_port_check != p_port) {
		/*
		   We should never be here!
		   Somehow, this port GUID already exists in the table.
		 */
		osm_log(p_rcv->p_log, OSM_LOG_ERROR,
			"__osm_ni_rcv_process_new: ERR 0D15: "
			"Duplicate Port GUID 0x%" PRIx64
			"! Found by the two directed routes:\n",
			cl_ntoh64(p_ni->port_guid));
		osm_dump_dr_path(p_rcv->p_log,
				 osm_physp_get_dr_path_ptr(p_port->p_physp),
				 OSM_LOG_ERROR);
		osm_dump_dr_path(p_rcv->p_log,
				 osm_physp_get_dr_path_ptr(p_port_check->
							   p_physp),
				 OSM_LOG_ERROR);
		osm_port_delete(&p_port);
		osm_node_delete(&p_node);
		goto Exit;
	}

	/* If we are a master, then this means the port is new on the subnet.
	   Mark it as new - need to send trap 64 on these ports.
	   The condition that we are master is true, since if we are in discovering
	   state (meaning we woke up from standby or we are just initializing),
	   then these ports may be new to us, but are not new on the subnet.
	   If we are master, then the subnet as we know it is the updated one,
	   and any new ports we encounter should cause trap 64. C14-72.1.1 */
	if (p_rcv->p_subn->sm_state == IB_SMINFO_STATE_MASTER)
		p_port->is_new = 1;

	/* If there were RouterInfo or other router attribute,
	   this would be elsewhere */
	if (p_ni->node_type == IB_NODE_TYPE_ROUTER) {
		if ((p_rtr = osm_router_new(p_port)) == NULL)
			osm_log(p_rcv->p_log, OSM_LOG_ERROR,
				"__osm_ni_rcv_process_new: ERR 0D1A: "
				"Unable to create new router object\n");
		else {
			p_rtr_guid_tbl = &p_rcv->p_subn->rtr_guid_tbl;
			p_rtr_check =
			    (osm_router_t *) cl_qmap_insert(p_rtr_guid_tbl,
							    p_ni->port_guid,
							    &p_rtr->map_item);
			if (p_rtr_check != p_rtr)
				osm_log(p_rcv->p_log, OSM_LOG_ERROR,
					"__osm_ni_rcv_process_new: ERR 0D1B: "
					"Unable to add port GUID:0x%016" PRIx64
					" to router table\n",
					cl_ntoh64(p_ni->port_guid));
		}
	}

	p_node_check =
	    (osm_node_t *) cl_qmap_insert(&p_rcv->p_subn->node_guid_tbl,
					  p_ni->node_guid, &p_node->map_item);
	if (p_node_check != p_node) {
		/*
		   This node must have been inserted by another thread.
		   This is unexpected, but is not an error.
		   We can simply clean-up, since the other thread will
		   see this processing through to completion.
		 */
		osm_log(p_rcv->p_log, OSM_LOG_VERBOSE,
			"__osm_ni_rcv_process_new: "
			"Discovery race detected at node 0x%" PRIx64 "\n",
			cl_ntoh64(p_ni->node_guid));
		osm_node_delete(&p_node);
		p_node = p_node_check;
		__osm_ni_rcv_set_links(p_rcv, p_node, port_num, p_ni_context);
		goto Exit;
	} else
		__osm_ni_rcv_set_links(p_rcv, p_node, port_num, p_ni_context);

	p_node->discovery_count++;
	__osm_ni_rcv_get_node_desc(p_rcv, p_node, p_madw);

	switch (p_ni->node_type) {
	case IB_NODE_TYPE_CA:
	case IB_NODE_TYPE_ROUTER:
		__osm_ni_rcv_process_new_ca_or_router(p_rcv, p_node, p_madw);
		break;
	case IB_NODE_TYPE_SWITCH:
		__osm_ni_rcv_process_new_switch(p_rcv, p_node, p_madw);
		break;
	default:
		osm_log(p_rcv->p_log, OSM_LOG_ERROR,
			"__osm_ni_rcv_process_new: ERR 0D16: "
			"Unknown node type %u with GUID 0x%" PRIx64 "\n",
			p_ni->node_type, cl_ntoh64(p_ni->node_guid));
		break;
	}

      Exit:
	OSM_LOG_EXIT(p_rcv->p_log);
}

/**********************************************************************
 The plock must be held before calling this function.
**********************************************************************/
static void
__osm_ni_rcv_process_existing(IN const osm_ni_rcv_t * const p_rcv,
			      IN osm_node_t * const p_node,
			      IN const osm_madw_t * const p_madw)
{
	ib_node_info_t *p_ni;
	ib_smp_t *p_smp;
	osm_ni_context_t *p_ni_context;
	uint8_t port_num;

	OSM_LOG_ENTER(p_rcv->p_log, __osm_ni_rcv_process_existing);

	p_smp = osm_madw_get_smp_ptr(p_madw);
	p_ni = (ib_node_info_t *) ib_smp_get_payload_ptr(p_smp);
	p_ni_context = osm_madw_get_ni_context_ptr(p_madw);
	port_num = ib_node_info_get_local_port_num(p_ni);

	if (osm_log_is_active(p_rcv->p_log, OSM_LOG_VERBOSE))
		osm_log(p_rcv->p_log, OSM_LOG_VERBOSE,
			"__osm_ni_rcv_process_existing: "
			"Rediscovered %s node 0x%" PRIx64
			" TID 0x%" PRIx64 ", discovered %u times already\n",
			ib_get_node_type_str(p_ni->node_type),
			cl_ntoh64(p_ni->node_guid),
			cl_ntoh64(p_smp->trans_id), p_node->discovery_count);

	/*
	   If we haven't already encountered this existing node
	   on this particular sweep, then process further.
	 */
	p_node->discovery_count++;

	switch (p_ni->node_type) {
	case IB_NODE_TYPE_CA:
	case IB_NODE_TYPE_ROUTER:
		__osm_ni_rcv_process_existing_ca_or_router(p_rcv, p_node,
							   p_madw);
		break;

	case IB_NODE_TYPE_SWITCH:
		__osm_ni_rcv_process_existing_switch(p_rcv, p_node, p_madw);
		break;

	default:
		osm_log(p_rcv->p_log, OSM_LOG_ERROR,
			"__osm_ni_rcv_process_existing: ERR 0D09: "
			"Unknown node type %u with GUID 0x%" PRIx64 "\n",
			p_ni->node_type, cl_ntoh64(p_ni->node_guid));
		break;
	}

	__osm_ni_rcv_set_links(p_rcv, p_node, port_num, p_ni_context);

	OSM_LOG_EXIT(p_rcv->p_log);
}

/**********************************************************************
 **********************************************************************/
void osm_ni_rcv_construct(IN osm_ni_rcv_t * const p_rcv)
{
	memset(p_rcv, 0, sizeof(*p_rcv));
}

/**********************************************************************
 **********************************************************************/
void osm_ni_rcv_destroy(IN osm_ni_rcv_t * const p_rcv)
{
	CL_ASSERT(p_rcv);

	OSM_LOG_ENTER(p_rcv->p_log, osm_ni_rcv_destroy);

	OSM_LOG_EXIT(p_rcv->p_log);
}

/**********************************************************************
 **********************************************************************/
ib_api_status_t
osm_ni_rcv_init(IN osm_ni_rcv_t * const p_rcv,
		IN osm_req_t * const p_req,
		IN osm_subn_t * const p_subn,
		IN osm_log_t * const p_log,
		IN cl_plock_t * const p_lock)
{
	ib_api_status_t status = IB_SUCCESS;

	OSM_LOG_ENTER(p_log, osm_ni_rcv_init);

	osm_ni_rcv_construct(p_rcv);

	p_rcv->p_log = p_log;
	p_rcv->p_subn = p_subn;
	p_rcv->p_lock = p_lock;
	p_rcv->p_gen_req = p_req;

	OSM_LOG_EXIT(p_rcv->p_log);
	return (status);
}

/**********************************************************************
 **********************************************************************/
void osm_ni_rcv_process(IN void *context, IN void *data)
{
	osm_ni_rcv_t *p_rcv = context;
	osm_madw_t *p_madw = data;
	ib_node_info_t *p_ni;
	ib_smp_t *p_smp;
	osm_node_t *p_node;
	boolean_t process_new_flag = FALSE;

	CL_ASSERT(p_rcv);

	OSM_LOG_ENTER(p_rcv->p_log, osm_ni_rcv_process);

	CL_ASSERT(p_madw);

	p_smp = osm_madw_get_smp_ptr(p_madw);
	p_ni = (ib_node_info_t *) ib_smp_get_payload_ptr(p_smp);

	CL_ASSERT(p_smp->attr_id == IB_MAD_ATTR_NODE_INFO);

	if (p_ni->node_guid == 0) {
		osm_log(p_rcv->p_log, OSM_LOG_ERROR,
			"osm_ni_rcv_process: ERR 0D16: "
			"Got Zero Node GUID! Found on the directed route:\n");
		osm_dump_smp_dr_path(p_rcv->p_log, p_smp, OSM_LOG_ERROR);
		goto Exit;
	}

	if (p_ni->port_guid == 0) {
		osm_log(p_rcv->p_log, OSM_LOG_ERROR,
			"osm_ni_rcv_process: ERR 0D17: "
			"Got Zero Port GUID! Found on the directed route:\n");
		osm_dump_smp_dr_path(p_rcv->p_log, p_smp, OSM_LOG_ERROR);
		goto Exit;
	}

	/*
	   Determine if this node has already been discovered,
	   and process accordingly.
	   During processing of this node, hold the shared lock.
	 */

	CL_PLOCK_EXCL_ACQUIRE(p_rcv->p_lock);
	p_node = osm_get_node_by_guid(p_rcv->p_subn, p_ni->node_guid);

	osm_dump_node_info(p_rcv->p_log, p_ni, OSM_LOG_DEBUG);

	if (!p_node) {
		__osm_ni_rcv_process_new(p_rcv, p_madw);
		process_new_flag = TRUE;
	} else
		__osm_ni_rcv_process_existing(p_rcv, p_node, p_madw);

	CL_PLOCK_RELEASE(p_rcv->p_lock);

	/*
	 * If we processed a new node - need to signal to the SM that
	 * change detected.
	 */
	if (process_new_flag)
		osm_sm_signal(&p_rcv->p_subn->p_osm->sm,
			      OSM_SIGNAL_CHANGE_DETECTED);

      Exit:
	OSM_LOG_EXIT(p_rcv->p_log);
}

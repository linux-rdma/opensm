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
 *    Implementation of multicast functions.
 *
 * Environment:
 *    Linux User Mode
 *
 * $Revision: 1.5 $
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <stdlib.h>
#include <string.h>
#include <opensm/osm_multicast.h>
#include <opensm/osm_mcm_port.h>
#include <opensm/osm_mtree.h>
#include <opensm/osm_inform.h>

/**********************************************************************
 **********************************************************************/
/* osm_mcast_req_type_t values converted to test for easier printing. */
const static char *mcast_req_type_str[] = {
	"OSM_MCAST_REQ_TYPE_CREATE",
	"OSM_MCAST_REQ_TYPE_JOIN",
	"OSM_MCAST_REQ_TYPE_LEAVE",
	"OSM_MCAST_REQ_TYPE_SUBNET_CHANGE"
};

const char *osm_get_mcast_req_type_str(IN osm_mcast_req_type_t req_type)
{
	if (req_type > OSM_MCAST_REQ_TYPE_SUBNET_CHANGE)
		req_type = OSM_MCAST_REQ_TYPE_SUBNET_CHANGE;
	return (mcast_req_type_str[req_type]);
}

/**********************************************************************
 **********************************************************************/
void osm_mgrp_delete(IN osm_mgrp_t * const p_mgrp)
{
	osm_mcm_port_t *p_mcm_port;
	osm_mcm_port_t *p_next_mcm_port;

	CL_ASSERT(p_mgrp);

	p_next_mcm_port =
	    (osm_mcm_port_t *) cl_qmap_head(&p_mgrp->mcm_port_tbl);
	while (p_next_mcm_port !=
	       (osm_mcm_port_t *) cl_qmap_end(&p_mgrp->mcm_port_tbl)) {
		p_mcm_port = p_next_mcm_port;
		p_next_mcm_port =
		    (osm_mcm_port_t *) cl_qmap_next(&p_mcm_port->map_item);
		osm_mcm_port_delete(p_mcm_port);
	}
	/* destroy the mtree_node structure */
	osm_mtree_destroy(p_mgrp->p_root);

	free(p_mgrp);
}

/**********************************************************************
 **********************************************************************/
static void
osm_mgrp_init(IN osm_mgrp_t * const p_mgrp, IN const ib_net16_t mlid)
{
	CL_ASSERT(cl_ntoh16(mlid) >= IB_LID_MCAST_START_HO);

	memset(p_mgrp, 0, sizeof(*p_mgrp));
	cl_qmap_init(&p_mgrp->mcm_port_tbl);
	p_mgrp->mlid = mlid;
	p_mgrp->last_change_id = 0;
	p_mgrp->last_tree_id = 0;
	p_mgrp->to_be_deleted = FALSE;
}

/**********************************************************************
 **********************************************************************/
osm_mgrp_t *osm_mgrp_new(IN const ib_net16_t mlid)
{
	osm_mgrp_t *p_mgrp;

	p_mgrp = (osm_mgrp_t *) malloc(sizeof(*p_mgrp));
	if (p_mgrp)
		osm_mgrp_init(p_mgrp, mlid);

	return (p_mgrp);
}

/**********************************************************************
 **********************************************************************/
osm_mcm_port_t *osm_mgrp_add_port(IN osm_mgrp_t * const p_mgrp,
				  IN const ib_gid_t * const p_port_gid,
				  IN const uint8_t join_state,
				  IN boolean_t proxy_join)
{
	ib_net64_t port_guid;
	osm_mcm_port_t *p_mcm_port;
	cl_map_item_t *prev_item;
	uint8_t prev_join_state;
	uint8_t prev_scope;

	p_mcm_port = osm_mcm_port_new(p_port_gid, join_state, proxy_join);
	if (p_mcm_port) {
		port_guid = p_port_gid->unicast.interface_id;

		/*
		   prev_item = cl_qmap_insert(...)
		   Pointer to the item in the map with the specified key.  If insertion
		   was successful, this is the pointer to the item.  If an item with the
		   specified key already exists in the map, the pointer to that item is
		   returned.
		 */
		prev_item = cl_qmap_insert(&p_mgrp->mcm_port_tbl,
					   port_guid, &p_mcm_port->map_item);

		/* if already exists - revert the insertion and only update join state */
		if (prev_item != &p_mcm_port->map_item) {

			osm_mcm_port_delete(p_mcm_port);
			p_mcm_port = (osm_mcm_port_t *) prev_item;

			/*
			   o15.0.1.11
			   Join state of the end port should be the or of the
			   previous setting with the current one
			 */
			ib_member_get_scope_state(p_mcm_port->scope_state,
						  &prev_scope,
						  &prev_join_state);
			p_mcm_port->scope_state =
			    ib_member_set_scope_state(prev_scope,
						      prev_join_state |
						      join_state);

		} else {
			/* track the fact we modified the group ports */
			p_mgrp->last_change_id++;
		}
	}

	return (p_mcm_port);
}

/**********************************************************************
 **********************************************************************/
void
osm_mgrp_remove_port(IN osm_subn_t * const p_subn,
		     IN osm_log_t * const p_log,
		     IN osm_mgrp_t * const p_mgrp,
		     IN const ib_net64_t port_guid)
{
	cl_map_item_t *p_map_item;

	CL_ASSERT(p_mgrp);

	p_map_item = cl_qmap_get(&p_mgrp->mcm_port_tbl, port_guid);

	if (p_map_item != cl_qmap_end(&p_mgrp->mcm_port_tbl)) {
		cl_qmap_remove_item(&p_mgrp->mcm_port_tbl, p_map_item);
		osm_mcm_port_delete((osm_mcm_port_t *) p_map_item);

		/* track the fact we modified the group */
		p_mgrp->last_change_id++;
	}

	/*
	   no more ports so the group will be deleted after re-route
	   but only if it is not a well known group and not already deleted
	 */
	if ((cl_is_qmap_empty(&p_mgrp->mcm_port_tbl)) &&
	    (p_mgrp->well_known == FALSE) && (p_mgrp->to_be_deleted == FALSE)) {
		p_mgrp->to_be_deleted = TRUE;

		/* Send a Report to any InformInfo registered for
		   Trap 67 : MCGroup delete */
		osm_mgrp_send_delete_notice(p_subn, p_log, p_mgrp);
	}
}

/**********************************************************************
 **********************************************************************/
boolean_t
osm_mgrp_is_port_present(IN const osm_mgrp_t * const p_mgrp,
			 IN const ib_net64_t port_guid,
			 OUT osm_mcm_port_t ** const pp_mcm_port)
{
	cl_map_item_t *p_map_item;

	CL_ASSERT(p_mgrp);

	p_map_item = cl_qmap_get(&p_mgrp->mcm_port_tbl, port_guid);

	if (p_map_item != cl_qmap_end(&p_mgrp->mcm_port_tbl)) {
		if (pp_mcm_port)
			*pp_mcm_port = (osm_mcm_port_t *) p_map_item;
		return TRUE;
	}
	if (pp_mcm_port)
		*pp_mcm_port = NULL;
	return FALSE;
}

/**********************************************************************
 **********************************************************************/
static void
__osm_mgrp_apply_func_sub(const osm_mgrp_t * const p_mgrp,
			  const osm_mtree_node_t * const p_mtn,
			  osm_mgrp_func_t p_func, void *context)
{
	uint8_t i = 0;
	uint8_t max_children;
	osm_mtree_node_t *p_child_mtn;

	/*
	   Call the user, then recurse.
	 */
	p_func(p_mgrp, p_mtn, context);

	max_children = osm_mtree_node_get_max_children(p_mtn);
	for (i = 0; i < max_children; i++) {
		p_child_mtn = osm_mtree_node_get_child(p_mtn, i);
		if (p_child_mtn)
			__osm_mgrp_apply_func_sub(p_mgrp, p_child_mtn, p_func,
						  context);
	}
}

/**********************************************************************
 **********************************************************************/
void
osm_mgrp_apply_func(const osm_mgrp_t * const p_mgrp,
		    osm_mgrp_func_t p_func, void *context)
{
	osm_mtree_node_t *p_mtn;

	CL_ASSERT(p_mgrp);
	CL_ASSERT(p_func);

	p_mtn = p_mgrp->p_root;

	if (p_mtn)
		__osm_mgrp_apply_func_sub(p_mgrp, p_mtn, p_func, context);
}

/**********************************************************************
 **********************************************************************/
void
osm_mgrp_send_delete_notice(IN osm_subn_t * const p_subn,
			    IN osm_log_t * const p_log, IN osm_mgrp_t * p_mgrp)
{
	ib_mad_notice_attr_t notice;
	ib_api_status_t status;

	OSM_LOG_ENTER(p_log);

	/* prepare the needed info */

	/* details of the notice */
	notice.generic_type = 0x83;	/* is generic subn mgt type */
	ib_notice_set_prod_type_ho(&notice, 4);	/* A Class Manager generator */
	notice.g_or_v.generic.trap_num = CL_HTON16(67);	/* delete of mcg */
	/* The sm_base_lid is saved in network order already. */
	notice.issuer_lid = p_subn->sm_base_lid;
	/* following o14-12.1.11 and table 120 p726 */
	/* we need to provide the MGID */
	memcpy(&(notice.data_details.ntc_64_67.gid),
	       &(p_mgrp->mcmember_rec.mgid), sizeof(ib_gid_t));

	/* According to page 653 - the issuer gid in this case of trap
	   is the SM gid, since the SM is the initiator of this trap. */
	notice.issuer_gid.unicast.prefix = p_subn->opt.subnet_prefix;
	notice.issuer_gid.unicast.interface_id = p_subn->sm_port_guid;

	status = osm_report_notice(p_log, p_subn, &notice);
	if (status != IB_SUCCESS) {
		OSM_LOG(p_log, OSM_LOG_ERROR, "ERR 7601: "
			"Error sending trap reports (%s)\n",
			ib_get_err_str(status));
		goto Exit;
	}

Exit:
	OSM_LOG_EXIT(p_log);
}

/**********************************************************************
 **********************************************************************/
void
osm_mgrp_send_create_notice(IN osm_subn_t * const p_subn,
			    IN osm_log_t * const p_log, IN osm_mgrp_t * p_mgrp)
{
	ib_mad_notice_attr_t notice;
	ib_api_status_t status;

	OSM_LOG_ENTER(p_log);

	/* prepare the needed info */

	/* details of the notice */
	notice.generic_type = 0x83;	/* Generic SubnMgt type */
	ib_notice_set_prod_type_ho(&notice, 4);	/* A Class Manager generator */
	notice.g_or_v.generic.trap_num = CL_HTON16(66);	/* create of mcg */
	/* The sm_base_lid is saved in network order already. */
	notice.issuer_lid = p_subn->sm_base_lid;
	/* following o14-12.1.11 and table 120 p726 */
	/* we need to provide the MGID */
	memcpy(&(notice.data_details.ntc_64_67.gid),
	       &(p_mgrp->mcmember_rec.mgid), sizeof(ib_gid_t));

	/* According to page 653 - the issuer gid in this case of trap
	   is the SM gid, since the SM is the initiator of this trap. */
	notice.issuer_gid.unicast.prefix = p_subn->opt.subnet_prefix;
	notice.issuer_gid.unicast.interface_id = p_subn->sm_port_guid;

	status = osm_report_notice(p_log, p_subn, &notice);
	if (status != IB_SUCCESS) {
		OSM_LOG(p_log, OSM_LOG_ERROR, "ERR 7602: "
			"Error sending trap reports (%s)\n",
			ib_get_err_str(status));
		goto Exit;
	}

Exit:
	OSM_LOG_EXIT(p_log);
}

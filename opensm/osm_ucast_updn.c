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
 *      Implementation of Up Down Algorithm using ranking & Min Hop
 *      Calculation functions
 *
 * Environment:
 *      Linux User Mode
 *
 * $Revision: 1.0 $
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <stdlib.h>
#include <complib/cl_debug.h>
#include <complib/cl_qmap.h>
#include <opensm/osm_switch.h>
#include <opensm/osm_opensm.h>
#include <opensm/osm_ucast_mgr.h>

/* //////////////////////////// */
/*  Local types                 */
/* //////////////////////////// */

/* direction */
typedef enum _updn_switch_dir {
	UP = 0,
	DOWN
} updn_switch_dir_t;

/* guids list */
typedef struct _updn_input {
	uint32_t num_guids;
	uint64_t *guid_list;
} updn_input_t;

/* updn structure */
typedef struct _updn {
	boolean_t auto_detect_root_nodes;
	updn_input_t updn_ucast_reg_inputs;
	cl_list_t *p_root_nodes;
	osm_opensm_t *p_osm;
} updn_t;

struct updn_node {
	cl_list_item_t list;
	osm_switch_t *sw;
	updn_switch_dir_t dir;
	unsigned rank;
	unsigned visited;
};

/* ///////////////////////////////// */
/*  Statics                          */
/* ///////////////////////////////// */
static void __osm_updn_find_root_nodes_by_min_hop(OUT updn_t * p_updn);

/**********************************************************************
 **********************************************************************/
/* This function returns direction based on rank and guid info of current &
   remote ports */
static updn_switch_dir_t
__updn_get_dir(IN unsigned cur_rank,
	       IN unsigned rem_rank, IN uint64_t cur_guid, IN uint64_t rem_guid)
{
	/* HACK: comes to solve root nodes connection, in a classic subnet root nodes do not connect
	   directly, but in case they are we assign to root node an UP direction to allow UPDN to discover
	   the subnet correctly (and not from the point of view of the last root node).
	 */
	if (!cur_rank && !rem_rank)
		return UP;

	if (cur_rank < rem_rank)
		return DOWN;
	else if (cur_rank > rem_rank)
		return UP;
	else {
		/* Equal rank, decide by guid number, bigger == UP direction */
		if (cur_guid > rem_guid)
			return UP;
		else
			return DOWN;
	}
}

/**********************************************************************
 * This function does the bfs of min hop table calculation by guid index
 * as a starting point.
 **********************************************************************/
static int
__updn_bfs_by_node(IN osm_log_t * p_log,
		   IN osm_subn_t * p_subn, IN osm_switch_t * p_sw)
{
	uint8_t pn, pn_rem;
	cl_qlist_t list;
	uint16_t lid;
	struct updn_node *u;
	updn_switch_dir_t next_dir, current_dir;

	OSM_LOG_ENTER(p_log);

	lid = osm_node_get_base_lid(p_sw->p_node, 0);
	lid = cl_ntoh16(lid);
	osm_switch_set_hops(p_sw, lid, 0, 0);

	OSM_LOG(p_log, OSM_LOG_DEBUG,
		"Starting from switch - port GUID 0x%" PRIx64 " lid %u\n",
		cl_ntoh64(p_sw->p_node->node_info.port_guid), lid);

	u = p_sw->priv;
	u->dir = UP;

	/* Update list with the new element */
	cl_qlist_init(&list);
	cl_qlist_insert_tail(&list, &u->list);

	/* BFS the list till no next element */
	while (!cl_is_qlist_empty(&list)) {
		ib_net64_t remote_guid, current_guid;

		u = (struct updn_node *)cl_qlist_remove_head(&list);
		u->visited = 0;	/* cleanup */
		current_dir = u->dir;
		current_guid = osm_node_get_node_guid(u->sw->p_node);
		/* Go over all ports of the switch and find unvisited remote nodes */
		for (pn = 1; pn < u->sw->num_ports; pn++) {
			osm_node_t *p_remote_node;
			struct updn_node *rem_u;
			uint8_t current_min_hop, remote_min_hop,
			    set_hop_return_value;
			osm_switch_t *p_remote_sw;

			p_remote_node =
			    osm_node_get_remote_node(u->sw->p_node, pn,
						     &pn_rem);
			/* If no remote node OR remote node is not a SWITCH
			   continue to next pn */
			if (!p_remote_node || !p_remote_node->sw)
				continue;
			/* Fetch remote guid only after validation of remote node */
			remote_guid = osm_node_get_node_guid(p_remote_node);
			p_remote_sw = p_remote_node->sw;
			rem_u = p_remote_sw->priv;
			/* Decide which direction to mark it (UP/DOWN) */
			next_dir = __updn_get_dir(u->rank, rem_u->rank,
						  cl_ntoh64(current_guid),
						  cl_ntoh64(remote_guid));

			/* Check if this is a legal step : the only illegal step is going
			   from DOWN to UP */
			if ((current_dir == DOWN) && (next_dir == UP)) {
				OSM_LOG(p_log, OSM_LOG_DEBUG,
					"Avoiding move from 0x%016" PRIx64
					" to 0x%016" PRIx64 "\n",
					cl_ntoh64(current_guid),
					cl_ntoh64(remote_guid));
				/* Illegal step */
				continue;
			}
			/* Set MinHop value for the current lid */
			current_min_hop = osm_switch_get_least_hops(u->sw, lid);
			/* Check hop count if better insert into list && update
			   the remote node Min Hop Table */
			remote_min_hop =
			    osm_switch_get_hop_count(p_remote_sw, lid, pn_rem);
			if (current_min_hop + 1 < remote_min_hop) {
				set_hop_return_value =
				    osm_switch_set_hops(p_remote_sw, lid,
							pn_rem,
							current_min_hop + 1);
				if (set_hop_return_value) {
					OSM_LOG(p_log, OSM_LOG_ERROR, "ERR AA01: "
						"Invalid value returned from set min hop is: %d\n",
						set_hop_return_value);
				}
				/* Check if remote port has already been visited */
				if (!rem_u->visited) {
					/* Insert updn_switch item into the list */
					rem_u->dir = next_dir;
					rem_u->visited = 1;
					cl_qlist_insert_tail(&list,
							     &rem_u->list);
				}
			}
		}
	}

	OSM_LOG_EXIT(p_log);
	return 0;
}

/**********************************************************************
 **********************************************************************/
static void updn_destroy(IN updn_t * const p_updn)
{
	uint64_t *p_guid_list_item;

	/* free the array of guids */
	if (p_updn->updn_ucast_reg_inputs.guid_list)
		free(p_updn->updn_ucast_reg_inputs.guid_list);

	/* destroy the list of root nodes */
	if (p_updn->p_root_nodes) {
		while ((p_guid_list_item =
			cl_list_remove_head(p_updn->p_root_nodes)))
			free(p_guid_list_item);
		cl_list_remove_all(p_updn->p_root_nodes);
		cl_list_destroy(p_updn->p_root_nodes);
		free(p_updn->p_root_nodes);
	}
	free(p_updn);
}

/**********************************************************************
 **********************************************************************/
static updn_t *updn_construct(osm_log_t * p_log)
{
	updn_t *p_updn;

	OSM_LOG_ENTER(p_log);

	p_updn = malloc(sizeof(updn_t));
	if (p_updn)
		memset(p_updn, 0, sizeof(updn_t));

	OSM_LOG_EXIT(p_log);
	return (p_updn);
}

/**********************************************************************
 **********************************************************************/
static cl_status_t updn_init(IN updn_t * const p_updn, IN osm_opensm_t * p_osm)
{
	cl_list_t *p_list;
	cl_list_iterator_t guid_iterator;
	ib_api_status_t status = IB_SUCCESS;

	OSM_LOG_ENTER(&p_osm->log);

	p_updn->p_osm = p_osm;
	p_list = (cl_list_t *) malloc(sizeof(cl_list_t));
	if (!p_list) {
		status = IB_ERROR;
		goto Exit;
	}

	cl_list_construct(p_list);
	cl_list_init(p_list, 10);
	p_updn->p_root_nodes = p_list;
	p_updn->updn_ucast_reg_inputs.num_guids = 0;
	p_updn->updn_ucast_reg_inputs.guid_list = NULL;
	p_updn->auto_detect_root_nodes = FALSE;

	/*
	   Check the source for root node list, if file parse it, otherwise
	   wait for a callback to activate auto detection
	 */
	if (p_osm->subn.opt.root_guid_file) {
		status = osm_ucast_mgr_read_guid_file(&p_osm->sm.ucast_mgr,
						      p_osm->subn.opt.
						      root_guid_file,
						      p_updn->p_root_nodes);
		if (status != IB_SUCCESS)
			goto Exit;

		/* For Debug Purposes ... */
		OSM_LOG(&p_osm->log, OSM_LOG_DEBUG,
			"UPDN - Fetching root nodes from file %s\n",
			p_osm->subn.opt.root_guid_file);
		guid_iterator = cl_list_head(p_updn->p_root_nodes);
		while (guid_iterator != cl_list_end(p_updn->p_root_nodes)) {
			OSM_LOG(&p_osm->log, OSM_LOG_DEBUG,
				"Inserting GUID 0x%" PRIx64 " as root node\n",
				*((uint64_t *) cl_list_obj(guid_iterator)));
			guid_iterator = cl_list_next(guid_iterator);
		}
	} else {
		p_updn->auto_detect_root_nodes = TRUE;
	}
	/* If auto mode detection required - will be executed in main b4 the assignment of UI Ucast */

Exit:
	OSM_LOG_EXIT(&p_osm->log);
	return (status);
}

/**********************************************************************
 **********************************************************************/
/* NOTE : PLS check if we need to decide that the first */
/*        rank is a SWITCH for BFS purpose */
static int
updn_subn_rank(IN unsigned num_guids,
	       IN uint64_t * guid_list, IN updn_t * p_updn)
{
	osm_switch_t *p_sw;
	osm_physp_t *p_physp, *p_remote_physp;
	cl_qlist_t list;
	struct updn_node *u, *remote_u;
	uint8_t num_ports, port_num;
	osm_log_t *p_log = &p_updn->p_osm->log;
	unsigned idx = 0;
	unsigned max_rank = 0;

	OSM_LOG_ENTER(p_log);
	cl_qlist_init(&list);

	/* Rank all the roots and add them to list */

	for (idx = 0; idx < num_guids; idx++) {
		/* Apply the ranking for each guid given by user - bypass illegal ones */
		p_sw =
		    osm_get_switch_by_guid(&p_updn->p_osm->subn,
					   cl_hton64(guid_list[idx]));
		if (!p_sw) {
			OSM_LOG(p_log, OSM_LOG_ERROR, "ERR AA05: "
				"Root switch GUID 0x%" PRIx64 " not found\n",
				guid_list[idx]);
			continue;
		}

		u = p_sw->priv;
		OSM_LOG(p_log, OSM_LOG_DEBUG,
			"Ranking root port GUID 0x%" PRIx64 "\n",
			guid_list[idx]);
		u->rank = 0;
		cl_qlist_insert_tail(&list, &u->list);
	}

	/* BFS the list till it's empty */
	while (!cl_is_qlist_empty(&list)) {
		u = (struct updn_node *)cl_qlist_remove_head(&list);
		/* Go over all remote nodes and rank them (if not already visited) */
		p_sw = u->sw;
		num_ports = p_sw->num_ports;
		OSM_LOG(p_log, OSM_LOG_DEBUG,
			"Handling switch GUID 0x%" PRIx64 "\n",
			cl_ntoh64(osm_node_get_node_guid(p_sw->p_node)));
		for (port_num = 1; port_num < num_ports; port_num++) {
			ib_net64_t port_guid;

			/* Current port fetched in order to get remote side */
			p_physp =
			    osm_node_get_physp_ptr(p_sw->p_node, port_num);

			if (!p_physp)
				continue;

			p_remote_physp = p_physp->p_remote_physp;

			/*
			   make sure that all the following occur on p_remote_physp:
			   1. The port isn't NULL
			   2. It is a switch
			 */
			if (p_remote_physp && p_remote_physp->p_node->sw) {
				remote_u = p_remote_physp->p_node->sw->priv;
				port_guid = p_remote_physp->port_guid;

				if (remote_u->rank > u->rank + 1) {
					remote_u->rank = u->rank + 1;
					max_rank = remote_u->rank;
					cl_qlist_insert_tail(&list,
							     &remote_u->list);
					OSM_LOG(p_log, OSM_LOG_DEBUG,
						"Rank of port GUID 0x%" PRIx64
						" = %u\n", cl_ntoh64(port_guid),
						remote_u->rank);
				}
			}
		}
	}

	/* Print Summary of ranking */
	OSM_LOG(p_log, OSM_LOG_VERBOSE,
		"Subnet ranking completed. Max Node Rank = %d\n", max_rank);
	OSM_LOG_EXIT(p_log);
	return 0;
}

/**********************************************************************
 **********************************************************************/
/* hack: preserve min hops entries to any other root switches */
static void updn_clear_root_hops(updn_t * p_updn, osm_switch_t * p_sw)
{
	osm_port_t *p_port;
	unsigned i;

	for (i = 0; i < p_sw->num_hops; i++)
		if (p_sw->hops[i]) {
			p_port =
			    cl_ptr_vector_get(&p_updn->p_osm->subn.port_lid_tbl,
					      i);
			if (!p_port || !p_port->p_node->sw
			    || ((struct updn_node *)p_port->p_node->sw->priv)->
			    rank != 0)
				memset(p_sw->hops[i], 0xff, p_sw->num_ports);
		}
}

/**********************************************************************
 **********************************************************************/
static int __osm_subn_set_up_down_min_hop_table(IN updn_t * p_updn)
{
	osm_subn_t *p_subn = &p_updn->p_osm->subn;
	osm_log_t *p_log = &p_updn->p_osm->log;
	osm_switch_t *p_next_sw, *p_sw;

	OSM_LOG_ENTER(p_log);

	/* Go over all the switches in the subnet - for each init their Min Hop
	   Table */
	OSM_LOG(p_log, OSM_LOG_VERBOSE,
		"Init Min Hop Table of all switches [\n");

	p_next_sw = (osm_switch_t *) cl_qmap_head(&p_subn->sw_guid_tbl);
	while (p_next_sw != (osm_switch_t *) cl_qmap_end(&p_subn->sw_guid_tbl)) {
		p_sw = p_next_sw;
		p_next_sw = (osm_switch_t *) cl_qmap_next(&p_sw->map_item);
		/* Clear Min Hop Table */
		if (p_subn->opt.connect_roots)
			updn_clear_root_hops(p_updn, p_sw);
		else
			osm_switch_clear_hops(p_sw);
	}

	OSM_LOG(p_log, OSM_LOG_VERBOSE,
		"Init Min Hop Table of all switches ]\n");

	/* Now do the BFS for each port  in the subnet */
	OSM_LOG(p_log, OSM_LOG_VERBOSE,
		"BFS through all port guids in the subnet [\n");

	p_next_sw = (osm_switch_t *) cl_qmap_head(&p_subn->sw_guid_tbl);
	while (p_next_sw != (osm_switch_t *) cl_qmap_end(&p_subn->sw_guid_tbl)) {
		p_sw = p_next_sw;
		p_next_sw = (osm_switch_t *) cl_qmap_next(&p_sw->map_item);
		__updn_bfs_by_node(p_log, p_subn, p_sw);
	}

	OSM_LOG(p_log, OSM_LOG_VERBOSE,
		"BFS through all port guids in the subnet ]\n");
	/* Cleanup */
	OSM_LOG_EXIT(p_log);
	return 0;
}

/**********************************************************************
 **********************************************************************/
static int
updn_build_lid_matrices(IN uint32_t num_guids,
			IN uint64_t * guid_list, IN updn_t * p_updn)
{
	int status;

	OSM_LOG_ENTER(&p_updn->p_osm->log);

	OSM_LOG(&p_updn->p_osm->log, OSM_LOG_VERBOSE,
		"Ranking all port guids in the list\n");
	if (num_guids == 0) {
		OSM_LOG(&p_updn->p_osm->log, OSM_LOG_ERROR, "ERR AA0A: "
			"No guids were provided or number of guids is 0\n");
		status = -1;
		goto _exit;
	}

	/* Check if it's not a switched subnet */
	if (cl_is_qmap_empty(&p_updn->p_osm->subn.sw_guid_tbl)) {
		OSM_LOG(&p_updn->p_osm->log, OSM_LOG_ERROR, "ERR AAOB: "
			"This is not a switched subnet, cannot perform UPDN algorithm\n");
		status = -1;
		goto _exit;
	}

	/* Rank the subnet switches */
	updn_subn_rank(num_guids, guid_list, p_updn);

	/* After multiple ranking need to set Min Hop Table by UpDn algorithm  */
	OSM_LOG(&p_updn->p_osm->log, OSM_LOG_VERBOSE,
		"Setting all switches' Min Hop Table\n");
	status = __osm_subn_set_up_down_min_hop_table(p_updn);

_exit:
	OSM_LOG_EXIT(&p_updn->p_osm->log);
	return status;
}

/**********************************************************************
 **********************************************************************/
static struct updn_node *create_updn_node(osm_switch_t * sw)
{
	struct updn_node *u;

	u = malloc(sizeof(*u));
	if (!u)
		return NULL;
	memset(u, 0, sizeof(*u));
	u->sw = sw;
	u->rank = 0xffffffff;
	return u;
}

static void delete_updn_node(struct updn_node *u)
{
	u->sw->priv = NULL;
	free(u);
}

/**********************************************************************
 **********************************************************************/
static void dump_roots(cl_map_item_t *item, FILE *file, void *cxt)
{
	osm_switch_t *sw = (osm_switch_t *)item;
	if (!((struct updn_node *)sw->priv)->rank)
		fprintf(file, "0x%" PRIx64 "\n",
			cl_ntoh64(osm_node_get_node_guid(sw->p_node)));
}

/* UPDN callback function */
static int __osm_updn_call(void *ctx)
{
	updn_t *p_updn = ctx;
	cl_map_item_t *p_item;
	osm_switch_t *p_sw;
	int ret = 0;

	OSM_LOG_ENTER(&p_updn->p_osm->log);

	p_item = cl_qmap_head(&p_updn->p_osm->subn.sw_guid_tbl);
	while (p_item != cl_qmap_end(&p_updn->p_osm->subn.sw_guid_tbl)) {
		p_sw = (osm_switch_t *) p_item;
		p_item = cl_qmap_next(p_item);
		p_sw->priv = create_updn_node(p_sw);
		if (!p_sw->priv) {
			OSM_LOG(&(p_updn->p_osm->log), OSM_LOG_ERROR, "ERR AA0C: "
				"cannot create updn node\n");
			OSM_LOG_EXIT(&p_updn->p_osm->log);
			return -1;
		}
	}

	/* First auto detect root nodes - if required */
	if (p_updn->auto_detect_root_nodes) {
		osm_ucast_mgr_build_lid_matrices(&p_updn->p_osm->sm.ucast_mgr);
		__osm_updn_find_root_nodes_by_min_hop(p_updn);
	} else if (p_updn->p_osm->subn.opt.connect_roots &&
		   p_updn->updn_ucast_reg_inputs.num_guids > 1)
		osm_ucast_mgr_build_lid_matrices(&p_updn->p_osm->sm.ucast_mgr);

	/* printf ("-V- after osm_updn_find_root_nodes_by_min_hop\n"); */
	/* Only if there are assigned root nodes do the algorithm, otherwise perform do nothing */
	if (p_updn->updn_ucast_reg_inputs.num_guids > 0) {
		OSM_LOG(&p_updn->p_osm->log, OSM_LOG_DEBUG,
			"activating UPDN algorithm\n");
		ret = updn_build_lid_matrices(p_updn->updn_ucast_reg_inputs.
					      num_guids,
					      p_updn->updn_ucast_reg_inputs.
					      guid_list, p_updn);
	} else {
		OSM_LOG(&p_updn->p_osm->log, OSM_LOG_INFO,
			"disabling UPDN algorithm, no root nodes were found\n");
		ret = 1;
	}

	if (osm_log_is_active(&p_updn->p_osm->log, OSM_LOG_ROUTING))
		osm_dump_qmap_to_file(p_updn->p_osm, "opensm-updn-roots.dump",
				      &p_updn->p_osm->subn.sw_guid_tbl,
				      dump_roots, NULL);

	p_item = cl_qmap_head(&p_updn->p_osm->subn.sw_guid_tbl);
	while (p_item != cl_qmap_end(&p_updn->p_osm->subn.sw_guid_tbl)) {
		p_sw = (osm_switch_t *) p_item;
		p_item = cl_qmap_next(p_item);
		delete_updn_node(p_sw->priv);
	}

	OSM_LOG_EXIT(&p_updn->p_osm->log);
	return ret;
}

/**********************************************************************
 **********************************************************************/
/* UPDN convert cl_list to guid array in updn struct */
static void __osm_updn_convert_list2array(IN updn_t * p_updn)
{
	uint32_t i = 0, max_num = 0;
	uint64_t *p_guid;

	OSM_LOG_ENTER(&p_updn->p_osm->log);

	p_updn->updn_ucast_reg_inputs.num_guids =
	    cl_list_count(p_updn->p_root_nodes);
	if (p_updn->updn_ucast_reg_inputs.guid_list)
		free(p_updn->updn_ucast_reg_inputs.guid_list);
	p_updn->updn_ucast_reg_inputs.guid_list =
	    (uint64_t *) malloc(p_updn->updn_ucast_reg_inputs.num_guids *
				sizeof(uint64_t));
	if (p_updn->updn_ucast_reg_inputs.guid_list)
		memset(p_updn->updn_ucast_reg_inputs.guid_list, 0,
		       p_updn->updn_ucast_reg_inputs.num_guids *
		       sizeof(uint64_t));
	if (!cl_is_list_empty(p_updn->p_root_nodes)) {
		while ((p_guid =
			(uint64_t *) cl_list_remove_head(p_updn->
							 p_root_nodes))) {
			p_updn->updn_ucast_reg_inputs.guid_list[i] = *p_guid;
			free(p_guid);
			i++;
		}
		max_num = i;
		for (i = 0; i < max_num; i++)
			OSM_LOG(&p_updn->p_osm->log, OSM_LOG_DEBUG,
				"Map GUID 0x%" PRIx64 " into UPDN array\n",
				p_updn->updn_ucast_reg_inputs.guid_list[i]);
	}
	/* Since we need the template list for other sweeps, we wont destroy & free it */
	OSM_LOG_EXIT(&p_updn->p_osm->log);
}

/**********************************************************************
 **********************************************************************/
/* Find Root nodes automatically by Min Hop Table info */
static void __osm_updn_find_root_nodes_by_min_hop(OUT updn_t * p_updn)
{
	osm_opensm_t *p_osm = p_updn->p_osm;
	osm_switch_t *p_next_sw, *p_sw;
	osm_port_t *p_next_port, *p_port;
	osm_physp_t *p_physp;
	uint64_t *p_guid;
	cl_list_t *p_root_nodes_list = p_updn->p_root_nodes;
	double thd1, thd2;
	unsigned i, cas_num = 0;
	unsigned *cas_per_sw;
	uint16_t lid_ho;

	OSM_LOG_ENTER(&p_osm->log);

	OSM_LOG(&p_osm->log, OSM_LOG_DEBUG,
		"Current number of ports in the subnet is %d\n",
		cl_qmap_count(&p_osm->subn.port_guid_tbl));

	cas_per_sw = malloc((IB_LID_UCAST_END_HO + 1) * sizeof(*cas_per_sw));
	if (!cas_per_sw) {
		OSM_LOG(&p_osm->log, OSM_LOG_ERROR, "ERR AA14: "
			"cannot alloc mem for CAs per switch counter array\n");
		goto _exit;
	}
	memset(cas_per_sw, 0, (IB_LID_UCAST_END_HO + 1) * sizeof(*cas_per_sw));

	/* Find the Maximum number of CAs (and routers) for histogram normalization */
	OSM_LOG(&p_osm->log, OSM_LOG_VERBOSE,
		"Finding the number of CAs and storing them in cl_map\n");
	p_next_port = (osm_port_t *) cl_qmap_head(&p_osm->subn.port_guid_tbl);
	while (p_next_port !=
	       (osm_port_t *) cl_qmap_end(&p_osm->subn.port_guid_tbl)) {
		p_port = p_next_port;
		p_next_port =
		    (osm_port_t *) cl_qmap_next(&p_next_port->map_item);
		if (!p_port->p_node->sw) {
			p_physp = p_port->p_physp->p_remote_physp;
			if (!p_physp || !p_physp->p_node->sw)
				continue;
			lid_ho = osm_node_get_base_lid(p_physp->p_node, 0);
			lid_ho = cl_ntoh16(lid_ho);
			OSM_LOG(&p_osm->log, OSM_LOG_DEBUG,
				"Inserting GUID 0x%" PRIx64
				", sw lid: 0x%X into array\n",
				cl_ntoh64(osm_port_get_guid(p_port)), lid_ho);
			cas_per_sw[lid_ho]++;
			cas_num++;
		}
	}

	thd1 = cas_num * 0.9;
	thd2 = cas_num * 0.05;
	OSM_LOG(&p_osm->log, OSM_LOG_DEBUG,
		"Found %u CAs and RTRs, %u SWs in the subnet. "
		"Thresholds are thd1 = %f && thd2 = %f\n",
		cas_num, cl_qmap_count(&p_osm->subn.sw_guid_tbl), thd1, thd2);

	p_next_sw = (osm_switch_t *) cl_qmap_head(&p_osm->subn.sw_guid_tbl);
	OSM_LOG(&p_osm->log, OSM_LOG_VERBOSE,
		"Passing through all switches to collect Min Hop info\n");
	while (p_next_sw !=
	       (osm_switch_t *) cl_qmap_end(&p_osm->subn.sw_guid_tbl)) {
		unsigned hop_hist[IB_SUBNET_PATH_HOPS_MAX];
		uint16_t max_lid_ho;
		uint8_t hop_val;
		uint16_t numHopBarsOverThd1 = 0;
		uint16_t numHopBarsOverThd2 = 0;

		p_sw = p_next_sw;
		/* Roll to the next switch */
		p_next_sw = (osm_switch_t *) cl_qmap_next(&p_sw->map_item);

		memset(hop_hist, 0, sizeof(hop_hist));

		max_lid_ho = p_sw->max_lid_ho;
		/* Get base lid of switch by retrieving port 0 lid of node pointer */
		OSM_LOG(&p_osm->log, OSM_LOG_DEBUG,
			"Passing through switch lid 0x%X\n",
			cl_ntoh16(osm_node_get_base_lid(p_sw->p_node, 0)));
		for (lid_ho = 1; lid_ho <= max_lid_ho; lid_ho++)
			if (cas_per_sw[lid_ho]) {
				hop_val =
				    osm_switch_get_least_hops(p_sw, lid_ho);
				if (hop_val >= IB_SUBNET_PATH_HOPS_MAX)
					continue;

				hop_hist[hop_val] += cas_per_sw[lid_ho];
			}

		/* Now recognize the spines by requiring one bar to be above 90% of the
		   number of CAs and RTRs */
		for (i = 0; i < IB_SUBNET_PATH_HOPS_MAX; i++) {
			if (hop_hist[i] > thd1)
				numHopBarsOverThd1++;
			if (hop_hist[i] > thd2)
				numHopBarsOverThd2++;
		}

		/* If thd conditions are valid insert the root node to the list */
		if ((numHopBarsOverThd1 == 1) && (numHopBarsOverThd2 == 1)) {
			p_guid = malloc(sizeof(uint64_t));
			if (p_guid) {
				*p_guid =
				    cl_ntoh64(osm_node_get_node_guid
					      (p_sw->p_node));
				OSM_LOG(&p_osm->log, OSM_LOG_DEBUG,
					"Inserting GUID 0x%" PRIx64
					" as root node\n", *p_guid);
				cl_list_insert_tail(p_root_nodes_list, p_guid);
			} else {
				OSM_LOG(&p_osm->log, OSM_LOG_ERROR, "ERR AA13: "
					"No memory for p_guid\n");
			}
		}
	}

	free(cas_per_sw);
	/* Now convert the cl_list to array */
	__osm_updn_convert_list2array(p_updn);

_exit:
	OSM_LOG_EXIT(&p_osm->log);
	return;
}

/**********************************************************************
 **********************************************************************/
static void __osm_updn_delete(void *context)
{
	updn_t *p_updn = context;

	updn_destroy(p_updn);
}

int osm_ucast_updn_setup(osm_opensm_t * p_osm)
{
	updn_t *p_updn;

	p_updn = updn_construct(&p_osm->log);
	if (!p_updn)
		return -1;

	if (updn_init(p_updn, p_osm) != IB_SUCCESS) {
		updn_destroy(p_updn);
		return -1;
	}

	p_osm->routing_engine.context = p_updn;
	p_osm->routing_engine.delete = __osm_updn_delete;
	p_osm->routing_engine.build_lid_matrices = __osm_updn_call;

	if (!p_updn->auto_detect_root_nodes)
		__osm_updn_convert_list2array(p_updn);

	return 0;
}

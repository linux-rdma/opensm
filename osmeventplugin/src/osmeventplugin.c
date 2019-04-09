/*
 * Copyright (c) 2013 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 2008 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2007 The Regents of the University of California.
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

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <dlfcn.h>
#include <stdint.h>
#include <opensm/osm_config.h>
#include <complib/cl_qmap.h>
#include <complib/cl_passivelock.h>
#include <opensm/osm_version.h>
#include <opensm/osm_opensm.h>
#include <opensm/osm_log.h>

/** =========================================================================
 * This is a simple example plugin which:
 *  1) Implement the routing engine API
 *  2) logs some of the events the OSM generates to this interface.
 */

#define SAMPLE_PLUGIN_OUTPUT_FILE "/tmp/osm_sample_event_plugin_output"

struct  plugin_t {
	osm_opensm_t *osm;
	FILE *log_file;
};

/** =========================================================================
 * Forward declarations
 */
static void *construct(osm_opensm_t *osm);

static void destroy(void *context);

static void report(
	void *context,
	osm_epi_event_id_t event_id,
	void *event_data);

static int plugin_build_lid_matrices(
	IN void *context);

static int plugin_ucast_build_fwd_tables(
	IN void *context);

static void plugin_ucast_dump_tables(
	IN void *context);

static void plugin_update_sl2vl(
	void *context,
	IN osm_physp_t *port,
	IN uint8_t in_port_num,
	IN uint8_t out_port_num,
	IN OUT ib_slvl_table_t *t);

static void plugin_update_vlarb(
	void *context,
	IN osm_physp_t *port,
	IN uint8_t port_num,
	IN OUT ib_vl_arb_table_t *block,
	unsigned int block_length,
	unsigned int block_num);

static uint8_t plugin_path_sl(
	IN void *context,
	IN uint8_t path_sl_hint,
	IN const ib_net16_t slid,
	IN const ib_net16_t dlid);

static ib_api_status_t plugin_mcast_build_stree(
	IN void *context,
	IN OUT osm_mgrp_box_t *mgb);

static void plugin_destroy_routing_engine(
	IN void *context);

static int routing_engine_setup(
	osm_routing_engine_t *engine,
	osm_opensm_t *osm);

/** =========================================================================
 * Implement plugin functions
 */
static void *construct(osm_opensm_t *osm)
{
	struct plugin_t *plugin;
	cl_status_t status;

	plugin = (struct plugin_t *) calloc(1, sizeof(struct plugin_t));
	if (!plugin)
		return NULL;

	plugin->osm = osm;
	routing_engine_module_t plugin_routing_engine_module = {
		 "routing_engine_plugin",
		  OSM_ROUTING_ENGINE_TYPE_UNKNOWN, /* Generate a new type */
		  routing_engine_setup,
		  plugin,
	};

	status = osm_opensm_register_routing_engine(
		osm, &plugin_routing_engine_module, plugin);
	if (status != CL_SUCCESS) {
		destroy(plugin);
		return NULL;
	}

	OSM_LOG(&plugin->osm->log, OSM_LOG_INFO,
		"External routing engine '%s' has been registered with type '%d'\n",
		plugin_routing_engine_module.name,
		plugin_routing_engine_module.type);

	plugin->log_file = fopen(SAMPLE_PLUGIN_OUTPUT_FILE, "a+");
	if (!(plugin->log_file)) {
		osm_log(&osm->log, OSM_LOG_ERROR,
			"Sample Event Plugin: Failed to open output file \"%s\"\n",
			SAMPLE_PLUGIN_OUTPUT_FILE);
		destroy(plugin);
		return NULL;
	}

	return ((void *)plugin);
}

/** =========================================================================
 */
static void destroy(void *context)
{
	struct plugin_t *plugin = (struct plugin_t *) context;

	if (plugin) {
		OSM_LOG(&plugin->osm->log, OSM_LOG_INFO,
			"Destroying plugin...\n");

		if (plugin->log_file)
			fclose(plugin->log_file);

		free(plugin);
	}
}

/** =========================================================================
 */
static void handle_port_counter(
	struct plugin_t *plugin, osm_epi_pe_event_t *pc)
{
	if (pc->symbol_err_cnt > 0
	    || pc->link_err_recover > 0
	    || pc->link_downed > 0
	    || pc->rcv_err > 0
	    || pc->rcv_rem_phys_err > 0
	    || pc->rcv_switch_relay_err > 0
	    || pc->xmit_discards > 0
	    || pc->xmit_constraint_err > 0
	    || pc->rcv_constraint_err > 0
	    || pc->link_integrity > 0
	    || pc->buffer_overrun > 0
	    || pc->vl15_dropped > 0
	    || pc->xmit_wait > 0) {
		fprintf(plugin->log_file,
			"Port counter errors for node 0x%" PRIx64
			" (%s) port %d\n", pc->port_id.node_guid,
			pc->port_id.node_name, pc->port_id.port_num);
	}
}

/** =========================================================================
 */
static void
handle_port_counter_ext(
	struct plugin_t *plugin, osm_epi_dc_event_t *epc)
{
	fprintf(plugin->log_file,
		"Received Data counters for node 0x%" PRIx64 " (%s) port %d\n",
		epc->port_id.node_guid,
		epc->port_id.node_name, epc->port_id.port_num);
}

/** =========================================================================
 */
static void handle_port_select(
	struct plugin_t *plugin, osm_epi_ps_event_t *ps)
{
	if (ps->xmit_wait > 0) {
		fprintf(plugin->log_file,
			"Port select Xmit Wait counts for node 0x%" PRIx64
			" (%s) port %d\n", ps->port_id.node_guid,
			ps->port_id.node_name, ps->port_id.port_num);
	}
}

/** =========================================================================
 */
static void handle_trap_event(
	struct plugin_t *plugin, ib_mad_notice_attr_t *p_ntc)
{
	if (ib_notice_is_generic(p_ntc)) {
		fprintf(plugin->log_file,
			"Generic trap type %d; event %d; from LID %u\n",
			ib_notice_get_type(p_ntc),
			cl_ntoh16(p_ntc->g_or_v.generic.trap_num),
			cl_ntoh16(p_ntc->issuer_lid));
	} else {
		fprintf(plugin->log_file,
			"Vendor trap type %d; from LID %u\n",
			ib_notice_get_type(p_ntc),
			cl_ntoh16(p_ntc->issuer_lid));
	}

}

/** =========================================================================
 */
static void handle_lft_change_event(
	struct plugin_t *plugin,
	osm_epi_lft_change_event_t *lft_change)
{
	fprintf(plugin->log_file,
		"LFT changed for switch 0x%" PRIx64 " flags 0x%x LFTTop %u block %d\n",
		cl_ntoh64(osm_node_get_node_guid(lft_change->p_sw->p_node)),
		lft_change->flags, lft_change->lft_top, lft_change->block_num);
}

/** =========================================================================
 */
static void report(
	void *context,
	osm_epi_event_id_t event_id,
	void *event_data)
{
	struct plugin_t *plugin = (struct plugin_t *) context;

	switch (event_id) {
	case OSM_EVENT_ID_PORT_ERRORS:
		handle_port_counter(
			plugin, (osm_epi_pe_event_t *) event_data);
		break;
	case OSM_EVENT_ID_PORT_DATA_COUNTERS:
		handle_port_counter_ext(
			plugin, (osm_epi_dc_event_t *) event_data);
		break;
	case OSM_EVENT_ID_PORT_SELECT:
		handle_port_select(
			plugin, (osm_epi_ps_event_t *) event_data);
		break;
	case OSM_EVENT_ID_TRAP:
		handle_trap_event(
			plugin, (ib_mad_notice_attr_t *) event_data);
		break;
	case OSM_EVENT_ID_SUBNET_UP:
		fprintf(plugin->log_file, "Subnet up reported\n");
		break;
	case OSM_EVENT_ID_HEAVY_SWEEP_START:
		fprintf(plugin->log_file, "Heavy sweep started\n");
		break;
	case OSM_EVENT_ID_HEAVY_SWEEP_DONE:
		fprintf(plugin->log_file, "Heavy sweep completed\n");
		break;
	case OSM_EVENT_ID_UCAST_ROUTING_DONE:
		fprintf(plugin->log_file, "Unicast routing completed %d\n",
			(osm_epi_ucast_routing_flags_t) event_data);
		break;
	case OSM_EVENT_ID_STATE_CHANGE:
		fprintf(plugin->log_file, "SM state changed\n");
		break;
	case OSM_EVENT_ID_SA_DB_DUMPED:
		fprintf(plugin->log_file, "SA DB dump file updated\n");
		break;
	case OSM_EVENT_ID_LFT_CHANGE:
		handle_lft_change_event(
			plugin, (osm_epi_lft_change_event_t *) event_data);
		break;
	case OSM_EVENT_ID_MAX:
	default:
		osm_log(&plugin->osm->log, OSM_LOG_ERROR,
			"Unknown event (%d) reported to plugin\n", event_id);
	}
	fflush(plugin->log_file);
}

/** =========================================================================
 * Implement routing engine functions
 */
int routing_engine_setup(
	osm_routing_engine_t *engine,
	osm_opensm_t *osm)
{
	struct plugin_t *plugin = (struct plugin_t *) engine->context;

	OSM_LOG(&plugin->osm->log, OSM_LOG_INFO,
		"Setting up the plugin as a new routing engine...\n");

	engine->build_lid_matrices = plugin_build_lid_matrices;
	engine->ucast_build_fwd_tables = plugin_ucast_build_fwd_tables;
	engine->ucast_dump_tables = plugin_ucast_dump_tables;
	engine->update_sl2vl = plugin_update_sl2vl;
	engine->update_vlarb = plugin_update_vlarb;
	engine->path_sl = plugin_path_sl;
	engine->mcast_build_stree = plugin_mcast_build_stree;
	engine->destroy = plugin_destroy_routing_engine;

	return 0;
}

static int plugin_build_lid_matrices(
	IN void *context)
{
	struct plugin_t *plugin = (struct plugin_t *) context;

	OSM_LOG(&plugin->osm->log, OSM_LOG_ERROR,
		"Building LID matrices...\n");

	return 0;
}

static int plugin_ucast_build_fwd_tables(
	IN void *context)
{
	struct plugin_t *plugin = (struct plugin_t *) context;

	OSM_LOG(&plugin->osm->log, OSM_LOG_INFO,
		"Building Forwarding tables...\n");
	return 0;
}

static void plugin_ucast_dump_tables(
	IN void *context)
{
	struct plugin_t *plugin = (struct plugin_t *) context;

	OSM_LOG(&plugin->osm->log, OSM_LOG_INFO,
		"Dumping Unicast forwarding tables...\n");
}

static void plugin_update_sl2vl(
	void *context,
	IN osm_physp_t *port,
	IN uint8_t in_port_num, IN uint8_t out_port_num,
	IN OUT ib_slvl_table_t *t)
{
	struct plugin_t *plugin = (struct plugin_t *) context;

	OSM_LOG(&plugin->osm->log, OSM_LOG_INFO,
		"Update Service Layer to Virtual Lanes mapping...\n");
}

static void plugin_update_vlarb(
	void *context,
	IN osm_physp_t *port,
	IN uint8_t port_num,
	IN OUT ib_vl_arb_table_t *block,
	unsigned int block_length,
	unsigned int block_num)
{
	struct plugin_t *plugin = (struct plugin_t *) context;

	OSM_LOG(&plugin->osm->log, OSM_LOG_INFO,
		"Update Virtual Lane arbritration...\n");
}

static uint8_t plugin_path_sl(
	IN void *context,
	IN uint8_t path_sl_hint,
	IN const ib_net16_t slid,
	IN const ib_net16_t dlid)
{
	struct plugin_t *plugin = (struct plugin_t *) context;

	OSM_LOG(&plugin->osm->log, OSM_LOG_INFO,
		"Computing Service Layer for the path LID %d -> LID %d with hint: %d...\n",
		slid, dlid, path_sl_hint);
	return 0;
}

static ib_api_status_t plugin_mcast_build_stree(
	IN void *context,
	IN OUT osm_mgrp_box_t *mgb)
{
	struct plugin_t *plugin = (struct plugin_t *) context;

	OSM_LOG(&plugin->osm->log, OSM_LOG_INFO,
		"Building spanning tree for MLID: %d\n",
		mgb->mlid);
	return IB_SUCCESS;
}

static void plugin_destroy_routing_engine(
	IN void *context)
{
	struct plugin_t *plugin = (struct plugin_t *) context;

	OSM_LOG(&plugin->osm->log, OSM_LOG_INFO,
		"Destroying plugin routing engine\n");
}

/** =========================================================================
 * Define the object symbol for loading
 */

#if OSM_EVENT_PLUGIN_INTERFACE_VER != 2
#error OpenSM plugin interface version missmatch
#endif

osm_event_plugin_t osm_event_plugin = {
      OSM_VERSION,
      construct,
      destroy,
      report
};

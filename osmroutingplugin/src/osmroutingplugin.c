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
 * This is a simple routing engine plugin which implements the routing engine API
 */

struct  plugin_t {
	osm_opensm_t *osm;
};

/** =========================================================================
 * Forward declarations
 */
static void *construct(osm_opensm_t *osm);

static void destroy(void *context);

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
	external_routing_engine_module_t plugin_routing_engine_module = {
		 "routing_engine_plugin",
		  routing_engine_setup,
		  plugin,
	};

	status = osm_register_external_routing_engine(
		osm, &plugin_routing_engine_module, plugin);
	if (status != CL_SUCCESS) {
		destroy(plugin);
		return NULL;
	}

	OSM_LOG(&plugin->osm->log, OSM_LOG_INFO,
		"External routing engine '%s' has been registered with type '%d'\n",
		plugin_routing_engine_module.name,
		osm_routing_engine_type(plugin_routing_engine_module.name));

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

		free(plugin);
	}
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
      destroy
};

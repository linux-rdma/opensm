/*
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2011 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
 * Copyright (c) 2008 Xsigo Systems Inc.  All rights reserved.
 * Copyright (c) 2009 System Fabric Works, Inc. All rights reserved.
 * Copyright (c) 2009 HNR Consulting. All rights reserved.
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
 *    Implementation of osm_subn_t.
 * This object represents an IBA subnet.
 * This object is part of the opensm family of objects.
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <limits.h>
#include <errno.h>
#include <ctype.h>
#include <complib/cl_debug.h>
#include <complib/cl_log.h>
#include <opensm/osm_subnet.h>
#include <opensm/osm_opensm.h>
#include <opensm/osm_log.h>
#include <opensm/osm_madw.h>
#include <opensm/osm_port.h>
#include <opensm/osm_switch.h>
#include <opensm/osm_remote_sm.h>
#include <opensm/osm_partition.h>
#include <opensm/osm_node.h>
#include <opensm/osm_multicast.h>
#include <opensm/osm_inform.h>
#include <opensm/osm_console.h>
#include <opensm/osm_perfmgr.h>
#include <opensm/osm_event_plugin.h>
#include <opensm/osm_qos_policy.h>
#include <opensm/osm_service.h>

static const char null_str[] = "(null)";

#define OPT_OFFSET(opt) offsetof(osm_subn_opt_t, opt)

typedef struct opt_rec {
	const char *name;
	unsigned long opt_offset;
	void (*parse_fn)(osm_subn_t *p_subn, char *p_key, char *p_val_str,
			 void *p_val1, void *p_val2,
			 void (*)(osm_subn_t *, void *));
	void (*setup_fn)(osm_subn_t *p_subn, void *p_val);
	int  can_update;
} opt_rec_t;

static void log_report(const char *fmt, ...)
{
	char buf[128];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	printf("%s", buf);
	cl_log_event("OpenSM", CL_LOG_INFO, buf, NULL, 0);
}

static void log_config_value(char *name, const char *fmt, ...)
{
	char buf[128];
	va_list args;
	unsigned n;
	va_start(args, fmt);
	n = snprintf(buf, sizeof(buf), " Loading Cached Option:%s = ", name);
	if (n > sizeof(buf))
		n = sizeof(buf);
	n += vsnprintf(buf + n, sizeof(buf) - n, fmt, args);
	if (n > sizeof(buf) - 2)
		n = sizeof(buf) - 2;
	snprintf(buf + n, sizeof(buf) - n, "\n");
	va_end(args);
	printf("%s", buf);
	cl_log_event("OpenSM", CL_LOG_INFO, buf, NULL, 0);
}

static void opts_setup_log_flags(osm_subn_t *p_subn, void *p_val)
{
	p_subn->p_osm->log.level = *((uint8_t *) p_val);
}

static void opts_setup_force_log_flush(osm_subn_t *p_subn, void *p_val)
{
	p_subn->p_osm->log.flush = *((boolean_t *) p_val);
}

static void opts_setup_accum_log_file(osm_subn_t *p_subn, void *p_val)
{
	p_subn->p_osm->log.accum_log_file = *((boolean_t *) p_val);
}

static void opts_setup_log_max_size(osm_subn_t *p_subn, void *p_val)
{
	uint32_t log_max_size = *((uint32_t *) p_val);

	p_subn->p_osm->log.max_size = log_max_size << 20; /* convert from MB to bytes */
}

static void opts_setup_sminfo_polling_timeout(osm_subn_t *p_subn, void *p_val)
{
	osm_sm_t *p_sm = &p_subn->p_osm->sm;
	uint32_t sminfo_polling_timeout = *((uint32_t *) p_val);

	cl_timer_stop(&p_sm->polling_timer);
	cl_timer_start(&p_sm->polling_timer, sminfo_polling_timeout);
}

static void opts_setup_sm_priority(osm_subn_t *p_subn, void *p_val)
{
	osm_sm_t *p_sm = &p_subn->p_osm->sm;
	uint8_t sm_priority = *((uint8_t *) p_val);

	osm_set_sm_priority(p_sm, sm_priority);
}

static void opts_parse_net64(IN osm_subn_t *p_subn, IN char *p_key,
			     IN char *p_val_str, void *p_v1, void *p_v2,
			     void (*pfn)(osm_subn_t *, void *))
{
	uint64_t *p_val1 = p_v1, *p_val2 = p_v2;
	uint64_t val = strtoull(p_val_str, NULL, 0);

	if (cl_hton64(val) != *p_val1) {
		log_config_value(p_key, "0x%016" PRIx64, val);
		if (pfn)
			pfn(p_subn, &val);
		*p_val1 = *p_val2 = cl_ntoh64(val);
	}
}

static void opts_parse_uint32(IN osm_subn_t *p_subn, IN char *p_key,
			      IN char *p_val_str, void *p_v1, void *p_v2,
			      void (*pfn)(osm_subn_t *, void *))
{
	uint32_t *p_val1 = p_v1, *p_val2 = p_v2;
	uint32_t val = strtoul(p_val_str, NULL, 0);

	if (val != *p_val1) {
		log_config_value(p_key, "%u", val);
		if (pfn)
			pfn(p_subn, &val);
		*p_val1 = *p_val2 = val;
	}
}

static void opts_parse_int32(IN osm_subn_t *p_subn, IN char *p_key,
			     IN char *p_val_str, void *p_v1, void *p_v2,
			     void (*pfn)(osm_subn_t *, void *))
{
	int32_t *p_val1 = p_v1, *p_val2 = p_v2;
	int32_t val = strtol(p_val_str, NULL, 0);

	if (val != *p_val1) {
		log_config_value(p_key, "%d", val);
		if (pfn)
			pfn(p_subn, &val);
		*p_val1 = *p_val2 = val;
	}
}

static void opts_parse_uint16(IN osm_subn_t *p_subn, IN char *p_key,
			      IN char *p_val_str, void *p_v1, void *p_v2,
			      void (*pfn)(osm_subn_t *, void *))
{
	uint16_t *p_val1 = p_v1, *p_val2 = p_v2;
	uint16_t val = (uint16_t) strtoul(p_val_str, NULL, 0);

	if (val != *p_val1) {
		log_config_value(p_key, "%u", val);
		if (pfn)
			pfn(p_subn, &val);
		*p_val1 = *p_val2 = val;
	}
}

static void opts_parse_net16(IN osm_subn_t *p_subn, IN char *p_key,
			     IN char *p_val_str, void *p_v1, void *p_v2,
			     void (*pfn)(osm_subn_t *, void *))
{
	uint16_t *p_val1 = p_v1, *p_val2 = p_v2;
	uint16_t val = strtoul(p_val_str, NULL, 0);

	if (cl_hton16(val) != *p_val1) {
		log_config_value(p_key, "0x%04x", val);
		if (pfn)
			pfn(p_subn, &val);
		*p_val1 = *p_val2 = cl_hton16(val);
	}
}

static void opts_parse_uint8(IN osm_subn_t *p_subn, IN char *p_key,
			     IN char *p_val_str, void *p_v1, void *p_v2,
			     void (*pfn)(osm_subn_t *, void *))
{
	uint8_t *p_val1 = p_v1, *p_val2 = p_v2;
	uint8_t val = strtoul(p_val_str, NULL, 0);

	if (val != *p_val1) {
		log_config_value(p_key, "%u", val);
		if (pfn)
			pfn(p_subn, &val);
		*p_val1 = *p_val2 = val;
	}
}

static void opts_parse_boolean(IN osm_subn_t *p_subn, IN char *p_key,
			       IN char *p_val_str, void *p_v1, void *p_v2,
			       void (*pfn)(osm_subn_t *, void *))
{
	boolean_t *p_val1 = p_v1, *p_val2 = p_v2;
	boolean_t val;

	if (!p_val_str)
		return;

	if (strcmp("TRUE", p_val_str))
		val = FALSE;
	else
		val = TRUE;

	if (val != *p_val1) {
		log_config_value(p_key, "%s", p_val_str);
		if (pfn)
			pfn(p_subn, &val);
		*p_val1 = *p_val2 = val;
	}
}

static void opts_parse_charp(IN osm_subn_t *p_subn, IN char *p_key,
			     IN char *p_val_str, void *p_v1, void *p_v2,
			     void (*pfn)(osm_subn_t *, void *))
{
	char **p_val1 = p_v1, **p_val2 = p_v2;
	const char *current_str = *p_val1 ? *p_val1 : null_str ;

	if (p_val_str && strcmp(p_val_str, current_str)) {
		char *new;
		log_config_value(p_key, "%s", p_val_str);
		/* special case the "(null)" string */
		new = strcmp(null_str, p_val_str) ? strdup(p_val_str) : NULL;
		if (pfn)
			pfn(p_subn, new);
		if (*p_val1 && *p_val1 != *p_val2)
			free(*p_val1);
		if (*p_val2)
			free(*p_val2);
		*p_val1 = *p_val2 = new;
	}
}

static const opt_rec_t opt_tbl[] = {
	{ "guid", OPT_OFFSET(guid), opts_parse_net64, NULL, 0 },
	{ "m_key", OPT_OFFSET(m_key), opts_parse_net64, NULL, 1 },
	{ "sm_key", OPT_OFFSET(sm_key), opts_parse_net64, NULL, 1 },
	{ "sa_key", OPT_OFFSET(sa_key), opts_parse_net64, NULL, 1 },
	{ "subnet_prefix", OPT_OFFSET(subnet_prefix), opts_parse_net64, NULL, 1 },
	{ "m_key_lease_period", OPT_OFFSET(m_key_lease_period), opts_parse_net16, NULL, 1 },
	{ "sweep_interval", OPT_OFFSET(sweep_interval), opts_parse_uint32, NULL, 1 },
	{ "max_wire_smps", OPT_OFFSET(max_wire_smps), opts_parse_uint32, NULL, 1 },
	{ "max_wire_smps2", OPT_OFFSET(max_wire_smps2), opts_parse_uint32, NULL, 1 },
	{ "max_smps_timeout", OPT_OFFSET(max_smps_timeout), opts_parse_uint32, NULL, 1 },
	{ "console", OPT_OFFSET(console), opts_parse_charp, NULL, 0 },
	{ "console_port", OPT_OFFSET(console_port), opts_parse_uint16, NULL, 0 },
	{ "transaction_timeout", OPT_OFFSET(transaction_timeout), opts_parse_uint32, NULL, 0 },
	{ "transaction_retries", OPT_OFFSET(transaction_retries), opts_parse_uint32, NULL, 0 },
	{ "max_msg_fifo_timeout", OPT_OFFSET(max_msg_fifo_timeout), opts_parse_uint32, NULL, 1 },
	{ "sm_priority", OPT_OFFSET(sm_priority), opts_parse_uint8, opts_setup_sm_priority, 1 },
	{ "lmc", OPT_OFFSET(lmc), opts_parse_uint8, NULL, 1 },
	{ "lmc_esp0", OPT_OFFSET(lmc_esp0), opts_parse_boolean, NULL, 1 },
	{ "max_op_vls", OPT_OFFSET(max_op_vls), opts_parse_uint8, NULL, 1 },
	{ "force_link_speed", OPT_OFFSET(force_link_speed), opts_parse_uint8, NULL, 1 },
	{ "force_link_speed_ext", OPT_OFFSET(force_link_speed_ext), opts_parse_uint8, NULL, 1 },
	{ "fdr10", OPT_OFFSET(fdr10), opts_parse_uint8, NULL, 1 },
	{ "reassign_lids", OPT_OFFSET(reassign_lids), opts_parse_boolean, NULL, 1 },
	{ "ignore_other_sm", OPT_OFFSET(ignore_other_sm), opts_parse_boolean, NULL, 1 },
	{ "single_thread", OPT_OFFSET(single_thread), opts_parse_boolean, NULL, 0 },
	{ "disable_multicast", OPT_OFFSET(disable_multicast), opts_parse_boolean, NULL, 1 },
	{ "subnet_timeout", OPT_OFFSET(subnet_timeout), opts_parse_uint8, NULL, 1 },
	{ "packet_life_time", OPT_OFFSET(packet_life_time), opts_parse_uint8, NULL, 1 },
	{ "vl_stall_count", OPT_OFFSET(vl_stall_count), opts_parse_uint8, NULL, 1 },
	{ "leaf_vl_stall_count", OPT_OFFSET(leaf_vl_stall_count), opts_parse_uint8, NULL, 1 },
	{ "head_of_queue_lifetime", OPT_OFFSET(head_of_queue_lifetime), opts_parse_uint8, NULL, 1 },
	{ "leaf_head_of_queue_lifetime", OPT_OFFSET(leaf_head_of_queue_lifetime), opts_parse_uint8, NULL, 1 },
	{ "local_phy_errors_threshold", OPT_OFFSET(local_phy_errors_threshold), opts_parse_uint8, NULL, 1 },
	{ "overrun_errors_threshold", OPT_OFFSET(overrun_errors_threshold), opts_parse_uint8, NULL, 1 },
	{ "use_mfttop", OPT_OFFSET(use_mfttop), opts_parse_boolean, NULL, 1},
	{ "sminfo_polling_timeout", OPT_OFFSET(sminfo_polling_timeout), opts_parse_uint32, opts_setup_sminfo_polling_timeout, 1 },
	{ "polling_retry_number", OPT_OFFSET(polling_retry_number), opts_parse_uint32, NULL, 1 },
	{ "force_heavy_sweep", OPT_OFFSET(force_heavy_sweep), opts_parse_boolean, NULL, 1 },
	{ "port_prof_ignore_file", OPT_OFFSET(port_prof_ignore_file), opts_parse_charp, NULL, 0 },
	{ "hop_weights_file", OPT_OFFSET(hop_weights_file), opts_parse_charp, NULL, 0 },
	{ "dimn_ports_file", OPT_OFFSET(port_search_ordering_file), opts_parse_charp, NULL, 0 },
	{ "port_search_ordering_file", OPT_OFFSET(port_search_ordering_file), opts_parse_charp, NULL, 0 },
	{ "port_profile_switch_nodes", OPT_OFFSET(port_profile_switch_nodes), opts_parse_boolean, NULL, 1 },
	{ "sweep_on_trap", OPT_OFFSET(sweep_on_trap), opts_parse_boolean, NULL, 1 },
	{ "routing_engine", OPT_OFFSET(routing_engine_names), opts_parse_charp, NULL, 0 },
	{ "connect_roots", OPT_OFFSET(connect_roots), opts_parse_boolean, NULL, 1 },
	{ "use_ucast_cache", OPT_OFFSET(use_ucast_cache), opts_parse_boolean, NULL, 0 },
	{ "log_file", OPT_OFFSET(log_file), opts_parse_charp, NULL, 0 },
	{ "log_max_size", OPT_OFFSET(log_max_size), opts_parse_uint32, opts_setup_log_max_size, 1 },
	{ "log_flags", OPT_OFFSET(log_flags), opts_parse_uint8, opts_setup_log_flags, 1 },
	{ "force_log_flush", OPT_OFFSET(force_log_flush), opts_parse_boolean, opts_setup_force_log_flush, 1 },
	{ "accum_log_file", OPT_OFFSET(accum_log_file), opts_parse_boolean, opts_setup_accum_log_file, 1 },
	{ "partition_config_file", OPT_OFFSET(partition_config_file), opts_parse_charp, NULL, 0 },
	{ "no_partition_enforcement", OPT_OFFSET(no_partition_enforcement), opts_parse_boolean, NULL, 1 },
	{ "qos", OPT_OFFSET(qos), opts_parse_boolean, NULL, 1 },
	{ "qos_policy_file", OPT_OFFSET(qos_policy_file), opts_parse_charp, NULL, 0 },
	{ "dump_files_dir", OPT_OFFSET(dump_files_dir), opts_parse_charp, NULL, 0 },
	{ "lid_matrix_dump_file", OPT_OFFSET(lid_matrix_dump_file), opts_parse_charp, NULL, 0 },
	{ "lfts_file", OPT_OFFSET(lfts_file), opts_parse_charp, NULL, 0 },
	{ "root_guid_file", OPT_OFFSET(root_guid_file), opts_parse_charp, NULL, 0 },
	{ "cn_guid_file", OPT_OFFSET(cn_guid_file), opts_parse_charp, NULL, 0 },
	{ "io_guid_file", OPT_OFFSET(io_guid_file), opts_parse_charp, NULL, 0 },
	{ "port_shifting", OPT_OFFSET(port_shifting), opts_parse_boolean, NULL, 1 },
	{ "scatter_ports", OPT_OFFSET(scatter_ports), opts_parse_uint32, NULL, 1 },
	{ "max_reverse_hops", OPT_OFFSET(max_reverse_hops), opts_parse_uint16, NULL, 0 },
	{ "ids_guid_file", OPT_OFFSET(ids_guid_file), opts_parse_charp, NULL, 0 },
	{ "guid_routing_order_file", OPT_OFFSET(guid_routing_order_file), opts_parse_charp, NULL, 0 },
	{ "sa_db_file", OPT_OFFSET(sa_db_file), opts_parse_charp, NULL, 0 },
	{ "sa_db_dump", OPT_OFFSET(sa_db_dump), opts_parse_boolean, NULL, 1 },
	{ "torus_config", OPT_OFFSET(torus_conf_file), opts_parse_charp, NULL, 1 },
	{ "do_mesh_analysis", OPT_OFFSET(do_mesh_analysis), opts_parse_boolean, NULL, 1 },
	{ "exit_on_fatal", OPT_OFFSET(exit_on_fatal), opts_parse_boolean, NULL, 1 },
	{ "honor_guid2lid_file", OPT_OFFSET(honor_guid2lid_file), opts_parse_boolean, NULL, 1 },
	{ "daemon", OPT_OFFSET(daemon), opts_parse_boolean, NULL, 0 },
	{ "sm_inactive", OPT_OFFSET(sm_inactive), opts_parse_boolean, NULL, 1 },
	{ "babbling_port_policy", OPT_OFFSET(babbling_port_policy), opts_parse_boolean, NULL, 1 },
	{ "use_optimized_slvl", OPT_OFFSET(use_optimized_slvl), opts_parse_boolean, NULL, 1 },
#ifdef ENABLE_OSM_PERF_MGR
	{ "perfmgr", OPT_OFFSET(perfmgr), opts_parse_boolean, NULL, 0 },
	{ "perfmgr_redir", OPT_OFFSET(perfmgr_redir), opts_parse_boolean, NULL, 0 },
	{ "perfmgr_sweep_time_s", OPT_OFFSET(perfmgr_sweep_time_s), opts_parse_uint16, NULL, 0 },
	{ "perfmgr_max_outstanding_queries", OPT_OFFSET(perfmgr_max_outstanding_queries), opts_parse_uint32, NULL, 0 },
	{ "event_db_dump_file", OPT_OFFSET(event_db_dump_file), opts_parse_charp, NULL, 0 },
#endif				/* ENABLE_OSM_PERF_MGR */
	{ "event_plugin_name", OPT_OFFSET(event_plugin_name), opts_parse_charp, NULL, 0 },
	{ "event_plugin_options", OPT_OFFSET(event_plugin_options), opts_parse_charp, NULL, 0 },
	{ "node_name_map_name", OPT_OFFSET(node_name_map_name), opts_parse_charp, NULL, 0 },
	{ "qos_max_vls", OPT_OFFSET(qos_options.max_vls), opts_parse_uint32, NULL, 1 },
	{ "qos_high_limit", OPT_OFFSET(qos_options.high_limit), opts_parse_int32, NULL, 1 },
	{ "qos_vlarb_high", OPT_OFFSET(qos_options.vlarb_high), opts_parse_charp, NULL, 1 },
	{ "qos_vlarb_low", OPT_OFFSET(qos_options.vlarb_low), opts_parse_charp, NULL, 1 },
	{ "qos_sl2vl", OPT_OFFSET(qos_options.sl2vl), opts_parse_charp, NULL, 1 },
	{ "qos_ca_max_vls", OPT_OFFSET(qos_ca_options.max_vls), opts_parse_uint32, NULL, 1 },
	{ "qos_ca_high_limit", OPT_OFFSET(qos_ca_options.high_limit), opts_parse_int32, NULL, 1 },
	{ "qos_ca_vlarb_high", OPT_OFFSET(qos_ca_options.vlarb_high), opts_parse_charp, NULL, 1 },
	{ "qos_ca_vlarb_low", OPT_OFFSET(qos_ca_options.vlarb_low), opts_parse_charp, NULL, 1 },
	{ "qos_ca_sl2vl", OPT_OFFSET(qos_ca_options.sl2vl), opts_parse_charp, NULL, 1 },
	{ "qos_sw0_max_vls", OPT_OFFSET(qos_sw0_options.max_vls), opts_parse_uint32, NULL, 1 },
	{ "qos_sw0_high_limit", OPT_OFFSET(qos_sw0_options.high_limit), opts_parse_int32, NULL, 1 },
	{ "qos_sw0_vlarb_high", OPT_OFFSET(qos_sw0_options.vlarb_high), opts_parse_charp, NULL, 1 },
	{ "qos_sw0_vlarb_low", OPT_OFFSET(qos_sw0_options.vlarb_low), opts_parse_charp, NULL, 1 },
	{ "qos_sw0_sl2vl", OPT_OFFSET(qos_sw0_options.sl2vl), opts_parse_charp, NULL, 1 },
	{ "qos_swe_max_vls", OPT_OFFSET(qos_swe_options.max_vls), opts_parse_uint32, NULL, 1 },
	{ "qos_swe_high_limit", OPT_OFFSET(qos_swe_options.high_limit), opts_parse_int32, NULL, 1 },
	{ "qos_swe_vlarb_high", OPT_OFFSET(qos_swe_options.vlarb_high), opts_parse_charp, NULL, 1 },
	{ "qos_swe_vlarb_low", OPT_OFFSET(qos_swe_options.vlarb_low), opts_parse_charp, NULL, 1 },
	{ "qos_swe_sl2vl", OPT_OFFSET(qos_swe_options.sl2vl), opts_parse_charp, NULL, 1 },
	{ "qos_rtr_max_vls", OPT_OFFSET(qos_rtr_options.max_vls), opts_parse_uint32, NULL, 1 },
	{ "qos_rtr_high_limit", OPT_OFFSET(qos_rtr_options.high_limit), opts_parse_int32, NULL, 1 },
	{ "qos_rtr_vlarb_high", OPT_OFFSET(qos_rtr_options.vlarb_high), opts_parse_charp, NULL, 1 },
	{ "qos_rtr_vlarb_low", OPT_OFFSET(qos_rtr_options.vlarb_low), opts_parse_charp, NULL, 1 },
	{ "qos_rtr_sl2vl", OPT_OFFSET(qos_rtr_options.sl2vl), opts_parse_charp, NULL, 1 },
	{ "enable_quirks", OPT_OFFSET(enable_quirks), opts_parse_boolean, NULL, 1 },
	{ "no_clients_rereg", OPT_OFFSET(no_clients_rereg), opts_parse_boolean, NULL, 1 },
	{ "prefix_routes_file", OPT_OFFSET(prefix_routes_file), opts_parse_charp, NULL, 0 },
	{ "consolidate_ipv6_snm_req", OPT_OFFSET(consolidate_ipv6_snm_req), opts_parse_boolean, NULL, 1 },
	{ "lash_start_vl", OPT_OFFSET(lash_start_vl), opts_parse_uint8, NULL, 1 },
	{ "sm_sl", OPT_OFFSET(sm_sl), opts_parse_uint8, NULL, 1 },
	{ "log_prefix", OPT_OFFSET(log_prefix), opts_parse_charp, NULL, 1 },
	{0}
};

static int compar_mgids(const void *m1, const void *m2)
{
	return memcmp(m1, m2, sizeof(ib_gid_t));
}

void osm_subn_construct(IN osm_subn_t * p_subn)
{
	memset(p_subn, 0, sizeof(*p_subn));
	cl_ptr_vector_construct(&p_subn->port_lid_tbl);
	cl_qmap_init(&p_subn->sw_guid_tbl);
	cl_qmap_init(&p_subn->node_guid_tbl);
	cl_qmap_init(&p_subn->port_guid_tbl);
	cl_qmap_init(&p_subn->alias_port_guid_tbl);
	cl_qmap_init(&p_subn->sm_guid_tbl);
	cl_qlist_init(&p_subn->sa_sr_list);
	cl_qlist_init(&p_subn->sa_infr_list);
	cl_qlist_init(&p_subn->prefix_routes_list);
	cl_qmap_init(&p_subn->rtr_guid_tbl);
	cl_qmap_init(&p_subn->prtn_pkey_tbl);
	cl_fmap_init(&p_subn->mgrp_mgid_tbl, compar_mgids);
}

static void subn_destroy_qos_options(osm_qos_options_t *opt)
{
	free(opt->vlarb_high);
	free(opt->vlarb_low);
	free(opt->sl2vl);
}

static void subn_opt_destroy(IN osm_subn_opt_t * p_opt)
{
	free(p_opt->console);
	free(p_opt->port_prof_ignore_file);
	free(p_opt->hop_weights_file);
	free(p_opt->port_search_ordering_file);
	free(p_opt->routing_engine_names);
	free(p_opt->log_file);
	free(p_opt->partition_config_file);
	free(p_opt->qos_policy_file);
	free(p_opt->dump_files_dir);
	free(p_opt->lid_matrix_dump_file);
	free(p_opt->lfts_file);
	free(p_opt->root_guid_file);
	free(p_opt->cn_guid_file);
	free(p_opt->io_guid_file);
	free(p_opt->ids_guid_file);
	free(p_opt->guid_routing_order_file);
	free(p_opt->sa_db_file);
	free(p_opt->torus_conf_file);
#ifdef ENABLE_OSM_PERF_MGR
	free(p_opt->event_db_dump_file);
#endif /* ENABLE_OSM_PERF_MGR */
	free(p_opt->event_plugin_name);
	free(p_opt->event_plugin_options);
	free(p_opt->node_name_map_name);
	free(p_opt->prefix_routes_file);
	free(p_opt->log_prefix);
	subn_destroy_qos_options(&p_opt->qos_options);
	subn_destroy_qos_options(&p_opt->qos_ca_options);
	subn_destroy_qos_options(&p_opt->qos_sw0_options);
	subn_destroy_qos_options(&p_opt->qos_swe_options);
	subn_destroy_qos_options(&p_opt->qos_rtr_options);
}

void osm_subn_destroy(IN osm_subn_t * p_subn)
{
	int i;
	osm_node_t *p_node, *p_next_node;
	osm_alias_guid_t *p_alias_guid, *p_next_alias_guid;
	osm_port_t *p_port, *p_next_port;
	osm_switch_t *p_sw, *p_next_sw;
	osm_remote_sm_t *p_rsm, *p_next_rsm;
	osm_prtn_t *p_prtn, *p_next_prtn;
	osm_infr_t *p_infr, *p_next_infr;
	osm_svcr_t *p_svcr, *p_next_svcr;

	/* it might be a good idea to de-allocate all known objects */
	p_next_node = (osm_node_t *) cl_qmap_head(&p_subn->node_guid_tbl);
	while (p_next_node !=
	       (osm_node_t *) cl_qmap_end(&p_subn->node_guid_tbl)) {
		p_node = p_next_node;
		p_next_node = (osm_node_t *) cl_qmap_next(&p_node->map_item);
		osm_node_delete(&p_node);
	}

	p_next_alias_guid = (osm_alias_guid_t *) cl_qmap_head(&p_subn->alias_port_guid_tbl);
	while (p_next_alias_guid !=
	       (osm_alias_guid_t *) cl_qmap_end(&p_subn->alias_port_guid_tbl)) {
		p_alias_guid = p_next_alias_guid;
		p_next_alias_guid = (osm_alias_guid_t *) cl_qmap_next(&p_alias_guid->map_item);
		osm_alias_guid_delete(&p_alias_guid);
	}

	p_next_port = (osm_port_t *) cl_qmap_head(&p_subn->port_guid_tbl);
	while (p_next_port !=
	       (osm_port_t *) cl_qmap_end(&p_subn->port_guid_tbl)) {
		p_port = p_next_port;
		p_next_port = (osm_port_t *) cl_qmap_next(&p_port->map_item);
		osm_port_delete(&p_port);
	}

	p_next_sw = (osm_switch_t *) cl_qmap_head(&p_subn->sw_guid_tbl);
	while (p_next_sw != (osm_switch_t *) cl_qmap_end(&p_subn->sw_guid_tbl)) {
		p_sw = p_next_sw;
		p_next_sw = (osm_switch_t *) cl_qmap_next(&p_sw->map_item);
		osm_switch_delete(&p_sw);
	}

	p_next_rsm = (osm_remote_sm_t *) cl_qmap_head(&p_subn->sm_guid_tbl);
	while (p_next_rsm !=
	       (osm_remote_sm_t *) cl_qmap_end(&p_subn->sm_guid_tbl)) {
		p_rsm = p_next_rsm;
		p_next_rsm = (osm_remote_sm_t *) cl_qmap_next(&p_rsm->map_item);
		free(p_rsm);
	}

	p_next_prtn = (osm_prtn_t *) cl_qmap_head(&p_subn->prtn_pkey_tbl);
	while (p_next_prtn !=
	       (osm_prtn_t *) cl_qmap_end(&p_subn->prtn_pkey_tbl)) {
		p_prtn = p_next_prtn;
		p_next_prtn = (osm_prtn_t *) cl_qmap_next(&p_prtn->map_item);
		osm_prtn_delete(p_subn, &p_prtn);
	}

	cl_fmap_remove_all(&p_subn->mgrp_mgid_tbl);

	for (i = 0; i <= p_subn->max_mcast_lid_ho - IB_LID_MCAST_START_HO;
	     i++)
		if (p_subn->mboxes[i])
			osm_mgrp_box_delete(p_subn->mboxes[i]);

	p_next_infr = (osm_infr_t *) cl_qlist_head(&p_subn->sa_infr_list);
	while (p_next_infr !=
	       (osm_infr_t *) cl_qlist_end(&p_subn->sa_infr_list)) {
		p_infr = p_next_infr;
		p_next_infr = (osm_infr_t *) cl_qlist_next(&p_infr->list_item);
		osm_infr_delete(p_infr);
	}

	p_next_svcr = (osm_svcr_t *) cl_qlist_head(&p_subn->sa_sr_list);
	while (p_next_svcr !=
	       (osm_svcr_t *) cl_qlist_end(&p_subn->sa_sr_list)) {
		p_svcr = p_next_svcr;
		p_next_svcr = (osm_svcr_t *) cl_qlist_next(&p_svcr->list_item);
		osm_svcr_delete(p_svcr);
	}

	cl_ptr_vector_destroy(&p_subn->port_lid_tbl);

	osm_qos_policy_destroy(p_subn->p_qos_policy);

	while (!cl_is_qlist_empty(&p_subn->prefix_routes_list)) {
		cl_list_item_t *item = cl_qlist_remove_head(&p_subn->prefix_routes_list);
		free(item);
	}

	subn_opt_destroy(&p_subn->opt);
	free(p_subn->opt.file_opts);
}

ib_api_status_t osm_subn_init(IN osm_subn_t * p_subn, IN osm_opensm_t * p_osm,
			      IN const osm_subn_opt_t * p_opt)
{
	cl_status_t status;

	p_subn->p_osm = p_osm;

	status = cl_ptr_vector_init(&p_subn->port_lid_tbl,
				    OSM_SUBNET_VECTOR_MIN_SIZE,
				    OSM_SUBNET_VECTOR_GROW_SIZE);
	if (status != CL_SUCCESS)
		return status;

	status = cl_ptr_vector_set_capacity(&p_subn->port_lid_tbl,
					    OSM_SUBNET_VECTOR_CAPACITY);
	if (status != CL_SUCCESS)
		return status;

	/*
	   LID zero is not valid.  NULL out this entry for the
	   convenience of other code.
	 */
	cl_ptr_vector_set(&p_subn->port_lid_tbl, 0, NULL);

	p_subn->opt = *p_opt;
	p_subn->max_ucast_lid_ho = IB_LID_UCAST_END_HO;
	p_subn->max_mcast_lid_ho = IB_LID_MCAST_END_HO;
	p_subn->min_ca_mtu = IB_MAX_MTU;
	p_subn->min_ca_rate = IB_MAX_RATE;
	p_subn->min_data_vls = IB_MAX_NUM_VLS - 1;
	p_subn->ignore_existing_lfts = TRUE;

	/* we assume master by default - so we only need to set it true if STANDBY */
	p_subn->coming_out_of_standby = FALSE;
	p_subn->sweeping_enabled = TRUE;
	p_subn->last_sm_port_state = 1;

	return IB_SUCCESS;
}

osm_port_t *osm_get_port_by_mad_addr(IN osm_log_t * p_log,
				     IN const osm_subn_t * p_subn,
				     IN osm_mad_addr_t * p_mad_addr)
{
	osm_port_t *port = osm_get_port_by_lid(p_subn, p_mad_addr->dest_lid);
	if (!port)
		OSM_LOG(p_log, OSM_LOG_ERROR, "ERR 7504: "
			"Lid is out of range: %u\n",
			cl_ntoh16(p_mad_addr->dest_lid));

	return port;
}

ib_api_status_t osm_get_gid_by_mad_addr(IN osm_log_t * p_log,
					IN const osm_subn_t * p_subn,
					IN osm_mad_addr_t * p_mad_addr,
					OUT ib_gid_t * p_gid)
{
	const osm_port_t *p_port;

	if (p_gid == NULL) {
		OSM_LOG(p_log, OSM_LOG_ERROR, "ERR 7505: "
			"Provided output GID is NULL\n");
		return IB_INVALID_PARAMETER;
	}

	p_port = osm_get_port_by_mad_addr(p_log, p_subn, p_mad_addr);
	if (!p_port)
		return IB_INVALID_PARAMETER;

	p_gid->unicast.interface_id = p_port->p_physp->port_guid;
	p_gid->unicast.prefix = p_subn->opt.subnet_prefix;

	return IB_SUCCESS;
}

osm_physp_t *osm_get_physp_by_mad_addr(IN osm_log_t * p_log,
				       IN const osm_subn_t * p_subn,
				       IN osm_mad_addr_t * p_mad_addr)
{
	osm_port_t *p_port;

	p_port = osm_get_port_by_mad_addr(p_log, p_subn, p_mad_addr);
	if (!p_port)
		return NULL;

	return p_port->p_physp;
}

osm_switch_t *osm_get_switch_by_guid(IN const osm_subn_t * p_subn,
				     IN uint64_t guid)
{
	osm_switch_t *p_switch;

	p_switch = (osm_switch_t *) cl_qmap_get(&(p_subn->sw_guid_tbl), guid);
	if (p_switch == (osm_switch_t *) cl_qmap_end(&(p_subn->sw_guid_tbl)))
		p_switch = NULL;
	return p_switch;
}

osm_node_t *osm_get_node_by_guid(IN osm_subn_t const *p_subn, IN uint64_t guid)
{
	osm_node_t *p_node;

	p_node = (osm_node_t *) cl_qmap_get(&(p_subn->node_guid_tbl), guid);
	if (p_node == (osm_node_t *) cl_qmap_end(&(p_subn->node_guid_tbl)))
		p_node = NULL;
	return p_node;
}

osm_port_t *osm_get_port_by_guid(IN osm_subn_t const *p_subn, IN ib_net64_t guid)
{
	osm_port_t *p_port;

	p_port = (osm_port_t *) cl_qmap_get(&(p_subn->port_guid_tbl), guid);
	if (p_port == (osm_port_t *) cl_qmap_end(&(p_subn->port_guid_tbl)))
		p_port = NULL;
	return p_port;
}

osm_port_t *osm_get_port_by_alias_guid(IN osm_subn_t const *p_subn,
				       IN ib_net64_t guid)
{
	osm_alias_guid_t *p_alias_guid;

	p_alias_guid = (osm_alias_guid_t *) cl_qmap_get(&(p_subn->alias_port_guid_tbl), guid);
	if (p_alias_guid == (osm_alias_guid_t *) cl_qmap_end(&(p_subn->alias_port_guid_tbl)))
		return NULL;
	return p_alias_guid->p_base_port;
}

osm_port_t *osm_get_port_by_lid_ho(IN osm_subn_t const * subn, IN uint16_t lid)
{
	if (lid < cl_ptr_vector_get_size(&subn->port_lid_tbl))
		return cl_ptr_vector_get(&subn->port_lid_tbl, lid);
	return NULL;
}

osm_mgrp_t *osm_get_mgrp_by_mgid(IN osm_subn_t * subn, IN ib_gid_t * mgid)
{
	osm_mgrp_t *mgrp;

	mgrp= (osm_mgrp_t *)cl_fmap_get(&subn->mgrp_mgid_tbl, mgid);
	if (mgrp != (osm_mgrp_t *)cl_fmap_end(&subn->mgrp_mgid_tbl))
		return mgrp;
	return NULL;
}

int is_mlnx_ext_port_info_supported(ib_net16_t devid)
{
	uint16_t devid_ho;

	devid_ho = cl_ntoh16(devid);
	if (devid_ho == 0xc738)
		return 1;
	if (devid_ho >= 0x1003 && devid_ho <= 0x1010)
		return 1;
	return 0;
}

static void subn_init_qos_options(osm_qos_options_t *opt, osm_qos_options_t *f)
{
	opt->max_vls = 0;
	opt->high_limit = -1;
	if (opt->vlarb_high)
		free(opt->vlarb_high);
	opt->vlarb_high = NULL;
	if (opt->vlarb_low)
		free(opt->vlarb_low);
	opt->vlarb_low = NULL;
	if (opt->sl2vl)
		free(opt->sl2vl);
	opt->sl2vl = NULL;
	if (f)
		memcpy(f, opt, sizeof(*f));
}

void osm_subn_set_default_opt(IN osm_subn_opt_t * p_opt)
{
	memset(p_opt, 0, sizeof(osm_subn_opt_t));
	p_opt->guid = 0;
	p_opt->m_key = OSM_DEFAULT_M_KEY;
	p_opt->sm_key = OSM_DEFAULT_SM_KEY;
	p_opt->sa_key = OSM_DEFAULT_SA_KEY;
	p_opt->subnet_prefix = IB_DEFAULT_SUBNET_PREFIX;
	p_opt->m_key_lease_period = 0;
	p_opt->sweep_interval = OSM_DEFAULT_SWEEP_INTERVAL_SECS;
	p_opt->max_wire_smps = OSM_DEFAULT_SMP_MAX_ON_WIRE;
	p_opt->max_wire_smps2 = p_opt->max_wire_smps;
	p_opt->console = strdup(OSM_DEFAULT_CONSOLE);
	p_opt->console_port = OSM_DEFAULT_CONSOLE_PORT;
	p_opt->transaction_timeout = OSM_DEFAULT_TRANS_TIMEOUT_MILLISEC;
	p_opt->transaction_retries = OSM_DEFAULT_RETRY_COUNT;
	p_opt->max_smps_timeout = 1000 * p_opt->transaction_timeout *
				  p_opt->transaction_retries;
	/* by default we will consider waiting for 50x transaction timeout normal */
	p_opt->max_msg_fifo_timeout = 50 * OSM_DEFAULT_TRANS_TIMEOUT_MILLISEC;
	p_opt->sm_priority = OSM_DEFAULT_SM_PRIORITY;
	p_opt->lmc = OSM_DEFAULT_LMC;
	p_opt->lmc_esp0 = FALSE;
	p_opt->max_op_vls = OSM_DEFAULT_MAX_OP_VLS;
	p_opt->force_link_speed = 15;
	p_opt->force_link_speed_ext = 31;
	p_opt->fdr10 = 1;
	p_opt->reassign_lids = FALSE;
	p_opt->ignore_other_sm = FALSE;
	p_opt->single_thread = FALSE;
	p_opt->disable_multicast = FALSE;
	p_opt->force_log_flush = FALSE;
	p_opt->subnet_timeout = OSM_DEFAULT_SUBNET_TIMEOUT;
	p_opt->packet_life_time = OSM_DEFAULT_SWITCH_PACKET_LIFE;
	p_opt->vl_stall_count = OSM_DEFAULT_VL_STALL_COUNT;
	p_opt->leaf_vl_stall_count = OSM_DEFAULT_LEAF_VL_STALL_COUNT;
	p_opt->head_of_queue_lifetime = OSM_DEFAULT_HEAD_OF_QUEUE_LIFE;
	p_opt->leaf_head_of_queue_lifetime =
	    OSM_DEFAULT_LEAF_HEAD_OF_QUEUE_LIFE;
	p_opt->local_phy_errors_threshold = OSM_DEFAULT_ERROR_THRESHOLD;
	p_opt->overrun_errors_threshold = OSM_DEFAULT_ERROR_THRESHOLD;
	p_opt->use_mfttop = TRUE;
	p_opt->sminfo_polling_timeout =
	    OSM_SM_DEFAULT_POLLING_TIMEOUT_MILLISECS;
	p_opt->polling_retry_number = OSM_SM_DEFAULT_POLLING_RETRY_NUMBER;
	p_opt->force_heavy_sweep = FALSE;
	p_opt->log_flags = OSM_LOG_DEFAULT_LEVEL;
	p_opt->honor_guid2lid_file = FALSE;
	p_opt->daemon = FALSE;
	p_opt->sm_inactive = FALSE;
	p_opt->babbling_port_policy = FALSE;
	p_opt->use_optimized_slvl = FALSE;
#ifdef ENABLE_OSM_PERF_MGR
	p_opt->perfmgr = FALSE;
	p_opt->perfmgr_redir = TRUE;
	p_opt->perfmgr_sweep_time_s = OSM_PERFMGR_DEFAULT_SWEEP_TIME_S;
	p_opt->perfmgr_max_outstanding_queries =
	    OSM_PERFMGR_DEFAULT_MAX_OUTSTANDING_QUERIES;
	p_opt->event_db_dump_file = NULL; /* use default */
#endif				/* ENABLE_OSM_PERF_MGR */

	p_opt->event_plugin_name = NULL;
	p_opt->event_plugin_options = NULL;
	p_opt->node_name_map_name = NULL;

	p_opt->dump_files_dir = getenv("OSM_TMP_DIR");
	if (!p_opt->dump_files_dir || !(*p_opt->dump_files_dir))
		p_opt->dump_files_dir = OSM_DEFAULT_TMP_DIR;
	p_opt->dump_files_dir = strdup(p_opt->dump_files_dir);
	p_opt->log_file = strdup(OSM_DEFAULT_LOG_FILE);
	p_opt->log_max_size = 0;
	p_opt->partition_config_file = strdup(OSM_DEFAULT_PARTITION_CONFIG_FILE);
	p_opt->no_partition_enforcement = FALSE;
	p_opt->qos = FALSE;
	p_opt->qos_policy_file = strdup(OSM_DEFAULT_QOS_POLICY_FILE);
	p_opt->accum_log_file = TRUE;
	p_opt->port_prof_ignore_file = NULL;
	p_opt->hop_weights_file = NULL;
	p_opt->port_search_ordering_file = NULL;
	p_opt->port_profile_switch_nodes = FALSE;
	p_opt->sweep_on_trap = TRUE;
	p_opt->use_ucast_cache = FALSE;
	p_opt->routing_engine_names = NULL;
	p_opt->connect_roots = FALSE;
	p_opt->lid_matrix_dump_file = NULL;
	p_opt->lfts_file = NULL;
	p_opt->root_guid_file = NULL;
	p_opt->cn_guid_file = NULL;
	p_opt->io_guid_file = NULL;
	p_opt->port_shifting = FALSE;
	p_opt->scatter_ports = OSM_DEFAULT_SCATTER_PORTS;
	p_opt->max_reverse_hops = 0;
	p_opt->ids_guid_file = NULL;
	p_opt->guid_routing_order_file = NULL;
	p_opt->sa_db_file = NULL;
	p_opt->sa_db_dump = FALSE;
	p_opt->torus_conf_file = strdup(OSM_DEFAULT_TORUS_CONF_FILE);
	p_opt->do_mesh_analysis = FALSE;
	p_opt->exit_on_fatal = TRUE;
	p_opt->enable_quirks = FALSE;
	p_opt->no_clients_rereg = FALSE;
	p_opt->prefix_routes_file = strdup(OSM_DEFAULT_PREFIX_ROUTES_FILE);
	p_opt->consolidate_ipv6_snm_req = FALSE;
	p_opt->lash_start_vl = 0;
	p_opt->sm_sl = OSM_DEFAULT_SL;
	p_opt->log_prefix = NULL;
	subn_init_qos_options(&p_opt->qos_options, NULL);
	subn_init_qos_options(&p_opt->qos_ca_options, NULL);
	subn_init_qos_options(&p_opt->qos_sw0_options, NULL);
	subn_init_qos_options(&p_opt->qos_swe_options, NULL);
	subn_init_qos_options(&p_opt->qos_rtr_options, NULL);
}

static char *clean_val(char *val)
{
	char *p = val;
	/* clean leading spaces */
	while (isspace(*p))
		p++;
	val = p;
	if (!*val)
		return val;
	/* clean trailing spaces */
	p = val + strlen(val) - 1;
	while (p > val && isspace(*p))
		p--;
	p[1] = '\0';
	/* clean quotas */
	if ((*val == '\"' && *p == '\"') || (*val == '\'' && *p == '\'')) {
		val++;
		*p-- = '\0';
	}
	return val;
}

static int subn_dump_qos_options(FILE * file, const char *set_name,
				 const char *prefix, osm_qos_options_t * opt)
{
	return fprintf(file, "# %s\n"
		       "%s_max_vls %u\n"
		       "%s_high_limit %d\n"
		       "%s_vlarb_high %s\n"
		       "%s_vlarb_low %s\n"
		       "%s_sl2vl %s\n",
		       set_name,
		       prefix, opt->max_vls,
		       prefix, opt->high_limit,
		       prefix, opt->vlarb_high,
		       prefix, opt->vlarb_low, prefix, opt->sl2vl);
}

static ib_api_status_t append_prefix_route(IN osm_subn_t * p_subn,
					   uint64_t prefix, uint64_t guid)
{
	osm_prefix_route_t *route;

	route = malloc(sizeof *route);
	if (! route) {
		OSM_LOG(&p_subn->p_osm->log, OSM_LOG_ERROR, "out of memory");
		return IB_ERROR;
	}

	route->prefix = cl_hton64(prefix);
	route->guid = cl_hton64(guid);
	cl_qlist_insert_tail(&p_subn->prefix_routes_list, &route->list_item);
	return IB_SUCCESS;
}

static ib_api_status_t parse_prefix_routes_file(IN osm_subn_t * p_subn)
{
	osm_log_t *log = &p_subn->p_osm->log;
	FILE *fp;
	char buf[1024];
	int line = 0;
	int errors = 0;

	while (!cl_is_qlist_empty(&p_subn->prefix_routes_list)) {
		cl_list_item_t *item = cl_qlist_remove_head(&p_subn->prefix_routes_list);
		free(item);
	}

	fp = fopen(p_subn->opt.prefix_routes_file, "r");
	if (! fp) {
		if (errno == ENOENT)
			return IB_SUCCESS;

		OSM_LOG(log, OSM_LOG_ERROR, "fopen(%s) failed: %s",
			p_subn->opt.prefix_routes_file, strerror(errno));
		return IB_ERROR;
	}

	while (fgets(buf, sizeof buf, fp) != NULL) {
		char *p_prefix, *p_guid, *p_extra, *p_last, *p_end;
		uint64_t prefix, guid;

		line++;
		if (errors > 10)
			break;

		p_prefix = strtok_r(buf, " \t\n", &p_last);
		if (! p_prefix)
			continue; /* ignore blank lines */

		if (*p_prefix == '#')
			continue; /* ignore comment lines */

		p_guid = strtok_r(NULL, " \t\n", &p_last);
		if (! p_guid) {
			OSM_LOG(log, OSM_LOG_ERROR, "%s:%d: missing GUID\n",
				p_subn->opt.prefix_routes_file, line);
			errors++;
			continue;
		}

		p_extra = strtok_r(NULL, " \t\n", &p_last);
		if (p_extra && *p_extra != '#') {
			OSM_LOG(log, OSM_LOG_INFO, "%s:%d: extra tokens ignored\n",
				p_subn->opt.prefix_routes_file, line);
		}

		if (strcmp(p_prefix, "*") == 0)
			prefix = 0;
		else {
			prefix = strtoull(p_prefix, &p_end, 16);
			if (*p_end != '\0') {
				OSM_LOG(log, OSM_LOG_ERROR, "%s:%d: illegal prefix: %s\n",
					p_subn->opt.prefix_routes_file, line, p_prefix);
				errors++;
				continue;
			}
		}

		if (strcmp(p_guid, "*") == 0)
			guid = 0;
		else {
			guid = strtoull(p_guid, &p_end, 16);
			if (*p_end != '\0' && *p_end != '#') {
				OSM_LOG(log, OSM_LOG_ERROR, "%s:%d: illegal GUID: %s\n",
					p_subn->opt.prefix_routes_file, line, p_guid);
				errors++;
				continue;
			}
		}

		if (append_prefix_route(p_subn, prefix, guid) != IB_SUCCESS) {
			errors++;
			break;
		}
	}

	fclose(fp);
	return (errors == 0) ? IB_SUCCESS : IB_ERROR;
}

static void subn_verify_max_vls(unsigned *max_vls, const char *prefix)
{
	if (!*max_vls || *max_vls > 15) {
		if (*max_vls)
			log_report(" Invalid Cached Option: %s_max_vls=%u: "
				   "Using Default = %u\n",
				   prefix, *max_vls, OSM_DEFAULT_QOS_MAX_VLS);
		*max_vls = 0;
	}
}

static void subn_verify_high_limit(int *high_limit, const char *prefix)
{
	if (*high_limit < 0 || *high_limit > 255) {
		if (*high_limit > 255)
			log_report(" Invalid Cached Option: %s_high_limit=%d: "
				   "Using Default: %d\n",
				   prefix, *high_limit,
				   OSM_DEFAULT_QOS_HIGH_LIMIT);
		*high_limit = -1;
	}
}

static void subn_verify_vlarb(char **vlarb, const char *prefix,
			      const char *suffix)
{
	char *str, *tok, *end, *ptr;
	int count = 0;

	if (*vlarb == NULL)
		return;

	str = strdup(*vlarb);

	tok = strtok_r(str, ",\n", &ptr);
	while (tok) {
		char *vl_str, *weight_str;

		vl_str = tok;
		weight_str = strchr(tok, ':');

		if (weight_str) {
			long vl, weight;

			*weight_str = '\0';
			weight_str++;

			vl = strtol(vl_str, &end, 0);

			if (*end)
				log_report(" Warning: Cached Option "
					   "%s_vlarb_%s:vl=%s"
					   " improperly formatted\n",
					   prefix, suffix, vl_str);
			else if (vl < 0 || vl > 14)
				log_report(" Warning: Cached Option "
					   "%s_vlarb_%s:vl=%ld out of range\n",
					   prefix, suffix, vl);

			weight = strtol(weight_str, &end, 0);

			if (*end)
				log_report(" Warning: Cached Option "
					   "%s_vlarb_%s:weight=%s "
					   "improperly formatted\n",
					   prefix, suffix, weight_str);
			else if (weight < 0 || weight > 255)
				log_report(" Warning: Cached Option "
					   "%s_vlarb_%s:weight=%ld "
					   "out of range\n",
					   prefix, suffix, weight);
		} else
			log_report(" Warning: Cached Option "
				   "%s_vlarb_%s:vl:weight=%s "
				   "improperly formatted\n",
				   prefix, suffix, tok);

		count++;
		tok = strtok_r(NULL, ",\n", &ptr);
	}

	if (count > 64)
		log_report(" Warning: Cached Option %s_vlarb_%s: > 64 listed:"
			   " excess vl:weight pairs will be dropped\n",
			   prefix, suffix);

	free(str);
}

static void subn_verify_sl2vl(char **sl2vl, const char *prefix)
{
	char *str, *tok, *end, *ptr;
	int count = 0;

	if (*sl2vl == NULL)
		return;

	str = strdup(*sl2vl);

	tok = strtok_r(str, ",\n", &ptr);
	while (tok) {
		long vl = strtol(tok, &end, 0);

		if (*end)
			log_report(" Warning: Cached Option %s_sl2vl:vl=%s "
				   "improperly formatted\n", prefix, tok);
		else if (vl < 0 || vl > 15)
			log_report(" Warning: Cached Option %s_sl2vl:vl=%ld "
				   "out of range\n", prefix, vl);

		count++;
		tok = strtok_r(NULL, ",\n", &ptr);
	}

	if (count < 16)
		log_report(" Warning: Cached Option %s_sl2vl: < 16 VLs "
			   "listed\n", prefix);
	else if (count > 16)
		log_report(" Warning: Cached Option %s_sl2vl: > 16 listed: "
			   "excess VLs will be dropped\n", prefix);

	free(str);
}

static void subn_verify_qos_set(osm_qos_options_t *set, const char *prefix)
{
	subn_verify_max_vls(&set->max_vls, prefix);
	subn_verify_high_limit(&set->high_limit, prefix);
	subn_verify_vlarb(&set->vlarb_low, prefix, "low");
	subn_verify_vlarb(&set->vlarb_high, prefix, "high");
	subn_verify_sl2vl(&set->sl2vl, prefix);
}

int osm_subn_verify_config(IN osm_subn_opt_t * p_opts)
{
	if (p_opts->lmc > 7) {
		log_report(" Invalid Cached Option Value:lmc = %u:"
			   "Using Default:%u\n", p_opts->lmc, OSM_DEFAULT_LMC);
		p_opts->lmc = OSM_DEFAULT_LMC;
	}

	if (15 < p_opts->sm_priority) {
		log_report(" Invalid Cached Option Value:sm_priority = %u:"
			   "Using Default:%u\n",
			   p_opts->sm_priority, OSM_DEFAULT_SM_PRIORITY);
		p_opts->sm_priority = OSM_DEFAULT_SM_PRIORITY;
	}

	if ((15 < p_opts->force_link_speed) ||
	    (p_opts->force_link_speed > 7 && p_opts->force_link_speed < 15)) {
		log_report(" Invalid Cached Option Value:force_link_speed = %u:"
			   "Using Default:%u\n", p_opts->force_link_speed,
			   IB_PORT_LINK_SPEED_ENABLED_MASK);
		p_opts->force_link_speed = IB_PORT_LINK_SPEED_ENABLED_MASK;
	}

	if ((31 < p_opts->force_link_speed_ext) ||
	    (p_opts->force_link_speed_ext > 3 && p_opts->force_link_speed_ext < 30)) {
		log_report(" Invalid Cached Option Value:force_link_speed_ext = %u:"
			   "Using Default:%u\n", p_opts->force_link_speed_ext,
			   31);
		p_opts->force_link_speed_ext = 31;
	}

	if (2 < p_opts->fdr10) {
		log_report(" Invalid Cached Option Value:fdr10 = %u:"
			   "Using Default:%u\n", p_opts->fdr10, 1);
		p_opts->fdr10 = 1;
	}

	if (p_opts->max_wire_smps == 0)
		p_opts->max_wire_smps = 0x7FFFFFFF;
	else if (p_opts->max_wire_smps > 0x7FFFFFFF) {
		log_report(" Invalid Cached Option Value: max_wire_smps = %u,"
			   " Using Default: %u\n",
			   p_opts->max_wire_smps, OSM_DEFAULT_SMP_MAX_ON_WIRE);
		p_opts->max_wire_smps = OSM_DEFAULT_SMP_MAX_ON_WIRE;
	}

	if (p_opts->max_wire_smps2 > 0x7FFFFFFF) {
		log_report(" Invalid Cached Option Value: max_wire_smps2 = %u,"
			   " Using Default: %u",
			   p_opts->max_wire_smps2, p_opts->max_wire_smps);
		p_opts->max_wire_smps2 = p_opts->max_wire_smps;
	}

	if (strcmp(p_opts->console, OSM_DISABLE_CONSOLE)
	    && strcmp(p_opts->console, OSM_LOCAL_CONSOLE)
#ifdef ENABLE_OSM_CONSOLE_LOOPBACK
	    && strcmp(p_opts->console, OSM_LOOPBACK_CONSOLE)
#endif
#ifdef ENABLE_OSM_CONSOLE_SOCKET
	    && strcmp(p_opts->console, OSM_REMOTE_CONSOLE)
#endif
	    ) {
		log_report(" Invalid Cached Option Value:console = %s"
			   ", Using Default:%s\n",
			   p_opts->console, OSM_DEFAULT_CONSOLE);
		free(p_opts->console);
		p_opts->console = strdup(OSM_DEFAULT_CONSOLE);
	}

	if (p_opts->qos) {
		subn_verify_qos_set(&p_opts->qos_options, "qos");
		subn_verify_qos_set(&p_opts->qos_ca_options, "qos_ca");
		subn_verify_qos_set(&p_opts->qos_sw0_options, "qos_sw0");
		subn_verify_qos_set(&p_opts->qos_swe_options, "qos_swe");
		subn_verify_qos_set(&p_opts->qos_rtr_options, "qos_rtr");
	}

#ifdef ENABLE_OSM_PERF_MGR
	if (p_opts->perfmgr_sweep_time_s < 1) {
		log_report(" Invalid Cached Option Value:perfmgr_sweep_time_s "
			   "= %u Using Default:%u\n",
			   p_opts->perfmgr_sweep_time_s,
			   OSM_PERFMGR_DEFAULT_SWEEP_TIME_S);
		p_opts->perfmgr_sweep_time_s = OSM_PERFMGR_DEFAULT_SWEEP_TIME_S;
	}
	if (p_opts->perfmgr_max_outstanding_queries < 1) {
		log_report(" Invalid Cached Option Value:"
			   "perfmgr_max_outstanding_queries = %u"
			   " Using Default:%u\n",
			   p_opts->perfmgr_max_outstanding_queries,
			   OSM_PERFMGR_DEFAULT_MAX_OUTSTANDING_QUERIES);
		p_opts->perfmgr_max_outstanding_queries =
		    OSM_PERFMGR_DEFAULT_MAX_OUTSTANDING_QUERIES;
	}
#endif

	return 0;
}

int osm_subn_parse_conf_file(char *file_name, osm_subn_opt_t * p_opts)
{
	char line[1024];
	FILE *opts_file;
	char *p_key, *p_val;
	const opt_rec_t *r;
	void *p_field1, *p_field2;

	opts_file = fopen(file_name, "r");
	if (!opts_file) {
		if (errno == ENOENT)
			return 1;
		printf("cannot open file \'%s\': %s\n",
		       file_name, strerror(errno));
		return -1;
	}

	printf(" Reading Cached Option File: %s\n", file_name);
	cl_log_event("OpenSM", CL_LOG_INFO, line, NULL, 0);

	p_opts->config_file = file_name;
	if (!p_opts->file_opts && !(p_opts->file_opts = malloc(sizeof(*p_opts)))) {
		fclose(opts_file);
		return -1;
	}
	memcpy(p_opts->file_opts, p_opts, sizeof(*p_opts));
	p_opts->file_opts->file_opts = NULL;

	while (fgets(line, 1023, opts_file) != NULL) {
		/* get the first token */
		p_key = strtok_r(line, " \t\n", &p_val);
		if (!p_key)
			continue;

		p_val = clean_val(p_val);

		for (r = opt_tbl; r->name; r++) {
			if (strcmp(r->name, p_key))
				continue;

			p_field1 = (void *)p_opts->file_opts + r->opt_offset;
			p_field2 = (void *)p_opts + r->opt_offset;
			/* don't call setup function first time */
			r->parse_fn(NULL, p_key, p_val, p_field1, p_field2,
				    NULL);
			break;
		}
	}
	fclose(opts_file);

	osm_subn_verify_config(p_opts);

	return 0;
}

int osm_subn_rescan_conf_files(IN osm_subn_t * p_subn)
{
	char line[1024];
	osm_subn_opt_t *p_opts = &p_subn->opt;
	const opt_rec_t *r;
	FILE *opts_file;
	char *p_key, *p_val;
	void *p_field1, *p_field2;

	if (!p_opts->config_file)
		return 0;

	opts_file = fopen(p_opts->config_file, "r");
	if (!opts_file) {
		if (errno == ENOENT)
			return 1;
		OSM_LOG(&p_subn->p_osm->log, OSM_LOG_ERROR,
			"cannot open file \'%s\': %s\n",
			p_opts->config_file, strerror(errno));
		return -1;
	}

	subn_init_qos_options(&p_opts->qos_options,
			      &p_opts->file_opts->qos_options);
	subn_init_qos_options(&p_opts->qos_ca_options,
			      &p_opts->file_opts->qos_ca_options);
	subn_init_qos_options(&p_opts->qos_sw0_options,
			      &p_opts->file_opts->qos_sw0_options);
	subn_init_qos_options(&p_opts->qos_swe_options,
			      &p_opts->file_opts->qos_swe_options);
	subn_init_qos_options(&p_opts->qos_rtr_options,
			      &p_opts->file_opts->qos_rtr_options);

	while (fgets(line, 1023, opts_file) != NULL) {
		/* get the first token */
		p_key = strtok_r(line, " \t\n", &p_val);
		if (!p_key)
			continue;

		p_val = clean_val(p_val);

		for (r = opt_tbl; r->name; r++) {
			if (!r->can_update || strcmp(r->name, p_key))
				continue;

			p_field1 = (void *)p_opts->file_opts + r->opt_offset;
			p_field2 = (void *)p_opts + r->opt_offset;
			r->parse_fn(p_subn, p_key, p_val, p_field1, p_field2,
				    r->setup_fn);
			break;
		}
	}
	fclose(opts_file);

	osm_subn_verify_config(p_opts);

	parse_prefix_routes_file(p_subn);

	return 0;
}

int osm_subn_output_conf(FILE *out, IN osm_subn_opt_t * p_opts)
{
	fprintf(out,
		"#\n# DEVICE ATTRIBUTES OPTIONS\n#\n"
		"# The port GUID on which the OpenSM is running\n"
		"guid 0x%016" PRIx64 "\n\n"
		"# M_Key value sent to all ports qualifying all Set(PortInfo)\n"
		"m_key 0x%016" PRIx64 "\n\n"
		"# The lease period used for the M_Key on this subnet in [sec]\n"
		"m_key_lease_period %u\n\n"
		"# SM_Key value of the SM used for SM authentication\n"
		"sm_key 0x%016" PRIx64 "\n\n"
		"# SM_Key value to qualify rcv SA queries as 'trusted'\n"
		"sa_key 0x%016" PRIx64 "\n\n"
		"# Note that for both values above (sm_key and sa_key)\n"
		"# OpenSM version 3.2.1 and below used the default value '1'\n"
		"# in a host byte order, it is fixed now but you may need to\n"
		"# change the values to interoperate with old OpenSM running\n"
		"# on a little endian machine.\n\n"
		"# Subnet prefix used on this subnet\n"
		"subnet_prefix 0x%016" PRIx64 "\n\n"
		"# The LMC value used on this subnet\n"
		"lmc %u\n\n"
		"# lmc_esp0 determines whether LMC value used on subnet is used for\n"
		"# enhanced switch port 0. If TRUE, LMC value for subnet is used for\n"
		"# ESP0. Otherwise, LMC value for ESP0s is 0.\n"
		"lmc_esp0 %s\n\n"
		"# sm_sl determines SMSL used for SM/SA communication\n"
		"sm_sl %u\n\n"
		"# The code of maximal time a packet can live in a switch\n"
		"# The actual time is 4.096usec * 2^<packet_life_time>\n"
		"# The value 0x14 disables this mechanism\n"
		"packet_life_time 0x%02x\n\n"
		"# The number of sequential packets dropped that cause the port\n"
		"# to enter the VLStalled state. The result of setting this value to\n"
		"# zero is undefined.\n"
		"vl_stall_count 0x%02x\n\n"
		"# The number of sequential packets dropped that cause the port\n"
		"# to enter the VLStalled state. This value is for switch ports\n"
		"# driving a CA or router port. The result of setting this value\n"
		"# to zero is undefined.\n"
		"leaf_vl_stall_count 0x%02x\n\n"
		"# The code of maximal time a packet can wait at the head of\n"
		"# transmission queue.\n"
		"# The actual time is 4.096usec * 2^<head_of_queue_lifetime>\n"
		"# The value 0x14 disables this mechanism\n"
		"head_of_queue_lifetime 0x%02x\n\n"
		"# The maximal time a packet can wait at the head of queue on\n"
		"# switch port connected to a CA or router port\n"
		"leaf_head_of_queue_lifetime 0x%02x\n\n"
		"# Limit the maximal operational VLs\n"
		"max_op_vls %u\n\n"
		"# Force PortInfo:LinkSpeedEnabled on switch ports\n"
		"# If 0, don't modify PortInfo:LinkSpeedEnabled on switch port\n"
		"# Otherwise, use value for PortInfo:LinkSpeedEnabled on switch port\n"
		"# Values are (IB Spec 1.2.1, 14.2.5.6 Table 146 \"PortInfo\")\n"
		"#    1: 2.5 Gbps\n"
		"#    3: 2.5 or 5.0 Gbps\n"
		"#    5: 2.5 or 10.0 Gbps\n"
		"#    7: 2.5 or 5.0 or 10.0 Gbps\n"
		"#    2,4,6,8-14 Reserved\n"
		"#    Default 15: set to PortInfo:LinkSpeedSupported\n"
		"force_link_speed %u\n\n"
		"# Force PortInfo:LinkSpeedExtEnabled on ports\n"
		"# If 0, don't modify PortInfo:LinkSpeedExtEnabled on port\n"
		"# Otherwise, use value for PortInfo:LinkSpeedExtEnabled on port\n"
		"# Values are (MgtWG RefID #4722)\n"
		"#    1: 14.0625 Gbps\n"
		"#    2: 25.78125 Gbps\n"
		"#    3: 14.0625 Gbps or 25.78125 Gbps\n"
		"#    30: Disable extended link speeds\n"
		"#    Default 31: set to PortInfo:LinkSpeedExtSupported\n"
		"force_link_speed_ext %u\n\n"
		"# FDR10 on ports on devices that support FDR10\n"
		"# Values are:\n"
		"#    0: don't use fdr10 (no MLNX ExtendedPortInfo MADs)\n"
		"#    Default 1: enable fdr10 when supported\n"
		"#    2: disable fdr10 when supported\n"
		"fdr10 %u\n\n"
		"# The subnet_timeout code that will be set for all the ports\n"
		"# The actual timeout is 4.096usec * 2^<subnet_timeout>\n"
		"subnet_timeout %u\n\n"
		"# Threshold of local phy errors for sending Trap 129\n"
		"local_phy_errors_threshold 0x%02x\n\n"
		"# Threshold of credit overrun errors for sending Trap 130\n"
		"overrun_errors_threshold 0x%02x\n\n"
		"# Use SwitchInfo:MulticastFDBTop if advertised in PortInfo:CapabilityMask\n"
		"use_mfttop %s\n\n",
		cl_ntoh64(p_opts->guid),
		cl_ntoh64(p_opts->m_key),
		cl_ntoh16(p_opts->m_key_lease_period),
		cl_ntoh64(p_opts->sm_key),
		cl_ntoh64(p_opts->sa_key),
		cl_ntoh64(p_opts->subnet_prefix),
		p_opts->lmc,
		p_opts->lmc_esp0 ? "TRUE" : "FALSE",
		p_opts->sm_sl,
		p_opts->packet_life_time,
		p_opts->vl_stall_count,
		p_opts->leaf_vl_stall_count,
		p_opts->head_of_queue_lifetime,
		p_opts->leaf_head_of_queue_lifetime,
		p_opts->max_op_vls,
		p_opts->force_link_speed,
		p_opts->force_link_speed_ext,
		p_opts->fdr10,
		p_opts->subnet_timeout,
		p_opts->local_phy_errors_threshold,
		p_opts->overrun_errors_threshold,
		p_opts->use_mfttop ? "TRUE" : "FALSE");

	fprintf(out,
		"#\n# PARTITIONING OPTIONS\n#\n"
		"# Partition configuration file to be used\n"
		"partition_config_file %s\n\n"
		"# Disable partition enforcement by switches\n"
		"no_partition_enforcement %s\n\n",
		p_opts->partition_config_file,
		p_opts->no_partition_enforcement ? "TRUE" : "FALSE");

	fprintf(out,
		"#\n# SWEEP OPTIONS\n#\n"
		"# The number of seconds between subnet sweeps (0 disables it)\n"
		"sweep_interval %u\n\n"
		"# If TRUE cause all lids to be reassigned\n"
		"reassign_lids %s\n\n"
		"# If TRUE forces every sweep to be a heavy sweep\n"
		"force_heavy_sweep %s\n\n"
		"# If TRUE every trap will cause a heavy sweep.\n"
		"# NOTE: successive identical traps (>10) are suppressed\n"
		"sweep_on_trap %s\n\n",
		p_opts->sweep_interval,
		p_opts->reassign_lids ? "TRUE" : "FALSE",
		p_opts->force_heavy_sweep ? "TRUE" : "FALSE",
		p_opts->sweep_on_trap ? "TRUE" : "FALSE");

	fprintf(out,
		"#\n# ROUTING OPTIONS\n#\n"
		"# If TRUE count switches as link subscriptions\n"
		"port_profile_switch_nodes %s\n\n",
		p_opts->port_profile_switch_nodes ? "TRUE" : "FALSE");

	fprintf(out,
		"# Name of file with port guids to be ignored by port profiling\n"
		"port_prof_ignore_file %s\n\n", p_opts->port_prof_ignore_file ?
		p_opts->port_prof_ignore_file : null_str);

	fprintf(out,
		"# The file holding routing weighting factors per output port\n"
		"hop_weights_file %s\n\n",
		p_opts->hop_weights_file ? p_opts->hop_weights_file : null_str);

	fprintf(out,
		"# The file holding non-default port order per switch for routing\n"
		"port_search_ordering_file %s\n\n",
		p_opts->port_search_ordering_file ?
		p_opts->port_search_ordering_file : null_str);

	fprintf(out,
		"# Routing engine\n"
		"# Multiple routing engines can be specified separated by\n"
		"# commas so that specific ordering of routing algorithms will\n"
		"# be tried if earlier routing engines fail.\n"
		"# Supported engines: minhop, updn, dnup, file, ftree, lash,\n"
		"#    dor, torus-2QoS\n"
		"routing_engine %s\n\n", p_opts->routing_engine_names ?
		p_opts->routing_engine_names : null_str);

	fprintf(out,
		"# Connect roots (use FALSE if unsure)\n"
		"connect_roots %s\n\n",
		p_opts->connect_roots ? "TRUE" : "FALSE");

	fprintf(out,
		"# Use unicast routing cache (use FALSE if unsure)\n"
		"use_ucast_cache %s\n\n",
		p_opts->use_ucast_cache ? "TRUE" : "FALSE");

	fprintf(out,
		"# Lid matrix dump file name\n"
		"lid_matrix_dump_file %s\n\n", p_opts->lid_matrix_dump_file ?
		p_opts->lid_matrix_dump_file : null_str);

	fprintf(out,
		"# LFTs file name\nlfts_file %s\n\n",
		p_opts->lfts_file ? p_opts->lfts_file : null_str);

	fprintf(out,
		"# The file holding the root node guids (for fat-tree or Up/Down)\n"
		"# One guid in each line\nroot_guid_file %s\n\n",
		p_opts->root_guid_file ? p_opts->root_guid_file : null_str);

	fprintf(out,
		"# The file holding the fat-tree compute node guids\n"
		"# One guid in each line\ncn_guid_file %s\n\n",
		p_opts->cn_guid_file ? p_opts->cn_guid_file : null_str);

	fprintf(out,
		"# The file holding the fat-tree I/O node guids\n"
		"# One guid in each line\nio_guid_file %s\n\n",
		p_opts->io_guid_file ? p_opts->io_guid_file : null_str);

	fprintf(out,
		"# Number of reverse hops allowed for I/O nodes \n"
		"# Used for connectivity between I/O nodes connected to Top Switches\nmax_reverse_hops %d\n\n",
		p_opts->max_reverse_hops);

	fprintf(out,
		"# The file holding the node ids which will be used by"
		" Up/Down algorithm instead\n# of GUIDs (one guid and"
		" id in each line)\nids_guid_file %s\n\n",
		p_opts->ids_guid_file ? p_opts->ids_guid_file : null_str);

	fprintf(out,
		"# The file holding guid routing order guids (for MinHop and Up/Down)\n"
		"guid_routing_order_file %s\n\n",
		p_opts->guid_routing_order_file ? p_opts->guid_routing_order_file : null_str);

	fprintf(out,
		"# Do mesh topology analysis (for LASH algorithm)\n"
		"do_mesh_analysis %s\n\n",
		p_opts->do_mesh_analysis ? "TRUE" : "FALSE");

	fprintf(out,
		"# Starting VL for LASH algorithm\n"
		"lash_start_vl %u\n\n",
		p_opts->lash_start_vl);

	fprintf(out,
		"# Port Shifting (use FALSE if unsure)\n"
		"port_shifting %s\n\n",
		p_opts->port_shifting ? "TRUE" : "FALSE");

	fprintf(out,
		"# Assign ports in a random order instead of round-robin.\n"
		"# If zero disable, otherwise use the value as a random seed\n"
		"scatter_ports %d\n\n",
		p_opts->scatter_ports);

	fprintf(out,
		"# SA database file name\nsa_db_file %s\n\n",
		p_opts->sa_db_file ? p_opts->sa_db_file : null_str);

	fprintf(out,
		"# If TRUE causes OpenSM to dump SA database at the end of\n"
		"# every light sweep, regardless of the verbosity level\n"
		"sa_db_dump %s\n\n",
		p_opts->sa_db_dump ? "TRUE" : "FALSE");

	fprintf(out,
		"# Torus-2QoS configuration file name\ntorus_config %s\n\n",
		p_opts->torus_conf_file ? p_opts->torus_conf_file : null_str);

	fprintf(out,
		"#\n# HANDOVER - MULTIPLE SMs OPTIONS\n#\n"
		"# SM priority used for deciding who is the master\n"
		"# Range goes from 0 (lowest priority) to 15 (highest).\n"
		"sm_priority %u\n\n"
		"# If TRUE other SMs on the subnet should be ignored\n"
		"ignore_other_sm %s\n\n"
		"# Timeout in [msec] between two polls of active master SM\n"
		"sminfo_polling_timeout %u\n\n"
		"# Number of failing polls of remote SM that declares it dead\n"
		"polling_retry_number %u\n\n"
		"# If TRUE honor the guid2lid file when coming out of standby\n"
		"# state, if such file exists and is valid\n"
		"honor_guid2lid_file %s\n\n",
		p_opts->sm_priority,
		p_opts->ignore_other_sm ? "TRUE" : "FALSE",
		p_opts->sminfo_polling_timeout,
		p_opts->polling_retry_number,
		p_opts->honor_guid2lid_file ? "TRUE" : "FALSE");

	fprintf(out,
		"#\n# TIMING AND THREADING OPTIONS\n#\n"
		"# Maximum number of SMPs sent in parallel\n"
		"max_wire_smps %u\n\n"
		"# Maximum number of timeout based SMPs allowed to be outstanding\n"
		"# A value less than or equal to max_wire_smps disables this mechanism\n"
		"max_wire_smps2 %u\n\n"
		"# The timeout in [usec] used for sending SMPs above max_wire_smps limit and below max_wire_smps2 limit\n"
		"max_smps_timeout %u\n\n"
		"# The maximum time in [msec] allowed for a transaction to complete\n"
		"transaction_timeout %u\n\n"
		"# The maximum number of retries allowed for a transaction to complete\n"
		"transaction_retries %u\n\n"
		"# Maximal time in [msec] a message can stay in the incoming message queue.\n"
		"# If there is more than one message in the queue and the last message\n"
		"# stayed in the queue more than this value, any SA request will be\n"
		"# immediately be dropped but BUSY status is not currently returned.\n"
		"max_msg_fifo_timeout %u\n\n"
		"# Use a single thread for handling SA queries\n"
		"single_thread %s\n\n",
		p_opts->max_wire_smps,
		p_opts->max_wire_smps2,
		p_opts->max_smps_timeout,
		p_opts->transaction_timeout,
		p_opts->transaction_retries,
		p_opts->max_msg_fifo_timeout,
		p_opts->single_thread ? "TRUE" : "FALSE");

	fprintf(out,
		"#\n# MISC OPTIONS\n#\n"
		"# Daemon mode\n"
		"daemon %s\n\n"
		"# SM Inactive\n"
		"sm_inactive %s\n\n"
		"# Babbling Port Policy\n"
		"babbling_port_policy %s\n\n"
		"# Use Optimized SLtoVLMapping programming if supported by device\n"
		"use_optimized_slvl %s\n\n",
		p_opts->daemon ? "TRUE" : "FALSE",
		p_opts->sm_inactive ? "TRUE" : "FALSE",
		p_opts->babbling_port_policy ? "TRUE" : "FALSE",
		p_opts->use_optimized_slvl ? "TRUE" : "FALSE");

#ifdef ENABLE_OSM_PERF_MGR
	fprintf(out,
		"#\n# Performance Manager Options\n#\n"
		"# perfmgr enable\n"
		"perfmgr %s\n\n"
		"# perfmgr redirection enable\n"
		"perfmgr_redir %s\n\n"
		"# sweep time in seconds\n"
		"perfmgr_sweep_time_s %u\n\n"
		"# Max outstanding queries\n"
		"perfmgr_max_outstanding_queries %u\n\n",
		p_opts->perfmgr ? "TRUE" : "FALSE",
		p_opts->perfmgr_redir ? "TRUE" : "FALSE",
		p_opts->perfmgr_sweep_time_s,
		p_opts->perfmgr_max_outstanding_queries);

	fprintf(out,
		"#\n# Event DB Options\n#\n"
		"# Dump file to dump the events to\n"
		"event_db_dump_file %s\n\n", p_opts->event_db_dump_file ?
		p_opts->event_db_dump_file : null_str);
#endif				/* ENABLE_OSM_PERF_MGR */

	fprintf(out,
		"#\n# Event Plugin Options\n#\n"
		"# Event plugin name(s)\n"
		"event_plugin_name %s\n\n"
		"# Options string that would be passed to the plugin(s)\n"
		"event_plugin_options %s\n\n",
		p_opts->event_plugin_name ?
		p_opts->event_plugin_name : null_str,
		p_opts->event_plugin_options ?
		p_opts->event_plugin_options : null_str);

	fprintf(out,
		"#\n# Node name map for mapping node's to more descriptive node descriptions\n"
		"# (man ibnetdiscover for more information)\n#\n"
		"node_name_map_name %s\n\n", p_opts->node_name_map_name ?
		p_opts->node_name_map_name : null_str);

	fprintf(out,
		"#\n# DEBUG FEATURES\n#\n"
		"# The log flags used\n"
		"log_flags 0x%02x\n\n"
		"# Force flush of the log file after each log message\n"
		"force_log_flush %s\n\n"
		"# Log file to be used\n"
		"log_file %s\n\n"
		"# Limit the size of the log file in MB. If overrun, log is restarted\n"
		"log_max_size %lu\n\n"
		"# If TRUE will accumulate the log over multiple OpenSM sessions\n"
		"accum_log_file %s\n\n"
		"# The directory to hold the file OpenSM dumps\n"
		"dump_files_dir %s\n\n"
		"# If TRUE enables new high risk options and hardware specific quirks\n"
		"enable_quirks %s\n\n"
		"# If TRUE disables client reregistration\n"
		"no_clients_rereg %s\n\n"
		"# If TRUE OpenSM should disable multicast support and\n"
		"# no multicast routing is performed if TRUE\n"
		"disable_multicast %s\n\n"
		"# If TRUE opensm will exit on fatal initialization issues\n"
		"exit_on_fatal %s\n\n" "# console [off|local"
#ifdef ENABLE_OSM_CONSOLE_LOOPBACK
		"|loopback"
#endif
#ifdef ENABLE_OSM_CONSOLE_SOCKET
		"|socket]\n"
#else
		"]\n"
#endif
		"console %s\n\n"
		"# Telnet port for console (default %d)\n"
		"console_port %d\n\n",
		p_opts->log_flags,
		p_opts->force_log_flush ? "TRUE" : "FALSE",
		p_opts->log_file,
		p_opts->log_max_size,
		p_opts->accum_log_file ? "TRUE" : "FALSE",
		p_opts->dump_files_dir,
		p_opts->enable_quirks ? "TRUE" : "FALSE",
		p_opts->no_clients_rereg ? "TRUE" : "FALSE",
		p_opts->disable_multicast ? "TRUE" : "FALSE",
		p_opts->exit_on_fatal ? "TRUE" : "FALSE",
		p_opts->console,
		OSM_DEFAULT_CONSOLE_PORT, p_opts->console_port);

	fprintf(out,
		"#\n# QoS OPTIONS\n#\n"
		"# Enable QoS setup\n"
		"qos %s\n\n"
		"# QoS policy file to be used\n"
		"qos_policy_file %s\n\n",
		p_opts->qos ? "TRUE" : "FALSE", p_opts->qos_policy_file);

	subn_dump_qos_options(out,
			      "QoS default options", "qos",
			      &p_opts->qos_options);
	fprintf(out, "\n");
	subn_dump_qos_options(out,
			      "QoS CA options", "qos_ca",
			      &p_opts->qos_ca_options);
	fprintf(out, "\n");
	subn_dump_qos_options(out,
			      "QoS Switch Port 0 options", "qos_sw0",
			      &p_opts->qos_sw0_options);
	fprintf(out, "\n");
	subn_dump_qos_options(out,
			      "QoS Switch external ports options", "qos_swe",
			      &p_opts->qos_swe_options);
	fprintf(out, "\n");
	subn_dump_qos_options(out,
			      "QoS Router ports options", "qos_rtr",
			      &p_opts->qos_rtr_options);
	fprintf(out, "\n");

	fprintf(out,
		"# Prefix routes file name\n"
		"prefix_routes_file %s\n\n",
		p_opts->prefix_routes_file);

	fprintf(out,
		"#\n# IPv6 Solicited Node Multicast (SNM) Options\n#\n"
		"consolidate_ipv6_snm_req %s\n\n",
		p_opts->consolidate_ipv6_snm_req ? "TRUE" : "FALSE");

	fprintf(out, "# Log prefix\nlog_prefix %s\n\n", p_opts->log_prefix);

	/* optional string attributes ... */

	return 0;
}

int osm_subn_write_conf_file(char *file_name, IN osm_subn_opt_t * p_opts)
{
	FILE *opts_file;

	opts_file = fopen(file_name, "w");
	if (!opts_file) {
		printf("cannot open file \'%s\' for writing: %s\n",
			file_name, strerror(errno));
		return -1;
	}

	if (osm_subn_output_conf(opts_file, p_opts) < 0)
		return -1;

	fclose(opts_file);

	return 0;
}

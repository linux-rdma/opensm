/*
 * Copyright (c) 2004-2006 Voltaire, Inc. All rights reserved.
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
 *    Declaration of OSM QoS Policy data types and functions.
 *
 * Environment:
 *    Linux User Mode
 *
 * Author:
 *    Yevgeny Kliteynik, Mellanox
 */

#ifndef OSM_QOS_POLICY_H
#define OSM_QOS_POLICY_H

#include <iba/ib_types.h>
#include <complib/cl_list.h>
#include <opensm/osm_port.h>
#include <opensm/osm_sa_path_record.h>

#define YYSTYPE char *
#define OSM_QOS_POLICY_MAX_PORTS_ON_SWITCH  128
#define OSM_QOS_POLICY_DEFAULT_LEVEL_NAME   "default"

/***************************************************/

typedef struct _osm_qos_port_group_t {
	char *name;			/* single string (this port group name) */
	char *use;			/* single string (description) */
	cl_list_t port_name_list;	/* list of port names (.../.../...) */
	uint64_t **guid_range_arr;	/* array of guid ranges (pair of 64-bit guids) */
	unsigned guid_range_len;	/* num of guid ranges in the array */
	cl_list_t partition_list;	/* list of partition names */
	boolean_t node_type_ca;
	boolean_t node_type_switch;
	boolean_t node_type_router;
	boolean_t node_type_self;
} osm_qos_port_group_t;

/***************************************************/

typedef struct _osm_qos_vlarb_scope_t {
	cl_list_t group_list;		/* list of group names (strings) */
	cl_list_t across_list;		/* list of 'across' group names (strings) */
	cl_list_t vlarb_high_list;	/* list of num pairs (n:m,...), 32-bit values */
	cl_list_t vlarb_low_list;	/* list of num pairs (n:m,...), 32-bit values */
	uint32_t vl_high_limit;		/* single integer */
	boolean_t vl_high_limit_set;
} osm_qos_vlarb_scope_t;

/***************************************************/

typedef struct _osm_qos_sl2vl_scope_t {
	cl_list_t group_list;		/* list of strings (port group names) */
	boolean_t from[OSM_QOS_POLICY_MAX_PORTS_ON_SWITCH];
	boolean_t to[OSM_QOS_POLICY_MAX_PORTS_ON_SWITCH];
	cl_list_t across_from_list;	/* list of strings (port group names) */
	cl_list_t across_to_list;	/* list of strings (port group names) */
	uint8_t sl2vl_table[16];	/* array of sl2vl values */
	boolean_t sl2vl_table_set;
} osm_qos_sl2vl_scope_t;

/***************************************************/

typedef struct _osm_qos_level_t {
	char *use;
	char *name;
	uint8_t sl;
	boolean_t sl_set;
	uint8_t mtu_limit;
	boolean_t mtu_limit_set;
	uint8_t rate_limit;
	boolean_t rate_limit_set;
	uint8_t pkt_life;
	boolean_t pkt_life_set;
	uint64_t **path_bits_range_arr;	/* array of bit ranges (real values are 32bits) */
	unsigned path_bits_range_len;	/* num of bit ranges in the array */
	uint64_t **pkey_range_arr;	/* array of PKey ranges (real values are 16bits) */
	unsigned pkey_range_len;
} osm_qos_level_t;


/***************************************************/

typedef struct _osm_qos_match_rule_t {
	char *use;
	cl_list_t source_list;			/* list of strings */
	cl_list_t source_group_list;		/* list of pointers to relevant port-group */
	cl_list_t destination_list;		/* list of strings */
	cl_list_t destination_group_list;	/* list of pointers to relevant port-group */
	char *qos_level_name;
	osm_qos_level_t *p_qos_level;
	uint64_t **service_id_range_arr;	/* array of SID ranges (64-bit values) */
	unsigned service_id_range_len;
	uint64_t **qos_class_range_arr;		/* array of QoS Class ranges (real values are 16bits) */
	unsigned qos_class_range_len;
	uint64_t **pkey_range_arr;		/* array of PKey ranges (real values are 16bits) */
	unsigned pkey_range_len;
} osm_qos_match_rule_t;

/***************************************************/

typedef struct _osm_qos_policy_t {
	cl_list_t port_groups;			/* list of osm_qos_port_group_t */
	cl_list_t sl2vl_tables;			/* list of osm_qos_sl2vl_scope_t */
	cl_list_t vlarb_tables;			/* list of osm_qos_vlarb_scope_t */
	cl_list_t qos_levels;			/* list of osm_qos_level_t */
	cl_list_t qos_match_rules;		/* list of osm_qos_match_rule_t */
	osm_qos_level_t *p_default_qos_level;	/* default QoS level */
} osm_qos_policy_t;

/***************************************************/

osm_qos_port_group_t * osm_qos_policy_port_group_create();
void osm_qos_policy_port_group_destroy(osm_qos_port_group_t * p_port_group);

osm_qos_vlarb_scope_t * osm_qos_policy_vlarb_scope_create();
void osm_qos_policy_vlarb_scope_destroy(osm_qos_vlarb_scope_t * p_vlarb_scope);

osm_qos_sl2vl_scope_t * osm_qos_policy_sl2vl_scope_create();
void osm_qos_policy_sl2vl_scope_destroy(osm_qos_sl2vl_scope_t * p_sl2vl_scope);

osm_qos_level_t * osm_qos_policy_qos_level_create();
void osm_qos_policy_qos_level_destroy(osm_qos_level_t * p_qos_level);

boolean_t osm_qos_level_has_pkey(IN const osm_qos_level_t * p_qos_level,
				 IN ib_net16_t pkey);

ib_net16_t osm_qos_level_get_shared_pkey(IN const osm_qos_level_t * p_qos_level,
					 IN const osm_physp_t * p_src_physp,
					 IN const osm_physp_t * p_dest_physp);

osm_qos_match_rule_t * osm_qos_policy_match_rule_create();
void osm_qos_policy_match_rule_destroy(osm_qos_match_rule_t * p_match_rule);

osm_qos_policy_t * osm_qos_policy_create();
void osm_qos_policy_destroy(osm_qos_policy_t * p_qos_policy);
int osm_qos_policy_validate(osm_qos_policy_t * p_qos_policy, osm_log_t * p_log);

void osm_qos_policy_get_qos_level_by_pr(IN const osm_qos_policy_t * p_qos_policy,
					IN const osm_pr_rcv_t * p_rcv,
					IN const ib_path_rec_t * p_pr,
					IN const osm_physp_t * p_src_physp,
					IN const osm_physp_t * p_dest_physp,
					IN ib_net64_t comp_mask,
					OUT osm_qos_level_t ** pp_qos_level);

/***************************************************/

int osm_qos_parse_policy_file(IN osm_subn_t * const p_subn);

/***************************************************/

#endif				/* ifndef OSM_QOS_POLICY_H */

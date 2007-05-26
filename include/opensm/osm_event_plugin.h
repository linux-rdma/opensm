/*
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

#ifndef _OSM_EVENT_PLUGIN_H_
#define _OSM_EVENT_PLUGIN_H_

#include <time.h>
#include <opensm/osm_log.h>
#include <iba/ib_types.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else /* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif /* __cplusplus */

BEGIN_C_DECLS

/****h* OpenSM Event plugin interface
* DESCRIPTION
*       Database interface to record subnet events
*
*       Implementations of this object _MUST_ be thread safe.
*
* AUTHOR
*	Ira Weiny, LLNL
*
*********/

typedef enum {
	OSM_EVENT_PLUGIN_SUCCESS = 0,
	OSM_EVENT_PLUGIN_FAIL
} osm_epi_err_t;

#define OSM_EPI_NODE_NAME_LEN (128)
/** =========================================================================
 * Event types
 */
typedef enum {
	OSM_EVENT_ID_PORT_COUNTER = 0,
	OSM_EVENT_ID_PORT_COUNTER_EXT,
	OSM_EVENT_ID_PORT_SELECT,
	OSM_EVENT_ID_TRAP
} osm_epi_event_id_t;

typedef struct {
	uint64_t   node_guid;
	uint8_t    port_num;
	char       node_name[OSM_EPI_NODE_NAME_LEN];
} osm_epi_node_id_t;

/** =========================================================================
 * Port counter event
 * OSM_EVENT_ID_PORT_COUNTER
 */
typedef struct {
	uint64_t          symbol_err_cnt;
	uint64_t          link_err_recover;
	uint64_t          link_downed;
	uint64_t          rcv_err;
	uint64_t          rcv_rem_phys_err;
	uint64_t          rcv_switch_relay_err;
	uint64_t          xmit_discards;
	uint64_t          xmit_constraint_err;
	uint64_t          rcv_constraint_err;
	uint64_t          link_integrity;
	uint64_t          buffer_overrun;
	uint64_t          vl15_dropped;
	uint64_t          xmit_data;
	uint64_t          rcv_data;
	uint64_t          xmit_pkts;
	uint64_t          rcv_pkts;
	time_t            time_diff_s;
	osm_epi_node_id_t node_id;
} osm_epi_pc_event_t;

/** =========================================================================
 * Port counter extended event
 */
typedef struct {
	uint64_t          xmit_data;
	uint64_t          rcv_data;
	uint64_t          xmit_pkts;
	uint64_t          rcv_pkts;
	uint64_t          unicast_xmit_pkts;
	uint64_t          unicast_rcv_pkts;
	uint64_t          multicast_xmit_pkts;
	uint64_t          multicast_rcv_pkts;
	time_t            time_diff_s;
	osm_epi_node_id_t node_id;
} osm_epi_pc_ext_event_t;

/** =========================================================================
 * Port select event
 */
typedef struct {
	uint64_t          xmit_data;
	uint64_t          rcv_data;
	uint64_t          xmit_pkts;
	uint64_t          rcv_pkts;
	uint64_t          xmit_wait;
	time_t            time_diff_s;
	osm_epi_node_id_t node_id;
} osm_epi_ps_event_t;

/** =========================================================================
 * Trap events
 */
typedef struct {
	uint8_t           type;
	uint32_t          prod_type;
	uint16_t          trap_num;
	uint16_t          issuer_lid;
	time_t            time;
	osm_epi_node_id_t node_id;
} osm_epi_trap_event_t;

/** =========================================================================
 * Plugin creators should allocate an object of this type
 *    (name osm_event_plugin)
 * The version should be set to OSM_EVENT_PLUGIN_INTERFACE_VER
 */
#define OSM_EVENT_PLUGIN_INTERFACE_VER (1)
typedef struct
{
	int            interface_version;
	void          *(*construct)(osm_log_t *osm_log);
	void           (*destroy)(void *db);

	osm_epi_err_t  (*report)(void *db, osm_epi_event_id_t id, void *data);

} __osm_epi_plugin_t;

/** =========================================================================
 * The database structure should be considered opaque
 */
typedef struct {
	void                *handle;
	__osm_epi_plugin_t  *db_impl;
	void                *db_data;
	osm_log_t           *p_log;
} osm_epi_plugin_t;


/**
 * functions
 */
osm_epi_plugin_t *osm_epi_construct(osm_log_t *p_log, char *type);
void              osm_epi_destroy(osm_epi_plugin_t *db);
osm_epi_err_t     osm_epi_report(void *db, osm_epi_event_id_t id, void *data);

/** =========================================================================
 * helper functions to fill in the various db objects from wire objects
 */

void osm_epi_fill_pc_event(ib_port_counters_t *wire_read,
				osm_epi_pc_event_t *event);
void osm_epi_fill_pc_ext_event(ib_port_counters_t *wire_read,
				osm_epi_pc_ext_event_t *event);
void osm_epi_fill_ps_event(ib_port_counters_ext_t *wire_read,
				osm_epi_ps_event_t *event);


END_C_DECLS

#endif		/* _OSM_EVENT_PLUGIN_H_ */


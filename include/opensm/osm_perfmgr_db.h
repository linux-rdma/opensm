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

#ifndef _PERFMGR_EVENT_DB_H_
#define _PERFMGR_EVENT_DB_H_

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

/****h* OpenSM/PerfMgr Event Database
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
	PERFMGR_EVENT_DB_SUCCESS = 0,
	PERFMGR_EVENT_DB_FAIL,
	PERFMGR_EVENT_DB_NOMEM,
	PERFMGR_EVENT_DB_GUIDNOTFOUND,
	PERFMGR_EVENT_DB_PORTNOTFOUND,
	PERFMGR_EVENT_DB_NOT_IMPL
} perfmgr_db_err_t;

/** =========================================================================
 * Port error reading
 */
typedef struct {
	uint64_t   symbol_err_cnt;
	uint64_t   link_err_recover;
	uint64_t   link_downed;
	uint64_t   rcv_err;
	uint64_t   rcv_rem_phys_err;
	uint64_t   rcv_switch_relay_err;
	uint64_t   xmit_discards;
	uint64_t   xmit_constraint_err;
	uint64_t   rcv_constraint_err;
	uint64_t   link_integrity;
	uint64_t   buffer_overrun;
	uint64_t   vl15_dropped;
	time_t     time;
} perfmgr_db_err_reading_t;

/** =========================================================================
 * Port data count reading
 */
typedef struct {
	uint64_t   xmit_data; /* can be used for std or extended */
	uint64_t   rcv_data;  /* can be used for std or extended */
	uint64_t   xmit_pkts; /* can be used for std or extended */
	uint64_t   rcv_pkts;  /* can be used for std or extended */
	uint64_t   unicast_xmit_pkts;
	uint64_t   unicast_rcv_pkts;
	uint64_t   multicast_xmit_pkts;
	uint64_t   multicast_rcv_pkts;
	time_t     time;
} perfmgr_db_data_cnt_reading_t;

/** =========================================================================
 * Port select errors
 */
typedef struct {
	uint64_t   xmit_wait;
	time_t     time;
} perfmgr_db_ps_reading_t;

/** =========================================================================
 * Trap readings
 */
typedef struct {
	uint8_t    type;
	uint32_t   prod_type;
	uint16_t   trap_num;
	uint16_t   issuer_lid;
	time_t     time;
} perfmgr_db_trap_reading_t;

/** =========================================================================
 * Dump output options
 */
typedef enum {
	PERFMGR_EVENT_DB_DUMP_HR = 0, /* Human readable */
	PERFMGR_EVENT_DB_DUMP_MR      /* Machine readable */
} perfmgr_db_dump_t;

/** =========================================================================
 * Plugin creators should allocate an object of this type
 *    (name perfmgr_event_db)
 * The version should be set to PERFMGR_EVENT_DB_INTERFACE_VER
 */
#define PERFMGR_EVENT_DB_INTERFACE_VER (1)
typedef struct
{
	int                interface_version;
	void              *(*construct)(osm_log_t *osm_log);
	void               (*destroy)(void *db);

	perfmgr_db_err_t  (*create_entry)(void *db, uint64_t guid,
					uint8_t num_ports, char *name);

	/* perfmgr_db_err_reading_t functions */
	perfmgr_db_err_t  (*add_err_reading)(void *db, uint64_t guid,
				uint8_t port, perfmgr_db_err_reading_t *reading);
	perfmgr_db_err_t  (*get_prev_err_reading)(void *db, uint64_t guid,
				uint8_t port, perfmgr_db_err_reading_t *reading);
	perfmgr_db_err_t  (*clear_prev_err)(void *db, uint64_t guid, uint8_t port);

	/* perfmgr_db_data_cnt_reading_t functions */
	perfmgr_db_err_t  (*add_dc_reading)(void *db, uint64_t guid,
				uint8_t port, perfmgr_db_data_cnt_reading_t *reading);
	perfmgr_db_err_t  (*get_prev_dc_reading)(void *db, uint64_t guid,
				uint8_t port, perfmgr_db_data_cnt_reading_t *reading);
	perfmgr_db_err_t  (*clear_prev_dc)(void *db, uint64_t guid, uint8_t port);

	void               (*clear_counters)(void *db);
	perfmgr_db_err_t  (*dump)(void *db, char *file, perfmgr_db_dump_t dump_type);
} __perfmgr_event_db_t;

/** =========================================================================
 * The database structure which should be considered opaque
 */
typedef struct {
	void                 *handle;
	__perfmgr_event_db_t *db_impl;
	void                 *db_data;
	osm_log_t            *p_log;
} perfmgr_event_db_t;


/**
 * functions
 */
perfmgr_event_db_t *perfmgr_db_construct(osm_log_t *p_log, char *type);
void                perfmgr_db_destroy(perfmgr_event_db_t *db);

perfmgr_db_err_t   perfmgr_db_create_entry(perfmgr_event_db_t *db, uint64_t guid,
					uint8_t num_ports, char *node_name);

perfmgr_db_err_t   perfmgr_db_add_err_reading(perfmgr_event_db_t *db, uint64_t guid,
					uint8_t port, perfmgr_db_err_reading_t *reading);
perfmgr_db_err_t   perfmgr_db_get_prev_err(perfmgr_event_db_t *db,
					uint64_t guid, uint8_t port,
					perfmgr_db_err_reading_t *reading);
perfmgr_db_err_t   perfmgr_db_clear_prev_err(perfmgr_event_db_t *db, uint64_t guid,
					uint8_t port);

perfmgr_db_err_t   perfmgr_db_add_dc_reading(perfmgr_event_db_t *db, uint64_t guid,
					uint8_t port, perfmgr_db_data_cnt_reading_t *reading);
perfmgr_db_err_t   perfmgr_db_get_prev_dc(perfmgr_event_db_t *db,
					uint64_t guid, uint8_t port,
					perfmgr_db_data_cnt_reading_t *reading);
perfmgr_db_err_t   perfmgr_db_clear_prev_dc(perfmgr_event_db_t *db, uint64_t guid,
					uint8_t port);

void                perfmgr_db_clear_counters(perfmgr_event_db_t *db);
perfmgr_db_err_t   perfmgr_db_dump(perfmgr_event_db_t *db, char *file,
					perfmgr_db_dump_t dump_type);

/** =========================================================================
 * helper functions to fill in the various db objects from wire objects
 */

void perfmgr_db_fill_err_read(ib_port_counters_t *wire_read,
				perfmgr_db_err_reading_t *reading);
void perfmgr_db_fill_data_cnt_read_pc(ib_port_counters_t *wire_read,
				perfmgr_db_data_cnt_reading_t *reading);
void perfmgr_db_fill_data_cnt_read_epc(ib_port_counters_ext_t *wire_read,
				perfmgr_db_data_cnt_reading_t *reading);

END_C_DECLS

#endif		/* _PERFMGR_PM_DB_H_ */


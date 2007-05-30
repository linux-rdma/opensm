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

#ifndef _OSM_PERFMGR_H_
#define _OSM_PERFMGR_H_

#if HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#ifdef ENABLE_OSM_PERF_MGR

#include <iba/ib_types.h>
#include <complib/cl_passivelock.h>
#include <complib/cl_event.h>
#include <complib/cl_thread.h>
#include <opensm/osm_subnet.h>
#include <opensm/osm_req.h>
#include <opensm/osm_log.h>
#include <opensm/osm_event_db.h>
#include <opensm/osm_sm.h>
#include <opensm/osm_base.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/****h* OpenSM/PerfMgr
* NAME
*	PerfMgr
*
* DESCRIPTION
*       Performance manager thread which takes care of polling the fabric for
*       Port counters values.
*
*	The PerfMgr object is thread safe.
*
* AUTHOR
*	Ira Weiny, LLNL
*
*********/

#define OSM_PERFMGR_DEFAULT_SWEEP_TIME_S 180
#define OSM_PERFMGR_DEFAULT_DUMP_FILE OSM_DEFAULT_TMP_DIR "/opensm_port_counters.log"
#define OSM_DEFAULT_EVENT_PLUGIN "ibeventdb"

/****s* OpenSM: PerfMgr/osm_perfmgr_state_t */
typedef enum
{
  PERFMGR_STATE_DISABLE,
  PERFMGR_STATE_ENABLED,
  PERFMGR_STATE_NO_DB
} osm_perfmgr_state_t;

/****s* OpenSM: PerfMgr/osm_perfmgr_state_t */
typedef enum
{
  PERFMGR_SWEEP_SLEEP,
  PERFMGR_SWEEP_ACTIVE,
  PERFMGR_SWEEP_SUSPENDED
} osm_perfmgr_sweep_state_t;

#define PERFMGR_MAX_OUTSTANDING_QUERIES 500

/* Node to store information about which nodes we are monitoring */
typedef struct _monitored_node {
	cl_map_item_t	        map_item;
	struct _monitored_node *next;
	uint64_t                guid;
} __monitored_node_t;

/****s* OpenSM: PerfMgr/osm_perfmgr_t
*  This object should be treated as opaque and should
*  be manipulated only through the provided functions.
*/
typedef struct _osm_perfmgr
{
  osm_thread_state_t    thread_state;
  cl_event_t            sig_sweep;
  cl_thread_t           sweeper;
  osm_subn_t           *subn;
  osm_sm_t             *sm;
  cl_plock_t           *lock;
  osm_log_t            *log;
  osm_mad_pool_t       *mad_pool;
  atomic32_t            trans_id;
  osm_vendor_t         *vendor;
  osm_bind_handle_t     bind_handle;
  cl_disp_reg_handle_t  pc_disp_h;
  osm_perfmgr_state_t   state;
  osm_perfmgr_sweep_state_t  sweep_state;
  uint16_t              sweep_time_s;
  char                 *db_file;
  char                 *event_db_dump_file;
  char                 *event_db_plugin;
  perfmgr_event_db_t   *db;
  atomic32_t            outstanding_queries; /* this along with sig_query */
  cl_event_t            sig_query;           /* will throttle our querys */
  cl_qmap_t             monitored_map;       /* map the nodes we are tracking */
  __monitored_node_t   *remove_list;
} osm_perfmgr_t;
/*
* FIELDS
*	subn
*	      Subnet object for this subnet.
*
*	log
*	      Pointer to the log object.
*
*	mad_pool
*		Pointer to the MAD pool.
*
*       event_db_dump_file
*               File to be used to dump the Port Counters
*
*	mad_ctrl
*		Mad Controller
*********/

/****f* OpenSM: Creation Functions */
void osm_perfmgr_shutdown(osm_perfmgr_t *const p_perfmgr );
void osm_perfmgr_destroy(osm_perfmgr_t * const p_perfmgr );

/****f* OpenSM: Inline accessor functions */
inline static void osm_perfmgr_set_state(osm_perfmgr_t *p_perfmgr,
		osm_perfmgr_state_t state)
{
	p_perfmgr->state = state;
}
inline static osm_perfmgr_state_t osm_perfmgr_get_state(osm_perfmgr_t
		*p_perfmgr) { return (p_perfmgr->state); }
inline static char *osm_perfmgr_get_state_str(osm_perfmgr_t *p_perfmgr)
{
	switch (p_perfmgr->state)
	{
		case PERFMGR_STATE_DISABLE:
			return ("Disabled");
			break;
		case PERFMGR_STATE_ENABLED:
			return ("Enabled");
			break;
		case PERFMGR_STATE_NO_DB:
			return ("No Database");
			break;
	}
	return ("UNKNOWN");
}
inline static char *osm_perfmgr_get_sweep_state_str(osm_perfmgr_t *perfmgr)
{
	switch (perfmgr->sweep_state)
	{
		case PERFMGR_SWEEP_SLEEP:
			return ("Sleeping");
			break;
		case PERFMGR_SWEEP_ACTIVE:
			return ("Active");
			break;
		case PERFMGR_SWEEP_SUSPENDED:
			return ("Suspended");
			break;
	}
	return ("UNKNOWN");
}
inline static void osm_perfmgr_set_sweep_time_s(osm_perfmgr_t *p_perfmgr, uint16_t time_s)
{
	p_perfmgr->sweep_time_s = time_s;
	cl_event_signal(&p_perfmgr->sig_sweep);
}
inline static uint16_t osm_perfmgr_get_sweep_time_s(osm_perfmgr_t *p_perfmgr)
{
	return (p_perfmgr->sweep_time_s);
}
void osm_perfmgr_clear_counters(osm_perfmgr_t *p_perfmgr);
void osm_perfmgr_dump_counters(osm_perfmgr_t *p_perfmgr, perfmgr_edb_dump_t dump_type);

ib_api_status_t osm_perfmgr_bind(osm_perfmgr_t * const p_perfmgr, const ib_net64_t port_guid);

#if 0
/* Work out the tracking of notice events */
ib_api_status_t osm_report_notice_to_perfmgr(osm_log_t *const p_log, osm_subn_t *p_subn,
					ib_mad_notice_attr_t *p_ntc )
#endif

/****f* OpenSM: PerfMgr/osm_perfmgr_init */
ib_api_status_t
osm_perfmgr_init(
	osm_perfmgr_t* const perfmgr,
	osm_subn_t* const subn,
	osm_sm_t * const sm,
	osm_log_t* const log,
	osm_mad_pool_t * const mad_pool,
	osm_vendor_t * const vendor,
	cl_dispatcher_t* const disp,
	cl_plock_t* const lock,
	const osm_subn_opt_t * const p_opt);
/*
* PARAMETERS
*	perfmgr
*		[in] Pointer to an osm_perfmgr_t object to initialize.
*
*	subn
*		[in] Pointer to the Subnet object for this subnet.
*
*	sm
*		[in] Pointer to the Subnet object for this subnet.
*
*	log
*		[in] Pointer to the log object.
*
*	mad_pool
*		[in] Pointer to the MAD pool.
*
*	vendor
*		[in] Pointer to the vendor specific interfaces object.
*
*	disp
*		[in] Pointer to the OpenSM central Dispatcher.
*
*	lock
*		[in] Pointer to the OpenSM serializing lock.
*
*	p_opt
*		[in] Starting options
*
* RETURN VALUES
*	IB_SUCCESS if the PerfMgr object was initialized successfully.
*********/

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* ENABLE_OSM_PERF_MGR */

#endif /* _OSM_PERFMGR_H_ */

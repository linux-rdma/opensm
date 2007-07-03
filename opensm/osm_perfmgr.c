/*
 * Copyright (c) 2007 The Regents of the University of California.
 * Copyright (c) 2007 Voltaire, Inc. All rights reserved. 
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
 *    Implementation of osm_perfmgr_t.
 * This object implements an IBA performance manager.
 *
 * Author:
 *    Ira Weiny, LLNL
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#ifdef ENABLE_OSM_PERF_MGR

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <poll.h>
#include <netinet/in.h>
#include <complib/cl_debug.h>
#include <iba/ib_types.h>
#include <errno.h>
#include <sys/time.h>
#include <opensm/osm_perfmgr.h>
#include <opensm/osm_log.h>
#include <opensm/osm_node.h>
#include <complib/cl_thread.h>
#include <vendor/osm_vendor_api.h>

#define OSM_PERFMGR_INITIAL_TID_VALUE 0xcafe

/**********************************************************************
 * Internal helper functions.
 **********************************************************************/
static inline void
__init_monitored_nodes(osm_perfmgr_t *pm)
{
	cl_qmap_init(&pm->monitored_map);
	pm->remove_list = NULL;
	cl_event_construct(&pm->sig_query);
	cl_event_init(&pm->sig_query, FALSE);
}

static inline void
__mark_for_removal(osm_perfmgr_t *pm, __monitored_node_t *node)
{
	if (pm->remove_list) {
		node->next = pm->remove_list;
		pm->remove_list = node;
	} else {
		node->next = NULL;
		pm->remove_list = node;
	}
}

static inline void
__remove_marked_nodes(osm_perfmgr_t *pm)
{
	while (pm->remove_list) {
		__monitored_node_t *next = pm->remove_list->next;
		cl_qmap_remove_item(&(pm->monitored_map),
				(cl_map_item_t *)(pm->remove_list));
		free(pm->remove_list);
		pm->remove_list = next;
	}
}

static inline void
__decrement_outstanding_queries(osm_perfmgr_t *pm)
{
	cl_atomic_dec(&(pm->outstanding_queries));
	cl_event_signal(&(pm->sig_query));
}

/**********************************************************************
 * Receive the MAD from the vendor layer and post it for processing by
 * the dispatcher.
 **********************************************************************/
static void
osm_perfmgr_mad_recv_callback(osm_madw_t *p_madw, void* bind_context,
			      osm_madw_t *p_req_madw )
{
	osm_perfmgr_t      *pm = (osm_perfmgr_t *)bind_context;

	OSM_LOG_ENTER( pm->log, osm_pm_mad_recv_callback );

	osm_madw_copy_context( p_madw, p_req_madw );
	osm_mad_pool_put( pm->mad_pool, p_req_madw );

	__decrement_outstanding_queries(pm);

	/* post this message for later processing. */
	if (cl_disp_post(pm->pc_disp_h, OSM_MSG_MAD_PORT_COUNTERS,
			(void *)p_madw, NULL, NULL) != CL_SUCCESS) {
		osm_log(pm->log, OSM_LOG_ERROR,
			"osm_perfmgr_mad_recv_callback: ERR 4C01: "
			"PerfMgr Dispatcher post failed\n");
		osm_mad_pool_put(pm->mad_pool, p_madw);
	}
#if 0
	do {
		struct timeval      rcv_time;
		gettimeofday(&rcv_time, NULL);
		osm_log(pm->log, OSM_LOG_INFO,
			"perfmgr rcv time %ld\n",
			rcv_time.tv_usec -
			p_madw->context.perfmgr_context.query_start.tv_usec);
	} while (0);
#endif
	OSM_LOG_EXIT( pm->log );
}

/**********************************************************************
 * Process MAD send errors.
 **********************************************************************/
static void
osm_perfmgr_mad_send_err_callback(void* bind_context, osm_madw_t *p_madw)
{
	osm_perfmgr_t *pm = (osm_perfmgr_t *)bind_context;
	osm_madw_context_t *context = &(p_madw->context);

	OSM_LOG_ENTER( pm->log, osm_pm_mad_send_err_callback );

	osm_log( pm->log, OSM_LOG_ERROR,
		"osm_pm_mad_send_err_callback: ERR 4C02: 0x%" PRIx64 " port %d\n",
		context->perfmgr_context.node_guid,
		context->perfmgr_context.port);

	osm_mad_pool_put( pm->mad_pool, p_madw );

	__decrement_outstanding_queries(pm);

	OSM_LOG_EXIT( pm->log );
}

/**********************************************************************
 * Bind the PM to the vendor layer for MAD sends/receives
 **********************************************************************/
ib_api_status_t
osm_perfmgr_bind(osm_perfmgr_t * const pm, const ib_net64_t port_guid)
{
	osm_bind_info_t bind_info;
	ib_api_status_t status = IB_SUCCESS;

	OSM_LOG_ENTER( pm->log, osm_pm_bind );

	if( pm->bind_handle != OSM_BIND_INVALID_HANDLE ) {
		osm_log( pm->log, OSM_LOG_ERROR,
		         "osm_pm_mad_ctrl_bind: ERR 4C03: Multiple binds not allowed\n" );
		status = IB_ERROR;
		goto Exit;
	}

	bind_info.port_guid = port_guid;
	bind_info.mad_class = IB_MCLASS_PERF;
	bind_info.class_version = 1;
	bind_info.is_responder = FALSE;
	bind_info.is_report_processor = FALSE;
	bind_info.is_trap_processor = FALSE;
	bind_info.recv_q_size = OSM_PM_DEFAULT_QP1_RCV_SIZE;
	bind_info.send_q_size = OSM_PM_DEFAULT_QP1_SEND_SIZE;

	osm_log( pm->log, OSM_LOG_VERBOSE,
	         "osm_pm_mad_bind: "
	         "Binding to port GUID 0x%" PRIx64 "\n",
	         cl_ntoh64( port_guid ) );

	pm->bind_handle = osm_vendor_bind( pm->vendor,
	                                  &bind_info,
	                                   pm->mad_pool,
	                                   osm_perfmgr_mad_recv_callback,
	                                   osm_perfmgr_mad_send_err_callback,
	                                   pm );

	if( pm->bind_handle == OSM_BIND_INVALID_HANDLE ) {
		status = IB_ERROR;
		osm_log( pm->log, OSM_LOG_ERROR,
		         "osm_pm_mad_bind: ERR 4C04: Vendor specific bind failed (%s)\n",
		         ib_get_err_str(status) );
		goto Exit;
	}

Exit:
	OSM_LOG_EXIT( pm->log );
	return( status );
}

/**********************************************************************
 * Unbind the PM from the vendor layer for MAD sends/receives
 **********************************************************************/
static void
osm_perfmgr_mad_unbind(osm_perfmgr_t * const pm)
{
	OSM_LOG_ENTER( pm->log, osm_sa_mad_ctrl_unbind );
	if( pm->bind_handle == OSM_BIND_INVALID_HANDLE ) {
		osm_log( pm->log, OSM_LOG_ERROR,
		         "osm_pm_mad_unbind: ERR 4C05: No previous bind\n" );
		goto Exit;
	}
	osm_vendor_unbind( pm->bind_handle );
Exit:
	OSM_LOG_EXIT( pm->log );
}

/**********************************************************************
 * Given a node and a port return the appropriate lid to query that port
 **********************************************************************/
static ib_net16_t
get_lid(osm_node_t *p_node, uint8_t port)
{
	ib_net16_t lid = 0;

	switch (p_node->node_info.node_type)
	{
		case IB_NODE_TYPE_CA:
		case IB_NODE_TYPE_ROUTER:
			  lid = osm_node_get_base_lid(p_node, port);
			  break;
		case IB_NODE_TYPE_SWITCH:
			  lid = osm_node_get_base_lid(p_node, 0);
			  break;
		default:
			  break;
	}
	return (lid);
}

/**********************************************************************
 * Form the Port Counter MAD for a single port and send the MAD.
 **********************************************************************/
static ib_api_status_t
osm_perfmgr_send_pc_mad(osm_perfmgr_t *perfmgr, ib_net16_t dest_lid, uint8_t port,
			uint8_t mad_method, osm_madw_context_t* const p_context )
{
	ib_api_status_t     status = IB_SUCCESS;
	ib_port_counters_t *port_counter = NULL;
	ib_perfmgr_mad_t   *pm_mad = NULL;
	osm_madw_t         *p_madw = NULL;

	OSM_LOG_ENTER(perfmgr->log, osm_perfmgr_send_pc_mad);

	p_madw = osm_mad_pool_get(perfmgr->mad_pool, perfmgr->bind_handle, MAD_BLOCK_SIZE, NULL);
	if (p_madw == NULL)
		return (IB_INSUFFICIENT_MEMORY);

	pm_mad = osm_madw_get_perfmgr_mad_ptr(p_madw);

	/* build the mad */
	pm_mad->header.base_ver = 1;
	pm_mad->header.mgmt_class = IB_MCLASS_PERF;
	pm_mad->header.class_ver = 1;
	pm_mad->header.method = mad_method;
	pm_mad->header.status = 0;
	pm_mad->header.class_spec = 0;
	pm_mad->header.trans_id = cl_hton64((uint64_t)cl_atomic_inc(&(perfmgr->trans_id)));
	pm_mad->header.attr_id = IB_MAD_ATTR_PORT_CNTRS;
	pm_mad->header.resv = 0;
	pm_mad->header.attr_mod = 0;

	port_counter = (ib_port_counters_t *)&(pm_mad->data);
	memset(port_counter, 0, sizeof(*port_counter));
	port_counter->port_select = port;
	port_counter->counter_select = 0xFFFF;

	p_madw->mad_addr.dest_lid = dest_lid;
	p_madw->mad_addr.addr_type.gsi.remote_qp = cl_hton32(1);
	p_madw->mad_addr.addr_type.gsi.remote_qkey = cl_hton32(IB_QP1_WELL_KNOWN_Q_KEY);
	/* FIXME what about other partitions */
	p_madw->mad_addr.addr_type.gsi.pkey = cl_hton16(0xFFFF);
	p_madw->mad_addr.addr_type.gsi.service_level = 0;
	p_madw->mad_addr.addr_type.gsi.global_route = FALSE;
	p_madw->resp_expected = TRUE;

	if( p_context )
		p_madw->context = *p_context;

	status = osm_vendor_send(perfmgr->bind_handle, p_madw, TRUE);

	if (status == IB_SUCCESS) {
		/* pause this thread if we have too many outstanding requests */
		cl_atomic_inc(&(perfmgr->outstanding_queries));
		if (perfmgr->outstanding_queries >
				perfmgr->max_outstanding_queries) {
			perfmgr->sweep_state = PERFMGR_SWEEP_SUSPENDED;
			cl_event_wait_on( &perfmgr->sig_query, EVENT_NO_TIMEOUT, TRUE );
			perfmgr->sweep_state = PERFMGR_SWEEP_ACTIVE;
		}
	}

	OSM_LOG_EXIT(perfmgr->log);
	return( status );
}

/**********************************************************************
 * sweep the node_guid_tbl and collect the node_guids to be tracked
 **********************************************************************/
static void
__collect_guids(cl_map_item_t * const p_map_item, void *context)
{
	osm_node_t         *node = (osm_node_t *)p_map_item;
	uint64_t            node_guid = cl_ntoh64(node->node_info.node_guid);
	osm_perfmgr_t      *pm = (osm_perfmgr_t *)context;
	__monitored_node_t *mon_node = NULL;

	OSM_LOG_ENTER( pm->log, __collect_guids );

	if (cl_qmap_get(&(pm->monitored_map), node_guid)
			== cl_qmap_end(&(pm->monitored_map))) {
		/* if not already in our map add it */
		mon_node = malloc(sizeof(*mon_node));
		if (!mon_node) {
			osm_log(pm->log, OSM_LOG_ERROR,
				"PerfMgr: __collect_guids ERR 4C06: malloc failed so not handling node GUID 0x%" PRIx64 "\n", node_guid);
			goto Exit;
		}
		mon_node->guid = node_guid;
		cl_qmap_insert(&(pm->monitored_map), node_guid,
				(cl_map_item_t *)mon_node);
	}

Exit:
	OSM_LOG_EXIT( pm->log );
}

/**********************************************************************
 * query the Port Counters of all the nodes in the subnet.
 **********************************************************************/
static void
__osm_perfmgr_query_counters(cl_map_item_t * const p_map_item, void *context )
{
	ib_api_status_t     status = IB_SUCCESS;
	uint8_t             port = 0;
	osm_perfmgr_t      *pm = (osm_perfmgr_t *)context;
	osm_node_t         *node = NULL;
	__monitored_node_t *mon_node = (__monitored_node_t *)p_map_item;
	osm_madw_context_t  mad_context;
	uint8_t             num_ports = 0;
	uint64_t            node_guid = 0;

	OSM_LOG_ENTER( pm->log, __osm_pm_query_counters );

	cl_plock_acquire(pm->lock);
	node = osm_get_node_by_guid(pm->subn, cl_hton64(mon_node->guid));
	if (!node) {
		osm_log(pm->log, OSM_LOG_ERROR,
			"__osm_pm_query_counters: ERR 4C07: Node guid 0x%" PRIx64 " no longer exists so removing from PerfMgr monitoring\n",
			mon_node->guid);
		__mark_for_removal(pm, mon_node);
		goto Exit;
	}

	num_ports = osm_node_get_num_physp(node);
	node_guid = cl_ntoh64(node->node_info.node_guid);

	/* make sure we have a database object ready to store this information */
	if (perfmgr_db_create_entry(pm->db, node_guid, num_ports,
				    node->print_desc) !=
		PERFMGR_EVENT_DB_SUCCESS)
	{
		osm_log(pm->log, OSM_LOG_ERROR,
			"__osm_pm_query_counters: ERR 4C08: DB create entry failed for 0x%" PRIx64 " (%s) : %s\n",
			node_guid, node->print_desc, strerror(errno));
		goto Exit;
	}

	/* issue the queries for each port */
	for (port = 1; port < num_ports; port++)
	{
		ib_net16_t lid;

		if (!osm_physp_is_valid(osm_node_get_physp_ptr(node, port)))
			continue;

		lid = get_lid(node, port);
		if (lid == 0)
		{
			osm_log(pm->log, OSM_LOG_DEBUG,
				"__osm_pm_query_counters: WARN: node 0x%" PRIx64 " port %d (%s): port out of range, skipping\n",
				cl_ntoh64(node->node_info.node_guid), port,
				node->print_desc);
			continue;
		}

		mad_context.perfmgr_context.node_guid = node_guid;
		mad_context.perfmgr_context.port = port;
		mad_context.perfmgr_context.mad_method = IB_MAD_METHOD_GET;
#if 0
		gettimeofday(&(mad_context.perfmgr_context.query_start), NULL);
#endif
		osm_log(pm->log, OSM_LOG_VERBOSE,
				"__osm_pm_query_counters: Getting stats for node 0x%" PRIx64 " port %d (lid %X) (%s)\n",
				node_guid, port, cl_ntoh16(lid),
				node->print_desc);
		status = osm_perfmgr_send_pc_mad(pm, lid, port, IB_MAD_METHOD_GET, &mad_context);
		if (status != IB_SUCCESS)
		{
		      osm_log(pm->log, OSM_LOG_ERROR,
				"__osm_pm_query_counters: ERR 4C09: Failed to issue port counter query for node 0x%" PRIx64 " port %d (%s)\n",
				node->node_info.node_guid, port,
				node->print_desc);
		}
	}
Exit:
	cl_plock_release(pm->lock);
	OSM_LOG_EXIT( pm->log );
}

/**********************************************************************
 * Main PerfMgr Thread.
 * Loop continuously and query the performance counters.
 **********************************************************************/
void
__osm_perfmgr_sweeper(void *p_ptr)
{
	ib_api_status_t status;
	osm_perfmgr_t *const pm = ( osm_perfmgr_t * ) p_ptr;

	OSM_LOG_ENTER( pm->log, __osm_pm_sweeper );

	if( pm->thread_state == OSM_THREAD_STATE_INIT )
		pm->thread_state = OSM_THREAD_STATE_RUN;

	__init_monitored_nodes(pm);

	while( pm->thread_state == OSM_THREAD_STATE_RUN ) {
		/*  do the sweep only if in MASTER state
		 *  AND we have been activated.
		 *  FIXME put something in here to try and reduce the load on the system
		 *  when it is not IDLE.
		if (pm->sm->state_mgr.state != OSM_SM_STATE_IDLE)
		 */
		if (pm->subn->sm_state == IB_SMINFO_STATE_MASTER &&
		    pm->state == PERFMGR_STATE_ENABLED) {
#if 0
			struct timeval before, after;
			gettimeofday(&before, NULL);
#endif
			pm->sweep_state = PERFMGR_SWEEP_ACTIVE;
			/* With the global lock held collect the node guids */
			/* FIXME we should be able to track trap messages here
			 * and not have to sweep the node_guid_tbl each pass
			 */
			osm_log(pm->log, OSM_LOG_VERBOSE, "Gathering PerfMgr stats\n");
			cl_plock_acquire(pm->lock);
			cl_qmap_apply_func(&(pm->subn->node_guid_tbl),
					__collect_guids, (void *)pm);
			cl_plock_release(pm->lock);

			/* then for each node query their counters */
			cl_qmap_apply_func(&(pm->monitored_map),
					__osm_perfmgr_query_counters, (void *)pm);

			/* Clean out any nodes found to be removed during the
			 * sweep
			 */
			__remove_marked_nodes(pm);
#if 0
			gettimeofday(&after, NULL);
			osm_log(pm->log, OSM_LOG_INFO,
				"total sweep time : %ld us\n", after.tv_usec - before.tv_usec);
#endif
		}

		pm->sweep_state = PERFMGR_SWEEP_SLEEP;

		/* Wait for a forced sweep or period timeout. */
		status = cl_event_wait_on( &pm->sig_sweep, pm->sweep_time_s * 1000000, TRUE );
	}

	OSM_LOG_EXIT( pm->log );
}

/**********************************************************************
 **********************************************************************/
void
osm_perfmgr_shutdown(osm_perfmgr_t * const pm)
{
	OSM_LOG_ENTER( pm->log, osm_perfmgr_shutdown );
	osm_perfmgr_mad_unbind(pm);
	OSM_LOG_EXIT( pm->log );
}

/**********************************************************************
 **********************************************************************/
void
osm_perfmgr_destroy(osm_perfmgr_t * const pm)
{
	OSM_LOG_ENTER( pm->log, osm_perfmgr_destroy );
	free(pm->event_db_dump_file);
	perfmgr_db_destroy(pm->db);
	OSM_LOG_EXIT( pm->log );
}

/**********************************************************************
 * Detect if someone else on the network could have cleared the counters
 * without us knowing.  This is easy to detect because the counters never wrap
 * but are "sticky"
 *
 * The one time this will not work is if the port is getting errors fast enough
 * to have the reading overtake the previous reading.  In this case counters
 * will be missed.
 **********************************************************************/
static void
osm_perfmgr_check_oob_clear(osm_perfmgr_t *pm, uint64_t node_guid, uint8_t port,
				perfmgr_db_err_reading_t *cr,
				perfmgr_db_data_cnt_reading_t *dc)
{
	perfmgr_db_err_reading_t       prev_err;
	perfmgr_db_data_cnt_reading_t  prev_dc;

	if (perfmgr_db_get_prev_err(pm->db, node_guid, port, &prev_err)
			!= PERFMGR_EVENT_DB_SUCCESS)
	{
		osm_log(pm->log, OSM_LOG_VERBOSE,
			"osm_perfmgr_check_oob_clear: Failed to find previous error reading for 0x%" PRIx64 " port %u\n",
			node_guid, port);
		return;
	}

	if (cr->symbol_err_cnt < prev_err.symbol_err_cnt
		|| cr->link_err_recover < prev_err.link_err_recover
		|| cr->link_downed < prev_err.link_downed
		|| cr->rcv_err < prev_err.rcv_err
		|| cr->rcv_rem_phys_err < prev_err.rcv_rem_phys_err
		|| cr->rcv_switch_relay_err < prev_err.rcv_switch_relay_err
		|| cr->xmit_discards < prev_err.xmit_discards
		|| cr->xmit_constraint_err < prev_err.xmit_constraint_err
		|| cr->rcv_constraint_err < prev_err.rcv_constraint_err
		|| cr->link_integrity < prev_err.link_integrity
		|| cr->buffer_overrun < prev_err.buffer_overrun
		|| cr->vl15_dropped < prev_err.vl15_dropped
	   )
	{
		osm_log(pm->log, OSM_LOG_ERROR,
			"PerfMgr: ERR 4C0A: Detected an out of band error clear on node 0x%" PRIx64 " port %u\n",
			node_guid, port
			);
		perfmgr_db_clear_prev_err(pm->db, node_guid, port);
	}

	/* FIXME handle extended counters */
	if (perfmgr_db_get_prev_dc(pm->db, node_guid, port, &prev_dc)
			!= PERFMGR_EVENT_DB_SUCCESS)
	{
		osm_log(pm->log, OSM_LOG_VERBOSE,
			"osm_perfmgr_check_oob_clear: Failed to find previous data count reading for 0x%" PRIx64 " port %u\n",
			node_guid, port);
		return;
	}

	if (dc->xmit_data < prev_dc.xmit_data
		|| dc->rcv_data < prev_dc.rcv_data
		|| dc->xmit_pkts < prev_dc.xmit_pkts
		|| dc->rcv_pkts < prev_dc.rcv_pkts
		)
	{
		osm_log(pm->log, OSM_LOG_ERROR,
			"PerfMgr: ERR 4C0B: Detected an out of band data counter clear on node 0x%" PRIx64 " port %u\n",
			node_guid, port
			);
		perfmgr_db_clear_prev_dc(pm->db, node_guid, port);
	}
}

/**********************************************************************
 * Return 1 if the value is "close" to overflowing
 **********************************************************************/
int counter_overflow_4(uint8_t val)
{
	return (val >= 10);
}
int counter_overflow_8(uint8_t val)
{
	return (val >= (UINT8_MAX - (UINT8_MAX/4)));
}
int counter_overflow_16(ib_net16_t val)
{
	return (cl_ntoh16(val) >= (UINT16_MAX - (UINT16_MAX/4)));
}
int counter_overflow_32(ib_net32_t val)
{
	return (cl_ntoh32(val) >= (UINT32_MAX - (UINT32_MAX/4)));
}

/**********************************************************************
 * Check if the port counters have overflowed and if so issue a clear
 * MAD to the port.
 **********************************************************************/
static void
osm_perfmgr_check_overflow(osm_perfmgr_t *pm, uint64_t node_guid,
			   uint8_t port, ib_port_counters_t *pc)
{
	osm_madw_context_t  mad_context;

	OSM_LOG_ENTER( pm->log, osm_perfmgr_check_overflow );
	if (counter_overflow_16(pc->symbol_err_cnt)
		|| counter_overflow_8(pc->link_err_recover)
		|| counter_overflow_8(pc->link_downed)
		|| counter_overflow_16(pc->rcv_err)
		|| counter_overflow_16(pc->rcv_rem_phys_err)
		|| counter_overflow_16(pc->rcv_switch_relay_err)
		|| counter_overflow_16(pc->xmit_discards)
		|| counter_overflow_8(pc->xmit_constraint_err)
		|| counter_overflow_8(pc->rcv_constraint_err)
		|| counter_overflow_4(PC_LINK_INT(pc->link_int_buffer_overrun))
		|| counter_overflow_4(PC_BUF_OVERRUN(pc->link_int_buffer_overrun))
		|| counter_overflow_16(pc->vl15_dropped)
		|| counter_overflow_32(pc->xmit_data)
		|| counter_overflow_32(pc->rcv_data)
		|| counter_overflow_32(pc->xmit_pkts)
		|| counter_overflow_32(pc->rcv_pkts)
		)
	{
		osm_log(pm->log, OSM_LOG_INFO,
			"PerfMgr: Counter overflow: 0x%" PRIx64 " port %d; clearing counters\n",
			node_guid, port);
		osm_node_t *p_node = NULL;
		ib_net16_t  lid = 0;
		cl_plock_acquire(pm->lock);
		p_node = osm_get_node_by_guid(pm->subn, cl_hton64(node_guid));
		lid = get_lid(p_node, port);
		cl_plock_release(pm->lock);
		if (lid == 0)
		{
			osm_log(pm->log, OSM_LOG_ERROR,
				"PerfMgr: ERR 4C0C: Failed to clear counters for node 0x%" PRIx64 " port %d; failed to get lid\n",
				node_guid, port);
			goto Exit;
		}
		mad_context.perfmgr_context.node_guid = node_guid;
		mad_context.perfmgr_context.port = port;
		mad_context.perfmgr_context.mad_method = IB_MAD_METHOD_SET;
		/* clear port counters */
		osm_perfmgr_send_pc_mad(pm, lid, port, IB_MAD_METHOD_SET, &mad_context);
		perfmgr_db_clear_prev_dc(pm->db, node_guid, port);
	}
Exit:
	OSM_LOG_EXIT( pm->log );
}

/**********************************************************************
 * Check values for logging of errors
 **********************************************************************/
static void
osm_perfmgr_log_events(osm_perfmgr_t *pm, uint64_t node_guid, uint8_t port,
			perfmgr_db_err_reading_t *reading)
{
	perfmgr_db_err_reading_t  prev_read;
	time_t                    time_diff = 0;
	perfmgr_db_err_t          err = perfmgr_db_get_prev_err(pm->db, node_guid, port, &prev_read);

	if (err != PERFMGR_EVENT_DB_SUCCESS)
	{
		osm_log(pm->log, OSM_LOG_VERBOSE,
			"osm_perfmgr_log_events: Failed to find previous reading for 0x%" PRIx64 " port %u\n",
			node_guid, port);
		return;
	}
	time_diff = (reading->time - prev_read.time);

	/* FIXME these events should be defineable by the user in a config
	 * file somewhere. */
	if (reading->symbol_err_cnt > prev_read.symbol_err_cnt) {
		osm_log(pm->log, OSM_LOG_ERROR,
			"osm_perfmgr_log_events: ERR 4C0D: "
			"Found %"PRIu64" Symbol errors in %lu sec on node 0x%" PRIx64 " port %u\n",
			(reading->symbol_err_cnt - prev_read.symbol_err_cnt),
			time_diff,
			node_guid,
			port);
	}
	if (reading->rcv_err > prev_read.rcv_err) {
		osm_log(pm->log, OSM_LOG_ERROR,
			"osm_perfmgr_log_events: ERR 4C0E: "
			"Found %"PRIu64" Receive errors in %lu sec on node 0x%" PRIx64 " port %u\n",
			(reading->rcv_err - prev_read.rcv_err),
			time_diff,
			node_guid,
			port);
	}
	if (reading->xmit_discards > prev_read.xmit_discards) {
		osm_log(pm->log, OSM_LOG_ERROR,
			"osm_perfmgr_log_events: ERR 4C0F: "
			"Found %"PRIu64" Xmit Discards in %lu sec on node 0x%" PRIx64 " port %u\n",
			(reading->xmit_discards - prev_read.xmit_discards),
			time_diff,
			node_guid,
			port);
	}
}

/**********************************************************************
 * The dispatcher uses a thread pool which will call this function when
 * we have a thread available to process our mad recieved from the wire.
 **********************************************************************/
static void
osm_pc_rcv_process(void *context, void *data)
{
	osm_perfmgr_t      *const pm = (osm_perfmgr_t *)context;
	osm_madw_t         *p_madw = (osm_madw_t *)data;
	osm_madw_context_t *mad_context = &(p_madw->context);
	ib_port_counters_t *wire_read = (ib_port_counters_t *)&(osm_madw_get_perfmgr_mad_ptr(p_madw)->data);
	ib_mad_t           *p_mad = osm_madw_get_mad_ptr(p_madw);
	uint64_t            node_guid = mad_context->perfmgr_context.node_guid;
	uint8_t             port_num = mad_context->perfmgr_context.port;

	perfmgr_db_err_reading_t      err_reading;
	perfmgr_db_data_cnt_reading_t data_reading;

	OSM_LOG_ENTER( pm->log, osm_pc_rcv_process );

	osm_log(pm->log, OSM_LOG_VERBOSE,
		"osm_pc_rcv_process: Processing received MAD context 0x%" PRIx64 " port %u\n",
		node_guid, port_num);

	/* Could also be redirection (IBM eHCA PMA does this) */
	if (p_mad->attr_id == IB_MAD_ATTR_CLASS_PORT_INFO) {
		osm_log(pm->log, OSM_LOG_VERBOSE,
		        "osm_pc_rcv_process: Redirection received. Not currently implemented!\n");
		goto Exit;
	}

	CL_ASSERT( p_mad->attr_id == IB_MAD_ATTR_PORT_CNTRS );

	perfmgr_db_fill_err_read(wire_read, &err_reading);
	/* FIXME separate query for extended counters if they are supported
	 * on the port.
	 */
	perfmgr_db_fill_data_cnt_read_pc(wire_read, &data_reading);

	/* detect an out of band clear on the port */
	if (mad_context->perfmgr_context.mad_method != IB_MAD_METHOD_SET)
		osm_perfmgr_check_oob_clear(pm, node_guid, port_num,
				&err_reading, &data_reading);

	/* log any critical events from this reading */
	osm_perfmgr_log_events(pm, node_guid, port_num, &err_reading);

	if (mad_context->perfmgr_context.mad_method == IB_MAD_METHOD_GET) {
		perfmgr_db_add_err_reading(pm->db, node_guid, port_num, &err_reading);
		perfmgr_db_add_dc_reading(pm->db, node_guid, port_num, &data_reading);

	} else {
		perfmgr_db_clear_prev_err(pm->db, node_guid, port_num);
		perfmgr_db_clear_prev_dc(pm->db, node_guid, port_num);
	}
	osm_perfmgr_check_overflow(pm, node_guid, port_num, wire_read);

#if 0
	do {
		struct timeval      proc_time;
		gettimeofday(&proc_time, NULL);
		osm_log(pm->log, OSM_LOG_INFO,
			"perfmgr done processing time %ld\n",
			proc_time.tv_usec -
			p_madw->context.perfmgr_context.query_start.tv_usec);
	} while (0);
#endif

 Exit:
	osm_mad_pool_put( pm->mad_pool, p_madw );

	OSM_LOG_EXIT( pm->log );
}

/**********************************************************************
 * Initialize the PerfMgr object
 **********************************************************************/
ib_api_status_t
osm_perfmgr_init(
	osm_perfmgr_t * const pm,
	osm_subn_t * const subn,
	osm_sm_t * const sm,
	osm_log_t * const log,
	osm_mad_pool_t * const mad_pool,
	osm_vendor_t * const vendor,
	cl_dispatcher_t* const disp,
	cl_plock_t* const lock,
	const osm_subn_opt_t * const p_opt,
	osm_epi_plugin_t     *event_plugin
	)
{
	ib_api_status_t    status = IB_SUCCESS;

	OSM_LOG_ENTER( log, osm_pm_init );

	osm_log(log, OSM_LOG_VERBOSE, "Initializing PerfMgr\n");

	memset( pm, 0, sizeof( *pm ) );

	cl_event_construct(&pm->sig_sweep);
	cl_event_init(&pm->sig_sweep, FALSE);
	pm->subn = subn;
	pm->sm = sm;
	pm->log = log;
	pm->mad_pool = mad_pool;
	pm->vendor = vendor;
	pm->trans_id = OSM_PERFMGR_INITIAL_TID_VALUE;
	pm->lock = lock;
	pm->state = p_opt->perfmgr ? PERFMGR_STATE_ENABLED : PERFMGR_STATE_DISABLE;
	pm->sweep_time_s = p_opt->perfmgr_sweep_time_s;
	pm->event_db_dump_file = strdup(p_opt->event_db_dump_file);
	pm->max_outstanding_queries = p_opt->perfmgr_max_outstanding_queries;
	pm->event_plugin = event_plugin;

	pm->db = perfmgr_db_construct(pm->log, pm->event_plugin);
	if (!pm->db)
	{
	      pm->state = PERFMGR_STATE_NO_DB;
	      goto Exit;
	}

	pm->pc_disp_h = cl_disp_register(disp, OSM_MSG_MAD_PORT_COUNTERS,
	                                 osm_pc_rcv_process, pm);
	if( pm->pc_disp_h == CL_DISP_INVALID_HANDLE )
		goto Exit;

	pm->thread_state = OSM_THREAD_STATE_INIT;
	status = cl_thread_init( &pm->sweeper, __osm_perfmgr_sweeper, pm,
				 "PerfMgr sweeper" );
	if( status != IB_SUCCESS )
		goto Exit;

Exit:
	OSM_LOG_EXIT( log );
	return ( status );
}

/**********************************************************************
 * Clear the counters from the db
 **********************************************************************/
void
osm_perfmgr_clear_counters(osm_perfmgr_t *pm)
{
	/**
	 * FIXME todo issue clear on the fabric?
	 */
	perfmgr_db_clear_counters(pm->db);
	osm_log( pm->log, OSM_LOG_INFO, "PerfMgr counters cleared\n");
}

/*******************************************************************
 * Have the DB dump it's information to the file specified
 *******************************************************************/
void
osm_perfmgr_dump_counters(osm_perfmgr_t *pm, perfmgr_db_dump_t dump_type)
{
	if (perfmgr_db_dump(pm->db, pm->event_db_dump_file, dump_type) != 0)
	{
		osm_log( pm->log, OSM_LOG_ERROR,
			"PB dump port counters: ERR 4C10: Failed to dump file %s : %s",
			pm->event_db_dump_file, strerror(errno));
	}
}

#if 0
/*******************************************************************
 * Use this later to track events on the fabric
 **********************************************************************/
ib_api_status_t
osm_report_notice_to_perfmgr(osm_log_t* const log, osm_subn_t*  subn,
			ib_mad_notice_attr_t *p_ntc )
{
  OSM_LOG_ENTER( log, osm_report_trap_to_pm );
  if ((p_ntc->generic_type & 0x80)
	  && (cl_ntoh16(p_ntc->g_or_v.generic.trap_num) == 128)) {
	  osm_log( log, OSM_LOG_INFO, "PerfMgr notified of trap 128\n");
  }
  OSM_LOG_EXIT( log );
  return (IB_SUCCESS);
}
#endif

#endif /* ENABLE_OSM_PERF_MGR */

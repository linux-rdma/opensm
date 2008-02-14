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
 *    Implementation of osm_sir_rcv_t.
 * This object represents the SwitchInfo Receiver object.
 * This object is part of the opensm family of objects.
 *
 * Environment:
 *    Linux User Mode
 *
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <string.h>
#include <iba/ib_types.h>
#include <complib/cl_debug.h>
#include <complib/cl_qlist.h>
#include <vendor/osm_vendor_api.h>
#include <opensm/osm_node.h>
#include <opensm/osm_helper.h>
#include <opensm/osm_pkey.h>
#include <opensm/osm_sa.h>

typedef struct _osm_sir_item {
	cl_list_item_t list_item;
	ib_switch_info_record_t rec;
} osm_sir_item_t;

typedef struct _osm_sir_search_ctxt {
	const ib_switch_info_record_t *p_rcvd_rec;
	ib_net64_t comp_mask;
	cl_qlist_t *p_list;
	osm_sa_t *sa;
	const osm_physp_t *p_req_physp;
} osm_sir_search_ctxt_t;

/**********************************************************************
 **********************************************************************/
static ib_api_status_t
__osm_sir_rcv_new_sir(IN osm_sa_t * sa,
		      IN const osm_switch_t * const p_sw,
		      IN cl_qlist_t * const p_list, IN ib_net16_t const lid)
{
	osm_sir_item_t *p_rec_item;
	ib_api_status_t status = IB_SUCCESS;

	OSM_LOG_ENTER(sa->p_log, __osm_sir_rcv_new_sir);

	p_rec_item = malloc(sizeof(*p_rec_item));
	if (p_rec_item == NULL) {
		osm_log(sa->p_log, OSM_LOG_ERROR,
			"__osm_sir_rcv_new_sir: ERR 5308: "
			"rec_item alloc failed\n");
		status = IB_INSUFFICIENT_RESOURCES;
		goto Exit;
	}

	if (osm_log_is_active(sa->p_log, OSM_LOG_DEBUG))
		osm_log(sa->p_log, OSM_LOG_DEBUG,
			"__osm_sir_rcv_new_sir: "
			"New SwitchInfoRecord: lid 0x%X\n", cl_ntoh16(lid)
		    );

	memset(p_rec_item, 0, sizeof(*p_rec_item));

	p_rec_item->rec.lid = lid;
	p_rec_item->rec.switch_info = p_sw->switch_info;

	cl_qlist_insert_tail(p_list, &p_rec_item->list_item);

Exit:
	OSM_LOG_EXIT(sa->p_log);
	return (status);
}

/**********************************************************************
 **********************************************************************/
static osm_port_t *__osm_sir_get_port_by_guid(IN osm_sa_t * sa,
					      IN uint64_t port_guid)
{
	osm_port_t *p_port;

	CL_PLOCK_ACQUIRE(sa->p_lock);

	p_port = osm_get_port_by_guid(sa->p_subn, port_guid);
	if (!p_port) {
		osm_log(sa->p_log, OSM_LOG_DEBUG,
			"__osm_sir_get_port_by_guid ERR 5309: "
			"Invalid port GUID 0x%016" PRIx64 "\n", port_guid);
		p_port = NULL;
	}

	CL_PLOCK_RELEASE(sa->p_lock);
	return p_port;
}

/**********************************************************************
 **********************************************************************/
static void
__osm_sir_rcv_create_sir(IN osm_sa_t * sa,
			 IN const osm_switch_t * const p_sw,
			 IN cl_qlist_t * const p_list,
			 IN ib_net16_t const match_lid,
			 IN const osm_physp_t * const p_req_physp)
{
	osm_port_t *p_port;
	const osm_physp_t *p_physp;
	uint16_t match_lid_ho;
	ib_net16_t min_lid_ho;
	ib_net16_t max_lid_ho;

	OSM_LOG_ENTER(sa->p_log, __osm_sir_rcv_create_sir);

	if (osm_log_is_active(sa->p_log, OSM_LOG_DEBUG)) {
		osm_log(sa->p_log, OSM_LOG_DEBUG,
			"__osm_sir_rcv_create_sir: "
			"Looking for SwitchInfoRecord with LID: 0x%X\n",
			cl_ntoh16(match_lid)
		    );
	}

	/* In switches, the port guid is the node guid. */
	p_port =
	    __osm_sir_get_port_by_guid(sa,
				       p_sw->p_node->node_info.port_guid);
	if (!p_port) {
		osm_log(sa->p_log, OSM_LOG_ERROR,
			"__osm_sir_rcv_create_sir: ERR 530A: "
			"Failed to find Port by Node Guid:0x%016" PRIx64
			"\n", cl_ntoh64(p_sw->p_node->node_info.node_guid)
		    );
		goto Exit;
	}

	/* check that the requester physp and the current physp are under
	   the same partition. */
	p_physp = p_port->p_physp;
	if (!p_physp) {
		osm_log(sa->p_log, OSM_LOG_ERROR,
			"__osm_sir_rcv_create_sir: ERR 530B: "
			"Failed to find default physical Port by Node Guid:0x%016"
			PRIx64 "\n",
			cl_ntoh64(p_sw->p_node->node_info.node_guid)
		    );
		goto Exit;
	}
	if (!osm_physp_share_pkey(sa->p_log, p_req_physp, p_physp))
		goto Exit;

	/* get the port 0 of the switch */
	osm_port_get_lid_range_ho(p_port, &min_lid_ho, &max_lid_ho);

	match_lid_ho = cl_ntoh16(match_lid);
	if (match_lid_ho) {
		/*
		   We validate that the lid belongs to this switch.
		 */
		if (osm_log_is_active(sa->p_log, OSM_LOG_DEBUG)) {
			osm_log(sa->p_log, OSM_LOG_DEBUG,
				"__osm_sir_rcv_create_sir: "
				"Comparing LID: 0x%X <= 0x%X <= 0x%X\n",
				min_lid_ho, match_lid_ho, max_lid_ho);
		}

		if (match_lid_ho < min_lid_ho || match_lid_ho > max_lid_ho)
			goto Exit;

	}

	__osm_sir_rcv_new_sir(sa, p_sw, p_list,
			      osm_port_get_base_lid(p_port));

Exit:
	OSM_LOG_EXIT(sa->p_log);
}

/**********************************************************************
 **********************************************************************/
static void
__osm_sir_rcv_by_comp_mask(IN cl_map_item_t * const p_map_item,
			   IN void *context)
{
	const osm_sir_search_ctxt_t *const p_ctxt =
	    (osm_sir_search_ctxt_t *) context;
	const osm_switch_t *const p_sw = (osm_switch_t *) p_map_item;
	const ib_switch_info_record_t *const p_rcvd_rec = p_ctxt->p_rcvd_rec;
	const osm_physp_t *const p_req_physp = p_ctxt->p_req_physp;
	osm_sa_t *sa = p_ctxt->sa;
	ib_net64_t const comp_mask = p_ctxt->comp_mask;
	ib_net16_t match_lid = 0;

	OSM_LOG_ENTER(p_ctxt->sa->p_log, __osm_sir_rcv_by_comp_mask);

	osm_dump_switch_info(p_ctxt->sa->p_log,
			     &p_sw->switch_info, OSM_LOG_VERBOSE);

	if (comp_mask & IB_SWIR_COMPMASK_LID) {
		match_lid = p_rcvd_rec->lid;
		if (!match_lid)
			goto Exit;
	}

	__osm_sir_rcv_create_sir(sa, p_sw, p_ctxt->p_list,
				 match_lid, p_req_physp);

Exit:
	OSM_LOG_EXIT(p_ctxt->sa->p_log);
}

/**********************************************************************
 **********************************************************************/
void osm_sir_rcv_process(IN void *ctx, IN void *data)
{
	osm_sa_t *sa = ctx;
	osm_madw_t *p_madw = data;
	const ib_sa_mad_t *sad_mad;
	const ib_switch_info_record_t *p_rcvd_rec;
	ib_switch_info_record_t *p_resp_rec;
	cl_qlist_t rec_list;
	osm_madw_t *p_resp_madw;
	ib_sa_mad_t *p_resp_sa_mad;
	uint32_t num_rec, pre_trim_num_rec;
#ifndef VENDOR_RMPP_SUPPORT
	uint32_t trim_num_rec;
#endif
	uint32_t i;
	osm_sir_search_ctxt_t context;
	osm_sir_item_t *p_rec_item;
	ib_api_status_t status;
	osm_physp_t *p_req_physp;

	CL_ASSERT(sa);

	OSM_LOG_ENTER(sa->p_log, osm_sir_rcv_process);

	CL_ASSERT(p_madw);

	sad_mad = osm_madw_get_sa_mad_ptr(p_madw);
	p_rcvd_rec =
	    (ib_switch_info_record_t *) ib_sa_mad_get_payload_ptr(sad_mad);

	CL_ASSERT(sad_mad->attr_id == IB_MAD_ATTR_SWITCH_INFO_RECORD);

	/* we only support SubnAdmGet and SubnAdmGetTable methods */
	if ((sad_mad->method != IB_MAD_METHOD_GET) &&
	    (sad_mad->method != IB_MAD_METHOD_GETTABLE)) {
		osm_log(sa->p_log, OSM_LOG_ERROR,
			"osm_sir_rcv_process: ERR 5305: "
			"Unsupported Method (%s)\n",
			ib_get_sa_method_str(sad_mad->method));
		osm_sa_send_error(sa, p_madw,
				  IB_MAD_STATUS_UNSUP_METHOD_ATTR);
		goto Exit;
	}

	/* update the requester physical port. */
	p_req_physp = osm_get_physp_by_mad_addr(sa->p_log,
						sa->p_subn,
						osm_madw_get_mad_addr_ptr
						(p_madw));
	if (p_req_physp == NULL) {
		osm_log(sa->p_log, OSM_LOG_ERROR,
			"osm_sir_rcv_process: ERR 5304: "
			"Cannot find requester physical port\n");
		goto Exit;
	}

	if (osm_log_is_active(sa->p_log, OSM_LOG_DEBUG))
		osm_dump_switch_info_record(sa->p_log, p_rcvd_rec,
					    OSM_LOG_DEBUG);

	cl_qlist_init(&rec_list);

	context.p_rcvd_rec = p_rcvd_rec;
	context.p_list = &rec_list;
	context.comp_mask = sad_mad->comp_mask;
	context.sa = sa;
	context.p_req_physp = p_req_physp;

	cl_plock_acquire(sa->p_lock);

	/* Go over all switches */
	cl_qmap_apply_func(&sa->p_subn->sw_guid_tbl,
			   __osm_sir_rcv_by_comp_mask, &context);

	cl_plock_release(sa->p_lock);

	num_rec = cl_qlist_count(&rec_list);

	/*
	 * C15-0.1.30:
	 * If we do a SubnAdmGet and got more than one record it is an error !
	 */
	if ((sad_mad->method == IB_MAD_METHOD_GET) && (num_rec > 1)) {
		osm_log(sa->p_log, OSM_LOG_ERROR,
			"osm_sir_rcv_process: ERR 5303: "
			"Got more than one record for SubnAdmGet (%u)\n",
			num_rec);
		osm_sa_send_error(sa, p_madw,
				  IB_SA_MAD_STATUS_TOO_MANY_RECORDS);

		/* need to set the mem free ... */
		p_rec_item = (osm_sir_item_t *) cl_qlist_remove_head(&rec_list);
		while (p_rec_item != (osm_sir_item_t *) cl_qlist_end(&rec_list)) {
			free(p_rec_item);
			p_rec_item =
			    (osm_sir_item_t *) cl_qlist_remove_head(&rec_list);
		}

		goto Exit;
	}

	pre_trim_num_rec = num_rec;
#ifndef VENDOR_RMPP_SUPPORT
	/* we limit the number of records to a single packet */
	trim_num_rec =
	    (MAD_BLOCK_SIZE -
	     IB_SA_MAD_HDR_SIZE) / sizeof(ib_switch_info_record_t);
	if (trim_num_rec < num_rec) {
		osm_log(sa->p_log, OSM_LOG_VERBOSE,
			"osm_sir_rcv_process: "
			"Number of records:%u trimmed to:%u to fit in one MAD\n",
			num_rec, trim_num_rec);
		num_rec = trim_num_rec;
	}
#endif

	osm_log(sa->p_log, OSM_LOG_DEBUG,
		"osm_sir_rcv_process: " "Returning %u records\n", num_rec);

	if ((sad_mad->method == IB_MAD_METHOD_GET) && (num_rec == 0)) {
		osm_sa_send_error(sa, p_madw,
				  IB_SA_MAD_STATUS_NO_RECORDS);
		goto Exit;
	}

	/*
	 * Get a MAD to reply. Address of Mad is in the received mad_wrapper
	 */
	p_resp_madw = osm_mad_pool_get(sa->p_mad_pool,
				       p_madw->h_bind,
				       num_rec *
				       sizeof(ib_switch_info_record_t) +
				       IB_SA_MAD_HDR_SIZE, &p_madw->mad_addr);

	if (!p_resp_madw) {
		osm_log(sa->p_log, OSM_LOG_ERROR,
			"osm_sir_rcv_process: ERR 5306: "
			"osm_mad_pool_get failed\n");

		for (i = 0; i < num_rec; i++) {
			p_rec_item =
			    (osm_sir_item_t *) cl_qlist_remove_head(&rec_list);
			free(p_rec_item);
		}

		osm_sa_send_error(sa, p_madw,
				  IB_SA_MAD_STATUS_NO_RESOURCES);
		goto Exit;
	}

	p_resp_sa_mad = osm_madw_get_sa_mad_ptr(p_resp_madw);

	/*
	   Copy the MAD header back into the response mad.
	   Set the 'R' bit and the payload length,
	   Then copy all records from the list into the response payload.
	 */

	memcpy(p_resp_sa_mad, sad_mad, IB_SA_MAD_HDR_SIZE);
	p_resp_sa_mad->method |= IB_MAD_METHOD_RESP_MASK;
	/* C15-0.1.5 - always return SM_Key = 0 (table 185 p 884) */
	p_resp_sa_mad->sm_key = 0;
	/* Fill in the offset (paylen will be done by the rmpp SAR) */
	p_resp_sa_mad->attr_offset =
	    ib_get_attr_offset(sizeof(ib_switch_info_record_t));

	p_resp_rec = (ib_switch_info_record_t *)
	    ib_sa_mad_get_payload_ptr(p_resp_sa_mad);

#ifndef VENDOR_RMPP_SUPPORT
	/* we support only one packet RMPP - so we will set the first and
	   last flags for gettable */
	if (p_resp_sa_mad->method == IB_MAD_METHOD_GETTABLE_RESP) {
		p_resp_sa_mad->rmpp_type = IB_RMPP_TYPE_DATA;
		p_resp_sa_mad->rmpp_flags =
		    IB_RMPP_FLAG_FIRST | IB_RMPP_FLAG_LAST |
		    IB_RMPP_FLAG_ACTIVE;
	}
#else
	/* forcefully define the packet as RMPP one */
	if (p_resp_sa_mad->method == IB_MAD_METHOD_GETTABLE_RESP)
		p_resp_sa_mad->rmpp_flags = IB_RMPP_FLAG_ACTIVE;
#endif

	for (i = 0; i < pre_trim_num_rec; i++) {
		p_rec_item = (osm_sir_item_t *) cl_qlist_remove_head(&rec_list);
		/* copy only if not trimmed */
		if (i < num_rec)
			*p_resp_rec = p_rec_item->rec;
		free(p_rec_item);
		p_resp_rec++;
	}

	CL_ASSERT(cl_is_qlist_empty(&rec_list));

	status = osm_sa_vendor_send(p_resp_madw->h_bind, p_resp_madw, FALSE,
				    sa->p_subn);
	if (status != IB_SUCCESS) {
		osm_log(sa->p_log, OSM_LOG_ERROR,
			"osm_sir_rcv_process: ERR 5307: "
			"osm_sa_vendor_send status = %s\n",
			ib_get_err_str(status));
		goto Exit;
	}

Exit:
	OSM_LOG_EXIT(sa->p_log);
}

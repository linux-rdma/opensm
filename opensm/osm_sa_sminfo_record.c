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
 *    Implementation of osm_smir_rcv_t.
 * This object represents the SMInfo Receiver object.
 * This object is part of the opensm family of objects.
 *
 * Environment:
 *    Linux User Mode
 *
 * $Revision: 1.7 $
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <string.h>
#include <iba/ib_types.h>
#include <complib/cl_qmap.h>
#include <complib/cl_passivelock.h>
#include <complib/cl_debug.h>
#include <complib/cl_qlist.h>
#include <vendor/osm_vendor_api.h>
#include <opensm/osm_madw.h>
#include <opensm/osm_log.h>
#include <opensm/osm_subnet.h>
#include <opensm/osm_mad_pool.h>
#include <opensm/osm_helper.h>
#include <opensm/osm_msgdef.h>
#include <opensm/osm_port.h>
#include <opensm/osm_pkey.h>
#include <opensm/osm_remote_sm.h>
#include <opensm/osm_sa.h>
#include <opensm/osm_opensm.h>

typedef struct _osm_smir_item {
	cl_list_item_t list_item;
	ib_sminfo_record_t rec;
} osm_smir_item_t;

typedef struct _osm_smir_search_ctxt {
	const ib_sminfo_record_t *p_rcvd_rec;
	ib_net64_t comp_mask;
	cl_qlist_t *p_list;
	osm_sa_t *sa;
	const osm_physp_t *p_req_physp;
} osm_smir_search_ctxt_t;

static ib_api_status_t
__osm_smir_rcv_new_smir(IN osm_sa_t * sa,
			IN const osm_port_t * const p_port,
			IN cl_qlist_t * const p_list,
			IN ib_net64_t const guid,
			IN ib_net32_t const act_count,
			IN uint8_t const pri_state,
			IN const osm_physp_t * const p_req_physp)
{
	osm_smir_item_t *p_rec_item;
	ib_api_status_t status = IB_SUCCESS;

	OSM_LOG_ENTER(sa->p_log, __osm_smir_rcv_new_smir);

	p_rec_item = malloc(sizeof(*p_rec_item));
	if (p_rec_item == NULL) {
		osm_log(sa->p_log, OSM_LOG_ERROR,
			"__osm_smir_rcv_new_smir: ERR 2801: "
			"rec_item alloc failed\n");
		status = IB_INSUFFICIENT_RESOURCES;
		goto Exit;
	}

	if (osm_log_is_active(sa->p_log, OSM_LOG_DEBUG))
		osm_log(sa->p_log, OSM_LOG_DEBUG,
			"__osm_smir_rcv_new_smir: "
			"New SMInfo: GUID 0x%016" PRIx64 "\n", cl_ntoh64(guid)
		    );

	memset(p_rec_item, 0, sizeof(*p_rec_item));

	p_rec_item->rec.lid = osm_port_get_base_lid(p_port);
	p_rec_item->rec.sm_info.guid = guid;
	p_rec_item->rec.sm_info.act_count = act_count;
	p_rec_item->rec.sm_info.pri_state = pri_state;

	cl_qlist_insert_tail(p_list, &p_rec_item->list_item);

      Exit:
	OSM_LOG_EXIT(sa->p_log);
	return (status);
}

/**********************************************************************
 **********************************************************************/
static void
__osm_sa_smir_by_comp_mask(IN osm_sa_t * sa,
			   IN const osm_remote_sm_t * const p_rem_sm,
			   osm_smir_search_ctxt_t * const p_ctxt)
{
	const ib_sminfo_record_t *const p_rcvd_rec = p_ctxt->p_rcvd_rec;
	const osm_physp_t *const p_req_physp = p_ctxt->p_req_physp;
	ib_net64_t const comp_mask = p_ctxt->comp_mask;

	OSM_LOG_ENTER(sa->p_log, __osm_sa_smir_by_comp_mask);

	if (comp_mask & IB_SMIR_COMPMASK_GUID) {
		if (p_rem_sm->smi.guid != p_rcvd_rec->sm_info.guid)
			goto Exit;
	}

	if (comp_mask & IB_SMIR_COMPMASK_PRIORITY) {
		if (ib_sminfo_get_priority(&p_rem_sm->smi) !=
		    ib_sminfo_get_priority(&p_rcvd_rec->sm_info))
			goto Exit;
	}

	if (comp_mask & IB_SMIR_COMPMASK_SMSTATE) {
		if (ib_sminfo_get_state(&p_rem_sm->smi) !=
		    ib_sminfo_get_state(&p_rcvd_rec->sm_info))
			goto Exit;
	}

	/* Implement any other needed search cases */

	__osm_smir_rcv_new_smir(sa, p_rem_sm->p_port, p_ctxt->p_list,
				p_rem_sm->smi.guid,
				p_rem_sm->smi.act_count,
				p_rem_sm->smi.pri_state, p_req_physp);

      Exit:
	OSM_LOG_EXIT(sa->p_log);
}

/**********************************************************************
 **********************************************************************/
static void
__osm_sa_smir_by_comp_mask_cb(IN cl_map_item_t * const p_map_item,
			      IN void *context)
{
	const osm_remote_sm_t *const p_rem_sm = (osm_remote_sm_t *) p_map_item;
	osm_smir_search_ctxt_t *const p_ctxt =
	    (osm_smir_search_ctxt_t *) context;

	__osm_sa_smir_by_comp_mask(p_ctxt->sa, p_rem_sm, p_ctxt);
}

/**********************************************************************
 **********************************************************************/
void osm_smir_rcv_process(IN void *ctx, IN void *data)
{
	osm_sa_t *sa = ctx;
	osm_madw_t *p_madw = data;
	const ib_sa_mad_t *sad_mad;
	const ib_sminfo_record_t *p_rcvd_rec;
	const cl_qmap_t *p_tbl;
	const osm_port_t *p_port = NULL;
	const ib_sm_info_t *p_smi;
	cl_qlist_t rec_list;
	osm_madw_t *p_resp_madw;
	ib_sa_mad_t *p_resp_sa_mad;
	ib_sminfo_record_t *p_resp_rec;
	uint32_t num_rec, pre_trim_num_rec;
#ifndef VENDOR_RMPP_SUPPORT
	uint32_t trim_num_rec;
#endif
	uint32_t i;
	osm_smir_search_ctxt_t context;
	osm_smir_item_t *p_rec_item;
	ib_api_status_t status = IB_SUCCESS;
	ib_net64_t comp_mask;
	ib_net64_t port_guid;
	osm_physp_t *p_req_physp;
	osm_port_t *local_port;
	osm_remote_sm_t *p_rem_sm;
	cl_qmap_t *p_sm_guid_tbl;
	uint8_t pri_state;

	CL_ASSERT(sa);

	OSM_LOG_ENTER(sa->p_log, osm_smir_rcv_process);

	CL_ASSERT(p_madw);

	sad_mad = osm_madw_get_sa_mad_ptr(p_madw);
	p_rcvd_rec =
	    (ib_sminfo_record_t *) ib_sa_mad_get_payload_ptr(sad_mad);
	comp_mask = sad_mad->comp_mask;

	CL_ASSERT(sad_mad->attr_id == IB_MAD_ATTR_SMINFO_RECORD);

	/* we only support SubnAdmGet and SubnAdmGetTable methods */
	if ((sad_mad->method != IB_MAD_METHOD_GET) &&
	    (sad_mad->method != IB_MAD_METHOD_GETTABLE)) {
		osm_log(sa->p_log, OSM_LOG_ERROR,
			"osm_smir_rcv_process: ERR 2804: "
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
			"osm_smir_rcv_process: ERR 2803: "
			"Cannot find requester physical port\n");
		goto Exit;
	}

	if (osm_log_is_active(sa->p_log, OSM_LOG_DEBUG))
		osm_dump_sm_info_record(sa->p_log, p_rcvd_rec,
					OSM_LOG_DEBUG);

	p_tbl = &sa->p_subn->sm_guid_tbl;
	p_smi = &p_rcvd_rec->sm_info;

	cl_qlist_init(&rec_list);

	context.p_rcvd_rec = p_rcvd_rec;
	context.p_list = &rec_list;
	context.comp_mask = sad_mad->comp_mask;
	context.sa = sa;
	context.p_req_physp = p_req_physp;

	cl_plock_acquire(sa->p_lock);

	/*
	   If the user specified a LID, it obviously narrows our
	   work load, since we don't have to search every port
	 */
	if (comp_mask & IB_SMIR_COMPMASK_LID) {
		status =
		    osm_get_port_by_base_lid(sa->p_subn, p_rcvd_rec->lid,
					     &p_port);
		if ((status != IB_SUCCESS) || (p_port == NULL)) {
			status = IB_NOT_FOUND;
			osm_log(sa->p_log, OSM_LOG_ERROR,
				"osm_smir_rcv_process: ERR 2806: "
				"No port found with LID 0x%x\n",
				cl_ntoh16(p_rcvd_rec->lid));
		}
	}

	if (status == IB_SUCCESS) {
		/* Handle our own SM first */
		local_port =
		    osm_get_port_by_guid(sa->p_subn,
					 sa->p_subn->sm_port_guid);
		if (!local_port) {
			cl_plock_release(sa->p_lock);
			osm_log(sa->p_log, OSM_LOG_ERROR,
				"osm_smir_rcv_process: ERR 2809: "
				"No port found with GUID 0x%016" PRIx64 "\n",
				cl_ntoh64(sa->p_subn->sm_port_guid));
			goto Exit;
		}

		if (!p_port || local_port == p_port) {
			if (FALSE ==
			    osm_physp_share_pkey(sa->p_log, p_req_physp,
						 local_port->p_physp)) {
				cl_plock_release(sa->p_lock);
				osm_log(sa->p_log, OSM_LOG_ERROR,
					"osm_smir_rcv_process: ERR 2805: "
					"Cannot get SMInfo record due to pkey violation\n");
				goto Exit;
			}

			/* Check that other search components specified match */
			if (comp_mask & IB_SMIR_COMPMASK_GUID) {
				if (sa->p_subn->sm_port_guid != p_smi->guid)
					goto Remotes;
			}
			if (comp_mask & IB_SMIR_COMPMASK_PRIORITY) {
				if (sa->p_subn->opt.sm_priority !=
				    ib_sminfo_get_priority(p_smi))
					goto Remotes;
			}
			if (comp_mask & IB_SMIR_COMPMASK_SMSTATE) {
				if (sa->p_subn->sm_state !=
				    ib_sminfo_get_state(p_smi))
					goto Remotes;
			}

			/* Now, add local SMInfo to list */
			pri_state = sa->p_subn->sm_state & 0x0F;
			pri_state |=
			    (sa->p_subn->opt.sm_priority & 0x0F) << 4;
			__osm_smir_rcv_new_smir(sa, local_port,
						context.p_list,
						sa->p_subn->sm_port_guid,
						cl_ntoh32(sa->p_subn->p_osm->stats.qp0_mads_sent),
						pri_state, p_req_physp);
		}

	      Remotes:
		if (p_port && p_port != local_port) {
			/* Find remote SM corresponding to p_port */
			port_guid = osm_port_get_guid(p_port);
			p_sm_guid_tbl = &sa->p_subn->sm_guid_tbl;
			p_rem_sm =
			    (osm_remote_sm_t *) cl_qmap_get(p_sm_guid_tbl,
							    port_guid);
			if (p_rem_sm !=
			    (osm_remote_sm_t *) cl_qmap_end(p_sm_guid_tbl))
				__osm_sa_smir_by_comp_mask(sa, p_rem_sm,
							   &context);
			else {
				osm_log(sa->p_log, OSM_LOG_ERROR,
					"osm_smir_rcv_process: ERR 280A: "
					"No remote SM for GUID 0x%016" PRIx64
					"\n", cl_ntoh64(port_guid));
			}
		} else {
			/* Go over all other known (remote) SMs */
			cl_qmap_apply_func(&sa->p_subn->sm_guid_tbl,
					   __osm_sa_smir_by_comp_mask_cb,
					   &context);
		}
	}

	cl_plock_release(sa->p_lock);

	num_rec = cl_qlist_count(&rec_list);

	/*
	 * C15-0.1.30:
	 * If we do a SubnAdmGet and got more than one record it is an error !
	 */
	if (sad_mad->method == IB_MAD_METHOD_GET) {
		if (num_rec == 0) {
			osm_sa_send_error(sa, p_madw,
					  IB_SA_MAD_STATUS_NO_RECORDS);
			goto Exit;
		}
		if (num_rec > 1) {
			osm_log(sa->p_log, OSM_LOG_ERROR,
				"osm_smir_rcv_process: ERR 2808: "
				"Got more than one record for SubnAdmGet (%u)\n",
				num_rec);
			osm_sa_send_error(sa, p_madw,
					  IB_SA_MAD_STATUS_TOO_MANY_RECORDS);

			/* need to set the mem free ... */
			p_rec_item =
			    (osm_smir_item_t *) cl_qlist_remove_head(&rec_list);
			while (p_rec_item !=
			       (osm_smir_item_t *) cl_qlist_end(&rec_list)) {
				free(p_rec_item);
				p_rec_item = (osm_smir_item_t *)
				    cl_qlist_remove_head(&rec_list);
			}

			goto Exit;
		}
	}

	pre_trim_num_rec = num_rec;
#ifndef VENDOR_RMPP_SUPPORT
	trim_num_rec =
	    (MAD_BLOCK_SIZE - IB_SA_MAD_HDR_SIZE) / sizeof(ib_sminfo_record_t);
	if (trim_num_rec < num_rec) {
		osm_log(sa->p_log, OSM_LOG_VERBOSE,
			"osm_smir_rcv_process: "
			"Number of records:%u trimmed to:%u to fit in one MAD\n",
			num_rec, trim_num_rec);
		num_rec = trim_num_rec;
	}
#endif

	osm_log(sa->p_log, OSM_LOG_DEBUG,
		"osm_smir_rcv_process: " "Returning %u records\n", num_rec);

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
				       num_rec * sizeof(ib_sminfo_record_t) +
				       IB_SA_MAD_HDR_SIZE, &p_madw->mad_addr);

	if (!p_resp_madw) {
		osm_log(sa->p_log, OSM_LOG_ERROR,
			"osm_smir_rcv_process: ERR 2807: "
			"osm_mad_pool_get failed\n");

		for (i = 0; i < num_rec; i++) {
			p_rec_item =
			    (osm_smir_item_t *) cl_qlist_remove_head(&rec_list);
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
	    ib_get_attr_offset(sizeof(ib_sminfo_record_t));

	p_resp_rec = (ib_sminfo_record_t *)
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
		p_rec_item =
		    (osm_smir_item_t *) cl_qlist_remove_head(&rec_list);
		/* copy only if not trimmed */
		if (i < num_rec) {
			*p_resp_rec = p_rec_item->rec;
			p_resp_rec->sm_info.sm_key = 0;
		}
		free(p_rec_item);
		p_resp_rec++;
	}

	CL_ASSERT(cl_is_qlist_empty(&rec_list));

	status = osm_sa_vendor_send(p_resp_madw->h_bind, p_resp_madw, FALSE,
				    sa->p_subn);
	if (status != IB_SUCCESS) {
		osm_log(sa->p_log, OSM_LOG_ERROR,
			"osm_smir_rcv_process: ERR 2802: "
			"Error sending MAD (%s)\n", ib_get_err_str(status));
		goto Exit;
	}

      Exit:
	OSM_LOG_EXIT(sa->p_log);
}

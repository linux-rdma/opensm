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
 *    Implementation of osm_slvl_rec_rcv_t.
 * This object represents the SLtoVL Mapping Query Receiver object.
 * This object is part of the opensm family of objects.
 *
 * Environment:
 *    Linux User Mode
 *
 * $Revision: 1.6 $
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
#include <opensm/osm_sa_slvl_record.h>
#include <opensm/osm_port.h>
#include <opensm/osm_node.h>
#include <vendor/osm_vendor_api.h>
#include <opensm/osm_helper.h>
#include <opensm/osm_pkey.h>

#define OSM_SLVL_REC_RCV_POOL_MIN_SIZE    32
#define OSM_SLVL_REC_RCV_POOL_GROW_SIZE   32

typedef struct _osm_slvl_item {
	cl_pool_item_t pool_item;
	ib_slvl_table_record_t rec;
} osm_slvl_item_t;

typedef struct _osm_slvl_search_ctxt {
	const ib_slvl_table_record_t *p_rcvd_rec;
	ib_net64_t comp_mask;
	uint8_t in_port_num;
	cl_qlist_t *p_list;
	osm_slvl_rec_rcv_t *p_rcv;
	const osm_physp_t *p_req_physp;
} osm_slvl_search_ctxt_t;

/**********************************************************************
 **********************************************************************/
void osm_slvl_rec_rcv_construct(IN osm_slvl_rec_rcv_t * const p_rcv)
{
	memset(p_rcv, 0, sizeof(*p_rcv));
	cl_qlock_pool_construct(&p_rcv->pool);
}

/**********************************************************************
 **********************************************************************/
void osm_slvl_rec_rcv_destroy(IN osm_slvl_rec_rcv_t * const p_rcv)
{
	OSM_LOG_ENTER(p_rcv->p_log, osm_slvl_rec_rcv_destroy);
	cl_qlock_pool_destroy(&p_rcv->pool);
	OSM_LOG_EXIT(p_rcv->p_log);
}

/**********************************************************************
 **********************************************************************/
ib_api_status_t
osm_slvl_rec_rcv_init(IN osm_slvl_rec_rcv_t * const p_rcv,
		      IN osm_sa_resp_t * const p_resp,
		      IN osm_mad_pool_t * const p_mad_pool,
		      IN const osm_subn_t * const p_subn,
		      IN osm_log_t * const p_log, IN cl_plock_t * const p_lock)
{
	ib_api_status_t status;

	OSM_LOG_ENTER(p_log, osm_slvl_rec_rcv_init);

	osm_slvl_rec_rcv_construct(p_rcv);

	p_rcv->p_log = p_log;
	p_rcv->p_subn = p_subn;
	p_rcv->p_lock = p_lock;
	p_rcv->p_resp = p_resp;
	p_rcv->p_mad_pool = p_mad_pool;

	/* used for matching records collection */
	status = cl_qlock_pool_init(&p_rcv->pool,
				    OSM_SLVL_REC_RCV_POOL_MIN_SIZE,
				    0,
				    OSM_SLVL_REC_RCV_POOL_GROW_SIZE,
				    sizeof(osm_slvl_item_t), NULL, NULL, NULL);

	OSM_LOG_EXIT(p_log);
	return (status);
}

/**********************************************************************
 **********************************************************************/
static void
__osm_sa_slvl_create(IN osm_slvl_rec_rcv_t * const p_rcv,
		     IN const osm_physp_t * const p_physp,
		     IN osm_slvl_search_ctxt_t * const p_ctxt,
		     IN uint8_t in_port_idx)
{
	osm_slvl_item_t *p_rec_item;
	uint16_t lid;
	ib_api_status_t status = IB_SUCCESS;

	OSM_LOG_ENTER(p_rcv->p_log, __osm_sa_slvl_create);

	p_rec_item = (osm_slvl_item_t *) cl_qlock_pool_get(&p_rcv->pool);
	if (p_rec_item == NULL) {
		osm_log(p_rcv->p_log, OSM_LOG_ERROR,
			"__osm_sa_slvl_create: ERR 2602: "
			"cl_qlock_pool_get failed\n");
		status = IB_INSUFFICIENT_RESOURCES;
		goto Exit;
	}

	if (p_physp->p_node->node_info.node_type != IB_NODE_TYPE_SWITCH)
		lid = p_physp->port_info.base_lid;
	else
		lid = osm_node_get_base_lid(p_physp->p_node, 0);

	if (osm_log_is_active(p_rcv->p_log, OSM_LOG_DEBUG))
		osm_log(p_rcv->p_log, OSM_LOG_DEBUG,
			"__osm_sa_slvl_create: "
			"New SLtoVL Map for: OUT port 0x%016" PRIx64
			", lid 0x%X, port 0x%X to In Port:%u\n",
			cl_ntoh64(osm_physp_get_port_guid(p_physp)),
			cl_ntoh16(lid), osm_physp_get_port_num(p_physp),
			in_port_idx);

	memset(&p_rec_item->rec, 0, sizeof(p_rec_item->rec));

	p_rec_item->rec.lid = lid;
	p_rec_item->rec.out_port_num = osm_physp_get_port_num(p_physp);
	p_rec_item->rec.in_port_num = in_port_idx;
	p_rec_item->rec.slvl_tbl =
	    *(osm_physp_get_slvl_tbl(p_physp, in_port_idx));

	cl_qlist_insert_tail(p_ctxt->p_list,
			     (cl_list_item_t *) & p_rec_item->pool_item);

      Exit:
	OSM_LOG_EXIT(p_rcv->p_log);
}

/**********************************************************************
 **********************************************************************/
static void
__osm_sa_slvl_by_comp_mask(IN osm_slvl_rec_rcv_t * const p_rcv,
			   IN const osm_port_t * const p_port,
			   osm_slvl_search_ctxt_t * const p_ctxt)
{
	const ib_slvl_table_record_t *p_rcvd_rec;
	ib_net64_t comp_mask;
	const osm_physp_t *p_out_physp, *p_in_physp;
	uint8_t in_port_num, out_port_num;
	uint8_t num_ports;
	uint8_t in_port_start, in_port_end;
	uint8_t out_port_start, out_port_end;
	const osm_physp_t *p_req_physp;

	OSM_LOG_ENTER(p_rcv->p_log, __osm_sa_slvl_by_comp_mask);

	p_rcvd_rec = p_ctxt->p_rcvd_rec;
	comp_mask = p_ctxt->comp_mask;
	num_ports = osm_node_get_num_physp(p_port->p_node);
	in_port_start = 0;
	in_port_end = num_ports;
	out_port_start = 0;
	out_port_end = num_ports;
	p_req_physp = p_ctxt->p_req_physp;

	if (p_port->p_node->node_info.node_type != IB_NODE_TYPE_SWITCH) {
		osm_log(p_rcv->p_log, OSM_LOG_DEBUG,
			"__osm_sa_slvl_by_comp_mask:  "
			"Using Physical Default Port Number: 0x%X (for End Node)\n",
			p_port->p_physp->port_num);
		p_out_physp = p_port->p_physp;
		/* check that the p_out_physp and the p_req_physp share a pkey */
		if (osm_physp_share_pkey
		    (p_rcv->p_log, p_req_physp, p_out_physp))
			__osm_sa_slvl_create(p_rcv, p_out_physp, p_ctxt, 0);
	} else {
		if (comp_mask & IB_SLVL_COMPMASK_OUT_PORT)
			out_port_start = out_port_end =
			    p_rcvd_rec->out_port_num;
		if (comp_mask & IB_SLVL_COMPMASK_IN_PORT)
			in_port_start = in_port_end = p_rcvd_rec->in_port_num;

		for (out_port_num = out_port_start;
		     out_port_num <= out_port_end; out_port_num++) {
			p_out_physp =
			    osm_node_get_physp_ptr(p_port->p_node,
						   out_port_num);
			if (!osm_physp_is_valid(p_out_physp))
				continue;

			for (in_port_num = in_port_start;
			     in_port_num <= in_port_end; in_port_num++) {
#if 0
				if (out_port_num && out_port_num == in_port_num)
					continue;
#endif

				p_in_physp =
				    osm_node_get_physp_ptr(p_port->p_node,
							   in_port_num);
				if (!osm_physp_is_valid(p_in_physp))
					continue;

				/* if the requester and the p_out_physp don't share a pkey -
				   continue */
				if (!osm_physp_share_pkey
				    (p_rcv->p_log, p_req_physp, p_out_physp))
					continue;

				__osm_sa_slvl_create(p_rcv, p_out_physp, p_ctxt,
						     in_port_num);
			}
		}
	}
	OSM_LOG_EXIT(p_rcv->p_log);
}

/**********************************************************************
 **********************************************************************/
static void
__osm_sa_slvl_by_comp_mask_cb(IN cl_map_item_t * const p_map_item,
			      IN void *context)
{
	const osm_port_t *const p_port = (osm_port_t *) p_map_item;
	osm_slvl_search_ctxt_t *const p_ctxt =
	    (osm_slvl_search_ctxt_t *) context;

	__osm_sa_slvl_by_comp_mask(p_ctxt->p_rcv, p_port, p_ctxt);
}

/**********************************************************************
 **********************************************************************/
void osm_slvl_rec_rcv_process(IN void *ctx, IN void *data)
{
	osm_slvl_rec_rcv_t *p_rcv = ctx;
	osm_madw_t *p_madw = data;
	const ib_sa_mad_t *p_rcvd_mad;
	const ib_slvl_table_record_t *p_rcvd_rec;
	const cl_ptr_vector_t *p_tbl;
	const osm_port_t *p_port = NULL;
	const ib_slvl_table_t *p_slvl_tbl;
	cl_qlist_t rec_list;
	osm_madw_t *p_resp_madw;
	ib_sa_mad_t *p_resp_sa_mad;
	ib_slvl_table_record_t *p_resp_rec;
	uint32_t num_rec, pre_trim_num_rec;
#ifndef VENDOR_RMPP_SUPPORT
	uint32_t trim_num_rec;
#endif
	uint32_t i;
	osm_slvl_search_ctxt_t context;
	osm_slvl_item_t *p_rec_item;
	ib_api_status_t status = IB_SUCCESS;
	ib_net64_t comp_mask;
	osm_physp_t *p_req_physp;

	CL_ASSERT(p_rcv);

	OSM_LOG_ENTER(p_rcv->p_log, osm_slvl_rec_rcv_process);

	CL_ASSERT(p_madw);

	p_rcvd_mad = osm_madw_get_sa_mad_ptr(p_madw);
	p_rcvd_rec =
	    (ib_slvl_table_record_t *) ib_sa_mad_get_payload_ptr(p_rcvd_mad);
	comp_mask = p_rcvd_mad->comp_mask;

	CL_ASSERT(p_rcvd_mad->attr_id == IB_MAD_ATTR_SLVL_RECORD);

	/* we only support SubnAdmGet and SubnAdmGetTable methods */
	if ((p_rcvd_mad->method != IB_MAD_METHOD_GET) &&
	    (p_rcvd_mad->method != IB_MAD_METHOD_GETTABLE)) {
		osm_log(p_rcv->p_log, OSM_LOG_ERROR,
			"osm_slvl_rec_rcv_process: ERR 2604: "
			"Unsupported Method (%s)\n",
			ib_get_sa_method_str(p_rcvd_mad->method));
		osm_sa_send_error(p_rcv->p_resp, p_madw,
				  IB_MAD_STATUS_UNSUP_METHOD_ATTR);
		goto Exit;
	}

	/* update the requester physical port. */
	p_req_physp = osm_get_physp_by_mad_addr(p_rcv->p_log,
						p_rcv->p_subn,
						osm_madw_get_mad_addr_ptr
						(p_madw));
	if (p_req_physp == NULL) {
		osm_log(p_rcv->p_log, OSM_LOG_ERROR,
			"osm_slvl_rec_rcv_process: ERR 2603: "
			"Cannot find requester physical port\n");
		goto Exit;
	}

	p_slvl_tbl = (ib_slvl_table_t *) ib_sa_mad_get_payload_ptr(p_rcvd_mad);

	cl_qlist_init(&rec_list);

	context.p_rcvd_rec = p_rcvd_rec;
	context.p_list = &rec_list;
	context.comp_mask = p_rcvd_mad->comp_mask;
	context.p_rcv = p_rcv;
	context.in_port_num = p_rcvd_rec->in_port_num;
	context.p_req_physp = p_req_physp;

	cl_plock_acquire(p_rcv->p_lock);

	osm_log(p_rcv->p_log, OSM_LOG_DEBUG,
		"osm_slvl_rec_rcv_process: "
		"Got Query Lid:0x%04X(%02X), In-Port:0x%02X(%02X), Out-Port:0x%02X(%02X)\n",
		cl_ntoh16(p_rcvd_rec->lid),
		(comp_mask & IB_SLVL_COMPMASK_LID) != 0,
		p_rcvd_rec->in_port_num,
		(comp_mask & IB_SLVL_COMPMASK_IN_PORT) != 0,
		p_rcvd_rec->out_port_num,
		(comp_mask & IB_SLVL_COMPMASK_OUT_PORT) != 0);

	/*
	   If the user specified a LID, it obviously narrows our
	   work load, since we don't have to search every port
	 */
	if (comp_mask & IB_SLVL_COMPMASK_LID) {

		p_tbl = &p_rcv->p_subn->port_lid_tbl;

		CL_ASSERT(cl_ptr_vector_get_size(p_tbl) < 0x10000);

		status =
		    osm_get_port_by_base_lid(p_rcv->p_subn, p_rcvd_rec->lid,
					     &p_port);
		if ((status != IB_SUCCESS) || (p_port == NULL)) {
			status = IB_NOT_FOUND;
			osm_log(p_rcv->p_log, OSM_LOG_ERROR,
				"osm_slvl_rec_rcv_process: ERR 2608: "
				"No port found with LID 0x%x\n",
				cl_ntoh16(p_rcvd_rec->lid));
		}
	}

	if (status == IB_SUCCESS) {
		/* if we have a unique port - no need for a port search */
		if (p_port)
			/*  this does the loop on all the port phys ports */
			__osm_sa_slvl_by_comp_mask(p_rcv, p_port, &context);
		else
			cl_qmap_apply_func(&p_rcv->p_subn->port_guid_tbl,
					   __osm_sa_slvl_by_comp_mask_cb,
					   &context);
	}

	cl_plock_release(p_rcv->p_lock);

	num_rec = cl_qlist_count(&rec_list);

	/*
	 * C15-0.1.30:
	 * If we do a SubnAdmGet and got more than one record it is an error !
	 */
	if (p_rcvd_mad->method == IB_MAD_METHOD_GET) {
		if (num_rec == 0) {
			osm_sa_send_error(p_rcv->p_resp, p_madw,
					  IB_SA_MAD_STATUS_NO_RECORDS);
			goto Exit;
		}
		if (num_rec > 1) {
			osm_log(p_rcv->p_log, OSM_LOG_ERROR,
				"osm_slvl_rec_rcv_process: ERR 2607: "
				"Got more than one record for SubnAdmGet (%u)\n",
				num_rec);
			osm_sa_send_error(p_rcv->p_resp, p_madw,
					  IB_SA_MAD_STATUS_TOO_MANY_RECORDS);

			/* need to set the mem free ... */
			p_rec_item =
			    (osm_slvl_item_t *) cl_qlist_remove_head(&rec_list);
			while (p_rec_item !=
			       (osm_slvl_item_t *) cl_qlist_end(&rec_list)) {
				cl_qlock_pool_put(&p_rcv->pool,
						  &p_rec_item->pool_item);
				p_rec_item =
				    (osm_slvl_item_t *)
				    cl_qlist_remove_head(&rec_list);
			}

			goto Exit;
		}
	}

	pre_trim_num_rec = num_rec;
#ifndef VENDOR_RMPP_SUPPORT
	trim_num_rec =
	    (MAD_BLOCK_SIZE -
	     IB_SA_MAD_HDR_SIZE) / sizeof(ib_slvl_table_record_t);
	if (trim_num_rec < num_rec) {
		osm_log(p_rcv->p_log, OSM_LOG_VERBOSE,
			"osm_slvl_rec_rcv_process: "
			"Number of records:%u trimmed to:%u to fit in one MAD\n",
			num_rec, trim_num_rec);
		num_rec = trim_num_rec;
	}
#endif

	osm_log(p_rcv->p_log, OSM_LOG_DEBUG,
		"osm_slvl_rec_rcv_process: " "Returning %u records\n", num_rec);

	if ((p_rcvd_mad->method == IB_MAD_METHOD_GET) && (num_rec == 0)) {
		osm_sa_send_error(p_rcv->p_resp, p_madw,
				  IB_SA_MAD_STATUS_NO_RECORDS);
		goto Exit;
	}

	/*
	 * Get a MAD to reply. Address of Mad is in the received mad_wrapper
	 */
	p_resp_madw = osm_mad_pool_get(p_rcv->p_mad_pool,
				       p_madw->h_bind,
				       num_rec *
				       sizeof(ib_slvl_table_record_t) +
				       IB_SA_MAD_HDR_SIZE, &p_madw->mad_addr);

	if (!p_resp_madw) {
		osm_log(p_rcv->p_log, OSM_LOG_ERROR,
			"osm_slvl_rec_rcv_process: ERR 2605: "
			"osm_mad_pool_get failed\n");

		for (i = 0; i < num_rec; i++) {
			p_rec_item =
			    (osm_slvl_item_t *) cl_qlist_remove_head(&rec_list);
			cl_qlock_pool_put(&p_rcv->pool, &p_rec_item->pool_item);
		}

		osm_sa_send_error(p_rcv->p_resp, p_madw,
				  IB_SA_MAD_STATUS_NO_RESOURCES);
		goto Exit;
	}

	p_resp_sa_mad = osm_madw_get_sa_mad_ptr(p_resp_madw);

	/*
	   Copy the MAD header back into the response mad.
	   Set the 'R' bit and the payload length,
	   Then copy all records from the list into the response payload.
	 */

	memcpy(p_resp_sa_mad, p_rcvd_mad, IB_SA_MAD_HDR_SIZE);
	p_resp_sa_mad->method |= IB_MAD_METHOD_RESP_MASK;
	/* C15-0.1.5 - always return SM_Key = 0 (table 185 p 884) */
	p_resp_sa_mad->sm_key = 0;

	/* Fill in the offset (paylen will be done by the rmpp SAR) */
	p_resp_sa_mad->attr_offset =
	    ib_get_attr_offset(sizeof(ib_slvl_table_record_t));

	p_resp_rec = (ib_slvl_table_record_t *)
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
		    (osm_slvl_item_t *) cl_qlist_remove_head(&rec_list);
		/* copy only if not trimmed */
		if (i < num_rec)
			*p_resp_rec = p_rec_item->rec;
		cl_qlock_pool_put(&p_rcv->pool, &p_rec_item->pool_item);
		p_resp_rec++;
	}

	CL_ASSERT(cl_is_qlist_empty(&rec_list));

	status = osm_vendor_send(p_resp_madw->h_bind, p_resp_madw, FALSE);
	if (status != IB_SUCCESS) {
		osm_log(p_rcv->p_log, OSM_LOG_ERROR,
			"osm_slvl_rec_rcv_process: ERR 2606: "
			"osm_vendor_send status = %s\n",
			ib_get_err_str(status));
		goto Exit;
	}

      Exit:
	OSM_LOG_EXIT(p_rcv->p_log);
}

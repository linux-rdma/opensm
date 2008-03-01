/*
 * Copyright (c) 2006-2007 Voltaire, Inc. All rights reserved.
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
 *    Implementation of osm_gir_rcv_t.
 * This object represents the GUIDInfoRecord Receiver object.
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
#include <complib/cl_qmap.h>
#include <complib/cl_passivelock.h>
#include <complib/cl_debug.h>
#include <complib/cl_qlist.h>
#include <vendor/osm_vendor_api.h>
#include <opensm/osm_port.h>
#include <opensm/osm_node.h>
#include <opensm/osm_helper.h>
#include <opensm/osm_pkey.h>
#include <opensm/osm_sa.h>

typedef struct _osm_gir_item {
	cl_list_item_t list_item;
	ib_guidinfo_record_t rec;
} osm_gir_item_t;

typedef struct _osm_gir_search_ctxt {
	const ib_guidinfo_record_t *p_rcvd_rec;
	ib_net64_t comp_mask;
	cl_qlist_t *p_list;
	osm_sa_t *sa;
	const osm_physp_t *p_req_physp;
} osm_gir_search_ctxt_t;

/**********************************************************************
 **********************************************************************/
static ib_api_status_t
__osm_gir_rcv_new_gir(IN osm_sa_t * sa,
		      IN const osm_node_t * const p_node,
		      IN cl_qlist_t * const p_list,
		      IN ib_net64_t const match_port_guid,
		      IN ib_net16_t const match_lid,
		      IN const osm_physp_t * const p_req_physp,
		      IN uint8_t const block_num)
{
	osm_gir_item_t *p_rec_item;
	ib_api_status_t status = IB_SUCCESS;

	OSM_LOG_ENTER(sa->p_log);

	p_rec_item = malloc(sizeof(*p_rec_item));
	if (p_rec_item == NULL) {
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 5102: "
			"rec_item alloc failed\n");
		status = IB_INSUFFICIENT_RESOURCES;
		goto Exit;
	}

	if (osm_log_is_active(sa->p_log, OSM_LOG_DEBUG)) {
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
			"New GUIDInfoRecord: lid 0x%X, block num %d\n",
			cl_ntoh16(match_lid), block_num);
	}

	memset(p_rec_item, 0, sizeof(*p_rec_item));

	p_rec_item->rec.lid = match_lid;
	p_rec_item->rec.block_num = block_num;
	if (!block_num)
		p_rec_item->rec.guid_info.guid[0] =
		    osm_physp_get_port_guid(p_req_physp);

	cl_qlist_insert_tail(p_list, &p_rec_item->list_item);

Exit:
	OSM_LOG_EXIT(sa->p_log);
	return (status);
}

/**********************************************************************
 **********************************************************************/
static void
__osm_sa_gir_create_gir(IN osm_sa_t * sa,
			IN osm_node_t * const p_node,
			IN cl_qlist_t * const p_list,
			IN ib_net64_t const match_port_guid,
			IN ib_net16_t const match_lid,
			IN const osm_physp_t * const p_req_physp,
			IN uint8_t const match_block_num)
{
	const osm_physp_t *p_physp;
	uint8_t port_num;
	uint8_t num_ports;
	uint16_t match_lid_ho;
	ib_net16_t base_lid_ho;
	ib_net16_t max_lid_ho;
	uint8_t lmc;
	ib_net64_t port_guid;
	uint8_t block_num, start_block_num, end_block_num, num_blocks;

	OSM_LOG_ENTER(sa->p_log);

	if (osm_log_is_active(sa->p_log, OSM_LOG_DEBUG)) {
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
			"Looking for GUIDRecord with LID: 0x%X GUID:0x%016"
			PRIx64 "\n", cl_ntoh16(match_lid),
			cl_ntoh64(match_port_guid)
		    );
	}

	/*
	   For switches, do not return the GUIDInfo record(s)
	   for each port on the switch, just for port 0.
	 */
	if (osm_node_get_type(p_node) == IB_NODE_TYPE_SWITCH)
		num_ports = 1;
	else
		num_ports = osm_node_get_num_physp(p_node);

	for (port_num = 0; port_num < num_ports; port_num++) {
		p_physp = osm_node_get_physp_ptr(p_node, port_num);
		if (!p_physp)
			continue;

		/* Check to see if the found p_physp and the requester physp
		   share a pkey. If not, continue */
		if (!osm_physp_share_pkey(sa->p_log, p_physp, p_req_physp))
			continue;

		port_guid = osm_physp_get_port_guid(p_physp);

		if (match_port_guid && (port_guid != match_port_guid))
			continue;

		/*
		   Note: the following check is a temporary workaround
		   Since 1. GUIDCap should never be 0 on ports where this applies
		   and   2. GUIDCap should not be used on ports where it doesn't apply
		   So this should really be a check for whether the port is a
		   switch external port or not!
		 */
		if (p_physp->port_info.guid_cap == 0)
			continue;

		num_blocks = p_physp->port_info.guid_cap / 8;
		if (p_physp->port_info.guid_cap % 8)
			num_blocks++;
		if (match_block_num == 255) {
			start_block_num = 0;
			end_block_num = num_blocks - 1;
		} else {
			if (match_block_num >= num_blocks)
				continue;
			end_block_num = start_block_num = match_block_num;
		}

		base_lid_ho = cl_ntoh16(osm_physp_get_base_lid(p_physp));
		match_lid_ho = cl_ntoh16(match_lid);
		if (match_lid_ho) {
			lmc = osm_physp_get_lmc(p_physp);
			max_lid_ho = (uint16_t) (base_lid_ho + (1 << lmc) - 1);

			/*
			   We validate that the lid belongs to this node.
			 */
			if (osm_log_is_active(sa->p_log, OSM_LOG_DEBUG)) {
				OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
					"Comparing LID: 0x%X <= 0x%X <= 0x%X\n",
					base_lid_ho, match_lid_ho, max_lid_ho);
			}

			if (match_lid_ho < base_lid_ho
			    || match_lid_ho > max_lid_ho)
				continue;
		}

		for (block_num = start_block_num; block_num <= end_block_num;
		     block_num++)
			__osm_gir_rcv_new_gir(sa, p_node, p_list, port_guid,
					      cl_ntoh16(base_lid_ho), p_physp,
					      block_num);

	}

	OSM_LOG_EXIT(sa->p_log);
}

/**********************************************************************
 **********************************************************************/
static void
__osm_sa_gir_by_comp_mask_cb(IN cl_map_item_t * const p_map_item,
			     IN void *context)
{
	const osm_gir_search_ctxt_t *const p_ctxt =
	    (osm_gir_search_ctxt_t *) context;
	osm_node_t *const p_node = (osm_node_t *) p_map_item;
	const ib_guidinfo_record_t *const p_rcvd_rec = p_ctxt->p_rcvd_rec;
	const osm_physp_t *const p_req_physp = p_ctxt->p_req_physp;
	osm_sa_t *sa = p_ctxt->sa;
	const ib_guid_info_t *p_comp_gi;
	ib_net64_t const comp_mask = p_ctxt->comp_mask;
	ib_net64_t match_port_guid = 0;
	ib_net16_t match_lid = 0;
	uint8_t match_block_num = 255;

	OSM_LOG_ENTER(p_ctxt->sa->p_log);

	if (comp_mask & IB_GIR_COMPMASK_LID)
		match_lid = p_rcvd_rec->lid;

	if (comp_mask & IB_GIR_COMPMASK_BLOCKNUM)
		match_block_num = p_rcvd_rec->block_num;

	p_comp_gi = &p_rcvd_rec->guid_info;
	/* Different rule for block 0 v. other blocks */
	if (comp_mask & IB_GIR_COMPMASK_GID0) {
		if (!p_rcvd_rec->block_num)
			match_port_guid = osm_physp_get_port_guid(p_req_physp);
		if (p_comp_gi->guid[0] != match_port_guid)
			goto Exit;
	}

	if (comp_mask & IB_GIR_COMPMASK_GID1) {
		if (p_comp_gi->guid[1] != 0)
			goto Exit;
	}

	if (comp_mask & IB_GIR_COMPMASK_GID2) {
		if (p_comp_gi->guid[2] != 0)
			goto Exit;
	}

	if (comp_mask & IB_GIR_COMPMASK_GID3) {
		if (p_comp_gi->guid[3] != 0)
			goto Exit;
	}

	if (comp_mask & IB_GIR_COMPMASK_GID4) {
		if (p_comp_gi->guid[4] != 0)
			goto Exit;
	}

	if (comp_mask & IB_GIR_COMPMASK_GID5) {
		if (p_comp_gi->guid[5] != 0)
			goto Exit;
	}

	if (comp_mask & IB_GIR_COMPMASK_GID6) {
		if (p_comp_gi->guid[6] != 0)
			goto Exit;
	}

	if (comp_mask & IB_GIR_COMPMASK_GID7) {
		if (p_comp_gi->guid[7] != 0)
			goto Exit;
	}

	__osm_sa_gir_create_gir(sa, p_node, p_ctxt->p_list,
				match_port_guid, match_lid, p_req_physp,
				match_block_num);

Exit:
	OSM_LOG_EXIT(p_ctxt->sa->p_log);
}

/**********************************************************************
 **********************************************************************/
void osm_gir_rcv_process(IN void *ctx, IN void *data)
{
	osm_sa_t *sa = ctx;
	osm_madw_t *p_madw = data;
	const ib_sa_mad_t *p_rcvd_mad;
	const ib_guidinfo_record_t *p_rcvd_rec;
	cl_qlist_t rec_list;
	osm_madw_t *p_resp_madw;
	ib_sa_mad_t *p_resp_sa_mad;
	ib_guidinfo_record_t *p_resp_rec;
	uint32_t num_rec, pre_trim_num_rec;
#ifndef VENDOR_RMPP_SUPPORT
	uint32_t trim_num_rec;
#endif
	uint32_t i;
	osm_gir_search_ctxt_t context;
	osm_gir_item_t *p_rec_item;
	osm_physp_t *p_req_physp;

	CL_ASSERT(sa);

	OSM_LOG_ENTER(sa->p_log);

	CL_ASSERT(p_madw);

	p_rcvd_mad = osm_madw_get_sa_mad_ptr(p_madw);
	p_rcvd_rec =
	    (ib_guidinfo_record_t *) ib_sa_mad_get_payload_ptr(p_rcvd_mad);

	CL_ASSERT(p_rcvd_mad->attr_id == IB_MAD_ATTR_GUIDINFO_RECORD);

	/* we only support SubnAdmGet and SubnAdmGetTable methods */
	if ((p_rcvd_mad->method != IB_MAD_METHOD_GET) &&
	    (p_rcvd_mad->method != IB_MAD_METHOD_GETTABLE)) {
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 5105: "
			"Unsupported Method (%s)\n",
			ib_get_sa_method_str(p_rcvd_mad->method));
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
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 5104: "
			"Cannot find requester physical port\n");
		goto Exit;
	}

	if (osm_log_is_active(sa->p_log, OSM_LOG_DEBUG))
		osm_dump_guidinfo_record(sa->p_log, p_rcvd_rec,
					 OSM_LOG_DEBUG);

	cl_qlist_init(&rec_list);

	context.p_rcvd_rec = p_rcvd_rec;
	context.p_list = &rec_list;
	context.comp_mask = p_rcvd_mad->comp_mask;
	context.sa = sa;
	context.p_req_physp = p_req_physp;

	cl_plock_acquire(sa->p_lock);

	cl_qmap_apply_func(&sa->p_subn->node_guid_tbl,
			   __osm_sa_gir_by_comp_mask_cb, &context);

	cl_plock_release(sa->p_lock);

	num_rec = cl_qlist_count(&rec_list);

	/*
	 * C15-0.1.30:
	 * If we do a SubnAdmGet and got more than one record it is an error !
	 */
	if (p_rcvd_mad->method == IB_MAD_METHOD_GET) {
		if (num_rec == 0) {
			osm_sa_send_error(sa, p_madw,
					  IB_SA_MAD_STATUS_NO_RECORDS);
			goto Exit;
		}
		if (num_rec > 1) {
			OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 5103: "
				"Got more than one record for SubnAdmGet (%u)\n",
				num_rec);
			osm_sa_send_error(sa, p_madw,
					  IB_SA_MAD_STATUS_TOO_MANY_RECORDS);

			/* need to set the mem free ... */
			p_rec_item =
			    (osm_gir_item_t *) cl_qlist_remove_head(&rec_list);
			while (p_rec_item !=
			       (osm_gir_item_t *) cl_qlist_end(&rec_list)) {
				free(p_rec_item);
				p_rec_item = (osm_gir_item_t *)
				    cl_qlist_remove_head(&rec_list);
			}

			goto Exit;
		}
	}

	pre_trim_num_rec = num_rec;
#ifndef VENDOR_RMPP_SUPPORT
	trim_num_rec =
	    (MAD_BLOCK_SIZE -
	     IB_SA_MAD_HDR_SIZE) / sizeof(ib_guidinfo_record_t);
	if (trim_num_rec < num_rec) {
		OSM_LOG(sa->p_log, OSM_LOG_VERBOSE,
			"Number of records:%u trimmed to:%u to fit in one MAD\n",
			num_rec, trim_num_rec);
		num_rec = trim_num_rec;
	}
#endif

	OSM_LOG(sa->p_log, OSM_LOG_DEBUG, "Returning %u records\n", num_rec);

	if ((p_rcvd_mad->method == IB_MAD_METHOD_GET) && (num_rec == 0)) {
		osm_sa_send_error(sa, p_madw,
				  IB_SA_MAD_STATUS_NO_RECORDS);
		goto Exit;
	}

	/*
	 * Get a MAD to reply. Address of Mad is in the received mad_wrapper
	 */
	p_resp_madw = osm_mad_pool_get(sa->p_mad_pool,
				       p_madw->h_bind,
				       num_rec * sizeof(ib_guidinfo_record_t) +
				       IB_SA_MAD_HDR_SIZE, &p_madw->mad_addr);

	if (!p_resp_madw) {
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 5106: "
			"osm_mad_pool_get failed\n");

		for (i = 0; i < num_rec; i++) {
			p_rec_item =
			    (osm_gir_item_t *) cl_qlist_remove_head(&rec_list);
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

	memcpy(p_resp_sa_mad, p_rcvd_mad, IB_SA_MAD_HDR_SIZE);
	p_resp_sa_mad->method |= IB_MAD_METHOD_RESP_MASK;
	/* C15-0.1.5 - always return SM_Key = 0 (table 185 p 884) */
	p_resp_sa_mad->sm_key = 0;
	/* Fill in the offset (paylen will be done by the rmpp SAR) */
	p_resp_sa_mad->attr_offset =
	    ib_get_attr_offset(sizeof(ib_guidinfo_record_t));

	p_resp_rec = (ib_guidinfo_record_t *)
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
		p_rec_item = (osm_gir_item_t *) cl_qlist_remove_head(&rec_list);
		/* copy only if not trimmed */
		if (i < num_rec)
			*p_resp_rec = p_rec_item->rec;
		free(p_rec_item);
		p_resp_rec++;
	}

	CL_ASSERT(cl_is_qlist_empty(&rec_list));

	osm_sa_vendor_send(p_resp_madw->h_bind, p_resp_madw, FALSE, sa->p_subn);

Exit:
	OSM_LOG_EXIT(sa->p_log);
}

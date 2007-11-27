/*
 * Copyright (c) 2004-2007 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2006 Mellanox Technologies LTD. All rights reserved.
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
 *    Implementation of osm_sr_rcv_t.
 * This object represents the ServiceRecord Receiver object.
 * This object is part of the opensm family of objects.
 *
 * Environment:
 *    Linux User Mode
 *
 * $Revision: 1.9 $
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
#include <opensm/osm_sa_service_record.h>
#include <opensm/osm_port.h>
#include <opensm/osm_node.h>
#include <opensm/osm_switch.h>
#include <vendor/osm_vendor.h>
#include <vendor/osm_vendor_api.h>
#include <opensm/osm_helper.h>
#include <opensm/osm_sa.h>
#include <opensm/osm_service.h>
#include <opensm/osm_pkey.h>

typedef struct _osm_sr_item {
	cl_list_item_t list_item;
	ib_service_record_t service_rec;
} osm_sr_item_t;

typedef struct osm_sr_match_item {
	cl_qlist_t sr_list;
	ib_service_record_t *p_service_rec;
	ib_net64_t comp_mask;
	osm_sr_rcv_t *p_rcv;
} osm_sr_match_item_t;

typedef struct _osm_sr_search_ctxt {
	osm_sr_match_item_t *p_sr_item;
	const osm_physp_t *p_req_physp;
} osm_sr_search_ctxt_t;

/**********************************************************************
 **********************************************************************/
void osm_sr_rcv_construct(IN osm_sr_rcv_t * const p_rcv)
{
	memset(p_rcv, 0, sizeof(*p_rcv));
	cl_timer_construct(&p_rcv->sr_timer);
}

/**********************************************************************
 **********************************************************************/
void osm_sr_rcv_destroy(IN osm_sr_rcv_t * const p_rcv)
{
	OSM_LOG_ENTER(p_rcv->p_log, osm_sr_rcv_destroy);
	cl_timer_trim(&p_rcv->sr_timer, 1);
	cl_timer_destroy(&p_rcv->sr_timer);
	OSM_LOG_EXIT(p_rcv->p_log);
}

/**********************************************************************
 **********************************************************************/
ib_api_status_t
osm_sr_rcv_init(IN osm_sr_rcv_t * const p_rcv,
		IN osm_sa_resp_t * const p_resp,
		IN osm_mad_pool_t * const p_mad_pool,
		IN osm_subn_t * const p_subn,
		IN osm_log_t * const p_log, IN cl_plock_t * const p_lock)
{
	ib_api_status_t status;

	OSM_LOG_ENTER(p_log, osm_sr_rcv_init);

	osm_sr_rcv_construct(p_rcv);

	p_rcv->p_log = p_log;
	p_rcv->p_subn = p_subn;
	p_rcv->p_lock = p_lock;
	p_rcv->p_resp = p_resp;
	p_rcv->p_mad_pool = p_mad_pool;

	status = cl_timer_init(&p_rcv->sr_timer, osm_sr_rcv_lease_cb, p_rcv);

	OSM_LOG_EXIT(p_rcv->p_log);
	return (status);
}

/**********************************************************************
 **********************************************************************/
static boolean_t
__match_service_pkey_with_ports_pkey(IN osm_sr_rcv_t * const p_rcv,
				     IN const osm_madw_t * const p_madw,
				     ib_service_record_t * const p_service_rec,
				     ib_net64_t const comp_mask)
{
	boolean_t valid = TRUE;
	osm_physp_t *p_req_physp;
	ib_net64_t service_guid;
	osm_port_t *service_port;

	/* update the requester physical port. */
	p_req_physp = osm_get_physp_by_mad_addr(p_rcv->p_log,
						p_rcv->p_subn,
						osm_madw_get_mad_addr_ptr
						(p_madw));
	if (p_req_physp == NULL) {
		osm_log(p_rcv->p_log, OSM_LOG_ERROR,
			"__match_service_pkey_with_ports_pkey: ERR 2404: "
			"Cannot find requester physical port\n");
		valid = FALSE;
		goto Exit;
	}

	if ((comp_mask & IB_SR_COMPMASK_SPKEY) == IB_SR_COMPMASK_SPKEY) {
		/* We have a ServiceP_Key - check matching on requester port, and
		   ServiceGid port (if such exists) */
		/* Make sure it matches the p_req_physp */
		if (!osm_physp_has_pkey
		    (p_rcv->p_log, p_service_rec->service_pkey, p_req_physp)) {
			valid = FALSE;
			goto Exit;
		}

		/* Make sure it matches the port of the ServiceGid */
		if ((comp_mask & IB_SR_COMPMASK_SGID) == IB_SR_COMPMASK_SGID) {
			service_guid =
			    p_service_rec->service_gid.unicast.interface_id;
			service_port =
			    osm_get_port_by_guid(p_rcv->p_subn, service_guid);
			if (!service_port) {
				osm_log(p_rcv->p_log, OSM_LOG_ERROR,
					"__match_service_pkey_with_ports_pkey: ERR 2405: "
					"No port object for port 0x%016" PRIx64
					"\n", cl_ntoh64(service_guid));
				valid = FALSE;
				goto Exit;
			}
			/* check on the table of the default physical port of the service port */
			if (!osm_physp_has_pkey(p_rcv->p_log,
						p_service_rec->service_pkey,
						service_port->p_physp)) {
				valid = FALSE;
				goto Exit;
			}
		}
	}

      Exit:
	return valid;
}

/**********************************************************************
 **********************************************************************/
static boolean_t
__match_name_to_key_association(IN osm_sr_rcv_t * const p_rcv,
				ib_service_record_t * p_service_rec,
				ib_net64_t comp_mask)
{
	UNUSED_PARAM(p_service_rec);
	UNUSED_PARAM(p_rcv);

	if ((comp_mask & (IB_SR_COMPMASK_SKEY | IB_SR_COMPMASK_SNAME)) ==
	    (IB_SR_COMPMASK_SKEY | IB_SR_COMPMASK_SNAME)) {
		/* For now, we are not maintaining the ServiceAssociation record
		 * so just return TRUE
		 */
		return TRUE;
	}

	return TRUE;
}

/**********************************************************************
 **********************************************************************/
static boolean_t
__validate_sr(IN osm_sr_rcv_t * const p_rcv, IN const osm_madw_t * const p_madw)
{
	boolean_t valid = TRUE;
	ib_sa_mad_t *p_sa_mad;
	ib_service_record_t *p_recvd_service_rec;

	OSM_LOG_ENTER(p_rcv->p_log, __validate_sr);

	p_sa_mad = osm_madw_get_sa_mad_ptr(p_madw);
	p_recvd_service_rec =
	    (ib_service_record_t *) ib_sa_mad_get_payload_ptr(p_sa_mad);

	valid = __match_service_pkey_with_ports_pkey(p_rcv,
						     p_madw,
						     p_recvd_service_rec,
						     p_sa_mad->comp_mask);

	if (!valid) {
		osm_log(p_rcv->p_log, OSM_LOG_DEBUG,
			"__validate_sr: " "No Match for Service Pkey\n");
		valid = FALSE;
		goto Exit;
	}

	valid = __match_name_to_key_association(p_rcv,
						p_recvd_service_rec,
						p_sa_mad->comp_mask);

	if (!valid) {
		osm_log(p_rcv->p_log, OSM_LOG_DEBUG,
			"__validate_sr: "
			"Service Record Name to key matching failed\n");
		valid = FALSE;
		goto Exit;
	}

      Exit:
	OSM_LOG_EXIT(p_rcv->p_log);
	return valid;
}

/**********************************************************************
 **********************************************************************/
static void
__osm_sr_rcv_respond(IN osm_sr_rcv_t * const p_rcv,
		     IN const osm_madw_t * const p_madw,
		     IN cl_qlist_t * const p_list)
{
	osm_madw_t *p_resp_madw;
	const ib_sa_mad_t *p_sa_mad;
	ib_sa_mad_t *p_resp_sa_mad;
	uint32_t num_rec, num_copied;
#ifndef VENDOR_RMPP_SUPPORT
	uint32_t trim_num_rec;
#endif
	ib_service_record_t *p_resp_sr;
	ib_api_status_t status;
	osm_sr_item_t *p_sr_item;
	const ib_sa_mad_t *p_rcvd_mad = osm_madw_get_sa_mad_ptr(p_madw);
	boolean_t trusted_req = TRUE;

	OSM_LOG_ENTER(p_rcv->p_log, __osm_sr_rcv_respond);

	num_rec = cl_qlist_count(p_list);

	/*
	 * C15-0.1.30:
	 * If we do a SubnAdmGet and got more than one record it is an error !
	 */
	if ((p_rcvd_mad->method == IB_MAD_METHOD_GET) && (num_rec > 1)) {
		osm_log(p_rcv->p_log, OSM_LOG_ERROR,
			"__osm_sr_rcv_respond: ERR 2406: "
			"Got more than one record for SubnAdmGet (%u).\n",
			num_rec);
		osm_sa_send_error(p_rcv->p_resp, p_madw,
				  IB_SA_MAD_STATUS_TOO_MANY_RECORDS);

		/* need to set the mem free ... */
		p_sr_item = (osm_sr_item_t *) cl_qlist_remove_head(p_list);
		while (p_sr_item != (osm_sr_item_t *) cl_qlist_end(p_list)) {
			free(p_sr_item);
			p_sr_item =
			    (osm_sr_item_t *) cl_qlist_remove_head(p_list);
		}

		goto Exit;
	}
#ifndef VENDOR_RMPP_SUPPORT
	trim_num_rec =
	    (MAD_BLOCK_SIZE - IB_SA_MAD_HDR_SIZE) / sizeof(ib_service_record_t);
	if (trim_num_rec < num_rec) {
		osm_log(p_rcv->p_log, OSM_LOG_VERBOSE,
			"__osm_sr_rcv_respond: "
			"Number of records:%u trimmed to:%u to fit in one MAD\n",
			num_rec, trim_num_rec);
		num_rec = trim_num_rec;
	}
#endif

	if (osm_log_is_active(p_rcv->p_log, OSM_LOG_DEBUG)) {
		osm_log(p_rcv->p_log, OSM_LOG_DEBUG,
			"__osm_sr_rcv_respond: "
			"Generating response with %u records\n", num_rec);
	}

	/*
	   Get a MAD to reply. Address of Mad is in the received mad_wrapper
	 */
	p_resp_madw = osm_mad_pool_get(p_rcv->p_mad_pool,
				       p_madw->h_bind,
				       num_rec * sizeof(ib_service_record_t) +
				       IB_SA_MAD_HDR_SIZE, &p_madw->mad_addr);
	if (!p_resp_madw) {
		osm_log(p_rcv->p_log, OSM_LOG_ERROR,
			"__osm_sr_rcv_respond: ERR 2402: "
			"Unable to allocate MAD\n");
		/* Release the quick pool items */
		p_sr_item = (osm_sr_item_t *) cl_qlist_remove_head(p_list);
		while (p_sr_item != (osm_sr_item_t *) cl_qlist_end(p_list)) {
			free(p_sr_item);
			p_sr_item =
			    (osm_sr_item_t *) cl_qlist_remove_head(p_list);
		}

		goto Exit;
	}

	p_sa_mad = osm_madw_get_sa_mad_ptr(p_madw);
	p_resp_sa_mad = osm_madw_get_sa_mad_ptr(p_resp_madw);

	memcpy(p_resp_sa_mad, p_sa_mad, IB_SA_MAD_HDR_SIZE);

	/* but what if it was a SET ? setting the response bit is not enough */
	if (p_rcvd_mad->method == IB_MAD_METHOD_SET) {
		p_resp_sa_mad->method = IB_MAD_METHOD_GET;
	}
	p_resp_sa_mad->method |= IB_MAD_METHOD_RESP_MASK;
	/* C15-0.1.5 - always return SM_Key = 0 (table 185 p 884) */
	p_resp_sa_mad->sm_key = 0;

	/* Fill in the offset (paylen will be done by the rmpp SAR) */
	p_resp_sa_mad->attr_offset =
	    ib_get_attr_offset(sizeof(ib_service_record_t));

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

	p_resp_sr =
	    (ib_service_record_t *) ib_sa_mad_get_payload_ptr(p_resp_sa_mad);

	if ((p_resp_sa_mad->method != IB_MAD_METHOD_GETTABLE_RESP) &&
	    (num_rec == 0)) {
		p_resp_sa_mad->status = IB_SA_MAD_STATUS_NO_RECORDS;
		memset(p_resp_sr, 0, sizeof(*p_resp_sr));
	} else {
		/*
		   p923 - The ServiceKey shall be set to 0, except in the case of a trusted
		   request.
		   Note: In the mad controller we check that the SM_Key received on
		   the mad is valid. Meaning - is either zero or equal to the local
		   sm_key.
		 */
		if (p_sa_mad->sm_key == 0)
			trusted_req = FALSE;

		p_sr_item = (osm_sr_item_t *) cl_qlist_remove_head(p_list);

		/* we need to track the number of copied items so we can
		 * stop the copy - but clear them all
		 */
		num_copied = 0;
		while (p_sr_item != (osm_sr_item_t *) cl_qlist_end(p_list)) {
			/*  Copy the Link Records from the list into the MAD */
			if (num_copied < num_rec) {
				*p_resp_sr = p_sr_item->service_rec;
				if (trusted_req == FALSE)
					memset(p_resp_sr->service_key, 0,
					       sizeof(p_resp_sr->service_key));

				num_copied++;
			}
			free(p_sr_item);
			p_resp_sr++;
			p_sr_item =
			    (osm_sr_item_t *) cl_qlist_remove_head(p_list);
		}
	}

	status = osm_sa_vendor_send(p_resp_madw->h_bind, p_resp_madw, FALSE,
				    p_rcv->p_subn);

	if (status != IB_SUCCESS) {
		osm_log(p_rcv->p_log, OSM_LOG_ERROR,
			"__osm_sr_rcv_respond: ERR 2407: "
			"Unable to send MAD (%s)\n", ib_get_err_str(status));
		/*  osm_mad_pool_put( p_rcv->p_mad_pool, p_resp_madw ); */
		goto Exit;
	}

      Exit:
	OSM_LOG_EXIT(p_rcv->p_log);
}

/**********************************************************************
 **********************************************************************/
static void
__get_matching_sr(IN cl_list_item_t * const p_list_item, IN void *context)
{
	osm_sr_search_ctxt_t *const p_ctxt = (osm_sr_search_ctxt_t *) context;
	osm_svcr_t *p_svcr = (osm_svcr_t *) p_list_item;
	osm_sr_item_t *p_sr_pool_item;
	osm_sr_match_item_t *p_sr_item = p_ctxt->p_sr_item;
	ib_net64_t comp_mask = p_sr_item->comp_mask;
	const osm_physp_t *p_req_physp = p_ctxt->p_req_physp;

	if ((comp_mask & IB_SR_COMPMASK_SID) == IB_SR_COMPMASK_SID) {
		if (p_sr_item->p_service_rec->service_id !=
		    p_svcr->service_record.service_id)
			return;
	}
	if ((comp_mask & IB_SR_COMPMASK_SGID) == IB_SR_COMPMASK_SGID) {
		if (memcmp(&p_sr_item->p_service_rec->service_gid,
			   &p_svcr->service_record.service_gid,
			   sizeof(p_svcr->service_record.service_gid)) != 0)
			return;
	}
	if ((comp_mask & IB_SR_COMPMASK_SPKEY) == IB_SR_COMPMASK_SPKEY) {
		if (p_sr_item->p_service_rec->service_pkey !=
		    p_svcr->service_record.service_pkey)
			return;
	}

	if ((comp_mask & IB_SR_COMPMASK_SKEY) == IB_SR_COMPMASK_SKEY) {
		if (memcmp(p_sr_item->p_service_rec->service_key,
			   p_svcr->service_record.service_key,
			   16 * sizeof(uint8_t)))
			return;
	}
	if ((comp_mask & IB_SR_COMPMASK_SNAME) == IB_SR_COMPMASK_SNAME) {
		if (memcmp(p_sr_item->p_service_rec->service_name,
			   p_svcr->service_record.service_name,
			   sizeof(p_svcr->service_record.service_name)) != 0)
			return;
	}
	if ((comp_mask & IB_SR_COMPMASK_SDATA8_0) == IB_SR_COMPMASK_SDATA8_0) {
		if (p_sr_item->p_service_rec->service_data8[0] !=
		    p_svcr->service_record.service_data8[0])
			return;
	}

	if ((comp_mask & IB_SR_COMPMASK_SDATA8_1) == IB_SR_COMPMASK_SDATA8_1) {
		if (p_sr_item->p_service_rec->service_data8[1] !=
		    p_svcr->service_record.service_data8[1])
			return;
	}
	if ((comp_mask & IB_SR_COMPMASK_SDATA8_2) == IB_SR_COMPMASK_SDATA8_2) {
		if (p_sr_item->p_service_rec->service_data8[2] !=
		    p_svcr->service_record.service_data8[2])
			return;
	}
	if ((comp_mask & IB_SR_COMPMASK_SDATA8_3) == IB_SR_COMPMASK_SDATA8_3) {
		if (p_sr_item->p_service_rec->service_data8[3] !=
		    p_svcr->service_record.service_data8[3])
			return;
	}
	if ((comp_mask & IB_SR_COMPMASK_SDATA8_4) == IB_SR_COMPMASK_SDATA8_4) {
		if (p_sr_item->p_service_rec->service_data8[4] !=
		    p_svcr->service_record.service_data8[4])
			return;
	}
	if ((comp_mask & IB_SR_COMPMASK_SDATA8_5) == IB_SR_COMPMASK_SDATA8_5) {
		if (p_sr_item->p_service_rec->service_data8[5] !=
		    p_svcr->service_record.service_data8[5])
			return;
	}
	if ((comp_mask & IB_SR_COMPMASK_SDATA8_6) == IB_SR_COMPMASK_SDATA8_6) {
		if (p_sr_item->p_service_rec->service_data8[6] !=
		    p_svcr->service_record.service_data8[6])
			return;
	}

	if ((comp_mask & IB_SR_COMPMASK_SDATA8_7) == IB_SR_COMPMASK_SDATA8_7) {
		if (p_sr_item->p_service_rec->service_data8[7] !=
		    p_svcr->service_record.service_data8[7])
			return;
	}

	if ((comp_mask & IB_SR_COMPMASK_SDATA8_8) == IB_SR_COMPMASK_SDATA8_8) {
		if (p_sr_item->p_service_rec->service_data8[8] !=
		    p_svcr->service_record.service_data8[8])
			return;
	}

	if ((comp_mask & IB_SR_COMPMASK_SDATA8_9) == IB_SR_COMPMASK_SDATA8_9) {
		if (p_sr_item->p_service_rec->service_data8[9] !=
		    p_svcr->service_record.service_data8[9])
			return;
	}

	if ((comp_mask & IB_SR_COMPMASK_SDATA8_10) == IB_SR_COMPMASK_SDATA8_10) {
		if (p_sr_item->p_service_rec->service_data8[10] !=
		    p_svcr->service_record.service_data8[10])
			return;
	}

	if ((comp_mask & IB_SR_COMPMASK_SDATA8_11) == IB_SR_COMPMASK_SDATA8_11) {
		if (p_sr_item->p_service_rec->service_data8[11] !=
		    p_svcr->service_record.service_data8[11])
			return;
	}

	if ((comp_mask & IB_SR_COMPMASK_SDATA8_12) == IB_SR_COMPMASK_SDATA8_12) {
		if (p_sr_item->p_service_rec->service_data8[12] !=
		    p_svcr->service_record.service_data8[12])
			return;
	}
	if ((comp_mask & IB_SR_COMPMASK_SDATA8_13) == IB_SR_COMPMASK_SDATA8_13) {
		if (p_sr_item->p_service_rec->service_data8[13] !=
		    p_svcr->service_record.service_data8[13])
			return;
	}
	if ((comp_mask & IB_SR_COMPMASK_SDATA8_14) == IB_SR_COMPMASK_SDATA8_14) {
		if (p_sr_item->p_service_rec->service_data8[14] !=
		    p_svcr->service_record.service_data8[14])
			return;
	}
	if ((comp_mask & IB_SR_COMPMASK_SDATA8_15) == IB_SR_COMPMASK_SDATA8_15) {
		if (p_sr_item->p_service_rec->service_data8[15] !=
		    p_svcr->service_record.service_data8[15])
			return;
	}
	if ((comp_mask & IB_SR_COMPMASK_SDATA16_0) == IB_SR_COMPMASK_SDATA16_0) {
		if (p_sr_item->p_service_rec->service_data16[0] !=
		    p_svcr->service_record.service_data16[0])
			return;
	}
	if ((comp_mask & IB_SR_COMPMASK_SDATA16_1) == IB_SR_COMPMASK_SDATA16_1) {
		if (p_sr_item->p_service_rec->service_data16[1] !=
		    p_svcr->service_record.service_data16[1])
			return;
	}
	if ((comp_mask & IB_SR_COMPMASK_SDATA16_2) == IB_SR_COMPMASK_SDATA16_2) {
		if (p_sr_item->p_service_rec->service_data16[2] !=
		    p_svcr->service_record.service_data16[2])
			return;
	}
	if ((comp_mask & IB_SR_COMPMASK_SDATA16_3) == IB_SR_COMPMASK_SDATA16_3) {
		if (p_sr_item->p_service_rec->service_data16[3] !=
		    p_svcr->service_record.service_data16[3])
			return;
	}
	if ((comp_mask & IB_SR_COMPMASK_SDATA16_4) == IB_SR_COMPMASK_SDATA16_4) {
		if (p_sr_item->p_service_rec->service_data16[4] !=
		    p_svcr->service_record.service_data16[4])
			return;
	}
	if ((comp_mask & IB_SR_COMPMASK_SDATA16_5) == IB_SR_COMPMASK_SDATA16_5) {
		if (p_sr_item->p_service_rec->service_data16[5] !=
		    p_svcr->service_record.service_data16[5])
			return;
	}
	if ((comp_mask & IB_SR_COMPMASK_SDATA16_6) == IB_SR_COMPMASK_SDATA16_6) {
		if (p_sr_item->p_service_rec->service_data16[6] !=
		    p_svcr->service_record.service_data16[6])
			return;
	}
	if ((comp_mask & IB_SR_COMPMASK_SDATA16_7) == IB_SR_COMPMASK_SDATA16_7) {
		if (p_sr_item->p_service_rec->service_data16[7] !=
		    p_svcr->service_record.service_data16[7])
			return;
	}
	if ((comp_mask & IB_SR_COMPMASK_SDATA32_0) == IB_SR_COMPMASK_SDATA32_0) {
		if (p_sr_item->p_service_rec->service_data32[0] !=
		    p_svcr->service_record.service_data32[0])
			return;
	}
	if ((comp_mask & IB_SR_COMPMASK_SDATA32_1) == IB_SR_COMPMASK_SDATA32_1) {
		if (p_sr_item->p_service_rec->service_data32[1] !=
		    p_svcr->service_record.service_data32[1])
			return;
	}
	if ((comp_mask & IB_SR_COMPMASK_SDATA32_2) == IB_SR_COMPMASK_SDATA32_2) {
		if (p_sr_item->p_service_rec->service_data32[2] !=
		    p_svcr->service_record.service_data32[2])
			return;
	}
	if ((comp_mask & IB_SR_COMPMASK_SDATA32_3) == IB_SR_COMPMASK_SDATA32_3) {
		if (p_sr_item->p_service_rec->service_data32[3] !=
		    p_svcr->service_record.service_data32[3])
			return;
	}

	if ((comp_mask & IB_SR_COMPMASK_SDATA64_0) == IB_SR_COMPMASK_SDATA64_0) {
		if (p_sr_item->p_service_rec->service_data64[0] !=
		    p_svcr->service_record.service_data64[0])
			return;
	}
	if ((comp_mask & IB_SR_COMPMASK_SDATA64_1) == IB_SR_COMPMASK_SDATA64_1) {
		if (p_sr_item->p_service_rec->service_data64[1] !=
		    p_svcr->service_record.service_data64[1])
			return;
	}

	/* Check that the requester port has the pkey which is the service_pkey.
	   If not - then it cannot receive this ServiceRecord. */
	/* The check is relevant only if the service_pkey is valid */
	if (!ib_pkey_is_invalid(p_svcr->service_record.service_pkey)) {
		if (!osm_physp_has_pkey(p_sr_item->p_rcv->p_log,
					p_svcr->service_record.service_pkey,
					p_req_physp)) {
			osm_log(p_sr_item->p_rcv->p_log, OSM_LOG_VERBOSE,
				"__get_matching_sr: "
				"requester port doesn't have the service_pkey: 0x%X\n",
				cl_ntoh16(p_svcr->service_record.service_pkey));
			return;
		}
	}

	p_sr_pool_item = malloc(sizeof(*p_sr_pool_item));
	if (p_sr_pool_item == NULL) {
		osm_log(p_sr_item->p_rcv->p_log, OSM_LOG_ERROR,
			"__get_matching_sr: ERR 2408: "
			"Unable to acquire Service Record from pool\n");
		goto Exit;
	}

	p_sr_pool_item->service_rec = p_svcr->service_record;

	cl_qlist_insert_tail(&p_sr_item->sr_list, &p_sr_pool_item->list_item);

      Exit:
	return;
}

/**********************************************************************
 **********************************************************************/
static void
osm_sr_rcv_process_get_method(IN osm_sr_rcv_t * const p_rcv,
			      IN const osm_madw_t * const p_madw)
{
	ib_sa_mad_t *p_sa_mad;
	ib_service_record_t *p_recvd_service_rec;
	osm_sr_match_item_t sr_match_item;
	osm_sr_search_ctxt_t context;
	osm_physp_t *p_req_physp;

	OSM_LOG_ENTER(p_rcv->p_log, osm_sr_rcv_process_get_method);

	CL_ASSERT(p_madw);

	/* update the requester physical port. */
	p_req_physp = osm_get_physp_by_mad_addr(p_rcv->p_log,
						p_rcv->p_subn,
						osm_madw_get_mad_addr_ptr
						(p_madw));
	if (p_req_physp == NULL) {
		osm_log(p_rcv->p_log, OSM_LOG_ERROR,
			"osm_sr_rcv_process_get_method: ERR 2409: "
			"Cannot find requester physical port\n");
		goto Exit;
	}

	p_sa_mad = osm_madw_get_sa_mad_ptr(p_madw);
	p_recvd_service_rec =
	    (ib_service_record_t *) ib_sa_mad_get_payload_ptr(p_sa_mad);

	if (osm_log_is_active(p_rcv->p_log, OSM_LOG_DEBUG)) {
		osm_dump_service_record(p_rcv->p_log,
					p_recvd_service_rec, OSM_LOG_DEBUG);
	}

	cl_qlist_init(&sr_match_item.sr_list);
	sr_match_item.p_service_rec = p_recvd_service_rec;
	sr_match_item.comp_mask = p_sa_mad->comp_mask;
	sr_match_item.p_rcv = p_rcv;

	context.p_sr_item = &sr_match_item;
	context.p_req_physp = p_req_physp;

	/* Grab the lock */
	cl_plock_excl_acquire(p_rcv->p_lock);

	cl_qlist_apply_func(&p_rcv->p_subn->sa_sr_list,
			    __get_matching_sr, &context);

	cl_plock_release(p_rcv->p_lock);

	if ((p_sa_mad->method == IB_MAD_METHOD_GET) &&
	    (cl_qlist_count(&sr_match_item.sr_list) == 0)) {
		osm_log(p_rcv->p_log, OSM_LOG_DEBUG,
			"osm_sr_rcv_process_get_method: "
			"No records matched the Service Record query\n");

		osm_sa_send_error(p_rcv->p_resp, p_madw,
				  IB_SA_MAD_STATUS_NO_RECORDS);
		goto Exit;
	}

	__osm_sr_rcv_respond(p_rcv, p_madw, &sr_match_item.sr_list);

      Exit:
	OSM_LOG_EXIT(p_rcv->p_log);
	return;
}

/**********************************************************************
 **********************************************************************/
static void
osm_sr_rcv_process_set_method(IN osm_sr_rcv_t * const p_rcv,
			      IN const osm_madw_t * const p_madw)
{
	ib_sa_mad_t *p_sa_mad;
	ib_net16_t sa_status = IB_SA_MAD_STATUS_REQ_INVALID;
	ib_service_record_t *p_recvd_service_rec;
	ib_net64_t comp_mask;
	osm_svcr_t *p_svcr;
	osm_sr_item_t *p_sr_item;
	cl_qlist_t sr_list;

	OSM_LOG_ENTER(p_rcv->p_log, osm_sr_rcv_process_set_method);

	CL_ASSERT(p_madw);

	p_sa_mad = osm_madw_get_sa_mad_ptr(p_madw);
	p_recvd_service_rec =
	    (ib_service_record_t *) ib_sa_mad_get_payload_ptr(p_sa_mad);

	comp_mask = p_sa_mad->comp_mask;

	if (osm_log_is_active(p_rcv->p_log, OSM_LOG_DEBUG)) {
		osm_dump_service_record(p_rcv->p_log,
					p_recvd_service_rec, OSM_LOG_DEBUG);
	}

	if ((comp_mask & (IB_SR_COMPMASK_SID | IB_SR_COMPMASK_SGID)) !=
	    (IB_SR_COMPMASK_SID | IB_SR_COMPMASK_SGID)) {
		osm_log(p_rcv->p_log, OSM_LOG_VERBOSE,
			"osm_sr_rcv_process_set_method: "
			"Component Mask RID check failed for METHOD_SET\n");
		osm_sa_send_error(p_rcv->p_resp, p_madw, sa_status);
		goto Exit;
	}

	/* if we were not provided with a service lease make it
	   infinite */
	if ((comp_mask & IB_SR_COMPMASK_SLEASE) != IB_SR_COMPMASK_SLEASE) {
		osm_log(p_rcv->p_log, OSM_LOG_DEBUG,
			"osm_sr_rcv_process_set_method: "
			"ServiceLease Component Mask not set - using infinite lease\n");
		p_recvd_service_rec->service_lease = 0xFFFFFFFF;
	}

	/* Grab the lock */
	cl_plock_excl_acquire(p_rcv->p_lock);

	/* If Record exists with matching RID */
	p_svcr = osm_svcr_get_by_rid(p_rcv->p_subn,
				     p_rcv->p_log, p_recvd_service_rec);

	if (p_svcr == NULL) {
		/* Create the instance of the osm_svcr_t object */
		p_svcr = osm_svcr_new(p_recvd_service_rec);
		if (p_svcr == NULL) {
			cl_plock_release(p_rcv->p_lock);

			osm_log(p_rcv->p_log, OSM_LOG_ERROR,
				"osm_sr_rcv_process_set_method: ERR 2411: "
				"osm_svcr_get_by_rid failed\n");

			osm_sa_send_error(p_rcv->p_resp, p_madw,
					  IB_SA_MAD_STATUS_NO_RESOURCES);
			goto Exit;
		}

		/* Add this new osm_svcr_t object to subnet object */
		osm_svcr_insert_to_db(p_rcv->p_subn, p_rcv->p_log, p_svcr);

	} else {
		/* Update the old instance of the osm_svcr_t object */
		osm_svcr_init(p_svcr, p_recvd_service_rec);
	}

	cl_plock_release(p_rcv->p_lock);

	if (p_recvd_service_rec->service_lease != 0xFFFFFFFF) {
#if 0
		cl_timer_trim(&p_rcv->sr_timer,
			      p_recvd_service_rec->service_lease * 1000);
#endif
		/*  This was a bug since no check was made to see if too long */
		/*  just make sure the timer works - get a call back within a second */
		cl_timer_trim(&p_rcv->sr_timer, 1000);
		p_svcr->modified_time = cl_get_time_stamp_sec();
	}

	p_sr_item = malloc(sizeof(*p_sr_item));
	if (p_sr_item == NULL) {
		osm_log(p_rcv->p_log, OSM_LOG_ERROR,
			"osm_sr_rcv_process_set_method: ERR 2412: "
			"Unable to acquire Service record\n");
		osm_sa_send_error(p_rcv->p_resp, p_madw,
				  IB_SA_MAD_STATUS_NO_RESOURCES);
		goto Exit;
	}

	if ((comp_mask & IB_SR_COMPMASK_SPKEY) != IB_SR_COMPMASK_SPKEY) {
		/* Set the Default Service P_Key in the response */
		p_recvd_service_rec->service_pkey = IB_DEFAULT_PKEY;
	}

	p_sr_item->service_rec = *p_recvd_service_rec;
	cl_qlist_init(&sr_list);

	cl_qlist_insert_tail(&sr_list, &p_sr_item->list_item);

	__osm_sr_rcv_respond(p_rcv, p_madw, &sr_list);

      Exit:
	OSM_LOG_EXIT(p_rcv->p_log);
	return;
}

/**********************************************************************
 **********************************************************************/
static void
osm_sr_rcv_process_delete_method(IN osm_sr_rcv_t * const p_rcv,
				 IN const osm_madw_t * const p_madw)
{
	ib_sa_mad_t *p_sa_mad;
	ib_service_record_t *p_recvd_service_rec;
	ib_net64_t comp_mask;
	osm_svcr_t *p_svcr;
	osm_sr_item_t *p_sr_item;
	cl_qlist_t sr_list;

	OSM_LOG_ENTER(p_rcv->p_log, osm_sr_rcv_process_delete_method);

	CL_ASSERT(p_madw);

	p_sa_mad = osm_madw_get_sa_mad_ptr(p_madw);
	p_recvd_service_rec =
	    (ib_service_record_t *) ib_sa_mad_get_payload_ptr(p_sa_mad);

	comp_mask = p_sa_mad->comp_mask;

	if (osm_log_is_active(p_rcv->p_log, OSM_LOG_DEBUG)) {
		osm_dump_service_record(p_rcv->p_log,
					p_recvd_service_rec, OSM_LOG_DEBUG);
	}

	/* Grab the lock */
	cl_plock_excl_acquire(p_rcv->p_lock);

	/* If Record exists with matching RID */
	p_svcr = osm_svcr_get_by_rid(p_rcv->p_subn,
				     p_rcv->p_log, p_recvd_service_rec);

	if (p_svcr == NULL) {
		cl_plock_release(p_rcv->p_lock);
		osm_log(p_rcv->p_log, OSM_LOG_DEBUG,
			"osm_sr_rcv_process_delete_method: "
			"No records matched the RID\n");
		osm_sa_send_error(p_rcv->p_resp, p_madw,
				  IB_SA_MAD_STATUS_NO_RECORDS);
		goto Exit;
	} else {
		osm_svcr_remove_from_db(p_rcv->p_subn, p_rcv->p_log, p_svcr);
	}

	cl_plock_release(p_rcv->p_lock);

	p_sr_item = malloc(sizeof(*p_sr_item));
	if (p_sr_item == NULL) {
		osm_log(p_rcv->p_log, OSM_LOG_ERROR,
			"osm_sr_rcv_process_delete_method: ERR 2413: "
			"Unable to acquire Service record\n");
		osm_sa_send_error(p_rcv->p_resp, p_madw,
				  IB_SA_MAD_STATUS_NO_RESOURCES);
		goto Exit;
	}

	/* provide back the copy of the record */
	p_sr_item->service_rec = p_svcr->service_record;
	cl_qlist_init(&sr_list);

	cl_qlist_insert_tail(&sr_list, &p_sr_item->list_item);

	if (p_svcr)
		osm_svcr_delete(p_svcr);

	__osm_sr_rcv_respond(p_rcv, p_madw, &sr_list);

      Exit:
	OSM_LOG_EXIT(p_rcv->p_log);
	return;
}

/**********************************************************************
 **********************************************************************/
void osm_sr_rcv_process(IN void *context, IN void *data)
{
	osm_sr_rcv_t *p_rcv = context;
	osm_madw_t *p_madw = data;
	ib_sa_mad_t *p_sa_mad;
	ib_net16_t sa_status = IB_SA_MAD_STATUS_REQ_INVALID;
	boolean_t valid;

	OSM_LOG_ENTER(p_rcv->p_log, osm_sr_rcv_process);

	CL_ASSERT(p_madw);

	p_sa_mad = osm_madw_get_sa_mad_ptr(p_madw);

	CL_ASSERT(p_sa_mad->attr_id == IB_MAD_ATTR_SERVICE_RECORD);

	switch (p_sa_mad->method) {
	case IB_MAD_METHOD_SET:
		valid = __validate_sr(p_rcv, p_madw);
		if (!valid) {
			osm_log(p_rcv->p_log, OSM_LOG_VERBOSE,
				"osm_sr_rcv_process: "
				"Component Mask check failed for set request\n");
			osm_sa_send_error(p_rcv->p_resp, p_madw, sa_status);
			goto Exit;
		}
		osm_sr_rcv_process_set_method(p_rcv, p_madw);
		break;
	case IB_MAD_METHOD_DELETE:
		valid = __validate_sr(p_rcv, p_madw);
		if (!valid) {
			osm_log(p_rcv->p_log, OSM_LOG_DEBUG,
				"osm_sr_rcv_process: "
				"Component Mask check failed for delete request\n");
			osm_sa_send_error(p_rcv->p_resp, p_madw, sa_status);
			goto Exit;
		}
		osm_sr_rcv_process_delete_method(p_rcv, p_madw);
		break;
	case IB_MAD_METHOD_GET:
	case IB_MAD_METHOD_GETTABLE:
		osm_sr_rcv_process_get_method(p_rcv, p_madw);
		break;
	default:
		osm_log(p_rcv->p_log, OSM_LOG_DEBUG,
			"osm_sr_rcv_process: "
			"Unsupported Method (%s)\n",
			ib_get_sa_method_str(p_sa_mad->method));
		osm_sa_send_error(p_rcv->p_resp, p_madw,
				  IB_MAD_STATUS_UNSUP_METHOD_ATTR);
		break;
	}

      Exit:
	OSM_LOG_EXIT(p_rcv->p_log);
	return;
}

/**********************************************************************
 **********************************************************************/
void osm_sr_rcv_lease_cb(IN void *context)
{
	osm_sr_rcv_t *p_rcv = (osm_sr_rcv_t *) context;
	cl_list_item_t *p_list_item;
	cl_list_item_t *p_next_list_item;
	osm_svcr_t *p_svcr;
	uint32_t curr_time;
	uint32_t elapsed_time;
	uint32_t trim_time = 20;	/*  maxiaml timer refresh is 20 seconds */

	OSM_LOG_ENTER(p_rcv->p_log, osm_sr_rcv_lease_cb);

	cl_plock_excl_acquire(p_rcv->p_lock);

	p_list_item = cl_qlist_head(&p_rcv->p_subn->sa_sr_list);

	while (p_list_item != cl_qlist_end(&p_rcv->p_subn->sa_sr_list)) {
		p_svcr = (osm_svcr_t *) p_list_item;

		if (p_svcr->service_record.service_lease == 0xFFFFFFFF) {
			p_list_item = cl_qlist_next(p_list_item);
			continue;
		}

		/* current time in seconds */
		curr_time = cl_get_time_stamp_sec();
		/* elapsed time from last modify */
		elapsed_time = curr_time - p_svcr->modified_time;
		/* but it can not be less then 1 */
		if (elapsed_time < 1)
			elapsed_time = 1;

		if (elapsed_time < p_svcr->lease_period) {
			/*
			   Just update the service lease period
			   note: for simplicity we work with a uint32_t field
			   external to the network order lease_period of the MAD
			 */
			p_svcr->lease_period -= elapsed_time;

			osm_log(p_rcv->p_log, OSM_LOG_DEBUG,
				"osm_sr_rcv_lease_cb: "
				"Remaining time for Service Name:%s is:0x%X\n",
				p_svcr->service_record.service_name,
				p_svcr->lease_period);

			p_svcr->modified_time = curr_time;

			/* Update the trim timer */
			if (trim_time > p_svcr->lease_period) {
				trim_time = p_svcr->lease_period;
				if (trim_time < 1)
					trim_time = 1;
			}

			p_list_item = cl_qlist_next(p_list_item);
			continue;

		} else {
			p_next_list_item = cl_qlist_next(p_list_item);

			/* Remove the service Record */
			osm_svcr_remove_from_db(p_rcv->p_subn,
						p_rcv->p_log, p_svcr);

			osm_svcr_delete(p_svcr);

			p_list_item = p_next_list_item;
			continue;
		}
	}

	/* Release the Lock */
	cl_plock_release(p_rcv->p_lock);

	if (trim_time != 0xFFFFFFFF) {
		cl_timer_trim(&p_rcv->sr_timer, trim_time * 1000);	/* Convert to milli seconds */
	}

	OSM_LOG_EXIT(p_rcv->p_log);
}

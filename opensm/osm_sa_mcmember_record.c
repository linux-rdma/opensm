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
 *    Implementation of osm_mcmr_recv_t.
 * This object represents the MCMemberRecord Receiver object.
 * This object is part of the opensm family of objects.
 *
 * Environment:
 *    Linux User Mode
 *
 * $Revision: 1.15 $
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <stdlib.h>
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
#include <opensm/osm_pkey.h>
#include <opensm/osm_inform.h>
#include <opensm/osm_sa.h>

#define JOIN_MC_COMP_MASK (IB_MCR_COMPMASK_MGID | \
				IB_MCR_COMPMASK_PORT_GID | \
				IB_MCR_COMPMASK_JOIN_STATE)

#define REQUIRED_MC_CREATE_COMP_MASK (IB_MCR_COMPMASK_MGID | \
					IB_MCR_COMPMASK_PORT_GID | \
					IB_MCR_COMPMASK_JOIN_STATE | \
					IB_MCR_COMPMASK_QKEY | \
					IB_MCR_COMPMASK_TCLASS | \
					IB_MCR_COMPMASK_PKEY | \
					IB_MCR_COMPMASK_FLOW | \
					IB_MCR_COMPMASK_SL)

typedef struct _osm_mcmr_item {
	cl_list_item_t list_item;
	ib_member_rec_t rec;
} osm_mcmr_item_t;

typedef struct osm_sa_mcmr_search_ctxt {
	const ib_member_rec_t *p_mcmember_rec;
	osm_mgrp_t *p_mgrp;
	osm_sa_t *sa;
	cl_qlist_t *p_list;	/* hold results */
	ib_net64_t comp_mask;
	const osm_physp_t *p_req_physp;
	boolean_t trusted_req;
} osm_sa_mcmr_search_ctxt_t;

/**********************************************************************
 Look for a MGRP in the mgrp_mlid_tbl by mlid
**********************************************************************/
static osm_mgrp_t *__get_mgrp_by_mlid(IN osm_sa_t * sa,
				      IN ib_net16_t const mlid)
{
	cl_map_item_t *map_item;

	map_item = cl_qmap_get(&sa->p_subn->mgrp_mlid_tbl, mlid);
	if (map_item == cl_qmap_end(&sa->p_subn->mgrp_mlid_tbl)) {
		return NULL;
	}
	return (osm_mgrp_t *) map_item;

}

/*********************************************************************
Copy certain fields between two mcmember records
used during the process of join request to copy data from the mgrp to the
port record.
**********************************************************************/
static inline void
__copy_from_create_mc_rec(IN ib_member_rec_t * const dest,
			  IN const ib_member_rec_t * const src)
{
	dest->qkey = src->qkey;
	dest->mlid = src->mlid;
	dest->tclass = src->tclass;
	dest->pkey = src->pkey;
	dest->sl_flow_hop = src->sl_flow_hop;
	dest->mtu = src->mtu;
	dest->rate = src->rate;
	dest->pkt_life = src->pkt_life;
}

/*********************************************************************
Return an mlid to the pool of free mlids.
But this implementation is not a pool - it simply scans through
the MGRP database for unused mlids...
*********************************************************************/
static void __free_mlid(IN osm_sa_t * sa, IN uint16_t mlid)
{
	UNUSED_PARAM(sa);
	UNUSED_PARAM(mlid);
}

/*********************************************************************
Get a new unused mlid by scanning all the used ones in the subnet.
TODO: Implement a more scalable - O(1) solution based on pool of
available mlids.
**********************************************************************/
static ib_net16_t
__get_new_mlid(IN osm_sa_t * sa, IN ib_net16_t requested_mlid)
{
	osm_subn_t *p_subn = sa->p_subn;
	osm_mgrp_t *p_mgrp;
	uint8_t *used_mlids_array;
	uint16_t idx;
	uint16_t mlid;		/* the result */
	uint16_t max_num_mlids;

	OSM_LOG_ENTER(sa->p_log, __get_new_mlid);

	if (requested_mlid && cl_ntoh16(requested_mlid) >= IB_LID_MCAST_START_HO
	    && cl_ntoh16(requested_mlid) < p_subn->max_multicast_lid_ho
	    && cl_qmap_get(&p_subn->mgrp_mlid_tbl,
			   requested_mlid) ==
	    cl_qmap_end(&p_subn->mgrp_mlid_tbl)) {
		mlid = cl_ntoh16(requested_mlid);
		goto Exit;
	}

	/* If MCGroups table empty, first return the min mlid */
	p_mgrp = (osm_mgrp_t *) cl_qmap_head(&p_subn->mgrp_mlid_tbl);
	if (p_mgrp == (osm_mgrp_t *) cl_qmap_end(&p_subn->mgrp_mlid_tbl)) {
		mlid = IB_LID_MCAST_START_HO;
		osm_log(sa->p_log, OSM_LOG_VERBOSE,
			"__get_new_mlid: "
			"No multicast groups found using minimal mlid:0x%04X\n",
			mlid);
		goto Exit;
	}

	max_num_mlids =
	    sa->p_subn->max_multicast_lid_ho - IB_LID_MCAST_START_HO;

	/* track all used mlids in the array (by mlid index) */
	used_mlids_array = (uint8_t *) malloc(sizeof(uint8_t) * max_num_mlids);
	if (used_mlids_array)
		memset(used_mlids_array, 0, sizeof(uint8_t) * max_num_mlids);
	if (!used_mlids_array)
		return 0;

	/* scan all available multicast groups in the DB and fill in the table */
	while (p_mgrp != (osm_mgrp_t *) cl_qmap_end(&p_subn->mgrp_mlid_tbl)) {
		/* ignore mgrps marked for deletion */
		if (p_mgrp->to_be_deleted == FALSE) {
			osm_log(sa->p_log, OSM_LOG_DEBUG,
				"__get_new_mlid: "
				"Found mgrp with lid:0x%X MGID: 0x%016" PRIx64
				" : " "0x%016" PRIx64 "\n",
				cl_ntoh16(p_mgrp->mlid),
				cl_ntoh64(p_mgrp->mcmember_rec.mgid.unicast.
					  prefix),
				cl_ntoh64(p_mgrp->mcmember_rec.mgid.unicast.
					  interface_id));

			/* Map in table */
			if (cl_ntoh16(p_mgrp->mlid) >
			    sa->p_subn->max_multicast_lid_ho) {
				osm_log(sa->p_log, OSM_LOG_ERROR,
					"__get_new_mlid: ERR 1B27: "
					"Found mgrp with mlid:0x%04X > max allowed mlid:0x%04X\n",
					cl_ntoh16(p_mgrp->mlid),
					max_num_mlids + IB_LID_MCAST_START_HO);
			} else {
				used_mlids_array[cl_ntoh16(p_mgrp->mlid) -
						 IB_LID_MCAST_START_HO] = 1;
			}
		}
		p_mgrp = (osm_mgrp_t *) cl_qmap_next(&p_mgrp->map_item);
	}

	/* Find "mlid holes" in the mgrp table */
	for (idx = 0;
	     (idx < max_num_mlids) && (used_mlids_array[idx] == 1); idx++) ;

	/* did it go above the maximal mlid allowed */
	if (idx < max_num_mlids) {
		mlid = idx + IB_LID_MCAST_START_HO;
		osm_log(sa->p_log, OSM_LOG_VERBOSE,
			"__get_new_mlid: "
			"Found available mlid:0x%04X at idx:%u\n", mlid, idx);
	} else {
		osm_log(sa->p_log, OSM_LOG_ERROR,
			"__get_new_mlid: ERR 1B23: "
			"All available:%u mlids are taken\n", max_num_mlids);
		mlid = 0;
	}

	free(used_mlids_array);

      Exit:
	OSM_LOG_EXIT(sa->p_log);
	return cl_hton16(mlid);
}

/*********************************************************************
This procedure is only invoked to cleanup an INTERMEDIATE mgrp.
If there is only one port on the mgrp it means that the current
request was the only member and the group is not really needed. So we
silently drop it. Since it was an intermediate group no need to
re-route it.
**********************************************************************/
static void
__cleanup_mgrp(IN osm_sa_t * sa, IN ib_net16_t const mlid)
{
	osm_mgrp_t *p_mgrp;

	p_mgrp = __get_mgrp_by_mlid(sa, mlid);
	if (p_mgrp) {
		/* Remove MGRP only if osm_mcm_port_t count is 0 and
		 * Not a well known group
		 */
		if (cl_is_qmap_empty(&p_mgrp->mcm_port_tbl) &&
		    (p_mgrp->well_known == FALSE)) {
			cl_qmap_remove_item(&sa->p_subn->mgrp_mlid_tbl,
					    (cl_map_item_t *) p_mgrp);
			osm_mgrp_delete(p_mgrp);
		}
	}
}

/*********************************************************************
Add a port to the group. Calculating its PROXY_JOIN by the Port and
requester gids.
**********************************************************************/
static ib_api_status_t
__add_new_mgrp_port(IN osm_sa_t * sa,
		    IN osm_mgrp_t * p_mgrp,
		    IN ib_member_rec_t * p_recvd_mcmember_rec,
		    IN osm_mad_addr_t * p_mad_addr,
		    OUT osm_mcm_port_t ** pp_mcmr_port)
{
	boolean_t proxy_join;
	ib_gid_t requester_gid;
	ib_api_status_t res;

	/* set the proxy_join if the requester gid is not identical to the
	   joined gid */
	res = osm_get_gid_by_mad_addr(sa->p_log,
				      sa->p_subn,
				      p_mad_addr, &requester_gid);
	if (res != IB_SUCCESS) {
		osm_log(sa->p_log, OSM_LOG_ERROR,
			"__add_new_mgrp_port: ERR 1B29: "
			"Could not find GID for requester\n");

		return IB_INVALID_PARAMETER;
	}

	if (!memcmp(&p_recvd_mcmember_rec->port_gid, &requester_gid,
		    sizeof(ib_gid_t))) {
		proxy_join = FALSE;
		osm_log(sa->p_log, OSM_LOG_DEBUG,
			"__add_new_mgrp_port: "
			"Create new port with proxy_join FALSE\n");
	} else {
		/* The port is not the one specified in PortGID.
		   The check that the requester is in the same partition as
		   the PortGID is done before - just need to update the proxy_join. */
		proxy_join = TRUE;
		osm_log(sa->p_log, OSM_LOG_DEBUG,
			"__add_new_mgrp_port: "
			"Create new port with proxy_join TRUE\n");
	}

	*pp_mcmr_port = osm_mgrp_add_port(p_mgrp,
					  &p_recvd_mcmember_rec->port_gid,
					  p_recvd_mcmember_rec->scope_state,
					  proxy_join);
	if (*pp_mcmr_port == NULL) {
		osm_log(sa->p_log, OSM_LOG_ERROR,
			"__add_new_mgrp_port: ERR 1B06: "
			"osm_mgrp_add_port failed\n");

		return IB_INSUFFICIENT_MEMORY;
	}

	return IB_SUCCESS;
}

/**********************************************************************
 **********************************************************************/
static inline boolean_t __check_join_comp_mask(ib_net64_t comp_mask)
{
	return ((comp_mask & JOIN_MC_COMP_MASK) == JOIN_MC_COMP_MASK);
}

/**********************************************************************
 **********************************************************************/
static inline boolean_t
__check_create_comp_mask(ib_net64_t comp_mask,
			 ib_member_rec_t * p_recvd_mcmember_rec)
{
	return (((comp_mask & REQUIRED_MC_CREATE_COMP_MASK) ==
		 REQUIRED_MC_CREATE_COMP_MASK)
	    );
}

/**********************************************************************
Generate the response MAD
**********************************************************************/
static void
__osm_mcmr_rcv_respond(IN osm_sa_t * sa,
		       IN const osm_madw_t * const p_madw,
		       IN ib_member_rec_t * p_mcmember_rec)
{
	osm_madw_t *p_resp_madw;
	ib_sa_mad_t *p_sa_mad, *p_resp_sa_mad;
	ib_member_rec_t *p_resp_mcmember_rec;
	ib_api_status_t status;

	OSM_LOG_ENTER(sa->p_log, __osm_mcmr_rcv_respond);

	/*
	 *  Get a MAD to reply. Address of Mad is in the received mad_wrapper
	 */
	p_resp_madw = osm_mad_pool_get(sa->p_mad_pool,
				       p_madw->h_bind,
				       sizeof(ib_member_rec_t) +
				       IB_SA_MAD_HDR_SIZE,
				       osm_madw_get_mad_addr_ptr(p_madw));
	if (!p_resp_madw) {
		goto Exit;
	}

	p_resp_sa_mad = (ib_sa_mad_t *) p_resp_madw->p_mad;
	p_sa_mad = (ib_sa_mad_t *) p_madw->p_mad;
	/*  Copy the MAD header back into the response mad */
	memcpy(p_resp_sa_mad, p_sa_mad, IB_SA_MAD_HDR_SIZE);
	/*  based on the current method decide about the response: */
	if ((p_resp_sa_mad->method == IB_MAD_METHOD_GET) ||
	    (p_resp_sa_mad->method == IB_MAD_METHOD_SET)) {
		p_resp_sa_mad->method = IB_MAD_METHOD_GET_RESP;
	} else if (p_resp_sa_mad->method == IB_MAD_METHOD_DELETE) {
		p_resp_sa_mad->method |= IB_MAD_METHOD_RESP_MASK;
	} else {
		CL_ASSERT(p_resp_sa_mad->method == 0);
	}

	/* C15-0.1.5 - always return SM_Key = 0 (table 185 p 884) */
	p_resp_sa_mad->sm_key = 0;

	/* Fill in the offset (paylen will be done by the rmpp SAR) */
	p_resp_sa_mad->attr_offset =
	    ib_get_attr_offset(sizeof(ib_member_rec_t));
	p_resp_mcmember_rec = (ib_member_rec_t *) & p_resp_sa_mad->data;

	*p_resp_mcmember_rec = *p_mcmember_rec;

	/* Fill in the mtu, rate, and packet lifetime selectors */
	p_resp_mcmember_rec->mtu &= 0x3f;
	p_resp_mcmember_rec->mtu |= 2 << 6;	/* exactly */
	p_resp_mcmember_rec->rate &= 0x3f;
	p_resp_mcmember_rec->rate |= 2 << 6;	/* exactly */
	p_resp_mcmember_rec->pkt_life &= 0x3f;
	p_resp_mcmember_rec->pkt_life |= 2 << 6;	/* exactly */

	status = osm_sa_vendor_send(p_resp_madw->h_bind, p_resp_madw, FALSE,
				    sa->p_subn);

	if (status != IB_SUCCESS) {
		osm_log(sa->p_log, OSM_LOG_ERROR,
			"__osm_mcmr_rcv_respond: ERR 1B07: "
			"Unable to send MAD (%s) for TID <0x%" PRIx64 ">\n",
			ib_get_err_str(status), p_resp_sa_mad->trans_id);
	}

      Exit:
	OSM_LOG_EXIT(sa->p_log);
	return;
}

/*********************************************************************
In joining an existing group, or when querying the mc groups,
we make sure the following components provided match: MTU and RATE
HACK: Currently we ignore the PKT_LIFETIME field.
**********************************************************************/
static boolean_t
__validate_more_comp_fields(osm_log_t * p_log,
			    const osm_mgrp_t * p_mgrp,
			    const ib_member_rec_t * p_recvd_mcmember_rec,
			    ib_net64_t comp_mask)
{
	uint8_t mtu_sel;
	uint8_t mtu_required;
	uint8_t mtu_mgrp;
	uint8_t rate_sel;
	uint8_t rate_required;
	uint8_t rate_mgrp;

	if (comp_mask & IB_MCR_COMPMASK_MTU_SEL) {
		mtu_sel = (uint8_t) (p_recvd_mcmember_rec->mtu >> 6);
		/* Clearing last 2 bits */
		mtu_required = (uint8_t) (p_recvd_mcmember_rec->mtu & 0x3F);
		mtu_mgrp = (uint8_t) (p_mgrp->mcmember_rec.mtu & 0x3F);
		switch (mtu_sel) {
		case 0:	/* Greater than MTU specified */
			if (mtu_mgrp <= mtu_required) {
				osm_log(p_log, OSM_LOG_DEBUG,
					"__validate_more_comp_fields: "
					"Requested mcast group has MTU %x, which is not greater than %x\n",
					mtu_mgrp, mtu_required);
				return FALSE;
			}
			break;
		case 1:	/* Less than MTU specified */
			if (mtu_mgrp >= mtu_required) {
				osm_log(p_log, OSM_LOG_DEBUG,
					"__validate_more_comp_fields: "
					"Requested mcast group has MTU %x, which is not less than %x\n",
					mtu_mgrp, mtu_required);
				return FALSE;
			}
			break;
		case 2:	/* Exactly MTU specified */
			if (mtu_mgrp != mtu_required) {
				osm_log(p_log, OSM_LOG_DEBUG,
					"__validate_more_comp_fields: "
					"Requested mcast group has MTU %x, which is not equal to %x\n",
					mtu_mgrp, mtu_required);
				return FALSE;
			}
			break;
		default:
			break;
		}
	}

	/* what about rate ? */
	if (comp_mask & IB_MCR_COMPMASK_RATE_SEL) {
		rate_sel = (uint8_t) (p_recvd_mcmember_rec->rate >> 6);
		/* Clearing last 2 bits */
		rate_required = (uint8_t) (p_recvd_mcmember_rec->rate & 0x3F);
		rate_mgrp = (uint8_t) (p_mgrp->mcmember_rec.rate & 0x3F);
		switch (rate_sel) {
		case 0:	/* Greater than RATE specified */
			if (rate_mgrp <= rate_required) {
				osm_log(p_log, OSM_LOG_DEBUG,
					"__validate_more_comp_fields: "
					"Requested mcast group has RATE %x, which is not greater than %x\n",
					rate_mgrp, rate_required);
				return FALSE;
			}
			break;
		case 1:	/* Less than RATE specified */
			if (rate_mgrp >= rate_required) {
				osm_log(p_log, OSM_LOG_DEBUG,
					"__validate_more_comp_fields: "
					"Requested mcast group has RATE %x, which is not less than %x\n",
					rate_mgrp, rate_required);
				return FALSE;
			}
			break;
		case 2:	/* Exactly RATE specified */
			if (rate_mgrp != rate_required) {
				osm_log(p_log, OSM_LOG_DEBUG,
					"__validate_more_comp_fields: "
					"Requested mcast group has RATE %x, which is not equal to %x\n",
					rate_mgrp, rate_required);
				return FALSE;
			}
			break;
		default:
			break;
		}
	}

	return TRUE;
}

/*********************************************************************
In joining an existing group, we make sure the following components
are physically realizable: MTU and RATE
**********************************************************************/
static boolean_t
__validate_port_caps(osm_log_t * const p_log,
		     const osm_mgrp_t * p_mgrp, const osm_physp_t * p_physp)
{
	uint8_t mtu_required;
	uint8_t mtu_mgrp;
	uint8_t rate_required;
	uint8_t rate_mgrp;

	mtu_required = ib_port_info_get_mtu_cap(&p_physp->port_info);
	mtu_mgrp = (uint8_t) (p_mgrp->mcmember_rec.mtu & 0x3F);
	if (mtu_required < mtu_mgrp) {
		osm_log(p_log, OSM_LOG_DEBUG,
			"__validate_port_caps: "
			"Port's MTU %x is less than %x\n",
			mtu_required, mtu_mgrp);
		return FALSE;
	}

	rate_required = ib_port_info_compute_rate(&p_physp->port_info);
	rate_mgrp = (uint8_t) (p_mgrp->mcmember_rec.rate & 0x3F);
	if (rate_required < rate_mgrp) {
		osm_log(p_log, OSM_LOG_DEBUG,
			"__validate_port_caps: "
			"Port's RATE %x is less than %x\n",
			rate_required, rate_mgrp);
		return FALSE;
	}

	return TRUE;
}

/**********************************************************************
 * o15-0.2.1: If SA supports UD multicast, then if SA receives a SubnAdmSet()
 * or SubnAdmDelete() method that would modify an existing
 * MCMemberRecord, SA shall not modify that MCMemberRecord and shall
 * return an error status of ERR_REQ_INVALID in response in the
 * following cases:
 * 1. Saved MCMemberRecord.ProxyJoin is not set and the request is
 * issued by a requester with a GID other than the Port-GID.
 * 2. Saved MCMemberRecord.ProxyJoin is set and the requester is not
 * part of the partition for that MCMemberRecord.
 **********************************************************************/
static boolean_t
__validate_modify(IN osm_sa_t * sa,
		  IN osm_mgrp_t * p_mgrp,
		  IN osm_mad_addr_t * p_mad_addr,
		  IN ib_member_rec_t * p_recvd_mcmember_rec,
		  OUT osm_mcm_port_t ** pp_mcm_port)
{
	ib_net64_t portguid;
	ib_gid_t request_gid;
	osm_physp_t *p_request_physp;
	ib_api_status_t res;

	portguid = p_recvd_mcmember_rec->port_gid.unicast.interface_id;

	*pp_mcm_port = NULL;

	/* o15-0.2.1: If this is a new port being added - nothing to check */
	if (!osm_mgrp_is_port_present(p_mgrp, portguid, pp_mcm_port)) {
		osm_log(sa->p_log, OSM_LOG_DEBUG,
			"__validate_modify: "
			"This is a new port in the MC group\n");
		return TRUE;
	}

	/* We validate the request according the the proxy_join.
	   Check if the proxy_join is set or not */
	if ((*pp_mcm_port)->proxy_join == FALSE) {
		/* The proxy_join is not set. Modifying can by done only
		   if the requester GID == PortGID */
		res = osm_get_gid_by_mad_addr(sa->p_log,
					      sa->p_subn,
					      p_mad_addr, &request_gid);

		if (res != IB_SUCCESS) {
			osm_log(sa->p_log, OSM_LOG_DEBUG,
				"__validate_modify: "
				"Could not find port for requested address\n");
			return FALSE;
		}

		if (memcmp
		    (&((*pp_mcm_port)->port_gid), &request_gid,
		     sizeof(ib_gid_t))) {
			osm_log(sa->p_log, OSM_LOG_DEBUG,
				"__validate_modify: "
				"No ProxyJoin but different ports: stored:0x%016"
				PRIx64 " request:0x%016" PRIx64 "\n",
				cl_ntoh64((*pp_mcm_port)->port_gid.unicast.
					  interface_id),
				cl_ntoh64(p_mad_addr->addr_type.gsi.grh_info.
					  src_gid.unicast.interface_id)
			    );
			return FALSE;
		}
	} else {
		/* The proxy_join is set. Modification allowed only if the
		   requester is part of the partition for this MCMemberRecord */
		p_request_physp = osm_get_physp_by_mad_addr(sa->p_log,
							    sa->p_subn,
							    p_mad_addr);
		if (p_request_physp == NULL)
			return FALSE;

		if (!osm_physp_has_pkey(sa->p_log, p_mgrp->mcmember_rec.pkey,
					p_request_physp)) {
			/* the request port is not part of the partition for this mgrp */
			osm_log(sa->p_log, OSM_LOG_DEBUG,
				"__validate_modify: "
				"ProxyJoin but port not in partition. stored:0x%016"
				PRIx64 " request:0x%016" PRIx64 "\n",
				cl_ntoh64((*pp_mcm_port)->port_gid.unicast.
					  interface_id),
				cl_ntoh64(p_mad_addr->addr_type.gsi.grh_info.
					  src_gid.unicast.interface_id)
			    );
			return FALSE;
		}
	}
	return TRUE;
}

/**********************************************************************
 **********************************************************************/
/*
 * Check legality of the requested MGID DELETE
 * o15-0.1.14 = VALID DELETE:
 * To be a valid delete MAD needs to:
 * 1 the MADs PortGID and MGID components match the PortGID and
 *   MGID of a stored MCMemberRecord;
 * 2 the MADs JoinState component contains at least one bit set to 1
 *   in the same position as that stored MCMemberRecords JoinState
 *   has a bit set to 1,
 *   i.e., the logical AND of the two JoinState components
 *   is not all zeros;
 * 3 the MADs JoinState component does not have some bits set
 *   which are not set in the stored MCMemberRecords JoinState component;
 * 4 either the stored MCMemberRecord:ProxyJoin is reset (0), and the
 *   MADs source is the stored PortGID;
 *   OR
 *   the stored MCMemberRecord:ProxyJoin is set (1), (see o15-
 *   0.1.2:); and the MADs source is a member of the partition indicated
 *   by the stored MCMemberRecord:P_Key.
 */
static boolean_t
__validate_delete(IN osm_sa_t * sa,
		  IN osm_mgrp_t * p_mgrp,
		  IN osm_mad_addr_t * p_mad_addr,
		  IN ib_member_rec_t * p_recvd_mcmember_rec,
		  OUT osm_mcm_port_t ** pp_mcm_port)
{
	ib_net64_t portguid;

	portguid = p_recvd_mcmember_rec->port_gid.unicast.interface_id;

	*pp_mcm_port = NULL;

	/* 1 */
	if (!osm_mgrp_is_port_present(p_mgrp, portguid, pp_mcm_port)) {
		osm_log(sa->p_log, OSM_LOG_DEBUG,
			"__validate_delete: "
			"Failed to find the port in the MC group\n");
		return FALSE;
	}

	/* 2 */
	if (!(p_recvd_mcmember_rec->scope_state & 0x0F &
	      (*pp_mcm_port)->scope_state)) {
		osm_log(sa->p_log, OSM_LOG_DEBUG,
			"__validate_delete: "
			"Could not find any matching bits in the stored and requested JoinStates\n");
		return FALSE;
	}

	/* 3 */
	if (((p_recvd_mcmember_rec->scope_state & 0x0F) |
	     (0x0F & (*pp_mcm_port)->scope_state)) !=
	    (0x0F & (*pp_mcm_port)->scope_state)) {
		osm_log(sa->p_log, OSM_LOG_DEBUG,
			"__validate_delete: "
			"Some bits in the request JoinState (0x%X) are not set in the stored port (0x%X)\n",
			(p_recvd_mcmember_rec->scope_state & 0x0F),
			(0x0F & (*pp_mcm_port)->scope_state)
		    );
		return FALSE;
	}

	/* 4 */
	/* Validate according the the proxy_join (o15-0.1.2) */
	if (__validate_modify(sa, p_mgrp, p_mad_addr, p_recvd_mcmember_rec,
			      pp_mcm_port) == FALSE) {
		osm_log(sa->p_log, OSM_LOG_DEBUG,
			"__validate_delete: "
			"proxy_join validation failure\n");
		return FALSE;
	}
	return TRUE;
}

/**********************************************************************
 **********************************************************************/
/*
 * Check legality of the requested MGID (note this does not hold for SA
 * created MGIDs)
 *
 * Implementing o15-0.1.5:
 * A multicast GID is considered to be invalid if:
 * 1. It does not comply with the rules as specified in 4.1.1 "GID Usage and
 *    Properties" on page 145:
 *
 * 14) The multicast GID format is (bytes are comma sep):
 *     0xff,<Fl><Sc>,<Si>,<Si>,<P>,<P>,<P>,<P>,<P>,<P>,<P>,<P>,<Id>,<Id>,<Id>,<Id>
 *     Fl  4bit = Flags (b)
 *     Sc  4bit = Scope (c)
 *     Si 16bit = Signature (2)
 *     P  64bit = GID Prefix (should be a subnet unique ID - normally Subnet Prefix)
 *     Id 32bit = Unique ID in the Subnet (might be MLID or Pkey ?)
 *
 *  a) 8-bits of 11111111 at the start of the GID identifies this as being a
 *     multicast GID.
 *  b) Flags is a set of four 1-bit flags: 000T with three flags reserved
 *     and defined as zero (0). The T flag is defined as follows:
 *     i) T = 0 indicates this is a permanently assigned (i.e. wellknown)
 *        multicast GID. See RFC 2373 and RFC 2375 as reference
 *        for these permanently assigned GIDs.
 *     ii) T = 1 indicates this is a non-permanently assigned (i.e. transient)
 *        multicast GID.
 *  c) Scope is a 4-bit multicast scope value used to limit the scope of
 *     the multicast group. The following table defines scope value and
 *     interpretation.
 *
 *     Multicast Address Scope Values:
 *     0x2 Link-local
 *     0x5 Site-local
 *     0x8 Organization-local
 *     0xE Global
 *
 * 2. It contains the SA-specific signature of 0xA01B and has the link-local
 *    scope bits set. (EZ: the idea here is that SA created MGIDs are the
 *    only source for this signature with link-local scope)
 */
static ib_api_status_t
__validate_requested_mgid(IN osm_sa_t * sa,
			  IN const ib_member_rec_t * p_mcm_rec)
{
	uint16_t signature;
	boolean_t valid = TRUE;

	OSM_LOG_ENTER(sa->p_log, __validate_requested_mgid);

	/* 14-a: mcast GID must start with 0xFF */
	if (p_mcm_rec->mgid.multicast.header[0] != 0xFF) {
		osm_log(sa->p_log, OSM_LOG_ERROR,
			"__validate_requested_mgid: ERR 1B01: "
			"Wrong MGID Prefix 0x%02X must be 0xFF\n",
			cl_ntoh16(p_mcm_rec->mgid.multicast.header[0])
		    );
		valid = FALSE;
		goto Exit;
	}

	/* the MGID signature can mark IPoIB or SA assigned MGIDs */
	memcpy(&signature, &(p_mcm_rec->mgid.multicast.raw_group_id),
	       sizeof(signature));
	signature = cl_ntoh16(signature);
	osm_log(sa->p_log, OSM_LOG_DEBUG,
		"__validate_requested_mgid:  "
		"MGID Signed as 0x%04X\n", signature);

	/*
	 * We skip any checks for MGIDs that follow IPoIB
	 * GID structure as defined by the IETF ipoib-link-multicast.
	 *
	 * For IPv4 over IB, the signature will be "0x401B".
	 *
	 * |   8    |  4 |  4 |     16 bits     | 16 bits | 48 bits  | 32 bits |
	 * +--------+----+----+-----------------+---------+----------+---------+
	 * |11111111|0001|scop|<IPoIB signature>|< P_Key >|00.......0|<all 1's>|
	 * +--------+----+----+-----------------+---------+----------+---------+
	 *
	 * For IPv6 over IB, the signature will be "0x601B".
	 *
	 * |   8    |  4 |  4 |     16 bits     | 16 bits |       80 bits      |
	 * +--------+----+----+-----------------+---------+--------------------+
	 * |11111111|0001|scop|<IPoIB signature>|< P_Key >|000.............0001|
	 * +--------+----+----+-----------------+---------+--------------------+
	 *
	 */
	if (signature == 0x401B || signature == 0x601B) {
		osm_log(sa->p_log, OSM_LOG_DEBUG,
			"__validate_requested_mgid:  "
			"Skipping MGID Validation for IPoIB Signed (0x%04X) MGIDs\n",
			signature);
		goto Exit;
	}

	/* 14-b: the 3 upper bits in the "flags" should be zero: */
	if (p_mcm_rec->mgid.multicast.header[1] & 0xE0) {
		osm_log(sa->p_log, OSM_LOG_ERROR,
			"__validate_requested_mgid: ERR 1B28: "
			"MGID uses Reserved Flags: flags=0x%X\n",
			(p_mcm_rec->mgid.multicast.header[1] & 0xE0) >> 4);
		valid = FALSE;
		goto Exit;
	}

	/* 2 - now what if the link local format 0xA01B is used -
	   the scope should not be link local */
	if ((signature == 0xA01B) &&
	    ((p_mcm_rec->mgid.multicast.header[1] & 0x0F) ==
	     IB_MC_SCOPE_LINK_LOCAL)) {
		osm_log(sa->p_log, OSM_LOG_ERROR,
			"__validate_requested_mgid: ERR 1B24: "
			"MGID uses 0xA01B signature but with link-local scope\n");
		valid = FALSE;
		goto Exit;
	}

	/*
	 * For SA assigned MGIDs (signature 0xA01B):
	 * There is no real way to make sure the Unique MGID Prefix is really unique.
	 * If we could enforce using the Subnet Prefix for that purpose it would
	 * have been nice. But the spec does not require it.
	 */

      Exit:
	OSM_LOG_EXIT(sa->p_log);
	return (valid);
}

/**********************************************************************
 Check if the requested new MC group parameters are realizable.
 Also set the default MTU and Rate if not provided by the user.
**********************************************************************/
static boolean_t
__mgrp_request_is_realizable(IN osm_sa_t * sa,
			     IN ib_net64_t comp_mask,
			     IN ib_member_rec_t * p_mcm_rec,
			     IN const osm_physp_t * const p_physp)
{
	uint8_t mtu_sel = 2;	/* exactly */
	uint8_t mtu_required, mtu, port_mtu;
	uint8_t rate_sel = 2;	/* exactly */
	uint8_t rate_required, rate, port_rate;
	osm_log_t *p_log = sa->p_log;

	OSM_LOG_ENTER(sa->p_log, __mgrp_request_is_realizable);

	/*
	 * End of o15-0.2.3 specifies:
	 * ....
	 * The entity may also supply the other components such as HopLimit, MTU,
	 * etc. during group creation time. If these components are not provided
	 * during group creation time, SA will provide them for the group. The values
	 * chosen are vendor-dependent and beyond the scope of the specification.
	 *
	 * so we might also need to assign RATE/MTU if they are not comp masked in.
	 */

	port_mtu = p_physp ? ib_port_info_get_mtu_cap(&p_physp->port_info) : 0;
	if (!(comp_mask & IB_MCR_COMPMASK_MTU) ||
	    !(comp_mask & IB_MCR_COMPMASK_MTU_SEL) ||
	    (mtu_sel = (p_mcm_rec->mtu >> 6)) == 3)
		mtu = port_mtu ? port_mtu : sa->p_subn->min_ca_mtu;
	else {
		mtu_required = (uint8_t) (p_mcm_rec->mtu & 0x3F);
		mtu = mtu_required;
		switch (mtu_sel) {
		case 0:	/* Greater than MTU specified */
			if (port_mtu && mtu_required >= port_mtu) {
				osm_log(p_log, OSM_LOG_DEBUG,
					"__mgrp_request_is_realizable: "
					"Requested MTU %x >= the port\'s mtu:%x\n",
					mtu_required, port_mtu);
				return FALSE;
			}
			/* we provide the largest MTU possible if we can */
			if (port_mtu)
				mtu = port_mtu;
			else if (mtu_required < sa->p_subn->min_ca_mtu)
				mtu = sa->p_subn->min_ca_mtu;
			else
				mtu++;
			break;
		case 1:	/* Less than MTU specified */
			/* use the smaller of the two:
			   a. one lower then the required
			   b. the mtu of the requesting port (if exists) */
			if (port_mtu && mtu_required > port_mtu)
				mtu = port_mtu;
			else
				mtu--;
			break;
		case 2:	/* Exactly MTU specified */
		default:
			break;
		}
		/* make sure it still be in the range */
		if (mtu < IB_MIN_MTU || mtu > IB_MAX_MTU) {
			osm_log(p_log, OSM_LOG_DEBUG,
				"__mgrp_request_is_realizable: "
				"Calculated MTU %x is out of range\n", mtu);
			return FALSE;
		}
	}
	p_mcm_rec->mtu = (mtu_sel << 6) | mtu;

	port_rate =
	    p_physp ? ib_port_info_compute_rate(&p_physp->port_info) : 0;
	if (!(comp_mask & IB_MCR_COMPMASK_RATE)
	    || !(comp_mask & IB_MCR_COMPMASK_RATE_SEL)
	    || (rate_sel = (p_mcm_rec->rate >> 6)) == 3)
		rate = port_rate ? port_rate : sa->p_subn->min_ca_rate;
	else {
		rate_required = (uint8_t) (p_mcm_rec->rate & 0x3F);
		rate = rate_required;
		switch (rate_sel) {
		case 0:	/* Greater than RATE specified */
			if (port_rate && rate_required >= port_rate) {
				osm_log(p_log, OSM_LOG_DEBUG,
					"__mgrp_request_is_realizable: "
					"Requested RATE %x >= the port\'s rate:%x\n",
					rate_required, port_rate);
				return FALSE;
			}
			/* we provide the largest RATE possible if we can */
			if (port_rate)
				rate = port_rate;
			else if (rate_required < sa->p_subn->min_ca_rate)
				rate = sa->p_subn->min_ca_rate;
			else
				rate++;
			break;
		case 1:	/* Less than RATE specified */
			/* use the smaller of the two:
			   a. one lower then the required
			   b. the rate of the requesting port (if exists) */
			if (port_rate && rate_required > port_rate)
				rate = port_rate;
			else
				rate--;
			break;
		case 2:	/* Exactly RATE specified */
		default:
			break;
		}
		/* make sure it still is in the range */
		if (rate < IB_MIN_RATE || rate > IB_MAX_RATE) {
			osm_log(p_log, OSM_LOG_DEBUG,
				"__mgrp_request_is_realizable: "
				"Calculated RATE %x is out of range\n", rate);
			return FALSE;
		}
	}
	p_mcm_rec->rate = (rate_sel << 6) | rate;

	OSM_LOG_EXIT(sa->p_log);
	return TRUE;
}

/**********************************************************************
 Call this function to create a new mgrp.
**********************************************************************/
ib_api_status_t
osm_mcmr_rcv_create_new_mgrp(IN osm_sa_t * sa,
			     IN ib_net64_t comp_mask,
			     IN const ib_member_rec_t *
			     const p_recvd_mcmember_rec,
			     IN const osm_physp_t * const p_physp,
			     OUT osm_mgrp_t ** pp_mgrp)
{
	ib_net16_t mlid;
	uint8_t zero_mgid, valid;
	uint8_t scope, i;
	ib_gid_t *p_mgid;
	osm_mgrp_t *p_prev_mgrp;
	ib_api_status_t status = IB_SUCCESS;
	ib_member_rec_t mcm_rec = *p_recvd_mcmember_rec;	/* copy for modifications */

	OSM_LOG_ENTER(sa->p_log, osm_mcmr_rcv_create_new_mgrp);

	/* but what if the given MGID was not 0 ? */
	zero_mgid = 1;
	for (i = 0; i < sizeof(p_recvd_mcmember_rec->mgid); i++) {
		if (p_recvd_mcmember_rec->mgid.raw[i] != 0) {
			zero_mgid = 0;
			break;
		}
	}

	/*
	   we allocate a new mlid number before we might use it
	   for MGID ...
	 */
	mlid = __get_new_mlid(sa, mcm_rec.mlid);
	if (mlid == 0) {
		osm_log(sa->p_log, OSM_LOG_ERROR,
			"osm_mcmr_rcv_create_new_mgrp: ERR 1B19: "
			"__get_new_mlid failed\n");
		status = IB_SA_MAD_STATUS_NO_RESOURCES;
		goto Exit;
	}

	osm_log(sa->p_log, OSM_LOG_DEBUG,
		"osm_mcmr_rcv_create_new_mgrp: "
		"Obtained new mlid 0x%X\n", cl_ntoh16(mlid));

	/* we need to create the new MGID if it was not defined */
	if (zero_mgid) {
		/* create a new MGID */

		/* use the given scope state only if requested! */
		if (comp_mask & IB_MCR_COMPMASK_SCOPE) {
			ib_member_get_scope_state(p_recvd_mcmember_rec->
						  scope_state, &scope, NULL);
		} else {
			/* to guarantee no collision with other subnets use local scope! */
			scope = IB_MC_SCOPE_LINK_LOCAL;
		}

		p_mgid = &(mcm_rec.mgid);
		p_mgid->raw[0] = 0xFF;
		p_mgid->raw[1] = 0x10 | scope;
		p_mgid->raw[2] = 0xA0;
		p_mgid->raw[3] = 0x1B;

		/* HACK: use the SA port gid to make it globally unique */
		memcpy((&p_mgid->raw[4]),
		       &sa->p_subn->opt.subnet_prefix, sizeof(uint64_t));

		/* HACK: how do we get a unique number - use the mlid twice */
		memcpy(&p_mgid->raw[10], &mlid, sizeof(uint16_t));
		memcpy(&p_mgid->raw[12], &mlid, sizeof(uint16_t));
		osm_log(sa->p_log, OSM_LOG_DEBUG,
			"osm_mcmr_rcv_create_new_mgrp: "
			"Allocated new MGID:0x%016" PRIx64 " : "
			"0x%016" PRIx64 "\n",
			cl_ntoh64(p_mgid->unicast.prefix),
			cl_ntoh64(p_mgid->unicast.interface_id));
	} else {
		/* a specific MGID was requested so validate the resulting MGID */
		valid = __validate_requested_mgid(sa, &mcm_rec);
		if (!valid) {
			osm_log(sa->p_log, OSM_LOG_ERROR,
				"osm_mcmr_rcv_create_new_mgrp: ERR 1B22: "
				"Invalid requested MGID\n");
			__free_mlid(sa, mlid);
			status = IB_SA_MAD_STATUS_REQ_INVALID;
			goto Exit;
		}
	}

	/* check the requested parameters are realizable */
	if (__mgrp_request_is_realizable(sa, comp_mask, &mcm_rec, p_physp) ==
	    FALSE) {
		osm_log(sa->p_log, OSM_LOG_ERROR,
			"osm_mcmr_rcv_create_new_mgrp: ERR 1B26: "
			"Requested MGRP parameters are not realizable\n");
		__free_mlid(sa, mlid);
		status = IB_SA_MAD_STATUS_REQ_INVALID;
		goto Exit;
	}

	/* create a new MC Group */
	*pp_mgrp = osm_mgrp_new(mlid);
	if (*pp_mgrp == NULL) {
		osm_log(sa->p_log, OSM_LOG_ERROR,
			"osm_mcmr_rcv_create_new_mgrp: ERR 1B08: "
			"osm_mgrp_new failed\n");
		__free_mlid(sa, mlid);
		status = IB_SA_MAD_STATUS_NO_RESOURCES;
		goto Exit;
	}

	/* Initialize the mgrp */
	(*pp_mgrp)->mcmember_rec = mcm_rec;
	(*pp_mgrp)->mcmember_rec.mlid = mlid;

	/* the mcmember_record should have mtu_sel, rate_sel, and pkt_lifetime_sel = 2 */
	(*pp_mgrp)->mcmember_rec.mtu &= 0x3f;
	(*pp_mgrp)->mcmember_rec.mtu |= 2 << 6;	/* exactly */
	(*pp_mgrp)->mcmember_rec.rate &= 0x3f;
	(*pp_mgrp)->mcmember_rec.rate |= 2 << 6;	/* exactly */
	(*pp_mgrp)->mcmember_rec.pkt_life &= 0x3f;
	(*pp_mgrp)->mcmember_rec.pkt_life |= 2 << 6;	/* exactly */

	/* Insert the new group in the data base */

	/* since we might have an old group by that mlid
	   one whose deletion was delayed for an idle time
	   we need to deallocate it first */
	p_prev_mgrp =
	    (osm_mgrp_t *) cl_qmap_get(&sa->p_subn->mgrp_mlid_tbl, mlid);
	if (p_prev_mgrp !=
	    (osm_mgrp_t *) cl_qmap_end(&sa->p_subn->mgrp_mlid_tbl)) {
		osm_log(sa->p_log, OSM_LOG_DEBUG,
			"osm_mcmr_rcv_create_new_mgrp: "
			"Found previous group for mlid:0x%04x - Need to destroy it\n",
			cl_ntoh16(mlid));
		cl_qmap_remove_item(&sa->p_subn->mgrp_mlid_tbl,
				    (cl_map_item_t *) p_prev_mgrp);
		osm_mgrp_delete(p_prev_mgrp);
	}

	cl_qmap_insert(&sa->p_subn->mgrp_mlid_tbl,
		       mlid, &(*pp_mgrp)->map_item);

	/* Send a Report to any InformInfo registerd for
	   Trap 66: MCGroup create */
	osm_mgrp_send_create_notice(sa->p_subn, sa->p_log, *pp_mgrp);

      Exit:
	OSM_LOG_EXIT(sa->p_log);
	return status;

}


typedef struct osm_sa_pr_mcmr_search_ctxt {
	ib_gid_t *p_mgid;
	osm_mgrp_t *p_mgrp;
	osm_sa_t *sa;
} osm_sa_pr_mcmr_search_ctxt_t;

/**********************************************************************
 *********************************************************************/
static void
__search_mgrp_by_mgid(IN cl_map_item_t * const p_map_item, IN void *context)
{
	osm_mgrp_t *p_mgrp = (osm_mgrp_t *) p_map_item;
	osm_sa_pr_mcmr_search_ctxt_t *p_ctxt =
	    (osm_sa_pr_mcmr_search_ctxt_t *) context;
	const ib_gid_t *p_recvd_mgid;
	osm_sa_t *sa;

	p_recvd_mgid = p_ctxt->p_mgid;
	sa = p_ctxt->sa;

	/* ignore groups marked for deletion */
	if (p_mgrp->to_be_deleted)
		return;

	/* compare entire MGID so different scope will not sneak in for
	   the same MGID */
	if (memcmp(&p_mgrp->mcmember_rec.mgid, p_recvd_mgid, sizeof(ib_gid_t)))
		return;

	if (p_ctxt->p_mgrp) {
		osm_log(sa->p_log, OSM_LOG_ERROR,
			"__search_mgrp_by_mgid: ERR 1B30: "
			"Multiple MC groups for same MGID\n");
		return;
	}
	p_ctxt->p_mgrp = p_mgrp;
}

/**********************************************************************
 **********************************************************************/
ib_api_status_t
osm_get_mgrp_by_mgid(IN osm_sa_t *sa,
		   IN ib_gid_t *p_mgid,
		   OUT osm_mgrp_t **pp_mgrp)
{
	osm_sa_pr_mcmr_search_ctxt_t mcmr_search_context;

	mcmr_search_context.p_mgid = p_mgid;
	mcmr_search_context.sa = sa;
	mcmr_search_context.p_mgrp = NULL;

	cl_qmap_apply_func(&sa->p_subn->mgrp_mlid_tbl,
			   __search_mgrp_by_mgid, &mcmr_search_context);

	if (mcmr_search_context.p_mgrp == NULL)
		return IB_NOT_FOUND;

	*pp_mgrp = mcmr_search_context.p_mgrp;
	return IB_SUCCESS;
}

/**********************************************************************
 Call this function to find or create a new mgrp.
**********************************************************************/
ib_api_status_t
osm_mcmr_rcv_find_or_create_new_mgrp(IN osm_sa_t * sa,
				     IN ib_net64_t comp_mask,
				     IN ib_member_rec_t *
				     const p_recvd_mcmember_rec,
				     OUT osm_mgrp_t ** pp_mgrp)
{
	ib_api_status_t status;

	status = osm_get_mgrp_by_mgid(sa, &p_recvd_mcmember_rec->mgid, pp_mgrp);
	if (status == IB_SUCCESS)
		return status;
	return osm_mcmr_rcv_create_new_mgrp(sa, comp_mask,
					    p_recvd_mcmember_rec, NULL,
					    pp_mgrp);
}

/*********************************************************************
Process a request for leaving the group
**********************************************************************/
static void
__osm_mcmr_rcv_leave_mgrp(IN osm_sa_t * sa,
			  IN const osm_madw_t * const p_madw)
{
	boolean_t valid;
	osm_mgrp_t *p_mgrp;
	ib_api_status_t status;
	ib_sa_mad_t *p_sa_mad;
	ib_member_rec_t *p_recvd_mcmember_rec;
	ib_member_rec_t mcmember_rec;
	ib_net16_t mlid;
	ib_net16_t sa_status;
	ib_net64_t portguid;
	osm_mcm_port_t *p_mcm_port;
	uint8_t port_join_state;
	uint8_t new_join_state;

	OSM_LOG_ENTER(sa->p_log, __osm_mcmr_rcv_leave_mgrp);

	p_mgrp = NULL;
	p_sa_mad = osm_madw_get_sa_mad_ptr(p_madw);
	p_recvd_mcmember_rec =
	    (ib_member_rec_t *) ib_sa_mad_get_payload_ptr(p_sa_mad);

	mcmember_rec = *p_recvd_mcmember_rec;

	if (osm_log_is_active(sa->p_log, OSM_LOG_DEBUG)) {
		osm_log(sa->p_log, OSM_LOG_DEBUG,
			"__osm_mcmr_rcv_leave_mgrp: Dump of record\n");
		osm_dump_mc_record(sa->p_log, &mcmember_rec, OSM_LOG_DEBUG);
	}

	CL_PLOCK_EXCL_ACQUIRE(sa->p_lock);
	status = osm_get_mgrp_by_mgid(sa, &p_recvd_mcmember_rec->mgid, &p_mgrp);
	if (status == IB_SUCCESS) {
		mlid = p_mgrp->mlid;
		portguid = p_recvd_mcmember_rec->port_gid.unicast.interface_id;

		/* check validity of the delete request o15-0.1.14 */
		valid = __validate_delete(sa,
					  p_mgrp,
					  osm_madw_get_mad_addr_ptr(p_madw),
					  p_recvd_mcmember_rec, &p_mcm_port);

		if (valid) {
			/*
			 * according to the same o15-0.1.14 we get the stored JoinState and the
			 * request JoinState and they must be opposite to leave -
			 * otherwise just update it
			 */
			port_join_state = p_mcm_port->scope_state & 0x0F;
			new_join_state =
			    port_join_state & ~(p_recvd_mcmember_rec->
						scope_state & 0x0F);
			if (new_join_state) {
				/* Just update the result JoinState */
				p_mcm_port->scope_state =
				    new_join_state | (p_mcm_port->
						      scope_state & 0xf0);

				mcmember_rec.scope_state =
				    p_mcm_port->scope_state;

				CL_PLOCK_RELEASE(sa->p_lock);

				osm_log(sa->p_log, OSM_LOG_DEBUG,
					"__osm_mcmr_rcv_leave_mgrp: "
					"After update JoinState != 0. Updating from 0x%X to 0x%X\n",
					port_join_state, new_join_state);
			} else {
				/* we need to return the stored scope state */
				mcmember_rec.scope_state =
				    p_mcm_port->scope_state;

				/* OK we can leave */
				/* note: osm_sm_mcgrp_leave() will release sa->p_lock */

				status =
				    osm_sm_mcgrp_leave(sa->sm, mlid,
						       portguid);
				if (status != IB_SUCCESS) {
					osm_log(sa->p_log, OSM_LOG_ERROR,
						"__osm_mcmr_rcv_leave_mgrp: ERR 1B09: "
						"osm_sm_mcgrp_leave failed\n");
				}
			}
		} else {
			CL_PLOCK_RELEASE(sa->p_lock);
			osm_log(sa->p_log, OSM_LOG_ERROR,
				"__osm_mcmr_rcv_leave_mgrp: ERR 1B25: "
				"Received an invalid delete request for "
				"MGID: 0x%016" PRIx64 " : "
				"0x%016" PRIx64 " for "
				"PortGID: 0x%016" PRIx64 " : "
				"0x%016" PRIx64 "\n",
				cl_ntoh64(p_recvd_mcmember_rec->mgid.unicast.
					  prefix),
				cl_ntoh64(p_recvd_mcmember_rec->mgid.unicast.
					  interface_id),
				cl_ntoh64(p_recvd_mcmember_rec->port_gid.
					  unicast.prefix),
				cl_ntoh64(p_recvd_mcmember_rec->port_gid.
					  unicast.interface_id));
			sa_status = IB_SA_MAD_STATUS_REQ_INVALID;
			osm_sa_send_error(sa, p_madw, sa_status);
			goto Exit;
		}
	} else {
		CL_PLOCK_RELEASE(sa->p_lock);
		osm_log(sa->p_log, OSM_LOG_DEBUG,
			"__osm_mcmr_rcv_leave_mgrp: "
			"Failed since multicast group not present\n");
		sa_status = IB_SA_MAD_STATUS_REQ_INVALID;
		osm_sa_send_error(sa, p_madw, sa_status);
		goto Exit;
	}

	/* Send an SA response */
	__osm_mcmr_rcv_respond(sa, p_madw, &mcmember_rec);

      Exit:
	OSM_LOG_EXIT(sa->p_log);
	return;
}

/**********************************************************************
 Handle a join (or create) request
**********************************************************************/
static void
__osm_mcmr_rcv_join_mgrp(IN osm_sa_t * sa,
			 IN const osm_madw_t * const p_madw)
{
	boolean_t valid;
	osm_mgrp_t *p_mgrp = NULL;
	ib_api_status_t status;
	ib_sa_mad_t *p_sa_mad;
	ib_member_rec_t *p_recvd_mcmember_rec;
	ib_member_rec_t mcmember_rec;
	ib_net16_t sa_status;
	ib_net16_t mlid;
	osm_mcm_port_t *p_mcmr_port;
	ib_net64_t portguid;
	osm_port_t *p_port;
	osm_physp_t *p_physp;
	osm_physp_t *p_request_physp;
	uint8_t is_new_group;	/* TRUE = there is a need to create a group */
	osm_mcast_req_type_t req_type;
	uint8_t join_state;

	OSM_LOG_ENTER(sa->p_log, __osm_mcmr_rcv_join_mgrp);

	p_mgrp = NULL;
	p_sa_mad = osm_madw_get_sa_mad_ptr(p_madw);
	p_recvd_mcmember_rec =
	    (ib_member_rec_t *) ib_sa_mad_get_payload_ptr(p_sa_mad);

	portguid = p_recvd_mcmember_rec->port_gid.unicast.interface_id;

	mcmember_rec = *p_recvd_mcmember_rec;

	if (osm_log_is_active(sa->p_log, OSM_LOG_DEBUG)) {
		osm_log(sa->p_log, OSM_LOG_DEBUG,
			"__osm_mcmr_rcv_join_mgrp: "
			"Dump of incoming record\n");
		osm_dump_mc_record(sa->p_log, &mcmember_rec, OSM_LOG_DEBUG);
	}

	CL_PLOCK_EXCL_ACQUIRE(sa->p_lock);

	/* make sure the requested port guid is known to the SM */
	p_port = osm_get_port_by_guid(sa->p_subn, portguid);
	if (!p_port) {
		CL_PLOCK_RELEASE(sa->p_lock);

		osm_log(sa->p_log, OSM_LOG_DEBUG,
			"__osm_mcmr_rcv_join_mgrp: "
			"Unknown port GUID 0x%016" PRIx64 "\n", portguid);
		sa_status = IB_SA_MAD_STATUS_REQ_INVALID;
		osm_sa_send_error(sa, p_madw, sa_status);
		goto Exit;
	}

	p_physp = p_port->p_physp;
	/* Check that the p_physp and the requester physp are in the same
	   partition. */
	p_request_physp =
	    osm_get_physp_by_mad_addr(sa->p_log,
				      sa->p_subn,
				      osm_madw_get_mad_addr_ptr(p_madw));
	if (p_request_physp == NULL) {
		CL_PLOCK_RELEASE(sa->p_lock);
		goto Exit;
	}

	if (!osm_physp_share_pkey(sa->p_log, p_physp, p_request_physp)) {
		CL_PLOCK_RELEASE(sa->p_lock);

		osm_log(sa->p_log, OSM_LOG_DEBUG,
			"__osm_mcmr_rcv_join_mgrp: "
			"Port and requester don't share pkey\n");
		sa_status = IB_SA_MAD_STATUS_REQ_INVALID;
		osm_sa_send_error(sa, p_madw, sa_status);
		goto Exit;
	}

	ib_member_get_scope_state(p_recvd_mcmember_rec->scope_state, NULL,
				  &join_state);

	/* do we need to create a new group? */
	status = osm_get_mgrp_by_mgid(sa, &p_recvd_mcmember_rec->mgid, &p_mgrp);
	if ((status == IB_NOT_FOUND) || p_mgrp->to_be_deleted) {
		/* check for JoinState.FullMember = 1 o15.0.1.9 */
		if ((join_state & 0x01) != 0x01) {
			CL_PLOCK_RELEASE(sa->p_lock);
			osm_log(sa->p_log, OSM_LOG_ERROR,
				"__osm_mcmr_rcv_join_mgrp: ERR 1B10: "
				"Provided Join State != FullMember - required for create, "
				"MGID: 0x%016" PRIx64 " : "
				"0x%016" PRIx64 " from port 0x%016" PRIx64
				" (%s)\n",
				cl_ntoh64(p_recvd_mcmember_rec->mgid.unicast.
					  prefix),
				cl_ntoh64(p_recvd_mcmember_rec->mgid.unicast.
					  interface_id), cl_ntoh64(portguid),
				p_port->p_node->print_desc);
			sa_status = IB_SA_MAD_STATUS_REQ_INVALID;
			osm_sa_send_error(sa, p_madw, sa_status);
			goto Exit;
		}

		/* check the comp_mask */
		valid = __check_create_comp_mask(p_sa_mad->comp_mask,
						 p_recvd_mcmember_rec);
		if (valid) {
			status = osm_mcmr_rcv_create_new_mgrp(sa,
							      p_sa_mad->
							      comp_mask,
							      p_recvd_mcmember_rec,
							      p_physp, &p_mgrp);
			if (status != IB_SUCCESS) {
				CL_PLOCK_RELEASE(sa->p_lock);
				sa_status = status;
				osm_sa_send_error(sa, p_madw,
						  sa_status);
				goto Exit;
			}
			/* copy the MGID to the result */
			mcmember_rec.mgid = p_mgrp->mcmember_rec.mgid;
		} else {
			CL_PLOCK_RELEASE(sa->p_lock);

			osm_log(sa->p_log, OSM_LOG_ERROR,
				"__osm_mcmr_rcv_join_mgrp: ERR 1B11: "
				"method = %s, "
				"scope_state = 0x%x, "
				"component mask = 0x%016" PRIx64 ", "
				"expected comp mask = 0x%016" PRIx64 ", "
				"MGID: 0x%016" PRIx64 " : "
				"0x%016" PRIx64 " from port 0x%016" PRIx64
				" (%s)\n",
				ib_get_sa_method_str(p_sa_mad->method),
				p_recvd_mcmember_rec->scope_state,
				cl_ntoh64(p_sa_mad->comp_mask),
				CL_NTOH64(REQUIRED_MC_CREATE_COMP_MASK),
				cl_ntoh64(p_recvd_mcmember_rec->mgid.unicast.
					  prefix),
				cl_ntoh64(p_recvd_mcmember_rec->mgid.unicast.
					  interface_id), cl_ntoh64(portguid),
				p_port->p_node->print_desc);

			sa_status = IB_SA_MAD_STATUS_INSUF_COMPS;
			osm_sa_send_error(sa, p_madw, sa_status);
			goto Exit;
		}
		is_new_group = 1;
		req_type = OSM_MCAST_REQ_TYPE_CREATE;
	} else {
		/* no need for a new group */
		is_new_group = 0;
		req_type = OSM_MCAST_REQ_TYPE_JOIN;
	}

	CL_ASSERT(p_mgrp);
	mlid = p_mgrp->mlid;

	/*
	 * o15-0.2.4: If SA supports UD multicast, then SA shall cause an
	 * endport to join an existing multicast group if:
	 * 1. It receives a SubnAdmSet() method for a MCMemberRecord, and
	 *    - WE KNOW THAT ALREADY
	 * 2. The MGID is specified and matches an existing multicast
	 *    group, and
	 *    - WE KNOW THAT ALREADY
	 * 3. The MCMemberRecord:JoinState is not all 0s, and
	 * 4. PortGID is specified and
	 *    - WE KNOW THAT ALREADY (as it matched a real one)
	 * 5. All other components match that existing group, either by
	 *    being wildcarded or by having values identical to those specified
	 *    by the component mask and in use by the group with the exception
	 *    of components such as ProxyJoin and Reserved, which are ignored
	 *    by SA.
	 *
	 * We need to check #3 and #5 here:
	 */
	valid = __validate_more_comp_fields(sa->p_log,
					    p_mgrp,
					    p_recvd_mcmember_rec,
					    p_sa_mad->comp_mask)
	    && __validate_port_caps(sa->p_log, p_mgrp, p_physp)
	    && (join_state != 0);

	if (!valid) {
		/* since we might have created the new group we need to cleanup */
		__cleanup_mgrp(sa, mlid);

		CL_PLOCK_RELEASE(sa->p_lock);

		osm_log(sa->p_log, OSM_LOG_ERROR,
			"__osm_mcmr_rcv_join_mgrp: ERR 1B12: "
			"__validate_more_comp_fields, __validate_port_caps, "
			"or JoinState = 0 failed from port 0x%016" PRIx64
			" (%s), " "sending IB_SA_MAD_STATUS_REQ_INVALID\n",
			cl_ntoh64(portguid), p_port->p_node->print_desc);

		sa_status = IB_SA_MAD_STATUS_REQ_INVALID;
		osm_sa_send_error(sa, p_madw, sa_status);
		goto Exit;
	}

	/*
	 * Do some validation of the modification
	 */
	if (!is_new_group) {
		/*
		 * o15-0.2.1 requires validation of the requesting port
		 * in the case of modification:
		 */
		valid = __validate_modify(sa,
					  p_mgrp,
					  osm_madw_get_mad_addr_ptr(p_madw),
					  p_recvd_mcmember_rec, &p_mcmr_port);
		if (!valid) {
			CL_PLOCK_RELEASE(sa->p_lock);

			osm_log(sa->p_log, OSM_LOG_ERROR,
				"__osm_mcmr_rcv_join_mgrp: ERR 1B13: "
				"__validate_modify failed from port 0x%016"
				PRIx64 " (%s), "
				"sending IB_SA_MAD_STATUS_REQ_INVALID\n",
				cl_ntoh64(portguid),
				p_port->p_node->print_desc);

			sa_status = IB_SA_MAD_STATUS_REQ_INVALID;
			osm_sa_send_error(sa, p_madw, sa_status);
			goto Exit;
		}
	}

	/* create or update existing port (join-state will be updated) */
	status = __add_new_mgrp_port(sa,
				     p_mgrp,
				     p_recvd_mcmember_rec,
				     osm_madw_get_mad_addr_ptr(p_madw),
				     &p_mcmr_port);

	if (status != IB_SUCCESS) {
		/* we fail to add the port so we might need to delete the group */
		__cleanup_mgrp(sa, mlid);

		CL_PLOCK_RELEASE(sa->p_lock);
		if (status == IB_INVALID_PARAMETER)
			sa_status = IB_SA_MAD_STATUS_REQ_INVALID;
		else
			sa_status = IB_SA_MAD_STATUS_NO_RESOURCES;

		osm_sa_send_error(sa, p_madw, sa_status);
		goto Exit;
	}

	/* o15.0.1.11: copy the join state */
	mcmember_rec.scope_state = p_mcmr_port->scope_state;

	/* copy qkey mlid tclass pkey sl_flow_hop mtu rate pkt_life sl_flow_hop */
	__copy_from_create_mc_rec(&mcmember_rec, &p_mgrp->mcmember_rec);

	/* Release the lock as we don't need it. */
	CL_PLOCK_RELEASE(sa->p_lock);

	/* do the actual routing (actually schedule the update) */
	status =
	    osm_sm_mcgrp_join(sa->sm,
			      mlid,
			      p_recvd_mcmember_rec->port_gid.unicast.
			      interface_id, req_type);

	if (status != IB_SUCCESS) {
		osm_log(sa->p_log, OSM_LOG_ERROR,
			"__osm_mcmr_rcv_join_mgrp: ERR 1B14: "
			"osm_sm_mcgrp_join failed from port 0x%016" PRIx64
			" (%s), " "sending IB_SA_MAD_STATUS_NO_RESOURCES\n",
			cl_ntoh64(portguid), p_port->p_node->print_desc);

		CL_PLOCK_EXCL_ACQUIRE(sa->p_lock);

		/* the request for routing failed so we need to remove the port */
		p_mgrp = __get_mgrp_by_mlid(sa, mlid);
		if (p_mgrp != NULL) {
			osm_mgrp_remove_port(sa->p_subn,
					     sa->p_log,
					     p_mgrp,
					     p_recvd_mcmember_rec->port_gid.
					     unicast.interface_id);
			__cleanup_mgrp(sa, mlid);
		}
		CL_PLOCK_RELEASE(sa->p_lock);
		sa_status = IB_SA_MAD_STATUS_NO_RESOURCES;
		osm_sa_send_error(sa, p_madw, sa_status);
		goto Exit;

	}
	/* failed to route */
	if (osm_log_is_active(sa->p_log, OSM_LOG_DEBUG))
		osm_dump_mc_record(sa->p_log, &mcmember_rec, OSM_LOG_DEBUG);

	__osm_mcmr_rcv_respond(sa, p_madw, &mcmember_rec);

      Exit:
	OSM_LOG_EXIT(sa->p_log);
	return;
}

/**********************************************************************
 Add a patched multicast group to the results list
**********************************************************************/
static ib_api_status_t
__osm_mcmr_rcv_new_mcmr(IN osm_sa_t * sa,
			IN const ib_member_rec_t * p_rcvd_rec,
			IN cl_qlist_t * const p_list)
{
	osm_mcmr_item_t *p_rec_item;
	ib_api_status_t status = IB_SUCCESS;

	OSM_LOG_ENTER(sa->p_log, __osm_mcmr_rcv_new_mcmr);

	p_rec_item = malloc(sizeof(*p_rec_item));
	if (p_rec_item == NULL) {
		osm_log(sa->p_log, OSM_LOG_ERROR,
			"__osm_mcmr_rcv_new_mcmr: ERR 1B15: "
			"rec_item alloc failed\n");
		status = IB_INSUFFICIENT_RESOURCES;
		goto Exit;
	}

	memset(p_rec_item, 0, sizeof(*p_rec_item));

	/* HACK: Untrusted requesters should result with 0 Join
	   State, Port Guid, and Proxy */
	p_rec_item->rec = *p_rcvd_rec;
	cl_qlist_insert_tail(p_list, &p_rec_item->list_item);

      Exit:
	OSM_LOG_EXIT(sa->p_log);
	return (status);
}

/**********************************************************************
 Match the given mgrp to the requested mcmr
**********************************************************************/
static void
__osm_sa_mcm_by_comp_mask_cb(IN cl_map_item_t * const p_map_item,
			     IN void *context)
{
	const osm_mgrp_t *const p_mgrp = (osm_mgrp_t *) p_map_item;
	osm_sa_mcmr_search_ctxt_t *const p_ctxt =
	    (osm_sa_mcmr_search_ctxt_t *) context;
	osm_sa_t *sa = p_ctxt->sa;
	const ib_member_rec_t *p_rcvd_rec = p_ctxt->p_mcmember_rec;
	const osm_physp_t *p_req_physp = p_ctxt->p_req_physp;

	/* since we might change scope_state */
	ib_member_rec_t match_rec;
	ib_net64_t comp_mask = p_ctxt->comp_mask;
	osm_mcm_port_t *p_mcm_port;
	ib_net64_t portguid = p_rcvd_rec->port_gid.unicast.interface_id;
	/* will be used for group or port info */
	uint8_t scope_state;
	uint8_t scope_state_mask = 0;
	cl_map_item_t *p_item;
	ib_gid_t port_gid;
	boolean_t proxy_join = FALSE;

	OSM_LOG_ENTER(sa->p_log, __osm_sa_mcm_by_comp_mask_cb);

	osm_log(sa->p_log, OSM_LOG_DEBUG,
		"__osm_sa_mcm_by_comp_mask_cb: "
		"Checking mlid:0x%X\n", cl_ntoh16(p_mgrp->mlid));

	/* the group might be marked for deletion */
	if (p_mgrp->to_be_deleted) {
		osm_log(sa->p_log, OSM_LOG_DEBUG,
			"__osm_sa_mcm_by_comp_mask_cb: "
			"Group mlid:0x%X is marked to be deleted\n",
			cl_ntoh16(p_mgrp->mlid));
		goto Exit;
	}

	/* first try to eliminate the group by MGID, MLID, or P_Key */
	if ((IB_MCR_COMPMASK_MGID & comp_mask) &&
	    memcmp(&p_rcvd_rec->mgid, &p_mgrp->mcmember_rec.mgid,
		   sizeof(ib_gid_t)))
		goto Exit;

	if ((IB_MCR_COMPMASK_MLID & comp_mask) &&
	    memcmp(&p_rcvd_rec->mlid, &p_mgrp->mcmember_rec.mlid,
		   sizeof(uint16_t)))
		goto Exit;

	/* if the requester physical port doesn't have the pkey that is defined for
	   the group - exit. */
	if (!osm_physp_has_pkey(sa->p_log, p_mgrp->mcmember_rec.pkey,
				p_req_physp))
		goto Exit;

	/* now do the rest of the match */
	if ((IB_MCR_COMPMASK_QKEY & comp_mask) &&
	    (p_rcvd_rec->qkey != p_mgrp->mcmember_rec.qkey))
		goto Exit;

	if ((IB_MCR_COMPMASK_PKEY & comp_mask) &&
	    (p_rcvd_rec->pkey != p_mgrp->mcmember_rec.pkey))
		goto Exit;

	if ((IB_MCR_COMPMASK_TCLASS & comp_mask) &&
	    (p_rcvd_rec->tclass != p_mgrp->mcmember_rec.tclass))
		goto Exit;

	/* check SL, Flow, and Hop limit */
	{
		uint8_t mgrp_sl, query_sl;
		uint32_t mgrp_flow, query_flow;
		uint8_t mgrp_hop, query_hop;

		ib_member_get_sl_flow_hop(p_rcvd_rec->sl_flow_hop,
					  &query_sl, &query_flow, &query_hop);

		ib_member_get_sl_flow_hop(p_mgrp->mcmember_rec.sl_flow_hop,
					  &mgrp_sl, &mgrp_flow, &mgrp_hop);

		if (IB_MCR_COMPMASK_SL & comp_mask)
			if (query_sl != mgrp_sl)
				goto Exit;

		if (IB_MCR_COMPMASK_FLOW & comp_mask)
			if (query_flow != mgrp_flow)
				goto Exit;

		if (IB_MCR_COMPMASK_HOP & comp_mask)
			if (query_hop != mgrp_hop)
				goto Exit;
	}

	if ((IB_MCR_COMPMASK_PROXY & comp_mask) &&
	    (p_rcvd_rec->proxy_join != p_mgrp->mcmember_rec.proxy_join))
		goto Exit;

	/* need to validate mtu, rate, and pkt_lifetime fields */
	if (__validate_more_comp_fields(sa->p_log,
					p_mgrp, p_rcvd_rec, comp_mask) == FALSE)
		goto Exit;

	/* Port specific fields */
	/* so did we get the PortGUID mask */
	if (IB_MCR_COMPMASK_PORT_GID & comp_mask) {
		/* try to find this port */
		if (osm_mgrp_is_port_present(p_mgrp, portguid, &p_mcm_port)) {
			scope_state = p_mcm_port->scope_state;
			memcpy(&port_gid, &(p_mcm_port->port_gid),
			       sizeof(ib_gid_t));
			proxy_join = p_mcm_port->proxy_join;
		} else {
			/* port not in group */
			goto Exit;
		}
	} else {
		/* point to the group information */
		scope_state = p_mgrp->mcmember_rec.scope_state;
	}

	if (IB_MCR_COMPMASK_SCOPE & comp_mask)
		scope_state_mask = 0xF0;

	if (IB_MCR_COMPMASK_JOIN_STATE & comp_mask)
		scope_state_mask = scope_state_mask | 0x0F;

	/* Many MC records returned */
	if ((p_ctxt->trusted_req == TRUE)
	    && !(IB_MCR_COMPMASK_PORT_GID & comp_mask)) {
		osm_log(sa->p_log, OSM_LOG_DEBUG,
			"__osm_sa_mcm_by_comp_mask_cb: "
			"Trusted req is TRUE and no specific port defined\n");

		/* return all the ports that match in this MC group */
		p_item = cl_qmap_head(&(p_mgrp->mcm_port_tbl));
		while (p_item != cl_qmap_end(&(p_mgrp->mcm_port_tbl))) {
			p_mcm_port = (osm_mcm_port_t *) p_item;

			if ((scope_state_mask & p_rcvd_rec->scope_state) ==
			    (scope_state_mask & p_mcm_port->scope_state)) {
				/* add to the list */
				match_rec = p_mgrp->mcmember_rec;
				match_rec.scope_state = p_mcm_port->scope_state;
				memcpy(&(match_rec.port_gid),
				       &(p_mcm_port->port_gid),
				       sizeof(ib_gid_t));
				osm_log(sa->p_log, OSM_LOG_DEBUG,
					"__osm_sa_mcm_by_comp_mask_cb: "
					"Record of port_gid: 0x%016" PRIx64
					"0x%016" PRIx64
					" in multicast_lid: 0x%X is returned\n",
					cl_ntoh64(match_rec.port_gid.unicast.
						  prefix),
					cl_ntoh64(match_rec.port_gid.unicast.
						  interface_id),
					cl_ntoh16(p_mgrp->mlid)
				    );

				match_rec.proxy_join =
				    (uint8_t) (p_mcm_port->proxy_join);

				__osm_mcmr_rcv_new_mcmr(sa, &match_rec,
							p_ctxt->p_list);
			}
			p_item = cl_qmap_next(p_item);
		}
	}
	/* One MC record returned */
	else {
		if ((scope_state_mask & p_rcvd_rec->scope_state) !=
		    (scope_state_mask & scope_state))
			goto Exit;

		/* add to the list */
		match_rec = p_mgrp->mcmember_rec;
		match_rec.scope_state = scope_state;
		memcpy(&(match_rec.port_gid), &port_gid, sizeof(ib_gid_t));
		match_rec.proxy_join = (uint8_t) proxy_join;

		__osm_mcmr_rcv_new_mcmr(sa, &match_rec, p_ctxt->p_list);
	}

      Exit:
	OSM_LOG_EXIT(sa->p_log);
}

/**********************************************************************
 Handle a query request
**********************************************************************/
static void
__osm_mcmr_query_mgrp(IN osm_sa_t * sa,
		      IN const osm_madw_t * const p_madw)
{
	const ib_sa_mad_t *p_rcvd_mad;
	const ib_member_rec_t *p_rcvd_rec;
	cl_qlist_t rec_list;
	osm_madw_t *p_resp_madw;
	ib_sa_mad_t *p_resp_sa_mad;
	ib_member_rec_t *p_resp_rec;
	uint32_t num_rec, pre_trim_num_rec;
#ifndef VENDOR_RMPP_SUPPORT
	uint32_t trim_num_rec;
#endif
	uint32_t i;
	osm_sa_mcmr_search_ctxt_t context;
	osm_mcmr_item_t *p_rec_item;
	ib_api_status_t status;
	ib_net64_t comp_mask;
	osm_physp_t *p_req_physp;
	boolean_t trusted_req;

	OSM_LOG_ENTER(sa->p_log, __osm_mcmr_query_mgrp);

	p_rcvd_mad = osm_madw_get_sa_mad_ptr(p_madw);
	p_rcvd_rec = (ib_member_rec_t *) ib_sa_mad_get_payload_ptr(p_rcvd_mad);
	comp_mask = p_rcvd_mad->comp_mask;

	/*
	   if sm_key is not zero and does not match we never get here
	   see main SA receiver
	 */
	trusted_req = (p_rcvd_mad->sm_key != 0);

	/* update the requester physical port. */
	p_req_physp = osm_get_physp_by_mad_addr(sa->p_log,
						sa->p_subn,
						osm_madw_get_mad_addr_ptr
						(p_madw));
	if (p_req_physp == NULL) {
		osm_log(sa->p_log, OSM_LOG_ERROR,
			"__osm_mcmr_query_mgrp: ERR 1B04: "
			"Cannot find requester physical port\n");
		goto Exit;
	}

	cl_qlist_init(&rec_list);

	context.p_mcmember_rec = p_rcvd_rec;
	context.p_list = &rec_list;
	context.comp_mask = p_rcvd_mad->comp_mask;
	context.sa = sa;
	context.p_req_physp = p_req_physp;
	context.trusted_req = trusted_req;

	CL_PLOCK_ACQUIRE(sa->p_lock);

	/* simply go over all MCGs and match */
	cl_qmap_apply_func(&sa->p_subn->mgrp_mlid_tbl,
			   __osm_sa_mcm_by_comp_mask_cb, &context);

	CL_PLOCK_RELEASE(sa->p_lock);

	num_rec = cl_qlist_count(&rec_list);

	/*
	 * C15-0.1.30:
	 * If we do a SubnAdmGet and got more than one record it is an error !
	 */
	if ((p_rcvd_mad->method == IB_MAD_METHOD_GET) && (num_rec > 1)) {
		osm_log(sa->p_log, OSM_LOG_ERROR,
			"__osm_mcmr_query_mgrp: ERR 1B05: "
			"Got more than one record for SubnAdmGet (%u)\n",
			num_rec);
		osm_sa_send_error(sa, p_madw,
				  IB_SA_MAD_STATUS_TOO_MANY_RECORDS);

		/* need to set the mem free ... */
		p_rec_item =
		    (osm_mcmr_item_t *) cl_qlist_remove_head(&rec_list);
		while (p_rec_item !=
		       (osm_mcmr_item_t *) cl_qlist_end(&rec_list)) {
			free(p_rec_item);
			p_rec_item =
			    (osm_mcmr_item_t *) cl_qlist_remove_head(&rec_list);
		}

		goto Exit;
	}

	pre_trim_num_rec = num_rec;
#ifndef VENDOR_RMPP_SUPPORT
	trim_num_rec =
	    (MAD_BLOCK_SIZE - IB_SA_MAD_HDR_SIZE) / sizeof(ib_member_rec_t);
	if (trim_num_rec < num_rec) {
		osm_log(sa->p_log, OSM_LOG_VERBOSE,
			"__osm_mcmr_query_mgrp: "
			"Number of records:%u trimmed to:%u to fit in one MAD\n",
			num_rec, trim_num_rec);
		num_rec = trim_num_rec;
	}
#endif

	osm_log(sa->p_log, OSM_LOG_DEBUG,
		"__osm_mcmr_query_mgrp: " "Returning %u records\n", num_rec);

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
				       num_rec * sizeof(ib_member_rec_t) +
				       IB_SA_MAD_HDR_SIZE,
				       osm_madw_get_mad_addr_ptr(p_madw));

	if (!p_resp_madw) {
		osm_log(sa->p_log, OSM_LOG_ERROR,
			"__osm_mcmr_query_mgrp: ERR 1B16: "
			"osm_mad_pool_get failed\n");

		for (i = 0; i < num_rec; i++) {
			p_rec_item =
			    (osm_mcmr_item_t *) cl_qlist_remove_head(&rec_list);
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
	    ib_get_attr_offset(sizeof(ib_member_rec_t));

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

	p_resp_rec =
	    (ib_member_rec_t *) ib_sa_mad_get_payload_ptr(p_resp_sa_mad);

	/*
	   p923 - The PortGID, JoinState and ProxyJoin shall be zero,
	   except in the case of a trusted request.
	   Note: In the mad controller we check that the SM_Key received on
	   the mad is valid. Meaning - is either zero or equal to the local
	   sm_key.
	 */

	for (i = 0; i < pre_trim_num_rec; i++) {
		p_rec_item =
		    (osm_mcmr_item_t *) cl_qlist_remove_head(&rec_list);
		/* copy only if not trimmed */
		if (i < num_rec) {
			*p_resp_rec = p_rec_item->rec;
			if (trusted_req == FALSE) {
				memset(&p_resp_rec->port_gid, 0,
				       sizeof(ib_gid_t));
				ib_member_set_join_state(p_resp_rec, 0);
				p_resp_rec->proxy_join = 0;
			}
		}
		free(p_rec_item);
		p_resp_rec++;
	}

	CL_ASSERT(cl_is_qlist_empty(&rec_list));

	status = osm_sa_vendor_send(p_resp_madw->h_bind, p_resp_madw, FALSE,
				    sa->p_subn);
	if (status != IB_SUCCESS) {
		osm_log(sa->p_log, OSM_LOG_ERROR,
			"__osm_mcmr_query_mgrp: ERR 1B17: "
			"osm_sa_vendor_send status = %s\n",
			ib_get_err_str(status));
		goto Exit;
	}

      Exit:
	OSM_LOG_EXIT(sa->p_log);
}

/**********************************************************************
 **********************************************************************/
void osm_mcmr_rcv_process(IN void *context, IN void *data)
{
	osm_sa_t *sa = context;
	osm_madw_t *p_madw = data;
	ib_sa_mad_t *p_sa_mad;
	ib_net16_t sa_status = IB_SA_MAD_STATUS_REQ_INVALID;
	ib_member_rec_t *p_recvd_mcmember_rec;
	boolean_t valid;

	CL_ASSERT(sa);

	OSM_LOG_ENTER(sa->p_log, osm_mcmr_rcv_process);

	CL_ASSERT(p_madw);

	p_sa_mad = osm_madw_get_sa_mad_ptr(p_madw);
	p_recvd_mcmember_rec =
	    (ib_member_rec_t *) ib_sa_mad_get_payload_ptr(p_sa_mad);

	CL_ASSERT(p_sa_mad->attr_id == IB_MAD_ATTR_MCMEMBER_RECORD);

	switch (p_sa_mad->method) {
	case IB_MAD_METHOD_SET:
		valid = __check_join_comp_mask(p_sa_mad->comp_mask);
		if (!valid) {
			osm_log(sa->p_log, OSM_LOG_ERROR,
				"osm_mcmr_rcv_process: ERR 1B18: "
				"component mask = 0x%016" PRIx64 ", "
				"expected comp mask = 0x%016" PRIx64 " ,"
				"MGID: 0x%016" PRIx64 " : "
				"0x%016" PRIx64 " for "
				"PortGID: 0x%016" PRIx64 " : "
				"0x%016" PRIx64 "\n",
				cl_ntoh64(p_sa_mad->comp_mask),
				CL_NTOH64(JOIN_MC_COMP_MASK),
				cl_ntoh64(p_recvd_mcmember_rec->mgid.unicast.
					  prefix),
				cl_ntoh64(p_recvd_mcmember_rec->mgid.unicast.
					  interface_id),
				cl_ntoh64(p_recvd_mcmember_rec->port_gid.
					  unicast.prefix),
				cl_ntoh64(p_recvd_mcmember_rec->port_gid.
					  unicast.interface_id));

			osm_sa_send_error(sa, p_madw, sa_status);
			goto Exit;
		}

		/*
		 * Join or Create Multicast Group
		 */
		__osm_mcmr_rcv_join_mgrp(sa, p_madw);
		break;
	case IB_MAD_METHOD_DELETE:
		valid = __check_join_comp_mask(p_sa_mad->comp_mask);
		if (!valid) {
			osm_log(sa->p_log, OSM_LOG_ERROR,
				"osm_mcmr_rcv_process: ERR 1B20: "
				"component mask = 0x%016" PRIx64 ", "
				"expected comp mask = 0x%016" PRIx64 "\n",
				cl_ntoh64(p_sa_mad->comp_mask),
				CL_NTOH64(JOIN_MC_COMP_MASK));

			osm_sa_send_error(sa, p_madw, sa_status);
			goto Exit;
		}

		/*
		 * Leave Multicast Group
		 */
		__osm_mcmr_rcv_leave_mgrp(sa, p_madw);
		break;
	case IB_MAD_METHOD_GET:
	case IB_MAD_METHOD_GETTABLE:
		/*
		 * Querying a Multicast Group
		 */
		__osm_mcmr_query_mgrp(sa, p_madw);
		break;
	default:
		osm_log(sa->p_log, OSM_LOG_ERROR,
			"osm_mcmr_rcv_process: ERR 1B21: "
			"Unsupported Method (%s)\n",
			ib_get_sa_method_str(p_sa_mad->method));
		osm_sa_send_error(sa, p_madw,
				  IB_MAD_STATUS_UNSUP_METHOD_ATTR);
		break;
	}

      Exit:
	OSM_LOG_EXIT(sa->p_log);
	return;
}

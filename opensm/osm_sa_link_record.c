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
 *    Implementation of osm_lr_rcv_t.
 * This object represents the LinkRecord Receiver object.
 * This object is part of the opensm family of objects.
 *
 * Environment:
 *    Linux User Mode
 *
 * $Revision: 1.8 $
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <string.h>
#include <iba/ib_types.h>
#include <complib/cl_qmap.h>
#include <complib/cl_debug.h>
#include <vendor/osm_vendor_api.h>
#include <opensm/osm_node.h>
#include <opensm/osm_switch.h>
#include <opensm/osm_helper.h>
#include <opensm/osm_pkey.h>
#include <opensm/osm_sa.h>

typedef struct _osm_lr_item {
	cl_list_item_t list_item;
	ib_link_record_t link_rec;
} osm_lr_item_t;

/**********************************************************************
 **********************************************************************/
static void
__osm_lr_rcv_build_physp_link(IN osm_sa_t * sa,
			      IN const ib_net16_t from_lid,
			      IN const ib_net16_t to_lid,
			      IN const uint8_t from_port,
			      IN const uint8_t to_port, IN cl_qlist_t * p_list)
{
	osm_lr_item_t *p_lr_item;

	p_lr_item = malloc(sizeof(*p_lr_item));
	if (p_lr_item == NULL) {
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 1801: "
			"Unable to acquire link record\n"
			"\t\t\t\tFrom port 0x%u\n"
			"\t\t\t\tTo port   0x%u\n"
			"\t\t\t\tFrom lid  0x%X\n"
			"\t\t\t\tTo lid    0x%X\n",
			from_port, to_port,
			cl_ntoh16(from_lid), cl_ntoh16(to_lid));
		return;
	}
	memset(p_lr_item, 0, sizeof(*p_lr_item));

	p_lr_item->link_rec.from_port_num = from_port;
	p_lr_item->link_rec.to_port_num = to_port;
	p_lr_item->link_rec.to_lid = to_lid;
	p_lr_item->link_rec.from_lid = from_lid;

	cl_qlist_insert_tail(p_list, &p_lr_item->list_item);
}

/**********************************************************************
 **********************************************************************/
static void
__get_base_lid(IN const osm_physp_t * p_physp, OUT ib_net16_t * p_base_lid)
{
	if (p_physp->p_node->node_info.node_type == IB_NODE_TYPE_SWITCH)
		*p_base_lid = osm_physp_get_base_lid
			      (osm_node_get_physp_ptr(p_physp->p_node, 0));
	else
		*p_base_lid = osm_physp_get_base_lid(p_physp);
}

/**********************************************************************
 **********************************************************************/
static void
__osm_lr_rcv_get_physp_link(IN osm_sa_t * sa,
			    IN const ib_link_record_t * const p_lr,
			    IN const osm_physp_t * p_src_physp,
			    IN const osm_physp_t * p_dest_physp,
			    IN const ib_net64_t comp_mask,
			    IN cl_qlist_t * const p_list,
			    IN const osm_physp_t * p_req_physp)
{
	uint8_t src_port_num;
	uint8_t dest_port_num;
	ib_net16_t from_base_lid;
	ib_net16_t to_base_lid;
	ib_net16_t lmc_mask;

	OSM_LOG_ENTER(sa->p_log);

	/*
	   If only one end of the link is specified, determine
	   the other side.
	 */
	if (p_src_physp) {
		if (p_dest_physp) {
			/*
			   Ensure the two physp's are actually connected.
			   If not, bail out.
			 */
			if (osm_physp_get_remote(p_src_physp) != p_dest_physp)
				goto Exit;
		} else {
			p_dest_physp = osm_physp_get_remote(p_src_physp);
			if (p_dest_physp == NULL)
				goto Exit;
		}
	} else {
		if (p_dest_physp) {
			p_src_physp = osm_physp_get_remote(p_dest_physp);
			if (p_src_physp == NULL)
				goto Exit;
		} else
			goto Exit;	/* no physp's, so nothing to do */
	}

	/* Check that the p_src_physp, p_dest_physp and p_req_physp
	   all share a pkey (doesn't have to be the same p_key). */
	if (!osm_physp_share_pkey(sa->p_log, p_src_physp, p_dest_physp)) {
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
			"Source and Dest PhysPorts do not share PKey\n");
		goto Exit;
	}
	if (!osm_physp_share_pkey(sa->p_log, p_src_physp, p_req_physp)) {
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
			"Source and Requester PhysPorts do not share PKey\n");
		goto Exit;
	}
	if (!osm_physp_share_pkey(sa->p_log, p_req_physp, p_dest_physp)) {
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
			"Requester and Dest PhysPorts do not share PKey\n");
		goto Exit;
	}

	src_port_num = osm_physp_get_port_num(p_src_physp);
	dest_port_num = osm_physp_get_port_num(p_dest_physp);

	if (comp_mask & IB_LR_COMPMASK_FROM_PORT)
		if (src_port_num != p_lr->from_port_num)
			goto Exit;

	if (comp_mask & IB_LR_COMPMASK_TO_PORT)
		if (dest_port_num != p_lr->to_port_num)
			goto Exit;

	__get_base_lid(p_src_physp, &from_base_lid);
	__get_base_lid(p_dest_physp, &to_base_lid);

	lmc_mask = ~((1 << sa->p_subn->opt.lmc) - 1);
	lmc_mask = cl_hton16(lmc_mask);

	if (comp_mask & IB_LR_COMPMASK_FROM_LID)
		if (from_base_lid != (p_lr->from_lid & lmc_mask))
			goto Exit;

	if (comp_mask & IB_LR_COMPMASK_TO_LID)
		if (to_base_lid != (p_lr->to_lid & lmc_mask))
			goto Exit;

	if (osm_log_is_active(sa->p_log, OSM_LOG_DEBUG))
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG, "Acquiring link record\n"
			"\t\t\t\tsrc port 0x%" PRIx64 " (port 0x%X)"
			", dest port 0x%" PRIx64 " (port 0x%X)\n",
			cl_ntoh64(osm_physp_get_port_guid(p_src_physp)),
			src_port_num,
			cl_ntoh64(osm_physp_get_port_guid(p_dest_physp)),
			dest_port_num);


	__osm_lr_rcv_build_physp_link(sa, from_base_lid, to_base_lid,
				      src_port_num, dest_port_num, p_list);

Exit:
	OSM_LOG_EXIT(sa->p_log);
}

/**********************************************************************
 **********************************************************************/
static void
__osm_lr_rcv_get_port_links(IN osm_sa_t * sa,
			    IN const ib_link_record_t * const p_lr,
			    IN const osm_port_t * p_src_port,
			    IN const osm_port_t * p_dest_port,
			    IN const ib_net64_t comp_mask,
			    IN cl_qlist_t * const p_list,
			    IN const osm_physp_t * p_req_physp)
{
	const osm_physp_t *p_src_physp;
	const osm_physp_t *p_dest_physp;
	const cl_qmap_t *p_node_tbl;
	osm_node_t * p_node;
	uint8_t port_num;
	uint8_t num_ports;
	uint8_t dest_num_ports;
	uint8_t dest_port_num;

	OSM_LOG_ENTER(sa->p_log);

	if (p_src_port) {
		if (p_dest_port) {
			/*
			   Build an LR for every link connected between both ports.
			   The inner function will discard physp combinations
			   that do not actually connect.  Don't bother screening
			   for that here.
			 */
			num_ports = osm_node_get_num_physp(p_src_port->p_node);
			dest_num_ports =
			    osm_node_get_num_physp(p_dest_port->p_node);
			for (port_num = 1; port_num < num_ports; port_num++) {
				p_src_physp =
				    osm_node_get_physp_ptr(p_src_port->p_node,
							   port_num);
				for (dest_port_num = 1;
				     dest_port_num < dest_num_ports;
				     dest_port_num++) {
					p_dest_physp =
					    osm_node_get_physp_ptr(p_dest_port->
								   p_node,
								   dest_port_num);
					/* both physical ports should be with data */
					if (p_src_physp && p_dest_physp)
						__osm_lr_rcv_get_physp_link
						    (sa, p_lr, p_src_physp,
						     p_dest_physp, comp_mask,
						     p_list, p_req_physp);
				}
			}
		} else {
			/*
			   Build an LR for every link connected from the source port.
			 */
			if (comp_mask & IB_LR_COMPMASK_FROM_PORT) {
				port_num = p_lr->from_port_num;
				/* If the port number is out of the range of the p_src_port, then
				   this couldn't be a relevant record. */
				if (port_num <
				    p_src_port->p_node->physp_tbl_size) {
					p_src_physp =
					    osm_node_get_physp_ptr(p_src_port->
								   p_node,
								   port_num);
					if (p_src_physp)
						__osm_lr_rcv_get_physp_link
						    (sa, p_lr, p_src_physp,
						     NULL, comp_mask, p_list,
						     p_req_physp);
				}
			} else {
				num_ports =
				    osm_node_get_num_physp(p_src_port->p_node);
				for (port_num = 1; port_num < num_ports;
				     port_num++) {
					p_src_physp =
					    osm_node_get_physp_ptr(p_src_port->
								   p_node,
								   port_num);
					if (p_src_physp)
						__osm_lr_rcv_get_physp_link
						    (sa, p_lr, p_src_physp,
						     NULL, comp_mask, p_list,
						     p_req_physp);
				}
			}
		}
	} else {
		if (p_dest_port) {
			/*
			   Build an LR for every link connected to the dest port.
			 */
			if (comp_mask & IB_LR_COMPMASK_TO_PORT) {
				port_num = p_lr->to_port_num;
				/* If the port number is out of the range of the p_dest_port, then
				   this couldn't be a relevant record. */
				if (port_num <
				    p_dest_port->p_node->physp_tbl_size) {
					p_dest_physp =
					    osm_node_get_physp_ptr(p_dest_port->
								   p_node,
								   port_num);
					if (p_dest_physp)
						__osm_lr_rcv_get_physp_link
						    (sa, p_lr, NULL,
						     p_dest_physp, comp_mask,
						     p_list, p_req_physp);
				}
			} else {
				num_ports =
				    osm_node_get_num_physp(p_dest_port->p_node);
				for (port_num = 1; port_num < num_ports;
				     port_num++) {
					p_dest_physp =
					    osm_node_get_physp_ptr(p_dest_port->
								   p_node,
								   port_num);
					if (p_dest_physp)
						__osm_lr_rcv_get_physp_link
						    (sa, p_lr, NULL,
						     p_dest_physp, comp_mask,
						     p_list, p_req_physp);
				}
			}
		} else {
			/*
			   Process the world (recurse once back into this function).
			 */
			p_node_tbl = &sa->p_subn->node_guid_tbl;
			p_node = (osm_node_t *)cl_qmap_head(p_node_tbl);

			while (p_node != (osm_node_t *)cl_qmap_end(p_node_tbl)) {
				/*
				   Get only one port for each node.
				   After the recursive call, this function will
				   scan all the ports of this node anyway.
				 */
				p_src_physp = osm_node_get_any_physp_ptr(p_node);
				p_src_port = osm_get_port_by_guid(sa->p_subn,
				        osm_physp_get_port_guid(p_src_physp));
				__osm_lr_rcv_get_port_links(sa, p_lr,
							    p_src_port, NULL,
							    comp_mask, p_list,
							    p_req_physp);
				p_node = (osm_node_t *) cl_qmap_next(&p_node->
								     map_item);
			}
		}
	}

	OSM_LOG_EXIT(sa->p_log);
}

/**********************************************************************
 Returns the SA status to return to the client.
 **********************************************************************/
static ib_net16_t
__osm_lr_rcv_get_end_points(IN osm_sa_t * sa,
			    IN const osm_madw_t * const p_madw,
			    OUT const osm_port_t ** const pp_src_port,
			    OUT const osm_port_t ** const pp_dest_port)
{
	const ib_link_record_t *p_lr;
	const ib_sa_mad_t *p_sa_mad;
	ib_net64_t comp_mask;
	ib_api_status_t status;
	ib_net16_t sa_status = IB_SA_MAD_STATUS_SUCCESS;

	OSM_LOG_ENTER(sa->p_log);

	/*
	   Determine what fields are valid and then get a pointer
	   to the source and destination port objects, if possible.
	 */
	p_sa_mad = osm_madw_get_sa_mad_ptr(p_madw);
	p_lr = (ib_link_record_t *) ib_sa_mad_get_payload_ptr(p_sa_mad);

	comp_mask = p_sa_mad->comp_mask;
	*pp_src_port = NULL;
	*pp_dest_port = NULL;

	if (p_sa_mad->comp_mask & IB_LR_COMPMASK_FROM_LID) {
		status = osm_get_port_by_base_lid(sa->p_subn,
						  p_lr->from_lid, pp_src_port);

		if ((status != IB_SUCCESS) || (*pp_src_port == NULL)) {
			/*
			   This 'error' is the client's fault (bad lid) so
			   don't enter it as an error in our own log.
			   Return an error response to the client.
			 */
			OSM_LOG(sa->p_log, OSM_LOG_VERBOSE,
				"No source port with LID = 0x%X\n",
				cl_ntoh16(p_lr->from_lid));

			sa_status = IB_SA_MAD_STATUS_NO_RECORDS;
			goto Exit;
		}
	}

	if (p_sa_mad->comp_mask & IB_LR_COMPMASK_TO_LID) {
		status = osm_get_port_by_base_lid(sa->p_subn,
						  p_lr->to_lid, pp_dest_port);

		if ((status != IB_SUCCESS) || (*pp_dest_port == NULL)) {
			/*
			   This 'error' is the client's fault (bad lid) so
			   don't enter it as an error in our own log.
			   Return an error response to the client.
			 */
			OSM_LOG(sa->p_log, OSM_LOG_VERBOSE,
				"No dest port with LID = 0x%X\n",
				cl_ntoh16(p_lr->to_lid));

			sa_status = IB_SA_MAD_STATUS_NO_RECORDS;
			goto Exit;
		}
	}

Exit:
	OSM_LOG_EXIT(sa->p_log);
	return (sa_status);
}

/**********************************************************************
 **********************************************************************/
static void
__osm_lr_rcv_respond(IN osm_sa_t * sa,
		     IN const osm_madw_t * const p_madw,
		     IN cl_qlist_t * const p_list)
{
	osm_madw_t *p_resp_madw;
	const ib_sa_mad_t *p_sa_mad;
	ib_sa_mad_t *p_resp_sa_mad;
	size_t num_rec, num_copied;
#ifndef VENDOR_RMPP_SUPPORT
	size_t trim_num_rec;
#endif
	ib_link_record_t *p_resp_lr;
	osm_lr_item_t *p_lr_item;
	const ib_sa_mad_t *p_rcvd_mad = osm_madw_get_sa_mad_ptr(p_madw);

	OSM_LOG_ENTER(sa->p_log);

	num_rec = cl_qlist_count(p_list);
	/*
	 * C15-0.1.30:
	 * If we do a SubnAdmGet and got more than one record it is an error !
	 */
	if (p_rcvd_mad->method == IB_MAD_METHOD_GET && num_rec > 1) {
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 1806: "
			"Got more than one record for SubnAdmGet (%zu)\n",
			num_rec);
		osm_sa_send_error(sa, p_madw,
				  IB_SA_MAD_STATUS_TOO_MANY_RECORDS);

		/* need to set the mem free ... */
		p_lr_item = (osm_lr_item_t *) cl_qlist_remove_head(p_list);
		while (p_lr_item != (osm_lr_item_t *) cl_qlist_end(p_list)) {
			free(p_lr_item);
			p_lr_item =
			    (osm_lr_item_t *) cl_qlist_remove_head(p_list);
		}

		goto Exit;
	}
#ifndef VENDOR_RMPP_SUPPORT
	trim_num_rec =
	    (MAD_BLOCK_SIZE - IB_SA_MAD_HDR_SIZE) / sizeof(ib_link_record_t);
	if (trim_num_rec < num_rec) {
		OSM_LOG(sa->p_log, OSM_LOG_VERBOSE,
			"Number of records:%u trimmed to:%u to fit in one MAD\n",
			num_rec, trim_num_rec);
		num_rec = trim_num_rec;
	}
#endif

	if (osm_log_is_active(sa->p_log, OSM_LOG_DEBUG)) {
		OSM_LOG(sa->p_log, OSM_LOG_DEBUG,
			"Generating response with %zu records", num_rec);
	}

	/*
	   Get a MAD to reply. Address of Mad is in the received mad_wrapper
	 */
	p_resp_madw = osm_mad_pool_get(sa->p_mad_pool, p_madw->h_bind,
				       num_rec * sizeof(ib_link_record_t) +
				       IB_SA_MAD_HDR_SIZE, &p_madw->mad_addr);
	if (!p_resp_madw) {
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 1802: "
			"Unable to allocate MAD\n");
		/* Release the quick pool items */
		p_lr_item = (osm_lr_item_t *) cl_qlist_remove_head(p_list);
		while (p_lr_item != (osm_lr_item_t *) cl_qlist_end(p_list)) {
			free(p_lr_item);
			p_lr_item =
			    (osm_lr_item_t *) cl_qlist_remove_head(p_list);
		}

		goto Exit;
	}

	p_sa_mad = osm_madw_get_sa_mad_ptr(p_madw);
	p_resp_sa_mad = osm_madw_get_sa_mad_ptr(p_resp_madw);

	/* Copy the header from the request to response */
	memcpy(p_resp_sa_mad, p_sa_mad, IB_SA_MAD_HDR_SIZE);
	p_resp_sa_mad->method |= IB_MAD_METHOD_RESP_MASK;
	p_resp_sa_mad->attr_offset =
	    ib_get_attr_offset(sizeof(ib_link_record_t));
	/* C15-0.1.5 - always return SM_Key = 0 (table table 185 p 884) */
	p_resp_sa_mad->sm_key = 0;

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

	p_resp_lr =
	    (ib_link_record_t *) ib_sa_mad_get_payload_ptr(p_resp_sa_mad);

	if ((p_rcvd_mad->method == IB_MAD_METHOD_GET) && (num_rec == 0)) {
		p_resp_sa_mad->status = IB_SA_MAD_STATUS_NO_RECORDS;
		memset(p_resp_lr, 0, sizeof(*p_resp_lr));
	} else {
		p_lr_item = (osm_lr_item_t *) cl_qlist_remove_head(p_list);
		/* we need to track the number of copied items so we can
		 * stop the copy - but clear them all
		 */
		num_copied = 0;
		while (p_lr_item != (osm_lr_item_t *) cl_qlist_end(p_list)) {
			/*  Copy the Link Records from the list into the MAD */
			/*  only if we did not go over the mad size (since we might trimmed it) */
			if (num_copied < num_rec) {
				*p_resp_lr = p_lr_item->link_rec;
				num_copied++;
			}
			free(p_lr_item);
			p_resp_lr++;
			p_lr_item =
			    (osm_lr_item_t *) cl_qlist_remove_head(p_list);
		}
	}

	osm_sa_vendor_send(p_resp_madw->h_bind, p_resp_madw, FALSE, sa->p_subn);

Exit:
	OSM_LOG_EXIT(sa->p_log);
}

/**********************************************************************
 **********************************************************************/
void osm_lr_rcv_process(IN void *context, IN void *data)
{
	osm_sa_t *sa = context;
	osm_madw_t *p_madw = data;
	const ib_link_record_t *p_lr;
	const ib_sa_mad_t *p_sa_mad;
	const osm_port_t *p_src_port;
	const osm_port_t *p_dest_port;
	cl_qlist_t lr_list;
	ib_net16_t sa_status;
	osm_physp_t *p_req_physp;

	OSM_LOG_ENTER(sa->p_log);

	CL_ASSERT(p_madw);

	p_sa_mad = osm_madw_get_sa_mad_ptr(p_madw);
	p_lr = (ib_link_record_t *) ib_sa_mad_get_payload_ptr(p_sa_mad);

	CL_ASSERT(p_sa_mad->attr_id == IB_MAD_ATTR_LINK_RECORD);

	/* we only support SubnAdmGet and SubnAdmGetTable methods */
	if (p_sa_mad->method != IB_MAD_METHOD_GET &&
	    p_sa_mad->method != IB_MAD_METHOD_GETTABLE) {
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 1804: "
			"Unsupported Method (%s)\n",
			ib_get_sa_method_str(p_sa_mad->method));
		osm_sa_send_error(sa, p_madw, IB_MAD_STATUS_UNSUP_METHOD_ATTR);
		goto Exit;
	}

	/* update the requester physical port. */
	p_req_physp = osm_get_physp_by_mad_addr(sa->p_log, sa->p_subn,
						osm_madw_get_mad_addr_ptr
						(p_madw));
	if (p_req_physp == NULL) {
		OSM_LOG(sa->p_log, OSM_LOG_ERROR, "ERR 1805: "
			"Cannot find requester physical port\n");
		goto Exit;
	}

	if (osm_log_is_active(sa->p_log, OSM_LOG_DEBUG))
		osm_dump_link_record(sa->p_log, p_lr, OSM_LOG_DEBUG);

	cl_qlist_init(&lr_list);

	/*
	   Most SA functions (including this one) are read-only on the
	   subnet object, so we grab the lock non-exclusively.
	 */
	cl_plock_acquire(sa->p_lock);

	sa_status = __osm_lr_rcv_get_end_points(sa, p_madw,
						&p_src_port, &p_dest_port);

	if (sa_status == IB_SA_MAD_STATUS_SUCCESS)
		__osm_lr_rcv_get_port_links(sa, p_lr, p_src_port,
					    p_dest_port, p_sa_mad->comp_mask,
					    &lr_list, p_req_physp);

	cl_plock_release(sa->p_lock);

	if (cl_qlist_count(&lr_list) == 0 &&
	    p_sa_mad->method == IB_MAD_METHOD_GET) {
		osm_sa_send_error(sa, p_madw, IB_SA_MAD_STATUS_NO_RECORDS);
		goto Exit;
	}

	__osm_lr_rcv_respond(sa, p_madw, &lr_list);

Exit:

	OSM_LOG_EXIT(sa->p_log);
}

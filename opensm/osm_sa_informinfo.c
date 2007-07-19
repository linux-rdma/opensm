/*
 * Copyright (c) 2004-2006 Voltaire, Inc. All rights reserved.
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
 *    Implementation of osm_infr_rcv_t.
 * This object represents the InformInfo Receiver object.
 * This object is part of the opensm family of objects.
 *
 * Environment:
 *    Linux User Mode
 *
 * $Revision: 1.8 $
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <string.h>
#include <iba/ib_types.h>
#include <complib/cl_qmap.h>
#include <complib/cl_passivelock.h>
#include <complib/cl_debug.h>
#include <complib/cl_qlist.h>
#include <opensm/osm_sa_informinfo.h>
#include <opensm/osm_port.h>
#include <opensm/osm_node.h>
#include <opensm/osm_switch.h>
#include <vendor/osm_vendor.h>
#include <vendor/osm_vendor_api.h>
#include <opensm/osm_helper.h>
#include <opensm/osm_sa.h>
#include <opensm/osm_inform.h>
#include <opensm/osm_pkey.h>

#define OSM_IIR_RCV_POOL_MIN_SIZE      32
#define OSM_IIR_RCV_POOL_GROW_SIZE     32

typedef struct _osm_iir_item
{
  cl_pool_item_t          pool_item;
  ib_inform_info_record_t rec;
} osm_iir_item_t;

typedef struct _osm_iir_search_ctxt
{
  const ib_inform_info_record_t*  p_rcvd_rec;
  ib_net64_t                      comp_mask;
  cl_qlist_t*                     p_list;
  ib_gid_t                        subscriber_gid;
  ib_net16_t                      subscriber_enum;
  osm_infr_rcv_t*                 p_rcv;
  osm_physp_t*                    p_req_physp;
} osm_iir_search_ctxt_t;

/**********************************************************************
 **********************************************************************/
void
osm_infr_rcv_construct(
  IN osm_infr_rcv_t* const p_rcv )
{
  memset( p_rcv, 0, sizeof(*p_rcv) );
  cl_qlock_pool_construct( &p_rcv->pool );
}

/**********************************************************************
 **********************************************************************/
void
osm_infr_rcv_destroy(
  IN osm_infr_rcv_t* const p_rcv )
{
  CL_ASSERT( p_rcv );

  OSM_LOG_ENTER( p_rcv->p_log, osm_infr_rcv_destroy );
  cl_qlock_pool_destroy( &p_rcv->pool );
  OSM_LOG_EXIT( p_rcv->p_log );
}

/**********************************************************************
 **********************************************************************/
ib_api_status_t
osm_infr_rcv_init(
  IN osm_infr_rcv_t*    const p_rcv,
  IN osm_sa_resp_t*     const p_resp,
  IN osm_mad_pool_t*    const p_mad_pool,
  IN osm_subn_t*        const p_subn,
  IN osm_log_t*         const p_log,
  IN cl_plock_t*        const p_lock )
{
  ib_api_status_t status = IB_ERROR;

  OSM_LOG_ENTER( p_log, osm_infr_rcv_init );

  osm_infr_rcv_construct( p_rcv );

  p_rcv->p_log = p_log;
  p_rcv->p_subn = p_subn;
  p_rcv->p_lock = p_lock;
  p_rcv->p_resp = p_resp;
  p_rcv->p_mad_pool = p_mad_pool;

  status = cl_qlock_pool_init( &p_rcv->pool,
                               OSM_IIR_RCV_POOL_MIN_SIZE,
                               0,
                               OSM_IIR_RCV_POOL_GROW_SIZE,
                               sizeof(osm_iir_item_t),
                               NULL, NULL, NULL );

  OSM_LOG_EXIT( p_rcv->p_log );
  return( status );
}

/**********************************************************************
o13-14.1.1: Except for Set(InformInfo) requests with Inform-
Info:LIDRangeBegin=0xFFFF, managers that support event forwarding
shall, upon receiving a Set(InformInfo), verify that the requester
originating the Set(InformInfo) and a Trap() source identified by Inform-
can access each other - can use path record to verify that.
**********************************************************************/
static boolean_t
__validate_ports_access_rights(
  IN osm_infr_rcv_t*   const p_rcv,
  IN osm_infr_t*       p_infr_rec )
{
  boolean_t valid = TRUE;
  osm_physp_t* p_requester_physp;
  osm_port_t*  p_port;
  osm_physp_t* p_physp;
  ib_net64_t portguid;
  ib_net16_t lid_range_begin;
  ib_net16_t lid_range_end;
  ib_net16_t lid;
  const cl_ptr_vector_t* p_tbl;
  ib_gid_t zero_gid;

  OSM_LOG_ENTER( p_rcv->p_log, __validate_ports_access_rights );

  /* get the requester physp from the request address */
  p_requester_physp = osm_get_physp_by_mad_addr( p_rcv->p_log,
                                                 p_rcv->p_subn,
                                                 &p_infr_rec->report_addr );

  memset( &zero_gid, 0, sizeof(zero_gid) );
  if ( memcmp (&(p_infr_rec->inform_record.inform_info.gid),
               &zero_gid, sizeof(ib_gid_t) ) )
  {
    /* a gid is defined */
    portguid = p_infr_rec->inform_record.inform_info.gid.unicast.interface_id;

    p_port = osm_get_port_by_guid( p_rcv->p_subn, portguid );

    if ( p_port == NULL )
    {
      osm_log( p_rcv->p_log, OSM_LOG_ERROR,
               "__validate_ports_access_rights: ERR 4301: "
               "Invalid port guid: 0x%016" PRIx64 "\n",
               cl_ntoh64(portguid) );
      valid = FALSE;
      goto Exit;
    }

    /* get the destination InformInfo physical port */
    p_physp = p_port->p_physp;

    /* make sure that the requester and destination port can access each other
       according to the current partitioning. */
    if (! osm_physp_share_pkey( p_rcv->p_log, p_physp, p_requester_physp))
    {
      osm_log( p_rcv->p_log, OSM_LOG_DEBUG,
               "__validate_ports_access_rights: "
               "port and requester don't share pkey\n" );
      valid = FALSE;
      goto Exit;
    }
  }
  else
  {
    /* gid is zero - check if LID range is defined */
    lid_range_begin = cl_ntoh16(p_infr_rec->inform_record.inform_info.lid_range_begin);
    /* if lid is 0xFFFF - meaning all endports managed by the manager */
    if ( lid_range_begin == 0xFFFF )
      goto Exit;

    lid_range_end = cl_ntoh16(p_infr_rec->inform_record.inform_info.lid_range_end);

    /* lid_range_end is set to zero if no range desired. In this case -
       just make it equal to the lid_range_begin. */
    if (lid_range_end == 0)
      lid_range_end = lid_range_begin;

    /* go over all defined lids within the range and make sure that the
       requester port can access them according to current partitioning. */
    for ( lid = lid_range_begin; lid <= lid_range_end; lid++ )
    {
      p_tbl = &p_rcv->p_subn->port_lid_tbl;
      if ( cl_ptr_vector_get_size( p_tbl ) > lid )
      {
        p_port = cl_ptr_vector_get( p_tbl, lid );
      }
      else
      {
        /* lid requested is out of range */
        osm_log( p_rcv->p_log, OSM_LOG_ERROR,
                 "__validate_ports_access_rights: ERR 4302: "
                 "Given LID (0x%X) is out of range:0x%X\n",
                 lid, cl_ptr_vector_get_size(p_tbl) );
        valid = FALSE;
        goto Exit;
      }
      if ( p_port == NULL )
        continue;

      p_physp = p_port->p_physp;
      /* make sure that the requester and destination port can access
         each other according to the current partitioning. */
      if (! osm_physp_share_pkey( p_rcv->p_log, p_physp, p_requester_physp))
      {
        osm_log( p_rcv->p_log, OSM_LOG_DEBUG,
                 "__validate_ports_access_rights: "
                 "port and requester don't share pkey\n" );
        valid = FALSE;
        goto Exit;
      }
    }
  }

 Exit:
  OSM_LOG_EXIT( p_rcv->p_log );
  return valid;
}

/**********************************************************************
 **********************************************************************/
static boolean_t
__validate_infr(
  IN osm_infr_rcv_t*    const p_rcv,
  IN osm_infr_t*        p_infr_rec )
{
  boolean_t valid = TRUE;

  OSM_LOG_ENTER( p_rcv->p_log, __validate_infr );

  valid = __validate_ports_access_rights( p_rcv, p_infr_rec );
  if (!valid)
  {
    osm_log( p_rcv->p_log, OSM_LOG_DEBUG,
             "__validate_infr: "
             "Invalid Access for InformInfo\n" );
    valid = FALSE;
  }

  OSM_LOG_EXIT( p_rcv->p_log );
  return valid;
}

/**********************************************************************
o13-12.1.1: Confirm a valid request for event subscription by responding
with an InformInfo attribute that is a copy of the data in the
Set(InformInfo) request.
**********************************************************************/
static void
__osm_infr_rcv_respond(
  IN osm_infr_rcv_t*    const p_rcv,
  IN const osm_madw_t*  const p_madw )
{
  osm_madw_t*           p_resp_madw;
  const ib_sa_mad_t*    p_sa_mad;
  ib_sa_mad_t*          p_resp_sa_mad;
  ib_inform_info_t*     p_resp_infr;
  ib_api_status_t       status;

  OSM_LOG_ENTER( p_rcv->p_log, __osm_infr_rcv_respond );

  if( osm_log_is_active( p_rcv->p_log, OSM_LOG_DEBUG ) )
  {
    osm_log( p_rcv->p_log, OSM_LOG_DEBUG,
             "__osm_infr_rcv_respond: "
             "Generating successful InformInfo response\n");
  }

  /*
    Get a MAD to reply. Address of Mad is in the received mad_wrapper
  */
  p_resp_madw = osm_mad_pool_get( p_rcv->p_mad_pool,
                                  p_madw->h_bind,
                                  MAD_BLOCK_SIZE,
                                  &p_madw->mad_addr );
  if ( !p_resp_madw )
  {
    osm_log( p_rcv->p_log, OSM_LOG_ERROR,
             "__osm_infr_rcv_respond: ERR 4303: "
             "Unable to allocate MAD\n" );
    goto Exit;
  }

  p_sa_mad = osm_madw_get_sa_mad_ptr( p_madw );
  p_resp_sa_mad = osm_madw_get_sa_mad_ptr( p_resp_madw );

  /* copy the request InformInfo */
  memcpy( p_resp_sa_mad, p_sa_mad, MAD_BLOCK_SIZE );
  p_resp_sa_mad->method = IB_MAD_METHOD_GET_RESP;
  /* C15-0.1.5 - always return SM_Key = 0 (table 185 p 884) */
  p_resp_sa_mad->sm_key = 0;

  p_resp_infr = (ib_inform_info_t*)ib_sa_mad_get_payload_ptr( p_resp_sa_mad );

  status = osm_vendor_send( p_resp_madw->h_bind, p_resp_madw,  FALSE );

  if ( status != IB_SUCCESS )
  {
    osm_log( p_rcv->p_log, OSM_LOG_ERROR,
             "__osm_infr_rcv_respond: ERR 4304: "
             "Unable to send MAD (%s)\n", ib_get_err_str( status ) );
    /* osm_mad_pool_put( p_rcv->p_mad_pool, p_resp_madw ); */
    goto Exit;
  }

 Exit:
  OSM_LOG_EXIT( p_rcv->p_log );
}

/**********************************************************************
 **********************************************************************/
static void
__osm_sa_inform_info_rec_by_comp_mask(
  IN osm_infr_rcv_t*       const p_rcv,
  IN const osm_infr_t*     const p_infr,
  osm_iir_search_ctxt_t*   const p_ctxt )
{
  const ib_inform_info_record_t* p_rcvd_rec = NULL;
  ib_net64_t               comp_mask;
  ib_net64_t               portguid;
  osm_port_t *             p_subscriber_port;
  osm_physp_t *            p_subscriber_physp;
  const osm_physp_t*       p_req_physp;
  osm_iir_item_t*          p_rec_item;

  OSM_LOG_ENTER( p_rcv->p_log, __osm_sa_inform_info_rec_by_comp_mask );

  p_rcvd_rec = p_ctxt->p_rcvd_rec;
  comp_mask = p_ctxt->comp_mask;
  p_req_physp = p_ctxt->p_req_physp;

  if (comp_mask & IB_IIR_COMPMASK_SUBSCRIBERGID)
  {
    if (memcmp(&p_infr->inform_record.subscriber_gid,
	       &p_ctxt->subscriber_gid,
	       sizeof(p_infr->inform_record.subscriber_gid)))
      goto Exit;
  }

  if (comp_mask & IB_IIR_COMPMASK_ENUM)
  {
    if (p_infr->inform_record.subscriber_enum != p_ctxt->subscriber_enum)
      goto Exit;
  }

  /* Implement any other needed search cases */

  /* Ensure pkey is shared before returning any records */
  portguid = p_infr->inform_record.subscriber_gid.unicast.interface_id;
  p_subscriber_port = osm_get_port_by_guid( p_rcv->p_subn, portguid );
  if ( p_subscriber_port == NULL )
  {
    osm_log( p_rcv->p_log, OSM_LOG_ERROR,
             "__osm_sa_inform_info_rec_by_comp_mask: ERR 430D: "
             "Invalid subscriber port guid: 0x%016" PRIx64 "\n",
             cl_ntoh64(portguid) );
    goto Exit;
  }

  /* get the subscriber InformInfo physical port */
  p_subscriber_physp = p_subscriber_port->p_physp;
  /* make sure that the requester and subscriber port can access each other
     according to the current partitioning. */
  if (! osm_physp_share_pkey( p_rcv->p_log, p_req_physp, p_subscriber_physp ))
  {
    osm_log( p_rcv->p_log, OSM_LOG_DEBUG,
             "__osm_sa_inform_info_rec_by_comp_mask: "
             "requester and subscriber ports don't share pkey\n" );
    goto Exit;
  }

  p_rec_item = (osm_iir_item_t*)cl_qlock_pool_get( &p_rcv->pool );
  if( p_rec_item == NULL )
  {
    osm_log( p_rcv->p_log, OSM_LOG_ERROR,
             "__osm_sa_inform_info_rec_by_comp_mask: ERR 430E: "
             "cl_qlock_pool_get failed\n" );
    goto Exit;
  }

  memcpy((void *)&p_rec_item->rec, (void *)&p_infr->inform_record, sizeof(ib_inform_info_record_t));
  cl_qlist_insert_tail( p_ctxt->p_list, (cl_list_item_t*)&p_rec_item->pool_item );

Exit:
  OSM_LOG_EXIT( p_rcv->p_log );
}

/**********************************************************************
 **********************************************************************/
static void
__osm_sa_inform_info_rec_by_comp_mask_cb(
  IN cl_list_item_t*       const p_list_item,
  IN void*                 context )
{
  const osm_infr_t* const p_infr = (osm_infr_t *)p_list_item;
  osm_iir_search_ctxt_t*   const p_ctxt = (osm_iir_search_ctxt_t *)context;

  __osm_sa_inform_info_rec_by_comp_mask( p_ctxt->p_rcv, p_infr, p_ctxt );
}

/**********************************************************************
Received a Get(InformInfoRecord) or GetTable(InformInfoRecord) MAD
**********************************************************************/
static void
osm_infr_rcv_process_get_method(
  IN osm_infr_rcv_t*      const p_rcv,
  IN const osm_madw_t*    const p_madw )
{
  ib_sa_mad_t*            p_rcvd_mad;
  const ib_inform_info_record_t* p_rcvd_rec;
  ib_inform_info_record_t* p_resp_rec;
  cl_qlist_t              rec_list;
  osm_madw_t*             p_resp_madw;
  ib_sa_mad_t*            p_resp_sa_mad;
  uint32_t                num_rec, pre_trim_num_rec;
#ifndef VENDOR_RMPP_SUPPORT
  uint32_t                trim_num_rec;
#endif
  uint32_t                i, j;
  osm_iir_search_ctxt_t   context;
  osm_iir_item_t*         p_rec_item;
  ib_api_status_t         status = IB_SUCCESS;
  osm_physp_t*            p_req_physp;

  OSM_LOG_ENTER( p_rcv->p_log, osm_infr_rcv_process_get_method );

  CL_ASSERT( p_madw );
  p_rcvd_mad = osm_madw_get_sa_mad_ptr( p_madw );
  p_rcvd_rec =
    (ib_inform_info_record_t*)ib_sa_mad_get_payload_ptr( p_rcvd_mad );

  /* update the requester physical port. */
  p_req_physp = osm_get_physp_by_mad_addr(p_rcv->p_log,
                                          p_rcv->p_subn,
                                          osm_madw_get_mad_addr_ptr(p_madw) );
  if (p_req_physp == NULL)
  {
    osm_log( p_rcv->p_log, OSM_LOG_ERROR,
             "osm_infr_rcv_process_get_method: ERR 4309: "
             "Cannot find requester physical port\n" );
    goto Exit;
  }

  if( osm_log_is_active( p_rcv->p_log, OSM_LOG_DEBUG ) )
    osm_dump_inform_info_record( p_rcv->p_log, p_rcvd_rec, OSM_LOG_DEBUG );

  cl_qlist_init( &rec_list );

  context.p_rcvd_rec = p_rcvd_rec;
  context.p_list = &rec_list;
  context.comp_mask = p_rcvd_mad->comp_mask;
  context.subscriber_gid = p_rcvd_rec->subscriber_gid;
  context.subscriber_enum = p_rcvd_rec->subscriber_enum;
  context.p_rcv = p_rcv;
  context.p_req_physp = p_req_physp;

  osm_log( p_rcv->p_log, OSM_LOG_DEBUG,
           "osm_infr_rcv_process_get_method: "
           "Query Subscriber GID:0x%016" PRIx64 " : 0x%016" PRIx64 "(%02X) Enum:0x%X(%02X)\n",
           cl_ntoh64(p_rcvd_rec->subscriber_gid.unicast.prefix),
           cl_ntoh64(p_rcvd_rec->subscriber_gid.unicast.interface_id),
           (p_rcvd_mad->comp_mask & IB_IIR_COMPMASK_SUBSCRIBERGID) != 0,
           cl_ntoh16(p_rcvd_rec->subscriber_enum),
           (p_rcvd_mad->comp_mask & IB_IIR_COMPMASK_ENUM) != 0 );

  cl_plock_acquire( p_rcv->p_lock );

  cl_qlist_apply_func( &p_rcv->p_subn->sa_infr_list,
                       __osm_sa_inform_info_rec_by_comp_mask_cb,
                       &context );

  cl_plock_release( p_rcv->p_lock );

  num_rec = cl_qlist_count( &rec_list );

  /*
   * C15-0.1.30:
   * If we do a SubnAdmGet and got more than one record it is an error !
   */
  if (p_rcvd_mad->method == IB_MAD_METHOD_GET)
  {
    if (num_rec == 0)
    {
      osm_sa_send_error( p_rcv->p_resp, p_madw, IB_SA_MAD_STATUS_NO_RECORDS );
      goto Exit;
    }
    if (num_rec > 1)
    {
      osm_log( p_rcv->p_log, OSM_LOG_ERROR,
               "osm_infr_rcv_process_get_method: ERR 430A: "
               "More than one record for SubnAdmGet (%u)\n",
               num_rec );
      osm_sa_send_error( p_rcv->p_resp, p_madw,
                         IB_SA_MAD_STATUS_TOO_MANY_RECORDS);

      /* need to set the mem free ... */
      p_rec_item = (osm_iir_item_t*)cl_qlist_remove_head( &rec_list );
      while( p_rec_item != (osm_iir_item_t*)cl_qlist_end( &rec_list ) )
      {
        cl_qlock_pool_put( &p_rcv->pool, &p_rec_item->pool_item );
        p_rec_item = (osm_iir_item_t*)cl_qlist_remove_head( &rec_list );
      }

      goto Exit;
    }
  }

  pre_trim_num_rec = num_rec;
#ifndef VENDOR_RMPP_SUPPORT
  /* we limit the number of records to a single packet */
  trim_num_rec = (MAD_BLOCK_SIZE - IB_SA_MAD_HDR_SIZE) / sizeof(ib_inform_info_record_t);
  if (trim_num_rec < num_rec)
  {
    osm_log( p_rcv->p_log, OSM_LOG_VERBOSE,
             "osm_infr_rcv_process_get_method: "
             "Number of records:%u trimmed to:%u to fit in one MAD\n",
             num_rec, trim_num_rec );
    num_rec = trim_num_rec;
  }
#endif

  osm_log( p_rcv->p_log, OSM_LOG_DEBUG,
           "osm_infr_rcv_process_get_method: "
           "Returning %u records\n", num_rec );

  /*
   * Get a MAD to reply. Address of Mad is in the received mad_wrapper
   */
  p_resp_madw = osm_mad_pool_get( p_rcv->p_mad_pool,
                                  p_madw->h_bind,
                                  num_rec * sizeof(ib_inform_info_record_t) + IB_SA_MAD_HDR_SIZE,
                                  &p_madw->mad_addr );

  if( !p_resp_madw )
  {
    osm_log(p_rcv->p_log, OSM_LOG_ERROR,
            "osm_infr_rcv_process_get_method: ERR 430B: "
            "osm_mad_pool_get failed\n" );

    for( i = 0; i < num_rec; i++ )
    {
      p_rec_item = (osm_iir_item_t*)cl_qlist_remove_head( &rec_list );
      cl_qlock_pool_put( &p_rcv->pool, &p_rec_item->pool_item );
    }

    osm_sa_send_error( p_rcv->p_resp, p_madw,
                       IB_SA_MAD_STATUS_NO_RESOURCES );

    goto Exit;
  }

  p_resp_sa_mad = osm_madw_get_sa_mad_ptr( p_resp_madw );

  /*
    Copy the MAD header back into the response mad.
    Set the 'R' bit and the payload length,
    Then copy all records from the list into the response payload.
  */

  memcpy( p_resp_sa_mad, p_rcvd_mad, IB_SA_MAD_HDR_SIZE );
  p_resp_sa_mad->method |= IB_MAD_METHOD_RESP_MASK;
  /* C15-0.1.5 - always return SM_Key = 0 (table 185 p 884) */
  p_resp_sa_mad->sm_key = 0;
  /* Fill in the offset (paylen will be done by the rmpp SAR) */
  p_resp_sa_mad->attr_offset =
    ib_get_attr_offset( sizeof(ib_inform_info_record_t) );

  p_resp_rec = (ib_inform_info_record_t*)ib_sa_mad_get_payload_ptr( p_resp_sa_mad );

#ifndef VENDOR_RMPP_SUPPORT
  /* we support only one packet RMPP - so we will set the first and
     last flags for gettable */
  if (p_resp_sa_mad->method == IB_MAD_METHOD_GETTABLE_RESP)
  {
    p_resp_sa_mad->rmpp_type = IB_RMPP_TYPE_DATA;
    p_resp_sa_mad->rmpp_flags = IB_RMPP_FLAG_FIRST | IB_RMPP_FLAG_LAST | IB_RMPP_FLAG_ACTIVE;
  }
#else
  /* forcefully define the packet as RMPP one */
  if (p_resp_sa_mad->method == IB_MAD_METHOD_GETTABLE_RESP)
    p_resp_sa_mad->rmpp_flags = IB_RMPP_FLAG_ACTIVE;
#endif

  for( i = 0; i < pre_trim_num_rec; i++ )
  {
    p_rec_item = (osm_iir_item_t*)cl_qlist_remove_head( &rec_list );
    /* copy only if not trimmed */
    if (i < num_rec)
    {
      *p_resp_rec = p_rec_item->rec;
      /* clear reserved and pad fields in InformInfoRecord */
      for (j = 0; j < 6; j++)
        p_resp_rec->reserved[j] = 0;
      for (j = 0; j < 4; j++)
        p_resp_rec->pad[j] = 0;
    }
    cl_qlock_pool_put( &p_rcv->pool, &p_rec_item->pool_item );
    p_resp_rec++;
  }

  CL_ASSERT( cl_is_qlist_empty( &rec_list ) );

  status = osm_vendor_send( p_resp_madw->h_bind, p_resp_madw, FALSE );
  if (status != IB_SUCCESS)
  {
    osm_log(p_rcv->p_log, OSM_LOG_ERROR,
            "osm_infr_rcv_process_get_method: ERR 430C: "
            "osm_vendor_send status = %s\n",
            ib_get_err_str(status));
    goto Exit;
  }

 Exit:
  OSM_LOG_EXIT( p_rcv->p_log );
}

/*********************************************************************
Received a Set(InformInfo) MAD
**********************************************************************/
static void
osm_infr_rcv_process_set_method(
  IN osm_infr_rcv_t*      const p_rcv,
  IN const osm_madw_t*    const p_madw )
{
  ib_sa_mad_t *p_sa_mad;
  ib_inform_info_t *p_recvd_inform_info;
  osm_infr_t inform_info_rec; /* actual inform record to be stored for reports */
  osm_infr_t *p_infr;
  ib_net32_t qpn;
  uint8_t resp_time_val;
  ib_api_status_t res;

  OSM_LOG_ENTER( p_rcv->p_log, osm_infr_rcv_process_set_method );

  CL_ASSERT( p_madw );

  p_sa_mad = osm_madw_get_sa_mad_ptr( p_madw );
  p_recvd_inform_info =
    (ib_inform_info_t*)ib_sa_mad_get_payload_ptr( p_sa_mad );

#if 0
  if( osm_log_is_active( p_rcv->p_log, OSM_LOG_DEBUG ) )
    osm_dump_inform_info( p_rcv->p_log, p_recvd_inform_info, OSM_LOG_DEBUG );
#endif

  /* Grab the lock */
  cl_plock_excl_acquire( p_rcv->p_lock );

  /* define the inform record */
  inform_info_rec.inform_record.inform_info = *p_recvd_inform_info;

  /* following C13-32.1.2 Tbl 120: we only copy the source address vector */
  inform_info_rec.report_addr = p_madw->mad_addr;

  /* we will need to know the mad srvc to send back through */
  inform_info_rec.h_bind = p_madw->h_bind;
  inform_info_rec.p_infr_rcv = p_rcv;

  /* update the subscriber GID according to mad address */
  res = osm_get_gid_by_mad_addr(
    p_rcv->p_log,
    p_rcv->p_subn,
    &p_madw->mad_addr,
    &inform_info_rec.inform_record.subscriber_gid );
  if ( res != IB_SUCCESS )
  {
    cl_plock_release( p_rcv->p_lock );

    osm_log( p_rcv->p_log, OSM_LOG_ERROR,
             "osm_infr_rcv_process_set_method: ERR 4308 "
             "Subscribe Request from unknown LID: 0x%04X\n",
             cl_ntoh16(p_madw->mad_addr.dest_lid)
             );
    osm_sa_send_error( p_rcv->p_resp, p_madw, IB_SA_MAD_STATUS_REQ_INVALID );
    goto Exit;
  }

  /* HACK: enum is always 0 (currently) */
  inform_info_rec.inform_record.subscriber_enum = 0;

  /* Subscribe values above 1 are undefined */
  if ( p_recvd_inform_info->subscribe > 1 )
  {
    cl_plock_release( p_rcv->p_lock );

    osm_log( p_rcv->p_log, OSM_LOG_ERROR,
             "osm_infr_rcv_process_set_method: ERR 4308 "
             "Invalid subscribe: %d\n",
             p_recvd_inform_info->subscribe
             );
    osm_sa_send_error( p_rcv->p_resp, p_madw, IB_SA_MAD_STATUS_REQ_INVALID );
    goto Exit;
  }

  /*
   * MODIFICATIONS DONE ON INCOMING REQUEST:
   *
   * QPN:
   * Internally we keep the QPN field of the InformInfo updated
   * so we can simply compare it in the record - when finding such.
   */
  if ( p_recvd_inform_info->subscribe )
  {
    ib_inform_info_set_qpn(
      &inform_info_rec.inform_record.inform_info,
      inform_info_rec.report_addr.addr_type.gsi.remote_qp );

    osm_log( p_rcv->p_log, OSM_LOG_DEBUG,
             "osm_infr_rcv_process_set_method: "
             "Subscribe Request with QPN: 0x%06X\n",
             cl_ntoh32(inform_info_rec.report_addr.addr_type.gsi.remote_qp)
             );
  }
  else
  {
    ib_inform_info_get_qpn_resp_time(
      p_recvd_inform_info->g_or_v.generic.qpn_resp_time_val,
      &qpn, &resp_time_val );

    osm_log( p_rcv->p_log, OSM_LOG_DEBUG,
             "osm_infr_rcv_process_set_method: "
             "UnSubscribe Request with QPN: 0x%06X\n",
             cl_ntoh32(qpn)
             );
  }

  /* If record exists with matching InformInfo */
  p_infr = osm_infr_get_by_rec( p_rcv->p_subn, p_rcv->p_log, &inform_info_rec );

  /* check to see if the request was for subscribe */
  if ( p_recvd_inform_info->subscribe )
  {
    /* validate the request for a new or update InformInfo */
    if ( __validate_infr( p_rcv, &inform_info_rec ) != TRUE )
    {
      cl_plock_release( p_rcv->p_lock );

      osm_log( p_rcv->p_log, OSM_LOG_ERROR,
               "osm_infr_rcv_process_set_method: ERR 4305: "
               "Failed to validate a new inform object\n");

      /* o13-13.1.1: we need to set the subscribe bit to 0 */
      p_recvd_inform_info->subscribe = 0;
      osm_sa_send_error( p_rcv->p_resp, p_madw, IB_SA_MAD_STATUS_REQ_INVALID );
      goto Exit;
    }

    /* ok - we can try and create a new entry */
    if (p_infr == NULL)
    {
      /* Create the instance of the osm_infr_t object */
      p_infr = osm_infr_new( &inform_info_rec );
      if (p_infr == NULL)
      {
        cl_plock_release( p_rcv->p_lock );

        osm_log( p_rcv->p_log, OSM_LOG_ERROR,
                 "osm_infr_rcv_process_set_method: ERR 4306: "
                 "Failed to create a new inform object\n");

        /* o13-13.1.1: we need to set the subscribe bit to 0 */
        p_recvd_inform_info->subscribe = 0;
        osm_sa_send_error( p_rcv->p_resp, p_madw,
                           IB_SA_MAD_STATUS_NO_RESOURCES );
        goto Exit;
      }

      /* Add this new osm_infr_t object to subnet object */
      osm_infr_insert_to_db( p_rcv->p_subn, p_rcv->p_log, p_infr );
    }
    else
    {
      /* Update the old instance of the osm_infr_t object */
      p_infr->inform_record = inform_info_rec.inform_record;
    }
  }
  else
  {
    /* We got an UnSubscribe request */
    if (p_infr == NULL)
    {
      cl_plock_release( p_rcv->p_lock );

      /* No Such Item - So Error */
      osm_log( p_rcv->p_log, OSM_LOG_ERROR,
               "osm_infr_rcv_process_set_method: ERR 4307: "
               "Failed to UnSubscribe to non existing inform object\n");

      /* o13-13.1.1: we need to set the subscribe bit to 0 */
      p_recvd_inform_info->subscribe = 0;
      osm_sa_send_error( p_rcv->p_resp, p_madw, IB_SA_MAD_STATUS_REQ_INVALID );
      goto Exit;
    }
    else
    {
      /* Delete this object from the subnet list of informs */
      osm_infr_remove_from_db( p_rcv->p_subn, p_rcv->p_log, p_infr );
    }
  }

  cl_plock_release( p_rcv->p_lock );

  /* send the success response */
  __osm_infr_rcv_respond( p_rcv, p_madw );

 Exit:
  OSM_LOG_EXIT( p_rcv->p_log );
}

/*********************************************************************
**********************************************************************/
void
osm_infr_rcv_process(
  IN void *context,
  IN void *data )
{
  osm_infr_rcv_t *p_rcv = context;
  osm_madw_t *p_madw = data;
  ib_sa_mad_t *p_sa_mad;

  OSM_LOG_ENTER( p_rcv->p_log, osm_infr_rcv_process );

  CL_ASSERT( p_madw );

  p_sa_mad = osm_madw_get_sa_mad_ptr( p_madw );

  CL_ASSERT( p_sa_mad->attr_id == IB_MAD_ATTR_INFORM_INFO );

  if (p_sa_mad->method != IB_MAD_METHOD_SET)
  {
    osm_log( p_rcv->p_log, OSM_LOG_DEBUG,
             "osm_infr_rcv_process: "
             "Unsupported Method (%s)\n",
             ib_get_sa_method_str( p_sa_mad->method ) );
    osm_sa_send_error( p_rcv->p_resp, p_madw, IB_MAD_STATUS_UNSUP_METHOD_ATTR );
    goto Exit;
  }

  osm_infr_rcv_process_set_method( p_rcv, p_madw );

 Exit:
  OSM_LOG_EXIT( p_rcv->p_log );
}

/*********************************************************************
**********************************************************************/
void
osm_infir_rcv_process(
  IN void *context,
  IN void *data )
{
  osm_infr_rcv_t *p_rcv = context;
  osm_madw_t *p_madw = data;
  ib_sa_mad_t *p_sa_mad;

  OSM_LOG_ENTER( p_rcv->p_log, osm_infr_rcv_process );

  CL_ASSERT( p_madw );

  p_sa_mad = osm_madw_get_sa_mad_ptr( p_madw );

  CL_ASSERT( p_sa_mad->attr_id == IB_MAD_ATTR_INFORM_INFO_RECORD );

  if ( (p_sa_mad->method != IB_MAD_METHOD_GET) &&
       (p_sa_mad->method != IB_MAD_METHOD_GETTABLE) )
  {
    osm_log( p_rcv->p_log, OSM_LOG_DEBUG,
             "osm_infir_rcv_process: "
             "Unsupported Method (%s)\n",
             ib_get_sa_method_str( p_sa_mad->method ) );
    osm_sa_send_error( p_rcv->p_resp, p_madw, IB_MAD_STATUS_UNSUP_METHOD_ATTR );
    goto Exit;
  }

  osm_infr_rcv_process_get_method( p_rcv, p_madw );

 Exit:
  OSM_LOG_EXIT( p_rcv->p_log );
}

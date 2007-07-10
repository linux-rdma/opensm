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
 *    Implementation of osm_pir_rcv_t.
 * This object represents the PortInfoRecord Receiver object.
 * This object is part of the opensm family of objects.
 *
 * Environment:
 *    Linux User Mode
 *
 * $Revision: 1.10 $
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
#include <opensm/osm_sa_portinfo_record.h>
#include <opensm/osm_port.h>
#include <opensm/osm_node.h>
#include <opensm/osm_switch.h>
#include <vendor/osm_vendor_api.h>
#include <opensm/osm_helper.h>
#include <opensm/osm_pkey.h>

#define OSM_PIR_RCV_POOL_MIN_SIZE      32
#define OSM_PIR_RCV_POOL_GROW_SIZE     32

typedef  struct _osm_pir_item
{
  cl_pool_item_t           pool_item;
  ib_portinfo_record_t     rec;
} osm_pir_item_t;

typedef  struct _osm_pir_search_ctxt
{
  const ib_portinfo_record_t* p_rcvd_rec;
  ib_net64_t               comp_mask;
  cl_qlist_t*              p_list;
  osm_pir_rcv_t*           p_rcv;
  const osm_physp_t*       p_req_physp;
  boolean_t                is_enhanced_comp_mask;
} osm_pir_search_ctxt_t;

/**********************************************************************
 **********************************************************************/
void
osm_pir_rcv_construct(
  IN osm_pir_rcv_t* const p_rcv )
{
  memset( p_rcv, 0, sizeof(*p_rcv) );
  cl_qlock_pool_construct( &p_rcv->pool );
}

/**********************************************************************
 **********************************************************************/
void
osm_pir_rcv_destroy(
  IN osm_pir_rcv_t* const p_rcv )
{
  OSM_LOG_ENTER( p_rcv->p_log, osm_pir_rcv_destroy );
  cl_qlock_pool_destroy( &p_rcv->pool );
  OSM_LOG_EXIT( p_rcv->p_log );
}

/**********************************************************************
 **********************************************************************/
ib_api_status_t
osm_pir_rcv_init(
  IN osm_pir_rcv_t*  const p_rcv,
  IN osm_sa_resp_t*  const p_resp,
  IN osm_mad_pool_t* const p_mad_pool,
  IN osm_subn_t*     const p_subn,
  IN osm_log_t*      const p_log,
  IN cl_plock_t*     const p_lock )
{
  ib_api_status_t          status;

  OSM_LOG_ENTER( p_log, osm_pir_rcv_init );

  osm_pir_rcv_construct( p_rcv );

  p_rcv->p_log = p_log;
  p_rcv->p_subn = p_subn;
  p_rcv->p_lock = p_lock;
  p_rcv->p_resp = p_resp;
  p_rcv->p_mad_pool = p_mad_pool;

  status = cl_qlock_pool_init( &p_rcv->pool,
                               OSM_PIR_RCV_POOL_MIN_SIZE,
                               0,
                               OSM_PIR_RCV_POOL_GROW_SIZE,
                               sizeof(osm_pir_item_t),
                               NULL, NULL, NULL );

  OSM_LOG_EXIT( p_log );
  return( status );
}

/**********************************************************************
 **********************************************************************/
static ib_api_status_t
__osm_pir_rcv_new_pir(
  IN osm_pir_rcv_t*        const p_rcv,
  IN const osm_physp_t*    const p_physp,
  IN cl_qlist_t*           const p_list,
  IN ib_net16_t            const lid )
{
  osm_pir_item_t*          p_rec_item;
  ib_api_status_t          status = IB_SUCCESS;

  OSM_LOG_ENTER( p_rcv->p_log, __osm_pir_rcv_new_pir );

  p_rec_item = (osm_pir_item_t*)cl_qlock_pool_get( &p_rcv->pool );
  if( p_rec_item == NULL )
  {
    osm_log( p_rcv->p_log, OSM_LOG_ERROR,
             "__osm_pir_rcv_new_pir: ERR 2102: "
             "cl_qlock_pool_get failed\n" );
    status = IB_INSUFFICIENT_RESOURCES;
    goto Exit;
  }

  if( osm_log_is_active( p_rcv->p_log, OSM_LOG_DEBUG ) )
  {
    osm_log( p_rcv->p_log, OSM_LOG_DEBUG,
             "__osm_pir_rcv_new_pir: "
             "New PortInfoRecord: port 0x%016" PRIx64
             ", lid 0x%X, port 0x%X\n",
             cl_ntoh64( osm_physp_get_port_guid( p_physp ) ),
             cl_ntoh16( lid ), osm_physp_get_port_num( p_physp ) );
  }

  memset( &p_rec_item->rec, 0, sizeof( p_rec_item->rec ) );

  p_rec_item->rec.lid = lid;
  p_rec_item->rec.port_info = p_physp->port_info;
  p_rec_item->rec.port_num = osm_physp_get_port_num( p_physp );

  cl_qlist_insert_tail( p_list, (cl_list_item_t*)&p_rec_item->pool_item );

 Exit:
  OSM_LOG_EXIT( p_rcv->p_log );
  return( status );
}

/**********************************************************************
 **********************************************************************/
void
__osm_sa_pir_create(
  IN osm_pir_rcv_t*           const p_rcv,
  IN const osm_physp_t*       const p_physp,
  IN osm_pir_search_ctxt_t*   const p_ctxt )
{
  uint8_t                     lmc;
  uint16_t                    max_lid_ho;
  uint16_t                    base_lid_ho;
  uint16_t                    match_lid_ho;
  osm_physp_t                *p_node_physp;

  OSM_LOG_ENTER( p_rcv->p_log, __osm_sa_pir_create );

  if (p_physp->p_node->sw)
  {
    p_node_physp = osm_node_get_physp_ptr( p_physp->p_node, 0 );
    base_lid_ho = cl_ntoh16( osm_physp_get_base_lid( p_node_physp ) );
    lmc = osm_switch_sp0_is_lmc_capable(p_physp->p_node->sw, p_rcv->p_subn) ?
      osm_physp_get_lmc( p_node_physp ) : 0;
  }
  else
  {
    lmc = osm_physp_get_lmc( p_physp );
    base_lid_ho = cl_ntoh16( osm_physp_get_base_lid( p_physp ) );
  }
  max_lid_ho = (uint16_t)( base_lid_ho + (1 << lmc) - 1 );

  if( p_ctxt->comp_mask & IB_PIR_COMPMASK_LID )
  {
    match_lid_ho = cl_ntoh16( p_ctxt->p_rcvd_rec->lid );

    /*
      We validate that the lid belongs to this node.
    */
    if( osm_log_is_active( p_rcv->p_log, OSM_LOG_DEBUG ) )
    {
      osm_log( p_rcv->p_log, OSM_LOG_DEBUG,
               "__osm_sa_pir_create: "
               "Comparing LID: 0x%X <= 0x%X <= 0x%X\n",
               base_lid_ho, match_lid_ho, max_lid_ho
               );
    }

    if ( match_lid_ho < base_lid_ho || match_lid_ho > max_lid_ho )
      goto Exit;
  }

  __osm_pir_rcv_new_pir( p_rcv, p_physp, p_ctxt->p_list,
                         cl_hton16( base_lid_ho ) );

 Exit:
  OSM_LOG_EXIT( p_rcv->p_log );
}

/**********************************************************************
 **********************************************************************/
void
__osm_sa_pir_check_physp(
  IN osm_pir_rcv_t*        const p_rcv,
  IN const osm_physp_t*    const p_physp,
  osm_pir_search_ctxt_t*   const p_ctxt )
{
  const ib_portinfo_record_t* p_rcvd_rec;
  ib_net64_t               comp_mask;
  const ib_port_info_t*    p_comp_pi;
  const ib_port_info_t*    p_pi;

  OSM_LOG_ENTER( p_rcv->p_log, __osm_sa_pir_check_physp );

  p_rcvd_rec = p_ctxt->p_rcvd_rec;
  comp_mask = p_ctxt->comp_mask;
  p_comp_pi = &p_rcvd_rec->port_info;
  p_pi = &p_physp->port_info;

  osm_dump_port_info(
    p_rcv->p_log,
    osm_node_get_node_guid( p_physp->p_node ),
    p_physp->port_guid,
    p_physp->port_num,
    &p_physp->port_info,
    OSM_LOG_DEBUG );
    
  /* We have to re-check the base_lid, since if the given
     base_lid in p_pi is zero - we are comparing on all ports. */     
  if( comp_mask & IB_PIR_COMPMASK_BASELID )
  {
    if( p_comp_pi->base_lid != p_pi->base_lid )
      goto Exit;
  }
  if( comp_mask & IB_PIR_COMPMASK_MKEY )
  {
    if( p_comp_pi->m_key != p_pi->m_key )
      goto Exit;
  }
  if( comp_mask & IB_PIR_COMPMASK_GIDPRE )
  {
    if( p_comp_pi->subnet_prefix != p_pi->subnet_prefix )
      goto Exit;
  }
  if( comp_mask & IB_PIR_COMPMASK_SMLID )
  {
    if( p_comp_pi->master_sm_base_lid != p_pi->master_sm_base_lid )
      goto Exit;
  }
  
  /* IBTA 1.2 errata provides support for bitwise compare if the bit 31 
     of the attribute modifier of the Get/GetTable is set */
  if( comp_mask & IB_PIR_COMPMASK_CAPMASK )
  {
    if (p_ctxt->is_enhanced_comp_mask)
    {
      if ( ( ( p_comp_pi->capability_mask & p_pi->capability_mask ) != p_comp_pi->capability_mask) )
        goto Exit; 
    }
    else
    {
      if( p_comp_pi->capability_mask != p_pi->capability_mask )
        goto Exit;
    }
  }

  if( comp_mask & IB_PIR_COMPMASK_DIAGCODE )
  {
    if( p_comp_pi->diag_code != p_pi->diag_code )
      goto Exit;
  }
  if( comp_mask & IB_PIR_COMPMASK_MKEYLEASEPRD )
  {
    if( p_comp_pi->m_key_lease_period != p_pi->m_key_lease_period )
      goto Exit;
  }
  if( comp_mask & IB_PIR_COMPMASK_LOCALPORTNUM )
  {
    if( p_comp_pi->local_port_num != p_pi->local_port_num )
      goto Exit;
  }
  if( comp_mask & IB_PIR_COMPMASK_LNKWIDTHSUPPORT )
  {
    if( p_comp_pi->link_width_supported != p_pi->link_width_supported )
      goto Exit;
  }
  if( comp_mask & IB_PIR_COMPMASK_LNKWIDTHACTIVE )
  {
    if( p_comp_pi->link_width_active != p_pi->link_width_active )
      goto Exit;
  }
  if( comp_mask & IB_PIR_COMPMASK_LINKWIDTHENABLED )
  {
    if( p_comp_pi->link_width_enabled != p_pi->link_width_enabled )
      goto Exit;
  }
  if( comp_mask & IB_PIR_COMPMASK_LNKSPEEDSUPPORT )
  {
    if( ib_port_info_get_link_speed_sup( p_comp_pi )!=
        ib_port_info_get_link_speed_sup( p_pi) )
      goto Exit;
  }
  if( comp_mask & IB_PIR_COMPMASK_PORTSTATE )
  {
    if( ib_port_info_get_port_state( p_comp_pi ) !=
        ib_port_info_get_port_state( p_pi ) )
      goto Exit;
  }
  if ( comp_mask & IB_PIR_COMPMASK_PORTPHYSTATE )
  {
    if ( ib_port_info_get_port_phys_state( p_comp_pi ) !=
         ib_port_info_get_port_phys_state( p_pi ) )
      goto Exit;
  }
  if ( comp_mask & IB_PIR_COMPMASK_LINKDWNDFLTSTATE )
  {
    if ( ib_port_info_get_link_down_def_state( p_comp_pi ) !=
         ib_port_info_get_link_down_def_state( p_pi ) )
      goto Exit;
  }
  if ( comp_mask & IB_PIR_COMPMASK_MKEYPROTBITS )
  {
    if( ib_port_info_get_mpb( p_comp_pi ) !=
        ib_port_info_get_mpb( p_pi ) )
      goto Exit;
  }
  if( comp_mask & IB_PIR_COMPMASK_LMC )
  {
    if( ib_port_info_get_lmc( p_comp_pi ) !=
        ib_port_info_get_lmc( p_pi ) )
      goto Exit;
  }
  if ( comp_mask & IB_PIR_COMPMASK_LINKSPEEDACTIVE )
  {
    if ( ib_port_info_get_link_speed_active( p_comp_pi ) !=
         ib_port_info_get_link_speed_active( p_pi ) )
      goto Exit;
  }
  if ( comp_mask & IB_PIR_COMPMASK_LINKSPEEDENABLE )
  {
    if ( ib_port_info_get_link_speed_enabled( p_comp_pi ) !=
         ib_port_info_get_link_speed_enabled( p_pi ) )
      goto Exit;
  }
  if( comp_mask & IB_PIR_COMPMASK_NEIGHBORMTU )
  {
    if( ib_port_info_get_neighbor_mtu( p_comp_pi ) !=
        ib_port_info_get_neighbor_mtu( p_pi ) )
      goto Exit;
  }
  if( comp_mask & IB_PIR_COMPMASK_MASTERSMSL )
  {
    if( ib_port_info_get_master_smsl( p_comp_pi ) !=
        ib_port_info_get_master_smsl( p_pi ) )
      goto Exit;
  }
  if( comp_mask & IB_PIR_COMPMASK_VLCAP )
  {
    if( ib_port_info_get_vl_cap( p_comp_pi ) !=
        ib_port_info_get_vl_cap( p_pi ) )
      goto Exit;
  }
  if( comp_mask & IB_PIR_COMPMASK_INITTYPE )
  {
    if( ib_port_info_get_init_type( p_comp_pi ) !=
        ib_port_info_get_init_type( p_pi ) )
      goto Exit;
  }
  if( comp_mask & IB_PIR_COMPMASK_VLHIGHLIMIT )
  {
    if( p_comp_pi->vl_high_limit != p_pi->vl_high_limit )
      goto Exit;
  }
  if( comp_mask & IB_PIR_COMPMASK_VLARBHIGHCAP )
  {
    if( p_comp_pi->vl_arb_high_cap != p_pi->vl_arb_high_cap )
      goto Exit;
  }
  if( comp_mask & IB_PIR_COMPMASK_VLARBLOWCAP )
  {
    if( p_comp_pi->vl_arb_low_cap != p_pi->vl_arb_low_cap )
      goto Exit;
  }
  if( comp_mask & IB_PIR_COMPMASK_MTUCAP )
  {
    if( ib_port_info_get_mtu_cap( p_comp_pi ) !=
        ib_port_info_get_mtu_cap( p_pi ) )
      goto Exit;
  }
  if( comp_mask & IB_PIR_COMPMASK_VLSTALLCNT )
  {
    if( ib_port_info_get_vl_stall_count( p_comp_pi ) !=
        ib_port_info_get_vl_stall_count( p_pi ) )
      goto Exit;
  }
  if (comp_mask & IB_PIR_COMPMASK_HOQLIFE )
  {
    if ((p_comp_pi->vl_stall_life & 0x1F) != (p_pi->vl_stall_life & 0x1F) )
      goto Exit;
  }
  if (comp_mask & IB_PIR_COMPMASK_OPVLS )
  {
    if ((p_comp_pi->vl_enforce & 0xF0) != (p_pi->vl_enforce & 0xF0) )
      goto Exit;
  }
  if (comp_mask & IB_PIR_COMPMASK_PARENFIN )
  {
    if ((p_comp_pi->vl_enforce & 0x08) != (p_pi->vl_enforce & 0x08) )
      goto Exit;
  }
  if (comp_mask & IB_PIR_COMPMASK_PARENFOUT )
  {
    if ((p_comp_pi->vl_enforce & 0x04) != (p_pi->vl_enforce & 0x04) )
      goto Exit;
  }
  if (comp_mask & IB_PIR_COMPMASK_FILTERRAWIN )
  {
    if ((p_comp_pi->vl_enforce & 0x02) != (p_pi->vl_enforce & 0x02) )
      goto Exit;
  }
  if (comp_mask & IB_PIR_COMPMASK_FILTERRAWOUT )
  {
    if ((p_comp_pi->vl_enforce & 0x01) != (p_pi->vl_enforce & 0x01) )
      goto Exit;
  }
  if (comp_mask & IB_PIR_COMPMASK_MKEYVIO )
  {
    if (p_comp_pi->m_key_violations != p_pi->m_key_violations )
      goto Exit;
  }
  if (comp_mask & IB_PIR_COMPMASK_PKEYVIO )
  {
    if (p_comp_pi->p_key_violations != p_pi->p_key_violations )
      goto Exit;
  }
  if (comp_mask & IB_PIR_COMPMASK_QKEYVIO )
  {
    if (p_comp_pi->q_key_violations != p_pi->q_key_violations )
      goto Exit;
  }
  if (comp_mask & IB_PIR_COMPMASK_GUIDCAP )
  {
    if (p_comp_pi->guid_cap != p_pi->guid_cap )
      goto Exit;
  }
  if (comp_mask & IB_PIR_COMPMASK_SUBNTO )
  {
    if (ib_port_info_get_timeout(p_comp_pi) != ib_port_info_get_timeout(p_pi))
      goto Exit;
  }
  if (comp_mask & IB_PIR_COMPMASK_RESPTIME )
  {
    if ((p_comp_pi->resp_time_value & 0x1F) != (p_pi->resp_time_value &0x1F) )
      goto Exit;
  }
  if (comp_mask & IB_PIR_COMPMASK_LOCALPHYERR )
  {
    if( ib_port_info_get_local_phy_err_thd( p_comp_pi ) !=
        ib_port_info_get_local_phy_err_thd( p_pi ) )
      goto Exit;
  }
  if (comp_mask & IB_PIR_COMPMASK_OVERRUNERR)
  {
    if( ib_port_info_get_overrun_err_thd( p_comp_pi ) !=
        ib_port_info_get_overrun_err_thd( p_pi ) )
      goto Exit;
  }

  __osm_sa_pir_create( p_rcv, p_physp, p_ctxt );

 Exit:
  OSM_LOG_EXIT( p_rcv->p_log );
}

/**********************************************************************
 **********************************************************************/
static void
__osm_sa_pir_by_comp_mask(
  IN osm_pir_rcv_t*        const p_rcv,
  IN const osm_port_t*     const p_port,
  osm_pir_search_ctxt_t*   const p_ctxt )
{
  const ib_portinfo_record_t* p_rcvd_rec;
  ib_net64_t               comp_mask;
  const osm_physp_t*       p_physp;
  uint8_t                  port_num;
  uint8_t                  num_ports;
  const osm_physp_t*       p_req_physp;

  OSM_LOG_ENTER( p_rcv->p_log, __osm_sa_pir_by_comp_mask );

  p_rcvd_rec = p_ctxt->p_rcvd_rec;
  comp_mask = p_ctxt->comp_mask;
  p_req_physp = p_ctxt->p_req_physp;

  num_ports = osm_node_get_num_physp( p_port->p_node );

  if( comp_mask & IB_PIR_COMPMASK_PORTNUM )
  {
    if (p_rcvd_rec->port_num < num_ports)
    {
      p_physp = osm_node_get_physp_ptr( p_port->p_node, p_rcvd_rec->port_num );
      /* Check that the p_physp is valid, and that the p_physp and the
         p_req_physp share a pkey. */
      if( osm_physp_is_valid( p_physp ) &&
          osm_physp_share_pkey(p_rcv->p_log, p_req_physp, p_physp))
        __osm_sa_pir_check_physp( p_rcv, p_physp, p_ctxt );
    }
  }
  else
  {
    for( port_num = 0; port_num < num_ports; port_num++ )
    {
      p_physp = osm_node_get_physp_ptr( p_port->p_node, port_num );
      if( !osm_physp_is_valid( p_physp ) )
        continue;

      /* if the requester and the p_physp don't share a pkey -
         continue */
      if (!osm_physp_share_pkey(p_rcv->p_log, p_req_physp, p_physp ) )
        continue;

      __osm_sa_pir_check_physp( p_rcv, p_physp, p_ctxt );
    }
  }

  OSM_LOG_EXIT( p_rcv->p_log );
}

/**********************************************************************
 **********************************************************************/
static void
__osm_sa_pir_by_comp_mask_cb(
  IN cl_map_item_t*        const p_map_item,
  IN void*                 context )
{
  const osm_port_t*        const p_port = (osm_port_t*)p_map_item;
  osm_pir_search_ctxt_t*   const p_ctxt = (osm_pir_search_ctxt_t *)context;

  __osm_sa_pir_by_comp_mask( p_ctxt->p_rcv, p_port, p_ctxt );
}

/**********************************************************************
 **********************************************************************/
void
osm_pir_rcv_process(
  IN void *ctx,
  IN void *data )
{
  osm_pir_rcv_t *p_rcv = ctx;
  osm_madw_t *p_madw = data;
  const ib_sa_mad_t*       p_rcvd_mad;
  const ib_portinfo_record_t* p_rcvd_rec;
  const cl_ptr_vector_t*   p_tbl;
  const osm_port_t*        p_port = NULL;
  const ib_port_info_t*    p_pi;
  cl_qlist_t               rec_list;
  osm_madw_t*              p_resp_madw;
  ib_sa_mad_t*             p_resp_sa_mad;
  ib_portinfo_record_t*    p_resp_rec;
  uint32_t                 num_rec, pre_trim_num_rec;
#ifndef VENDOR_RMPP_SUPPORT
  uint32_t		   trim_num_rec;
#endif
  uint32_t                 i;
  osm_pir_search_ctxt_t    context;
  osm_pir_item_t*          p_rec_item;
  ib_api_status_t          status = IB_SUCCESS;
  ib_net64_t               comp_mask;
  osm_physp_t*             p_req_physp;
  boolean_t                trusted_req = TRUE;

  CL_ASSERT( p_rcv );

  OSM_LOG_ENTER( p_rcv->p_log, osm_pir_rcv_process );

  CL_ASSERT( p_madw );

  p_rcvd_mad = osm_madw_get_sa_mad_ptr( p_madw );
  p_rcvd_rec = (ib_portinfo_record_t*)ib_sa_mad_get_payload_ptr( p_rcvd_mad );
  comp_mask = p_rcvd_mad->comp_mask;

  CL_ASSERT( p_rcvd_mad->attr_id == IB_MAD_ATTR_PORTINFO_RECORD );

  /* we only support SubnAdmGet and SubnAdmGetTable methods */
  if ( (p_rcvd_mad->method != IB_MAD_METHOD_GET) &&
       (p_rcvd_mad->method != IB_MAD_METHOD_GETTABLE) )
  {
    osm_log( p_rcv->p_log, OSM_LOG_ERROR,
             "osm_pir_rcv_process: ERR 2105: "
             "Unsupported Method (%s)\n",
             ib_get_sa_method_str( p_rcvd_mad->method ) );
    osm_sa_send_error( p_rcv->p_resp, p_madw, IB_MAD_STATUS_UNSUP_METHOD_ATTR );
    goto Exit;
  }

  /* update the requester physical port. */
  p_req_physp = osm_get_physp_by_mad_addr(p_rcv->p_log,
                                          p_rcv->p_subn,
                                          osm_madw_get_mad_addr_ptr(p_madw) );
  if (p_req_physp == NULL)
  {
    osm_log( p_rcv->p_log, OSM_LOG_ERROR,
             "osm_pir_rcv_process: ERR 2104: "
             "Cannot find requester physical port\n" );
    goto Exit;
  }

  if ( osm_log_is_active( p_rcv->p_log, OSM_LOG_DEBUG ) )
    osm_dump_portinfo_record( p_rcv->p_log, p_rcvd_rec, OSM_LOG_DEBUG );

  p_tbl = &p_rcv->p_subn->port_lid_tbl;
  p_pi = &p_rcvd_rec->port_info;

  cl_qlist_init( &rec_list );

  context.p_rcvd_rec = p_rcvd_rec;
  context.p_list = &rec_list;
  context.comp_mask = p_rcvd_mad->comp_mask;
  context.p_rcv = p_rcv;
  context.p_req_physp = p_req_physp;
  context.is_enhanced_comp_mask = cl_ntoh32(p_rcvd_mad->attr_mod) & (1 << 31);

  cl_plock_acquire( p_rcv->p_lock );

  CL_ASSERT( cl_ptr_vector_get_size(p_tbl) < 0x10000 );

  /*
    If the user specified a LID, it obviously narrows our
    work load, since we don't have to search every port
  */
  if( comp_mask & IB_PIR_COMPMASK_LID )
  {
    status = osm_get_port_by_base_lid( p_rcv->p_subn, p_rcvd_rec->lid, &p_port );
    if ( ( status != IB_SUCCESS ) || ( p_port == NULL ) )
    {
      status = IB_NOT_FOUND;
      osm_log( p_rcv->p_log, OSM_LOG_ERROR,
               "osm_pir_rcv_process: ERR 2109: "
               "No port found with LID 0x%x\n",
               cl_ntoh16(p_rcvd_rec->lid) );
    }
  }
  else
  {
    if( comp_mask & IB_PIR_COMPMASK_BASELID )
    {
      if ((uint16_t)cl_ptr_vector_get_size(p_tbl) > cl_ntoh16(p_pi->base_lid))
      {
        p_port = cl_ptr_vector_get( p_tbl, cl_ntoh16(p_pi->base_lid) );
      }
      else
      {
        status = IB_NOT_FOUND;
        osm_log( p_rcv->p_log, OSM_LOG_ERROR,
                 "osm_pir_rcv_process: ERR 2103: "
                 "Given LID (0x%X) is out of range:0x%X\n",
                 cl_ntoh16(p_pi->base_lid), cl_ptr_vector_get_size(p_tbl));
      }
    }
  }

  if ( status == IB_SUCCESS )
  {
    if( p_port )
      __osm_sa_pir_by_comp_mask( p_rcv, p_port, &context );
    else
    {
      cl_qmap_apply_func( &p_rcv->p_subn->port_guid_tbl,
                          __osm_sa_pir_by_comp_mask_cb,
                          &context );
    }
  }

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
               "osm_pir_rcv_process: ERR 2108: "
               "Got more than one record for SubnAdmGet (%u)\n",
               num_rec );
      osm_sa_send_error( p_rcv->p_resp, p_madw,
                         IB_SA_MAD_STATUS_TOO_MANY_RECORDS);

      /* need to set the mem free ... */
      p_rec_item = (osm_pir_item_t*)cl_qlist_remove_head( &rec_list );
      while( p_rec_item != (osm_pir_item_t*)cl_qlist_end( &rec_list ) )
      {
        cl_qlock_pool_put( &p_rcv->pool, &p_rec_item->pool_item );
        p_rec_item = (osm_pir_item_t*)cl_qlist_remove_head( &rec_list );
      }

      goto Exit;
    }
  }

  pre_trim_num_rec = num_rec;
#ifndef VENDOR_RMPP_SUPPORT
  trim_num_rec = (MAD_BLOCK_SIZE - IB_SA_MAD_HDR_SIZE) / sizeof(ib_portinfo_record_t);
  if (trim_num_rec < num_rec)
  {
    osm_log( p_rcv->p_log, OSM_LOG_VERBOSE,
             "osm_pir_rcv_process: "
             "Number of records:%u trimmed to:%u to fit in one MAD\n",
             num_rec, trim_num_rec );
    num_rec = trim_num_rec;
  }
#endif

  osm_log( p_rcv->p_log, OSM_LOG_DEBUG,
           "osm_pir_rcv_process: "
           "Returning %u records\n", num_rec );

  if ((p_rcvd_mad->method == IB_MAD_METHOD_GET) && (num_rec == 0))
  {
    osm_sa_send_error( p_rcv->p_resp, p_madw,
                       IB_SA_MAD_STATUS_NO_RECORDS );
    goto Exit;
  }

  /*
   * Get a MAD to reply. Address of Mad is in the received mad_wrapper
   */
  p_resp_madw = osm_mad_pool_get( p_rcv->p_mad_pool,
                                  p_madw->h_bind,
                                  num_rec * sizeof(ib_portinfo_record_t) + IB_SA_MAD_HDR_SIZE,
                                  &p_madw->mad_addr );

  if( !p_resp_madw )
  {
    osm_log(p_rcv->p_log, OSM_LOG_ERROR,
            "osm_pir_rcv_process: ERR 2106: "
            "osm_mad_pool_get failed\n" );

    for( i = 0; i < num_rec; i++ )
    {
      p_rec_item = (osm_pir_item_t*)cl_qlist_remove_head( &rec_list );
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
    ib_get_attr_offset( sizeof(ib_portinfo_record_t) );

  p_resp_rec = (ib_portinfo_record_t*)
    ib_sa_mad_get_payload_ptr( p_resp_sa_mad );

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

  /*
    p922 - The M_Key returned shall be zero, except in the case of a 
    trusted request. 
    Note: In the mad controller we check that the SM_Key received on
    the mad is valid. Meaning - is either zero or equal to the local
    sm_key.
  */
  if (p_rcvd_mad->sm_key == 0)
    trusted_req = FALSE;

  for( i = 0; i < pre_trim_num_rec; i++ )
  {
    p_rec_item = (osm_pir_item_t*)cl_qlist_remove_head( &rec_list );
    /* copy only if not trimmed */
    if (i < num_rec)
    {
      *p_resp_rec = p_rec_item->rec;
      if (trusted_req == FALSE)
        p_resp_rec->port_info.m_key = 0;
    }
    cl_qlock_pool_put( &p_rcv->pool, &p_rec_item->pool_item );
    p_resp_rec++;
  }

  CL_ASSERT( cl_is_qlist_empty( &rec_list ) );

  status = osm_vendor_send( p_resp_madw->h_bind, p_resp_madw, FALSE);
  if (status != IB_SUCCESS)
  {
    osm_log(p_rcv->p_log, OSM_LOG_ERROR,
            "osm_pir_rcv_process: ERR 2107: "
            "osm_vendor_send status = %s\n",
            ib_get_err_str(status));
    goto Exit;
  }

 Exit:
  OSM_LOG_EXIT( p_rcv->p_log );
}

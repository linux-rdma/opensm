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
 *    Implementation of osm_pr_rcv_t.
 * This object represents the PathRecord Receiver object.
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
#include <opensm/osm_base.h>
#include <opensm/osm_sa_path_record.h>
#include <opensm/osm_port.h>
#include <opensm/osm_node.h>
#include <opensm/osm_switch.h>
#include <vendor/osm_vendor.h>
#include <vendor/osm_vendor_api.h>
#include <opensm/osm_helper.h>
#include <opensm/osm_pkey.h>
#include <opensm/osm_multicast.h>
#include <opensm/osm_partition.h>
#include <opensm/osm_opensm.h>
#ifdef ROUTER_EXP
#include <opensm/osm_router.h>
#include <opensm/osm_sa_mcmember_record.h>
#endif

#define OSM_PR_RCV_POOL_MIN_SIZE    64
#define OSM_PR_RCV_POOL_GROW_SIZE   64

extern uint8_t osm_get_lash_sl(osm_opensm_t *p_osm,
                               const osm_port_t *p_src_port,
			       const osm_port_t *p_dst_port);

typedef  struct   _osm_pr_item
{
  cl_pool_item_t     pool_item;
  ib_path_rec_t      path_rec;
} osm_pr_item_t;

typedef struct _osm_path_parms
{
  ib_net16_t         pkey;
  uint8_t            mtu;
  uint8_t            rate;
  uint8_t            sl;
  uint8_t            pkt_life;
  boolean_t          reversible;
} osm_path_parms_t;

typedef  struct   osm_sa_pr_mcmr_search_ctxt
{
  ib_gid_t        *p_mgid;
  osm_mgrp_t      *p_mgrp;
  osm_pr_rcv_t    *p_rcv;
} osm_sa_pr_mcmr_search_ctxt_t;

static const ib_gid_t zero_gid = { { 0x00, 0x00, 0x00, 0x00,
                                     0x00, 0x00, 0x00, 0x00,
                                     0x00, 0x00, 0x00, 0x00,
                                     0x00, 0x00, 0x00, 0x00 }, };

/**********************************************************************
 **********************************************************************/
void
osm_pr_rcv_construct(
  IN osm_pr_rcv_t* const p_rcv )
{
  memset( p_rcv, 0, sizeof(*p_rcv) );
  cl_qlock_pool_construct( &p_rcv->pr_pool );
}

/**********************************************************************
 **********************************************************************/
void
osm_pr_rcv_destroy(
  IN osm_pr_rcv_t* const p_rcv )
{
  OSM_LOG_ENTER( p_rcv->p_log, osm_pr_rcv_destroy );
  cl_qlock_pool_destroy( &p_rcv->pr_pool );
  OSM_LOG_EXIT( p_rcv->p_log );
}

/**********************************************************************
 **********************************************************************/
ib_api_status_t
osm_pr_rcv_init(
  IN osm_pr_rcv_t*      const p_rcv,
  IN osm_sa_resp_t*     const p_resp,
  IN osm_mad_pool_t*    const p_mad_pool,
  IN osm_subn_t*        const p_subn,
  IN osm_log_t*         const p_log,
  IN cl_plock_t*        const p_lock )
{
  ib_api_status_t       status;

  OSM_LOG_ENTER( p_log, osm_pr_rcv_init );

  osm_pr_rcv_construct( p_rcv );

  p_rcv->p_log = p_log;
  p_rcv->p_subn = p_subn;
  p_rcv->p_lock = p_lock;
  p_rcv->p_resp = p_resp;
  p_rcv->p_mad_pool = p_mad_pool;

  status = cl_qlock_pool_init( &p_rcv->pr_pool,
                               OSM_PR_RCV_POOL_MIN_SIZE,
                               0,
                               OSM_PR_RCV_POOL_GROW_SIZE,
                               sizeof(osm_pr_item_t),
                               NULL, NULL, NULL );

  OSM_LOG_EXIT( p_rcv->p_log );
  return( status );
}

/**********************************************************************
 **********************************************************************/
static inline boolean_t
__osm_sa_path_rec_is_tavor_port(
  IN const osm_port_t*     const p_port)
{
  osm_node_t const* p_node;
  ib_net32_t vend_id;

  p_node = osm_port_get_parent_node( p_port );
  vend_id = ib_node_info_get_vendor_id( &p_node->node_info );
	
  return( (p_node->node_info.device_id == CL_HTON16(23108)) &&
	  ((vend_id == CL_HTON32(OSM_VENDOR_ID_MELLANOX)) || 
	   (vend_id == CL_HTON32(OSM_VENDOR_ID_TOPSPIN)) || 
	   (vend_id == CL_HTON32(OSM_VENDOR_ID_SILVERSTORM)) || 
	   (vend_id == CL_HTON32(OSM_VENDOR_ID_VOLTAIRE))) );
}

/**********************************************************************
 **********************************************************************/
static boolean_t
 __osm_sa_path_rec_apply_tavor_mtu_limit(
  IN const ib_path_rec_t*  const p_pr,
  IN const osm_port_t*     const p_src_port,
  IN const osm_port_t*     const p_dest_port,
  IN const ib_net64_t      comp_mask)
{
  uint8_t required_mtu;
	
  /* only if at least one of the ports is a Tavor device */
  if (! __osm_sa_path_rec_is_tavor_port(p_src_port) && 
      ! __osm_sa_path_rec_is_tavor_port(p_dest_port) )
    return( FALSE );

  /*
    we can apply the patch if either:
    1. No MTU required
    2. Required MTU < 
    3. Required MTU = 1K or 512 or 256
    4. Required MTU > 256 or 512
  */
  required_mtu = ib_path_rec_mtu( p_pr );
  if ( ( comp_mask & IB_PR_COMPMASK_MTUSELEC ) &&
       ( comp_mask & IB_PR_COMPMASK_MTU ) )
  {
    switch( ib_path_rec_mtu_sel( p_pr ) )
    {
    case 0:    /* must be greater than */
    case 2:    /* exact match */
      if( IB_MTU_LEN_1024 < required_mtu )
        return(FALSE);
      break;

    case 1:    /* must be less than */
               /* can't be disqualified by this one */
      break;

    case 3:    /* largest available */
               /* the ULP intentionally requested */
               /* the largest MTU possible */
      return(FALSE);
      break;
			
    default:
      /* if we're here, there's a bug in ib_path_rec_mtu_sel() */
      CL_ASSERT( FALSE );
      break;
    }
  }

  return(TRUE);
}

/**********************************************************************
 **********************************************************************/
static ib_api_status_t
__osm_pr_rcv_get_path_parms(
  IN osm_pr_rcv_t*         const p_rcv,
  IN const ib_path_rec_t*  const p_pr,
  IN const osm_port_t*     const p_src_port,
  IN const osm_port_t*     const p_dest_port,
  IN const uint16_t        dest_lid_ho,
  IN const ib_net64_t      comp_mask,
  OUT osm_path_parms_t*    const p_parms )
{
  const osm_node_t*        p_node;
  const osm_physp_t*       p_physp;
  const osm_physp_t*       p_dest_physp;
  const osm_prtn_t*        p_prtn;
  const ib_port_info_t*    p_pi;
  ib_api_status_t          status = IB_SUCCESS;
  ib_net16_t               pkey;
  uint8_t                  mtu;
  uint8_t                  rate;
  uint8_t                  pkt_life;
  uint8_t                  required_mtu;
  uint8_t                  required_rate;
  uint8_t                  required_pkt_life;
  uint8_t                  sl;
  ib_net16_t               dest_lid;

  OSM_LOG_ENTER( p_rcv->p_log, __osm_pr_rcv_get_path_parms );

  dest_lid = cl_hton16( dest_lid_ho );

  p_dest_physp = osm_port_get_default_phys_ptr( p_dest_port );
  p_physp = osm_port_get_default_phys_ptr( p_src_port );
  p_pi = &p_physp->port_info;

  mtu = ib_port_info_get_mtu_cap( p_pi );
  rate = ib_port_info_compute_rate( p_pi );

  /* 
    Mellanox Tavor device performance is better using 1K MTU.
    If required MTU and MTU selector are such that 1K is OK 
    and at least one end of the path is Tavor we override the
    port MTU with 1K.
  */
  if ( p_rcv->p_subn->opt.enable_quirks &&
       __osm_sa_path_rec_apply_tavor_mtu_limit(
		p_pr, p_src_port, p_dest_port, comp_mask) )
    if (mtu > IB_MTU_LEN_1024) 
    {
      mtu = IB_MTU_LEN_1024;
      osm_log( p_rcv->p_log, OSM_LOG_DEBUG,
	       "__osm_pr_rcv_get_path_parms: "
	       "Optimized Path MTU to 1K for Mellanox Tavor device\n");
    }

  /*
    Walk the subnet object from source to destination,
    tracking the most restrictive rate and mtu values along the way...

    If source port node is a switch, then p_physp should
    point to the port that routes the destination lid
  */

  p_node = osm_physp_get_node_ptr( p_physp );

  if( p_node->sw )
  {
    /*
     * If the dest_lid_ho is equal to the lid of the switch pointed by
     * p_sw then p_physp will be the physical port of the switch port zero.
     */
    p_physp = osm_switch_get_route_by_lid(p_node->sw, cl_ntoh16( dest_lid_ho ) );
    if ( p_physp == 0 )
    {
      osm_log( p_rcv->p_log, OSM_LOG_ERROR,
               "__osm_pr_rcv_get_path_parms: ERR 1F02: "
               "Cannot find routing to LID 0x%X from switch for GUID 0x%016" PRIx64 "\n",
               dest_lid_ho,
               cl_ntoh64( osm_node_get_node_guid( p_node ) ) );
      status = IB_NOT_FOUND;
      goto Exit;
    }
  }

  /*
   * Same as above
   */
  p_node = osm_physp_get_node_ptr( p_dest_physp );

  if( p_node->sw )
  {
    p_dest_physp = osm_switch_get_route_by_lid( p_node->sw, cl_ntoh16( dest_lid_ho ) );

    if ( p_dest_physp == 0 )
    {
      osm_log( p_rcv->p_log, OSM_LOG_ERROR,
               "__osm_pr_rcv_get_path_parms: ERR 1F03: "
               "Cannot find routing to LID 0x%X from switch for GUID 0x%016" PRIx64 "\n",
               dest_lid_ho,
               cl_ntoh64( osm_node_get_node_guid( p_node ) ) );
      status = IB_NOT_FOUND;
      goto Exit;
    }

  }

  while( p_physp != p_dest_physp )
  {
    p_physp = osm_physp_get_remote( p_physp );

    if ( p_physp == 0 )
    {
      osm_log( p_rcv->p_log, OSM_LOG_ERROR,
               "__osm_pr_rcv_get_path_parms: ERR 1F05: "
               "Cannot find remote phys port when routing to LID 0x%X from node GUID 0x%016" PRIx64 "\n",
               dest_lid_ho,
               cl_ntoh64( osm_node_get_node_guid( p_node ) ) );
      status = IB_ERROR;
      goto Exit;
    }

    /*
      This is point to point case (no switch in between)
    */
    if( p_physp == p_dest_physp )
      break;

    p_node = osm_physp_get_node_ptr( p_physp );

    if( !p_node->sw )
    {
      /*
        There is some sort of problem in the subnet object!
        If this isn't a switch, we should have reached
        the destination by now!
      */
      osm_log( p_rcv->p_log, OSM_LOG_ERROR,
               "__osm_pr_rcv_get_path_parms: ERR 1F06: "
               "Internal error, bad path\n" );
      status = IB_ERROR;
      goto Exit;
    }

    /*
      Check parameters for the ingress port in this switch.
    */
    p_pi = &p_physp->port_info;

    if( mtu > ib_port_info_get_mtu_cap( p_pi ) )
    {
      mtu = ib_port_info_get_mtu_cap( p_pi );
      if( osm_log_is_active( p_rcv->p_log, OSM_LOG_DEBUG ) )
      {
        osm_log( p_rcv->p_log, OSM_LOG_DEBUG,
                 "__osm_pr_rcv_get_path_parms: "
                 "New smallest MTU = %u at intervening port 0x%016" PRIx64
                 " port num 0x%X\n",
                 mtu,
                 cl_ntoh64( osm_physp_get_port_guid( p_physp ) ),
                 osm_physp_get_port_num( p_physp ) );
      }
    }

    if( rate > ib_port_info_compute_rate( p_pi ) )
    {
      rate = ib_port_info_compute_rate( p_pi );
      if( osm_log_is_active( p_rcv->p_log, OSM_LOG_DEBUG ) )
      {
        osm_log( p_rcv->p_log, OSM_LOG_DEBUG,
                 "__osm_pr_rcv_get_path_parms: "
                 "New smallest rate = %u at intervening port 0x%016" PRIx64
                 " port num 0x%X\n",
                 rate,
                 cl_ntoh64( osm_physp_get_port_guid( p_physp ) ),
                 osm_physp_get_port_num( p_physp ) );
      }
    }

    /*
      Continue with the egress port on this switch.
    */
    p_physp = osm_switch_get_route_by_lid( p_node->sw, dest_lid );

    if ( p_physp == 0 )
    {
      osm_log( p_rcv->p_log, OSM_LOG_ERROR,
               "__osm_pr_rcv_get_path_parms: ERR 1F07: "
               "Dead end on path to LID 0x%X from switch for GUID 0x%016" PRIx64 "\n",
               dest_lid_ho,
               cl_ntoh64( osm_node_get_node_guid( p_node ) ) );
      status = IB_ERROR;
      goto Exit;
    }

    CL_ASSERT( p_physp );
    CL_ASSERT( osm_physp_is_valid( p_physp ) );

    p_pi = &p_physp->port_info;

    if( mtu > ib_port_info_get_mtu_cap( p_pi ) )
    {
      mtu = ib_port_info_get_mtu_cap( p_pi );
      if( osm_log_is_active( p_rcv->p_log, OSM_LOG_DEBUG ) )
      {
        osm_log( p_rcv->p_log, OSM_LOG_DEBUG,
                 "__osm_pr_rcv_get_path_parms: "
                 "New smallest MTU = %u at intervening port 0x%016" PRIx64
                 " port num 0x%X\n",
                 mtu,
                 cl_ntoh64( osm_physp_get_port_guid( p_physp ) ),
                 osm_physp_get_port_num( p_physp ) );
      }
    }

    if( rate > ib_port_info_compute_rate( p_pi ) )
    {
      rate = ib_port_info_compute_rate( p_pi );
      if( osm_log_is_active( p_rcv->p_log, OSM_LOG_DEBUG ) )
      {
        osm_log( p_rcv->p_log, OSM_LOG_DEBUG,
                 "__osm_pr_rcv_get_path_parms: "
                 "New smallest rate = %u at intervening port 0x%016" PRIx64
                 " port num 0x%X\n",
                 rate,
                 cl_ntoh64( osm_physp_get_port_guid( p_physp ) ),
                 osm_physp_get_port_num( p_physp ) );
      }
    }

  }

  /*
    p_physp now points to the destination
  */
  p_pi = &p_physp->port_info;

  if( mtu > ib_port_info_get_mtu_cap( p_pi ) )
  {
    mtu = ib_port_info_get_mtu_cap( p_pi );
    if( osm_log_is_active( p_rcv->p_log, OSM_LOG_DEBUG ) )
    {
      osm_log( p_rcv->p_log, OSM_LOG_DEBUG,
               "__osm_pr_rcv_get_path_parms: "
               "New smallest MTU = %u at destination port 0x%016" PRIx64 "\n",
               mtu,
               cl_ntoh64(osm_physp_get_port_guid( p_physp )) );
    }
  }

  if( rate > ib_port_info_compute_rate( p_pi ) )
  {
    rate = ib_port_info_compute_rate( p_pi );
    if( osm_log_is_active( p_rcv->p_log, OSM_LOG_DEBUG ) )
    {
      osm_log( p_rcv->p_log, OSM_LOG_DEBUG,
               "__osm_pr_rcv_get_path_parms: "
               "New smallest rate = %u at destination port 0x%016" PRIx64 "\n",
               rate,
               cl_ntoh64(osm_physp_get_port_guid( p_physp )) );
    }
  }

  if( osm_log_is_active( p_rcv->p_log, OSM_LOG_DEBUG ) )
  {
    osm_log( p_rcv->p_log, OSM_LOG_DEBUG,
             "__osm_pr_rcv_get_path_parms: "
             "Path min MTU = %u, min rate = %u\n", mtu, rate );
  }

  /*
    Determine if these values meet the user criteria
    and adjust appropriately
  */

  /* we silently ignore cases where only the MTU selector is defined */
  if ( ( comp_mask & IB_PR_COMPMASK_MTUSELEC ) &&
       ( comp_mask & IB_PR_COMPMASK_MTU ) )
  {
    required_mtu = ib_path_rec_mtu( p_pr );
    switch( ib_path_rec_mtu_sel( p_pr ) )
    {
    case 0:    /* must be greater than */
      if( mtu <= required_mtu )
        status = IB_NOT_FOUND;
      break;

    case 1:    /* must be less than */
       if( mtu >= required_mtu )
       {
          /* adjust to use the highest mtu
             lower then the required one */
          if( required_mtu > 1 )
            mtu = required_mtu - 1;
          else
            status = IB_NOT_FOUND;
       }
      break;

    case 2:    /* exact match */
      if( mtu < required_mtu )
        status = IB_NOT_FOUND;
      else
        mtu = required_mtu;
      break;

    case 3:    /* largest available */
      /* can't be disqualified by this one */
      break;

    default:
      /* if we're here, there's a bug in ib_path_rec_mtu_sel() */
      CL_ASSERT( FALSE );
      status = IB_ERROR;
      break;
    }
  }

  /* we silently ignore cases where only the Rate selector is defined */
  if ( ( comp_mask & IB_PR_COMPMASK_RATESELEC ) &&
       ( comp_mask & IB_PR_COMPMASK_RATE ) )
  {
    required_rate = ib_path_rec_rate( p_pr );
    switch( ib_path_rec_rate_sel( p_pr ) )
    {
    case 0:    /* must be greater than */
      if( rate <= required_rate )
        status = IB_NOT_FOUND;
      break;

    case 1:    /* must be less than */
      if( rate >= required_rate )
      {
        /* adjust the rate to use the highest rate
           lower then the required one */
        if( required_rate > 2 )
          rate = required_rate - 1;
        else
          status = IB_NOT_FOUND;
      }
      break;

    case 2:    /* exact match */
      if( rate < required_rate )
        status = IB_NOT_FOUND;
      else
        rate = required_rate;
      break;

    case 3:    /* largest available */
      /* can't be disqualified by this one */
      break;

    default:
      /* if we're here, there's a bug in ib_path_rec_mtu_sel() */
      CL_ASSERT( FALSE );
      status = IB_ERROR;
      break;
    }
  }

  /* Verify the pkt_life_time */
  /* According to spec definition IBA 1.2 Table 205 PacketLifeTime description,
     for loopback paths, packetLifeTime shall be zero. */
  if ( p_src_port == p_dest_port )
    pkt_life = 0;	/* loopback */
  else
    pkt_life = OSM_DEFAULT_SUBNET_TIMEOUT;

  /* we silently ignore cases where only the PktLife selector is defined */
  if ( ( comp_mask & IB_PR_COMPMASK_PKTLIFETIMESELEC ) &&
       ( comp_mask & IB_PR_COMPMASK_PKTLIFETIME ) )
  {
    required_pkt_life = ib_path_rec_pkt_life( p_pr );
    switch( ib_path_rec_pkt_life_sel( p_pr ) )
    {
    case 0:    /* must be greater than */
      if( pkt_life <= required_pkt_life )
        status = IB_NOT_FOUND;
      break;

    case 1:    /* must be less than */
      if( pkt_life >= required_pkt_life )
      {
        /* adjust the lifetime to use the highest possible
           lower then the required one */
        if( required_pkt_life > 1 )
          pkt_life = required_pkt_life - 1;
        else
          status = IB_NOT_FOUND;
      }
      break;

    case 2:    /* exact match */
      if( pkt_life < required_pkt_life )
         status = IB_NOT_FOUND;
      else
         pkt_life = required_pkt_life;
      break;

    case 3:    /* smallest available */
      /* can't be disqualified by this one */
      break;

    default:
      /* if we're here, there's a bug in ib_path_rec_pkt_life_sel() */
      CL_ASSERT( FALSE );
      status = IB_ERROR;
      break;
    }
  }

  if (status != IB_SUCCESS)
    goto Exit;

  if( comp_mask & IB_PR_COMPMASK_RAWTRAFFIC &&
      cl_ntoh32( p_pr->hop_flow_raw ) & ( 1<<31 ) )
    pkey = osm_physp_find_common_pkey( p_physp, p_dest_physp );
  else if( comp_mask & IB_PR_COMPMASK_PKEY )
  {
    pkey = p_pr->pkey;
    if( !osm_physp_share_this_pkey( p_physp, p_dest_physp, pkey ) )
    {
      osm_log( p_rcv->p_log, OSM_LOG_ERROR,
               "__osm_pr_rcv_get_path_parms: ERR 1F1A: "
               "Ports do not share specified PKey 0x%04x\n", cl_ntoh16(pkey));
      status = IB_NOT_FOUND;
      goto Exit;
    }
  }
  else
  {
    pkey = osm_physp_find_common_pkey( p_physp, p_dest_physp );
    if ( !pkey )
    {
      osm_log( p_rcv->p_log, OSM_LOG_ERROR,
               "__osm_pr_rcv_get_path_parms: ERR 1F1B: "
               "Ports do not have any shared PKeys\n");
      status = IB_NOT_FOUND;
      goto Exit;
    }
  }

  if (p_rcv->p_subn->opt.routing_engine_name &&
      strcmp(p_rcv->p_subn->opt.routing_engine_name, "lash") == 0)
    /* slid and dest_lid are stored in network in lash */
    sl = osm_get_lash_sl(p_rcv->p_subn->p_osm, p_src_port, p_dest_port);
  else
    sl = OSM_DEFAULT_SL;

  if (pkey) {
    p_prtn = (osm_prtn_t *)cl_qmap_get(&p_rcv->p_subn->prtn_pkey_tbl,
                                       pkey & cl_ntoh16((uint16_t)~0x8000));
    if ( p_prtn == (osm_prtn_t *)cl_qmap_end(&p_rcv->p_subn->prtn_pkey_tbl) )
    {
      /* this may be possible when pkey tables are created somehow in
         previous runs or things are going wrong here */
      osm_log( p_rcv->p_log, OSM_LOG_ERROR,
               "__osm_pr_rcv_get_path_parms: ERR 1F1C: "
               "No partition found for PKey 0x%04x - using default SL %d\n",
               cl_ntoh16(pkey), sl );
    }
    else
    {
      if (p_rcv->p_subn->opt.routing_engine_name &&
          strcmp(p_rcv->p_subn->opt.routing_engine_name, "lash") == 0)
        /* slid and dest_lid are stored in network in lash */
        sl = osm_get_lash_sl(p_rcv->p_subn->p_osm, p_src_port, p_dest_port);
      else
        sl = p_prtn->sl;
    }

    /* reset pkey when raw traffic */
    if( comp_mask & IB_PR_COMPMASK_RAWTRAFFIC &&
        cl_ntoh32( p_pr->hop_flow_raw ) & ( 1<<31 ) )
      pkey = 0;
  }

  if ( ( comp_mask & IB_PR_COMPMASK_SL ) && ib_path_rec_sl( p_pr ) != sl )
  {
    status = IB_NOT_FOUND;
    goto Exit;
  }

  p_parms->mtu = mtu;
  p_parms->rate = rate;
  p_parms->pkt_life = pkt_life;
  p_parms->pkey = pkey;
  p_parms->sl = sl;

 Exit:
  OSM_LOG_EXIT( p_rcv->p_log );
  return( status );
}

/**********************************************************************
 **********************************************************************/
static void
__osm_pr_rcv_build_pr(
  IN osm_pr_rcv_t*         const p_rcv,
  IN const osm_port_t*     const p_src_port,
  IN const osm_port_t*     const p_dest_port,
  IN const ib_gid_t*       const p_dgid,
  IN const uint16_t        src_lid_ho,
  IN const uint16_t        dest_lid_ho,
  IN const uint8_t         preference,
  IN const osm_path_parms_t*  const p_parms,
  OUT ib_path_rec_t*       const p_pr )
{
  const osm_physp_t*       p_src_physp;
  const osm_physp_t*       p_dest_physp;
#ifdef ROUTER_EXP
  boolean_t                is_nonzero_gid = 0;
#endif

  OSM_LOG_ENTER( p_rcv->p_log, __osm_pr_rcv_build_pr );

  p_src_physp = osm_port_get_default_phys_ptr( p_src_port );
#ifndef ROUTER_EXP
  p_dest_physp = osm_port_get_default_phys_ptr( p_dest_port );

  p_pr->dgid.unicast.prefix = osm_physp_get_subnet_prefix( p_dest_physp );
  p_pr->dgid.unicast.interface_id = osm_physp_get_port_guid( p_dest_physp );
#else
  if ( p_dgid)
  {
    if ( memcmp( p_dgid, &zero_gid, sizeof(*p_dgid) ) )
      is_nonzero_gid = 1;
  }

  if ( is_nonzero_gid )
    p_pr->dgid = *p_dgid;
  else
  {
    p_dest_physp = osm_port_get_default_phys_ptr( p_dest_port );

    p_pr->dgid.unicast.prefix = osm_physp_get_subnet_prefix( p_dest_physp );
    p_pr->dgid.unicast.interface_id = osm_physp_get_port_guid( p_dest_physp );
  }
#endif

  p_pr->sgid.unicast.prefix = osm_physp_get_subnet_prefix( p_src_physp );
  p_pr->sgid.unicast.interface_id = osm_physp_get_port_guid( p_src_physp );

  p_pr->dlid = cl_hton16( dest_lid_ho );
  p_pr->slid = cl_hton16( src_lid_ho );

  p_pr->hop_flow_raw &= cl_hton32(1<<31);
#ifdef ROUTER_EXP
  /* Only set HopLimit if going through a router */
  if ( is_nonzero_gid )
    p_pr->hop_flow_raw |= cl_hton32(IB_HOPLIMIT_MAX);
#endif

  p_pr->pkey = p_parms->pkey;
  p_pr->sl = cl_hton16(p_parms->sl);
  p_pr->mtu = (uint8_t)(p_parms->mtu | 0x80);
  p_pr->rate = (uint8_t)(p_parms->rate | 0x80);

  /* According to 1.2 spec definition Table 205 PacketLifeTime description,
     for loopback paths, packetLifeTime shall be zero. */
  if ( p_src_port == p_dest_port )
    p_pr->pkt_life = 0x80;	/* loopback */
  else
    p_pr->pkt_life = (uint8_t)(p_parms->pkt_life | 0x80);

  p_pr->preference = preference;

  /* always return num_path = 0 so this is only the reversible component */
  if (p_parms->reversible)
    p_pr->num_path = 0x80;

  OSM_LOG_EXIT( p_rcv->p_log );
}

/**********************************************************************
 **********************************************************************/
static osm_pr_item_t*
__osm_pr_rcv_get_lid_pair_path(
  IN osm_pr_rcv_t*         const p_rcv,
  IN const ib_path_rec_t*  const p_pr,
  IN const osm_port_t*     const p_src_port,
  IN const osm_port_t*     const p_dest_port,
  IN const ib_gid_t*       const p_dgid,
  IN const uint16_t        src_lid_ho,
  IN const uint16_t        dest_lid_ho,
  IN const ib_net64_t      comp_mask,
  IN const uint8_t         preference )
{
  osm_path_parms_t         path_parms;
  osm_path_parms_t         rev_path_parms;
  osm_pr_item_t            *p_pr_item;
  ib_api_status_t          status, rev_path_status;

  OSM_LOG_ENTER( p_rcv->p_log, __osm_pr_rcv_get_lid_pair_path );

  if( osm_log_is_active( p_rcv->p_log, OSM_LOG_DEBUG ) )
  {
    osm_log( p_rcv->p_log, OSM_LOG_DEBUG,
             "__osm_pr_rcv_get_lid_pair_path: "
             "Src LID 0x%X, Dest LID 0x%X\n",
             src_lid_ho, dest_lid_ho );
  }

  p_pr_item = (osm_pr_item_t*)cl_qlock_pool_get( &p_rcv->pr_pool );
  if( p_pr_item == NULL )
  {
    osm_log( p_rcv->p_log, OSM_LOG_ERROR,
             "__osm_pr_rcv_get_lid_pair_path: ERR 1F01: "
             "Unable to allocate path record\n" );
    goto Exit;
  }

  status = __osm_pr_rcv_get_path_parms( p_rcv, p_pr, p_src_port,
                                        p_dest_port, dest_lid_ho,
                                        comp_mask, &path_parms );

  if( status != IB_SUCCESS )
  {
    cl_qlock_pool_put( &p_rcv->pr_pool, &p_pr_item->pool_item );
    p_pr_item = NULL;
    goto Exit;
  }

  /* now try the reversible path */
  rev_path_status = __osm_pr_rcv_get_path_parms( p_rcv, p_pr, p_dest_port,
                                                 p_src_port, src_lid_ho,
                                                 comp_mask, &rev_path_parms );
  path_parms.reversible = ( rev_path_status == IB_SUCCESS );

  /* did we get a Reversible Path compmask ? */
  /* 
     NOTE that if the reversible component = 0, it is a don't care
     rather then requiring non-reversible paths ... 
     see Vol1 Ver1.2 p900 l16
  */
  if( comp_mask & IB_PR_COMPMASK_REVERSIBLE )
  {
    if( (! path_parms.reversible && ( p_pr->num_path & 0x80 ) ) )
    {
      osm_log( p_rcv->p_log, OSM_LOG_DEBUG,
               "__osm_pr_rcv_get_lid_pair_path: "
               "Requested reversible path but failed to get one\n");

      cl_qlock_pool_put( &p_rcv->pr_pool, &p_pr_item->pool_item );
      p_pr_item = NULL;
      goto Exit;
    }
  }

  __osm_pr_rcv_build_pr( p_rcv, p_src_port, p_dest_port, p_dgid,
                         src_lid_ho, dest_lid_ho, preference, &path_parms,
                         &p_pr_item->path_rec );

 Exit:
  OSM_LOG_EXIT( p_rcv->p_log );
  return( p_pr_item );
}

/**********************************************************************
 **********************************************************************/
static void
__osm_pr_rcv_get_port_pair_paths(
  IN osm_pr_rcv_t*         const p_rcv,
  IN const osm_madw_t*     const p_madw,
  IN const osm_port_t*     const p_req_port,
  IN const osm_port_t*     const p_src_port,
  IN const osm_port_t*     const p_dest_port,
  IN const ib_gid_t*       const p_dgid,
  IN const ib_net64_t      comp_mask,
  IN cl_qlist_t*           const p_list )
{
  const ib_path_rec_t*     p_pr;
  const ib_sa_mad_t*       p_sa_mad;
  osm_pr_item_t*           p_pr_item;
  uint16_t                 src_lid_min_ho;
  uint16_t                 src_lid_max_ho;
  uint16_t                 dest_lid_min_ho;
  uint16_t                 dest_lid_max_ho;
  uint16_t                 src_lid_ho;
  uint16_t                 dest_lid_ho;
  uint32_t                 path_num;
  uint8_t                  preference;
  uintn_t                  iterations;
  uintn_t                  src_offset;
  uintn_t                  dest_offset;

  OSM_LOG_ENTER( p_rcv->p_log, __osm_pr_rcv_get_port_pair_paths );

  if( osm_log_is_active( p_rcv->p_log, OSM_LOG_DEBUG ) )
  {
    osm_log( p_rcv->p_log, OSM_LOG_DEBUG,
             "__osm_pr_rcv_get_port_pair_paths: "
             "Src port 0x%016" PRIx64 ", "
             "Dst port 0x%016" PRIx64 "\n",
             cl_ntoh64( osm_port_get_guid( p_src_port ) ),
             cl_ntoh64( osm_port_get_guid( p_dest_port ) ) );
  }

  /* Check that the req_port, src_port and dest_port all share a
     pkey. The check is done on the default physical port of the ports. */
  if (osm_port_share_pkey(p_rcv->p_log, p_req_port, p_src_port) == FALSE ||
      osm_port_share_pkey(p_rcv->p_log, p_req_port, p_dest_port) == FALSE ||
      osm_port_share_pkey(p_rcv->p_log, p_src_port, p_dest_port) == FALSE )
  {
    /* One of the pairs doesn't share a pkey so the path is disqualified. */
    goto Exit;
  }

  p_sa_mad = osm_madw_get_sa_mad_ptr( p_madw );
  p_pr = (ib_path_rec_t*)ib_sa_mad_get_payload_ptr( p_sa_mad );

  /*
    We shouldn't be here if the paths are disqualified in some way...
    Thus, we assume every possible connection is valid.

    We desire to return high-quality paths first.
    In OpenSM, higher quality means least overlap with other paths.
    This is acheived in practice by returning paths with
    different LID value on each end, which means these
    paths are more redundant that paths with the same LID repeated
    on one side.  For example, in OpenSM the paths between two
    endpoints with LMC = 1 might be as follows:

    Port A, LID 1 <-> Port B, LID 3
    Port A, LID 1 <-> Port B, LID 4
    Port A, LID 2 <-> Port B, LID 3
    Port A, LID 2 <-> Port B, LID 4

    The OpenSM unicast routing algorithms attempt to disperse each path
    to as varied a physical path as is reasonable.  1<->3 and 1<->4 have
    more physical overlap (hence less redundancy) than 1<->3 and 2<->4.

    OpenSM ranks paths in three preference groups:

    Preference Value    Description
    ----------------    -------------------------------------------
    0             Redundant in both directions with other
    pref value = 0 paths

    1             Redundant in one direction with other
    pref value = 0 and pref value = 1 paths

    2             Not redundant in either direction with
    other paths

    3-FF          Unused


    SA clients don't need to know these details, only that the lower
    preference paths are preferred, as stated in the spec.  The paths
    may not actually be physically redundant depending on the topology
    of the subnet, but the point of LMC > 0 is to offer redundancy,
    so it is assumed that the subnet is physically appropriate for the
    specified LMC value.  A more advanced implementation would inspect for 
    physical redundancy, but I'm not going to bother with that now.
  */

  /*
    Refine our search if the client specified end-point LIDs
  */
  if( comp_mask & IB_PR_COMPMASK_DLID )
  {
    dest_lid_min_ho = cl_ntoh16( p_pr->dlid );
    dest_lid_max_ho = cl_ntoh16( p_pr->dlid );
  }
  else
  {
    osm_port_get_lid_range_ho( p_dest_port, &dest_lid_min_ho,
                               &dest_lid_max_ho );
  }

  if( comp_mask & IB_PR_COMPMASK_SLID )
  {
    src_lid_min_ho = cl_ntoh16( p_pr->slid );
    src_lid_max_ho = cl_ntoh16( p_pr->slid );
  }
  else
  {
    osm_port_get_lid_range_ho( p_src_port, &src_lid_min_ho,
                               &src_lid_max_ho );
  }

  if ( src_lid_min_ho == 0 )
  {
    osm_log( p_rcv->p_log, OSM_LOG_ERROR,
             "__osm_pr_rcv_get_port_pair_paths: ERR 1F20:"
             "Obtained source LID of 0. No such LID possible\n");
     goto Exit;
  }

  if ( dest_lid_min_ho == 0 )
  {
    osm_log( p_rcv->p_log, OSM_LOG_ERROR,
             "__osm_pr_rcv_get_port_pair_paths: ERR 1F21:"
             "Obtained destination LID of 0. No such LID possible\n");
     goto Exit;
  }

  if( osm_log_is_active( p_rcv->p_log, OSM_LOG_DEBUG ) )
  {
    osm_log( p_rcv->p_log, OSM_LOG_DEBUG,
             "__osm_pr_rcv_get_port_pair_paths: "
             "Src LIDs [0x%X-0x%X], "
             "Dest LIDs [0x%X-0x%X]\n",
             src_lid_min_ho, src_lid_max_ho,
             dest_lid_min_ho, dest_lid_max_ho );
  }

  src_lid_ho = src_lid_min_ho;
  dest_lid_ho = dest_lid_min_ho;

  /*
    Preferred paths come first in OpenSM
  */
  preference = 0;
  path_num = 0;

  /* If SubnAdmGet, assume NumbPaths 1 (1.2 erratum) */
  if( p_sa_mad->method != IB_MAD_METHOD_GET )
    if( comp_mask & IB_PR_COMPMASK_NUMBPATH )
      iterations = ib_path_rec_num_path( p_pr );
    else
      iterations = (uintn_t)(-1);
  else
    iterations = 1;

  while( path_num < iterations )
  {
    /*
      These paths are "fully redundant"
    */

    p_pr_item = __osm_pr_rcv_get_lid_pair_path( p_rcv, p_pr,
                                                p_src_port, p_dest_port,
                                                p_dgid,
                                                src_lid_ho, dest_lid_ho,
                                                comp_mask, preference );

    if( p_pr_item )
    {
      cl_qlist_insert_tail( p_list,
                            (cl_list_item_t*)&p_pr_item->pool_item );
      ++path_num;
    }

    if( ++src_lid_ho > src_lid_max_ho )
      break;

    if( ++dest_lid_ho > dest_lid_max_ho )
      break;
  }

  /*
    Check if we've accumulated all the paths that the user cares to see
  */
  if( path_num == iterations )
    goto Exit;

  /*
    Don't bother reporting preference 1 paths for now.
    It's more trouble than it's worth and can only occur
    if ports have different LMC values, which isn't supported
    by OpenSM right now anyway.
  */
  preference = 2;
  src_lid_ho = src_lid_min_ho;
  dest_lid_ho = dest_lid_min_ho;
  src_offset = 0;
  dest_offset = 0;

  /*
    Iterate over the remaining paths
  */
  while( path_num < iterations )
  {
    dest_offset++;
    dest_lid_ho++;

    if( dest_lid_ho > dest_lid_max_ho )
    {
      src_offset++;
      src_lid_ho++;

      if( src_lid_ho > src_lid_max_ho )
        break;    /* done */

      dest_offset = 0;
      dest_lid_ho = dest_lid_min_ho;
    }

    /*
      These paths are "fully non-redundant" with paths already
      identified above and consequently not of much value.

      Don't return paths we already identified above, as indicated
      by the offset values being equal.
    */
    if( src_offset == dest_offset )
      continue;      /* already reported */

    p_pr_item = __osm_pr_rcv_get_lid_pair_path( p_rcv, p_pr,
                                                p_src_port, p_dest_port,
                                                p_dgid,
                                                src_lid_ho, dest_lid_ho,
                                                comp_mask, preference );

    if( p_pr_item )
    {
      cl_qlist_insert_tail( p_list,
                            (cl_list_item_t*)&p_pr_item->pool_item );
      ++path_num;
    }
  }

 Exit:
  OSM_LOG_EXIT( p_rcv->p_log );
}

/**********************************************************************
 **********************************************************************/
static ib_net16_t
__osm_pr_rcv_get_end_points(
  IN osm_pr_rcv_t*         const p_rcv,
  IN const osm_madw_t*     const p_madw,
  OUT const osm_port_t**   const pp_src_port,
  OUT const osm_port_t**   const pp_dest_port,
  OUT ib_gid_t*            const p_dgid )
{
  const ib_path_rec_t*     p_pr;
  const ib_sa_mad_t*       p_sa_mad;
  ib_net64_t               comp_mask;
  ib_net64_t               dest_guid;
  ib_api_status_t          status;
  ib_net16_t               sa_status = IB_SA_MAD_STATUS_SUCCESS;
#ifdef ROUTER_EXP
  osm_router_t*            p_rtr;
  osm_port_t*              p_rtr_port;
#endif

  OSM_LOG_ENTER( p_rcv->p_log, __osm_pr_rcv_get_end_points );

  /*
    Determine what fields are valid and then get a pointer
    to the source and destination port objects, if possible.
  */

  p_sa_mad = osm_madw_get_sa_mad_ptr( p_madw );
  p_pr = (ib_path_rec_t*)ib_sa_mad_get_payload_ptr( p_sa_mad );

  comp_mask = p_sa_mad->comp_mask;

  /*
    Check a few easy disqualifying cases up front before getting
    into the endpoints.
  */

  if( comp_mask & IB_PR_COMPMASK_SGID )
  {
    if ( ! ib_gid_is_link_local( &p_pr->sgid ) )
    {
      if ( ib_gid_get_subnet_prefix( &p_pr->sgid ) != p_rcv->p_subn->opt.subnet_prefix )
      {
        /*
          This 'error' is the client's fault (bad gid) so
          don't enter it as an error in our own log.
          Return an error response to the client.
        */
        osm_log( p_rcv->p_log, OSM_LOG_VERBOSE,
                 "__osm_pr_rcv_get_end_points: "
                 "Non local SGID subnet prefix 0x%016" PRIx64 "\n",
                 cl_ntoh64( p_pr->sgid.unicast.prefix ) );

        sa_status = IB_SA_MAD_STATUS_INVALID_GID;
        goto Exit;
      }
    }

    *pp_src_port = (osm_port_t*)cl_qmap_get(
      &p_rcv->p_subn->port_guid_tbl,
      p_pr->sgid.unicast.interface_id );

    if( *pp_src_port == (osm_port_t*)cl_qmap_end(
          &p_rcv->p_subn->port_guid_tbl ) )
    {
      /*
        This 'error' is the client's fault (bad gid) so
        don't enter it as an error in our own log.
        Return an error response to the client.
      */
      osm_log( p_rcv->p_log, OSM_LOG_VERBOSE,
               "__osm_pr_rcv_get_end_points: "
               "No source port with GUID 0x%016" PRIx64 "\n",
               cl_ntoh64( p_pr->sgid.unicast.interface_id) );

      sa_status = IB_SA_MAD_STATUS_INVALID_GID;
      goto Exit;
    }
  }
  else
  {
    *pp_src_port = 0;
    if( comp_mask & IB_PR_COMPMASK_SLID )
    {
      status = cl_ptr_vector_at( &p_rcv->p_subn->port_lid_tbl,
                                 cl_ntoh16(p_pr->slid), (void**)pp_src_port );

      if( (status != CL_SUCCESS) || (*pp_src_port == NULL) )
      {
        /*
          This 'error' is the client's fault (bad lid) so
          don't enter it as an error in our own log.
          Return an error response to the client.
        */
        osm_log( p_rcv->p_log, OSM_LOG_VERBOSE,
                 "__osm_pr_rcv_get_end_points: "
                 "No source port with LID = 0x%X\n",
                 cl_ntoh16( p_pr->slid) );

        sa_status = IB_SA_MAD_STATUS_NO_RECORDS;
        goto Exit;
      }
    }
  }

  if ( p_dgid )
    memset( p_dgid, 0, sizeof(*p_dgid));

  if( comp_mask & IB_PR_COMPMASK_DGID )
  {
    dest_guid = p_pr->dgid.unicast.interface_id;
    if ( ! ib_gid_is_link_local( &p_pr->dgid ) )
    {
      if ( ! ib_gid_is_multicast( &p_pr->dgid ) &&
             ib_gid_get_subnet_prefix( &p_pr->dgid ) != p_rcv->p_subn->opt.subnet_prefix )
      {
        osm_log( p_rcv->p_log, OSM_LOG_VERBOSE,
                 "__osm_pr_rcv_get_end_points: "
                 "Non local DGID subnet prefix 0x%016" PRIx64 "\n",
                 cl_ntoh64( p_pr->dgid.unicast.prefix ) );
#ifndef ROUTER_EXP
        /*
          This 'error' is the client's fault (bad gid) so
          don't enter it as an error in our own log.
          Return an error response to the client.
        */
        sa_status = IB_SA_MAD_STATUS_INVALID_GID;
        goto Exit;
#else
        /* Just use "first" router (if it exists) for now */
        p_rtr = (osm_router_t*)cl_qmap_head( &p_rcv->p_subn->rtr_guid_tbl );
        if ( p_rtr == (osm_router_t*)cl_qmap_end( &p_rcv->p_subn->rtr_guid_tbl ) )
        {
          osm_log( p_rcv->p_log, OSM_LOG_ERROR,
                   "__osm_pr_rcv_get_end_points: ERR 1F22: "
                   "Off subnet DGID but no routers found\n" );
          sa_status = IB_SA_MAD_STATUS_INVALID_GID;
          goto Exit;
        }

        p_rtr_port = osm_router_get_port_ptr( p_rtr );
        dest_guid = osm_port_get_guid( p_rtr_port ); 
        if ( p_dgid )
          *p_dgid = p_pr->dgid;
#endif
      }
    }

    *pp_dest_port = (osm_port_t*)cl_qmap_get(
      &p_rcv->p_subn->port_guid_tbl,
      dest_guid );

    if( *pp_dest_port == (osm_port_t*)cl_qmap_end(
          &p_rcv->p_subn->port_guid_tbl ) )
    {
      /*
        This 'error' is the client's fault (bad gid) so
        don't enter it as an error in our own log.
        Return an error response to the client.
      */
      osm_log( p_rcv->p_log, OSM_LOG_VERBOSE,
               "__osm_pr_rcv_get_end_points: "
               "No dest port with GUID 0x%016" PRIx64 "\n",
               cl_ntoh64( dest_guid ) );

      sa_status = IB_SA_MAD_STATUS_INVALID_GID;
      goto Exit;
    }
  }
  else
  {
    *pp_dest_port = 0;
    if( comp_mask & IB_PR_COMPMASK_DLID )
    {
      status = cl_ptr_vector_at( &p_rcv->p_subn->port_lid_tbl,
                                 cl_ntoh16(p_pr->dlid),  (void**)pp_dest_port );

      if( (status != CL_SUCCESS) || (*pp_dest_port == NULL) )
      {
        /*
          This 'error' is the client's fault (bad lid) so
          don't enter it as an error in our own log.
          Return an error response to the client.
        */
        osm_log( p_rcv->p_log, OSM_LOG_VERBOSE,
                 "__osm_pr_rcv_get_end_points: "
                 "No dest port with LID = 0x%X\n",
                 cl_ntoh16( p_pr->dlid) );

        sa_status = IB_SA_MAD_STATUS_NO_RECORDS;
        goto Exit;
      }
    }
  }

 Exit:
  OSM_LOG_EXIT( p_rcv->p_log );
  return( sa_status );
}

/**********************************************************************
 **********************************************************************/
static void
__osm_pr_rcv_process_world(
  IN osm_pr_rcv_t*         const p_rcv,
  IN const osm_madw_t*     const p_madw,
  IN const osm_port_t*     const requester_port,
  IN const ib_gid_t*       const p_dgid,
  IN const ib_net64_t      comp_mask,
  IN cl_qlist_t*           const p_list )
{
  const cl_qmap_t*         p_tbl;
  const osm_port_t*        p_dest_port;
  const osm_port_t*        p_src_port;

  OSM_LOG_ENTER( p_rcv->p_log, __osm_pr_rcv_process_world );

  /*
    Iterate the entire port space over itself.
    A path record from a port to itself is legit, so no
    need for a special case there.

    We compute both A -> B and B -> A, since we don't have
    any check to determine the reversability of the paths.
  */
  p_tbl = &p_rcv->p_subn->port_guid_tbl;

  p_dest_port = (osm_port_t*)cl_qmap_head( p_tbl );
  while( p_dest_port != (osm_port_t*)cl_qmap_end( p_tbl ) )
  {
    p_src_port = (osm_port_t*)cl_qmap_head( p_tbl );
    while( p_src_port != (osm_port_t*)cl_qmap_end( p_tbl ) )
    {
      __osm_pr_rcv_get_port_pair_paths( p_rcv, p_madw, requester_port, p_src_port,
                                        p_dest_port, p_dgid, comp_mask, p_list );

      p_src_port = (osm_port_t*)cl_qmap_next( &p_src_port->map_item );
    }

    p_dest_port = (osm_port_t*)cl_qmap_next( &p_dest_port->map_item );
  }

  OSM_LOG_EXIT( p_rcv->p_log );
}

/**********************************************************************
 **********************************************************************/
static void
__osm_pr_rcv_process_half(
  IN osm_pr_rcv_t*         const p_rcv,
  IN const osm_madw_t*     const p_madw,
  IN const osm_port_t*     const requester_port,
  IN const osm_port_t*     const p_src_port,
  IN const osm_port_t*     const p_dest_port,
  IN const ib_gid_t*       const p_dgid,
  IN const ib_net64_t      comp_mask,
  IN cl_qlist_t*           const p_list )
{
  const cl_qmap_t*         p_tbl;
  const osm_port_t*        p_port;

  OSM_LOG_ENTER( p_rcv->p_log, __osm_pr_rcv_process_half );

  /*
    Iterate over every port, looking for matches...
    A path record from a port to itself is legit, so no
    need to special case that one.
  */
  p_tbl = &p_rcv->p_subn->port_guid_tbl;

  if( p_src_port )
  {
    /*
      The src port if fixed, so iterate over destination ports.
    */
    p_port = (osm_port_t*)cl_qmap_head( p_tbl );
    while( p_port != (osm_port_t*)cl_qmap_end( p_tbl ) )
    {
      __osm_pr_rcv_get_port_pair_paths( p_rcv, p_madw, requester_port,
                                        p_src_port, p_port, p_dgid,
                                        comp_mask, p_list );
      p_port = (osm_port_t*)cl_qmap_next( &p_port->map_item );
    }
  }
  else
  {
    /*
      The dest port if fixed, so iterate over source ports.
    */
    p_port = (osm_port_t*)cl_qmap_head( p_tbl );
    while( p_port != (osm_port_t*)cl_qmap_end( p_tbl ) )
    {
      __osm_pr_rcv_get_port_pair_paths( p_rcv, p_madw, requester_port,
                                        p_port, p_dest_port, p_dgid,
                                        comp_mask, p_list );
      p_port = (osm_port_t*)cl_qmap_next( &p_port->map_item );
    }
  }

  OSM_LOG_EXIT( p_rcv->p_log );
}

/**********************************************************************
 **********************************************************************/
static void
__osm_pr_rcv_process_pair(
  IN osm_pr_rcv_t*         const p_rcv,
  IN const osm_madw_t*     const p_madw,
  IN const osm_port_t*     const requester_port,
  IN const osm_port_t*     const p_src_port,
  IN const osm_port_t*     const p_dest_port,
  IN const ib_gid_t*       const p_dgid,
  IN const ib_net64_t      comp_mask,
  IN cl_qlist_t*           const p_list )
{
  OSM_LOG_ENTER( p_rcv->p_log, __osm_pr_rcv_process_pair );

  __osm_pr_rcv_get_port_pair_paths( p_rcv, p_madw, requester_port, p_src_port,
                                    p_dest_port, p_dgid, comp_mask, p_list );

  OSM_LOG_EXIT( p_rcv->p_log );
}

/**********************************************************************
 *********************************************************************/
static void
__search_mgrp_by_mgid(
  IN  cl_map_item_t* const      p_map_item,
  IN  void*                     context )
{
  osm_mgrp_t*                   p_mgrp = (osm_mgrp_t*)p_map_item;
  osm_sa_pr_mcmr_search_ctxt_t *p_ctxt = (osm_sa_pr_mcmr_search_ctxt_t *) context;
  const ib_gid_t               *p_recvd_mgid;
  osm_pr_rcv_t                 *p_rcv;
  /* uint32_t i; */

  p_recvd_mgid = p_ctxt->p_mgid;
  p_rcv = p_ctxt->p_rcv;

  /* ignore groups marked for deletion */
  if ( p_mgrp->to_be_deleted )
    return;

  /* compare entire MGID so different scope will not sneak in for
     the same MGID */
  if ( memcmp( &p_mgrp->mcmember_rec.mgid,
                p_recvd_mgid,
                sizeof(ib_gid_t) ) )
    return;

#if 0
  for ( i = 0 ; i < sizeof(p_mgrp->mcmember_rec.mgid.multicast.raw_group_id); i++)
  {
    if ( p_mgrp->mcmember_rec.mgid.multicast.raw_group_id[i] !=
         p_recvd_mgid->mgid.multicast.raw_group_id[i] )
      return;
  }
#endif

  if( p_ctxt->p_mgrp )
  {
    osm_log( p_rcv->p_log, OSM_LOG_ERROR,
             "__search_mgrp_by_mgid: ERR 1F08: "
             "Multiple MC groups for same MGID\n" );
    return;
  }
  p_ctxt->p_mgrp = p_mgrp;
}

/**********************************************************************
 **********************************************************************/
static ib_api_status_t
__get_mgrp_by_mgid(
  IN osm_pr_rcv_t*             const p_rcv,
  IN ib_path_rec_t*            p_recvd_path_rec,
  OUT osm_mgrp_t **            pp_mgrp )
{
  osm_sa_pr_mcmr_search_ctxt_t mcmr_search_context;

  mcmr_search_context.p_mgid = &p_recvd_path_rec->dgid;
  mcmr_search_context.p_rcv = p_rcv;
  mcmr_search_context.p_mgrp = NULL;

  cl_qmap_apply_func( &p_rcv->p_subn->mgrp_mlid_tbl,
                      __search_mgrp_by_mgid,
                      &mcmr_search_context);

  if( mcmr_search_context.p_mgrp == NULL )
  {
    return IB_NOT_FOUND;
  }

  *pp_mgrp = mcmr_search_context.p_mgrp;
  return IB_SUCCESS;
}

/**********************************************************************
 **********************************************************************/
static osm_mgrp_t *
__get_mgrp_by_mlid(
  IN const osm_pr_rcv_t* const p_rcv,
  IN ib_net16_t          const mlid )
{
  cl_map_item_t *        map_item;

  map_item = cl_qmap_get( &p_rcv->p_subn->mgrp_mlid_tbl, mlid );

  if( map_item == cl_qmap_end(&p_rcv->p_subn->mgrp_mlid_tbl) )
  {
    return NULL;
  }

  return (osm_mgrp_t *)map_item;
}

/**********************************************************************
 **********************************************************************/
static void
__osm_pr_get_mgrp(
  IN osm_pr_rcv_t*         const p_rcv,
  IN const osm_madw_t*     const p_madw,
  OUT osm_mgrp_t           **pp_mgrp )
{
  ib_path_rec_t*           p_pr;
  const ib_sa_mad_t*       p_sa_mad;
  ib_net64_t               comp_mask;
  ib_api_status_t          status;

  OSM_LOG_ENTER( p_rcv->p_log, __osm_pr_get_mgrp );

  p_sa_mad = osm_madw_get_sa_mad_ptr( p_madw );
  p_pr = (ib_path_rec_t*)ib_sa_mad_get_payload_ptr( p_sa_mad );

  comp_mask = p_sa_mad->comp_mask;

  if( comp_mask & IB_PR_COMPMASK_DGID )
  {
    status = __get_mgrp_by_mgid( p_rcv, p_pr, pp_mgrp );
    if( status != IB_SUCCESS )
    {
      osm_log( p_rcv->p_log, OSM_LOG_ERROR,
               "__osm_pr_get_mgrp: ERR 1F09: "
               "No MC group found for PathRecord destination GID\n" );
      goto Exit;
    }
  }

  if( comp_mask & IB_PR_COMPMASK_DLID )
  {
    if( *pp_mgrp)
    {
      /* check that the MLID in the MC group is */
      /* the same as the DLID in the PathRecord */
      if( (*pp_mgrp)->mlid != p_pr->dlid )
      {
	/* Note: perhaps this might be better indicated as an invalid request */
        osm_log( p_rcv->p_log, OSM_LOG_ERROR,
                 "__osm_pr_get_mgrp: ERR 1F10: "
                 "MC group MLID does not match PathRecord destination LID\n" );
        *pp_mgrp = NULL;
        goto Exit;
      }
    }
    else
    {
      *pp_mgrp = __get_mgrp_by_mlid( p_rcv, p_pr->dlid );
      if( *pp_mgrp == NULL)
      {
        osm_log( p_rcv->p_log, OSM_LOG_ERROR,
                 "__osm_pr_get_mgrp: ERR 1F11: "
                 "No MC group found for PathRecord destination LID\n" );
      }
    }
  }

 Exit:
  OSM_LOG_EXIT( p_rcv->p_log );
}

/**********************************************************************
 **********************************************************************/
static ib_api_status_t
__osm_pr_match_mgrp_attributes(
  IN osm_pr_rcv_t*         const p_rcv,
  IN const osm_madw_t*     const p_madw,
  IN const osm_mgrp_t*     const p_mgrp )
{
  const ib_path_rec_t*     p_pr;
  const ib_sa_mad_t*       p_sa_mad;
  ib_net64_t               comp_mask;
  ib_api_status_t          status = IB_ERROR;
  uint32_t                 flow_label;
  uint8_t                  sl;
  uint8_t                  hop_limit;

  OSM_LOG_ENTER( p_rcv->p_log, __osm_pr_match_mgrp_attributes );

  p_sa_mad = osm_madw_get_sa_mad_ptr( p_madw );
  p_pr = (ib_path_rec_t*)ib_sa_mad_get_payload_ptr( p_sa_mad );

  comp_mask = p_sa_mad->comp_mask;

  /* If SGID and/or SLID specified, should validate as member of MC group */
  /* Also, MTU, rate, packet lifetime, and raw traffic requested are not currently checked */
  if( comp_mask & IB_PR_COMPMASK_PKEY )
  {
    if( p_pr->pkey != p_mgrp->mcmember_rec.pkey )
      goto Exit;
  }

  ib_member_get_sl_flow_hop( p_mgrp->mcmember_rec.sl_flow_hop,
                             &sl, &flow_label, &hop_limit );

  if( comp_mask & IB_PR_COMPMASK_SL )
  {
    if( ib_path_rec_sl( p_pr ) != sl )
      goto Exit;
  }

  /* If SubnAdmGet, assume NumbPaths of 1 (1.2 erratum) */
  if( ( comp_mask & IB_PR_COMPMASK_NUMBPATH ) &&
      ( p_sa_mad->method != IB_MAD_METHOD_GET ) )
  {
    if( ib_path_rec_num_path( p_pr ) == 0 )
      goto Exit;
  }

  if( comp_mask & IB_PR_COMPMASK_FLOWLABEL )
  {
    if( ib_path_rec_flow_lbl( p_pr ) != flow_label )
      goto Exit;
  }

  if( comp_mask & IB_PR_COMPMASK_HOPLIMIT )
  {
    if( ib_path_rec_hop_limit( p_pr ) != hop_limit )
      goto Exit;
  }

  if( comp_mask & IB_PR_COMPMASK_TCLASS )
  {
    if( p_pr->tclass != p_mgrp->mcmember_rec.tclass )
      goto Exit;
  }

  status = IB_SUCCESS;

 Exit:
  OSM_LOG_EXIT( p_rcv->p_log );
  return( status );
}

/**********************************************************************
 **********************************************************************/
static int
__osm_pr_rcv_check_mcast_dest(
  IN osm_pr_rcv_t*         const p_rcv,
  IN const osm_madw_t*     const p_madw )
{
  const ib_path_rec_t*     p_pr;
  const ib_sa_mad_t*       p_sa_mad;
  ib_net64_t               comp_mask;
  int                      is_multicast = 0;

  OSM_LOG_ENTER( p_rcv->p_log, __osm_pr_rcv_check_mcast_dest );

  p_sa_mad = osm_madw_get_sa_mad_ptr( p_madw );
  p_pr = (ib_path_rec_t*)ib_sa_mad_get_payload_ptr( p_sa_mad );

  comp_mask = p_sa_mad->comp_mask;

  if( comp_mask & IB_PR_COMPMASK_DGID )
  {
    is_multicast = ib_gid_is_multicast( &p_pr->dgid );
    if( !is_multicast )
      goto Exit;
  }

  if( comp_mask & IB_PR_COMPMASK_DLID )
  {
    if( cl_ntoh16( p_pr->dlid ) >= IB_LID_MCAST_START_HO &&
        cl_ntoh16( p_pr->dlid ) <= IB_LID_MCAST_END_HO )
      is_multicast = 1;
    else if( is_multicast )
    {
      osm_log( p_rcv->p_log, OSM_LOG_ERROR,
               "__osm_pr_rcv_check_mcast_dest: ERR 1F12: "
               "PathRecord request indicates MGID but not MLID\n" );
      is_multicast = -1;
    }
  }

 Exit:
  OSM_LOG_EXIT( p_rcv->p_log );
  return( is_multicast );
}

/**********************************************************************
 **********************************************************************/
static void
__osm_pr_rcv_respond(
  IN osm_pr_rcv_t*         const p_rcv,
  IN const osm_madw_t*     const p_madw,
  IN cl_qlist_t*           const p_list )
{
  osm_madw_t*              p_resp_madw;
  const ib_sa_mad_t*       p_sa_mad;
  ib_sa_mad_t*             p_resp_sa_mad;
  size_t                   num_rec, pre_trim_num_rec;
#ifndef VENDOR_RMPP_SUPPORT
  size_t		   trim_num_rec;
#endif
  ib_path_rec_t*           p_resp_pr;
  ib_api_status_t          status;
  const ib_sa_mad_t*       p_rcvd_mad = osm_madw_get_sa_mad_ptr( p_madw );
  osm_pr_item_t*           p_pr_item;
  uint32_t                 i;

  OSM_LOG_ENTER( p_rcv->p_log, __osm_pr_rcv_respond );

  num_rec = cl_qlist_count( p_list );

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
               "__osm_pr_rcv_respond: ERR 1F13: "
               "Got more than one record for SubnAdmGet (%zu)\n",
               num_rec );
      osm_sa_send_error( p_rcv->p_resp, p_madw,
                         IB_SA_MAD_STATUS_TOO_MANY_RECORDS );
      /* need to set the mem free ... */
      p_pr_item = (osm_pr_item_t*)cl_qlist_remove_head( p_list );
      while( p_pr_item != (osm_pr_item_t*)cl_qlist_end( p_list ) )
      {
        cl_qlock_pool_put( &p_rcv->pr_pool, &p_pr_item->pool_item );
        p_pr_item = (osm_pr_item_t*)cl_qlist_remove_head( p_list );
      }
      goto Exit;
    }
  }

  pre_trim_num_rec = num_rec;
#ifndef VENDOR_RMPP_SUPPORT
  trim_num_rec = (MAD_BLOCK_SIZE - IB_SA_MAD_HDR_SIZE) / sizeof(ib_path_rec_t);
  if (trim_num_rec < num_rec)
  {
    osm_log( p_rcv->p_log, OSM_LOG_VERBOSE,
             "__osm_pr_rcv_respond: "
             "Number of records:%u trimmed to:%u to fit in one MAD\n",
             num_rec,trim_num_rec );
    num_rec = trim_num_rec;
  }
#endif

  osm_log( p_rcv->p_log, OSM_LOG_DEBUG,
           "__osm_pr_rcv_respond: "
           "Generating response with %zu records\n", num_rec );

  if ((p_rcvd_mad->method == IB_MAD_METHOD_GET) && (num_rec == 0))
  {
    osm_sa_send_error( p_rcv->p_resp, p_madw, IB_SA_MAD_STATUS_NO_RECORDS );
    goto Exit;
  }

  /*
   * Get a MAD to reply. Address of Mad is in the received mad_wrapper
   */
  p_resp_madw = osm_mad_pool_get( p_rcv->p_mad_pool, p_madw->h_bind,
                                  num_rec * sizeof(ib_path_rec_t) + IB_SA_MAD_HDR_SIZE,
                                  &p_madw->mad_addr );
  if( !p_resp_madw )
  {
    osm_log( p_rcv->p_log, OSM_LOG_ERROR,
             "__osm_pr_rcv_respond: ERR 1F14: "
             "Unable to allocate MAD\n" );

    for( i = 0; i < num_rec; i++ )
    {
      p_pr_item = (osm_pr_item_t*)cl_qlist_remove_head( p_list );
      cl_qlock_pool_put( &p_rcv->pr_pool, &p_pr_item->pool_item );
    }

    osm_sa_send_error( p_rcv->p_resp, p_madw, IB_SA_MAD_STATUS_NO_RESOURCES );
    goto Exit;
  }

  p_sa_mad = osm_madw_get_sa_mad_ptr( p_madw );
  p_resp_sa_mad = osm_madw_get_sa_mad_ptr( p_resp_madw );

  memcpy( p_resp_sa_mad, p_sa_mad, IB_SA_MAD_HDR_SIZE );
  p_resp_sa_mad->method |= IB_MAD_METHOD_RESP_MASK;
  /* C15-0.1.5 - always return SM_Key = 0 (table 185 p 884) */
  p_resp_sa_mad->sm_key = 0;
  /* Fill in the offset (paylen will be done by the rmpp SAR) */
  p_resp_sa_mad->attr_offset = ib_get_attr_offset( sizeof(ib_path_rec_t) );

  p_resp_pr = (ib_path_rec_t*)ib_sa_mad_get_payload_ptr( p_resp_sa_mad );

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

  for ( i = 0; i < pre_trim_num_rec; i++ )
  {
    p_pr_item = (osm_pr_item_t*)cl_qlist_remove_head( p_list );
    /* copy only if not trimmed */
    if (i < num_rec)
        *p_resp_pr = p_pr_item->path_rec;

    cl_qlock_pool_put( &p_rcv->pr_pool, &p_pr_item->pool_item );
    p_resp_pr++;
  }

  CL_ASSERT( cl_is_qlist_empty( p_list ) );

  status = osm_vendor_send( p_resp_madw->h_bind, p_resp_madw, FALSE );

  if( status != IB_SUCCESS )
  {
    osm_log( p_rcv->p_log, OSM_LOG_ERROR,
             "__osm_pr_rcv_respond: ERR 1F15: "
             "Unable to send MAD (%s)\n", ib_get_err_str( status ) );
    /*  osm_mad_pool_put( p_rcv->p_mad_pool, p_resp_madw ); */
  }

 Exit:
  OSM_LOG_EXIT( p_rcv->p_log );
}

/**********************************************************************
 **********************************************************************/
void
osm_pr_rcv_process(
  IN void *context,
  IN void *data )
{
  osm_pr_rcv_t *p_rcv = context;
  osm_madw_t *p_madw = data;
  const ib_path_rec_t*     p_pr;
  const ib_sa_mad_t*       p_sa_mad;
  const osm_port_t*        p_src_port;
  const osm_port_t*        p_dest_port;
  cl_qlist_t               pr_list;
  ib_gid_t                 dgid;
  ib_net16_t               sa_status;
  osm_port_t*              requester_port;
  int ret;

  OSM_LOG_ENTER( p_rcv->p_log, osm_pr_rcv_process );

  CL_ASSERT( p_madw );

  p_sa_mad = osm_madw_get_sa_mad_ptr( p_madw );
  p_pr = (ib_path_rec_t*)ib_sa_mad_get_payload_ptr( p_sa_mad );

  CL_ASSERT( p_sa_mad->attr_id == IB_MAD_ATTR_PATH_RECORD );

  /* we only support SubnAdmGet and SubnAdmGetTable methods */
  if ((p_sa_mad->method != IB_MAD_METHOD_GET) &&
      (p_sa_mad->method != IB_MAD_METHOD_GETTABLE)) {
    osm_log( p_rcv->p_log, OSM_LOG_ERROR,
             "osm_pr_rcv_process: ERR 1F17: " 
             "Unsupported Method (%s)\n",
             ib_get_sa_method_str( p_sa_mad->method ) );
    osm_sa_send_error( p_rcv->p_resp, p_madw, IB_MAD_STATUS_UNSUP_METHOD_ATTR );
    goto Exit;
  }

  /* update the requester physical port. */
  requester_port = osm_get_port_by_mad_addr( p_rcv->p_log, p_rcv->p_subn,
                                             osm_madw_get_mad_addr_ptr( p_madw ) );
  if( requester_port == NULL )
  {
    osm_log( p_rcv->p_log, OSM_LOG_ERROR,
             "osm_pr_rcv_process: ERR 1F16: "
             "Cannot find requester physical port\n" );
    goto Exit;
  }

  if( osm_log_is_active( p_rcv->p_log, OSM_LOG_DEBUG ) )
    osm_dump_path_record( p_rcv->p_log, p_pr, OSM_LOG_DEBUG );

  cl_qlist_init( &pr_list );

  /*
    Most SA functions (including this one) are read-only on the
    subnet object, so we grab the lock non-exclusively.
  */
  cl_plock_acquire( p_rcv->p_lock );

  /* Handle multicast destinations separately */
  if( (ret = __osm_pr_rcv_check_mcast_dest( p_rcv, p_madw )) < 0 )
  {
    /* Multicast DGID with unicast DLID */
    cl_plock_release( p_rcv->p_lock );
    osm_sa_send_error( p_rcv->p_resp, p_madw, IB_MAD_STATUS_INVALID_FIELD );
    goto Exit;
  }

  if(ret > 0)
    goto McastDest;

  osm_log( p_rcv->p_log, OSM_LOG_DEBUG,
           "osm_pr_rcv_process: "
           "Unicast destination requested\n" );

  sa_status = __osm_pr_rcv_get_end_points( p_rcv, p_madw,
                                           &p_src_port, &p_dest_port,
                                           &dgid );

  if( sa_status == IB_SA_MAD_STATUS_SUCCESS )
  {
    /*
      What happens next depends on the type of endpoint information
      that was specified....
    */
    if( p_src_port )
    {
      if( p_dest_port )
        __osm_pr_rcv_process_pair( p_rcv, p_madw, requester_port,
                                   p_src_port, p_dest_port, &dgid,
                                   p_sa_mad->comp_mask, &pr_list );
      else
        __osm_pr_rcv_process_half( p_rcv, p_madw, requester_port,
                                   p_src_port, NULL, &dgid,
                                   p_sa_mad->comp_mask, &pr_list );
    }
    else
    {
      if( p_dest_port )
        __osm_pr_rcv_process_half( p_rcv, p_madw, requester_port,
                                   NULL, p_dest_port, &dgid,
                                   p_sa_mad->comp_mask, &pr_list );
      else
        /*
          Katie, bar the door!
        */
        __osm_pr_rcv_process_world( p_rcv, p_madw, requester_port,
                                    &dgid, p_sa_mad->comp_mask, &pr_list );
    }
  }
  goto Unlock;

 McastDest:
  osm_log(p_rcv->p_log, OSM_LOG_DEBUG,
             "osm_pr_rcv_process: "
             "Multicast destination requested\n" );
  {
    osm_mgrp_t *p_mgrp = NULL;
    ib_api_status_t status;
    osm_pr_item_t* p_pr_item;
    uint32_t flow_label;
    uint8_t  sl;
    uint8_t  hop_limit;

    /* First, get the MC info */
    __osm_pr_get_mgrp( p_rcv, p_madw, &p_mgrp );

    if ( p_mgrp )
    {
      /* Make sure the rest of the PathRecord matches the MC group attributes */
      status = __osm_pr_match_mgrp_attributes( p_rcv, p_madw, p_mgrp );
      if ( status == IB_SUCCESS )
      {
        p_pr_item = (osm_pr_item_t*)cl_qlock_pool_get( &p_rcv->pr_pool );
        if( p_pr_item == NULL )
        {
          osm_log( p_rcv->p_log, OSM_LOG_ERROR,
                   "osm_pr_rcv_process: ERR 1F18: "
                   "Unable to allocate path record for MC group\n" );
        }
        else
        {
	  /* Copy PathRecord request into response */
          p_sa_mad = osm_madw_get_sa_mad_ptr( p_madw );
          p_pr = (ib_path_rec_t*)ib_sa_mad_get_payload_ptr( p_sa_mad );
          p_pr_item->path_rec = *p_pr;

          /* Now, use the MC info to cruft up the PathRecord response */        
          p_pr_item->path_rec.dgid = p_mgrp->mcmember_rec.mgid;
          p_pr_item->path_rec.dlid = p_mgrp->mcmember_rec.mlid;
	  p_pr_item->path_rec.tclass = p_mgrp->mcmember_rec.tclass;
	  p_pr_item->path_rec.num_path = 1;
	  p_pr_item->path_rec.pkey = p_mgrp->mcmember_rec.pkey;

	  /* MTU, rate, and packet lifetime should be exactly */
	  p_pr_item->path_rec.mtu = (2<<6) | p_mgrp->mcmember_rec.mtu;
          p_pr_item->path_rec.rate = (2<<6) | p_mgrp->mcmember_rec.rate;
          p_pr_item->path_rec.pkt_life = (2<<6) | p_mgrp->mcmember_rec.pkt_life;

	  /* SL, Hop Limit, and Flow Label */
          ib_member_get_sl_flow_hop( p_mgrp->mcmember_rec.sl_flow_hop,
                                     &sl, &flow_label, &hop_limit );
	  p_pr_item->path_rec.sl = cl_hton16( sl );
#ifndef ROUTER_EXP
          p_pr_item->path_rec.hop_flow_raw = cl_hton32(hop_limit) |
                                             (flow_label << 8);
#else
          /* HopLimit is not yet set in non link local MC groups */
          /* If it were, this would not be needed */
	  if ( ib_mgid_get_scope( &p_mgrp->mcmember_rec.mgid ) == MC_SCOPE_LINK_LOCAL )
            p_pr_item->path_rec.hop_flow_raw = cl_hton32(hop_limit) |
                                               (flow_label << 8);
          else
            p_pr_item->path_rec.hop_flow_raw = cl_hton32(IB_HOPLIMIT_MAX) |
                                               (flow_label << 8);
#endif

          cl_qlist_insert_tail( &pr_list,
                                (cl_list_item_t*)&p_pr_item->pool_item );

        }
      }
      else
      {
        osm_log( p_rcv->p_log, OSM_LOG_ERROR,
                 "osm_pr_rcv_process: ERR 1F19: "
                 "MC group attributes don't match PathRecord request\n" );
      }
    }
  }

 Unlock:
  cl_plock_release( p_rcv->p_lock );

  /* Now, (finally) respond to the PathRecord request */
  __osm_pr_rcv_respond( p_rcv, p_madw, &pr_list );

 Exit:
  OSM_LOG_EXIT( p_rcv->p_log );
}

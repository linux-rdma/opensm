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
 *    Implementation of osm_drop_mgr_t.
 * This object represents the Drop Manager object.
 * This object is part of the opensm family of objects.
 *
 * Environment:
 *    Linux User Mode
 *
 * $Revision: 1.7 $
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <stdlib.h>
#include <string.h>
#include <iba/ib_types.h>
#include <complib/cl_qmap.h>
#include <complib/cl_passivelock.h>
#include <complib/cl_debug.h>
#include <complib/cl_ptr_vector.h>
#include <opensm/osm_drop_mgr.h>
#include <opensm/osm_router.h>
#include <opensm/osm_switch.h>
#include <opensm/osm_node.h>
#include <opensm/osm_helper.h>
#include <opensm/osm_mcm_info.h>
#include <opensm/osm_multicast.h>
#include <opensm/osm_remote_sm.h>
#include <opensm/osm_inform.h>

/**********************************************************************
 **********************************************************************/
void
osm_drop_mgr_construct(
  IN osm_drop_mgr_t* const p_mgr )
{
  CL_ASSERT( p_mgr );
  memset( p_mgr, 0, sizeof(*p_mgr) );
}

/**********************************************************************
 **********************************************************************/
void
osm_drop_mgr_destroy(
  IN osm_drop_mgr_t* const p_mgr )
{
  CL_ASSERT( p_mgr );

  OSM_LOG_ENTER( p_mgr->p_log, osm_drop_mgr_destroy );

  OSM_LOG_EXIT( p_mgr->p_log );
}

/**********************************************************************
 **********************************************************************/
ib_api_status_t
osm_drop_mgr_init(
  IN osm_drop_mgr_t* const p_mgr,
  IN osm_subn_t* const p_subn,
  IN osm_log_t* const p_log,
  IN osm_req_t* const p_req,
  IN cl_plock_t* const p_lock )
{
  ib_api_status_t status = IB_SUCCESS;

  OSM_LOG_ENTER( p_log, osm_drop_mgr_init );

  osm_drop_mgr_construct( p_mgr );

  p_mgr->p_log = p_log;
  p_mgr->p_subn = p_subn;
  p_mgr->p_lock = p_lock;
  p_mgr->p_req = p_req;

  OSM_LOG_EXIT( p_mgr->p_log );
  return( status );
}

/**********************************************************************
 **********************************************************************/
static void
__osm_drop_mgr_remove_router(
  IN const osm_drop_mgr_t* const p_mgr,
  IN const ib_net64_t portguid )
{
  osm_router_t *p_rtr;
  cl_qmap_t* p_rtr_guid_tbl;

  p_rtr_guid_tbl = &p_mgr->p_subn->rtr_guid_tbl;
  p_rtr = (osm_router_t*)cl_qmap_remove( p_rtr_guid_tbl, portguid );
  if( p_rtr != (osm_router_t*)cl_qmap_end( p_rtr_guid_tbl ) )
  {
    osm_log( p_mgr->p_log, OSM_LOG_VERBOSE,
             "__osm_drop_mgr_remove_router: "
             "Cleaned router for port guid 0x%016" PRIx64 "\n",
             cl_ntoh64( portguid ) );
    osm_router_delete( &p_rtr );
  }
}


/**********************************************************************
 **********************************************************************/
static void
drop_mgr_clean_physp(
  IN const osm_drop_mgr_t* const p_mgr,
  IN osm_physp_t *p_physp)
{
  cl_qmap_t *p_port_guid_tbl = &p_mgr->p_subn->port_guid_tbl;
  osm_physp_t *p_remote_physp;
  osm_port_t* p_remote_port;

  p_remote_physp = osm_physp_get_remote( p_physp );
  if( p_remote_physp && osm_physp_is_valid( p_remote_physp ) )
  {
    p_remote_port = (osm_port_t*)cl_qmap_get( p_port_guid_tbl,
                                              p_remote_physp->port_guid );

    if ( p_remote_port != (osm_port_t*)cl_qmap_end( p_port_guid_tbl ) )
    {
      /* Let's check if this is a case of link that is lost (both ports
         weren't recognized), or a "hiccup" in the subnet - in which case
         the remote port was recognized, and its state is ACTIVE.
         If this is just a "hiccup" - force a heavy sweep in the next sweep.
         We don't want to lose that part of the subnet. */
      if (osm_port_discovery_count_get( p_remote_port ) &&
          osm_physp_get_port_state( p_remote_physp ) == IB_LINK_ACTIVE )
      {
        osm_log( p_mgr->p_log, OSM_LOG_VERBOSE,
                 "drop_mgr_clean_physp: "
                 "Forcing delayed heavy sweep. Remote "
                 "port 0x%016" PRIx64 " port num: 0x%X "
                 "was recognized in ACTIVE state\n",
                 cl_ntoh64( p_remote_physp->port_guid ),
                 p_remote_physp->port_num );
        p_mgr->p_subn->force_delayed_heavy_sweep = TRUE;
      }

      /* If the remote node is ca or router - need to remove the remote port,
         since it is no longer reachable. This can be done if we reset the
         discovery count of the remote port. */
      if ( !p_remote_physp->p_node->sw )
      {
        osm_port_discovery_count_reset( p_remote_port );
        osm_log( p_mgr->p_log, OSM_LOG_DEBUG,
                 "drop_mgr_clean_physp: Resetting discovery count of node: "
                 "0x%016" PRIx64 " port num:0x%X\n",
                 cl_ntoh64( osm_node_get_node_guid( p_remote_physp->p_node ) ),
                 p_remote_physp->port_num );
      }
    }

    osm_log( p_mgr->p_log, OSM_LOG_VERBOSE,
             "drop_mgr_clean_physp: "
             "Unlinking local node 0x%016" PRIx64 ", port 0x%X"
             "\n\t\t\t\tand remote node 0x%016" PRIx64 ", port 0x%X\n",
             cl_ntoh64( osm_node_get_node_guid( p_physp->p_node ) ),
             p_physp->port_num,
             cl_ntoh64( osm_node_get_node_guid( p_remote_physp->p_node ) ),
             p_remote_physp->port_num );

    osm_physp_unlink( p_physp, p_remote_physp );

  }

  osm_log( p_mgr->p_log, OSM_LOG_DEBUG,
           "drop_mgr_clean_physp: Clearing physical port number 0x%X\n",
           p_physp->port_num );

  osm_physp_destroy( p_physp );
}

/**********************************************************************
 **********************************************************************/
static void
__osm_drop_mgr_remove_port(
  IN const osm_drop_mgr_t* const p_mgr,
  IN osm_port_t* p_port )
{
  ib_net64_t port_guid;
  osm_port_t *p_port_check;
  cl_list_t* p_new_ports_list;
  cl_list_iterator_t cl_list_item;
  cl_qmap_t* p_port_guid_tbl;
  cl_qmap_t* p_sm_guid_tbl;
  osm_mcm_info_t* p_mcm;
  osm_mgrp_t*  p_mgrp;
  cl_ptr_vector_t* p_port_lid_tbl;
  uint16_t min_lid_ho;
  uint16_t max_lid_ho;
  uint16_t lid_ho;
  osm_node_t *p_node;
  osm_remote_sm_t *p_sm;
  ib_gid_t port_gid;
  ib_mad_notice_attr_t notice;
  ib_api_status_t status;

  OSM_LOG_ENTER( p_mgr->p_log, __osm_drop_mgr_remove_port );

  port_guid = osm_port_get_guid( p_port );
  osm_log( p_mgr->p_log, OSM_LOG_VERBOSE,
           "__osm_drop_mgr_remove_port: "
           "Unreachable port 0x%016" PRIx64 "\n",
           cl_ntoh64( port_guid ) );

  /*
    Remove this port from the new_ports_list, if it exists there.
    Remove this port from the guid and LID tables.
    Remove also from the sm guid table - if the object
    exists there.
  */
  p_new_ports_list = &p_mgr->p_subn->new_ports_list;
  cl_list_item = cl_list_head(p_new_ports_list);
  while( cl_list_item != cl_list_end(p_new_ports_list) )
  {
    if ( (osm_port_t*)(cl_list_obj(cl_list_item)) == p_port )
    {
      /* Found the port in the new_ports_list. Remove it from there. */
      cl_list_remove_item(p_new_ports_list, cl_list_item);
      break;
    }
    cl_list_item = cl_list_next(cl_list_item);
  }

  p_port_guid_tbl = &p_mgr->p_subn->port_guid_tbl;
  p_port_check = (osm_port_t*)cl_qmap_remove( p_port_guid_tbl, port_guid );
  if( p_port_check != p_port )
  {
    osm_log( p_mgr->p_log, OSM_LOG_ERROR,
             "__osm_drop_mgr_remove_port: ERR 0101: "
             "Port 0x%016" PRIx64 " not in guid table\n",
             cl_ntoh64( port_guid ) );
    goto Exit;
  }

  p_sm_guid_tbl = &p_mgr->p_subn->sm_guid_tbl;  
  p_sm = (osm_remote_sm_t*)cl_qmap_remove( p_sm_guid_tbl, port_guid );
  if( p_sm != (osm_remote_sm_t*)cl_qmap_end( p_sm_guid_tbl ) )
  {
    /* need to remove this item */
    osm_log( p_mgr->p_log, OSM_LOG_VERBOSE,
             "__osm_drop_mgr_remove_port: "
             "Cleaned SM for port guid\n" );
    
    free(p_sm);
  }

  __osm_drop_mgr_remove_router( p_mgr, port_guid );

  osm_port_get_lid_range_ho( p_port, &min_lid_ho, &max_lid_ho );

  osm_log( p_mgr->p_log, OSM_LOG_VERBOSE,
           "__osm_drop_mgr_remove_port: "
           "Clearing abandoned LID range [0x%X,0x%X]\n",
           min_lid_ho, max_lid_ho );

  p_port_lid_tbl = &p_mgr->p_subn->port_lid_tbl;
  for( lid_ho = min_lid_ho; lid_ho <= max_lid_ho; lid_ho++ )
    cl_ptr_vector_set( p_port_lid_tbl, lid_ho, NULL );

  drop_mgr_clean_physp(p_mgr, p_port->p_physp);

  p_mcm = (osm_mcm_info_t*)cl_qlist_remove_head( &p_port->mcm_list );
  while( p_mcm != (osm_mcm_info_t *)cl_qlist_end( &p_port->mcm_list ) )
  {
    p_mgrp = (osm_mgrp_t *)cl_qmap_get( &p_mgr->p_subn->mgrp_mlid_tbl,
                                        p_mcm->mlid );
    if(p_mgrp != (osm_mgrp_t *)cl_qmap_end( &p_mgr->p_subn->mgrp_mlid_tbl ) )
    {
      osm_mgrp_remove_port(p_mgr->p_subn, p_mgr->p_log, p_mgrp, p_port->guid );
      osm_mcm_info_delete( (osm_mcm_info_t*)p_mcm );
    }
    p_mcm = (osm_mcm_info_t*)cl_qlist_remove_head( &p_port->mcm_list );
  }

  /* initialize the p_node - may need to get node_desc later */
  p_node = p_port->p_node;

  osm_port_delete( &p_port );

  /* issue a notice - trap 65 */
  
  /* details of the notice */
  notice.generic_type = 0x83; /* is generic subn mgt type */
  ib_notice_set_prod_type_ho(&notice, 4); /* A class manager generator */
  /* endport ceases to be reachable */
  notice.g_or_v.generic.trap_num = CL_HTON16(65); 
  /* The sm_base_lid is saved in network order already. */
  notice.issuer_lid = p_mgr->p_subn->sm_base_lid;
  /* following C14-72.1.2 and table 119 p725 */
  /* we need to provide the GID */
  port_gid.unicast.prefix = p_mgr->p_subn->opt.subnet_prefix;
  port_gid.unicast.interface_id = port_guid;
  memcpy(&(notice.data_details.ntc_64_67.gid),
         &(port_gid),
         sizeof(ib_gid_t));
 
  /* According to page 653 - the issuer gid in this case of trap
     is the SM gid, since the SM is the initiator of this trap. */
  notice.issuer_gid.unicast.prefix = p_mgr->p_subn->opt.subnet_prefix;
  notice.issuer_gid.unicast.interface_id = p_mgr->p_subn->sm_port_guid;

  status = osm_report_notice(p_mgr->p_log, p_mgr->p_subn, &notice);
  if( status != IB_SUCCESS )
  {
    osm_log( p_mgr->p_log, OSM_LOG_ERROR,
             "__osm_drop_mgr_remove_port: ERR 0103: "
             "Error sending trap reports (%s)\n",
             ib_get_err_str( status ) );
    goto Exit;
  }

  if (osm_log_is_active( p_mgr->p_log, OSM_LOG_INFO ))
  {
    osm_log( p_mgr->p_log, OSM_LOG_INFO,
             "__osm_drop_mgr_remove_port: "
             "Removed port with GUID:0x%016" PRIx64
             " LID range [0x%X,0x%X] of node:%s\n",
             cl_ntoh64( port_gid.unicast.interface_id ),
             min_lid_ho, max_lid_ho, p_node ? p_node->print_desc : "UNKNOWN" );
  }

 Exit:
  OSM_LOG_EXIT( p_mgr->p_log );
}

/**********************************************************************
 **********************************************************************/
static void
__osm_drop_mgr_remove_switch(
  IN const osm_drop_mgr_t* const p_mgr,
  IN osm_node_t* p_node )
{
  osm_switch_t *p_sw;
  cl_qmap_t* p_sw_guid_tbl;
  ib_net64_t node_guid;

  OSM_LOG_ENTER( p_mgr->p_log, __osm_drop_mgr_remove_switch );

  node_guid = osm_node_get_node_guid( p_node );
  p_sw_guid_tbl = &p_mgr->p_subn->sw_guid_tbl;

  p_sw = (osm_switch_t*)cl_qmap_remove( p_sw_guid_tbl, node_guid );
  if( p_sw == (osm_switch_t*)cl_qmap_end( p_sw_guid_tbl ) )
  {
    osm_log( p_mgr->p_log, OSM_LOG_ERROR,
             "__osm_drop_mgr_remove_switch: ERR 0102: "
             "Node 0x%016" PRIx64 " not in switch table\n",
             cl_ntoh64( osm_node_get_node_guid( p_node ) ) );
  }
  else
  {
    p_node->sw = NULL;
    osm_switch_delete( &p_sw );
  }

  OSM_LOG_EXIT( p_mgr->p_log );
}

/**********************************************************************
 **********************************************************************/
static boolean_t
__osm_drop_mgr_process_node(
  IN const osm_drop_mgr_t* const p_mgr,
  IN osm_node_t* p_node )
{
  osm_physp_t *p_physp;
  osm_port_t *p_port;
  osm_node_t *p_node_check;
  cl_qmap_t *p_node_guid_tbl;
  uint32_t port_num;
  uint32_t max_ports;
  ib_net64_t port_guid;
  cl_qmap_t* p_port_guid_tbl;
  boolean_t return_val = FALSE;

  OSM_LOG_ENTER( p_mgr->p_log, __osm_drop_mgr_process_node );

  osm_log( p_mgr->p_log, OSM_LOG_VERBOSE,
           "__osm_drop_mgr_process_node: "
           "Unreachable node 0x%016" PRIx64 "\n",
           cl_ntoh64( osm_node_get_node_guid( p_node ) ) );

  /*
    Delete all the logical and physical port objects
    associated with this node.
  */
  p_port_guid_tbl = &p_mgr->p_subn->port_guid_tbl;

  max_ports = osm_node_get_num_physp( p_node );
  for( port_num = 0; port_num < max_ports; port_num++ )
  {
    p_physp = osm_node_get_physp_ptr( p_node, port_num );
    if( osm_physp_is_valid( p_physp ) )
    {
      port_guid = osm_physp_get_port_guid( p_physp );

      p_port = (osm_port_t*)cl_qmap_get( p_port_guid_tbl, port_guid );

      if( p_port != (osm_port_t*)cl_qmap_end( p_port_guid_tbl ) )
        __osm_drop_mgr_remove_port( p_mgr, p_port );
      else
        drop_mgr_clean_physp( p_mgr, p_physp );
    }
  }

  return_val = TRUE;

  if (p_node->sw)
    __osm_drop_mgr_remove_switch( p_mgr, p_node );

  p_node_guid_tbl = &p_mgr->p_subn->node_guid_tbl;
  p_node_check = (osm_node_t*)cl_qmap_remove( p_node_guid_tbl,
                                              osm_node_get_node_guid( p_node ) );
  if( p_node_check != p_node )
  {
    osm_log( p_mgr->p_log, OSM_LOG_ERROR,
             "__osm_drop_mgr_process_node: ERR 0105: "
             "Node 0x%016" PRIx64 " not in guid table\n",
             cl_ntoh64( osm_node_get_node_guid( p_node ) ) );
  }

  /* free memory allocated to node */
  osm_node_delete( &p_node );

  OSM_LOG_EXIT( p_mgr->p_log );
  return( return_val );
}

/**********************************************************************
 **********************************************************************/
static void
__osm_drop_mgr_check_node(
  IN const osm_drop_mgr_t* const p_mgr,
  IN osm_node_t* p_node )
{
  ib_net64_t node_guid;
  osm_physp_t *p_physp;
  osm_port_t *p_port;
  cl_qmap_t* p_port_guid_tbl;
  ib_net64_t port_guid;

  OSM_LOG_ENTER( p_mgr->p_log, __osm_drop_mgr_check_node );

  node_guid = osm_node_get_node_guid( p_node );

  if ( osm_node_get_type( p_node ) != IB_NODE_TYPE_SWITCH )
  {
    osm_log( p_mgr->p_log, OSM_LOG_ERROR,
             "__osm_drop_mgr_check_node: ERR 0107: "
             "Node 0x%016" PRIx64 " is not a switch node\n",
             cl_ntoh64( node_guid ) );
    goto Exit;
  }

  /* Make sure we have a switch object for this node */
  if (!p_node->sw)
  {
    /* We do not have switch info for this node */
    osm_log( p_mgr->p_log, OSM_LOG_VERBOSE,
             "__osm_drop_mgr_check_node: "
             "Node 0x%016" PRIx64 " no switch in table\n",
             cl_ntoh64( node_guid ) );
    
    __osm_drop_mgr_process_node( p_mgr, p_node );
    goto Exit;
  }

  /* Make sure we have a port object for port zero */
  p_port_guid_tbl = &p_mgr->p_subn->port_guid_tbl;
  p_physp = osm_node_get_physp_ptr( p_node, 0 );
  if ( !osm_physp_is_valid( p_physp ) )
  {
    osm_log( p_mgr->p_log, OSM_LOG_VERBOSE,
             "__osm_drop_mgr_check_node: "
             "Node 0x%016" PRIx64 " no valid physical port 0\n",
             cl_ntoh64( node_guid ) );
    
    __osm_drop_mgr_process_node( p_mgr, p_node );
    goto Exit;
  }
   
  port_guid = osm_physp_get_port_guid( p_physp );

  p_port = (osm_port_t*)cl_qmap_get( p_port_guid_tbl, port_guid );

  if( p_port == (osm_port_t*)cl_qmap_end( p_port_guid_tbl ) )
  {
    osm_log( p_mgr->p_log, OSM_LOG_VERBOSE,
             "__osm_drop_mgr_check_node: "
             "Node 0x%016" PRIx64 " has no port object\n",
             cl_ntoh64( node_guid ) );
    
    __osm_drop_mgr_process_node( p_mgr, p_node );
    goto Exit;
  }

  if ( osm_port_discovery_count_get( p_port ) == 0 )
  {
    osm_log( p_mgr->p_log, OSM_LOG_VERBOSE,
             "__osm_drop_mgr_check_node: "
             "Node 0x%016" PRIx64 " port has discovery count zero\n",
             cl_ntoh64( node_guid ) );
    
    __osm_drop_mgr_process_node( p_mgr, p_node );
    goto Exit;
  }

  Exit:
  OSM_LOG_EXIT( p_mgr->p_log );
  return;
}

/**********************************************************************
 **********************************************************************/
void
osm_drop_mgr_process(
  IN const osm_drop_mgr_t* const p_mgr )
{
  cl_qmap_t   *p_node_guid_tbl;
  cl_qmap_t   *p_port_guid_tbl;
  cl_list_t   *p_lsweep_ports;
  osm_port_t  *p_port;
  osm_port_t  *p_next_port;
  osm_node_t  *p_node;
  osm_node_t  *p_next_node;
  ib_net64_t   port_guid;
  ib_net64_t   node_guid;
  uint8_t      port_num;
  osm_physp_t *p_physp;

  CL_ASSERT( p_mgr );

  OSM_LOG_ENTER( p_mgr->p_log, osm_drop_mgr_process );

  p_node_guid_tbl = &p_mgr->p_subn->node_guid_tbl;
  p_port_guid_tbl = &p_mgr->p_subn->port_guid_tbl;
  p_lsweep_ports = &p_mgr->p_subn->light_sweep_physp_list;

  CL_PLOCK_EXCL_ACQUIRE( p_mgr->p_lock );

  p_next_node = (osm_node_t*)cl_qmap_head( p_node_guid_tbl );
  while( p_next_node != (osm_node_t*)cl_qmap_end( p_node_guid_tbl ) )
  {
    p_node = p_next_node;
    p_next_node = (osm_node_t*)cl_qmap_next( &p_next_node->map_item );

    CL_ASSERT( cl_qmap_key( &p_node->map_item ) ==
               osm_node_get_node_guid( p_node ) );

    if( osm_log_is_active( p_mgr->p_log, OSM_LOG_DEBUG ) )
    {
      node_guid = osm_node_get_node_guid( p_node );
      osm_log( p_mgr->p_log, OSM_LOG_DEBUG,
               "osm_drop_mgr_process: "
               "Checking node 0x%016" PRIx64 "\n",
               cl_ntoh64( node_guid ) );
    }

    /*
      Check if this node was discovered during the last sweep.
      If not, it is unreachable in the current subnet, and
      should therefore be removed from the subnet object.
    */
    if( osm_node_discovery_count_get( p_node ) == 0 )
      __osm_drop_mgr_process_node( p_mgr, p_node );
  }

  /*
    Go over all the nodes. If the node is a switch - make sure
    there is also a switch record for it, and a portInfo record for
    port zero of of the node.
    If not - this means that there was some error in getting the data
    of this node. Drop the node.
  */
  p_next_node = (osm_node_t*)cl_qmap_head( p_node_guid_tbl );  
  while( p_next_node != (osm_node_t*)cl_qmap_end( p_node_guid_tbl ) )
  {
    p_node = p_next_node;
    p_next_node = (osm_node_t*)cl_qmap_next( &p_next_node->map_item );

    if( osm_log_is_active( p_mgr->p_log, OSM_LOG_DEBUG ) )
    {
      node_guid = osm_node_get_node_guid( p_node );
      osm_log( p_mgr->p_log, OSM_LOG_DEBUG,
               "osm_drop_mgr_process: "
               "Checking full discovery of node 0x%016" PRIx64 "\n",
               cl_ntoh64( node_guid ) );
    }

    if ( osm_node_get_type( p_node ) != IB_NODE_TYPE_SWITCH )
      continue;

    /* We are handling a switch node */
    __osm_drop_mgr_check_node( p_mgr, p_node );
  }
    
  p_next_port = (osm_port_t*)cl_qmap_head( p_port_guid_tbl );
  while( p_next_port != (osm_port_t*)cl_qmap_end( p_port_guid_tbl ) )
  {
    p_port = p_next_port;
    p_next_port = (osm_port_t*)cl_qmap_next( &p_next_port->map_item );

    CL_ASSERT( cl_qmap_key( &p_port->map_item ) ==
               osm_port_get_guid( p_port ) );

    if( osm_log_is_active( p_mgr->p_log, OSM_LOG_DEBUG ) )
    {
      port_guid = osm_port_get_guid( p_port );
      osm_log( p_mgr->p_log, OSM_LOG_DEBUG,
               "osm_drop_mgr_process: "
               "Checking port 0x%016" PRIx64 "\n",
               cl_ntoh64( port_guid ) );
    }

    /*
      If the port is unreachable, remove it from the guid table.
    */
    if( osm_port_discovery_count_get( p_port ) == 0 )
      __osm_drop_mgr_remove_port( p_mgr, p_port );
  }

  /* 
     scan through all the ports left - if the port is not DOWN and 
     it does not have a valid remote port - we need to track it for 
     next light sweep scan...
  */
  cl_list_remove_all( p_lsweep_ports );
  p_next_node = (osm_node_t*)cl_qmap_head( p_node_guid_tbl );
  while( p_next_node != (osm_node_t*)cl_qmap_end( p_node_guid_tbl ) )
  {
    p_node = p_next_node;
    p_next_node = (osm_node_t*)cl_qmap_next( &p_next_node->map_item );
    
    for (port_num = 1; port_num < osm_node_get_num_physp(p_node); port_num++)
    {
      p_physp = osm_node_get_physp_ptr(p_node, port_num);
      if (osm_physp_is_valid(p_physp) && 
          (osm_physp_get_port_state(p_physp) != IB_LINK_DOWN) &&
          ! osm_physp_get_remote(p_physp))
      {
        osm_log( p_mgr->p_log, OSM_LOG_ERROR,
                 "osm_drop_mgr_process: ERR 0108: "
                 "Unknown remote side for node 0x%016" PRIx64 
                 " port %u. Adding to light sweep sampling list\n",
                 cl_ntoh64( osm_node_get_node_guid( p_node )),
                 port_num);
        
        osm_dump_dr_path(p_mgr->p_log, 
                         osm_physp_get_dr_path_ptr( p_physp ),
                         OSM_LOG_ERROR);
        
        cl_list_insert_head( p_lsweep_ports, p_physp );
      }
    }
  }
 
  CL_PLOCK_RELEASE( p_mgr->p_lock );
  OSM_LOG_EXIT( p_mgr->p_log );
}

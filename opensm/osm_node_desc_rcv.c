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
 *    Implementation of osm_nd_rcv_t.
 * This object represents the NodeDescription Receiver object.
 * This object is part of the opensm family of objects.
 *
 * Environment:
 *    Linux User Mode
 *
 * $Revision: 1.5 $
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <string.h>
#include <iba/ib_types.h>
#include <complib/cl_qmap.h>
#include <complib/cl_passivelock.h>
#include <complib/cl_debug.h>
#include <opensm/osm_node_desc_rcv.h>
#include <opensm/osm_madw.h>
#include <opensm/osm_log.h>
#include <opensm/osm_node.h>
#include <opensm/osm_subnet.h>

/**********************************************************************
 **********************************************************************/
void
__osm_nd_rcv_process_nd(
  IN const osm_nd_rcv_t* const p_rcv,
  IN osm_node_t* const p_node,
  IN const ib_node_desc_t* const p_nd )
{
  OSM_LOG_ENTER( p_rcv->p_log, __osm_nd_rcv_process_nd );

  memcpy( &p_node->node_desc.description, p_nd, sizeof(*p_nd) );

  /* also set up a printable version */
  memcpy( &p_node->print_desc, p_nd, sizeof(*p_nd) );
  p_node->print_desc[IB_NODE_DESCRIPTION_SIZE] = '\0';

  if( osm_log_is_active( p_rcv->p_log, OSM_LOG_VERBOSE ) )
  {
    osm_log( p_rcv->p_log, OSM_LOG_VERBOSE,
             "__osm_nd_rcv_process_nd: "
             "Node 0x%" PRIx64 "\n\t\t\t\tDescription = %s\n",
             cl_ntoh64( osm_node_get_node_guid( p_node )),
             p_node->print_desc);
  }

  OSM_LOG_EXIT( p_rcv->p_log );
}

/**********************************************************************
 **********************************************************************/
void
osm_nd_rcv_construct(
  IN osm_nd_rcv_t* const p_rcv )
{
  memset( p_rcv, 0, sizeof(*p_rcv) );
}

/**********************************************************************
 **********************************************************************/
void
osm_nd_rcv_destroy(
  IN osm_nd_rcv_t* const p_rcv )
{
  CL_ASSERT( p_rcv );

  OSM_LOG_ENTER( p_rcv->p_log, osm_nd_rcv_destroy );

  OSM_LOG_EXIT( p_rcv->p_log );
}

/**********************************************************************
 **********************************************************************/
ib_api_status_t
osm_nd_rcv_init(
  IN osm_nd_rcv_t* const p_rcv,
  IN osm_subn_t* const p_subn,
  IN osm_log_t* const p_log,
  IN cl_plock_t* const p_lock )
{
  ib_api_status_t status = IB_SUCCESS;

  OSM_LOG_ENTER( p_log, osm_nd_rcv_init );

  osm_nd_rcv_construct( p_rcv );

  p_rcv->p_log = p_log;
  p_rcv->p_subn = p_subn;
  p_rcv->p_lock = p_lock;

  OSM_LOG_EXIT( p_rcv->p_log );
  return( status );
}

/**********************************************************************
 **********************************************************************/
void
osm_nd_rcv_process(
  IN void *context,
  IN void *data )
{
  osm_nd_rcv_t *p_rcv = context;
  osm_madw_t *p_madw = data;
  cl_qmap_t *p_guid_tbl;
  ib_node_desc_t *p_nd;
  ib_smp_t *p_smp;
  osm_node_t *p_node;
  ib_net64_t node_guid;

  CL_ASSERT( p_rcv );

  OSM_LOG_ENTER( p_rcv->p_log, osm_nd_rcv_process );

  CL_ASSERT( p_madw );

  p_guid_tbl = &p_rcv->p_subn->node_guid_tbl;
  p_smp = osm_madw_get_smp_ptr( p_madw );
  p_nd = (ib_node_desc_t*)ib_smp_get_payload_ptr( p_smp );

  /*
    Acquire the node object and add the node description.
  */

  node_guid = osm_madw_get_nd_context_ptr( p_madw )->node_guid;
  CL_PLOCK_EXCL_ACQUIRE( p_rcv->p_lock );
  p_node = (osm_node_t*)cl_qmap_get( p_guid_tbl, node_guid );

  if( p_node == (osm_node_t*)cl_qmap_end( p_guid_tbl) )
  {
    osm_log( p_rcv->p_log, OSM_LOG_ERROR,
             "osm_nd_rcv_process: ERR 0B01: "
             "NodeDescription received for nonexistent node "
             "0x%" PRIx64 "\n", cl_ntoh64(node_guid) );
  }
  else
  {
    __osm_nd_rcv_process_nd( p_rcv, p_node, p_nd );
  }

  CL_PLOCK_RELEASE( p_rcv->p_lock );
  OSM_LOG_EXIT( p_rcv->p_log );
}

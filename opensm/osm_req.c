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
 *    Implementation of osm_req_t.
 * This object represents the generic attribute requester.
 * This object is part of the opensm family of objects.
 *
 * Environment:
 *    Linux User Mode
 *
 * $Revision: 1.6 $
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <string.h>
#include <iba/ib_types.h>
#include <complib/cl_debug.h>
#include <opensm/osm_req.h>
#include <opensm/osm_madw.h>
#include <opensm/osm_attrib_req.h>
#include <opensm/osm_log.h>
#include <opensm/osm_helper.h>
#include <opensm/osm_mad_pool.h>
#include <opensm/osm_vl15intf.h>
#include <opensm/osm_msgdef.h>
#include <opensm/osm_opensm.h>

/**********************************************************************
 **********************************************************************/
void
osm_req_construct(
  IN osm_req_t* const p_req )
{
  CL_ASSERT( p_req );

  memset( p_req, 0, sizeof(*p_req) );
}

/**********************************************************************
 **********************************************************************/
void
osm_req_destroy(
  IN osm_req_t* const p_req )
{
  CL_ASSERT( p_req );
}

/**********************************************************************
 **********************************************************************/
ib_api_status_t
osm_req_init(
  IN osm_req_t*            const p_req,
  IN osm_mad_pool_t*       const p_pool,
  IN osm_vl15_t*           const p_vl15,
  IN osm_subn_t*           const p_subn,
  IN osm_log_t*            const p_log,
  IN atomic32_t*           const p_sm_trans_id )
{
  ib_api_status_t status = IB_SUCCESS;

  OSM_LOG_ENTER( p_log, osm_req_init );

  osm_req_construct( p_req );
  p_req->p_log = p_log;

  p_req->p_pool = p_pool;
  p_req->p_vl15 = p_vl15;
  p_req->p_subn = p_subn;
  p_req->p_sm_trans_id = p_sm_trans_id;

  OSM_LOG_EXIT( p_log );
  return( status );
}

/**********************************************************************
  The plock MAY or MAY NOT be held before calling this function.
**********************************************************************/
ib_api_status_t
osm_req_get(
  IN const osm_req_t* const p_req,
  IN const osm_dr_path_t* const p_path,
  IN const uint16_t attr_id,
  IN const uint32_t attr_mod,
  IN const cl_disp_msgid_t err_msg,
  IN const osm_madw_context_t* const p_context )
{
  osm_madw_t *p_madw;
  ib_api_status_t status = IB_SUCCESS;
  ib_net64_t tid;

  CL_ASSERT( p_req );

  OSM_LOG_ENTER( p_req->p_log, osm_req_get );

  CL_ASSERT( p_path );
  CL_ASSERT( attr_id );

  /* do nothing if we are exiting ... */
  if (osm_exit_flag)
    goto Exit;

  /* p_context may be NULL. */

  p_madw = osm_mad_pool_get(
    p_req->p_pool,
    p_path->h_bind,
    MAD_BLOCK_SIZE,
    NULL );

  if( p_madw == NULL )
  {
    osm_log( p_req->p_log, OSM_LOG_ERROR,
             "osm_req_get: ERR 1101: "
             "Unable to acquire MAD\n" );
    status = IB_INSUFFICIENT_RESOURCES;
    goto Exit;
  }

  tid = cl_hton64( (uint64_t)cl_atomic_inc( p_req->p_sm_trans_id ) );

  if( osm_log_is_active( p_req->p_log, OSM_LOG_DEBUG ) )
  {
    osm_log( p_req->p_log, OSM_LOG_DEBUG,
             "osm_req_get: "
             "Getting %s (0x%X), modifier 0x%X, TID 0x%" PRIx64 "\n",
             ib_get_sm_attr_str( attr_id ),
             cl_ntoh16( attr_id ),
             cl_ntoh32( attr_mod ),
             cl_ntoh64( tid ) );
  }

  ib_smp_init_new(
    osm_madw_get_smp_ptr( p_madw ),
    IB_MAD_METHOD_GET,
    tid,
    attr_id,
    attr_mod,
    p_path->hop_count,
    p_req->p_subn->opt.m_key,
    p_path->path,
    IB_LID_PERMISSIVE,
    IB_LID_PERMISSIVE );

  p_madw->mad_addr.dest_lid = IB_LID_PERMISSIVE;
  p_madw->mad_addr.addr_type.smi.source_lid = IB_LID_PERMISSIVE;
  p_madw->resp_expected = TRUE;
  p_madw->fail_msg = err_msg;

  /*
    Fill in the mad wrapper context for the recipient.
    In this case, the only thing the recipient needs is the
    guid value.
  */

  if( p_context )
    p_madw->context = *p_context;

  osm_vl15_post( p_req->p_vl15, p_madw );

 Exit:
  OSM_LOG_EXIT( p_req->p_log );
  return( status );
}

/**********************************************************************
  The plock MAY or MAY NOT be held before calling this function.
**********************************************************************/
ib_api_status_t
osm_req_set(
  IN const osm_req_t* const p_req,
  IN const osm_dr_path_t* const p_path,
  IN const uint8_t* const p_payload,
  IN const size_t payload_size,
  IN const uint16_t attr_id,
  IN const uint32_t attr_mod,
  IN const cl_disp_msgid_t err_msg,
  IN const osm_madw_context_t* const p_context )
{
  osm_madw_t *p_madw;
  ib_api_status_t status = IB_SUCCESS;
  ib_net64_t tid;

  CL_ASSERT( p_req );

  OSM_LOG_ENTER( p_req->p_log, osm_req_set );

  CL_ASSERT( p_path );
  CL_ASSERT( attr_id );
  CL_ASSERT( p_payload );

  /* do nothing if we are exiting ... */
  if (osm_exit_flag)
    goto Exit;

  /* p_context may be NULL. */

  p_madw = osm_mad_pool_get(
    p_req->p_pool,
    p_path->h_bind,
    MAD_BLOCK_SIZE,
    NULL );

  if( p_madw == NULL )
  {
    osm_log( p_req->p_log, OSM_LOG_ERROR,
             "osm_req_set: ERR 1102: "
             "Unable to acquire MAD\n" );
    status = IB_INSUFFICIENT_RESOURCES;
    goto Exit;
  }

  tid = cl_hton64( (uint64_t)cl_atomic_inc( p_req->p_sm_trans_id ) );

  if( osm_log_is_active( p_req->p_log, OSM_LOG_DEBUG ) )
  {
    osm_log( p_req->p_log, OSM_LOG_DEBUG,
             "osm_req_set: "
             "Setting %s (0x%X), modifier 0x%X, TID 0x%" PRIx64 "\n",
             ib_get_sm_attr_str( attr_id ),
             cl_ntoh16( attr_id ),
             cl_ntoh32( attr_mod ),
             cl_ntoh64( tid ) );
  }

  ib_smp_init_new(
    osm_madw_get_smp_ptr( p_madw ),
    IB_MAD_METHOD_SET,
    tid,
    attr_id,
    attr_mod,
    p_path->hop_count,
    p_req->p_subn->opt.m_key,
    p_path->path,
    IB_LID_PERMISSIVE,
    IB_LID_PERMISSIVE );

  p_madw->mad_addr.dest_lid = IB_LID_PERMISSIVE;
  p_madw->mad_addr.addr_type.smi.source_lid = IB_LID_PERMISSIVE;
  p_madw->resp_expected = TRUE;
  p_madw->fail_msg = err_msg;

  /*
    Fill in the mad wrapper context for the recipient.
    In this case, the only thing the recipient needs is the
    guid value.
  */

  if( p_context )
    p_madw->context = *p_context;

  memcpy( osm_madw_get_smp_ptr( p_madw )->data,
          p_payload, payload_size );

  osm_vl15_post( p_req->p_vl15, p_madw );

 Exit:
  OSM_LOG_EXIT( p_req->p_log );
  return( status );
}


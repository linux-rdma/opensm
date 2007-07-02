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
 *    Implementation of osm_sm_t.
 * This object represents the SM Receiver object.
 * This object is part of the opensm family of objects.
 *
 * Environment:
 *    Linux User Mode
 *
 * $Revision: 1.9 $
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <stdlib.h>
#include <string.h>
#include <complib/cl_qmap.h>
#include <complib/cl_passivelock.h>
#include <complib/cl_debug.h>
#include <iba/ib_types.h>
#include <opensm/osm_sm.h>
#include <opensm/osm_madw.h>
#include <opensm/osm_log.h>
#include <opensm/osm_node.h>
#include <opensm/osm_msgdef.h>
#include <opensm/osm_mcast_mgr.h>
#include <opensm/osm_mcm_info.h>
#include <complib/cl_thread.h>
#include <signal.h>

#define  OSM_SM_INITIAL_TID_VALUE 0x1233

/**********************************************************************
 **********************************************************************/
void
__osm_sm_sweeper(
   IN void *p_ptr )
{
   ib_api_status_t status;
   osm_sm_t *const p_sm = ( osm_sm_t * ) p_ptr;

   OSM_LOG_ENTER( p_sm->p_log, __osm_sm_sweeper );

   if( p_sm->thread_state == OSM_THREAD_STATE_INIT )
   {
      p_sm->thread_state = OSM_THREAD_STATE_RUN;
   }

   /* If the sweep interval was updated before - then run only if 
    * it is not zero. */
   while( p_sm->thread_state == OSM_THREAD_STATE_RUN &&
          p_sm->p_subn->opt.sweep_interval != 0 )
   {
      /*  do the sweep only if we are in MASTER state */
      if( p_sm->p_subn->sm_state == IB_SMINFO_STATE_MASTER ||
          p_sm->p_subn->sm_state == IB_SMINFO_STATE_DISCOVERING )
         osm_state_mgr_process( &p_sm->state_mgr, OSM_SIGNAL_SWEEP );

      /*
       * Wait on the event with a timeout.
       * Sweeps may be initiated "off schedule" by simply
       * signaling the event.
       */
      status = cl_event_wait_on( &p_sm->signal,
                                 p_sm->p_subn->opt.sweep_interval * 1000000,
                                 TRUE );

      if( status == CL_SUCCESS )
      {
         if( osm_log_is_active( p_sm->p_log, OSM_LOG_DEBUG ) )
         {
            osm_log( p_sm->p_log, OSM_LOG_DEBUG,
                     "__osm_sm_sweeper: " "Off schedule sweep signalled\n" );
         }
      }
      else
      {
         if( status != CL_TIMEOUT )
         {
            osm_log( p_sm->p_log, OSM_LOG_ERROR,
                     "__osm_sm_sweeper: ERR 2E01: "
                     "Event wait failed (%s)\n", CL_STATUS_MSG( status ) );
         }
      }
   }

   OSM_LOG_EXIT( p_sm->p_log );
}

/**********************************************************************
 **********************************************************************/
void
osm_sm_construct(
   IN osm_sm_t * const p_sm )
{
   memset( p_sm, 0, sizeof( *p_sm ) );
   p_sm->thread_state = OSM_THREAD_STATE_NONE;
   p_sm->sm_trans_id = OSM_SM_INITIAL_TID_VALUE;
   cl_event_construct( &p_sm->signal );
   cl_event_construct( &p_sm->subnet_up_event );
   cl_thread_construct( &p_sm->sweeper );
   osm_req_construct( &p_sm->req );
   osm_resp_construct( &p_sm->resp );
   osm_ni_rcv_construct( &p_sm->ni_rcv );
   osm_pi_rcv_construct( &p_sm->pi_rcv );
   osm_nd_rcv_construct( &p_sm->nd_rcv );
   osm_sm_mad_ctrl_construct( &p_sm->mad_ctrl );
   osm_si_rcv_construct( &p_sm->si_rcv );
   osm_lid_mgr_construct( &p_sm->lid_mgr );
   osm_ucast_mgr_construct( &p_sm->ucast_mgr );
   osm_link_mgr_construct( &p_sm->link_mgr );
   osm_state_mgr_construct( &p_sm->state_mgr );
   osm_state_mgr_ctrl_construct( &p_sm->state_mgr_ctrl );
   osm_drop_mgr_construct( &p_sm->drop_mgr );
   osm_lft_rcv_construct( &p_sm->lft_rcv );
   osm_mft_rcv_construct( &p_sm->mft_rcv );
   osm_sweep_fail_ctrl_construct( &p_sm->sweep_fail_ctrl );
   osm_sminfo_rcv_construct( &p_sm->sm_info_rcv );
   osm_trap_rcv_construct( &p_sm->trap_rcv );
   osm_sm_state_mgr_construct( &p_sm->sm_state_mgr );
   osm_slvl_rcv_construct( &p_sm->slvl_rcv );
   osm_vla_rcv_construct( &p_sm->vla_rcv );
   osm_pkey_rcv_construct( &p_sm->pkey_rcv );
   osm_mcast_mgr_construct( &p_sm->mcast_mgr );
}

/**********************************************************************
 **********************************************************************/
void
osm_sm_shutdown(
   IN osm_sm_t * const p_sm )
{
   boolean_t signal_event = FALSE;

   OSM_LOG_ENTER( p_sm->p_log, osm_sm_shutdown );

   /*
    * Signal our threads that we're leaving.
    */
   if( p_sm->thread_state != OSM_THREAD_STATE_NONE )
      signal_event = TRUE;

   p_sm->thread_state = OSM_THREAD_STATE_EXIT;

   /*
    * Don't trigger unless event has been initialized.
    * Destroy the thread before we tear down the other objects.
    */
   if( signal_event )
      cl_event_signal( &p_sm->signal );

   cl_thread_destroy( &p_sm->sweeper );

   /*
    * Always destroy controllers before the corresponding
    * receiver to guarantee that all callbacks from the
    * dispatcher are complete.
    */
   osm_sm_mad_ctrl_destroy( &p_sm->mad_ctrl );
   cl_disp_unregister(p_sm->ni_disp_h);
   cl_disp_unregister(p_sm->pi_disp_h);
   cl_disp_unregister(p_sm->si_disp_h);
   cl_disp_unregister(p_sm->nd_disp_h);
   cl_disp_unregister(p_sm->lft_disp_h);
   cl_disp_unregister(p_sm->mft_disp_h);
   cl_disp_unregister(p_sm->sm_info_disp_h);
   cl_disp_unregister(p_sm->trap_disp_h);
   cl_disp_unregister(p_sm->slvl_disp_h);
   cl_disp_unregister(p_sm->vla_disp_h);
   cl_disp_unregister(p_sm->pkey_disp_h);
   osm_sweep_fail_ctrl_destroy( &p_sm->sweep_fail_ctrl );
   osm_state_mgr_ctrl_destroy( &p_sm->state_mgr_ctrl );

   OSM_LOG_EXIT( p_sm->p_log );
}

/**********************************************************************
 **********************************************************************/
void
osm_sm_destroy(
   IN osm_sm_t * const p_sm )
{
   OSM_LOG_ENTER( p_sm->p_log, osm_sm_destroy );
   osm_trap_rcv_destroy( &p_sm->trap_rcv );
   osm_sminfo_rcv_destroy( &p_sm->sm_info_rcv );
   osm_req_destroy( &p_sm->req );
   osm_resp_destroy( &p_sm->resp );
   osm_ni_rcv_destroy( &p_sm->ni_rcv );
   osm_pi_rcv_destroy( &p_sm->pi_rcv );
   osm_si_rcv_destroy( &p_sm->si_rcv );
   osm_nd_rcv_destroy( &p_sm->nd_rcv );
   osm_lid_mgr_destroy( &p_sm->lid_mgr );
   osm_ucast_mgr_destroy( &p_sm->ucast_mgr );
   osm_link_mgr_destroy( &p_sm->link_mgr );
   osm_drop_mgr_destroy( &p_sm->drop_mgr );
   osm_lft_rcv_destroy( &p_sm->lft_rcv );
   osm_mft_rcv_destroy( &p_sm->mft_rcv );
   osm_slvl_rcv_destroy( &p_sm->slvl_rcv );
   osm_vla_rcv_destroy( &p_sm->vla_rcv );
   osm_pkey_rcv_destroy( &p_sm->pkey_rcv );
   osm_state_mgr_destroy( &p_sm->state_mgr );
   osm_sm_state_mgr_destroy( &p_sm->sm_state_mgr );
   osm_mcast_mgr_destroy( &p_sm->mcast_mgr );
   cl_event_destroy( &p_sm->signal );
   cl_event_destroy( &p_sm->subnet_up_event );

   osm_log( p_sm->p_log, OSM_LOG_SYS, "Exiting SM\n" ); /* Format Waived */
   OSM_LOG_EXIT( p_sm->p_log );
}

/**********************************************************************
 **********************************************************************/
ib_api_status_t
osm_sm_init(
   IN osm_sm_t * const p_sm,
   IN osm_subn_t * const p_subn,
   IN osm_db_t * const p_db,
   IN osm_vendor_t * const p_vendor,
   IN osm_mad_pool_t * const p_mad_pool,
   IN osm_vl15_t * const p_vl15,
   IN osm_log_t * const p_log,
   IN osm_stats_t * const p_stats,
   IN cl_dispatcher_t * const p_disp,
   IN cl_plock_t * const p_lock )
{
   ib_api_status_t status = IB_SUCCESS;

   OSM_LOG_ENTER( p_log, osm_sm_init );

   p_sm->p_subn = p_subn;
   p_sm->p_db = p_db;
   p_sm->p_vendor = p_vendor;
   p_sm->p_mad_pool = p_mad_pool;
   p_sm->p_vl15 = p_vl15;
   p_sm->p_log = p_log;
   p_sm->p_disp = p_disp;
   p_sm->p_lock = p_lock;

   status = cl_event_init( &p_sm->signal, FALSE );
   if( status != CL_SUCCESS )
      goto Exit;

   status = cl_event_init( &p_sm->subnet_up_event, FALSE );
   if( status != CL_SUCCESS )
      goto Exit;

   status = osm_sm_mad_ctrl_init( &p_sm->mad_ctrl,
                                  p_sm->p_subn,
                                  p_sm->p_mad_pool,
                                  p_sm->p_vl15,
                                  p_sm->p_vendor,
                                  p_log, p_stats, p_lock, p_disp );
   if( status != IB_SUCCESS )
      goto Exit;

   status = osm_req_init( &p_sm->req,
                          p_mad_pool,
                          p_vl15, p_subn, p_log, &p_sm->sm_trans_id );
   if( status != IB_SUCCESS )
      goto Exit;

   status = osm_resp_init( &p_sm->resp, p_mad_pool, p_vl15, p_subn, p_log );
   if( status != IB_SUCCESS )
      goto Exit;

   status = osm_ni_rcv_init( &p_sm->ni_rcv,
                             &p_sm->req,
                             p_subn, p_log, &p_sm->state_mgr, p_lock );
   if( status != IB_SUCCESS )
      goto Exit;

   status = osm_pi_rcv_init( &p_sm->pi_rcv,
                             &p_sm->req,
                             p_subn, p_log, &p_sm->state_mgr, p_lock );
   if( status != IB_SUCCESS )
      goto Exit;

   status = osm_si_rcv_init( &p_sm->si_rcv,
                             p_sm->p_subn,
                             p_sm->p_log,
                             &p_sm->req, &p_sm->state_mgr, p_sm->p_lock );

   if( status != IB_SUCCESS )
      goto Exit;

   status = osm_nd_rcv_init( &p_sm->nd_rcv, p_subn, p_log, p_lock );
   if( status != IB_SUCCESS )
      goto Exit;

   status = osm_lid_mgr_init( &p_sm->lid_mgr,
                              &p_sm->req,
                              p_sm->p_subn,
                              p_sm->p_db, p_sm->p_log, p_sm->p_lock );
   if( status != IB_SUCCESS )
      goto Exit;

   status = osm_ucast_mgr_init( &p_sm->ucast_mgr,
                                &p_sm->req,
                                p_sm->p_subn,
                                p_sm->p_log, p_sm->p_lock );
   if( status != IB_SUCCESS )
      goto Exit;

   status = osm_link_mgr_init( &p_sm->link_mgr,
                               &p_sm->req,
                               p_sm->p_subn, p_sm->p_log, p_sm->p_lock );
   if( status != IB_SUCCESS )
      goto Exit;

   status = osm_state_mgr_init( &p_sm->state_mgr,
                                p_sm->p_subn,
                                &p_sm->lid_mgr,
                                &p_sm->ucast_mgr,
                                &p_sm->mcast_mgr,
                                &p_sm->link_mgr,
                                &p_sm->drop_mgr,
                                &p_sm->req,
                                p_stats,
                                &p_sm->sm_state_mgr,
                                &p_sm->mad_ctrl,
                                p_sm->p_lock,
                                &p_sm->subnet_up_event,
                                p_sm->p_log );
   if( status != IB_SUCCESS )
      goto Exit;

   status = osm_state_mgr_ctrl_init( &p_sm->state_mgr_ctrl,
                                     &p_sm->state_mgr,
                                     p_sm->p_log, p_sm->p_disp );
   if( status != IB_SUCCESS )
      goto Exit;

   status = osm_drop_mgr_init( &p_sm->drop_mgr,
                               p_sm->p_subn,
                               p_sm->p_log, &p_sm->req, p_sm->p_lock );
   if( status != IB_SUCCESS )
      goto Exit;

   status = osm_lft_rcv_init( &p_sm->lft_rcv, p_subn, p_log, p_lock );
   if( status != IB_SUCCESS )
      goto Exit;

   status = osm_mft_rcv_init( &p_sm->mft_rcv, p_subn, p_log, p_lock );
   if( status != IB_SUCCESS )
      goto Exit;

   status = osm_sweep_fail_ctrl_init( &p_sm->sweep_fail_ctrl,
                                      p_log, &p_sm->state_mgr, p_disp );
   if( status != IB_SUCCESS )
      goto Exit;

   status = osm_sminfo_rcv_init( &p_sm->sm_info_rcv,
                                 p_subn,
                                 p_stats,
                                 &p_sm->resp,
                                 p_log,
                                 &p_sm->state_mgr,
                                 &p_sm->sm_state_mgr, p_lock );
   if( status != IB_SUCCESS )
      goto Exit;

   status = osm_trap_rcv_init( &p_sm->trap_rcv,
                               p_subn,
                               p_stats,
                               &p_sm->resp, p_log, &p_sm->state_mgr, p_lock );
   if( status != IB_SUCCESS )
      goto Exit;

   status = osm_sm_state_mgr_init( &p_sm->sm_state_mgr,
                                   &p_sm->state_mgr,
                                   p_sm->p_subn, &p_sm->req, p_sm->p_log );
   if( status != IB_SUCCESS )
      goto Exit;

   status = osm_mcast_mgr_init( &p_sm->mcast_mgr,
                                &p_sm->req, p_subn, p_log, p_lock );
   if( status != IB_SUCCESS )
      goto Exit;

   status = osm_slvl_rcv_init( &p_sm->slvl_rcv,
                               &p_sm->req, p_subn, p_log, p_lock );
   if( status != IB_SUCCESS )
      goto Exit;

   status = osm_vla_rcv_init( &p_sm->vla_rcv,
                              &p_sm->req, p_subn, p_log, p_lock );
   if( status != IB_SUCCESS )
      goto Exit;

   status = osm_pkey_rcv_init( &p_sm->pkey_rcv,
                               &p_sm->req, p_subn, p_log, p_lock );
   if( status != IB_SUCCESS )
      goto Exit;

   p_sm->ni_disp_h = cl_disp_register(p_disp, OSM_MSG_MAD_NODE_INFO,
                                      osm_ni_rcv_process, &p_sm->ni_rcv);
   if( p_sm->ni_disp_h == CL_DISP_INVALID_HANDLE )
      goto Exit;

   p_sm->pi_disp_h = cl_disp_register(p_disp, OSM_MSG_MAD_PORT_INFO,
                                      osm_pi_rcv_process, &p_sm->pi_rcv);
   if( p_sm->pi_disp_h == CL_DISP_INVALID_HANDLE )
      goto Exit;

   p_sm->si_disp_h = cl_disp_register(p_disp, OSM_MSG_MAD_SWITCH_INFO,
                                      osm_si_rcv_process, &p_sm->si_rcv);
   if( p_sm->si_disp_h == CL_DISP_INVALID_HANDLE )
      goto Exit;

   p_sm->nd_disp_h = cl_disp_register(p_disp, OSM_MSG_MAD_NODE_DESC,
                                      osm_nd_rcv_process, &p_sm->nd_rcv);
   if( p_sm->nd_disp_h == CL_DISP_INVALID_HANDLE )
      goto Exit;

   p_sm->lft_disp_h = cl_disp_register(p_disp, OSM_MSG_MAD_LFT,
                                       osm_lft_rcv_process, &p_sm->lft_rcv);
   if( p_sm->lft_disp_h == CL_DISP_INVALID_HANDLE )
      goto Exit;

   p_sm->mft_disp_h = cl_disp_register(p_disp, OSM_MSG_MAD_MFT,
                                       osm_mft_rcv_process, &p_sm->mft_rcv);
   if( p_sm->mft_disp_h == CL_DISP_INVALID_HANDLE )
      goto Exit;

   p_sm->sm_info_disp_h = cl_disp_register(p_disp, OSM_MSG_MAD_SM_INFO,
                                           osm_sminfo_rcv_process,
                                           &p_sm->sm_info_rcv);
   if( p_sm->sm_info_disp_h == CL_DISP_INVALID_HANDLE )
      goto Exit;

   p_sm->trap_disp_h = cl_disp_register(p_disp, OSM_MSG_MAD_NOTICE,
                                        osm_trap_rcv_process, &p_sm->trap_rcv);
   if( p_sm->trap_disp_h == CL_DISP_INVALID_HANDLE )
      goto Exit;

   p_sm->slvl_disp_h = cl_disp_register(p_disp, OSM_MSG_MAD_SLVL,
                                        osm_slvl_rcv_process, &p_sm->slvl_rcv);
   if( p_sm->slvl_disp_h == CL_DISP_INVALID_HANDLE )
      goto Exit;

   p_sm->vla_disp_h = cl_disp_register(p_disp, OSM_MSG_MAD_VL_ARB,
                                       osm_vla_rcv_process, &p_sm->vla_rcv);
   if( p_sm->vla_disp_h == CL_DISP_INVALID_HANDLE )
      goto Exit;

   p_sm->pkey_disp_h = cl_disp_register(p_disp, OSM_MSG_MAD_PKEY,
                                        osm_pkey_rcv_process, &p_sm->pkey_rcv);
   if( p_sm->pkey_disp_h == CL_DISP_INVALID_HANDLE )
      goto Exit;

   /*
    * Now that the component objects are initialized, start
    * the sweeper thread if the user wants sweeping.
    */
   if( p_sm->p_subn->opt.sweep_interval )
   {
      p_sm->thread_state = OSM_THREAD_STATE_INIT;
      status = cl_thread_init( &p_sm->sweeper, __osm_sm_sweeper, p_sm,
                               "opensm sweeper" );
      if( status != IB_SUCCESS )
         goto Exit;
   }

 Exit:
   OSM_LOG_EXIT( p_log );
   return ( status );
}

/**********************************************************************
 **********************************************************************/
void
osm_sm_sweep(
   IN osm_sm_t * const p_sm )
{
   OSM_LOG_ENTER( p_sm->p_log, osm_sm_sweep );
   osm_state_mgr_process( &p_sm->state_mgr, OSM_SIGNAL_SWEEP );
   OSM_LOG_EXIT( p_sm->p_log );
}

/**********************************************************************
 **********************************************************************/
ib_api_status_t
osm_sm_bind(
   IN osm_sm_t * const p_sm,
   IN const ib_net64_t port_guid )
{
   ib_api_status_t status;

   OSM_LOG_ENTER( p_sm->p_log, osm_sm_bind );

   status = osm_sm_mad_ctrl_bind( &p_sm->mad_ctrl, port_guid );

   if( status != IB_SUCCESS )
   {
      osm_log( p_sm->p_log, OSM_LOG_ERROR,
               "osm_sm_bind: ERR 2E10: "
               "SM MAD Controller bind failed (%s)\n",
               ib_get_err_str( status ) );
      goto Exit;
   }

 Exit:
   OSM_LOG_EXIT( p_sm->p_log );
   return ( status );
}

/**********************************************************************
 **********************************************************************/
static ib_api_status_t
__osm_sm_mgrp_connect(
   IN osm_sm_t * const p_sm,
   IN osm_mgrp_t * const p_mgrp,
   IN const ib_net64_t port_guid,
   IN osm_mcast_req_type_t req_type )
{
   ib_api_status_t status;
   osm_mcast_mgr_ctxt_t *ctx2;

   OSM_LOG_ENTER( p_sm->p_log, __osm_sm_mgrp_connect );

   /*
    * 'Schedule' all the QP0 traffic for when the state manager
    * isn't busy trying to do something else.
    */
   ctx2 =
      ( osm_mcast_mgr_ctxt_t * ) malloc( sizeof( osm_mcast_mgr_ctxt_t ) );
   memcpy( &ctx2->mlid, &p_mgrp->mlid, sizeof( p_mgrp->mlid ) );
   ctx2->req_type = req_type;
   ctx2->port_guid = port_guid;

   status = osm_state_mgr_process_idle( &p_sm->state_mgr,
                                        osm_mcast_mgr_process_mgrp_cb,
                                        NULL, &p_sm->mcast_mgr,
                                        ( void * )ctx2 );

   OSM_LOG_EXIT( p_sm->p_log );
   return ( status );
}

/**********************************************************************
 **********************************************************************/
static void
__osm_sm_mgrp_disconnect(
   IN osm_sm_t * const p_sm,
   IN osm_mgrp_t * const p_mgrp,
   IN const ib_net64_t port_guid )
{
   ib_api_status_t status;
   osm_mcast_mgr_ctxt_t *ctx2;

   OSM_LOG_ENTER( p_sm->p_log, __osm_sm_mgrp_disconnect );

   /*
    * 'Schedule' all the QP0 traffic for when the state manager
    * isn't busy trying to do something else.
    */
   ctx2 =
      ( osm_mcast_mgr_ctxt_t * ) malloc( sizeof( osm_mcast_mgr_ctxt_t ) );
   memcpy( &ctx2->mlid, &p_mgrp->mlid, sizeof( p_mgrp->mlid ) );
   ctx2->req_type = OSM_MCAST_REQ_TYPE_LEAVE;
   ctx2->port_guid = port_guid;

   status = osm_state_mgr_process_idle( &p_sm->state_mgr,
                                        osm_mcast_mgr_process_mgrp_cb,
                                        NULL, &p_sm->mcast_mgr, ctx2 );
   if( status != IB_SUCCESS )
   {
      osm_log( p_sm->p_log, OSM_LOG_ERROR,
               "__osm_sm_mgrp_disconnect: ERR 2E11: "
               "Failure processing multicast group (%s)\n",
               ib_get_err_str( status ) );
   }

   OSM_LOG_EXIT( p_sm->p_log );
}

/**********************************************************************
 **********************************************************************/
ib_api_status_t
osm_sm_mcgrp_join(
   IN osm_sm_t * const p_sm,
   IN const ib_net16_t mlid,
   IN const ib_net64_t port_guid,
   IN osm_mcast_req_type_t req_type )
{
   osm_mgrp_t *p_mgrp;
   osm_port_t *p_port;
   cl_qmap_t *p_tbl;
   ib_api_status_t status = IB_SUCCESS;
   osm_mcm_info_t *p_mcm;

   OSM_LOG_ENTER( p_sm->p_log, osm_sm_mcgrp_join );

   osm_log( p_sm->p_log, OSM_LOG_VERBOSE,
            "osm_sm_mcgrp_join: "
            "Port 0x%016" PRIx64 " joining MLID 0x%X\n",
            cl_ntoh64( port_guid ), cl_ntoh16( mlid ) );

   /*
    * Acquire the port object for the port joining this group.
    */
   CL_PLOCK_EXCL_ACQUIRE( p_sm->p_lock );
   p_port = osm_get_port_by_guid( p_sm->p_subn, port_guid );
   if( !p_port )
   {
      CL_PLOCK_RELEASE( p_sm->p_lock );
      osm_log( p_sm->p_log, OSM_LOG_ERROR,
               "osm_sm_mcgrp_join: ERR 2E05: "
               "No port object for port 0x%016" PRIx64 "\n",
               cl_ntoh64( port_guid ) );
      status = IB_INVALID_PARAMETER;
      goto Exit;
   }

   /*
    * If this multicast group does not already exist, create it.
    */
   p_tbl = &p_sm->p_subn->mgrp_mlid_tbl;
   p_mgrp = ( osm_mgrp_t * ) cl_qmap_get( p_tbl, mlid );
   if( p_mgrp == ( osm_mgrp_t * ) cl_qmap_end( p_tbl ) )
   {
      osm_log( p_sm->p_log, OSM_LOG_VERBOSE,
               "osm_sm_mcgrp_join: "
               "Creating group, MLID 0x%X\n", cl_ntoh16( mlid ) );

      p_mgrp = osm_mgrp_new( mlid );
      if( p_mgrp == NULL )
      {
         CL_PLOCK_RELEASE( p_sm->p_lock );
         osm_log( p_sm->p_log, OSM_LOG_ERROR,
                  "osm_sm_mcgrp_join: ERR 2E06: "
                  "Unable to allocate multicast group object\n" );
         status = IB_INSUFFICIENT_MEMORY;
         goto Exit;
      }

      cl_qmap_insert( p_tbl, mlid, &p_mgrp->map_item );
   }
   else
   {
      /*
       * The group already exists.  If the port is not a
       * member of the group, then fail immediately.
       * This can happen since the spinlock is released briefly
       * before the SA calls this function.
       */
      if( !osm_mgrp_is_guid( p_mgrp, port_guid ) )
      {
         CL_PLOCK_RELEASE( p_sm->p_lock );
         osm_log( p_sm->p_log, OSM_LOG_ERROR,
                  "osm_sm_mcgrp_join: ERR 2E12: "
                  "Port 0x%016" PRIx64 " not in mcast group 0x%X\n",
                  cl_ntoh64( port_guid ), cl_ntoh16( mlid ) );
         status = IB_NOT_FOUND;
         goto Exit;
      }
   }

   /* 
    * Check if the object (according to mlid) already exists on this port.
    * If it does - then no need to update it again, and no need to 
    * create the mc tree again. Just goto Exit.
    */
   p_mcm = ( osm_mcm_info_t * ) cl_qlist_head( &p_port->mcm_list );
   while( p_mcm != ( osm_mcm_info_t * ) cl_qlist_end( &p_port->mcm_list ) )
   {
      if( p_mcm->mlid == mlid )
      {
         CL_PLOCK_RELEASE( p_sm->p_lock );
         osm_log( p_sm->p_log, OSM_LOG_DEBUG,
                  "osm_sm_mcgrp_join: "
                  "Found mlid object for Port:"
                  "0x%016" PRIx64 " lid:0x%X\n",
                  cl_ntoh64( port_guid ), cl_ntoh16( mlid ) );
         goto Exit;
      }
      p_mcm = ( osm_mcm_info_t * ) cl_qlist_next( &p_mcm->list_item );
   }

   status = osm_port_add_mgrp( p_port, mlid );
   if( status != IB_SUCCESS )
   {
      CL_PLOCK_RELEASE( p_sm->p_lock );
      osm_log( p_sm->p_log, OSM_LOG_ERROR,
               "osm_sm_mcgrp_join: ERR 2E03: "
               "Unable to associate port 0x%" PRIx64 " to mlid 0x%X\n",
               cl_ntoh64( osm_port_get_guid( p_port ) ),
               cl_ntoh16( osm_mgrp_get_mlid( p_mgrp ) ) );
      goto Exit;
   }

   CL_PLOCK_RELEASE( p_sm->p_lock );
   status = __osm_sm_mgrp_connect( p_sm, p_mgrp, port_guid, req_type );

 Exit:
   OSM_LOG_EXIT( p_sm->p_log );
   return ( status );
}

/**********************************************************************
 **********************************************************************/
ib_api_status_t
osm_sm_mcgrp_leave(
   IN osm_sm_t * const p_sm,
   IN const ib_net16_t mlid,
   IN const ib_net64_t port_guid )
{
   osm_mgrp_t *p_mgrp;
   osm_port_t *p_port;
   cl_qmap_t *p_tbl;
   ib_api_status_t status = IB_SUCCESS;

   OSM_LOG_ENTER( p_sm->p_log, osm_sm_mcgrp_leave );

   osm_log( p_sm->p_log, OSM_LOG_VERBOSE,
            "osm_sm_mcgrp_leave: "
            "Port 0x%" PRIx64 " leaving MLID 0x%X\n",
            cl_ntoh64( port_guid ), cl_ntoh16( mlid ) );

   /*
    * Acquire the port object for the port leaving this group.
    */
   /* note: p_sm->p_lock is locked by caller, but will be released later
      this function */
   p_port = osm_get_port_by_guid( p_sm->p_subn, port_guid );
   if( !p_port )
   {
      CL_PLOCK_RELEASE( p_sm->p_lock );
      osm_log( p_sm->p_log, OSM_LOG_ERROR,
               "osm_sm_mcgrp_leave: ERR 2E04: "
               "No port object for port 0x%" PRIx64 "\n",
               cl_ntoh64( port_guid ) );
      status = IB_INVALID_PARAMETER;
      goto Exit;
   }

   /*
    * Get the multicast group object for this group.
    */
   p_tbl = &p_sm->p_subn->mgrp_mlid_tbl;
   p_mgrp = ( osm_mgrp_t * ) cl_qmap_get( p_tbl, mlid );
   if( p_mgrp == ( osm_mgrp_t * ) cl_qmap_end( p_tbl ) )
   {
      CL_PLOCK_RELEASE( p_sm->p_lock );
      osm_log( p_sm->p_log, OSM_LOG_ERROR,
               "osm_sm_mcgrp_leave: ERR 2E08: "
               "No multicast group for MLID 0x%X\n", cl_ntoh16( mlid ) );
      status = IB_INVALID_PARAMETER;
      goto Exit;
   }

   /*
    * Walk the list of ports in the group, and remove the appropriate one.
    */
   osm_mgrp_remove_port( p_sm->p_subn, p_sm->p_log, p_mgrp, port_guid );

   osm_port_remove_mgrp( p_port, mlid );

   CL_PLOCK_RELEASE( p_sm->p_lock );

   __osm_sm_mgrp_disconnect( p_sm, p_mgrp, port_guid );

 Exit:
   OSM_LOG_EXIT( p_sm->p_log );
   return ( status );
}

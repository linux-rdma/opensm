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
 *    Implementation of osm_sm_state_mgr_t.
 * This file implements the SM State Manager object.
 *
 * Environment:
 *    Linux User Mode
 *
 * $Revision: 1.7 $
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <string.h>
#include <iba/ib_types.h>
#include <complib/cl_passivelock.h>
#include <complib/cl_debug.h>
#include <time.h>
#include <opensm/osm_state_mgr.h>
#include <opensm/osm_madw.h>
#include <opensm/osm_switch.h>
#include <opensm/osm_log.h>
#include <opensm/osm_subnet.h>
#include <opensm/osm_helper.h>
#include <opensm/osm_msgdef.h>
#include <opensm/osm_node.h>
#include <opensm/osm_port.h>
#include <vendor/osm_vendor_api.h>
#include <opensm/osm_sm_state_mgr.h>
#include <opensm/osm_helper.h>
#include <opensm/osm_opensm.h>

/**********************************************************************
 **********************************************************************/
static void
__osm_sm_state_mgr_standby_msg(
   IN const osm_sm_state_mgr_t * p_sm_mgr )
{
   osm_log( p_sm_mgr->p_log, OSM_LOG_SYS, "Entering STANDBY state\n" );   /* Format Waived */

   if( osm_log_is_active( p_sm_mgr->p_log, OSM_LOG_VERBOSE ) )
   {
      osm_log( p_sm_mgr->p_log, OSM_LOG_VERBOSE,
               "__osm_sm_state_mgr_standby_msg: "
               "\n\n\n********************************"
               "**********************************\n"
               "******************** ENTERING SM STANDBY"
               " STATE *******************\n"
               "**************************************"
               "****************************\n\n\n" );
   }
}

/**********************************************************************
 **********************************************************************/
static void
__osm_sm_state_mgr_master_msg(
   IN const osm_sm_state_mgr_t * p_sm_mgr )
{
   osm_log( p_sm_mgr->p_log, OSM_LOG_SYS, "Entering MASTER state\n" );   /* Format Waived */

   if( osm_log_is_active( p_sm_mgr->p_log, OSM_LOG_VERBOSE ) )
   {
      osm_log( p_sm_mgr->p_log, OSM_LOG_VERBOSE,
               "__osm_sm_state_mgr_master_msg: "
               "\n\n\n********************************"
               "**********************************\n"
               "******************** ENTERING SM MASTER"
               " STATE ********************\n"
               "**************************************"
               "****************************\n\n\n" );
   }
}

/**********************************************************************
 **********************************************************************/
static void
__osm_sm_state_mgr_discovering_msg(
   IN const osm_sm_state_mgr_t * p_sm_mgr )
{
   if( osm_log_is_active( p_sm_mgr->p_log, OSM_LOG_VERBOSE ) )
   {
      osm_log( p_sm_mgr->p_log, OSM_LOG_VERBOSE,
               "__osm_sm_state_mgr_discovering_msg: "
               "\n\n\n********************************"
               "**********************************\n"
               "******************** ENTERING SM DISCOVERING"
               " STATE ***************\n"
               "**************************************"
               "****************************\n\n\n" );
   }
}

/**********************************************************************
 **********************************************************************/
static void
__osm_sm_state_mgr_notactive_msg(
   IN const osm_sm_state_mgr_t * p_sm_mgr )
{
   if( osm_log_is_active( p_sm_mgr->p_log, OSM_LOG_VERBOSE ) )
   {
      osm_log( p_sm_mgr->p_log, OSM_LOG_VERBOSE,
               "__osm_sm_state_mgr_notactive_msg: "
               "\n\n\n********************************"
               "**********************************\n"
               "******************** ENTERING SM NOT-ACTIVE"
               " STATE **********************\n"
               "**************************************"
               "****************************\n\n\n" );
   }
}

#if 0
/**********************************************************************
 **********************************************************************/
static void
__osm_sm_state_mgr_send_local_port_info_req(
   IN osm_sm_state_mgr_t * p_sm_mgr )
{
   osm_madw_context_t context;
   osm_port_t *p_port;
   ib_net64_t port_guid = p_sm_mgr->p_subn->sm_port_guid;
   ib_api_status_t status;

   OSM_LOG_ENTER( p_sm_mgr->p_log,
                  __osm_sm_state_mgr_send_local_port_info_req );
   /*
    * Send a query of SubnGet(PortInfo) to our own port, in order to
    * update the master_sm_base_lid of the subnet.
    */
   memset( &context, 0, sizeof( context ) );
   p_port = ( osm_port_t * ) cl_qmap_get( &p_sm_mgr->p_subn->port_guid_tbl,
                                          port_guid );
   if( p_port ==
       ( osm_port_t * ) cl_qmap_end( &p_sm_mgr->p_subn->port_guid_tbl ) )
   {
      osm_log( p_sm_mgr->p_log, OSM_LOG_ERROR,
               "__osm_sm_state_mgr_send_local_port_info_req: ERR 3205: "
               "No port object for port 0x%016" PRIx64 "\n",
               cl_ntoh64( port_guid ) );
      goto Exit;
   }

   context.pi_context.port_guid = port_guid;
   context.pi_context.node_guid = p_port->p_node->node_info.node_guid;
   context.pi_context.set_method = FALSE;
   context.pi_context.ignore_errors = FALSE;
   /* mark the update_master_sm_base_lid with TRUE - we want to update it */
   /* with the new master lid value. */
   context.pi_context.update_master_sm_base_lid = TRUE;
   context.pi_context.light_sweep = FALSE;
   context.pi_context.active_transition = FALSE;

   status = osm_req_get( p_sm_mgr->p_req,
                         osm_physp_get_dr_path_ptr
                         ( osm_port_get_default_phys_ptr( p_port ) ),
                         IB_MAD_ATTR_PORT_INFO,
                         cl_hton32( p_port->default_port_num ),
                         CL_DISP_MSGID_NONE, &context );

   if( status != IB_SUCCESS )
   {
      osm_log( p_sm_mgr->p_log, OSM_LOG_ERROR,
               "__osm_sm_state_mgr_send_local_port_info_req: ERR 3202: "
               "Failure requesting PortInfo (%s)\n",
               ib_get_err_str( status ) );
   }

 Exit:
   OSM_LOG_EXIT( p_sm_mgr->p_log );
}
#endif

/**********************************************************************
 **********************************************************************/
static void
__osm_sm_state_mgr_send_master_sm_info_req(
   IN osm_sm_state_mgr_t * p_sm_mgr )
{
   osm_madw_context_t context;
   const osm_port_t *p_port;
   ib_api_status_t status;

   OSM_LOG_ENTER( p_sm_mgr->p_log,
                  __osm_sm_state_mgr_send_master_sm_info_req );

   memset( &context, 0, sizeof( context ) );
   if( p_sm_mgr->p_subn->sm_state == IB_SMINFO_STATE_STANDBY )
   {
      /*
       * We are in STANDBY state - this means we need to poll on the master
       * SM (according to master_guid)
       * Send a query of SubnGet(SMInfo) to the subn master_sm_base_lid object.
       */
      p_port = ( osm_port_t * ) cl_qmap_get( &p_sm_mgr->p_subn->port_guid_tbl,
                                             p_sm_mgr->master_guid );
   }
   else
   {
      /*
       * We are not in STANDBY - this means we are in MASTER state - so we need
       * to poll on the SM that is saved in p_polling_sm under p_sm_mgr.
       * Send a query of SubnGet(SMInfo) to that SM.
       */
      p_port = p_sm_mgr->p_polling_sm->p_port;
   }
   if( p_port == NULL )
   {
      osm_log( p_sm_mgr->p_log, OSM_LOG_ERROR,
               "__osm_sm_state_mgr_send_master_sm_info_req: ERR 3203: "
               "No port object for GUID 0x%016" PRIx64 "\n",
               cl_ntoh64(p_sm_mgr->master_guid) );
      goto Exit;
   }

   context.smi_context.port_guid = p_port->guid;
   context.smi_context.set_method = FALSE;

   status = osm_req_get( p_sm_mgr->p_req,
                         osm_physp_get_dr_path_ptr
                         ( osm_port_get_default_phys_ptr( p_port ) ),
                         IB_MAD_ATTR_SM_INFO, 0, CL_DISP_MSGID_NONE,
                         &context );

   if( status != IB_SUCCESS )
   {
      osm_log( p_sm_mgr->p_log, OSM_LOG_ERROR,
               "__osm_sm_state_mgr_send_master_sm_info_req: ERR 3204: "
               "Failure rquesting SMInfo (%s)\n", ib_get_err_str( status ) );
   }

 Exit:
   OSM_LOG_EXIT( p_sm_mgr->p_log );
}

/**********************************************************************
 **********************************************************************/
static void
__osm_sm_state_mgr_start_polling(
   IN osm_sm_state_mgr_t * p_sm_mgr )
{
   uint32_t sminfo_polling_timeout =
      p_sm_mgr->p_subn->opt.sminfo_polling_timeout;
   cl_status_t cl_status;

   OSM_LOG_ENTER( p_sm_mgr->p_log, __osm_sm_state_mgr_start_polling );

   /*
    * Init the retry_nubmer back to zero - need to restart counting
    */
   p_sm_mgr->retry_number = 0;

   /*
    * Send a SubnGet(SMInfo) query to the current (or new) master found.
    */
   __osm_sm_state_mgr_send_master_sm_info_req( p_sm_mgr );

   /*
    * Start a timer that will wake up every sminfo_polling_timeout milliseconds.
    * The callback of the timer will send a SubnGet(SMInfo) to the Master SM
    * and restart the timer
    */
   cl_status = cl_timer_start( &p_sm_mgr->polling_timer,
                               sminfo_polling_timeout );
   if( cl_status != CL_SUCCESS )
   {
      osm_log( p_sm_mgr->p_log, OSM_LOG_ERROR,
               "__osm_sm_state_mgr_start_polling : ERR 3210: "
               "Failed to start timer\n" );
   }

   OSM_LOG_EXIT( p_sm_mgr->p_log );
}

/**********************************************************************
 **********************************************************************/
static void
__osm_sm_state_mgr_polling_callback(
   IN void *context )
{
   osm_sm_state_mgr_t *p_sm_mgr = ( osm_sm_state_mgr_t * ) context;
   uint32_t sminfo_polling_timeout =
      p_sm_mgr->p_subn->opt.sminfo_polling_timeout;
   cl_status_t cl_status;

   OSM_LOG_ENTER( p_sm_mgr->p_log, __osm_sm_state_mgr_polling_callback );

   /*
    * We can be here in one of two cases:
    * 1. We are a STANDBY sm polling on the master SM.
    * 2. We are a MASTER sm, waiting for a handover from a remote master sm.
    * If we are not in one of these cases - don't need to restart the poller.
    */
   if( !( ( p_sm_mgr->p_subn->sm_state == IB_SMINFO_STATE_MASTER &&
            p_sm_mgr->p_polling_sm != NULL ) ||
          ( p_sm_mgr->p_subn->sm_state == IB_SMINFO_STATE_STANDBY ) ) )
   {
      goto Exit;
   }

   /*
    * If we are a STANDBY sm and the osm_exit_flag is 1, then let's signal
    * the subnet_up. This is relevant for the case of running only once. In that
    * case - the program is stuck until this signal is received. In other
    * cases - it is not relevant whether or not the signal is on - since we are
    * currently in exit flow
    */
   if( p_sm_mgr->p_subn->sm_state == IB_SMINFO_STATE_STANDBY &&
       osm_exit_flag == 1 )
   {
      osm_log( p_sm_mgr->p_log, OSM_LOG_VERBOSE,
               "__osm_sm_state_mgr_polling_callback : "
               "Signalling subnet_up_event\n" );
      cl_event_signal( p_sm_mgr->p_state_mgr->p_subnet_up_event );
      goto Exit;
   }

   /*
    * Incr the retry number.
    * If it reached the max_retry_number in the subnet opt - call
    * osm_sm_state_mgr_process with signal OSM_SM_SIGNAL_POLLING_TIMEOUT
    */
   p_sm_mgr->retry_number++;
   osm_log( p_sm_mgr->p_log, OSM_LOG_VERBOSE,
            "__osm_sm_state_mgr_polling_callback : "
            "Retry number:%d\n", p_sm_mgr->retry_number );

   if( p_sm_mgr->retry_number >= p_sm_mgr->p_subn->opt.polling_retry_number )
   {
      osm_log( p_sm_mgr->p_log, OSM_LOG_DEBUG,
               "__osm_sm_state_mgr_polling_callback : "
               "Reached polling_retry_number value in retry_number. "
               "Go to DISCOVERY state\n" );
      osm_sm_state_mgr_process( p_sm_mgr, OSM_SM_SIGNAL_POLLING_TIMEOUT );
      goto Exit;
   }

   /* Send a SubnGet(SMInfo) request to the remote sm (depends on our state) */
   __osm_sm_state_mgr_send_master_sm_info_req( p_sm_mgr );

   /* restart the timer */
   cl_status = cl_timer_start( &p_sm_mgr->polling_timer,
                               sminfo_polling_timeout );
   if( cl_status != CL_SUCCESS )
   {
      osm_log( p_sm_mgr->p_log, OSM_LOG_ERROR,
               "__osm_sm_state_mgr_polling_callback : ERR 3211: "
               "Failed to restart timer\n" );
   }

 Exit:
   OSM_LOG_EXIT( p_sm_mgr->p_log );
   return;
}

/**********************************************************************
 **********************************************************************/
void
osm_sm_state_mgr_construct(
   IN osm_sm_state_mgr_t * const p_sm_mgr )
{
   memset( p_sm_mgr, 0, sizeof( *p_sm_mgr ) );
   cl_spinlock_construct( &p_sm_mgr->state_lock );
   cl_timer_construct( &p_sm_mgr->polling_timer );
}

/**********************************************************************
 **********************************************************************/
void
osm_sm_state_mgr_destroy(
   IN osm_sm_state_mgr_t * const p_sm_mgr )
{
   CL_ASSERT( p_sm_mgr );

   OSM_LOG_ENTER( p_sm_mgr->p_log, osm_sm_state_mgr_destroy );

   cl_spinlock_destroy( &p_sm_mgr->state_lock );
   cl_timer_destroy( &p_sm_mgr->polling_timer );

   OSM_LOG_EXIT( p_sm_mgr->p_log );
}

/**********************************************************************
 **********************************************************************/
ib_api_status_t
osm_sm_state_mgr_init(
   IN osm_sm_state_mgr_t * const p_sm_mgr,
   IN osm_state_mgr_t * const p_state_mgr,
   IN osm_subn_t * const p_subn,
   IN osm_req_t * const p_req,
   IN osm_log_t * const p_log )
{
   cl_status_t status;

   OSM_LOG_ENTER( p_log, osm_sm_state_mgr_init );

   CL_ASSERT( p_subn );
   CL_ASSERT( p_state_mgr );
   CL_ASSERT( p_req );

   osm_sm_state_mgr_construct( p_sm_mgr );

   p_sm_mgr->p_log = p_log;
   p_sm_mgr->p_req = p_req;
   p_sm_mgr->p_subn = p_subn;
   p_sm_mgr->p_state_mgr = p_state_mgr;

   /* init the state of the SM to idle */
   p_sm_mgr->p_subn->sm_state = IB_SMINFO_STATE_INIT;

   status = cl_spinlock_init( &p_sm_mgr->state_lock );
   if( status != CL_SUCCESS )
   {
      osm_log( p_sm_mgr->p_log, OSM_LOG_ERROR,
               "osm_sm_state_mgr_init: ERR 3201: "
               "Spinlock init failed (%s)\n", CL_STATUS_MSG( status ) );
   }

   status = cl_timer_init( &p_sm_mgr->polling_timer,
                           __osm_sm_state_mgr_polling_callback, p_sm_mgr );

   if( status != CL_SUCCESS )
   {
      osm_log( p_sm_mgr->p_log, OSM_LOG_ERROR,
               "osm_sm_state_mgr_init: ERR 3206: "
               "Timer init failed (%s)\n", CL_STATUS_MSG( status ) );
   }

   OSM_LOG_EXIT( p_sm_mgr->p_log );
   return ( status );
}

/**********************************************************************
 **********************************************************************/
void
__osm_sm_state_mgr_signal_error(
   IN const osm_sm_state_mgr_t * const p_sm_mgr,
   IN const osm_sm_signal_t signal )
{
   osm_log( p_sm_mgr->p_log, OSM_LOG_ERROR,
            "__osm_sm_state_mgr_signal_error: ERR 3207: "
            "Invalid signal %s in state %s\n",
            osm_get_sm_mgr_signal_str( signal ),
            osm_get_sm_mgr_state_str( p_sm_mgr->p_subn->sm_state ) );
}

/**********************************************************************
 **********************************************************************/
void
osm_sm_state_mgr_signal_master_is_alive(
   IN osm_sm_state_mgr_t * const p_sm_mgr )
{
   OSM_LOG_ENTER( p_sm_mgr->p_log, osm_sm_state_mgr_signal_master_is_alive );
   p_sm_mgr->retry_number = 0;
   OSM_LOG_EXIT( p_sm_mgr->p_log );
}

/**********************************************************************
 **********************************************************************/
ib_api_status_t
osm_sm_state_mgr_process(
   IN osm_sm_state_mgr_t * const p_sm_mgr,
   IN osm_sm_signal_t signal )
{
   ib_api_status_t status = IB_SUCCESS;

   CL_ASSERT( p_sm_mgr );

   OSM_LOG_ENTER( p_sm_mgr->p_log, osm_sm_state_mgr_process );

   /*
    * The state lock prevents many race conditions from screwing
    * up the state transition process.
    */
   cl_spinlock_acquire( &p_sm_mgr->state_lock );

   if( osm_log_is_active( p_sm_mgr->p_log, OSM_LOG_DEBUG ) )
   {
      osm_log( p_sm_mgr->p_log, OSM_LOG_DEBUG,
               "osm_sm_state_mgr_process: "
               "Received signal %s in state %s\n",
               osm_get_sm_mgr_signal_str( signal ),
               osm_get_sm_mgr_state_str( p_sm_mgr->p_subn->sm_state ) );
   }

   switch ( p_sm_mgr->p_subn->sm_state )
   {
   case IB_SMINFO_STATE_INIT:
      switch ( signal )
      {
      case OSM_SM_SIGNAL_INIT:
         /*
          * Update the state of the SM to DISCOVERING
          */
         __osm_sm_state_mgr_discovering_msg( p_sm_mgr );
         p_sm_mgr->p_subn->sm_state = IB_SMINFO_STATE_DISCOVERING;
         break;

      default:
         __osm_sm_state_mgr_signal_error( p_sm_mgr, signal );
         status = IB_INVALID_PARAMETER;
         break;
      }
      break;

   case IB_SMINFO_STATE_DISCOVERING:
      switch ( signal )
      {
      case OSM_SM_SIGNAL_DISCOVERY_COMPLETED:
         /*
          * Update the state of the SM to MASTER
          */
         __osm_sm_state_mgr_master_msg( p_sm_mgr );
         /* Turn on the moved_to_master_state flag */
         p_sm_mgr->p_subn->moved_to_master_state = TRUE;
         /* Turn on the first_time_master_sweep flag */
         if( p_sm_mgr->p_subn->first_time_master_sweep == FALSE )
            p_sm_mgr->p_subn->first_time_master_sweep = TRUE;

         p_sm_mgr->p_subn->sm_state = IB_SMINFO_STATE_MASTER;
         /*
          * Make sure to set the subnet master_sm_base_lid
          * to the sm_base_lid value
          */
         p_sm_mgr->p_subn->master_sm_base_lid = p_sm_mgr->p_subn->sm_base_lid;
         break;
      case OSM_SM_SIGNAL_MASTER_OR_HIGHER_SM_DETECTED:
         /*
          * Stop the discovering
          */
         osm_state_mgr_process( p_sm_mgr->p_state_mgr,
                                OSM_SIGNAL_MASTER_OR_HIGHER_SM_DETECTED );
         break;
      case OSM_SM_SIGNAL_MASTER_OR_HIGHER_SM_DETECTED_DONE:
         /*
          * Finished all discovery actions - move to STANDBY
          * start the polling
          */
         __osm_sm_state_mgr_standby_msg( p_sm_mgr );
         p_sm_mgr->p_subn->sm_state = IB_SMINFO_STATE_STANDBY;
         /*
          * Since another SM is doing the LFT config - we should not
          * ignore the results of it
          */
         p_sm_mgr->p_subn->ignore_existing_lfts = FALSE;

         __osm_sm_state_mgr_start_polling( p_sm_mgr );
         break;
      case OSM_SM_SIGNAL_HANDOVER:
        /*
         * Do nothing. We will discover it later on. If we already discovered
         * this SM, and got the HANDOVER - this means the remote SM is of 
         * lower priority. In this case we will stop polling it (since it is
         * a lower priority SM in STANDBY state).
         */
         break;
      default:
         __osm_sm_state_mgr_signal_error( p_sm_mgr, signal );
         status = IB_INVALID_PARAMETER;
         break;
      }
      break;

   case IB_SMINFO_STATE_STANDBY:
      switch ( signal )
      {
      case OSM_SM_SIGNAL_POLLING_TIMEOUT:
      case OSM_SM_SIGNAL_DISCOVER:
         /*
          * case 1: Polling timeout occured - this means that the Master SM
          * no longer alive.
          * case 2: Got a signal to move to DISCOVERING
          * Move to DISCOVERING state, and start sweeping
          */
         __osm_sm_state_mgr_discovering_msg( p_sm_mgr );
         p_sm_mgr->p_subn->sm_state = IB_SMINFO_STATE_DISCOVERING;
         p_sm_mgr->p_subn->coming_out_of_standby = TRUE;
         osm_state_mgr_process( p_sm_mgr->p_state_mgr, OSM_SIGNAL_EXIT_STBY );
         break;
      case OSM_SM_SIGNAL_DISABLE:
         /*
          * Update the state to NOT_ACTIVE
          */
         __osm_sm_state_mgr_notactive_msg( p_sm_mgr );
         p_sm_mgr->p_subn->sm_state = IB_SMINFO_STATE_NOTACTIVE;
         break;
      case OSM_SM_SIGNAL_HANDOVER:
         /*
          * Update state to MASTER, and start sweeping
          * OPTIONAL: send ACKNOWLEDGE
          */
         __osm_sm_state_mgr_master_msg( p_sm_mgr );
         /* Turn on the moved_to_master_state flag */
         p_sm_mgr->p_subn->moved_to_master_state = TRUE;
         /* Turn on the first_time_master_sweep flag */
         if( p_sm_mgr->p_subn->first_time_master_sweep == FALSE )
            p_sm_mgr->p_subn->first_time_master_sweep = TRUE;
         /* Turn on the force_immediate_heavy_sweep - we want a
          * heavy sweep to occur on the first sweep of this SM. */
         p_sm_mgr->p_subn->force_immediate_heavy_sweep = TRUE;

         p_sm_mgr->p_subn->sm_state = IB_SMINFO_STATE_MASTER;
         /*
          * Make sure to set the subnet master_sm_base_lid
          * to the sm_base_lid value
          */
         p_sm_mgr->p_subn->master_sm_base_lid = p_sm_mgr->p_subn->sm_base_lid;
         p_sm_mgr->p_subn->coming_out_of_standby = TRUE;
         osm_state_mgr_process( p_sm_mgr->p_state_mgr, OSM_SIGNAL_EXIT_STBY );
         break;
      case OSM_SM_SIGNAL_ACKNOWLEDGE:
         /*
          * Do nothing - already moved to STANDBY
          */
         break;
      default:
         __osm_sm_state_mgr_signal_error( p_sm_mgr, signal );
         status = IB_INVALID_PARAMETER;
         break;
      }
      break;

   case IB_SMINFO_STATE_NOTACTIVE:
      switch ( signal )
      {
      case OSM_SM_SIGNAL_STANDBY:
         /*
          * Update the state to STANDBY
          * start the polling
          */
         __osm_sm_state_mgr_standby_msg( p_sm_mgr );
         p_sm_mgr->p_subn->sm_state = IB_SMINFO_STATE_STANDBY;
         __osm_sm_state_mgr_start_polling( p_sm_mgr );
         break;
      default:
         __osm_sm_state_mgr_signal_error( p_sm_mgr, signal );
         status = IB_INVALID_PARAMETER;
         break;
      }
      break;

   case IB_SMINFO_STATE_MASTER:
      switch ( signal )
      {
      case OSM_SM_SIGNAL_POLLING_TIMEOUT:
         /*
          * we received a polling timeout - this means that we waited for
          * a remote master sm to send us a handover, but didn't get it, and
          * didn't get a response from that remote sm.
          * We want to force a heavy sweep - hopefully this occurred because
          * the remote sm died, and we'll find this out and configure the
          * subnet after a heavy sweep.
          * We also want to clear the p_polling_sm object - since we are
          * done polling on that remote sm - we are sweeping again.
          */
      case OSM_SM_SIGNAL_HANDOVER:
         /*
          * If we received a handover in a master state - then we want to
          * force a heavy sweep. This means that either we are in a sweep
          * currently - in this case - no change, or we are in idle state -
          * since we recognized a master SM before - so we want to make a
          * heavy sweep and reconfigure the new subnet.
          * We also want to clear the p_polling_sm object - since we are
          * done polling on that remote sm - we got a handover from it.
          */
         osm_log( p_sm_mgr->p_log, OSM_LOG_VERBOSE,
                  "osm_sm_state_mgr_process: "
                  "Forcing immediate heavy sweep. "
                  "Received OSM_SM_SIGNAL_HANDOVER or OSM_SM_SIGNAL_POLLING_TIMEOUT\n" );
         p_sm_mgr->p_polling_sm = NULL;
         p_sm_mgr->p_subn->force_immediate_heavy_sweep = TRUE;
         osm_state_mgr_process( p_sm_mgr->p_state_mgr, OSM_SIGNAL_SWEEP );
         break;
      case OSM_SM_SIGNAL_HANDOVER_SENT:
         /*
          * Just sent a HANDOVER signal - move to STANDBY
          * start the polling
          */
         __osm_sm_state_mgr_standby_msg( p_sm_mgr );
         p_sm_mgr->p_subn->sm_state = IB_SMINFO_STATE_STANDBY;
         __osm_sm_state_mgr_start_polling( p_sm_mgr );
         break;
      case OSM_SM_SIGNAL_WAIT_FOR_HANDOVER:
         /*
          * We found a remote master SM, and we are waiting for it
          * to handover the mastership to us. Need to start polling
          * on that SM, to make sure it is alive, if it isn't - then
          * we should move back to discovering, since something must
          * have happened to it.
          */
         __osm_sm_state_mgr_start_polling( p_sm_mgr );
         break;
      case OSM_SM_SIGNAL_DISCOVER:
         __osm_sm_state_mgr_discovering_msg( p_sm_mgr );
         p_sm_mgr->p_subn->sm_state = IB_SMINFO_STATE_DISCOVERING;
         break;
      default:
         __osm_sm_state_mgr_signal_error( p_sm_mgr, signal );
         status = IB_INVALID_PARAMETER;
         break;
      }
      break;

   default:
      osm_log( p_sm_mgr->p_log, OSM_LOG_ERROR,
               "osm_sm_state_mgr_process: ERR 3208: "
               "Invalid state %s\n",
               osm_get_sm_mgr_state_str( p_sm_mgr->p_subn->sm_state ) );

   }

   cl_spinlock_release( &p_sm_mgr->state_lock );

   OSM_LOG_EXIT( p_sm_mgr->p_log );
   return ( status );
}

/**********************************************************************
 **********************************************************************/
ib_api_status_t
osm_sm_state_mgr_check_legality(
   IN osm_sm_state_mgr_t * const p_sm_mgr,
   IN osm_sm_signal_t signal )
{
   ib_api_status_t status = IB_SUCCESS;

   CL_ASSERT( p_sm_mgr );

   OSM_LOG_ENTER( p_sm_mgr->p_log, osm_sm_state_mgr_check_legality );

   /*
    * The state lock prevents many race conditions from screwing
    * up the state transition process.
    */
   cl_spinlock_acquire( &p_sm_mgr->state_lock );

   if( osm_log_is_active( p_sm_mgr->p_log, OSM_LOG_DEBUG ) )
   {
      osm_log( p_sm_mgr->p_log, OSM_LOG_DEBUG,
               "osm_sm_state_mgr_check_legality: "
               "Received signal %s in state %s\n",
               osm_get_sm_mgr_signal_str( signal ),
               osm_get_sm_mgr_state_str( p_sm_mgr->p_subn->sm_state ) );
   }

   switch ( p_sm_mgr->p_subn->sm_state )
   {
   case IB_SMINFO_STATE_INIT:
      switch ( signal )
      {
      case OSM_SM_SIGNAL_INIT:
         status = IB_SUCCESS;
         break;
      default:
         __osm_sm_state_mgr_signal_error( p_sm_mgr, signal );
         status = IB_INVALID_PARAMETER;
         break;
      }
      break;

   case IB_SMINFO_STATE_DISCOVERING:
      switch ( signal )
      {
      case OSM_SM_SIGNAL_DISCOVERY_COMPLETED:
      case OSM_SM_SIGNAL_MASTER_OR_HIGHER_SM_DETECTED:
      case OSM_SM_SIGNAL_MASTER_OR_HIGHER_SM_DETECTED_DONE:
      case OSM_SM_SIGNAL_HANDOVER:
         status = IB_SUCCESS;
         break;
      default:
         __osm_sm_state_mgr_signal_error( p_sm_mgr, signal );
         status = IB_INVALID_PARAMETER;
         break;
      }
      break;

   case IB_SMINFO_STATE_STANDBY:
      switch ( signal )
      {
      case OSM_SM_SIGNAL_POLLING_TIMEOUT:
      case OSM_SM_SIGNAL_DISCOVER:
      case OSM_SM_SIGNAL_DISABLE:
      case OSM_SM_SIGNAL_HANDOVER:
      case OSM_SM_SIGNAL_ACKNOWLEDGE:
         status = IB_SUCCESS;
         break;
      default:
         __osm_sm_state_mgr_signal_error( p_sm_mgr, signal );
         status = IB_INVALID_PARAMETER;
         break;
      }
      break;

   case IB_SMINFO_STATE_NOTACTIVE:
      switch ( signal )
      {
      case OSM_SM_SIGNAL_STANDBY:
         status = IB_SUCCESS;
         break;
      default:
         __osm_sm_state_mgr_signal_error( p_sm_mgr, signal );
         status = IB_INVALID_PARAMETER;
         break;
      }
      break;

   case IB_SMINFO_STATE_MASTER:
      switch ( signal )
      {
      case OSM_SM_SIGNAL_HANDOVER:
      case OSM_SM_SIGNAL_HANDOVER_SENT:
         status = IB_SUCCESS;
         break;
      default:
         __osm_sm_state_mgr_signal_error( p_sm_mgr, signal );
         status = IB_INVALID_PARAMETER;
         break;
      }
      break;

   default:
      osm_log( p_sm_mgr->p_log, OSM_LOG_ERROR,
               "osm_sm_state_mgr_check_legality: ERR 3209: "
               "Invalid state %s\n",
               osm_get_sm_mgr_state_str( p_sm_mgr->p_subn->sm_state ) );
      status = IB_INVALID_PARAMETER;

   }

   cl_spinlock_release( &p_sm_mgr->state_lock );

   OSM_LOG_EXIT( p_sm_mgr->p_log );
   return ( status );
}

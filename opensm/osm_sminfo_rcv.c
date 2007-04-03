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
 *    Implementation of osm_sminfo_rcv_t.
 * This object represents the SMInfo Receiver object.
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

#include <stdlib.h>
#include <string.h>
#include <iba/ib_types.h>
#include <complib/cl_qmap.h>
#include <complib/cl_passivelock.h>
#include <complib/cl_debug.h>
#include <opensm/osm_sminfo_rcv.h>
#include <opensm/osm_madw.h>
#include <opensm/osm_log.h>
#include <opensm/osm_node.h>
#include <opensm/osm_helper.h>
#include <opensm/osm_subnet.h>
#include <opensm/osm_sm_state_mgr.h>
#include <opensm/osm_opensm.h>

/**********************************************************************
 **********************************************************************/
void
osm_sminfo_rcv_construct(
  IN osm_sminfo_rcv_t* const p_rcv )
{
  memset( p_rcv, 0, sizeof(*p_rcv) );
}

/**********************************************************************
 **********************************************************************/
void
osm_sminfo_rcv_destroy(
  IN osm_sminfo_rcv_t* const p_rcv )
{
  CL_ASSERT( p_rcv );

  OSM_LOG_ENTER( p_rcv->p_log, osm_sminfo_rcv_destroy );

  OSM_LOG_EXIT( p_rcv->p_log );
}

/**********************************************************************
 **********************************************************************/
ib_api_status_t
osm_sminfo_rcv_init(
  IN osm_sminfo_rcv_t* const p_rcv,
  IN osm_subn_t* const p_subn,
  IN osm_stats_t* const p_stats,
  IN osm_resp_t* const p_resp,
  IN osm_log_t* const p_log,
  IN osm_state_mgr_t* const p_state_mgr,
  IN osm_sm_state_mgr_t* const p_sm_state_mgr,
  IN cl_plock_t* const p_lock )
{
  ib_api_status_t status = IB_SUCCESS;

  OSM_LOG_ENTER( p_log, osm_sminfo_rcv_init );

  osm_sminfo_rcv_construct( p_rcv );

  p_rcv->p_log = p_log;
  p_rcv->p_subn = p_subn;
  p_rcv->p_lock = p_lock;
  p_rcv->p_stats = p_stats;
  p_rcv->p_resp = p_resp;
  p_rcv->p_state_mgr = p_state_mgr;
  p_rcv->p_sm_state_mgr = p_sm_state_mgr;

  OSM_LOG_EXIT( p_rcv->p_log );
  return( status );
}

/**********************************************************************
 Return TRUE if the remote sm given (by ib_sm_info_t) is higher,
 return FALSE otherwise.
 By higher - we mean: SM with higher priority or with same priority
 and lower GUID.
**********************************************************************/
static inline boolean_t
__osm_sminfo_rcv_remote_sm_is_higher(
  IN const osm_sminfo_rcv_t* p_rcv,
  IN const ib_sm_info_t*     p_remote_sm )
{

  return( osm_sm_is_greater_than( ib_sminfo_get_priority( p_remote_sm ),
      p_remote_sm->guid,
      p_rcv->p_subn->opt.sm_priority,
      p_rcv->p_subn->sm_port_guid) );

}

/**********************************************************************
 **********************************************************************/
static void
__osm_sminfo_rcv_process_get_request(
  IN const osm_sminfo_rcv_t*  const p_rcv,
  IN const osm_madw_t*     const p_madw )
{
  uint8_t                  payload[IB_SMP_DATA_SIZE];
  ib_smp_t*                p_smp;
  ib_sm_info_t*            p_smi = (ib_sm_info_t*)payload;
  ib_api_status_t          status;
  ib_sm_info_t*            p_remote_smi;
  
  OSM_LOG_ENTER( p_rcv->p_log, __osm_sminfo_rcv_process_get_request );

  CL_ASSERT( p_madw );

  /*
    No real need to grab the lock for this function.
  */
  memset( payload, 0, sizeof( payload ) );

  p_smp = osm_madw_get_smp_ptr( p_madw );

  CL_ASSERT( p_smp->method == IB_MAD_METHOD_GET );

  p_smi->guid = p_rcv->p_subn->sm_port_guid;
  p_smi->act_count = cl_hton32( p_rcv->p_stats->qp0_mads_sent );
  p_smi->pri_state = (uint8_t)(p_rcv->p_subn->sm_state |
                               p_rcv->p_subn->opt.sm_priority << 4);
  /*
    p.750 row 11 - Return 0 for the SM key unless we authenticate the 
    requester as the master SM.
  */
  p_remote_smi = ib_smp_get_payload_ptr ( osm_madw_get_smp_ptr (p_madw) );
  if (ib_sminfo_get_state( p_remote_smi ) == IB_SMINFO_STATE_MASTER )
  {
    osm_log( p_rcv->p_log, OSM_LOG_DEBUG,
             "__osm_sminfo_rcv_process_get_request: "
             "Responding to master SM with real sm_key\n" );
    p_smi->sm_key = p_rcv->p_subn->opt.sm_key;
  }
  else 
  {
    /* The requester is not authenticated as master - set sm_key to zero. */
    osm_log( p_rcv->p_log, OSM_LOG_DEBUG,
             "__osm_sminfo_rcv_process_get_request: "
             "Responding to SM not master with zero sm_key\n" );
    p_smi->sm_key = 0;
  }

  status = osm_resp_send( p_rcv->p_resp, p_madw, 0, payload );
  if( status != IB_SUCCESS )
  {
    osm_log( p_rcv->p_log, OSM_LOG_ERROR,
             "__osm_sminfo_rcv_process_get_request: ERR 2F02: "
             "Error sending response (%s)\n",
             ib_get_err_str( status ) );
    goto Exit;
  }

 Exit:
  OSM_LOG_EXIT( p_rcv->p_log );
}

/**********************************************************************
 * Check if the p_smp received is legal.
 * Current checks:
 *   MADHeader:AttributeModifiers of ACKNOWLEDGE that was not sent by a
 *             Standby SM.
 *   MADHeader:AttributeModifiers of HANDOVER/DISABLE/STANDBY/DISCOVER
 *             that was not sent by a Master SM.
 * FUTURE - TO DO:
 *   Check that the SM_Key is matching.
 **********************************************************************/
static ib_api_status_t
__osm_sminfo_rcv_check_set_req_legality(
  IN const ib_smp_t* const p_smp )
{
  ib_sm_info_t*   p_smi;

  p_smi = ib_smp_get_payload_ptr( p_smp );

  if (p_smp->attr_mod == IB_SMINFO_ATTR_MOD_ACKNOWLEDGE)
  {
    if ( ib_sminfo_get_state( p_smi ) == IB_SMINFO_STATE_STANDBY )
    {
      return( IB_SUCCESS );
    }
  }
  else if ( p_smp->attr_mod == IB_SMINFO_ATTR_MOD_HANDOVER ||
            p_smp->attr_mod == IB_SMINFO_ATTR_MOD_DISABLE ||
            p_smp->attr_mod == IB_SMINFO_ATTR_MOD_STANDBY ||
            p_smp->attr_mod == IB_SMINFO_ATTR_MOD_DISCOVER )
  {
    if ( ib_sminfo_get_state( p_smi ) == IB_SMINFO_STATE_MASTER )
    {
      return( IB_SUCCESS );
    }
  }

  return( IB_INVALID_PARAMETER );
}

/**********************************************************************
 **********************************************************************/
static void
__osm_sminfo_rcv_process_set_request(
  IN const osm_sminfo_rcv_t*  const p_rcv,
  IN const osm_madw_t*        const p_madw )
{
  uint8_t                 payload[IB_SMP_DATA_SIZE];
  ib_smp_t*               p_smp;
  ib_sm_info_t*           p_smi = (ib_sm_info_t*)payload;
  ib_sm_info_t*           p_rcv_smi;
  ib_api_status_t         status;
  osm_sm_signal_t         sm_signal;
  ib_sm_info_t*           p_remote_smi;

  OSM_LOG_ENTER( p_rcv->p_log, __osm_sminfo_rcv_process_set_request );

  CL_ASSERT( p_madw );

  /*
    No real need to grab the lock for this function.
  */
  memset( payload, 0, sizeof( payload ) );

  /* get the lock */
  CL_PLOCK_EXCL_ACQUIRE( p_rcv->p_lock );

  p_smp = osm_madw_get_smp_ptr( p_madw );
  p_rcv_smi = ib_smp_get_payload_ptr( p_smp );

  if( p_smp->method != IB_MAD_METHOD_SET )
  {
    osm_log( p_rcv->p_log, OSM_LOG_ERROR,
             "__osm_sminfo_rcv_process_set_request: ERR 2F03: "
             "Unsupported method 0x%X\n",
             p_smp->method );
    CL_PLOCK_RELEASE( p_rcv->p_lock );
    goto Exit;
  }

  p_smi->guid = p_rcv->p_subn->sm_port_guid;
  p_smi->act_count = cl_hton32( p_rcv->p_stats->qp0_mads_sent );
  p_smi->pri_state = (uint8_t)(p_rcv->p_subn->sm_state |
                               p_rcv->p_subn->opt.sm_priority << 4);
  /*
    p.750 row 11 - Return 0 for the SM key unless we authenticate the 
    requester as the master SM.
  */
  p_remote_smi = ib_smp_get_payload_ptr ( osm_madw_get_smp_ptr (p_madw) );
  if (ib_sminfo_get_state( p_remote_smi ) == IB_SMINFO_STATE_MASTER )
  {
    osm_log( p_rcv->p_log, OSM_LOG_DEBUG,
             "__osm_sminfo_rcv_process_set_request: "
             "Responding to master SM with real sm_key\n" );
    p_smi->sm_key = p_rcv->p_subn->opt.sm_key;
  }
  else 
  {
    /* The requester is not authenticated as master - set sm_key to zero. */
    osm_log( p_rcv->p_log, OSM_LOG_DEBUG,
             "__osm_sminfo_rcv_process_set_request: "
             "Responding to SM not master with zero sm_key\n" );
    p_smi->sm_key = 0;
  }

  /* Check the legality of the packet */
  status = __osm_sminfo_rcv_check_set_req_legality( p_smp );
  if ( status != IB_SUCCESS )
  {
    osm_log( p_rcv->p_log, OSM_LOG_ERROR,
             "__osm_sminfo_rcv_process_set_request: ERR 2F04: "
             "Check legality failed. AttributeModifier:0x%X RemoteState:%s\n",
             p_smp->attr_mod,
             osm_get_sm_mgr_state_str(ib_sminfo_get_state( p_rcv_smi ) ) );
    /* send a response with error code */
    status = osm_resp_send( p_rcv->p_resp, p_madw, 7, payload );
    if( status != IB_SUCCESS )
    {
      osm_log( p_rcv->p_log, OSM_LOG_ERROR,
               "__osm_sminfo_rcv_process_set_request: ERR 2F05: "
               "Error sending response (%s)\n",
               ib_get_err_str( status ) );
    }
    CL_PLOCK_RELEASE( p_rcv->p_lock );
    goto Exit;
  }

  /* translate from IB_SMINFO_ATTR to OSM_SM_SIGNAL */
  switch (p_smp->attr_mod)
  {
  case IB_SMINFO_ATTR_MOD_HANDOVER:
    sm_signal = OSM_SM_SIGNAL_HANDOVER;
    break;
  case IB_SMINFO_ATTR_MOD_ACKNOWLEDGE:
    sm_signal = OSM_SM_SIGNAL_ACKNOWLEDGE;
    break;
  case IB_SMINFO_ATTR_MOD_DISABLE:
    sm_signal = OSM_SM_SIGNAL_DISABLE;
    break;
  case IB_SMINFO_ATTR_MOD_STANDBY:
    sm_signal = OSM_SM_SIGNAL_STANDBY;
    break;
  case IB_SMINFO_ATTR_MOD_DISCOVER:
    sm_signal = OSM_SM_SIGNAL_DISCOVER;
    break;
  default:
    /*
      This code shouldn't be reached - checked in the
      check legality
    */
    osm_log( p_rcv->p_log, OSM_LOG_ERROR,
             "__osm_sminfo_rcv_process_set_request: ERR 2F06: "
             "THIS CODE SHOULD NOT BE REACHED!!\n");
    CL_PLOCK_RELEASE( p_rcv->p_lock );
    goto Exit;
  }

  /* check legality of the needed transition in the SM state machine */
  status = osm_sm_state_mgr_check_legality( p_rcv->p_sm_state_mgr,
                                            sm_signal );
  if ( status != IB_SUCCESS )
  {
    osm_log( p_rcv->p_log, OSM_LOG_ERROR,
             "__osm_sminfo_rcv_process_set_request: ERR 2F07: "
             "Check legality of SM needed transition. AttributeModifier:0x%X RemoteState:%s\n",
             p_smp->attr_mod,
             osm_get_sm_mgr_state_str(ib_sminfo_get_state( p_rcv_smi ) ) );
    /* send a response with error code */
    status = osm_resp_send( p_rcv->p_resp, p_madw, 7, payload );
    if( status != IB_SUCCESS )
    {
      osm_log( p_rcv->p_log, OSM_LOG_ERROR,
               "__osm_sminfo_rcv_process_set_request: ERR 2F08: "
               "Error sending response (%s)\n",
               ib_get_err_str( status ) );
    }
    CL_PLOCK_RELEASE( p_rcv->p_lock );
    goto Exit;
  }

  /* the SubnSet(SMInfo) command is ok. Send a response. */
  status = osm_resp_send( p_rcv->p_resp, p_madw, 0, payload );
  if( status != IB_SUCCESS )
  {
    osm_log( p_rcv->p_log, OSM_LOG_ERROR,
             "__osm_sminfo_rcv_process_set_request: ERR 2F09: "
             "Error sending response (%s)\n",
             ib_get_err_str( status ) );
  }

  /* it is a legal packet - act according to it */

  /* if the AttributeModifier is STANDBY - need to save on the */
  /* p_sm_state_mgr in the master_guid variable - the guid of the */
  /* current master. */
  if ( p_smp->attr_mod == IB_SMINFO_ATTR_MOD_STANDBY )
  {
    osm_log( p_rcv->p_log, OSM_LOG_VERBOSE,
             "__osm_sminfo_rcv_process_set_request: "
             "Received a STANDBY signal. Updating "
             "sm_state_mgr master_guid: 0x%016" PRIx64 "\n",
             cl_ntoh64(p_rcv_smi->guid) );
    p_rcv->p_sm_state_mgr->master_guid = p_rcv_smi->guid;
  }

  /* call osm_sm_state_mgr_process with the received signal. */
  CL_PLOCK_RELEASE( p_rcv->p_lock );
  status = osm_sm_state_mgr_process( p_rcv->p_sm_state_mgr,
                                     sm_signal );

  if( status != IB_SUCCESS )
  {
    osm_log( p_rcv->p_log, OSM_LOG_ERROR,
             "__osm_sminfo_rcv_process_set_request: ERR 2F10: "
             "Error in SM state transition (%s)\n",
             ib_get_err_str( status ) );
  }

 Exit:
  OSM_LOG_EXIT( p_rcv->p_log );
}

/**********************************************************************
 * Return a signal with which to call the osm_state_mgr_process.
 * This is done since we are locked by p_rcv->p_lock in this function, 
 * and thus cannot call osm_state_mgr_process (that locks the state_lock).
 * If return OSM_SIGNAL_NONE - do not call osm_state_mgr_process.
 **********************************************************************/
static osm_signal_t
__osm_sminfo_rcv_process_get_sm(
  IN const osm_sminfo_rcv_t*  const p_rcv,
  IN const osm_remote_sm_t*   const p_sm )
{
  const ib_sm_info_t*         p_smi;
  osm_signal_t ret_val = OSM_SIGNAL_NONE;

  OSM_LOG_ENTER( p_rcv->p_log, __osm_sminfo_rcv_process_get_sm );

  p_smi = &p_sm->smi;

  if( osm_log_is_active( p_rcv->p_log, OSM_LOG_VERBOSE ) )
  {
    osm_log( p_rcv->p_log, OSM_LOG_VERBOSE,
             "__osm_sminfo_rcv_process_get_sm: "
             "Detected SM 0x%016" PRIx64 " in state %u\n",
             cl_ntoh64( p_smi->guid ), ib_sminfo_get_state( p_smi ) );
  }

  /*
    Check the state of this SM vs. our own.
  */
  switch( p_rcv->p_subn->sm_state )
  {
  case IB_SMINFO_STATE_NOTACTIVE:
    break;

  case IB_SMINFO_STATE_DISCOVERING:
    switch( ib_sminfo_get_state( p_smi ) )
    {
    case IB_SMINFO_STATE_NOTACTIVE:
      break;
    case IB_SMINFO_STATE_MASTER:
      ret_val = OSM_SIGNAL_MASTER_OR_HIGHER_SM_DETECTED;
      /* save on the p_sm_state_mgr the guid of the current master. */
      osm_log( p_rcv->p_log, OSM_LOG_VERBOSE,
               "__osm_sminfo_rcv_process_get_sm: "
               "Found master SM. Updating sm_state_mgr master_guid: 0x%016" PRIx64 "\n",
               cl_ntoh64( p_sm->p_port->guid ) );
      p_rcv->p_sm_state_mgr->master_guid = p_sm->p_port->guid;
      break;
    case IB_SMINFO_STATE_DISCOVERING:
    case IB_SMINFO_STATE_STANDBY:
      if ( __osm_sminfo_rcv_remote_sm_is_higher(p_rcv, p_smi) == TRUE )
      {
        /* the remote is a higher sm - need to stop sweeping */
        ret_val = OSM_SIGNAL_MASTER_OR_HIGHER_SM_DETECTED;
        /* save on the p_sm_state_mgr the guid of the higher SM we found - */
        /* we will poll it - as long as it lives - we should be in Standby. */
        osm_log( p_rcv->p_log, OSM_LOG_VERBOSE,
                 "__osm_sminfo_rcv_process_get_sm: "
                 "Found higher SM. Updating sm_state_mgr master_guid:"
                 " 0x%016" PRIx64 "\n",
                 cl_ntoh64(p_sm->p_port->guid) );
        p_rcv->p_sm_state_mgr->master_guid = p_sm->p_port->guid;
      }
      break;
    default:
      break;
    }
    break;

  case IB_SMINFO_STATE_STANDBY:
    /* if the guid of the SM that sent us this response is equal to the */
    /* p_sm_mgr->master_guid - then this is a signal that the polling */
    switch( ib_sminfo_get_state( p_smi ) )
    {
    case IB_SMINFO_STATE_MASTER:
      /* This means the master is alive */
      /* Signal that to the SM state mgr */
      osm_sm_state_mgr_signal_master_is_alive( p_rcv->p_sm_state_mgr );
      break;
    case IB_SMINFO_STATE_STANDBY:
      /* This should be the response from the sm we are polling. */
      /* If it is - then signal master is alive */
      if (p_rcv->p_sm_state_mgr->master_guid == p_sm->p_port->guid) 
      {
        /* Make sure that it is an SM with higher priority than us.
           If we started polling it when it was master, and it moved
           to standby - then it might be with a lower priority than
           us - and then we don't want to continue polling it. */
        if ( __osm_sminfo_rcv_remote_sm_is_higher(p_rcv, p_smi) == TRUE )
          osm_sm_state_mgr_signal_master_is_alive( p_rcv->p_sm_state_mgr );
      }
      break;
    default:
      /* any other state - do nothing */
      break;
    }
    break;

  case IB_SMINFO_STATE_MASTER:
    switch( ib_sminfo_get_state( p_smi ) )
    {
    case IB_SMINFO_STATE_MASTER:
      /* If this is a response due to our polling, this means that we are
         waiting for a handover from this SM, and it is still alive - 
         signal that. */
      if ( p_rcv->p_sm_state_mgr->p_polling_sm != NULL )
      {
        osm_sm_state_mgr_signal_master_is_alive( p_rcv->p_sm_state_mgr );
      }
      else
      {
        /* This is a response we got while sweeping the subnet. 
           We will handle a case of handover needed later on, when the sweep
           is done and all SMs are recongnized. */
      }
      break; 
    default:
      /* any other state - do nothing */
      break;
    }
    break;

  default:
    break;
  }

  OSM_LOG_EXIT( p_rcv->p_log );
  return ret_val;
}

/**********************************************************************
 **********************************************************************/
static void
__osm_sminfo_rcv_process_get_response(
  IN const osm_sminfo_rcv_t*  const p_rcv,
  IN const osm_madw_t*     const p_madw )
{
  const ib_smp_t*          p_smp;
  const ib_sm_info_t*      p_smi;
  cl_qmap_t*               p_sm_tbl;
  cl_qmap_t*               p_port_tbl;
  osm_port_t*              p_port;
  ib_net64_t               port_guid;
  osm_remote_sm_t*         p_sm;
  osm_signal_t             process_get_sm_ret_val = OSM_SIGNAL_NONE;

  OSM_LOG_ENTER( p_rcv->p_log, __osm_sminfo_rcv_process_get_response );

  CL_ASSERT( p_madw );

  p_smp = osm_madw_get_smp_ptr( p_madw );

  if( p_smp->method != IB_MAD_METHOD_GET_RESP )
  {
    osm_log( p_rcv->p_log, OSM_LOG_ERROR,
             "__osm_sminfo_rcv_process_get_response: ERR 2F11: "
             "Unsupported method 0x%X\n",
             p_smp->method );
    goto Exit;
  }

  p_smi = ib_smp_get_payload_ptr( p_smp );
  p_sm_tbl = &p_rcv->p_subn->sm_guid_tbl;
  p_port_tbl = &p_rcv->p_subn->port_guid_tbl;
  port_guid = p_smi->guid;

  osm_dump_sm_info( p_rcv->p_log, p_smi, OSM_LOG_DEBUG );

  /* 
     Check that the sm_key of the found SM is the same as ours,
     or is zero. If not - OpenSM cannot continue with configuration!. */
  if ( p_smi->sm_key != 0 && 
       p_smi->sm_key != p_rcv->p_subn->opt.sm_key )
  {
    osm_log( p_rcv->p_log, OSM_LOG_ERROR,
             "__osm_sminfo_rcv_process_get_response: ERR 2F18: "
             "Got SM with sm_key that doesn't match our "
             "local key. Exiting\n" );
    osm_log( p_rcv->p_log, OSM_LOG_SYS,
             "Found remote SM with non-matching sm_key. Exiting\n" );
    osm_exit_flag = TRUE;
    goto Exit;
  }

  /*
    Determine if we already have another SM object for this SM.
  */
  CL_PLOCK_EXCL_ACQUIRE( p_rcv->p_lock );

  p_port = (osm_port_t*)cl_qmap_get( p_port_tbl, port_guid );
  if( p_port == (osm_port_t*)cl_qmap_end( p_port_tbl ) )
  {
    osm_log( p_rcv->p_log, OSM_LOG_ERROR,
             "__osm_sminfo_rcv_process_get_response: ERR 2F12: "
             "No port object for this SM\n" );
    goto Exit;
  }

  if( osm_port_get_guid( p_port ) != p_smi->guid )
  {
    osm_log( p_rcv->p_log, OSM_LOG_ERROR,
             "__osm_sminfo_rcv_process_get_response: ERR 2F13: "
             "Bogus SM port GUID"
             "\n\t\t\t\tExpected 0x%016" PRIx64
             ", Received 0x%016" PRIx64 "\n",
             cl_ntoh64( osm_port_get_guid( p_port ) ),
             cl_ntoh64( p_smi->guid ) );
    goto Exit;
  }

  p_sm = (osm_remote_sm_t*)cl_qmap_get( p_sm_tbl, port_guid );
  if( p_sm == (osm_remote_sm_t*)cl_qmap_end( p_sm_tbl ) )
  {
    p_sm = malloc( sizeof(*p_sm) );
    if( p_sm == NULL )
    {
      osm_log( p_rcv->p_log, OSM_LOG_ERROR,
               "__osm_sminfo_rcv_process_get_response: ERR 2F14: "
               "Unable to allocate SM object\n" );
      goto Exit;
    }

    osm_remote_sm_init( p_sm, p_port, p_smi );

    cl_qmap_insert( p_sm_tbl, port_guid, &p_sm->map_item );
  }
  else
  {
    /*
      We already know this SM.
      Update the SMInfo attribute.
    */
    p_sm->smi = *p_smi;
  }

  process_get_sm_ret_val = __osm_sminfo_rcv_process_get_sm( p_rcv, p_sm );

 Exit:
  CL_PLOCK_RELEASE( p_rcv->p_lock );
  
  /* If process_get_sm_ret_val != OSM_SIGNAL_NONE then we have to signal
   * to the state_mgr with that signal. */
  if (process_get_sm_ret_val != OSM_SIGNAL_NONE)
    osm_state_mgr_process( p_rcv->p_state_mgr,
                           process_get_sm_ret_val );
  OSM_LOG_EXIT( p_rcv->p_log );
}

/**********************************************************************
 **********************************************************************/
static void
__osm_sminfo_rcv_process_set_response(
  IN const osm_sminfo_rcv_t*  const p_rcv,
  IN const osm_madw_t*     const p_madw )
{
  const ib_smp_t*          p_smp;

  OSM_LOG_ENTER( p_rcv->p_log, __osm_sminfo_rcv_process_set_response );

  CL_ASSERT( p_madw );

  p_smp = osm_madw_get_smp_ptr( p_madw );

  if( p_smp->method != IB_MAD_METHOD_GET_RESP )
  {
    osm_log( p_rcv->p_log, OSM_LOG_ERROR,
             "__osm_sminfo_rcv_process_set_response: ERR 2F16: "
             "Unsupported method 0x%X\n",
             p_smp->method );
    goto Exit;
  }

  /* Check the AttributeModifier */
  if ( p_smp->attr_mod != IB_SMINFO_ATTR_MOD_HANDOVER )
  {
    osm_log( p_rcv->p_log, OSM_LOG_ERROR,
             "__osm_sminfo_rcv_process_set_response: ERR 2F17: "
             "Unsupported attribute modifier 0x%X\n",
             p_smp->attr_mod );
    goto Exit;
  }

  /*
    This is a response on a HANDOVER request -
    Nothing to do.
  */

 Exit:
  OSM_LOG_EXIT( p_rcv->p_log );
}

/**********************************************************************
 **********************************************************************/
void
osm_sminfo_rcv_process(
  IN void *context,
  IN void *data )
{
  osm_sminfo_rcv_t *p_rcv = context;
  osm_madw_t *p_madw = data;
  ib_smp_t *p_smp;
  osm_smi_context_t *p_smi_context;

  OSM_LOG_ENTER( p_rcv->p_log, osm_sminfo_rcv_process );

  CL_ASSERT( p_madw );

  p_smp = osm_madw_get_smp_ptr( p_madw );

  /*
    Determine if this is a request for our own SMInfo
    or if this is a response to our request for another
    SM's SMInfo.
  */
  if( ib_smp_is_response( p_smp ) )
  {
    /* Get the context - to see if this is a response to a Get or Set method */
    p_smi_context = osm_madw_get_smi_context_ptr( p_madw );
    if ( p_smi_context->set_method == FALSE )
    {
      /* this is a response to a Get method */
      __osm_sminfo_rcv_process_get_response( p_rcv, p_madw );
    }
    else
    {
      /* this is a response to a Set method */
      __osm_sminfo_rcv_process_set_response( p_rcv, p_madw );
    }
  }
  else
  {
    /* This is a request */
    if ( p_smp->method == IB_MAD_METHOD_GET )
    {
      /* This is a SubnGet request */
      __osm_sminfo_rcv_process_get_request( p_rcv, p_madw );
    }
    else
    {
      /* This is a SubnSet request */
      __osm_sminfo_rcv_process_set_request( p_rcv, p_madw );
    }
  }

  OSM_LOG_EXIT( p_rcv->p_log );
}

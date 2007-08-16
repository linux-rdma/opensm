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
 *    Implementation of osm_vl15_t.
 * This object represents the VL15 Interface object.
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
#include <opensm/osm_vl15intf.h>
#include <opensm/osm_madw.h>
#include <vendor/osm_vendor_api.h>
#include <opensm/osm_log.h>
#include <opensm/osm_helper.h>
#include <complib/cl_thread.h>
#include <signal.h>


/**********************************************************************
 **********************************************************************/
void
__osm_vl15_poller(
  IN void *p_ptr )
{
  ib_api_status_t status;
  osm_madw_t *p_madw;
  uint32_t mads_sent;
  uint32_t unicasts_sent;
  uint32_t mads_on_wire;
  osm_vl15_t* const p_vl = (osm_vl15_t*)p_ptr;
  cl_qlist_t* p_fifo;

  OSM_LOG_ENTER( p_vl->p_log, __osm_vl15_poller );

  if ( p_vl->thread_state == OSM_THREAD_STATE_NONE)
    p_vl->thread_state = OSM_THREAD_STATE_RUN;

  while( p_vl->thread_state == OSM_THREAD_STATE_RUN )
  {
    /*
      Start servicing the FIFOs by pulling off MAD wrappers
      and passing them to the transport interface.
      There are lots of corner cases here so tread carefully.

      The unicast FIFO has priority, since somebody is waiting
      for a timely response.
    */
    cl_spinlock_acquire( &p_vl->lock );

    if( cl_qlist_count( &p_vl->ufifo ) != 0 )
      p_fifo = &p_vl->ufifo;
    else
      p_fifo = &p_vl->rfifo;

    p_madw = (osm_madw_t*)cl_qlist_remove_head( p_fifo );

    cl_spinlock_release( &p_vl->lock );

    if( p_madw != (osm_madw_t*)cl_qlist_end( p_fifo ) )
    {
      if( osm_log_is_active( p_vl->p_log, OSM_LOG_DEBUG ) )
        osm_log( p_vl->p_log, OSM_LOG_DEBUG,
                 "__osm_vl15_poller: "
                 "Servicing p_madw = %p\n", p_madw );

      if( osm_log_is_active( p_vl->p_log, OSM_LOG_FRAMES ) )
        osm_dump_dr_smp( p_vl->p_log,
                         osm_madw_get_smp_ptr( p_madw ), OSM_LOG_FRAMES );

      /*
        Non-response-expected mads are not throttled on the wire
        since we can have no confirmation that they arrived
        at their destination.
      */
      if( p_madw->resp_expected == TRUE )
      {
        /*
          Note that other threads may not see the response MAD
          arrive before send() even returns.
          In that case, the wire count would temporarily go negative.
          To avoid this confusion, preincrement the counts on the
          assumption that send() will succeed.
        */
        mads_on_wire = cl_atomic_inc(&p_vl->p_stats->qp0_mads_outstanding_on_wire);
        CL_ASSERT( mads_on_wire <= p_vl->max_wire_smps );
      }
      else
        unicasts_sent = cl_atomic_inc(&p_vl->p_stats->qp0_unicasts_sent);

      mads_sent = cl_atomic_inc( &p_vl->p_stats->qp0_mads_sent );

      status = osm_vendor_send(
        osm_madw_get_bind_handle( p_madw ),
        p_madw, p_madw->resp_expected );

      if( status != IB_SUCCESS )
      {
        uint32_t outstanding;
        cl_status_t cl_status;

        osm_log( p_vl->p_log, OSM_LOG_ERROR,
                 "__osm_vl15_poller: ERR 3E03: "
                 "MAD send failed (%s)\n",
                 ib_get_err_str( status ) );

        /*
          The MAD was never successfully sent, so
          fix up the pre-incremented count values.
        */

        /* Decrement qp0_mads_sent and qp0_mads_outstanding_on_wire
           that were incremented in the code above. */
        mads_sent = cl_atomic_dec( &p_vl->p_stats->qp0_mads_sent );
        if( p_madw->resp_expected == TRUE )
          cl_atomic_dec( &p_vl->p_stats->qp0_mads_outstanding_on_wire );

        /*
           The following code is similar to the code in
           __osm_sm_mad_ctrl_retire_trans_mad. We need to decrement the
           qp0_mads_outstanding counter, and if we reached 0 - need to call
           the cl_disp_post with OSM_SIGNAL_NO_PENDING_TRANSACTION (in order
           to wake up the state mgr).
           There is one difference from the code in __osm_sm_mad_ctrl_retire_trans_mad.
           This code is called for all (vl15) mads, if osm_vendor_send() failed, unlike
           __osm_sm_mad_ctrl_retire_trans_mad which is called only on mads where
           resp_expected == TRUE. As a result, the qp0_mads_outstanding counter
           should be decremented and handled accordingly only if this is a mad
           with resp_expected == TRUE.
        */
        if ( p_madw->resp_expected == TRUE )
        {
          outstanding = cl_atomic_dec( &p_vl->p_stats->qp0_mads_outstanding );

          osm_log( p_vl->p_log, OSM_LOG_DEBUG,
                   "__osm_vl15_poller: "
                   "%u QP0 MADs outstanding\n",
                   p_vl->p_stats->qp0_mads_outstanding );

          if( outstanding == 0 )
          {
            /*
              The wire is clean.
              Signal the state manager.
            */
            if( osm_log_is_active( p_vl->p_log, OSM_LOG_DEBUG ) )
              osm_log( p_vl->p_log, OSM_LOG_DEBUG,
                       "__osm_vl15_poller: "
                       "Posting Dispatcher message %s\n",
                       osm_get_disp_msg_str( OSM_MSG_NO_SMPS_OUTSTANDING ) );

            cl_status = cl_disp_post( p_vl->h_disp,
                                      OSM_MSG_NO_SMPS_OUTSTANDING,
                                      (void *)OSM_SIGNAL_NO_PENDING_TRANSACTIONS,
                                      NULL,
                                      NULL );

            if( cl_status != CL_SUCCESS )
              osm_log( p_vl->p_log, OSM_LOG_ERROR,
                       "__osm_vl15_poller: ERR 3E06: "
                       "Dispatcher post message failed (%s)\n",
                       CL_STATUS_MSG( cl_status ) );
          }
        }
      }
      else if( osm_log_is_active( p_vl->p_log, OSM_LOG_DEBUG ) )
        osm_log( p_vl->p_log, OSM_LOG_DEBUG,
                 "__osm_vl15_poller: "
                 "%u QP0 MADs on wire, %u outstanding, %u unicasts sent, "
                 "%u total sent\n",
                 p_vl->p_stats->qp0_mads_outstanding_on_wire,
                 p_vl->p_stats->qp0_mads_outstanding,
                 p_vl->p_stats->qp0_unicasts_sent,
                 p_vl->p_stats->qp0_mads_sent );
    }
    else
      /*
        The VL15 FIFO is empty, so we have nothing left to do.
      */
      status = cl_event_wait_on( &p_vl->signal,
                                 EVENT_NO_TIMEOUT, TRUE );

    while( (p_vl->p_stats->qp0_mads_outstanding_on_wire >=
            (int32_t)p_vl->max_wire_smps ) &&
           (p_vl->thread_state == OSM_THREAD_STATE_RUN ) )
      status = cl_event_wait_on( &p_vl->signal,
                                 EVENT_NO_TIMEOUT, TRUE );

    if( status != CL_SUCCESS )
      osm_log( p_vl->p_log, OSM_LOG_ERROR,
               "__osm_vl15_poller: ERR 3E02: "
               "Event wait failed (%s)\n",
               CL_STATUS_MSG( status ) );
  }

  /*
    since we abort immediately when the state != OSM_THREAD_STATE_RUN
    we might have some mads on the queues. After the thread exits
    the vl15 destroy routine should put these mads back...
  */

  OSM_LOG_EXIT( p_vl->p_log );
}

/**********************************************************************
 **********************************************************************/
void
osm_vl15_construct(
  IN osm_vl15_t* const p_vl )
{
  memset( p_vl, 0, sizeof(*p_vl) );
  p_vl->state = OSM_VL15_STATE_INIT;
  p_vl->thread_state = OSM_THREAD_STATE_NONE;
  cl_event_construct( &p_vl->signal );
  cl_spinlock_construct( &p_vl->lock );
  cl_qlist_init( &p_vl->rfifo );
  cl_qlist_init( &p_vl->ufifo );
  cl_thread_construct( &p_vl->poller );
  p_vl->h_disp = CL_DISP_INVALID_HANDLE;
}

/**********************************************************************
 **********************************************************************/
void
osm_vl15_destroy(
  IN osm_vl15_t* const p_vl,
  IN struct _osm_mad_pool *p_pool)
{
  osm_madw_t* p_madw;

  OSM_LOG_ENTER( p_vl->p_log, osm_vl15_destroy );

  /*
    Signal our threads that we're leaving.
  */
  p_vl->thread_state = OSM_THREAD_STATE_EXIT;

  /*
    Don't trigger unless event has been initialized.
    Destroy the thread before we tear down the other objects.
  */
  if( p_vl->state != OSM_VL15_STATE_INIT )
    cl_event_signal( &p_vl->signal );

  cl_thread_destroy( &p_vl->poller );

  /*
    Return the outstanding messages to the pool
  */

  cl_spinlock_acquire( &p_vl->lock );

  while (!cl_is_qlist_empty( &p_vl->rfifo))
  {
    p_madw = (osm_madw_t*)cl_qlist_remove_head( &p_vl->rfifo);
    osm_mad_pool_put( p_pool, p_madw );
  }
  while (!cl_is_qlist_empty( &p_vl->ufifo))
  {
    p_madw = (osm_madw_t*)cl_qlist_remove_head( &p_vl->ufifo);
    osm_mad_pool_put( p_pool, p_madw );
  }

  cl_spinlock_release( &p_vl->lock );

  cl_event_destroy( &p_vl->signal );
  p_vl->state = OSM_VL15_STATE_INIT;
  cl_spinlock_destroy( &p_vl->lock );

  OSM_LOG_EXIT( p_vl->p_log );
}

/**********************************************************************
 **********************************************************************/
ib_api_status_t
osm_vl15_init(
  IN osm_vl15_t* const p_vl,
  IN osm_vendor_t* const p_vend,
  IN osm_log_t* const p_log,
  IN osm_stats_t* const p_stats,
  IN const int32_t max_wire_smps,
  IN osm_subn_t* const p_subn,
  IN cl_dispatcher_t* const p_disp,
  IN cl_plock_t* const p_lock
 )
{
  ib_api_status_t status = IB_SUCCESS;

  OSM_LOG_ENTER( p_log, osm_vl15_init );

  p_vl->p_vend = p_vend;
  p_vl->p_log = p_log;
  p_vl->p_stats = p_stats;
  p_vl->max_wire_smps = max_wire_smps;
  p_vl->p_subn = p_subn;
  p_vl->p_lock = p_lock;

  status = cl_event_init( &p_vl->signal, FALSE );
  if( status != IB_SUCCESS )
    goto Exit;

  p_vl->state = OSM_VL15_STATE_READY;

  status = cl_spinlock_init( &p_vl->lock );
  if( status != IB_SUCCESS )
    goto Exit;

  /*
    Initialize the thread after all other dependent objects
    have been initialized.
  */
  status = cl_thread_init( &p_vl->poller, __osm_vl15_poller, p_vl,
                           "opensm poller" );
  if( status != IB_SUCCESS )
    goto Exit;

  p_vl->h_disp = cl_disp_register(
    p_disp,
    CL_DISP_MSGID_NONE,
    NULL,
    NULL );

  if( p_vl->h_disp == CL_DISP_INVALID_HANDLE )
  {
    osm_log( p_log, OSM_LOG_ERROR,
             "osm_vl15_init: ERR 3E01: "
             "Dispatcher registration failed\n" );
    status = IB_INSUFFICIENT_RESOURCES;
    goto Exit;
  }

 Exit:
  OSM_LOG_EXIT( p_log );
  return( status );
}

/**********************************************************************
 **********************************************************************/
void
osm_vl15_poll(
  IN osm_vl15_t* const p_vl )
{
  OSM_LOG_ENTER( p_vl->p_log, osm_vl15_poll );

  CL_ASSERT( p_vl->state == OSM_VL15_STATE_READY );

  /*
    If we have room for more VL15 MADs on the wire,
    then signal the poller thread.

    This is not an airtight check, since the poller thread
    could be just about to send another MAD as we signal
    the event here.  To cover this rare case, the poller
    thread checks for a spurious wake-up.
  */
  if( p_vl->p_stats->qp0_mads_outstanding_on_wire <
      (int32_t)p_vl->max_wire_smps )
  {
    if( osm_log_is_active( p_vl->p_log, OSM_LOG_DEBUG ) )
      osm_log( p_vl->p_log, OSM_LOG_DEBUG,
               "osm_vl15_poll: "
               "Signalling poller thread\n" );

    cl_event_signal( &p_vl->signal );
  }

  OSM_LOG_EXIT( p_vl->p_log );
}

/**********************************************************************
 **********************************************************************/
void
osm_vl15_post(
  IN osm_vl15_t*           const p_vl,
  IN osm_madw_t*           const p_madw )
{
  OSM_LOG_ENTER( p_vl->p_log, osm_vl15_post );

  CL_ASSERT( p_vl->state == OSM_VL15_STATE_READY );

  if( osm_log_is_active( p_vl->p_log, OSM_LOG_DEBUG ) )
    osm_log( p_vl->p_log, OSM_LOG_DEBUG,
             "osm_vl15_post: "
             "Posting p_madw = 0x%p\n", p_madw );

  /*
    Determine in which fifo to place the pending madw.
  */
  cl_spinlock_acquire( &p_vl->lock );
  if( p_madw->resp_expected == TRUE )
  {
    cl_qlist_insert_tail( &p_vl->rfifo, (cl_list_item_t*)p_madw );
    cl_atomic_inc( &p_vl->p_stats->qp0_mads_outstanding );
  }
  else
    cl_qlist_insert_tail( &p_vl->ufifo, (cl_list_item_t*)p_madw );
  cl_spinlock_release( &p_vl->lock );

  if( osm_log_is_active( p_vl->p_log, OSM_LOG_DEBUG ) )
    osm_log( p_vl->p_log, OSM_LOG_DEBUG,
             "osm_vl15_post: "
             "%u QP0 MADs on wire, %u QP0 MADs outstanding\n",
             p_vl->p_stats->qp0_mads_outstanding_on_wire,
             p_vl->p_stats->qp0_mads_outstanding );

  osm_vl15_poll( p_vl );

  OSM_LOG_EXIT( p_vl->p_log );
}

void
osm_vl15_shutdown(
  IN osm_vl15_t* const p_vl,
  IN osm_mad_pool_t* const p_mad_pool)
{
  osm_madw_t* p_madw;

  OSM_LOG_ENTER( p_vl->p_log, osm_vl15_shutdown );

  /* we only should get here after the VL15 interface was initialized */
  CL_ASSERT( p_vl->state == OSM_VL15_STATE_READY );

  /* grap a lock on the object */
  cl_spinlock_acquire( &p_vl->lock );

  cl_disp_unregister( p_vl->h_disp );

  /* go over all outstanding MADs and retire their transactions */

  /* first we handle the list of response MADs */
  p_madw = (osm_madw_t*)cl_qlist_remove_head( &p_vl->ufifo );
  while ( p_madw != (osm_madw_t*)cl_qlist_end( &p_vl->ufifo ) )
  {
    if( osm_log_is_active( p_vl->p_log, OSM_LOG_DEBUG ) )
      osm_log( p_vl->p_log, OSM_LOG_DEBUG,
               "osm_vl15_shutdown: "
               "Releasing Response p_madw = %p\n", p_madw );

    osm_mad_pool_put( p_mad_pool, p_madw );

    p_madw = (osm_madw_t*)cl_qlist_remove_head( &p_vl->ufifo );
  }

  /* Request MADs we send out */
  p_madw = (osm_madw_t*)cl_qlist_remove_head( &p_vl->rfifo );
  while ( p_madw != (osm_madw_t*)cl_qlist_end( &p_vl->rfifo ) )
  {
    if( osm_log_is_active( p_vl->p_log, OSM_LOG_DEBUG ) )
      osm_log( p_vl->p_log, OSM_LOG_DEBUG,
               "osm_vl15_shutdown: "
               "Releasing Request p_madw = %p\n", p_madw );

    osm_mad_pool_put( p_mad_pool, p_madw );
    cl_atomic_dec( &p_vl->p_stats->qp0_mads_outstanding );

    p_madw = (osm_madw_t*)cl_qlist_remove_head( &p_vl->rfifo );
  }

  /* free the lock */
  cl_spinlock_release( &p_vl->lock );

  OSM_LOG_EXIT( p_vl->p_log );
}

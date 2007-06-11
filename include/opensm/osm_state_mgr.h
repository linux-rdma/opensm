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
 * 	Declaration of osm_state_mgr_t.
 *	This object represents the State Manager object.
 *	This object is part of the OpenSM family of objects.
 *
 * Environment:
 * 	Linux User Mode
 *
 * $Revision: 1.5 $
 */

#ifndef _OSM_STATE_MGR_H_
#define _OSM_STATE_MGR_H_

#include <complib/cl_passivelock.h>
#include <opensm/osm_base.h>
#include <opensm/osm_subnet.h>
#include <opensm/osm_lid_mgr.h>
#include <opensm/osm_ucast_mgr.h>
#include <opensm/osm_mcast_mgr.h>
#include <opensm/osm_link_mgr.h>
#include <opensm/osm_drop_mgr.h>
#include <opensm/osm_sm_mad_ctrl.h>
#include <opensm/osm_log.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else /* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif /* __cplusplus */

BEGIN_C_DECLS

/****h* OpenSM/State Manager
* NAME
*	State Manager
*
* DESCRIPTION
*	The State Manager object encapsulates the information
*	needed to control subnet sweeps and configuration.
*
*	The State Manager object is thread safe.
*
*	This object should be treated as opaque and should be
*	manipulated only through the provided functions.
*
* AUTHOR
*	Steve King, Intel
*
*********/

/****s* OpenSM: State Manager/osm_state_mgr_t
* NAME
*	osm_state_mgr_t
*
* DESCRIPTION
*	State Manager structure.
*
*	This object should be treated as opaque and should
*	be manipulated only through the provided functions.
*
* SYNOPSIS
*/
typedef struct _osm_state_mgr
{
  osm_subn_t					*p_subn;
  osm_log_t					*p_log;
  osm_lid_mgr_t				*p_lid_mgr;
  osm_ucast_mgr_t			*p_ucast_mgr;
  osm_mcast_mgr_t			*p_mcast_mgr;
  osm_link_mgr_t				*p_link_mgr;
  osm_drop_mgr_t				*p_drop_mgr;
  osm_req_t					*p_req;
  osm_stats_t					*p_stats;
  struct _osm_sm_state_mgr  *p_sm_state_mgr;
  const osm_sm_mad_ctrl_t	*p_mad_ctrl;
  cl_spinlock_t				state_lock;
  cl_spinlock_t				idle_lock;
  cl_qlist_t					idle_time_list;
  cl_plock_t					*p_lock;
  cl_event_t					*p_subnet_up_event;
  osm_sm_state_t				state;
} osm_state_mgr_t;
/*
* FIELDS
*	p_subn
*		Pointer to the Subnet object for this subnet.
*
*	p_log
*		Pointer to the log object.
*
*	p_lid_mgr
*		Pointer to the LID Manager object.
*
*	p_ucast_mgr
*		Pointer to the Unicast Manager object.
*
*	p_mcast_mgr
*		Pointer to the Multicast Manager object.
*
*	p_link_mgr
*		Pointer to the Link Manager object.
*
*	p_drop_mgr
*		Pointer to the Drop Manager object.
*
*	p_req
*		Pointer to the Requester object sending SMPs.
*
*  p_stats
*     Pointer to the OpenSM statistics block.
*
*  p_sm_state_mgr
*     Pointer to the SM state mgr object.
*
*	p_mad_ctrl
*		Pointer to the SM's MAD Controller object.
*
*	state_lock
*		Spinlock guarding the state and processes.
*
*	p_lock
*		lock guarding the subnet object.
*
*	p_subnet_up_event
*		Pointer to the event to set if/when the subnet comes up.
*
*	state
*		State of the SM.
*
*  state_step_mode
*     Controls the mode of progressing to next stage:
*     OSM_STATE_STEP_CONTINUOUS - normal automatic progress mode
*     OSM_STATE_STEP_TAKE_ONE - do one step and stop
*     OSM_STATE_STEP_BREAK  - stop before taking next step
*
*  next_stage_signal
*     Stores the signal to be provided when running the next stage.
*
* SEE ALSO
*	State Manager object
*********/

/****s* OpenSM: State Manager/_osm_idle_item
* NAME
*	_osm_idle_item
*
* DESCRIPTION
*	Idle item.  
*
* SYNOPSIS
*/

typedef osm_signal_t
(*osm_pfn_start_t)(
	IN				void						*context1,
	IN				void						*context2 );

typedef void
(*osm_pfn_done_t)(
	IN				void						*context1,
	IN				void						*context2 );

typedef struct _osm_idle_item
{
	cl_list_item_t	list_item;
	void*			context1;
	void*			context2;
	osm_pfn_start_t	pfn_start;
	osm_pfn_done_t	pfn_done;	
}osm_idle_item_t;

/*
* FIELDS
*	list_item
*		list item.
*
*	context1
*		Context pointer
*
*	context2
*		Context pointer
*
*	pfn_start
*		Pointer to the start function.
*
*	pfn_done
*		Pointer to the dine function.
* SEE ALSO
*	State Manager object
*********/

/****f* OpenSM: State Manager/osm_state_mgr_process_idle
* NAME
*	osm_state_mgr_process_idle
*
* DESCRIPTION
*	Formulates the osm_idle_item and inserts it into the queue and 
*	signals the state manager.
*
* SYNOPSIS
*/

ib_api_status_t
osm_state_mgr_process_idle(
	IN osm_state_mgr_t* const p_mgr,
	IN osm_pfn_start_t	pfn_start,
	IN osm_pfn_done_t	pfn_done,
	void*			context1,
	void*			context2
	);

/*
* PARAMETERS
*	p_mgr
*		[in] Pointer to a State Manager object to construct.
*
*	pfn_start
*		[in] Pointer the start function which will be called at
*			idle time.
*
*	pfn_done
*		[in] pointer the done function which will be called
*			when outstanding smps is zero
*
*	context1
*		[in] Pointer to void 
*	
*	context2
*		[in] Pointer to void 
*
* RETURN VALUE
*	IB_SUCCESS or IB_ERROR
*
* NOTES
*	Allows osm_state_mgr_destroy
*
*	Calling osm_state_mgr_construct is a prerequisite to calling any other
*	method except osm_state_mgr_init.
*
* SEE ALSO
*	State Manager object, osm_state_mgr_init,
*	osm_state_mgr_destroy
*********/

/****f* OpenSM: State Manager/osm_state_mgr_construct
* NAME
*	osm_state_mgr_construct
*
* DESCRIPTION
*	This function constructs a State Manager object.
*
* SYNOPSIS
*/
void
osm_state_mgr_construct(
	IN osm_state_mgr_t* const p_mgr );
/*
* PARAMETERS
*	p_mgr
*		[in] Pointer to a State Manager object to construct.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Allows osm_state_mgr_destroy
*
*	Calling osm_state_mgr_construct is a prerequisite to calling any other
*	method except osm_state_mgr_init.
*
* SEE ALSO
*	State Manager object, osm_state_mgr_init,
*	osm_state_mgr_destroy
*********/

/****f* OpenSM: State Manager/osm_state_mgr_destroy
* NAME
*	osm_state_mgr_destroy
*
* DESCRIPTION
*	The osm_state_mgr_destroy function destroys the object, releasing
*	all resources.
*
* SYNOPSIS
*/
void
osm_state_mgr_destroy(
	IN osm_state_mgr_t* const p_mgr );
/*
* PARAMETERS
*	p_mgr
*		[in] Pointer to the object to destroy.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Performs any necessary cleanup of the specified
*	State Manager object.
*	Further operations should not be attempted on the destroyed object.
*	This function should only be called after a call to
*	osm_state_mgr_construct or osm_state_mgr_init.
*
* SEE ALSO
*	State Manager object, osm_state_mgr_construct,
*	osm_state_mgr_init
*********/

/****f* OpenSM: State Manager/osm_state_mgr_init
* NAME
*	osm_state_mgr_init
*
* DESCRIPTION
*	The osm_state_mgr_init function initializes a
*	State Manager object for use.
*
* SYNOPSIS
*/
ib_api_status_t
osm_state_mgr_init(
	IN osm_state_mgr_t*			const p_mgr,
	IN osm_subn_t*				const p_subn,
	IN osm_lid_mgr_t*			const p_lid_mgr,
	IN osm_ucast_mgr_t*			const p_ucast_mgr,
	IN osm_mcast_mgr_t*			const p_mcast_mgr,
	IN osm_link_mgr_t*			const p_link_mgr,
	IN osm_drop_mgr_t*			const p_drop_mgr,
	IN osm_req_t*				const p_req,
   IN osm_stats_t*               const p_stats,
   IN struct _osm_sm_state_mgr*  const p_sm_state_mgr,
	IN const osm_sm_mad_ctrl_t* const p_mad_ctrl,
	IN cl_plock_t*				const p_lock,
	IN cl_event_t*				const p_subnet_up_event,
	IN osm_log_t*				const p_log );
/*
* PARAMETERS
*	p_mgr
*		[in] Pointer to an osm_state_mgr_t object to initialize.
*
*	p_subn
*		[in] Pointer to the Subnet object for this subnet.
*
*	p_lid_mgr
*		[in] Pointer to the LID Manager object.
*
*	p_ucast_mgr
*		[in] Pointer to the Unicast Manager object.
*
*	p_mcast_mgr
*		[in] Pointer to the Multicast Manager object.
*
*	p_link_mgr
*		[in] Pointer to the Link Manager object.
*
*	p_drop_mgr
*		[in] Pointer to the Drop Manager object.
*
*	p_req
*		[in] Pointer to the Request Controller object.
*
*  p_stats
*     [in] Pointer to the OpenSM statistics block.
*
*  p_sm_state_mgr
*     [in] Pointer to the SM state mgr object.
*
*	p_mad_ctrl
*		[in] Pointer to the SM's mad controller.
*
*	p_subnet_up_event
*		[in] Pointer to the event to set if/when the subnet comes up.
*
*	p_log
*		[in] Pointer to the log object.
*
* RETURN VALUES
*	IB_SUCCESS if the State Manager object was initialized
*	successfully.
*
* NOTES
*	Allows calling other State Manager methods.
*
* SEE ALSO
*	State Manager object, osm_state_mgr_construct,
*	osm_state_mgr_destroy
*********/

/****f* OpenSM: State Manager/osm_sm_is_greater_than
* NAME
*	osm_sm_is_greater_than
*
* DESCRIPTION
*  Compares two SM's (14.4.1.2)
*
* SYNOPSIS
*/
static inline boolean_t
osm_sm_is_greater_than (
  IN const uint8_t    l_priority,
  IN const ib_net64_t l_guid,
  IN const uint8_t    r_priority,
  IN const ib_net64_t r_guid )
{
  if( l_priority > r_priority )
  {
    return( TRUE );
  }
  else
  {
    if( l_priority == r_priority )
    {
      if( cl_ntoh64(l_guid) <  cl_ntoh64(r_guid) )
      {
        return( TRUE );
      }
    }
  }
  return( FALSE );
}
/*
* PARAMETERS
*	l_priority
*		[in] Priority of the SM on the "left"
*
*	l_guid
*		[in] GUID of the SM on the "left"
*
*	r_priority
*		[in] Priority of the SM on the "right"
*
*	r_guid
*		[in] GUID of the SM on the "right"
*
* RETURN VALUES
*  Return TRUE if an sm with l_priority and l_guid is higher than an sm
*  with r_priority and r_guid,
*  return FALSE otherwise.
*
* NOTES
*
* SEE ALSO
*	State Manager
*********/

/****f* OpenSM: State Manager/osm_state_mgr_process
* NAME
*	osm_state_mgr_process
*
* DESCRIPTION
*	Processes and maintains the states of the SM.
*
* SYNOPSIS
*/
void
osm_state_mgr_process(
	IN osm_state_mgr_t* const p_mgr,
	IN osm_signal_t signal );
/*
* PARAMETERS
*	p_mgr
*		[in] Pointer to an osm_state_mgr_t object.
*
*	signal
*		[in] Signal to the state engine.
*
* RETURN VALUES
*	None.
*
* NOTES
*
* SEE ALSO
*	State Manager
*********/
	
END_C_DECLS

#endif	/* _OSM_STATE_MGR_H_ */

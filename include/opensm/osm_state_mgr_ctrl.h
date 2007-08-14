/*
 * Copyright (c) 2004, 2005 Voltaire, Inc. All rights reserved.
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
 * 	Declaration of osm_state_mgr_ctrl_t.
 *	This object represents a controller that receives the
 *	State indication after a subnet sweep.
 *	This object is part of the OpenSM family of objects.
 *
 * Environment:
 * 	Linux User Mode
 *
 * $Revision: 1.4 $
 */

#ifndef _OSM_STATE_MGR_CTRL_H_
#define _OSM_STATE_MGR_CTRL_H_

#include <complib/cl_dispatcher.h>
#include <opensm/osm_base.h>
#include <opensm/osm_state_mgr.h>
#include <opensm/osm_log.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/****h* OpenSM/State Manager Controller
* NAME
*	State Manager Controller
*
* DESCRIPTION
*	The State Manager Controller object encapsulates the information
*	needed to pass the dispatcher message from the dispatcher
*	to the State Manager.
*
*	The State Manager Controller object is thread safe.
*
*	This object should be treated as opaque and should be
*	manipulated only through the provided functions.
*
* AUTHOR
*	Steve King, Intel
*
*********/
/****s* OpenSM: State Manager Controller/osm_state_mgr_ctrl_t
* NAME
*	osm_state_mgr_ctrl_t
*
* DESCRIPTION
*	State Manager Controller structure.
*
*	This object should be treated as opaque and should
*	be manipulated only through the provided functions.
*
* SYNOPSIS
*/
typedef struct _osm_state_mgr_ctrl {
	osm_state_mgr_t *p_mgr;
	osm_log_t *p_log;
	cl_dispatcher_t *p_disp;
	cl_disp_reg_handle_t h_disp;

} osm_state_mgr_ctrl_t;
/*
* FIELDS
*	p_mgr
*		Pointer to the State Manager object.
*
*	p_log
*		Pointer to the log object.
*
*	p_disp
*		Pointer to the Dispatcher.
*
*	h_disp
*		Handle returned from dispatcher registration.
*
* SEE ALSO
*	State Manager Controller object
*********/

/****f* OpenSM: State Manager Controller/osm_state_mgr_ctrl_construct
* NAME
*	osm_state_mgr_ctrl_construct
*
* DESCRIPTION
*	This function constructs a State Manager Controller object.
*
* SYNOPSIS
*/
void osm_state_mgr_ctrl_construct(IN osm_state_mgr_ctrl_t * const p_ctrl);
/*
* PARAMETERS
*	p_ctrl
*		[in] Pointer to a State Manager Controller
*		object to construct.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Allows calling osm_state_mgr_ctrl_init, and osm_state_mgr_ctrl_destroy.
*
*	Calling osm_state_mgr_ctrl_construct is a prerequisite to calling any
*	other method except osm_state_mgr_ctrl_init.
*
* SEE ALSO
*	State Manager Controller object, osm_state_mgr_ctrl_init,
*	osm_state_mgr_ctrl_destroy
*********/

/****f* OpenSM: State Manager Controller/osm_state_mgr_ctrl_destroy
* NAME
*	osm_state_mgr_ctrl_destroy
*
* DESCRIPTION
*	The osm_state_mgr_ctrl_destroy function destroys the object, releasing
*	all resources.
*
* SYNOPSIS
*/
void osm_state_mgr_ctrl_destroy(IN osm_state_mgr_ctrl_t * const p_ctrl);
/*
* PARAMETERS
*	p_ctrl
*		[in] Pointer to the object to destroy.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Performs any necessary cleanup of the specified
*	State Manager Controller object.
*	Further operations should not be attempted on the destroyed object.
*	This function should only be called after a call to
*	osm_state_mgr_ctrl_construct or osm_state_mgr_ctrl_init.
*
* SEE ALSO
*	State Manager Controller object, osm_state_mgr_ctrl_construct,
*	osm_state_mgr_ctrl_init
*********/

/****f* OpenSM: State Manager Controller/osm_state_mgr_ctrl_init
* NAME
*	osm_state_mgr_ctrl_init
*
* DESCRIPTION
*	The osm_state_mgr_ctrl_init function initializes a
*	State Manager Controller object for use.
*
* SYNOPSIS
*/
ib_api_status_t
osm_state_mgr_ctrl_init(IN osm_state_mgr_ctrl_t * const p_ctrl,
			IN osm_state_mgr_t * const p_mgr,
			IN osm_log_t * const p_log,
			IN cl_dispatcher_t * const p_disp);
/*
* PARAMETERS
*	p_ctrl
*		[in] Pointer to an osm_state_mgr_ctrl_t object to initialize.
*
*	p_mgr
*		[in] Pointer to an osm_state_mgr_t object.
*
*	p_log
*		[in] Pointer to the log object.
*
*	p_disp
*		[in] Pointer to the OpenSM central Dispatcher.
*
* RETURN VALUES
*	IB_SUCCESS if the State Manager Controller object
*	was initialized	successfully.
*
* NOTES
*	Allows calling other State Manager Controller methods.
*
* SEE ALSO
*	State Manager Controller object, osm_state_mgr_ctrl_construct,
*	osm_state_mgr_ctrl_destroy
*********/

END_C_DECLS
#endif				/* OSM_STATE_MGR_CTRL_H_ */

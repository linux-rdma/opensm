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
 * 	Declaration of osm_sweep_fail_ctrl_t.
 *	This object represents a controller that
 *	handles transport failures during sweeps.
 *	This object is part of the OpenSM family of objects.
 */

#ifndef _OSM_SWEEP_FAIL_CTRL_H_
#define _OSM_SWEEP_FAIL_CTRL_H_

#include <complib/cl_dispatcher.h>
#include <opensm/osm_base.h>
#include <opensm/osm_madw.h>
#include <opensm/osm_log.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/****h* OpenSM/Sweep Fail Controller
* NAME
*	Sweep Fail Controller
*
* DESCRIPTION
*	The Sweep Fail Controller object encapsulates
*	the information	needed to handle transport failures during
*	sweeps.
*
*	The Sweep Fail Controller object is thread safe.
*
*	This object should be treated as opaque and should be
*	manipulated only through the provided functions.
*
* AUTHOR
*	Steve King, Intel
*
*********/
struct osm_sm;
/****s* OpenSM: Sweep Fail Controller/osm_sweep_fail_ctrl_t
* NAME
*	osm_sweep_fail_ctrl_t
*
* DESCRIPTION
*	Sweep Fail Controller structure.
*
*	This object should be treated as opaque and should
*	be manipulated only through the provided functions.
*
* SYNOPSIS
*/
typedef struct osm_sweep_fail_ctrl {
	struct osm_sm *sm;
	cl_disp_reg_handle_t h_disp;
} osm_sweep_fail_ctrl_t;
/*
* FIELDS
*	sm
*		Pointer to the sm object.
*
*	h_disp
*		Handle returned from dispatcher registration.
*
* SEE ALSO
*	Sweep Fail Controller object
*	Sweep Failr object
*********/

/****f* OpenSM: Sweep Fail Controller/osm_sweep_fail_ctrl_construct
* NAME
*	osm_sweep_fail_ctrl_construct
*
* DESCRIPTION
*	This function constructs a Sweep Fail Controller object.
*
* SYNOPSIS
*/
void osm_sweep_fail_ctrl_construct(IN osm_sweep_fail_ctrl_t * const p_ctrl);
/*
* PARAMETERS
*	p_ctrl
*		[in] Pointer to a Sweep Fail Controller
*		object to construct.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Allows calling osm_sweep_fail_ctrl_init, osm_sweep_fail_ctrl_destroy
*
*	Calling osm_sweep_fail_ctrl_construct is a prerequisite to calling any other
*	method except osm_sweep_fail_ctrl_init.
*
* SEE ALSO
*	Sweep Fail Controller object, osm_sweep_fail_ctrl_init,
*	osm_sweep_fail_ctrl_destroy
*********/

/****f* OpenSM: Sweep Fail Controller/osm_sweep_fail_ctrl_destroy
* NAME
*	osm_sweep_fail_ctrl_destroy
*
* DESCRIPTION
*	The osm_sweep_fail_ctrl_destroy function destroys the object, releasing
*	all resources.
*
* SYNOPSIS
*/
void osm_sweep_fail_ctrl_destroy(IN osm_sweep_fail_ctrl_t * const p_ctrl);
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
*	Sweep Fail Controller object.
*	Further operations should not be attempted on the destroyed object.
*	This function should only be called after a call to
*	osm_sweep_fail_ctrl_construct or osm_sweep_fail_ctrl_init.
*
* SEE ALSO
*	Sweep Fail Controller object, osm_sweep_fail_ctrl_construct,
*	osm_sweep_fail_ctrl_init
*********/

/****f* OpenSM: Sweep Fail Controller/osm_sweep_fail_ctrl_init
* NAME
*	osm_sweep_fail_ctrl_init
*
* DESCRIPTION
*	The osm_sweep_fail_ctrl_init function initializes a
*	Sweep Fail Controller object for use.
*
* SYNOPSIS
*/
ib_api_status_t
osm_sweep_fail_ctrl_init(IN osm_sweep_fail_ctrl_t * const p_ctrl,
			 IN struct osm_sm * sm);
/*
* PARAMETERS
*	p_ctrl
*		[in] Pointer to an osm_sweep_fail_ctrl_t object to initialize.
*
*	sm
*		[in] Pointer to the SM object.
*
* RETURN VALUES
*	CL_SUCCESS if the Sweep Fail Controller object was initialized
*	successfully.
*
* NOTES
*	Allows calling other Sweep Fail Controller methods.
*
* SEE ALSO
*	Sweep Fail Controller object, osm_sweep_fail_ctrl_construct,
*	osm_sweep_fail_ctrl_destroy
*********/

END_C_DECLS
#endif				/* _OSM_SWEEP_FAIL_CTRL_H_ */

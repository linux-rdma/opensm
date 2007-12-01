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
 * 	Declaration of osm_drop_mgr_t.
 *	This object represents the Drop Manager object.
 *	This object is part of the OpenSM family of objects.
 *
 * Environment:
 * 	Linux User Mode
 *
 * $Revision: 1.4 $
 */

#ifndef _OSM_DROP_MGR_H_
#define _OSM_DROP_MGR_H_

#include <complib/cl_passivelock.h>
#include <opensm/osm_base.h>
#include <opensm/osm_madw.h>
#include <opensm/osm_subnet.h>
#include <opensm/osm_req.h>
#include <opensm/osm_log.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/****h* OpenSM/Drop Manager
* NAME
*	Drop Manager
*
* DESCRIPTION
*	The Drop Manager object encapsulates the information
*	needed to receive the SwitchInfo attribute from a node.
*
*	The Drop Manager object is thread safe.
*
*	This object should be treated as opaque and should be
*	manipulated only through the provided functions.
*
* AUTHOR
*	Steve King, Intel
*
*********/
struct osm_sm;
/****s* OpenSM: Drop Manager/osm_drop_mgr_t
* NAME
*	osm_drop_mgr_t
*
* DESCRIPTION
*	Drop Manager structure.
*
*	This object should be treated as opaque and should
*	be manipulated only through the provided functions.
*
* SYNOPSIS
*/
typedef struct _osm_drop_mgr {
	struct osm_sm *sm;
	osm_subn_t *p_subn;
	osm_log_t *p_log;
	osm_req_t *p_req;
	cl_plock_t *p_lock;

} osm_drop_mgr_t;
/*
* FIELDS
*	sm
*		Pointer to the SM object.
*
*	p_subn
*		Pointer to the Subnet object for this subnet.
*
*	p_log
*		Pointer to the log object.
*
*	p_req
*		Pointer to the Request object.
*
*	p_lock
*		Pointer to the serializing lock.
*
* SEE ALSO
*	Drop Manager object
*********/

/****f* OpenSM: Drop Manager/osm_drop_mgr_construct
* NAME
*	osm_drop_mgr_construct
*
* DESCRIPTION
*	This function constructs a Drop Manager object.
*
* SYNOPSIS
*/
void osm_drop_mgr_construct(IN osm_drop_mgr_t * const p_mgr);
/*
* PARAMETERS
*	p_mgr
*		[in] Pointer to a Drop Manager object to construct.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Allows calling osm_drop_mgr_init, osm_drop_mgr_destroy
*
*	Calling osm_drop_mgr_construct is a prerequisite to calling any other
*	method except osm_drop_mgr_init.
*
* SEE ALSO
*	Drop Manager object, osm_drop_mgr_init,
*	osm_drop_mgr_destroy
*********/

/****f* OpenSM: Drop Manager/osm_drop_mgr_destroy
* NAME
*	osm_drop_mgr_destroy
*
* DESCRIPTION
*	The osm_drop_mgr_destroy function destroys the object, releasing
*	all resources.
*
* SYNOPSIS
*/
void osm_drop_mgr_destroy(IN osm_drop_mgr_t * const p_mgr);
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
*	Drop Manager object.
*	Further operations should not be attempted on the destroyed object.
*	This function should only be called after a call to
*	osm_drop_mgr_construct or osm_drop_mgr_init.
*
* SEE ALSO
*	Drop Manager object, osm_drop_mgr_construct,
*	osm_drop_mgr_init
*********/

/****f* OpenSM: Drop Manager/osm_drop_mgr_init
* NAME
*	osm_drop_mgr_init
*
* DESCRIPTION
*	The osm_drop_mgr_init function initializes a
*	Drop Manager object for use.
*
* SYNOPSIS
*/
ib_api_status_t
osm_drop_mgr_init(IN osm_drop_mgr_t * const p_mgr, struct osm_sm * sm);
/*
* PARAMETERS
*	p_mgr
*		[in] Pointer to an osm_drop_mgr_t object to initialize.
*
*	sm
*		[in] Pointer to the SM object.
*
* RETURN VALUES
*	IB_SUCCESS if the Drop Manager object was initialized successfully.
*
* NOTES
*	Allows calling other Drop Manager methods.
*
* SEE ALSO
*	Drop Manager object, osm_drop_mgr_construct, osm_drop_mgr_destroy
*********/

/****f* OpenSM: Drop Manager/osm_drop_mgr_process
* NAME
*	osm_drop_mgr_process
*
* DESCRIPTION
*	Process the SwitchInfo attribute.
*
* SYNOPSIS
*/
void osm_drop_mgr_process(IN const osm_drop_mgr_t * const p_mgr);
/*
* PARAMETERS
*	p_mgr
*		[in] Pointer to an osm_drop_mgr_t object.
*
* RETURN VALUES
*	None
*
* NOTES
*	This function processes a SwitchInfo attribute.
*
* SEE ALSO
*	Drop Manager, Switch Info Response Controller
*********/

END_C_DECLS
#endif				/* _OSM_DROP_MGR_H_ */

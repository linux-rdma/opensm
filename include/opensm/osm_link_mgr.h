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
 * 	Declaration of osm_link_mgr_t.
 *	This object represents the Link Manager object.
 *	This object is part of the OpenSM family of objects.
 *
 * Environment:
 * 	Linux User Mode
 *
 * $Revision: 1.4 $
 */

#ifndef _OSM_LINK_MGR_H_
#define _OSM_LINK_MGR_H_

#include <complib/cl_passivelock.h>
#include <opensm/osm_base.h>
#include <opensm/osm_madw.h>
#include <opensm/osm_subnet.h>
#include <opensm/osm_log.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/****h* OpenSM/Link Manager
* NAME
*	Link Manager
*
* DESCRIPTION
*	The Link Manager object encapsulates the information
*	needed to control unicast LID forwarding on the subnet.
*
*	The Link Manager object is thread safe.
*
*	This object should be treated as opaque and should be
*	manipulated only through the provided functions.
*
* AUTHOR
*	Steve King, Intel
*
*********/
struct osm_sm;
/****s* OpenSM: Link Manager/osm_link_mgr_t
* NAME
*	osm_link_mgr_t
*
* DESCRIPTION
*	Link Manager structure.
*
*	This object should be treated as opaque and should
*	be manipulated only through the provided functions.
*
* SYNOPSIS
*/
typedef struct _osm_link_mgr {
	struct osm_sm *sm;
	osm_subn_t *p_subn;
	osm_log_t *p_log;
	cl_plock_t *p_lock;
} osm_link_mgr_t;
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
*	p_lock
*		Pointer to the serializing lock.
*
* SEE ALSO
*	Link Manager object
*********/

/****f* OpenSM: Link Manager/osm_link_mgr_construct
* NAME
*	osm_link_mgr_construct
*
* DESCRIPTION
*	This function constructs a Link Manager object.
*
* SYNOPSIS
*/
void osm_link_mgr_construct(IN osm_link_mgr_t * const p_mgr);
/*
* PARAMETERS
*	p_mgr
*		[in] Pointer to a Link Manager object to construct.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Allows osm_link_mgr_destroy
*
*	Calling osm_link_mgr_construct is a prerequisite to calling any other
*	method except osm_link_mgr_init.
*
* SEE ALSO
*	Link Manager object, osm_link_mgr_init,
*	osm_link_mgr_destroy
*********/

/****f* OpenSM: Link Manager/osm_link_mgr_destroy
* NAME
*	osm_link_mgr_destroy
*
* DESCRIPTION
*	The osm_link_mgr_destroy function destroys the object, releasing
*	all resources.
*
* SYNOPSIS
*/
void osm_link_mgr_destroy(IN osm_link_mgr_t * const p_mgr);
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
*	Link Manager object.
*	Further operations should not be attempted on the destroyed object.
*	This function should only be called after a call to
*	osm_link_mgr_construct or osm_link_mgr_init.
*
* SEE ALSO
*	Link Manager object, osm_link_mgr_construct,
*	osm_link_mgr_init
*********/

/****f* OpenSM: Link Manager/osm_link_mgr_init
* NAME
*	osm_link_mgr_init
*
* DESCRIPTION
*	The osm_link_mgr_init function initializes a
*	Link Manager object for use.
*
* SYNOPSIS
*/
ib_api_status_t
osm_link_mgr_init(IN osm_link_mgr_t * const p_mgr, IN struct osm_sm * sm);
/*
* PARAMETERS
*	p_mgr
*		[in] Pointer to an osm_link_mgr_t object to initialize.
*
*	sm
*		[in] Pointer to the SM object.
*
* RETURN VALUES
*	IB_SUCCESS if the Link Manager object was initialized
*	successfully.
*
* NOTES
*	Allows calling other Link Manager methods.
*
* SEE ALSO
*	Link Manager object, osm_link_mgr_construct,
*	osm_link_mgr_destroy
*********/

/****f* OpenSM: Link Manager/osm_link_mgr_process
* NAME
*	osm_link_mgr_process
*
* DESCRIPTION
*	Processes all ports in the subnet per the link manager command.
*
* SYNOPSIS
*/
osm_signal_t
osm_link_mgr_process(IN osm_link_mgr_t * const p_mgr,
		     IN const uint8_t link_state);
/*
* PARAMETERS
*	p_mgr
*		[in] Pointer to an osm_link_mgr_t object.
*
*	link_state
*		[in] state to which to the set the port.
*
*
* RETURN VALUES
*	Returns the appropriate signal to the caller:
*		OSM_SIGNAL_DONE - operation is complete
*		OSM_SIGNAL_DONE_PENDING - local operations are complete, but
*			transactions are still pending on the wire.
*
* NOTES
*
* SEE ALSO
*	Link Manager, Node Info Response Controller
*********/

END_C_DECLS
#endif				/* _OSM_LINK_MGR_H_ */

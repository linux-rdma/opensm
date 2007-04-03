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
 * 	Declaration of osm_slvl_rcv_t.
 *	This object represents the SLtoVL Map Receiver object.
 *	This object is part of the OpenSM family of objects.
 *
 * Environment:
 * 	Linux User Mode
 *
 * $Revision: 1.3 $
 */

#ifndef _OSM_SLVL_RCV_H_
#define _OSM_SLVL_RCV_H_

#include <complib/cl_passivelock.h>
#include <opensm/osm_base.h>
#include <opensm/osm_madw.h>
#include <opensm/osm_subnet.h>
#include <opensm/osm_vl15intf.h>
#include <opensm/osm_req.h>
#include <opensm/osm_log.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else /* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif /* __cplusplus */

BEGIN_C_DECLS

/****h* OpenSM/Slvl Map Receiver
* NAME
*	Slvl Map Receiver
*
* DESCRIPTION
*	The Slvl Map Receiver object encapsulates the information
*	needed to set or get the SLtoVL map attribute from a port.
*
*	The Slvl Map Receiver object is thread safe.
*
*	This object should be treated as opaque and should be
*	manipulated only through the provided functions.
*
* AUTHOR
*	Eitan Zahavi, Mellanox
*
*********/

/****s* OpenSM: Slvl Map Receiver/osm_slvl_rcv_t
* NAME
*	osm_slvl_rcv_t
*
* DESCRIPTION
*	Slvl Map Receiver structure.
*
*	This object should be treated as opaque and should
*	be manipulated only through the provided functions.
*
* SYNOPSIS
*/
typedef struct _osm_slvl_rcv
{
	osm_subn_t	*p_subn;
	osm_req_t	*p_req;
	osm_log_t	*p_log;
	cl_plock_t	*p_lock;
} osm_slvl_rcv_t;
/*
* FIELDS
*	p_subn
*		Pointer to the Subnet object for this subnet.
*
*	p_req
*		Pointer to the generic attribute request object.
*
*	p_log
*		Pointer to the log object.
*
*	p_lock
*		Pointer to the serializing lock.
*
* SEE ALSO
*	Slvl Map Receiver object
*********/

/****f* OpenSM: Slvl Map Receiver/osm_slvl_rcv_construct
* NAME
*	osm_slvl_rcv_construct
*
* DESCRIPTION
*	This function constructs a Slvl Map Receiver object.
*
* SYNOPSIS
*/
void osm_slvl_rcv_construct(
	IN osm_slvl_rcv_t* const p_ctrl );
/*
* PARAMETERS
*	p_ctrl
*		[in] Pointer to a Slvl Map Receiver object to construct.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Allows calling osm_slvl_rcv_destroy
*
*	Calling osm_slvl_rcv_construct is a prerequisite to calling any other
*	method except osm_slvl_rcv_init.
*
* SEE ALSO
*	Slvl Map Receiver object, osm_slvl_rcv_init,
*	osm_slvl_rcv_destroy
*********/

/****f* OpenSM: Slvl Map Receiver/osm_slvl_rcv_destroy
* NAME
*	osm_slvl_rcv_destroy
*
* DESCRIPTION
*	The osm_slvl_rcv_destroy function destroys the object, releasing
*	all resources.
*
* SYNOPSIS
*/
void osm_slvl_rcv_destroy(
	IN osm_slvl_rcv_t* const p_ctrl );
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
*	Slvl Map Receiver object.
*	Further operations should not be attempted on the destroyed object.
*	This function should only be called after a call to
*	osm_slvl_rcv_construct or osm_slvl_rcv_init.
*
* SEE ALSO
*	Slvl Map Receiver object, osm_slvl_rcv_construct,
*	osm_slvl_rcv_init
*********/

/****f* OpenSM: Slvl Map Receiver/osm_slvl_rcv_init
* NAME
*	osm_slvl_rcv_init
*
* DESCRIPTION
*	The osm_slvl_rcv_init function initializes a
*	Slvl Map Receiver object for use.
*
* SYNOPSIS
*/
ib_api_status_t osm_slvl_rcv_init(
	IN osm_slvl_rcv_t* const p_ctrl,
	IN osm_req_t* const p_req,
	IN osm_subn_t* const p_subn,
	IN osm_log_t* const p_log,
	IN cl_plock_t* const p_lock );
/*
* PARAMETERS
*	p_ctrl
*		[in] Pointer to an osm_slvl_rcv_t object to initialize.
*
*	p_req
*		[in] Pointer to an osm_req_t object.
*
*	p_subn
*		[in] Pointer to the Subnet object for this subnet.
*
*	p_log
*		[in] Pointer to the log object.
*
*	p_lock
*		[in] Pointer to the OpenSM serializing lock.
*
* RETURN VALUES
*	CL_SUCCESS if the Slvl Map Receiver object was initialized
*	successfully.
*
* NOTES
*	Allows calling other Slvl Map Receiver methods.
*
* SEE ALSO
*	Slvl Map Receiver object, osm_slvl_rcv_construct,
*	osm_slvl_rcv_destroy
*********/

/****f* OpenSM: Slvl Map Receiver/osm_slvl_rcv_process
* NAME
*	osm_slvl_rcv_process
*
* DESCRIPTION
*	Process the SLtoVL map attribute.
*
* SYNOPSIS
*/
void osm_slvl_rcv_process(
	IN void* context,
	IN void* p_data );
/*
* PARAMETERS
*	context
*		[in] Pointer to an osm_slvl_rcv_t object.
*
*	p_data
*		[in] Pointer to the MAD Wrapper containing the MAD
*		that contains the node's SLtoVL attribute.
*
* RETURN VALUES
*	CL_SUCCESS if the SLtoVL processing was successful.
*
* NOTES
*	This function processes a SLtoVL attribute.
*
* SEE ALSO
*	Slvl Map Receiver, Slvl Map Response Controller
*********/
	
END_C_DECLS

#endif	/* _OSM_SLVL_RCV_H_ */

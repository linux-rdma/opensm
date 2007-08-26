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
 * 	Declaration of osm_ni_rcv_t.
 *	This object represents the NodeInfo Receiver object.
 *	attribute from a node.
 *	This object is part of the OpenSM family of objects.
 *
 * Environment:
 * 	Linux User Mode
 *
 * $Revision: 1.4 $
 */

#ifndef _OSM_NI_RCV_H_
#define _OSM_NI_RCV_H_

#include <complib/cl_passivelock.h>
#include <opensm/osm_base.h>
#include <opensm/osm_madw.h>
#include <opensm/osm_vl15intf.h>
#include <opensm/osm_req.h>
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
/****h* OpenSM/Node Info Receiver
* NAME
*	Node Info Receiver
*
* DESCRIPTION
*	The Node Info Receiver object encapsulates the information
*	needed to receive the NodeInfo attribute from a node.
*
*	The Node Info Receiver object is thread safe.
*
*	This object should be treated as opaque and should be
*	manipulated only through the provided functions.
*
* AUTHOR
*	Steve King, Intel
*
*********/
/****s* OpenSM: Node Info Receiver/osm_ni_rcv_t
* NAME
*	osm_ni_rcv_t
*
* DESCRIPTION
*	Node Info Receiver structure.
*
*	This object should be treated as opaque and should
*	be manipulated only through the provided functions.
*
* SYNOPSIS
*/
typedef struct _osm_ni_rcv {
	osm_subn_t *p_subn;
	osm_req_t *p_gen_req;
	osm_log_t *p_log;
	cl_plock_t *p_lock;
} osm_ni_rcv_t;
/*
* FIELDS
*	p_subn
*		Pointer to the Subnet object for this subnet.
*
*	p_gen_req_ctrl
*		Pointer to the generic request controller.
*
*	p_log
*		Pointer to the log object.
*
*	p_lock
*		Pointer to the serializing lock.
*
* SEE ALSO
*	Node Info Receiver object
*********/

/****f* OpenSM: Node Info Receiver/osm_ni_rcv_construct
* NAME
*	osm_ni_rcv_construct
*
* DESCRIPTION
*	This function constructs a Node Info Receiver object.
*
* SYNOPSIS
*/
void osm_ni_rcv_construct(IN osm_ni_rcv_t * const p_ctrl);
/*
* PARAMETERS
*	p_ctrl
*		[in] Pointer to a Node Info Receiver object to construct.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Allows calling osm_ni_rcv_init, osm_ni_rcv_destroy,
*	and osm_ni_rcv_is_inited.
*
*	Calling osm_ni_rcv_construct is a prerequisite to calling any other
*	method except osm_ni_rcv_init.
*
* SEE ALSO
*	Node Info Receiver object, osm_ni_rcv_init,
*	osm_ni_rcv_destroy, osm_ni_rcv_is_inited
*********/

/****f* OpenSM: Node Info Receiver/osm_ni_rcv_destroy
* NAME
*	osm_ni_rcv_destroy
*
* DESCRIPTION
*	The osm_ni_rcv_destroy function destroys the object, releasing
*	all resources.
*
* SYNOPSIS
*/
void osm_ni_rcv_destroy(IN osm_ni_rcv_t * const p_ctrl);
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
*	Node Info Receiver object.
*	Further operations should not be attempted on the destroyed object.
*	This function should only be called after a call to
*	osm_ni_rcv_construct or osm_ni_rcv_init.
*
* SEE ALSO
*	Node Info Receiver object, osm_ni_rcv_construct,
*	osm_ni_rcv_init
*********/

/****f* OpenSM: Node Info Receiver/osm_ni_rcv_init
* NAME
*	osm_ni_rcv_init
*
* DESCRIPTION
*	The osm_ni_rcv_init function initializes a
*	Node Info Receiver object for use.
*
* SYNOPSIS
*/
ib_api_status_t osm_ni_rcv_init(IN osm_ni_rcv_t * const p_ctrl,
				IN osm_req_t * const p_req,
				IN osm_subn_t * const p_subn,
				IN osm_log_t * const p_log,
				IN cl_plock_t * const p_lock);
/*
* PARAMETERS
*	p_ctrl
*		[in] Pointer to an osm_ni_rcv_t object to initialize.
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
*	CL_SUCCESS if the Node Info Receiver object was initialized
*	successfully.
*
* NOTES
*	Allows calling other Node Info Receiver methods.
*
* SEE ALSO
*	Node Info Receiver object, osm_ni_rcv_construct,
*	osm_ni_rcv_destroy, osm_ni_rcv_is_inited
*********/

/****f* OpenSM: Node Info Receiver/osm_ni_rcv_is_inited
* NAME
*	osm_ni_rcv_is_inited
*
* DESCRIPTION
*	Indicates if the object has been initialized with osm_ni_rcv_init.
*
* SYNOPSIS
*/
boolean_t osm_ni_rcv_is_inited(IN const osm_ni_rcv_t * const p_ctrl);
/*
* PARAMETERS
*	p_ctrl
*		[in] Pointer to an osm_ni_rcv_t object.
*
* RETURN VALUES
*	TRUE if the object was initialized successfully,
*	FALSE otherwise.
*
* NOTES
*	The osm_ni_rcv_construct or osm_ni_rcv_init must be
*	called before using this function.
*
* SEE ALSO
*	Node Info Receiver object, osm_ni_rcv_construct,
*	osm_ni_rcv_init
*********/

/****f* OpenSM: Node Info Receiver/osm_ni_rcv_process
* NAME
*	osm_ni_rcv_process
*
* DESCRIPTION
*	Process the NodeInfo attribute.
*
* SYNOPSIS
*/
void osm_ni_rcv_process(IN void *context, IN void *data);
/*
* PARAMETERS
*	context
*		[in] Pointer to an osm_ni_rcv_t object.
*
*	data
*		[in] Pointer to the MAD Wrapper containing the MAD
*		that contains the node's NodeInfo attribute.
*
* RETURN VALUES
*	CL_SUCCESS if the NodeInfo processing was successful.
*
* NOTES
*	This function processes a NodeInfo attribute.
*
* SEE ALSO
*	Node Info Receiver, Node Info Response Controller
*********/

END_C_DECLS
#endif				/* _OSM_NI_RCV_H_ */

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
 * 	Declaration of osm_req_t.
 *	This object represents an object that generically requests
 *	attributes from a node.
 *	This object is part of the OpenSM family of objects.
 *
 * Environment:
 * 	Linux User Mode
 *
 * $Revision: 1.4 $
 */

#ifndef _OSM_REQ_H_
#define _OSM_REQ_H_

#include <complib/cl_atomic.h>
#include <complib/cl_dispatcher.h>
#include <opensm/osm_base.h>
#include <opensm/osm_log.h>
#include <opensm/osm_path.h>
#include <opensm/osm_subnet.h>
#include <opensm/osm_vl15intf.h>
#include <opensm/osm_madw.h>
#include <opensm/osm_mad_pool.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/****h* OpenSM/Generic Requester
* NAME
*	Generic Requester
*
* DESCRIPTION
*	The Generic Requester object encapsulates the information
*	needed to request an attribute from a node.
*
*	The Generic Requester object is thread safe.
*
*	This object should be treated as opaque and should be
*	manipulated only through the provided functions.
*
* AUTHOR
*	Steve King, Intel
*
*********/
/****s* OpenSM: Generic Requester/osm_req_t
* NAME
*	osm_req_t
*
* DESCRIPTION
*	Generic Requester structure.
*
*	This object should be treated as opaque and should
*	be manipulated only through the provided functions.
*
* SYNOPSIS
*/
typedef struct _osm_req {
	osm_mad_pool_t *p_pool;
	osm_vl15_t *p_vl15;
	osm_log_t *p_log;
	osm_subn_t *p_subn;
	atomic32_t *p_sm_trans_id;
} osm_req_t;
/*
* FIELDS
*	p_pool
*		Pointer to the MAD pool.
*
*	p_vl15
*		Pointer to the VL15 interface.
*
*	p_log
*		Pointer to the log object.
*
*	p_subn
*		Pointer to the subnet object.
*
*	p_sm_trans_id
*		Pointer to transaction ID.
*
* SEE ALSO
*	Generic Requester object
*********/

/****f* OpenSM: Generic Requester/osm_req_construct
* NAME
*	osm_req_construct
*
* DESCRIPTION
*	This function constructs a Generic Requester object.
*
* SYNOPSIS
*/
void osm_req_construct(IN osm_req_t * const p_req);
/*
* PARAMETERS
*	p_req
*		[in] Pointer to a Generic Requester object to construct.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Allows calling osm_req_init, and osm_req_destroy.
*
*	Calling osm_req_construct is a prerequisite to calling any other
*	method except osm_req_init.
*
* SEE ALSO
*	Generic Requester object, osm_req_init,
*	osm_req_destroy
*********/

/****f* OpenSM: Generic Requester/osm_req_destroy
* NAME
*	osm_req_destroy
*
* DESCRIPTION
*	The osm_req_destroy function destroys the object, releasing
*	all resources.
*
* SYNOPSIS
*/
void osm_req_destroy(IN osm_req_t * const p_req);
/*
* PARAMETERS
*	p_req
*		[in] Pointer to the object to destroy.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Performs any necessary cleanup of the specified
*	Generic Requester object.
*	Further operations should not be attempted on the destroyed object.
*	This function should only be called after a call to
*	osm_req_construct or osm_req_init.
*
* SEE ALSO
*	Generic Requester object, osm_req_construct,
*	osm_req_init
*********/

/****f* OpenSM: Generic Requester/osm_req_init
* NAME
*	osm_req_init
*
* DESCRIPTION
*	The osm_req_init function initializes a
*	Generic Requester object for use.
*
* SYNOPSIS
*/
ib_api_status_t
osm_req_init(IN osm_req_t * const p_req,
	     IN osm_mad_pool_t * const p_pool,
	     IN osm_vl15_t * const p_vl15,
	     IN osm_subn_t * const p_subn,
	     IN osm_log_t * const p_log, IN atomic32_t * const p_sm_trans_id);
/*
* PARAMETERS
*	p_req
*		[in] Pointer to an osm_req_t object to initialize.
*
*	p_mad_pool
*		[in] Pointer to the MAD pool.
*
*	p_vl15
*		[in] Pointer to the VL15 interface.
*
*	p_subn
*		[in] Pointer to the subnet object.
*
*	p_log
*		[in] Pointer to the log object.
*
*	p_sm_trans_id
*		[in] Pointer to the atomic SM transaction ID.
*
* RETURN VALUES
*	IB_SUCCESS if the Generic Requester object was initialized
*	successfully.
*
* NOTES
*	Allows calling other Generic Requester methods.
*
* SEE ALSO
*	Generic Requester object, osm_req_construct,
*	osm_req_destroy
*********/

END_C_DECLS
#endif				/* _OSM_REQ_H_ */

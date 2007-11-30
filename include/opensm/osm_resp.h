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
 * 	Declaration of osm_resp_t.
 *	This object represents an object that generically requests
 *	attributes from a node.
 *	This object is part of the OpenSM family of objects.
 *
 * Environment:
 * 	Linux User Mode
 *
 * $Revision: 1.4 $
 */

#ifndef _OSM_RESP_H_
#define _OSM_RESP_H_

#include <complib/cl_atomic.h>
#include <opensm/osm_base.h>
#include <complib/cl_dispatcher.h>
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
/****h* OpenSM/Generic Responder
* NAME
*	Generic Responder
*
* DESCRIPTION
*	The Generic Responder object encapsulates the information
*	needed to respond to an attribute from a node.
*
*	The Generic Responder object is thread safe.
*
*	This object should be treated as opaque and should be
*	manipulated only through the provided functions.
*
* AUTHOR
*	Steve King, Intel
*
*********/
/****s* OpenSM: Generic Responder/osm_resp_t
* NAME
*	osm_resp_t
*
* DESCRIPTION
*	Generic Responder structure.
*
*	This object should be treated as opaque and should
*	be manipulated only through the provided functions.
*
* SYNOPSIS
*/
typedef struct _osm_resp {
	osm_mad_pool_t *p_pool;
	osm_vl15_t *p_vl15;
	osm_log_t *p_log;
	osm_subn_t *p_subn;
} osm_resp_t;
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
* SEE ALSO
*	Generic Responder object
*********/

/****f* OpenSM: Generic Responder/osm_resp_construct
* NAME
*	osm_resp_construct
*
* DESCRIPTION
*	This function constructs a Generic Responder object.
*
* SYNOPSIS
*/
void osm_resp_construct(IN osm_resp_t * const p_resp);
/*
* PARAMETERS
*	p_resp
*		[in] Pointer to a Generic Responder object to construct.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Allows calling osm_resp_init, osm_resp_destroy
*
*	Calling osm_resp_construct is a prerequisite to calling any other
*	method except osm_resp_init.
*
* SEE ALSO
*	Generic Responder object, osm_resp_init,
*	osm_resp_destroy
*********/

/****f* OpenSM: Generic Responder/osm_resp_destroy
* NAME
*	osm_resp_destroy
*
* DESCRIPTION
*	The osm_resp_destroy function destroys the object, releasing
*	all resources.
*
* SYNOPSIS
*/
void osm_resp_destroy(IN osm_resp_t * const p_resp);
/*
* PARAMETERS
*	p_resp
*		[in] Pointer to the object to destroy.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Performs any necessary cleanup of the specified
*	Generic Responder object.
*	Further operations should not be attempted on the destroyed object.
*	This function should only be called after a call to
*	osm_resp_construct or osm_resp_init.
*
* SEE ALSO
*	Generic Responder object, osm_resp_construct,
*	osm_resp_init
*********/

/****f* OpenSM: Generic Responder/osm_resp_init
* NAME
*	osm_resp_init
*
* DESCRIPTION
*	The osm_resp_init function initializes a
*	Generic Responder object for use.
*
* SYNOPSIS
*/
ib_api_status_t
osm_resp_init(IN osm_resp_t * const p_resp,
	      IN osm_mad_pool_t * const p_pool,
	      IN osm_vl15_t * const p_vl15,
	      IN osm_subn_t * const p_subn, IN osm_log_t * const p_log);
/*
* PARAMETERS
*	p_resp
*		[in] Pointer to an osm_resp_t object to initialize.
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
* RETURN VALUES
*	IB_SUCCESS if the Generic Responder object was initialized
*	successfully.
*
* NOTES
*	Allows calling other Generic Responder methods.
*
* SEE ALSO
*	Generic Responder object, osm_resp_construct,
*	osm_resp_destroy
*********/

/****f* OpenSM: Generic Responder/osm_resp_send
* NAME
*	osm_resp_send
*
* DESCRIPTION
*	Starts the process to transmit a directed route response.
*
* SYNOPSIS
*/
ib_api_status_t
osm_resp_send(IN const osm_resp_t * const p_resp,
	      IN const osm_madw_t * const p_req_madw,
	      IN const ib_net16_t status, IN const uint8_t * const p_payload);
/*
* PARAMETERS
*	p_resp
*		[in] Pointer to an osm_resp_t object.
*
*	p_madw
*		[in] Pointer to the MAD Wrapper object for the requesting MAD
*		to which this response is generated.
*
*	status
*		[in] Status for this response.
*
*	p_payload
*		[in] Pointer to the payload of the response MAD.
*
* RETURN VALUES
*	IB_SUCCESS if the response was successful.
*
* NOTES
*
* SEE ALSO
*	Generic Responder
*********/

END_C_DECLS
#endif				/* _OSM_RESP_H_ */

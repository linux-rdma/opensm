/*
 * Copyright (c) 2006 Voltaire, Inc. All rights reserved.
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
 * 	Declaration of osm_gir_rcv_t.
 *	This object represents the GUIDInfo Record Receiver object.
 *	This object is part of the OpenSM family of objects.
 *
 * Environment:
 * 	Linux User Mode
 *
 */

#ifndef _OSM_GIR_RCV_H_
#define _OSM_GIR_RCV_H_

#include <complib/cl_passivelock.h>
#include <complib/cl_qlist.h>
#include <opensm/osm_base.h>
#include <opensm/osm_madw.h>
#include <opensm/osm_sa_response.h>
#include <opensm/osm_subnet.h>
#include <opensm/osm_port.h>
#include <opensm/osm_log.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else /* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif /* __cplusplus */

BEGIN_C_DECLS

/****h* OpenSM/GUIDInfo Record Receiver
* NAME
*	GUIDInfo Record Receiver
*
* DESCRIPTION
*	The GUIDInfo Record Receiver object encapsulates the information
*	needed to receive the GUIDInfoRecord attribute from a node.
*
*	The GUIDInfo Record Receiver object is thread safe.
*
*	This object should be treated as opaque and should be
*	manipulated only through the provided functions.
*
* AUTHOR
*	Hal Rosenstock, Voltaire
*
*********/

/****s* OpenSM: GUIDInfo Record Receiver/osm_gir_rcv_t
* NAME
*	osm_gir_rcv_t
*
* DESCRIPTION
*	GUIDInfo Record Receiver structure.
*
*	This object should be treated as opaque and should
*	be manipulated only through the provided functions.
*
* SYNOPSIS
*/
typedef struct _osm_gir_rcv
{
	const osm_subn_t	*p_subn;
	osm_sa_resp_t		*p_resp;
	osm_mad_pool_t		*p_mad_pool;
	osm_log_t		*p_log;
	cl_plock_t		*p_lock;
	cl_qlock_pool_t		pool;
} osm_gir_rcv_t;
/*
* FIELDS
*	p_subn
*		Pointer to the Subnet object for this subnet.
*
*	p_resp
*		Pointer to the SA responder.
*
*	p_mad_pool
*		Pointer to the mad pool.
*
*	p_log
*		Pointer to the log object.
*
*	p_lock
*		Pointer to the serializing lock.
*
*	pool
*		Pool of linkable GUIDInfo Record objects used to generate
*		the query response.
*
* SEE ALSO
*
*********/

/****f* OpenSM: GUIDInfo Record Receiver/osm_gir_rcv_construct
* NAME
*	osm_gir_rcv_construct
*
* DESCRIPTION
*	This function constructs a GUIDInfo Record Receiver object.
*
* SYNOPSIS
*/
void
osm_gir_rcv_construct(
	IN osm_gir_rcv_t* const p_rcv );
/*
* PARAMETERS
*	p_rcv
*		[in] Pointer to a GUIDInfo Record Receiver object to construct.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Allows calling osm_gir_rcv_init, osm_gir_rcv_destroy
*
*	Calling osm_gir_rcv_construct is a prerequisite to calling any other
*	method except osm_gir_rcv_init.
*
* SEE ALSO
*	GUIDInfo Record Receiver object, osm_gir_rcv_init,
*	osm_gir_rcv_destroy
*********/

/****f* OpenSM: GUIDInfo Record Receiver/osm_gir_rcv_destroy
* NAME
*	osm_gir_rcv_destroy
*
* DESCRIPTION
*	The osm_gir_rcv_destroy function destroys the object, releasing
*	all resources.
*
* SYNOPSIS
*/
void
osm_gir_rcv_destroy(
	IN osm_gir_rcv_t* const p_rcv );
/*
* PARAMETERS
*	p_rcv
*		[in] Pointer to the object to destroy.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Performs any necessary cleanup of the specified
*	GUIDInfo Record Receiver object.
*	Further operations should not be attempted on the destroyed object.
*	This function should only be called after a call to
*	osm_gir_rcv_construct or osm_gir_rcv_init.
*
* SEE ALSO
*	GUIDInfo Record Receiver object, osm_gir_rcv_construct,
*	osm_gir_rcv_init
*********/

/****f* OpenSM: GUIDInfo Record Receiver/osm_gir_rcv_init
* NAME
*	osm_gir_rcv_init
*
* DESCRIPTION
*	The osm_gir_rcv_init function initializes a
*	GUIDInfo Record Receiver object for use.
*
* SYNOPSIS
*/
ib_api_status_t
osm_gir_rcv_init(
	IN osm_gir_rcv_t* const p_rcv,
	IN osm_sa_resp_t* const p_resp,
	IN osm_mad_pool_t* const p_mad_pool,
	IN const osm_subn_t* const p_subn,
	IN osm_log_t* const p_log,
	IN cl_plock_t* const p_lock );
/*
* PARAMETERS
*	p_rcv
*		[in] Pointer to an osm_gir_rcv_t object to initialize.
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
*	CL_SUCCESS if the GUIDInfo Record Receiver object was initialized
*	successfully.
*
* NOTES
*	Allows calling other GUIDInfo Record Receiver methods.
*
* SEE ALSO
*	GUIDInfo Record Receiver object, osm_gir_rcv_construct,
*	osm_gir_rcv_destroy
*********/

/****f* OpenSM: GUIDInfo Record Receiver/osm_gir_rcv_process
* NAME
*	osm_gir_rcv_process
*
* DESCRIPTION
*	Process the GUIDInfoRecord attribute.
*
* SYNOPSIS
*/
void
osm_gir_rcv_process(
	IN void *context,
	IN void *data );
/*
* PARAMETERS
*	context
*		[in] Pointer to an osm_gir_rcv_t object.
*
*	data
*		[in] Pointer to the MAD Wrapper containing the MAD
*		that contains the node's GUIDInfoRecord attribute.
*
* RETURN VALUES
*	CL_SUCCESS if the GUIDInfoRecord processing was successful.
*
* NOTES
*	This function processes a GUIDInfoRecord attribute.
*
* SEE ALSO
*	GUIDInfo Record Receiver, GUIDInfo Record Response Controller
*********/

END_C_DECLS

#endif	/* _OSM_GIR_RCV_H_ */

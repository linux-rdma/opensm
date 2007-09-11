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
 * 	Declaration of osm_sa_resp_t.
 *	This object represents an object that responds to SA queries.
 *	This object is part of the OpenSM family of objects.
 *
 * Environment:
 * 	Linux User Mode
 *
 * $Revision: 1.4 $
 */

#ifndef _OSM_SA_RESP_H_
#define _OSM_SA_RESP_H_

#include <opensm/osm_base.h>
#include <opensm/osm_log.h>
#include <opensm/osm_madw.h>
#include <opensm/osm_mad_pool.h>
#include <opensm/osm_subnet.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/****h* OpenSM/SA Response
* NAME
*	SA Response
*
* DESCRIPTION
*	The SA Response object encapsulates the information
*	needed to respond to an SA query.
*
*	The SA Response object is thread safe.
*
*	This object should be treated as opaque and should be
*	manipulated only through the provided functions.
*
* AUTHOR
*	Ranjit Pandit, Intel
*	Steve King, Intel
*
*********/
/****s* OpenSM: SA Response/osm_sa_resp_t
* NAME
*	osm_sa_resp_t
*
* DESCRIPTION
*	SA Response structure.
*
*	This object should be treated as opaque and should
*	be manipulated only through the provided functions.
*
* SYNOPSIS
*/
typedef struct _osm_sa_resp {
	osm_mad_pool_t *p_pool;
	osm_subn_t *p_subn;
	osm_log_t *p_log;
} osm_sa_resp_t;
/*
* FIELDS
*	p_pool
*		Pointer to the MAD pool.
*
* SEE ALSO
*	SA Response object
*********/

/****f* OpenSM: SA Response/osm_sa_resp_construct
* NAME
*	osm_sa_resp_construct
*
* DESCRIPTION
*	This function constructs a SA Response object.
*
* SYNOPSIS
*/
void osm_sa_resp_construct(IN osm_sa_resp_t * const p_resp);
/*
* PARAMETERS
*	p_resp
*		[in] Pointer to a SA Response object to construct.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Allows calling osm_sa_resp_init, and osm_sa_resp_destroy.
*
*	Calling osm_sa_resp_construct is a prerequisite to calling any other
*	method except osm_sa_resp_init.
*
* SEE ALSO
*	SA Response object, osm_sa_resp_init,
*	osm_sa_resp_destroy
*********/

/****f* OpenSM: SA Response/osm_sa_resp_destroy
* NAME
*	osm_sa_resp_destroy
*
* DESCRIPTION
*	The osm_sa_resp_destroy function destroys the object, releasing
*	all resources.
*
* SYNOPSIS
*/
void osm_sa_resp_destroy(IN osm_sa_resp_t * const p_resp);
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
*	SA Response object.
*	Further operations should not be attempted on the destroyed object.
*	This function should only be called after a call to
*	osm_sa_resp_construct or osm_sa_resp_init.
*
* SEE ALSO
*	SA Response object, osm_sa_resp_construct,
*	osm_sa_resp_init
*********/

/****f* OpenSM: SA Response/osm_sa_resp_init
* NAME
*	osm_sa_resp_init
*
* DESCRIPTION
*	The osm_sa_resp_init function initializes a
*	SA Response object for use.
*
* SYNOPSIS
*/
ib_api_status_t
osm_sa_resp_init(IN osm_sa_resp_t * const p_resp,
		 IN osm_mad_pool_t * const p_pool,
		 IN osm_subn_t * const p_subn,
		 IN osm_log_t * const p_log);
/*
* PARAMETERS
*	p_resp
*		[in] Pointer to an osm_sa_resp_t object to initialize.
*
*	p_mad_pool
*		[in] Pointer to the MAD pool.
*
*	p_subn
*		[in] Pointer to Subnet object for this subnet.
*
*	p_log
*		[in] Pointer to the log object.
*
* RETURN VALUES
*	IB_SUCCESS if the SA Response object was initialized
*	successfully.
*
* NOTES
*	Allows calling other SA Response methods.
*
* SEE ALSO
*	SA Response object, osm_sa_resp_construct,
*	osm_sa_resp_destroy
*********/

/****f* IBA Base: Types/osm_sa_send_error
* NAME
*	osm_sa_send_error
*
* DESCRIPTION
*	Sends a generic SA response with the specified error status.
*	The payload is simply replicated from the request MAD.
*
* SYNOPSIS
*/
void
osm_sa_send_error(IN osm_sa_resp_t * const p_resp,
		  IN const osm_madw_t * const p_madw,
		  IN const ib_net16_t sa_status);
/*
* PARAMETERS
*	p_resp
*		[in] Pointer to an osm_sa_resp_t object.
*
*	p_madw
*		[in] Original MAD to which the response must be sent.
*
*	sa_status
*		[in] Status to send in the response.
*
* RETURN VALUES
*	None.
*
* NOTES
*	Allows calling other SA Response methods.
*
* SEE ALSO
*	SA Response object, osm_sa_resp_construct,
*	osm_sa_resp_destroy
*********/

END_C_DECLS
#endif				/* _OSM_SA_RESP_H_ */

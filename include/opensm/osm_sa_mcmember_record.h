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
 * 	Declaration of osm_mcmr_recv_t.
 *	This object represents the MCMemberRecord Receiver object.
 *	attribute from a node.
 *	This object is part of the OpenSM family of objects.
 *
 * Environment:
 * 	Linux User Mode
 *
 * $Revision: 1.7 $
 */

#ifndef _OSM_MCMR_H_
#define _OSM_MCMR_H_

#include <complib/cl_passivelock.h>
#include <opensm/osm_base.h>
#include <opensm/osm_madw.h>
#include <opensm/osm_sa_response.h>
#include <opensm/osm_subnet.h>
#include <opensm/osm_sm.h>
#include <opensm/osm_multicast.h>
#include <opensm/osm_log.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/****h* OpenSM/MCMember Receiver
* NAME
*	MCMember Receiver
*
* DESCRIPTION
*	The MCMember Receiver object encapsulates the information
*	needed to receive the MCMemberRecord attribute from a node.
*
*	The MCMember Receiver object is thread safe.
*
*	This object should be treated as opaque and should be
*	manipulated only through the provided functions.
*
* AUTHOR
*	Anil Keshavamurthy, Intel
*
*********/
/****s* OpenSM: MCMember Receiver/osm_mcmr_recv_t
* NAME
*	osm_mcmr_recv_t
*
* DESCRIPTION
*	MCMember Receiver structure.
*
*	This object should be treated as opaque and should
*	be manipulated only through the provided functions.
*
* SYNOPSIS
*/
typedef struct _osm_mcmr {
	osm_subn_t *p_subn;
	osm_sm_t *p_sm;
	osm_sa_resp_t *p_resp;
	osm_mad_pool_t *p_mad_pool;
	osm_log_t *p_log;
	cl_plock_t *p_lock;
} osm_mcmr_recv_t;

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
*	MCMember Receiver object
*********/

/****f* OpenSM: MCMember Receiver/osm_mcmr_rcv_construct
* NAME
*	osm_mcmr_rcv_construct
*
* DESCRIPTION
*	This function constructs a MCMember Receiver object.
*
* SYNOPSIS
*/
void osm_mcmr_rcv_construct(IN osm_mcmr_recv_t * const p_ctrl);
/*
* PARAMETERS
*	p_ctrl
*		[in] Pointer to a MCMember Receiver object to construct.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Allows calling osm_mcmr_rcv_init, osm_mcmr_rcv_destroy
*
*	Calling osm_mcmr_rcv_construct is a prerequisite to calling any other
*	method except osm_mcmr_init.
*
* SEE ALSO
*	MCMember Receiver object, osm_mcmr_init,
*	osm_mcmr_rcv_destroy
*********/

/****f* OpenSM: MCMember Receiver/osm_mcmr_rcv_destroy
* NAME
*	osm_mcmr_rcv_destroy
*
* DESCRIPTION
*	The osm_mcmr_rcv_destroy function destroys the object, releasing
*	all resources.
*
* SYNOPSIS
*/
void osm_mcmr_rcv_destroy(IN osm_mcmr_recv_t * const p_ctrl);
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
*	MCMember Receiver object.
*	Further operations should not be attempted on the destroyed object.
*	This function should only be called after a call to
*	osm_mcmr_rcv_construct or osm_mcmr_init.
*
* SEE ALSO
*	MCMember Receiver object, osm_mcmr_rcv_construct,
*	osm_mcmr_init
*********/

/****f* OpenSM: MCMember Receiver/osm_mcmr_rcv_init
* NAME
*	osm_mcmr_init
*
* DESCRIPTION
*	The osm_mcmr_init function initializes a
*	MCMember Receiver object for use.
*
* SYNOPSIS
*/
ib_api_status_t osm_mcmr_rcv_init(IN osm_sm_t * const p_sm,
				  IN osm_mcmr_recv_t * const p_ctrl,
				  IN osm_sa_resp_t * const p_resp,
				  IN osm_mad_pool_t * const p_mad_pool,
				  IN osm_subn_t * const p_subn,
				  IN osm_log_t * const p_log,
				  IN cl_plock_t * const p_lock);
/*
* PARAMETERS
*	p_sm
*		[in] pointer to osm_sm_t object
*	p_ctrl
*		[in] Pointer to an osm_mcmr_recv_t object to initialize.
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
*	CL_SUCCESS if the MCMember Receiver object was initialized
*	successfully.
*
* NOTES
*	Allows calling other MCMember Receiver methods.
*
* SEE ALSO
*	MCMember Receiver object, osm_mcmr_rcv_construct,
*	osm_mcmr_rcv_destroy
*********/

/****f* OpenSM: MCMember Receiver/osm_mcmr_rcv_process
* NAME
*	osm_mcmr_rcv_process
*
* DESCRIPTION
*	Process the MCMemberRecord attribute.
*
* SYNOPSIS
*/
void osm_mcmr_rcv_process(IN void *context, IN void *data);
/*
* PARAMETERS
*	context
*		[in] Pointer to an osm_mcmr_recv_t object.
*
*	data
*		[in] Pointer to the MAD Wrapper containing the MAD
*		that contains the node's MCMemberRecord attribute.
*
* RETURN VALUES
*	CL_SUCCESS if the MCMemberRecord processing was successful.
*
* NOTES
*	This function processes a MCMemberRecord attribute.
*
* SEE ALSO
*	MCMember Receiver, MCMember Response Controller
*********/

/****f* OpenSM: MC Member Record Receiver/osm_mcmr_rcv_create_new_mgrp
* NAME
*	osm_mcmr_rcv_create_new_mgrp
*
* DESCRIPTION
*	Create new Multicast group
*
* SYNOPSIS
*/

ib_api_status_t
osm_mcmr_rcv_create_new_mgrp(IN osm_mcmr_recv_t * const p_mcmr,
			     IN uint64_t comp_mask,
			     IN const ib_member_rec_t *
			     const p_recvd_mcmember_rec,
			     IN const osm_physp_t * const p_req_physp,
			     OUT osm_mgrp_t ** pp_mgrp);
/*
* PARAMETERS
*	p_mcmr
*		[in] Pointer to an osm_mcmr_recv_t object.
*	p_recvd_mcmember_rec
*		[in] Received Multicast member record
*
*  p_req_physp
*     [in] The requesting osm_physp_t object.
*     NULL if the creation is without a requesting port (e.g - ipoib known mcgroups)
*
*	pp_mgrp
*		[out] pointer the osm_mgrp_t object
*
* RETURN VALUES
*	IB_SUCCESS, IB_ERROR
*
* NOTES
*
*
* SEE ALSO
*
*********/

END_C_DECLS
#endif				/* _OSM_MCMR_H_ */

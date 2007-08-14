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
 * 	Declaration of osm_ucast_mgr_t.
 *	This object represents the Unicast Manager object.
 *	This object is part of the OpenSM family of objects.
 *
 * Environment:
 * 	Linux User Mode
 *
 * $Revision: 1.4 $
 */

#ifndef _OSM_UCAST_MGR_H_
#define _OSM_UCAST_MGR_H_

#include <complib/cl_passivelock.h>
#include <opensm/osm_base.h>
#include <opensm/osm_madw.h>
#include <opensm/osm_req.h>
#include <opensm/osm_subnet.h>
#include <opensm/osm_switch.h>
#include <opensm/osm_log.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
#define OSM_UCAST_MGR_LIST_SIZE_MIN 256
/****h* OpenSM/Unicast Manager
* NAME
*	Unicast Manager
*
* DESCRIPTION
*	The Unicast Manager object encapsulates the information
*	needed to control unicast LID forwarding on the subnet.
*
*	The Unicast Manager object is thread safe.
*
*	This object should be treated as opaque and should be
*	manipulated only through the provided functions.
*
* AUTHOR
*	Steve King, Intel
*
*********/
/****s* OpenSM: Unicast Manager/osm_ucast_mgr_t
* NAME
*	osm_ucast_mgr_t
*
* DESCRIPTION
*	Unicast Manager structure.
*
*	This object should be treated as opaque and should
*	be manipulated only through the provided functions.
*
* SYNOPSIS
*/
typedef struct _osm_ucast_mgr {
	osm_subn_t *p_subn;
	osm_req_t *p_req;
	osm_log_t *p_log;
	cl_plock_t *p_lock;
	boolean_t any_change;
	boolean_t some_hop_count_set;
	uint8_t *lft_buf;
} osm_ucast_mgr_t;
/*
* FIELDS
*	p_subn
*		Pointer to the Subnet object for this subnet.
*
*	p_req
*		Pointer to the Requester object sending SMPs.
*
*	p_log
*		Pointer to the log object.
*
*	p_lock
*		Pointer to the serializing lock.
*
*	any_change
*		Initialized to FALSE at the beginning of the algorithm,
*		set to TRUE by osm_ucast_mgr_set_fwd_table() if any mad
*		was sent.
*
*	some_hop_count_set
*		Initialized to FALSE at the beginning of each the min hop
*		tables calculation iteration cycle, set to TRUE to indicate
*		that some hop count changes were done.
*
*	lft_buf
*		LFT buffer - used during LFT calculation/setup.
*
* SEE ALSO
*	Unicast Manager object
*********/

/****f* OpenSM: Unicast Manager/osm_ucast_mgr_construct
* NAME
*	osm_ucast_mgr_construct
*
* DESCRIPTION
*	This function constructs a Unicast Manager object.
*
* SYNOPSIS
*/
void osm_ucast_mgr_construct(IN osm_ucast_mgr_t * const p_mgr);
/*
* PARAMETERS
*	p_mgr
*		[in] Pointer to a Unicast Manager object to construct.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Allows osm_ucast_mgr_destroy
*
*	Calling osm_ucast_mgr_construct is a prerequisite to calling any other
*	method except osm_ucast_mgr_init.
*
* SEE ALSO
*	Unicast Manager object, osm_ucast_mgr_init,
*	osm_ucast_mgr_destroy
*********/

/****f* OpenSM: Unicast Manager/osm_ucast_mgr_destroy
* NAME
*	osm_ucast_mgr_destroy
*
* DESCRIPTION
*	The osm_ucast_mgr_destroy function destroys the object, releasing
*	all resources.
*
* SYNOPSIS
*/
void osm_ucast_mgr_destroy(IN osm_ucast_mgr_t * const p_mgr);
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
*	Unicast Manager object.
*	Further operations should not be attempted on the destroyed object.
*	This function should only be called after a call to
*	osm_ucast_mgr_construct or osm_ucast_mgr_init.
*
* SEE ALSO
*	Unicast Manager object, osm_ucast_mgr_construct,
*	osm_ucast_mgr_init
*********/

/****f* OpenSM: Unicast Manager/osm_ucast_mgr_init
* NAME
*	osm_ucast_mgr_init
*
* DESCRIPTION
*	The osm_ucast_mgr_init function initializes a
*	Unicast Manager object for use.
*
* SYNOPSIS
*/
ib_api_status_t
osm_ucast_mgr_init(IN osm_ucast_mgr_t * const p_mgr,
		   IN osm_req_t * const p_req,
		   IN osm_subn_t * const p_subn,
		   IN osm_log_t * const p_log, IN cl_plock_t * const p_lock);
/*
* PARAMETERS
*	p_mgr
*		[in] Pointer to an osm_ucast_mgr_t object to initialize.
*
*	p_req
*		[in] Pointer to the attribute Requester object.
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
*	IB_SUCCESS if the Unicast Manager object was initialized
*	successfully.
*
* NOTES
*	Allows calling other Unicast Manager methods.
*
* SEE ALSO
*	Unicast Manager object, osm_ucast_mgr_construct,
*	osm_ucast_mgr_destroy
*********/

/****f* OpenSM: Unicast Manager/osm_ucast_mgr_set_fwd_table
* NAME
*	osm_ucast_mgr_set_fwd_table
*
* DESCRIPTION
*	Setup forwarding table for the switch (from prepared lft_buf).
*
* SYNOPSIS
*/
void
osm_ucast_mgr_set_fwd_table(IN osm_ucast_mgr_t * const p_mgr,
			    IN osm_switch_t * const p_sw);
/*
* PARAMETERS
*	p_mgr
*		[in] Pointer to an osm_ucast_mgr_t object.
*
*	p_mgr
*		[in] Pointer to an osm_switch_t object.
*
* SEE ALSO
*	Unicast Manager
*********/

/****f* OpenSM: Unicast Manager/osm_ucast_mgr_build_lid_matrices
* NAME
*	osm_ucast_mgr_build_lid_matrices
*
* DESCRIPTION
*	Build switches's lid matrices.
*
* SYNOPSIS
*/
void osm_ucast_mgr_build_lid_matrices(IN osm_ucast_mgr_t * const p_mgr);
/*
* PARAMETERS
*	p_mgr
*		[in] Pointer to an osm_ucast_mgr_t object.
*
* NOTES
*	This function processes the subnet, configuring switches'
*	min hops tables (aka lid matrices).
*
* SEE ALSO
*	Unicast Manager
*********/

/****f* OpenSM: Unicast Manager/osm_ucast_mgr_read_guid_file
* NAME
*	osm_ucast_mgr_read_guid_file
*
* DESCRIPTION
*	Read guid list from file.
*
* SYNOPSIS
*/
cl_status_t
osm_ucast_mgr_read_guid_file(IN osm_ucast_mgr_t * const p_mgr,
			     IN const char *guid_file_name,
			     IN cl_list_t * p_list);
/*
* PARAMETERS
*	p_mgr
*		[in] Pointer to an osm_ucast_mgr_t object.
*
*	guid_file_name
*		[in] Name of the file to read.
*
*	p_list
*		[in] Pointer to the list that will be filled with guids.
*
* RETURN VALUES
*	IB_SUCCESS if the file was read successfully.
*
* NOTES
*	This function reads guids from a file and inserts them
*	into a list.
*
* SEE ALSO
*	Unicast Manager
*********/

/****f* OpenSM: Unicast Manager/osm_ucast_mgr_process
* NAME
*	osm_ucast_mgr_process
*
* DESCRIPTION
*	Process and configure the subnet's unicast forwarding tables.
*
* SYNOPSIS
*/
osm_signal_t osm_ucast_mgr_process(IN osm_ucast_mgr_t * const p_mgr);
/*
* PARAMETERS
*	p_mgr
*		[in] Pointer to an osm_ucast_mgr_t object.
*
* RETURN VALUES
*	Returns the appropriate signal to the caller:
*		OSM_SIGNAL_DONE - operation is complete
*		OSM_SIGNAL_DONE_PENDING - local operations are complete, but
*			transactions are still pending on the wire.
*
* NOTES
*	This function processes the subnet, configuring switch
*	unicast forwarding tables.
*
* SEE ALSO
*	Unicast Manager, Node Info Response Controller
*********/
END_C_DECLS
#endif				/* _OSM_UCAST_MGR_H_ */

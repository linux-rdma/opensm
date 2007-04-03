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
 * 	Declaration of osm_mcm_port_t.
 *	This object represents the membership of a port in a multicast group.
 *	This object is part of the OpenSM family of objects.
 *
 * Environment:
 * 	Linux User Mode
 *
 * $Revision: 1.4 $
 */

#ifndef _OSM_MCM_PORT_H_
#define _OSM_MCM_PORT_H_

#include <iba/ib_types.h>
#include <complib/cl_qmap.h>
#include <opensm/osm_base.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else /* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif /* __cplusplus */

BEGIN_C_DECLS

/****s* OpenSM: MCM Port Object/osm_mcm_port_t
* NAME
*   osm_mcm_port_t
*
* DESCRIPTION
*   This object represents a particular	port as a member of a
*	multicast group.
*
*   This object should be treated as opaque and should
*   be manipulated only through the provided functions.
*
* SYNOPSIS
*/
typedef struct _osm_mcm_port
{
	cl_map_item_t	map_item;
	ib_gid_t	port_gid;
	uint8_t		scope_state;
	boolean_t	proxy_join;
} osm_mcm_port_t;
/*
* FIELDS
*	map_item
*		Map Item for qmap linkage.  Must be first element!!
*
*	port_gid
*		GID of the member port.
*
*	scope_state
*		???
*
*	proxy_join
*		If FALSE - Join was performed by the endport identified
*		by PortGID. If TRUE - Join was performed on behalf of
*		the endport identified by PortGID by another port within
*		the same partition.
*
* SEE ALSO
*	MCM Port Object
*********/

/****f* OpenSM: MCM Port Object/osm_mcm_port_construct
* NAME
*	osm_mcm_port_construct
*
* DESCRIPTION
*	This function constructs a MCM Port object.
*
* SYNOPSIS
*/
void
osm_mcm_port_construct(
	IN osm_mcm_port_t* const p_mcm );
/*
* PARAMETERS
*	p_mcm
*		[in] Pointer to a MCM Port Object to construct.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Allows calling osm_mcm_port_init, osm_mcm_port_destroy.
*
*	Calling osm_mcm_port_construct is a prerequisite to calling any other
*	method except osm_mcm_port_init.
*
* SEE ALSO
*	MCM Port Object, osm_mcm_port_init, osm_mcm_port_destroy
*********/

/****f* OpenSM: MCM Port Object/osm_mcm_port_destroy
* NAME
*	osm_mcm_port_destroy
*
* DESCRIPTION
*	The osm_mcm_port_destroy function destroys a MCM Port Object, releasing
*	all resources.
*
* SYNOPSIS
*/
void
osm_mcm_port_destroy(
	IN osm_mcm_port_t* const p_mcm );
/*
* PARAMETERS
*	p_mcm
*		[in] Pointer to a MCM Port Object to destroy.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Performs any necessary cleanup of the specified MCM Port Object.
*	Further operations should not be attempted on the destroyed object.
*	This function should only be called after a call to
*	osm_mcm_port_construct or osm_mcm_port_init.
*
* SEE ALSO
*	MCM Port Object, osm_mcm_port_construct, osm_mcm_port_init
*********/

/****f* OpenSM: MCM Port Object/osm_mcm_port_init
* NAME
*	osm_mcm_port_init
*
* DESCRIPTION
*	The osm_mcm_port_init function initializes a MCM Port Object for use.
*
* SYNOPSIS
*/
void
osm_mcm_port_init(
	IN osm_mcm_port_t* const p_mcm,
	IN const ib_gid_t* const p_port_gid,
	IN const uint8_t   scope_state,
   IN const boolean_t proxy_join );
/*
* PARAMETERS
*	p_mcm
*		[in] Pointer to an osm_mcm_port_t object to initialize.
*
*	p_port_gid
*		[in] Pointer to the GID of the port to add to the multicast group.
*
*	scope_state
*		[in] scope state of the join request
*
*  proxy_join
*     [in] proxy_join state analyzed from the request
*
* RETURN VALUES
*	None.
*
* NOTES
*	Allows calling other MCM Port Object methods.
*
* SEE ALSO
*	MCM Port Object, osm_mcm_port_construct, osm_mcm_port_destroy,
*********/

/****f* OpenSM: MCM Port Object/osm_mcm_port_init
* NAME
*	osm_mcm_port_init
*
* DESCRIPTION
*	The osm_mcm_port_init function initializes a MCM Port Object for use.
*
* SYNOPSIS
*/
osm_mcm_port_t*
osm_mcm_port_new(
	IN const ib_gid_t* const p_port_gid,
	IN const uint8_t   scope_state,
   IN const boolean_t proxy_join );
/*
* PARAMETERS
*	p_port_gid
*		[in] Pointer to the GID of the port to add to the multicast group.
*
*	scope_state
*		[in] scope state of the join request
*
*  proxy_join
*     [in] proxy_join state analyzed from the request
*
* RETURN VALUES
*	Pointer to the allocated and initialized MCM Port object.
*
* NOTES
*
* SEE ALSO
*	MCM Port Object, osm_mcm_port_construct, osm_mcm_port_destroy,
*********/

/****f* OpenSM: MCM Port Object/osm_mcm_port_destroy
* NAME
*	osm_mcm_port_destroy
*
* DESCRIPTION
*	The osm_mcm_port_destroy function destroys and dellallocates an
*	MCM Port Object, releasing all resources.
*
* SYNOPSIS
*/
void
osm_mcm_port_delete(
	IN osm_mcm_port_t* const p_mcm );
/*
* PARAMETERS
*	p_mcm
*		[in] Pointer to a MCM Port Object to delete.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*
* SEE ALSO
*	MCM Port Object, osm_mcm_port_construct, osm_mcm_port_init
*********/

END_C_DECLS

#endif		/* _OSM_MCM_PORT_H_ */

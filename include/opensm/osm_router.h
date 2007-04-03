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
 * 	Declaration of osm_router_t.
 *	This object represents an IBA router.
 *	This object is part of the OpenSM family of objects.
 *
 * Environment:
 * 	Linux User Mode
 *
 */

#ifndef _OSM_ROUTER_H_
#define _OSM_ROUTER_H_

#include <iba/ib_types.h>
#include <opensm/osm_base.h>
#include <opensm/osm_madw.h>
#include <opensm/osm_node.h>
#include <opensm/osm_port.h>
#include <opensm/osm_fwd_tbl.h>
#include <opensm/osm_mcast_tbl.h>
#include <opensm/osm_port_profile.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else /* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif /* __cplusplus */

BEGIN_C_DECLS

/****h* OpenSM/Router
* NAME
*	Router
*
* DESCRIPTION
*	The Router object encapsulates the information needed by the
*	OpenSM to manage routers.  The OpenSM allocates one router object
*	per router in the IBA subnet.
*
*	The Router object is not thread safe, thus callers must provide
*	serialization.
*
*	This object should be treated as opaque and should be
*	manipulated only through the provided functions.
*
* AUTHOR
*	Hal Rosenstock, Voltaire 
*
*********/

/****s* OpenSM: Router/osm_router_t
* NAME
*	osm_router_t
*
* DESCRIPTION
*	Router structure.
*
*	This object should be treated as opaque and should
*	be manipulated only through the provided functions.
*
* SYNOPSIS
*/
typedef struct _osm_router
{
	cl_map_item_t				map_item;
	osm_port_t				*p_port;
} osm_router_t;
/*
* FIELDS
*	map_item
*		Linkage structure for cl_qmap.  MUST BE FIRST MEMBER!
*
*	p_port
*		Pointer to the Port object for this router.
*
* SEE ALSO
*	Router object
*********/

/****f* OpenSM: Router/osm_router_construct
* NAME
*	osm_router_construct
*
* DESCRIPTION
*	This function constructs a Router object.
*
* SYNOPSIS
*/
void
osm_router_construct(
	IN osm_router_t* const p_rtr );
/*
* PARAMETERS
*	p_rtr
*		[in] Pointer to a Router object to construct.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Allows calling osm_router_init, and osm_router_destroy.
*
*	Calling osm_router_construct is a prerequisite to calling any other
*	method except osm_router_init.
*
* SEE ALSO
*	Router object, osm_router_init, osm_router_destroy
*********/

/****f* OpenSM: Router/osm_router_destroy
* NAME
*	osm_router_destroy
*
* DESCRIPTION
*	The osm_router_destroy function destroys the object, releasing
*	all resources.
*
* SYNOPSIS
*/
void
osm_router_destroy(
	IN osm_router_t* const p_rtr );
/*
* PARAMETERS
*	p_rtr
*		[in] Pointer to the object to destroy.
*
* RETURN VALUE
*	None.
*
* NOTES
*	Performs any necessary cleanup of the specified object.
*	Further operations should not be attempted on the destroyed object.
*	This function should only be called after a call to osm_router_construct
*	or osm_router_init.
*
* SEE ALSO
*	Router object, osm_router_construct, osm_router_init
*********/

/****f* OpenSM: Router/osm_router_destroy
* NAME
*	osm_router_destroy
*
* DESCRIPTION
*	Destroys and deallocates the object.
*
* SYNOPSIS
*/
void
osm_router_delete(
	IN OUT osm_router_t** const pp_rtr );
/*
* PARAMETERS
*	p_rtr
*		[in] Pointer to the object to destroy.
*
* RETURN VALUE
*	None.
*
* NOTES
*
* SEE ALSO
*	Router object, osm_router_construct, osm_router_init
*********/

/****f* OpenSM: Router/osm_router_init
* NAME
*	osm_router_init
*
* DESCRIPTION
*	The osm_router_init function initializes a Router object for use.
*
* SYNOPSIS
*/
ib_api_status_t
osm_router_init(
	IN osm_router_t* const p_rtr,
	IN osm_port_t* const p_port );
/*
* PARAMETERS
*	p_rtr
*		[in] Pointer to an osm_router_t object to initialize.
*
*	p_port
*		[in] Pointer to the port object of this router 
*
* RETURN VALUES
*	IB_SUCCESS if the Router object was initialized successfully.
*
* NOTES
*	Allows calling other node methods.
*
* SEE ALSO
*	Router object, osm_router_construct, osm_router_destroy
*********/

/****f* OpenSM: Router/osm_router_new
* NAME
*	osm_router_new
*
* DESCRIPTION
*	The osm_router_init function initializes a Router object for use.
*
* SYNOPSIS
*/
osm_router_t*
osm_router_new(
	IN osm_port_t* const p_port );
/*
* PARAMETERS
*	p_node
*		[in] Pointer to the node object of this router
*
* RETURN VALUES
*	Pointer to the new initialized router object.
*
* NOTES
*
* SEE ALSO
*	Router object, osm_router_construct, osm_router_destroy,
*********/

/****f* OpenSM: Router/osm_router_get_port_ptr
* NAME
*	osm_router_get_port_ptr
*
* DESCRIPTION
*	Returns a pointer to the Port object for this router.
*
* SYNOPSIS
*/
static inline osm_port_t*
osm_router_get_port_ptr(
        IN const osm_router_t* const p_rtr )
{
        return( p_rtr->p_port );
}
/*
* PARAMETERS
*	p_rtr
*		[in] Pointer to an osm_router_t object.
*
* RETURN VALUES
*	Returns a pointer to the Port object for this router.
*       
* NOTES
*
* SEE ALSO
*	Router object
*********/

/****f* OpenSM: Router/osm_router_get_node_ptr
* NAME
*	osm_router_get_node_ptr
*
* DESCRIPTION
*	Returns a pointer to the Node object for this router.
*
* SYNOPSIS
*/
static inline osm_node_t*
osm_router_get_node_ptr(
	IN const osm_router_t* const p_rtr )
{
	return( p_rtr->p_port->p_node );
}
/*
* PARAMETERS
*	p_rtr
*		[in] Pointer to an osm_router_t object.
*
* RETURN VALUES
*	Returns a pointer to the Node object for this router.
*	
* NOTES
*
* SEE ALSO
*	Router object
*********/

END_C_DECLS

#endif /* _OSM_ROUTER_H_ */

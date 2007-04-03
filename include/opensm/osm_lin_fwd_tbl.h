/*
 * Copyright (c) 2004-2006 Voltaire, Inc. All rights reserved.
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
 * 	Declaration of osm_lin_fwd_tbl_t.
 *	This object represents a linear forwarding table.
 *	This object is part of the OpenSM family of objects.
 *
 * Environment:
 * 	Linux User Mode
 *
 * $Revision: 1.4 $
 */

#ifndef _OSM_LIN_FWD_TBL_H_
#define _OSM_LIN_FWD_TBL_H_

#include <string.h>
#include <iba/ib_types.h>
#include <opensm/osm_base.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else /* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif /* __cplusplus */

BEGIN_C_DECLS

/****h* OpenSM/Linear Forwarding Table
* NAME
*	Linear Forwarding Table
*
* DESCRIPTION
*	The Linear Forwarding Table objects encapsulate the information
*	needed by the OpenSM to manage linear forwarding tables.  The OpenSM
*	allocates one Linear Forwarding Table object per switch in the
*	IBA subnet, if that switch uses a linear table.
*
*	The Linear Forwarding Table objects are not thread safe, thus
*	callers must provide serialization.
*
* AUTHOR
*	Steve King, Intel
*
*********/

/****s* OpenSM: Forwarding Table/osm_lin_fwd_tbl_t
* NAME
*	osm_lin_fwd_tbl_t
*
* DESCRIPTION
*	Linear Forwarding Table structure.
*
*	Callers may directly access this object.
*
* SYNOPSIS
*/
typedef struct _osm_lin_fwd_tbl
{
	uint16_t				size;
	uint8_t					port_tbl[1];

} osm_lin_fwd_tbl_t;
/*
* FIELDS
*	Size
*		Number of entries in the linear forwarding table.  This value
*		is taken from the SwitchInfo attribute.
*
*	port_tbl
*		The array that specifies the port number which routes the
*		corresponding LID.  Index is by LID.
*
* SEE ALSO
*	Forwarding Table object, Random Forwarding Table object.
*********/

/****f* OpenSM: Forwarding Table/osm_lin_tbl_new
* NAME
*	osm_lin_tbl_new
*
* DESCRIPTION
*	This function creates and initializes a Linear Forwarding Table object.
*
* SYNOPSIS
*/
osm_lin_fwd_tbl_t*
osm_lin_tbl_new(
	IN uint16_t const size );
/*
* PARAMETERS
*	size
*		[in] Number of entries in the Linear Forwarding Table.
*
* RETURN VALUE
*	On success, returns a pointer to a new Linear Forwarding Table object
*	of the specified size.
*	NULL otherwise.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: Forwarding Table/osm_lin_tbl_delete
* NAME
*	osm_lin_tbl_delete
*
* DESCRIPTION
*	This destroys and deallocates a Linear Forwarding Table object.
*
* SYNOPSIS
*/
void
osm_lin_tbl_delete(
	IN osm_lin_fwd_tbl_t** const pp_tbl );
/*
* PARAMETERS
*	pp_tbl
*		[in] Pointer a Pointer to the Linear Forwarding Table object.
*
* RETURN VALUE
*	On success, returns a pointer to a new Linear Forwarding Table object
*	of the specified size.
*	NULL otherwise.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: Forwarding Table/osm_lin_fwd_tbl_set
* NAME
*	osm_lin_fwd_tbl_set
*
* DESCRIPTION
*	Sets the port to route the specified LID.
*
* SYNOPSIS
*/
static inline void
osm_lin_fwd_tbl_set(
	IN osm_lin_fwd_tbl_t* const p_tbl,
	IN const uint16_t lid_ho,
	IN const uint8_t port )
{
	CL_ASSERT( lid_ho < p_tbl->size );
	if( lid_ho < p_tbl->size )
		p_tbl->port_tbl[lid_ho] = port;
}
/*
* PARAMETERS
*	p_tbl
*		[in] Pointer to the Linear Forwarding Table object.
*
*	lid_ho
*		[in] LID value (host order) for which to set the route.
*
*	port
*		[in] Port to route the specified LID value.
*
* RETURN VALUE
*	None.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: Forwarding Table/osm_lin_fwd_tbl_get
* NAME
*	osm_lin_fwd_tbl_get
*
* DESCRIPTION
*	Returns the port that routes the specified LID.
*
* SYNOPSIS
*/
static inline uint8_t
osm_lin_fwd_tbl_get(
	IN const osm_lin_fwd_tbl_t* const p_tbl,
	IN const uint16_t lid_ho )
{
	if( lid_ho < p_tbl->size )
		return( p_tbl->port_tbl[lid_ho] );
	else
		return( 0xFF );
}
/*
* PARAMETERS
*	p_tbl
*		[in] Pointer to the Linear Forwarding Table object.
*
*	lid_ho
*		[in] LID value (host order) for which to get the route.
*
* RETURN VALUE
*	Returns the port that routes the specified LID.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: Forwarding Table/osm_lin_fwd_tbl_get_size
* NAME
*	osm_lin_fwd_tbl_get_size
*
* DESCRIPTION
*	Returns the number of entries available in the forwarding table.
*
* SYNOPSIS
*/
static inline uint16_t
osm_lin_fwd_tbl_get_size(
	IN const osm_lin_fwd_tbl_t* const p_tbl )
{
	return( p_tbl->size );
}
/*
* PARAMETERS
*	p_tbl
*		[in] Pointer to the Forwarding Table object.
*
* RETURN VALUE
*	Returns the number of entries available in the forwarding table.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: Forwarding Table/osm_lin_fwd_tbl_get_lids_per_block
* NAME
*	osm_lin_fwd_tbl_get_lids_per_block
*
* DESCRIPTION
*	Returns the number of LIDs per LID block.
*
* SYNOPSIS
*/
static inline uint16_t
osm_lin_fwd_tbl_get_lids_per_block(
	IN const osm_lin_fwd_tbl_t* const p_tbl )
{
	UNUSED_PARAM( p_tbl );
	return( 64 );
}
/*
* PARAMETERS
*	p_tbl
*		[in] Pointer to the Forwarding Table object.
*
* RETURN VALUE
*	Returns the number of LIDs per LID block.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: Forwarding Table/osm_lin_fwd_tbl_get_max_block_id_in_use
* NAME
*	osm_lin_fwd_tbl_get_max_block_id_in_use
*
* DESCRIPTION
*	Returns the maximum block ID in actual use by the forwarding table.
*
* SYNOPSIS
*/
static inline uint16_t
osm_lin_fwd_tbl_get_max_block_id_in_use(
	IN const osm_lin_fwd_tbl_t* const p_tbl,
	IN const uint16_t lid_top_ho )
{
	return( (uint16_t)(lid_top_ho /
			osm_lin_fwd_tbl_get_lids_per_block( p_tbl ) ) );
}
/*
* PARAMETERS
*	p_tbl
*		[in] Pointer to the Forwarding Table object.
*
* RETURN VALUE
*	Returns the maximum block ID in actual use by the forwarding table.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: Forwarding Table/osm_lin_fwd_tbl_set_block
* NAME
*	osm_lin_fwd_tbl_set_block
*
* DESCRIPTION
*	Copies the specified block into the Linear Forwarding Table.
*
* SYNOPSIS
*/
static inline ib_api_status_t
osm_lin_fwd_tbl_set_block(
	IN osm_lin_fwd_tbl_t* const p_tbl,
	IN const uint8_t* const p_block,
	IN const uint32_t block_num )
{
	uint16_t lid_start;
	uint16_t num_lids;

	CL_ASSERT( p_tbl );
	CL_ASSERT( p_block );

	num_lids = osm_lin_fwd_tbl_get_lids_per_block( p_tbl );
	lid_start = (uint16_t)(block_num * num_lids);

	if( lid_start + num_lids > p_tbl->size )
		return( IB_INVALID_PARAMETER );

	memcpy( &p_tbl->port_tbl[lid_start], p_block, num_lids );
	return( IB_SUCCESS );
}
/*
* PARAMETERS
*	p_tbl
*		[in] Pointer to the Linear Forwarding Table object.
*
*	p_block
*		[in] Pointer to the Forwarding Table block.
*
*	block_num
*		[in] Block number of this block.
*
* RETURN VALUE
*	None.
*
* NOTES
*
* SEE ALSO
*********/

END_C_DECLS

#endif		/* _OSM_LIN_FWD_TBL_H_ */

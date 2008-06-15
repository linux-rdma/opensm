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
 * 	Declaration of osm_fwd_tbl_t.
 *	This object represents a unicast forwarding table.
 *	This object is part of the OpenSM family of objects.
 */

#ifndef _OSM_FWD_TBL_H_
#define _OSM_FWD_TBL_H_

#include <iba/ib_types.h>
#include <opensm/osm_base.h>
#include <opensm/osm_rand_fwd_tbl.h>
#include <opensm/osm_lin_fwd_tbl.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/****h* OpenSM/Forwarding Table
* NAME
*	Forwarding Table
*
* DESCRIPTION
*	The Forwarding Table objects encapsulate the information
*	needed by the OpenSM to manage forwarding tables.  The OpenSM
*	allocates one Forwarding Table object per switch in the
*	IBA subnet.
*
*	The Forwarding Table objects are not thread safe, thus
*	callers must provide serialization.
*
* AUTHOR
*	Steve King, Intel
*
*********/
/****s* OpenSM: Forwarding Table/osm_fwd_tbl_t
* NAME
*	osm_fwd_tbl_t
*
* DESCRIPTION
*	Forwarding Table structure.  This object hides the type
*	of fowarding table (linear or random) actually used by
*	the switch.
*
*	This object should be treated as opaque and should
*	be manipulated only through the provided functions.
*
* SYNOPSIS
*/
typedef struct _osm_fwd_tbl_t {
	osm_rand_fwd_tbl_t *p_rnd_tbl;
	osm_lin_fwd_tbl_t *p_lin_tbl;
} osm_fwd_tbl_t;
/*
* FIELDS
*	p_rnd_tbl
*		Pointer to the switch's Random Forwarding Table object.
*		If the switch does not use a Random Forwarding Table,
*		then this pointer is NULL.
*
*	p_lin_tbl
*		Pointer to the switch's Linear Forwarding Table object.
*		If the switch does not use a Linear Forwarding Table,
*		then this pointer is NULL.
*
* SEE ALSO
*	Forwarding Table object, Random Forwarding Table object.
*********/

/****f* OpenSM: Forwarding Table/osm_fwd_tbl_init
* NAME
*	osm_fwd_tbl_init
*
* DESCRIPTION
*	Initializes a Forwarding Table object.
*
* SYNOPSIS
*/
ib_api_status_t
osm_fwd_tbl_init(IN osm_fwd_tbl_t * const p_tbl,
		 IN const ib_switch_info_t * const p_si);
/*
* PARAMETERS
*	p_tbl
*		[in] Pointer to the Forwarding Table object.
*
*	p_si
*		[in] Pointer to the SwitchInfo attribute of the associated
*		switch.
*
* RETURN VALUE
*	IB_SUCCESS if the operation is successful.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: Forwarding Table/osm_fwd_tbl_destroy
* NAME
*	osm_fwd_tbl_destroy
*
* DESCRIPTION
*	Destroys a Forwarding Table object.
*
* SYNOPSIS
*/
void osm_fwd_tbl_destroy(IN osm_fwd_tbl_t * const p_tbl);
/*
* PARAMETERS
*	p_tbl
*		[in] Pointer to the Forwarding Table object.
*
* RETURN VALUE
*	None.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: Forwarding Table/osm_fwd_tbl_get
* NAME
*	osm_fwd_tbl_get
*
* DESCRIPTION
*	Returns the port that routes the specified LID.
*
* SYNOPSIS
*/
static inline uint8_t
osm_fwd_tbl_get(IN const osm_fwd_tbl_t * const p_tbl, IN uint16_t const lid_ho)
{
	if (p_tbl->p_lin_tbl)
		return (osm_lin_fwd_tbl_get(p_tbl->p_lin_tbl, lid_ho));
	else
		return (osm_rand_fwd_tbl_get(p_tbl->p_rnd_tbl, lid_ho));
}

/*
* PARAMETERS
*	p_tbl
*		[in] Pointer to the Forwarding Table object.
*
*	lid_ho
*		[in] LID (host order) for which to find the route.
*
* RETURN VALUE
*	Returns the port that routes the specified LID.
*	IB_INVALID_PORT_NUM if the table does not have a route for this LID.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: Forwarding Table/osm_fwd_tbl_set
* NAME
*	osm_fwd_tbl_set
*
* DESCRIPTION
*	Sets the port to route the specified LID.
*
* SYNOPSIS
*/
static inline void
osm_fwd_tbl_set(IN osm_fwd_tbl_t * const p_tbl,
		IN const uint16_t lid_ho, IN const uint8_t port)
{
	CL_ASSERT(p_tbl);
	if (p_tbl->p_lin_tbl)
		osm_lin_fwd_tbl_set(p_tbl->p_lin_tbl, lid_ho, port);
	else
		osm_rand_fwd_tbl_set(p_tbl->p_rnd_tbl, lid_ho, port);
}

/*
* PARAMETERS
*	p_tbl
*		[in] Pointer to the Forwarding Table object.
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

/****f* OpenSM: Forwarding Table/osm_fwd_tbl_set_block
* NAME
*	osm_fwd_tbl_set_block
*
* DESCRIPTION
*	Copies the specified block into the Forwarding Table.
*
* SYNOPSIS
*/
static inline ib_api_status_t
osm_fwd_tbl_set_block(IN osm_fwd_tbl_t * const p_tbl,
		      IN const uint8_t * const p_block,
		      IN const uint32_t block_num)
{
	CL_ASSERT(p_tbl);
	if (p_tbl->p_lin_tbl)
		return (osm_lin_fwd_tbl_set_block(p_tbl->p_lin_tbl,
						  p_block, block_num));
	else
		return (osm_rand_fwd_tbl_set_block(p_tbl->p_rnd_tbl,
						   p_block, block_num));
}

/*
* PARAMETERS
*	p_tbl
*		[in] Pointer to the Forwarding Table object.
*
* RETURN VALUE
*	None.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: Forwarding Table/osm_fwd_tbl_get_size
* NAME
*	osm_fwd_tbl_get_size
*
* DESCRIPTION
*	Returns the number of entries available in the forwarding table.
*
* SYNOPSIS
*/
static inline uint16_t
osm_fwd_tbl_get_size(IN const osm_fwd_tbl_t * const p_tbl)
{
	CL_ASSERT(p_tbl);
	if (p_tbl->p_lin_tbl)
		return (osm_lin_fwd_tbl_get_size(p_tbl->p_lin_tbl));
	else
		return (osm_rand_fwd_tbl_get_size(p_tbl->p_rnd_tbl));
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

/****f* OpenSM: Forwarding Table/osm_fwd_tbl_get_lids_per_block
* NAME
*	osm_fwd_tbl_get_lids_per_block
*
* DESCRIPTION
*	Returns the number of LIDs per LID block.
*
* SYNOPSIS
*/
static inline uint16_t
osm_fwd_tbl_get_lids_per_block(IN const osm_fwd_tbl_t * const p_tbl)
{
	CL_ASSERT(p_tbl);
	if (p_tbl->p_lin_tbl)
		return (osm_lin_fwd_tbl_get_lids_per_block(p_tbl->p_lin_tbl));
	else
		return (osm_rand_fwd_tbl_get_lids_per_block(p_tbl->p_rnd_tbl));
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

/****f* OpenSM: Forwarding Table/osm_fwd_tbl_get_max_block_id_in_use
* NAME
*	osm_fwd_tbl_get_max_block_id_in_use
*
* DESCRIPTION
*	Returns the number of LIDs per LID block.
*
* SYNOPSIS
*/
static inline uint16_t
osm_fwd_tbl_get_max_block_id_in_use(IN const osm_fwd_tbl_t * const p_tbl,
				    IN const uint16_t lid_top_ho)
{
	CL_ASSERT(p_tbl);
	if (p_tbl->p_lin_tbl)
		return (osm_lin_fwd_tbl_get_max_block_id_in_use
			(p_tbl->p_lin_tbl, lid_top_ho));
	else
		return (osm_rand_fwd_tbl_get_max_block_id_in_use
			(p_tbl->p_rnd_tbl, lid_top_ho));
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

END_C_DECLS
#endif				/* _OSM_FWD_TBL_H_ */

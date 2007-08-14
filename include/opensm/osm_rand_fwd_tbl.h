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
 * 	Declaration of osm_switch_t.
 *	This object represents an IBA switch.
 *	This object is part of the OpenSM family of objects.
 *
 * Environment:
 * 	Linux User Mode
 *
 * $Revision: 1.4 $
 */

#ifndef _OSM_RAND_FWD_TBL_H_
#define _OSM_RAND_FWD_TBL_H_

#include <stdlib.h>
#include <iba/ib_types.h>
#include <opensm/osm_base.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/****h* OpenSM/Random Forwarding Table
* NAME
*	Random Forwarding Table
*
* DESCRIPTION
*	The Random Forwarding Table objects encapsulate the information
*	needed by the OpenSM to manage random forwarding tables.  The OpenSM
*	allocates one Random Forwarding Table object per switch in the
*	IBA subnet, if that switch uses a random forwarding table.
*
*	The Random Forwarding Table objects are not thread safe, thus
*	callers must provide serialization.
*
*   ** RANDOM FORWARDING TABLES ARE NOT SUPPORTED IN THE CURRENT VERSION **
*
* AUTHOR
*	Steve King, Intel
*
*********/
/****s* OpenSM: Forwarding Table/osm_rand_fwd_tbl_t
* NAME
*	osm_rand_fwd_tbl_t
*
* DESCRIPTION
*	Random Forwarding Table structure.
*
*	THIS OBJECT IS PLACE HOLDER.  SUPPORT FOR SWITCHES WITH
*	RANDOM FORWARDING TABLES HAS NOT BEEN IMPLEMENTED YET.
*
* SYNOPSIS
*/
typedef struct _osm_rand_fwd_tbl {
	/* PLACE HOLDER STRUCTURE ONLY!! */
	uint32_t size;
} osm_rand_fwd_tbl_t;
/*
* FIELDS
*	RANDOM FORWARDING TABLES ARE NOT SUPPORTED YET!!
*
* SEE ALSO
*	Forwarding Table object, Random Forwarding Table object.
*********/

/****f* OpenSM: Forwarding Table/osm_rand_tbl_delete
* NAME
*	osm_rand_tbl_delete
*
* DESCRIPTION
*	This destroys and deallocates a Random Forwarding Table object.
*
* SYNOPSIS
*/
static inline void osm_rand_tbl_delete(IN osm_rand_fwd_tbl_t ** const pp_tbl)
{
	/*
	   TO DO - This is a place holder function only!
	 */
	free(*pp_tbl);
	*pp_tbl = NULL;
}

/*
* PARAMETERS
*	pp_tbl
*		[in] Pointer a Pointer to the Random Forwarding Table object.
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

/****f* OpenSM: Forwarding Table/osm_rand_fwd_tbl_set
* NAME
*	osm_rand_fwd_tbl_set
*
* DESCRIPTION
*	Sets the port to route the specified LID.
*
* SYNOPSIS
*/
static inline void
osm_rand_fwd_tbl_set(IN osm_rand_fwd_tbl_t * const p_tbl,
		     IN const uint16_t lid_ho, IN const uint8_t port)
{
	/* Random forwarding tables not supported yet. */
	UNUSED_PARAM(p_tbl);
	UNUSED_PARAM(lid_ho);
	UNUSED_PARAM(port);
	CL_ASSERT(FALSE);
}

/*
* PARAMETERS
*	p_tbl
*		[in] Pointer to the Random Forwarding Table object.
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

/****f* OpenSM: Forwarding Table/osm_rand_fwd_tbl_set_block
* NAME
*	osm_rand_fwd_tbl_set_block
*
* DESCRIPTION
*	Copies the specified block into the Random Forwarding Table.
*
* SYNOPSIS
*/
static inline ib_api_status_t
osm_rand_fwd_tbl_set_block(IN osm_rand_fwd_tbl_t * const p_tbl,
			   IN const uint8_t * const p_block,
			   IN const uint32_t block_num)
{
	/* Random forwarding tables not supported yet. */
	UNUSED_PARAM(p_tbl);
	UNUSED_PARAM(p_block);
	UNUSED_PARAM(block_num);
	CL_ASSERT(FALSE);
	return (IB_ERROR);
}

/*
* PARAMETERS
*	p_tbl
*		[in] Pointer to the Random Forwarding Table object.
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

/****f* OpenSM: Forwarding Table/osm_rand_fwd_tbl_get
* NAME
*	osm_rand_fwd_tbl_get
*
* DESCRIPTION
*	Returns the port that routes the specified LID.
*
* SYNOPSIS
*/
static inline uint8_t
osm_rand_fwd_tbl_get(IN const osm_rand_fwd_tbl_t * const p_tbl,
		     IN const uint16_t lid_ho)
{
	CL_ASSERT(FALSE);
	UNUSED_PARAM(p_tbl);
	UNUSED_PARAM(lid_ho);

	return (OSM_NO_PATH);
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

/****f* OpenSM: Forwarding Table/osm_rand_fwd_tbl_get_lids_per_block
* NAME
*	osm_rand_fwd_tbl_get_lids_per_block
*
* DESCRIPTION
*	Returns the number of LIDs per LID block.
*
* SYNOPSIS
*/
static inline uint16_t
osm_rand_fwd_tbl_get_lids_per_block(IN const osm_rand_fwd_tbl_t * const p_tbl)
{
	UNUSED_PARAM(p_tbl);
	return (16);
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

/****f* OpenSM: Forwarding Table/osm_rand_fwd_tbl_get_max_block_id_in_use
* NAME
*	osm_rand_fwd_tbl_get_max_block_id_in_use
*
* DESCRIPTION
*	Returns the maximum block ID in actual use by the forwarding table.
*
* SYNOPSIS
*/
static inline uint16_t
osm_rand_fwd_tbl_get_max_block_id_in_use(IN const osm_rand_fwd_tbl_t *
					 const p_tbl,
					 IN const uint16_t lid_top_ho)
{
	UNUSED_PARAM(p_tbl);
	UNUSED_PARAM(lid_top_ho);
	CL_ASSERT(FALSE);
	return (0);
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

/****f* OpenSM: Forwarding Table/osm_rand_fwd_tbl_get_size
* NAME
*	osm_rand_fwd_tbl_get_size
*
* DESCRIPTION
*	Returns the number of entries available in the forwarding table.
*
* SYNOPSIS
*/
static inline uint16_t
osm_rand_fwd_tbl_get_size(IN const osm_rand_fwd_tbl_t * const p_tbl)
{
	UNUSED_PARAM(p_tbl);
	CL_ASSERT(FALSE);
	return (0);
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

END_C_DECLS
#endif				/* _OSM_RAND_FWD_TBL_H_ */

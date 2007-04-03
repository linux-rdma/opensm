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
 * 	Declaration of Switch/osm_port_profile_t.
 *	This object represents a port profile for an IBA switch.
 *	This object is part of the OpenSM family of objects.
 *
 * Environment:
 * 	Linux User Mode
 *
 * $Revision: 1.7 $
 */

#ifndef _OSM_PORT_PROFILE_H_
#define _OSM_PORT_PROFILE_H_

#include <string.h>
#include <iba/ib_types.h>
#include <opensm/osm_base.h>
#include <opensm/osm_madw.h>
#include <opensm/osm_subnet.h>
#include <opensm/osm_node.h>
#include <opensm/osm_port.h>
#include <opensm/osm_fwd_tbl.h>
#include <opensm/osm_mcast_tbl.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else /* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif /* __cplusplus */

BEGIN_C_DECLS

/****h* OpenSM/Port Profile
* NAME
*	Port Profile
*
* DESCRIPTION
*	The Port Profile object contains profiling information for
*	each Physical Port on a switch.  The profile information
*	may be used to optimize path selection.
*
* AUTHOR
*	Steve King, Intel
*
*********/

/****s* OpenSM: Switch/osm_port_profile_t
* NAME
*	osm_port_profile_t
*
* DESCRIPTION
*	The Port Profile object contains profiling information for
*	each Physical Port on the switch.  The profile information
*	may be used to optimize path selection.
*
*	This object should be treated as opaque and should be
*	be manipulated only through the provided functions.
*
* SYNOPSIS
*/
typedef struct _osm_port_profile
{
	uint32_t		num_paths;
} osm_port_profile_t;
/*
* FIELDS
*	num_paths
*		The number of paths using this port.
*
* SEE ALSO
*********/

/****f* OpenSM: Port Profile/osm_port_prof_construct
* NAME
*	osm_port_prof_construct
*
* DESCRIPTION
*	
*
* SYNOPSIS
*/
static inline void
osm_port_prof_construct(
	IN osm_port_profile_t* const p_prof )
{
	CL_ASSERT( p_prof );
	memset( p_prof, 0, sizeof(*p_prof) );
}
/*
* PARAMETERS
*	p_prof
*		[in] Pointer to the Port Profile object to construct.
*
* RETURN VALUE
*	None.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: Port Profile/osm_port_prof_path_count_inc
* NAME
*	osm_port_prof_path_count_inc
*
* DESCRIPTION
*	Increments the count of the number of paths going through this port.
*	
*
* SYNOPSIS
*/
static inline void
osm_port_prof_path_count_inc(
	IN osm_port_profile_t* const p_prof )
{
	CL_ASSERT( p_prof );
	p_prof->num_paths++;
}
/*
* PARAMETERS
*	p_pro
*		[in] Pointer to the Port Profile object.
*
* RETURN VALUE
*	None.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: Port Profile/osm_port_prof_path_count_get
* NAME
*	osm_port_prof_path_count_get
*
* DESCRIPTION
*	Returns the count of the number of paths going through this port.
*
* SYNOPSIS
*/
static inline uint32_t
osm_port_prof_path_count_get(
	IN const osm_port_profile_t* const p_prof )
{
	return( p_prof->num_paths );
}
/*
* PARAMETERS
*	p_pro
*		[in] Pointer to the Port Profile object.
*
* RETURN VALUE
*	None.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: Port Profile Opt/osm_port_prof_is_ignored_port
* NAME
*	osm_port_prof_is_ignored_port
*
* DESCRIPTION
*	Check to see if this port is to be ignored in path counting.
*  This is done by examining the optional list of port_prof_ignore_guids.
*
* SYNOPSIS
*/
static inline boolean_t
osm_port_prof_is_ignored_port(
	IN const osm_subn_t *p_subn, 
	IN ib_net64_t port_guid,
	IN uint8_t port_num )
{
  const cl_map_t *p_map = &(p_subn->opt.port_prof_ignore_guids);
  const void *p_obj = cl_map_get(p_map, port_guid);
  size_t res;

  // HACK: we currently support ignoring ports 0 - 31
  if (p_obj != NULL) {
	 res = (size_t)p_obj & (size_t)(1 << port_num);
	 return (res != 0);
  }
  return FALSE;
}
/*
* PARAMETERS
*	p_subn
*		[in] Pointer to the OSM Subnet object.
*
*  port_guid 
*     [in] The port guid 
*
* RETURN VALUE
*	None.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: Port Profile Opt/osm_port_prof_set_ignored_port
* NAME
*	osm_port_prof_set_ignored_port
*
* DESCRIPTION
*	Set the ignored property of the port.
*
* SYNOPSIS
*/
static inline void
osm_port_prof_set_ignored_port(
	IN osm_subn_t *p_subn, 
	IN ib_net64_t port_guid,
	IN uint8_t port_num )
{
  cl_map_t *p_map = &(p_subn->opt.port_prof_ignore_guids);
  const void *p_obj = cl_map_get(p_map, port_guid);
  size_t value = 0;

  // HACK: we currently support ignoring ports 0 - 31
  CL_ASSERT(port_num < 32);
  
  if (p_obj != NULL) {
	 value = (size_t)p_obj;
	 cl_map_remove(p_map, port_guid);
  } 
  
  value = value | (1 << port_num);
  cl_map_insert(&(p_subn->opt.port_prof_ignore_guids),
					 port_guid,
					 (void *)value);
}
/*
* PARAMETERS
*	p_subn
*		[in] Pointer to the OSM Subnet object.
*
*  port_guid 
*     [in] The port guid 
*
* RETURN VALUE
*	None.
*
* NOTES
*
* SEE ALSO
*********/

END_C_DECLS

#endif		/* _OSM_PORT_PROFILE_H_ */

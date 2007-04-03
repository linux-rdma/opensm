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
 * 	Declaration of osm_stats_t.
 *	This object represents the OpenSM statistics object.
 *	This object is part of the OpenSM family of objects.
 *
 * Environment:
 * 	Linux User Mode
 *
 * $Revision: 1.4 $
 */

#ifndef _OSM_STATS_H_
#define _OSM_STATS_H_

#include <opensm/osm_base.h>
#include <complib/cl_atomic.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else /* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif /* __cplusplus */

BEGIN_C_DECLS

/****h* OpenSM/Statistics
* NAME
*	OpenSM
*
* DESCRIPTION
*	The OpenSM object encapsulates the information needed by the
*	OpenSM to track interesting traffic and internal statistics.
*
* AUTHOR
*	Steve King, Intel
*
*********/

/****s* OpenSM: Statistics/osm_stats_t
* NAME
*	osm_stats_t
*
* DESCRIPTION
*	OpenSM statistics block.
*
* SYNOPSIS
*/
typedef struct _osm_stats
{
	atomic32_t					qp0_mads_outstanding;
	atomic32_t					qp0_mads_outstanding_on_wire;
	atomic32_t					qp0_mads_rcvd;
	atomic32_t					qp0_mads_sent;
	atomic32_t					qp0_unicasts_sent;
	atomic32_t					qp1_mads_outstanding;
	atomic32_t					qp1_mads_rcvd;
	atomic32_t					qp1_mads_sent;

} osm_stats_t;
/*
* FIELDS
*	qp0_mads_outstanding
*		Contains the number of MADs outstanding on QP0.
*		When this value reaches zero, OpenSM has discovered all
*		nodes on the subnet, and finished retrieving attributes.
*		At that time, subnet configuration may begin.
*		This variable must be manipulated using atomic instructions.
*
*	qp0_mads_outstanding_on_wire
*		The number of MADs outstanding on the wire at any moment.
*
*	qp0_mads_rcvd
*		Total number of QP0 MADs received.
*
*	qp0_mads_sent
*		Total number of QP0 MADs sent.
*
*	qp0_unicasts_sent
*		Total number of response-less MADs sent on the wire.  This count
*		includes getresp(), send() and trap() methods.
*
* SEE ALSO
***************/

END_C_DECLS

#endif	/* _OSM_STATS_H_ */

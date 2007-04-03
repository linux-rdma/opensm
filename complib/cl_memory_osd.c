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
 *	Implementation of memory manipulation functions for Linux user mode.
 *
 * Environment:
 *	Linux User Mode
 *
 * $Revision: 1.3 $
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <complib/cl_memory.h>
#include <stdlib.h>

void*
__cl_malloc_priv(
	IN	const size_t	size )
{
	return malloc( size );
}

void
__cl_free_priv(
	IN	void* const	p_memory )
{
	free( p_memory );
}

void
cl_memset(
	IN	void* const		p_memory,
	IN	const uint8_t	fill,
	IN	const size_t	count )
{
	memset( p_memory, fill, count );
}

void*
cl_memcpy(
	IN	void* const			p_dest,
	IN	const void* const	p_src,
	IN	const size_t		count )
{
	return( memcpy( p_dest, p_src, count ) );
}

int32_t
cl_memcmp(
	IN	const void* const	p_mem,
	IN	const void* const	p_ref,
	IN	const size_t		count )
{
	return( memcmp( p_mem, p_ref, count ) );
}


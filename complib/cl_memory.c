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
 *	Implementation of memory allocation tracking functions.
 *
 * Environment:
 *	All
 *
 * $Revision: 1.4 $
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <string.h>
#include <complib/cl_memtrack.h>
#define  _MEM_DEBUG_MODE_ 0
#ifdef _MEM_DEBUG_MODE_
/* 
   In the mem debug mode we will be wrapping up the allocated buffer
   with magic constants and the required size and then check during free.
   
   The memory layout will be:
   |<magic start>|<req size>|<buffer ....>|<magic end>|
   
*/

#define _MEM_DEBUG_MAGIC_SIZE_  4
#define _MEM_DEBUG_EXTRA_SIZE_  sizeof(size) + 8
static uint8_t _MEM_DEBUG_MAGIC_START_[4] = {0x12, 0x34, 0x56, 0x78, };
static uint8_t _MEM_DEBUG_MAGIC_END_[4] =   {0x87, 0x65, 0x43, 0x21, };
#endif

cl_mem_tracker_t		*gp_mem_tracker = NULL;

/*
 * Allocates memory.
 */
void*
__cl_malloc_priv(
	IN	const size_t	size );

/*
 * Deallocates memory.
 */
void
__cl_free_priv(
	IN	void* const	p_memory );

/*
 * Allocate and initialize the memory tracker object.
 */
static inline void
__cl_mem_track_start( void )
{
	cl_status_t			status;

	if( gp_mem_tracker )
		return;

	/* Allocate the memory tracker object. */
	gp_mem_tracker = (cl_mem_tracker_t*)
		__cl_malloc_priv( sizeof(cl_mem_tracker_t) );

	if( !gp_mem_tracker )
		return;

	/* Initialize the free list. */
	cl_qlist_init( &gp_mem_tracker->free_hdr_list );
	/* Initialize the allocation list. */
	cl_qlist_init( &gp_mem_tracker->alloc_list );

	/* Initialize the spin lock to protect list operations. */
	status = cl_spinlock_init( &gp_mem_tracker->lock );
	if( status != CL_SUCCESS )
	{
		__cl_free_priv( gp_mem_tracker );
		return;
	}

	cl_msg_out( "\n\n\n*** Memory tracker object address = %p ***\n\n\n",
		gp_mem_tracker );
}

/*
 * Clean up memory tracking.
 */
static inline void
__cl_mem_track_stop( void )
{
	cl_list_item_t	*p_list_item;

	if( !gp_mem_tracker )
		return;

	if( !cl_is_qlist_empty( &gp_mem_tracker->alloc_list ) )
	{
		/* There are still items in the list.  Print them out. */
		cl_mem_display();
	}

	/* Free all allocated headers. */
	cl_spinlock_acquire( &gp_mem_tracker->lock );
	while( !cl_is_qlist_empty( &gp_mem_tracker->alloc_list ) )
	{
		p_list_item = cl_qlist_remove_head( &gp_mem_tracker->alloc_list );
		__cl_free_priv(
			PARENT_STRUCT( p_list_item, cl_malloc_hdr_t, list_item ) );
	}

	while( !cl_is_qlist_empty( &gp_mem_tracker->free_hdr_list ) )
	{
		p_list_item = cl_qlist_remove_head( &gp_mem_tracker->free_hdr_list );
		__cl_free_priv(
			PARENT_STRUCT( p_list_item, cl_malloc_hdr_t, list_item ) );
	}
	cl_spinlock_release( &gp_mem_tracker->lock );

	/* Destory all objects in the memory tracker object. */
	cl_spinlock_destroy( &gp_mem_tracker->lock );

	/* Free the memory allocated for the memory tracker object. */
	__cl_free_priv( gp_mem_tracker );
}

/*
 * Enables memory allocation tracking.
 */
void
__cl_mem_track(
	IN	const boolean_t	start )
{
	if( start )
		__cl_mem_track_start();
	else
		__cl_mem_track_stop();
}

/*
 * Display memory usage.
 */
void
cl_mem_display( void )
{
	cl_list_item_t		*p_list_item;
	cl_malloc_hdr_t		*p_hdr;

	if( !gp_mem_tracker )
		return;

	cl_spinlock_acquire( &gp_mem_tracker->lock );
	cl_msg_out( "\n\n\n*** Memory Usage ***\n" );
	p_list_item = cl_qlist_head( &gp_mem_tracker->alloc_list );
	while( p_list_item != cl_qlist_end( &gp_mem_tracker->alloc_list ) )
	{
		/*
		 * Get the pointer to the header.  Note that the object member of the
		 * list item will be used to store the pointer to the user's memory.
		 */
		p_hdr = PARENT_STRUCT( p_list_item, cl_malloc_hdr_t, list_item );

		cl_msg_out( "\tMemory block at %p allocated in file %s line %d\n",
			p_hdr->p_mem, p_hdr->file_name, p_hdr->line_num );

		p_list_item = cl_qlist_next( p_list_item );
	}
	cl_msg_out( "*** End of Memory Usage ***\n\n" );
	cl_spinlock_release( &gp_mem_tracker->lock );
}

/*
 * Check the memory using the magic bits to see if anything corrupted
 * our memory.
 */
boolean_t
cl_mem_check( void )
{
   boolean_t res = TRUE;

#ifdef _MEM_DEBUG_MODE_
   {
 	cl_list_item_t		*p_list_item;
	cl_malloc_hdr_t		*p_hdr;
   size_t size;
   void *p_mem;

	if( !gp_mem_tracker )
		return res;

	cl_spinlock_acquire( &gp_mem_tracker->lock );
   /*	cl_msg_out( "\n\n\n*** Memory Checker ***\n" ); */
	p_list_item = cl_qlist_head( &gp_mem_tracker->alloc_list );
	while( p_list_item != cl_qlist_end( &gp_mem_tracker->alloc_list ) )
	{
     /*
      * Get the pointer to the header.  Note that the object member of the
      * list item will be used to store the pointer to the user's memory.
      */
     p_hdr = PARENT_STRUCT( p_list_item, cl_malloc_hdr_t, list_item );
     
     /*     cl_msg_out( "\tMemory block at %p allocated in file %s line %d\n",
            p_hdr->p_mem, p_hdr->file_name, p_hdr->line_num ); */
     
     /* calc the start */
     p_mem = (char*)p_hdr->p_mem - sizeof(size) - _MEM_DEBUG_MAGIC_SIZE_;
     /* check the header magic: */
     if (memcmp(p_mem, &_MEM_DEBUG_MAGIC_START_, _MEM_DEBUG_MAGIC_SIZE_))
     {
       cl_msg_out("\n *** cl_mem_check ERROR: BAD Magic Start in free of memory:%p file:%s line:%d\n", 
                  p_hdr->p_mem , p_hdr->file_name, p_hdr->line_num
                  );
       res = FALSE;
     }
     else 
     {
       /* obtain the size from the header */
       memcpy(&size, (char*)p_mem + _MEM_DEBUG_MAGIC_SIZE_, sizeof(size));
       
       if (memcmp((char*)p_mem + sizeof(size) + _MEM_DEBUG_MAGIC_SIZE_ + size, 
                  &_MEM_DEBUG_MAGIC_END_, _MEM_DEBUG_MAGIC_SIZE_))
       {
         cl_msg_out("\n *** cl_mem_check ERROR: BAD Magic End in free of memory:%p file:%s line:%d\n", 
                    p_hdr->p_mem , p_hdr->file_name, p_hdr->line_num
                    );
         res = FALSE;
       }
     }

     p_list_item = cl_qlist_next( p_list_item );
	}
   /*	cl_msg_out( "*** End of Memory Checker ***\n\n" ); */
	cl_spinlock_release( &gp_mem_tracker->lock );
   }
#endif
   return res;
}

/*
 * Allocates memory and stores information about the allocation in a list.
 * The contents of the list can be printed out by calling the function
 * "MemoryReportUsage".  Memory allocation will succeed even if the list
 * cannot be created.
 */
void*
__cl_malloc_trk(
	IN	const char* const	p_file_name,
	IN	const int32_t		line_num,
	IN	const size_t		size )
{
	cl_malloc_hdr_t	*p_hdr;
	cl_list_item_t	*p_list_item;
	void			*p_mem;
	char			temp_buf[FILE_NAME_LENGTH];
	int32_t			temp_line;

#ifdef _MEM_DEBUG_MODE_
      /* If we are running in MEM_DEBUG_MODE then 
         the cl_mem_check will be called on every run */
      if (cl_mem_check() == FALSE) 
      {
        cl_msg_out( "*** MEMORY ERROR !!! ***\n" );
        CL_ASSERT(0);
      }
#endif

	/*
	 * Allocate the memory first, so that we give the user's allocation
	 * priority over the the header allocation.
	 */
#ifndef _MEM_DEBUG_MODE_
   p_mem = __cl_malloc_priv( size );
	if( !p_mem )
		return( NULL );
#else
   p_mem = __cl_malloc_priv( size + sizeof(size) + 32 );
	if( !p_mem )
		return( NULL );
   /* now poisen */
   memset(p_mem, 0xA5, size + _MEM_DEBUG_EXTRA_SIZE_);
   /* special layout */
   memcpy(p_mem, &_MEM_DEBUG_MAGIC_START_, _MEM_DEBUG_MAGIC_SIZE_);
   memcpy((char*)p_mem + _MEM_DEBUG_MAGIC_SIZE_, &size, sizeof(size));
   memcpy((char*)p_mem + sizeof(size) + size + _MEM_DEBUG_MAGIC_SIZE_,
          &_MEM_DEBUG_MAGIC_END_, _MEM_DEBUG_MAGIC_SIZE_);
   p_mem = (char*)p_mem +  _MEM_DEBUG_MAGIC_SIZE_ + sizeof(size);
#endif

	if( !gp_mem_tracker )
		return( p_mem );

	/*
	 * Make copies of the file name and line number in case those
	 * parameters are in paged pool.
	 */
	temp_line = line_num;
	strncpy( temp_buf, p_file_name, FILE_NAME_LENGTH );
	/* Make sure the string is null terminated. */
	temp_buf[FILE_NAME_LENGTH - 1] = '\0';

	cl_spinlock_acquire( &gp_mem_tracker->lock );

	/* Get a header from the free header list. */
	p_list_item = cl_qlist_remove_head( &gp_mem_tracker->free_hdr_list );
	if( p_list_item != cl_qlist_end( &gp_mem_tracker->free_hdr_list ) )
	{
		/* Set the header pointer to the header retrieved from the list. */
		p_hdr = PARENT_STRUCT( p_list_item, cl_malloc_hdr_t, list_item );
	}
	else
	{
		/* We failed to get a free header.  Allocate one. */
		p_hdr = __cl_malloc_priv( sizeof(cl_malloc_hdr_t) );
		if( !p_hdr )
		{
			/* We failed to allocate the header.  Return the user's memory. */
			cl_spinlock_release( &gp_mem_tracker->lock );
			return( p_mem );
		}
	}
	memcpy( p_hdr->file_name, temp_buf, FILE_NAME_LENGTH );
	p_hdr->line_num = temp_line;
	/*
	 * We store the pointer to the memory returned to the user.  This allows
	 * searching the list of allocated memory even if the buffer allocated is
	 * not in the list without dereferencing memory we do not own.
	 */
	p_hdr->p_mem = p_mem;

	/* Insert the header structure into our allocation list. */
	cl_qlist_insert_tail( &gp_mem_tracker->alloc_list, &p_hdr->list_item );
	cl_spinlock_release( &gp_mem_tracker->lock );

	return( p_mem );
}

/*
 * Allocate non-tracked memory.
 */
void*
__cl_malloc_ntrk(
	IN	const size_t	size )
{
	return( __cl_malloc_priv( size ) );
}

void*
__cl_zalloc_trk(
	IN	const char* const	p_file_name,
	IN	const int32_t		line_num,
	IN	const size_t		size )
{
	void	*p_buffer;

	p_buffer = __cl_malloc_trk( p_file_name, line_num, size );
	if( p_buffer )
		memset( p_buffer, 0, size );

	return( p_buffer );
}

void*
__cl_zalloc_ntrk(
	IN	const size_t	size )
{
	void	*p_buffer;

	p_buffer = __cl_malloc_priv( size );
	if( p_buffer )
		memset( p_buffer, 0, size );

	return( p_buffer );
}

static cl_status_t
__cl_find_mem(
	IN	const cl_list_item_t* const p_list_item,
	IN	void* const					p_memory )
{
	cl_malloc_hdr_t		*p_hdr;

	/* Get the pointer to the header. */
	p_hdr = PARENT_STRUCT( p_list_item, cl_malloc_hdr_t, list_item );

	if( p_memory == p_hdr->p_mem )
		return( CL_SUCCESS );

	return( CL_NOT_FOUND );
}

void
__cl_free_trk(
  IN	const char* const	p_file_name,
  IN	const int32_t		line_num,  
  IN	void* const	      p_memory )
{
	cl_malloc_hdr_t		*p_hdr;
	cl_list_item_t		*p_list_item;

#ifdef _MEM_DEBUG_MODE_
      /* If we are running in MEM_DEBUG_MODE then 
         the cl_mem_check will be called on every run */
      if (cl_mem_check() == FALSE) 
      {
        cl_msg_out( "*** MEMORY ERROR !!! ***\n" );
        CL_ASSERT(0);
      }
#endif

	if( gp_mem_tracker )
	{
		cl_spinlock_acquire( &gp_mem_tracker->lock );

		/*
		 * Removes an item from the allocation tracking list given a pointer
		 * To the user's data and returns the pointer to header referencing the
		 * allocated memory block.
		 */
		p_list_item = cl_qlist_find_from_tail( &gp_mem_tracker->alloc_list,
			__cl_find_mem, p_memory );

		if( p_list_item != cl_qlist_end(&gp_mem_tracker->alloc_list) )
		{
			/* Get the pointer to the header. */
			p_hdr = PARENT_STRUCT( p_list_item, cl_malloc_hdr_t, list_item );
			/* Remove the item from the list. */
			cl_qlist_remove_item( &gp_mem_tracker->alloc_list, p_list_item );

			/* Return the header to the free header list. */
			cl_qlist_insert_head( &gp_mem_tracker->free_hdr_list,
				&p_hdr->list_item );
		} else {
        	cl_msg_out("\n *** cl_free ERROR: free of non tracked memory:%p file:%s line:%d\n", 
                    p_memory , p_file_name, line_num
                    );
      }
		cl_spinlock_release( &gp_mem_tracker->lock );
	}

#ifdef _MEM_DEBUG_MODE_
   {
     size_t size;
     void *p_mem;

     /* calc the start */
     p_mem = (char*)p_memory - sizeof(size) - _MEM_DEBUG_MAGIC_SIZE_;
     /* check the header magic: */
     if (memcmp(p_mem, &_MEM_DEBUG_MAGIC_START_, _MEM_DEBUG_MAGIC_SIZE_))
     {
       cl_msg_out("\n *** cl_free ERROR: BAD Magic Start in free of memory:%p file:%s line:%d\n", 
                  p_memory , p_file_name, line_num
                  );
     } 
     else 
     {
       /* obtain the size from the header */
       memcpy(&size, (char*)p_mem + _MEM_DEBUG_MAGIC_SIZE_, sizeof(size));
       
       if (memcmp((char*)p_mem + sizeof(size) + _MEM_DEBUG_MAGIC_SIZE_ + size, 
                  &_MEM_DEBUG_MAGIC_END_, _MEM_DEBUG_MAGIC_SIZE_))
       {
         cl_msg_out("\n *** cl_free ERROR: BAD Magic End in free of memory:%p file:%s line:%d\n", 
                    p_memory , p_file_name, line_num
                    );
       }
       /* now poisen */
       memset(p_mem, 0x5A, size + _MEM_DEBUG_EXTRA_SIZE_);
     }
     __cl_free_priv( p_mem );
   }
#else
	__cl_free_priv( p_memory );
#endif
}

void
__cl_free_ntrk(
	IN	void* const	p_memory )
{
	__cl_free_priv( p_memory );
}

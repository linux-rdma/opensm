/*
 * Copyright (c) 2004-2007 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2006 Mellanox Technologies LTD. All rights reserved.
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
 *    Implementation of OpenSM FatTree routing
 *
 * Environment:
 *    Linux User Mode
 *
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <iba/ib_types.h>
#include <complib/cl_qmap.h>
#include <complib/cl_pool.h>
#include <complib/cl_debug.h>
#include <opensm/osm_opensm.h>
#include <opensm/osm_switch.h>

/*
 * FatTree rank is bounded between 2 and 8:
 *  - Tree of rank 1 has only trivial routing pathes,
 *    so no need to use FatTree routing.
 *  - Why maximum rank is 8:
 *    Each node (switch) is assigned a unique tuple.
 *    Switches are stored in two cl_qmaps - one is 
 *    ordered by guid, and the other by a key that is
 *    generated from tuple. Since cl_qmap supports only
 *    a 64-bit key, the maximal tuple lenght is 8 bytes.
 *    which means that maximal tree rank is 8.
 * Note that the above also implies that each switch 
 * can have at max 255 up/down ports.
 */

#define FAT_TREE_MIN_RANK 2
#define FAT_TREE_MAX_RANK 8

typedef enum {
   FTREE_DIRECTION_DOWN = -1,
   FTREE_DIRECTION_SAME,
   FTREE_DIRECTION_UP
} ftree_direction_t;


/***************************************************
 **
 **  Forward references
 **
 ***************************************************/

struct ftree_sw_t_;
struct ftree_hca_t_;
struct ftree_port_t_;
struct ftree_port_group_t_;
struct ftree_fabric_t_;

/***************************************************
 **
 **  ftree_tuple_t definition
 **
 ***************************************************/

#define FTREE_TUPLE_BUFF_LEN 1024
#define FTREE_TUPLE_LEN 8

typedef uint8_t ftree_tuple_t[FTREE_TUPLE_LEN];
typedef uint64_t ftree_tuple_key_t;

/***************************************************
 **
 **  ftree_sw_table_element_t definition
 **
 ***************************************************/

typedef struct {
   cl_map_item_t map_item;
   struct ftree_sw_t_ * p_sw;
} ftree_sw_tbl_element_t;

/***************************************************
 **
 **  ftree_fwd_tbl_t definition
 **
 ***************************************************/

typedef uint8_t * ftree_fwd_tbl_t;
#define FTREE_FWD_TBL_LEN (IB_LID_UCAST_END_HO + 1)

/***************************************************
 **
 **  ftree_port_t definition
 **
 ***************************************************/

typedef struct ftree_port_t_ 
{
   cl_map_item_t  map_item;
   uint8_t        port_num;           /* port number on the current node */
   uint8_t        remote_port_num;    /* port number on the remote node */
   uint32_t       counter_up;         /* number of allocated routs upwards */
   uint32_t       counter_down;       /* number of allocated routs downwards */
} ftree_port_t;

/***************************************************
 **
 **  ftree_port_group_t definition
 **
 ***************************************************/

typedef struct ftree_port_group_t_
{
   cl_map_item_t  map_item;
   ib_net16_t     base_lid;           /* base lid of the current node */
   ib_net16_t     remote_base_lid;    /* base lid of the remote node */
   ib_net64_t     port_guid;          /* port guid of this port */
   ib_net64_t     remote_port_guid;   /* port guid of the remote port */
   ib_net64_t     remote_node_guid;   /* node guid of the remote node */
   uint8_t        remote_node_type;   /* IB_NODE_TYPE_{CA,SWITCH,ROUTER,...} */
   union remote_hca_or_sw_
   {
      struct ftree_hca_t_ * remote_hca;
      struct ftree_sw_t_  * remote_sw;
   } remote_hca_or_sw;                /* pointer to remote hca/switch */
   cl_ptr_vector_t ports;             /* vector of ports to the same lid */
} ftree_port_group_t;

/***************************************************
 **
 **  ftree_sw_t definition
 **
 ***************************************************/

typedef struct ftree_sw_t_ 
{
   cl_map_item_t          map_item;
   osm_switch_t         * p_osm_sw;
   uint8_t                rank;
   ftree_tuple_t          tuple;
   ib_net16_t             base_lid;
   ftree_port_group_t  ** down_port_groups;
   uint8_t                down_port_groups_num;
   ftree_port_group_t  ** up_port_groups;
   uint8_t                up_port_groups_num;
   ftree_fwd_tbl_t        lft_buf;
} ftree_sw_t;

/***************************************************
 **
 **  ftree_hca_t definition
 **
 ***************************************************/

typedef struct ftree_hca_t_ {
   cl_map_item_t          map_item;
   osm_node_t           * p_osm_node;
   ftree_port_group_t  ** up_port_groups;
   uint16_t               up_port_groups_num;
} ftree_hca_t;

/***************************************************
 **
 **  ftree_fabric_t definition
 **
 ***************************************************/

typedef struct ftree_fabric_t_ 
{
   osm_opensm_t  * p_osm;
   cl_qmap_t       hca_tbl;
   cl_qmap_t       sw_tbl;
   cl_qmap_t       sw_by_tuple_tbl;
   uint8_t         tree_rank;
   ftree_sw_t   ** leaf_switches;
   uint32_t        leaf_switches_num;
   uint16_t        max_hcas_per_leaf;
   cl_pool_t       sw_fwd_tbl_pool;
   uint16_t        lft_max_lid_ho;
   boolean_t       fabric_built;
} ftree_fabric_t;

/***************************************************
 **
 ** comparators
 **
 ***************************************************/

static int OSM_CDECL
__osm_ftree_compare_switches_by_index(
   IN  const void * p1, 
   IN  const void * p2)
{
   ftree_sw_t ** pp_sw1 = (ftree_sw_t **)p1; 
   ftree_sw_t ** pp_sw2 = (ftree_sw_t **)p2; 

   uint16_t i;
   for (i = 0; i < FTREE_TUPLE_LEN; i++)
   {
      if ((*pp_sw1)->tuple[i] > (*pp_sw2)->tuple[i])
         return 1;
      if ((*pp_sw1)->tuple[i] < (*pp_sw2)->tuple[i])
         return -1;
   }
   return 0;
}

/***************************************************/

static int OSM_CDECL
__osm_ftree_compare_port_groups_by_remote_switch_index(
   IN  const void * p1, 
   IN  const void * p2)
{
   ftree_port_group_t ** pp_g1 = (ftree_port_group_t **)p1; 
   ftree_port_group_t ** pp_g2 = (ftree_port_group_t **)p2; 

   return __osm_ftree_compare_switches_by_index( 
                  &((*pp_g1)->remote_hca_or_sw.remote_sw),
                  &((*pp_g2)->remote_hca_or_sw.remote_sw) );
}

/***************************************************/

boolean_t
__osm_ftree_sw_less_by_index(
   IN  ftree_sw_t * p_sw1,
   IN  ftree_sw_t * p_sw2)
{
   if (__osm_ftree_compare_switches_by_index((void *)&p_sw1,
                                             (void *)&p_sw2) < 0)
      return TRUE;
   return FALSE;
}

/***************************************************/

boolean_t
__osm_ftree_sw_greater_by_index(
   IN  ftree_sw_t * p_sw1,
   IN  ftree_sw_t * p_sw2)
{
   if (__osm_ftree_compare_switches_by_index((void *)&p_sw1,
                                             (void *)&p_sw2) > 0)
      return TRUE;
   return FALSE;
}

/***************************************************
 **
 ** ftree_tuple_t functions
 **
 ***************************************************/

static void 
__osm_ftree_tuple_init(
   IN  ftree_tuple_t tuple)
{
   memset(tuple, 0xFF, FTREE_TUPLE_LEN);
}

/***************************************************/

static inline boolean_t
__osm_ftree_tuple_assigned(
   IN  ftree_tuple_t tuple)
{
   return (tuple[0] != 0xFF);
}

/***************************************************/

#define FTREE_TUPLE_BUFFERS_NUM 6

static char * 
__osm_ftree_tuple_to_str(
   IN  ftree_tuple_t tuple)
{
   static char buffer[FTREE_TUPLE_BUFFERS_NUM][FTREE_TUPLE_BUFF_LEN];
   static uint8_t ind = 0;
   char * ret_buffer;
   uint32_t i;

   if (!__osm_ftree_tuple_assigned(tuple))
      return "INDEX.NOT.ASSIGNED";

   buffer[ind][0] = '\0';

   for(i = 0; (i < FTREE_TUPLE_LEN) && (tuple[i] != 0xFF); i++)
   {
      if ((strlen(buffer[ind]) + 10) > FTREE_TUPLE_BUFF_LEN)
         return "INDEX.TOO.LONG";
      if (i != 0)
         strcat(buffer[ind],".");
      sprintf(&buffer[ind][strlen(buffer[ind])], "%u", tuple[i]);
   }

   ret_buffer = buffer[ind];
   ind = (ind + 1) % FTREE_TUPLE_BUFFERS_NUM;
   return ret_buffer;
} /* __osm_ftree_tuple_to_str() */

/***************************************************/

static inline ftree_tuple_key_t 
__osm_ftree_tuple_to_key(
   IN  ftree_tuple_t tuple)
{
   ftree_tuple_key_t key;
   memcpy(&key, tuple, FTREE_TUPLE_LEN);
   return key;
}

/***************************************************/

static inline void 
__osm_ftree_tuple_from_key(
   IN  ftree_tuple_t tuple, 
   IN  ftree_tuple_key_t key)
{
   memcpy(tuple, &key, FTREE_TUPLE_LEN);
}

/***************************************************
 **
 ** ftree_sw_tbl_element_t functions
 **
 ***************************************************/

static ftree_sw_tbl_element_t *
__osm_ftree_sw_tbl_element_create(
   IN  ftree_sw_t * p_sw)
{
   ftree_sw_tbl_element_t * p_element = 
      (ftree_sw_tbl_element_t *) malloc(sizeof(ftree_sw_tbl_element_t));
   if (!p_element)
       return NULL;
   memset(p_element, 0,sizeof(ftree_sw_tbl_element_t));

   if (p_element)
      p_element->p_sw = p_sw;
   return p_element;
}

/***************************************************/

static void
__osm_ftree_sw_tbl_element_destroy(
   IN  ftree_sw_tbl_element_t * p_element)
{
   if (!p_element)
      return;
   free(p_element);
}

/***************************************************
 **
 ** ftree_port_t functions
 **
 ***************************************************/

static ftree_port_t * 
__osm_ftree_port_create( 
   IN  uint8_t port_num,
   IN  uint8_t remote_port_num)
{
   ftree_port_t * p_port = (ftree_port_t *)malloc(sizeof(ftree_port_t));
   if (!p_port)
      return NULL;
   memset(p_port,0,sizeof(ftree_port_t));

   p_port->port_num = port_num;
   p_port->remote_port_num = remote_port_num;

   return p_port;
}

/***************************************************/

static void 
__osm_ftree_port_destroy(
   IN  ftree_port_t * p_port)
{
   if(p_port)
      free(p_port);
}

/***************************************************
 **
 ** ftree_port_group_t functions
 **
 ***************************************************/

static ftree_port_group_t * 
__osm_ftree_port_group_create( 
   IN  ib_net16_t    base_lid,
   IN  ib_net16_t    remote_base_lid,
   IN  ib_net64_t  * p_port_guid,
   IN  ib_net64_t  * p_remote_port_guid,
   IN  ib_net64_t  * p_remote_node_guid,
   IN  uint8_t       remote_node_type,
   IN  void        * p_remote_hca_or_sw)
{
   ftree_port_group_t * p_group = 
            (ftree_port_group_t *)malloc(sizeof(ftree_port_group_t));
   if (p_group == NULL) 
      return NULL;
   memset(p_group, 0, sizeof(ftree_port_group_t));

   p_group->base_lid = base_lid;
   p_group->remote_base_lid = remote_base_lid;
   memcpy(&p_group->port_guid, p_port_guid, sizeof(ib_net64_t));
   memcpy(&p_group->remote_port_guid, p_remote_port_guid, sizeof(ib_net64_t));
   memcpy(&p_group->remote_node_guid, p_remote_node_guid, sizeof(ib_net64_t));

   p_group->remote_node_type = remote_node_type;
   switch (remote_node_type)
   {
      case IB_NODE_TYPE_CA:
         p_group->remote_hca_or_sw.remote_hca = (ftree_hca_t *)p_remote_hca_or_sw;
         break;
      case IB_NODE_TYPE_SWITCH:
         p_group->remote_hca_or_sw.remote_sw = (ftree_sw_t *)p_remote_hca_or_sw;
         break;
      default:
         /* we shouldn't get here - port is created only in hca or switch */
         CL_ASSERT(0);
   }

   cl_ptr_vector_init(&p_group->ports,
                      0,  /* min size */
                      8); /* grow size */
   return p_group;
} /* __osm_ftree_port_group_create() */

/***************************************************/

static void 
__osm_ftree_port_group_destroy(
   IN  ftree_port_group_t * p_group)
{
   uint32_t i;
   uint32_t size;
   ftree_port_t * p_port;

   if (!p_group)
      return;

   /* remove all the elements of p_group->ports vector */
   size = cl_ptr_vector_get_size(&p_group->ports);
   for (i = 0; i < size; i++)
   {
      cl_ptr_vector_at(&p_group->ports, i, (void **)&p_port);
      __osm_ftree_port_destroy(p_port);
   }
   cl_ptr_vector_destroy(&p_group->ports);
   free(p_group);
} /* __osm_ftree_port_group_destroy() */

/***************************************************/

static void 
__osm_ftree_port_group_dump(
   IN  ftree_fabric_t *p_ftree,
   IN  ftree_port_group_t * p_group,
   IN  ftree_direction_t direction)
{
   ftree_port_t * p_port;
   uint32_t size;
   uint32_t i;
   char buff[10*1024];

   if (!p_group)
      return;

   if (!osm_log_is_active(&p_ftree->p_osm->log, OSM_LOG_DEBUG))
      return;

   size = cl_ptr_vector_get_size(&p_group->ports);
   buff[0] = '\0';

   for (i = 0; i < size; i++)
   {
      cl_ptr_vector_at(&p_group->ports, i, (void **)&p_port);
      CL_ASSERT(p_port);

      if (i != 0)
         strcat(buff,", ");
      sprintf(buff + strlen(buff), "%u", p_port->port_num);
   }

   osm_log(&p_ftree->p_osm->log, OSM_LOG_DEBUG,
           "__osm_ftree_port_group_dump:"
           "    Port Group of size %u, port(s): %s, direction: %s\n" 
           "                  Local <--> Remote GUID (LID):"
           "0x%016" PRIx64 " (0x%x) <--> 0x%016" PRIx64 " (0x%x)\n", 
           size,
           buff,
           (direction == FTREE_DIRECTION_DOWN)? "DOWN" : "UP",
           cl_ntoh64(p_group->port_guid),
           cl_ntoh16(p_group->base_lid),
           cl_ntoh64(p_group->remote_port_guid),
           cl_ntoh16(p_group->remote_base_lid));

} /* __osm_ftree_port_group_dump() */

/***************************************************/

static void
__osm_ftree_port_group_add_port(
   IN  ftree_port_group_t * p_group,
   IN  uint8_t              port_num,
   IN  uint8_t              remote_port_num)
{
   uint16_t i;
   ftree_port_t * p_port;

   for (i = 0; i < cl_ptr_vector_get_size(&p_group->ports); i++)
   {
      cl_ptr_vector_at(&p_group->ports, i, (void **)&p_port);
      if (p_port->port_num == port_num)
         return;
   }

   p_port = __osm_ftree_port_create(port_num,remote_port_num);
   cl_ptr_vector_insert(&p_group->ports, p_port, NULL);
}

/***************************************************
 **
 ** ftree_sw_t functions
 **
 ***************************************************/

static ftree_sw_t * 
__osm_ftree_sw_create(
   IN  ftree_fabric_t * p_ftree,
   IN  osm_switch_t   * p_osm_sw)
{
   ftree_sw_t * p_sw;
   uint8_t ports_num;

   /* make sure that the switch has ports */
   if (p_osm_sw->num_ports == 1)
      return NULL;

   p_sw = (ftree_sw_t *)malloc(sizeof(ftree_sw_t));
   if (p_sw == NULL) 
      return NULL;
   memset(p_sw, 0, sizeof(ftree_sw_t));

   p_sw->p_osm_sw = p_osm_sw;
   p_sw->rank = 0xFF;
   __osm_ftree_tuple_init(p_sw->tuple);

   p_sw->base_lid = osm_node_get_base_lid(p_sw->p_osm_sw->p_node, 0);

   ports_num = osm_node_get_num_physp(p_sw->p_osm_sw->p_node);
   p_sw->down_port_groups = 
      (ftree_port_group_t **) malloc(ports_num * sizeof(ftree_port_group_t *));
   p_sw->up_port_groups = 
      (ftree_port_group_t **) malloc(ports_num * sizeof(ftree_port_group_t *));
   if (!p_sw->down_port_groups || !p_sw->up_port_groups)
      return NULL;
   p_sw->down_port_groups_num = 0;
   p_sw->up_port_groups_num = 0;

   /* initialize lft buffer */
   p_sw->lft_buf = (ftree_fwd_tbl_t)cl_pool_get(&p_ftree->sw_fwd_tbl_pool);
   memset(p_sw->lft_buf, OSM_NO_PATH, FTREE_FWD_TBL_LEN);

   return p_sw;
} /* __osm_ftree_sw_create() */

/***************************************************/

static void 
__osm_ftree_sw_destroy(
   IN  ftree_fabric_t * p_ftree,
   IN  ftree_sw_t     * p_sw)
{
   uint8_t i;

   if (!p_sw)
      return;

   for (i = 0; i < p_sw->down_port_groups_num; i++)
      __osm_ftree_port_group_destroy(p_sw->down_port_groups[i]);
   for (i = 0; i < p_sw->up_port_groups_num; i++)
      __osm_ftree_port_group_destroy(p_sw->up_port_groups[i]);
   if (p_sw->down_port_groups)
      free(p_sw->down_port_groups);
   if (p_sw->up_port_groups)
      free(p_sw->up_port_groups);

   /* return switch fwd_tbl to pool */
   if (p_sw->lft_buf)
      cl_pool_put(&p_ftree->sw_fwd_tbl_pool, (void *)p_sw->lft_buf);

   free(p_sw);
} /* __osm_ftree_sw_destroy() */

/***************************************************/

static void 
__osm_ftree_sw_dump(
   IN  ftree_fabric_t * p_ftree,
   IN  ftree_sw_t * p_sw)
{
   uint32_t i;

   if (!p_sw)
      return;

   if (!osm_log_is_active(&p_ftree->p_osm->log, OSM_LOG_DEBUG))
      return;

   osm_log(&p_ftree->p_osm->log, OSM_LOG_DEBUG,
           "__osm_ftree_sw_dump: "
           "Switch index: %s, GUID: 0x%016" PRIx64 ", Ports: %u DOWN, %u UP\n",
          __osm_ftree_tuple_to_str(p_sw->tuple),
          cl_ntoh64(osm_node_get_node_guid(p_sw->p_osm_sw->p_node)),
          p_sw->down_port_groups_num, 
          p_sw->up_port_groups_num);

   for( i = 0; i < p_sw->down_port_groups_num; i++ )
      __osm_ftree_port_group_dump(p_ftree,
                                  p_sw->down_port_groups[i],
                                  FTREE_DIRECTION_DOWN);
   for( i = 0; i < p_sw->up_port_groups_num; i++ )
      __osm_ftree_port_group_dump(p_ftree,
                                  p_sw->up_port_groups[i],
                                  FTREE_DIRECTION_UP);

} /* __osm_ftree_sw_dump() */

/***************************************************/

static boolean_t
__osm_ftree_sw_ranked(
   IN  ftree_sw_t * p_sw)
{
   return (p_sw->rank != 0xFF); 
}

/***************************************************/

static ftree_port_group_t *
__osm_ftree_sw_get_port_group_by_remote_lid(
   IN  ftree_sw_t       * p_sw,
   IN  ib_net16_t         remote_base_lid,
   IN  ftree_direction_t  direction)
{
   uint32_t i;
   uint32_t size;
   ftree_port_group_t ** port_groups;

   if (direction == FTREE_DIRECTION_UP)
   {
      port_groups = p_sw->up_port_groups;
      size = p_sw->up_port_groups_num;
   }
   else
   {
      port_groups = p_sw->down_port_groups;
      size = p_sw->down_port_groups_num;
   }

   for (i = 0; i < size; i++)
      if (remote_base_lid == port_groups[i]->remote_base_lid)
         return port_groups[i];

   return NULL;
} /* __osm_ftree_sw_get_port_group_by_remote_lid() */

/***************************************************/

static void 
__osm_ftree_sw_add_port(
   IN  ftree_sw_t       * p_sw,
   IN  uint8_t            port_num,
   IN  uint8_t            remote_port_num,
   IN  ib_net16_t         base_lid,
   IN  ib_net16_t         remote_base_lid,
   IN  ib_net64_t         port_guid,
   IN  ib_net64_t         remote_port_guid,
   IN  ib_net64_t         remote_node_guid,
   IN  uint8_t            remote_node_type,
   IN  void             * p_remote_hca_or_sw,
   IN  ftree_direction_t  direction)
{
   ftree_port_group_t * p_group = 
       __osm_ftree_sw_get_port_group_by_remote_lid(p_sw,remote_base_lid,direction);

   if (!p_group)
   {
      p_group = __osm_ftree_port_group_create(
                     base_lid,
                     remote_base_lid,
                     &port_guid,
                     &remote_port_guid,
                     &remote_node_guid,
                     remote_node_type,
                     p_remote_hca_or_sw);
      CL_ASSERT(p_group);

      if (direction == FTREE_DIRECTION_UP)
         p_sw->up_port_groups[p_sw->up_port_groups_num++] = p_group;
      else
         p_sw->down_port_groups[p_sw->down_port_groups_num++] = p_group;
   }
   __osm_ftree_port_group_add_port(p_group,port_num,remote_port_num);

} /* __osm_ftree_sw_add_port() */

/***************************************************/

static inline void
__osm_ftree_sw_set_fwd_table_block(
    IN  ftree_sw_t * p_sw,
    IN  uint16_t     lid_ho, 
    IN  uint8_t      port_num)
{
   p_sw->lft_buf[lid_ho] = port_num;
}

/***************************************************/

static inline uint8_t
__osm_ftree_sw_get_fwd_table_block(
    IN  ftree_sw_t * p_sw,
    IN  uint16_t     lid_ho)
{
   return p_sw->lft_buf[lid_ho];
}

/***************************************************/

static inline cl_status_t
__osm_ftree_sw_set_hops(
   IN  ftree_sw_t     * p_sw,
   IN  uint16_t         max_lid_ho,
   IN  uint16_t         lid_ho,
   IN  uint8_t          port_num,
   IN  uint8_t          hops)
{
   /* set local min hop table(LID) */
   return osm_switch_set_hops(p_sw->p_osm_sw,
                              lid_ho,
                              port_num,
                              hops);
}

/***************************************************
 **
 ** ftree_hca_t functions
 **
 ***************************************************/

static ftree_hca_t * 
__osm_ftree_hca_create(
   IN  osm_node_t * p_osm_node)
{
   ftree_hca_t * p_hca = (ftree_hca_t *)malloc(sizeof(ftree_hca_t));
   if (p_hca == NULL) 
      return NULL;
   memset(p_hca,0,sizeof(ftree_hca_t));

   p_hca->p_osm_node = p_osm_node;
   p_hca->up_port_groups = (ftree_port_group_t **) 
        malloc(osm_node_get_num_physp(p_hca->p_osm_node) * sizeof (ftree_port_group_t *));
   if (!p_hca->up_port_groups)
      return NULL;
   p_hca->up_port_groups_num = 0;
   return p_hca;
}

/***************************************************/

static void 
__osm_ftree_hca_destroy(
   IN  ftree_hca_t * p_hca)
{
   uint32_t i;

   if (!p_hca)
      return;

   for (i = 0; i < p_hca->up_port_groups_num; i++)
      __osm_ftree_port_group_destroy(p_hca->up_port_groups[i]);

   if (p_hca->up_port_groups)
      free(p_hca->up_port_groups);

   free(p_hca);
}

/***************************************************/

static void 
__osm_ftree_hca_dump(
   IN  ftree_fabric_t * p_ftree,
   IN  ftree_hca_t * p_hca)
{
   uint32_t i;

   if (!p_hca)
      return;

   if (!osm_log_is_active(&p_ftree->p_osm->log,OSM_LOG_DEBUG))
      return;

   osm_log(&p_ftree->p_osm->log, OSM_LOG_DEBUG,
           "__osm_ftree_hca_dump: "
           "HCA GUID: 0x%016" PRIx64 ", Ports: %u UP\n",
          cl_ntoh64(osm_node_get_node_guid(p_hca->p_osm_node)), 
          p_hca->up_port_groups_num);

   for( i = 0; i < p_hca->up_port_groups_num; i++ ) 
      __osm_ftree_port_group_dump(p_ftree,
                                  p_hca->up_port_groups[i],
                                  FTREE_DIRECTION_UP);
}

/***************************************************/

static ftree_port_group_t *
__osm_ftree_hca_get_port_group_by_remote_lid(
   IN  ftree_hca_t * p_hca,
   IN  ib_net16_t    remote_base_lid)
{
   uint32_t i;
   for (i = 0; i < p_hca->up_port_groups_num; i++)
      if (remote_base_lid == p_hca->up_port_groups[i]->remote_base_lid)
         return p_hca->up_port_groups[i];

   return NULL;
}

/***************************************************/

static void 
__osm_ftree_hca_add_port(
   IN  ftree_hca_t * p_hca,
   IN  uint8_t       port_num,
   IN  uint8_t       remote_port_num,
   IN  ib_net16_t    base_lid,
   IN  ib_net16_t    remote_base_lid,
   IN  ib_net64_t    port_guid,
   IN  ib_net64_t    remote_port_guid,
   IN  ib_net64_t    remote_node_guid,
   IN  uint8_t       remote_node_type,
   IN  void        * p_remote_hca_or_sw)
{
   ftree_port_group_t * p_group;

   /* this function is supposed to be called only for adding ports
      in hca's that lead to switches */ 
   CL_ASSERT(remote_node_type == IB_NODE_TYPE_SWITCH);

   p_group = __osm_ftree_hca_get_port_group_by_remote_lid(p_hca,remote_base_lid);

   if (!p_group)
   {
      p_group = __osm_ftree_port_group_create(
                     base_lid,
                     remote_base_lid,
                     &port_guid,
                     &remote_port_guid,
                     &remote_node_guid,
                     remote_node_type,
                     p_remote_hca_or_sw);
      p_hca->up_port_groups[p_hca->up_port_groups_num++] = p_group;
   }
   __osm_ftree_port_group_add_port(p_group, port_num, remote_port_num);

} /* __osm_ftree_hca_add_port() */

/***************************************************
 **
 ** ftree_fabric_t functions
 **
 ***************************************************/

static ftree_fabric_t * 
__osm_ftree_fabric_create()
{
   cl_status_t status;
   ftree_fabric_t * p_ftree = (ftree_fabric_t *)malloc(sizeof(ftree_fabric_t));
   if (p_ftree == NULL) 
      return NULL;

   memset(p_ftree,0,sizeof(ftree_fabric_t));

   cl_qmap_init(&p_ftree->hca_tbl);
   cl_qmap_init(&p_ftree->sw_tbl);
   cl_qmap_init(&p_ftree->sw_by_tuple_tbl);

   status = cl_pool_init( &p_ftree->sw_fwd_tbl_pool,
                          8,                 /* min pool size */
                          0,                 /* max pool size - unlimited */
                          8,                 /* grow size */
                          FTREE_FWD_TBL_LEN, /* object_size */
                          NULL,              /* object initializer */
                          NULL,              /* object destructor */
                          NULL );            /* context */
   if (status != CL_SUCCESS)
      return NULL;

   p_ftree->tree_rank = 1;
   return p_ftree;
}

/***************************************************/

static void 
__osm_ftree_fabric_clear(ftree_fabric_t * p_ftree)
{
   ftree_hca_t * p_hca;
   ftree_hca_t * p_next_hca;
   ftree_sw_t * p_sw;
   ftree_sw_t * p_next_sw;
   ftree_sw_tbl_element_t * p_element;
   ftree_sw_tbl_element_t * p_next_element;

   if (!p_ftree)
      return;

   /* remove all the elements of hca_tbl */

   p_next_hca = (ftree_hca_t *)cl_qmap_head(&p_ftree->hca_tbl);
   while( p_next_hca != (ftree_hca_t *)cl_qmap_end( &p_ftree->hca_tbl ) )
   {
      p_hca = p_next_hca;
      p_next_hca = (ftree_hca_t *)cl_qmap_next(&p_hca->map_item );
      __osm_ftree_hca_destroy(p_hca);
   }
   cl_qmap_remove_all(&p_ftree->hca_tbl);

   /* remove all the elements of sw_tbl */

   p_next_sw = (ftree_sw_t *)cl_qmap_head(&p_ftree->sw_tbl);
   while( p_next_sw != (ftree_sw_t *)cl_qmap_end( &p_ftree->sw_tbl ) )
   {
      p_sw = p_next_sw;
      p_next_sw = (ftree_sw_t *)cl_qmap_next(&p_sw->map_item );
      __osm_ftree_sw_destroy(p_ftree,p_sw);
   }
   cl_qmap_remove_all(&p_ftree->sw_tbl);

   /* remove all the elements of sw_by_tuple_tbl */

   p_next_element = 
      (ftree_sw_tbl_element_t *)cl_qmap_head(&p_ftree->sw_by_tuple_tbl);
   while( p_next_element != 
          (ftree_sw_tbl_element_t *)cl_qmap_end( &p_ftree->sw_by_tuple_tbl ) )
   {
      p_element = p_next_element;
      p_next_element = 
         (ftree_sw_tbl_element_t *)cl_qmap_next(&p_element->map_item);
      __osm_ftree_sw_tbl_element_destroy(p_element);
   }
   cl_qmap_remove_all(&p_ftree->sw_by_tuple_tbl);

   /* free the leaf switches array */
   if ((p_ftree->leaf_switches_num > 0) && (p_ftree->leaf_switches))
      free(p_ftree->leaf_switches);

   p_ftree->leaf_switches_num = 0;
   p_ftree->leaf_switches = NULL;
   p_ftree->fabric_built = FALSE;

} /* __osm_ftree_fabric_destroy() */

/***************************************************/

static void 
__osm_ftree_fabric_destroy(ftree_fabric_t * p_ftree)
{
   if (!p_ftree)
      return;
   __osm_ftree_fabric_clear(p_ftree);
   cl_pool_destroy(&p_ftree->sw_fwd_tbl_pool);
   free(p_ftree);
}

/***************************************************/

static void 
__osm_ftree_fabric_set_rank(ftree_fabric_t * p_ftree, uint8_t rank)
{
   if (rank > p_ftree->tree_rank)
      p_ftree->tree_rank = rank;
}

/***************************************************/

static uint8_t 
__osm_ftree_fabric_get_rank(ftree_fabric_t * p_ftree)
{
   return p_ftree->tree_rank;
}

/***************************************************/

static void 
__osm_ftree_fabric_add_hca(ftree_fabric_t * p_ftree, osm_node_t * p_osm_node)
{
   ftree_hca_t * p_hca = __osm_ftree_hca_create(p_osm_node);

   CL_ASSERT(osm_node_get_type(p_osm_node) == IB_NODE_TYPE_CA);

   cl_qmap_insert(&p_ftree->hca_tbl,
                  p_osm_node->node_info.node_guid,
                  &p_hca->map_item);
}

/***************************************************/

static void 
__osm_ftree_fabric_add_sw(ftree_fabric_t * p_ftree, osm_switch_t * p_osm_sw)
{
   ftree_sw_t * p_sw = __osm_ftree_sw_create(p_ftree,p_osm_sw);

   CL_ASSERT(osm_node_get_type(p_osm_sw->p_node) == IB_NODE_TYPE_SWITCH);

   cl_qmap_insert(&p_ftree->sw_tbl,
                  p_osm_sw->p_node->node_info.node_guid,
                  &p_sw->map_item);

   /* track the max lid (in host order) that exists in the fabric */
   if (cl_ntoh16(p_sw->base_lid) > p_ftree->lft_max_lid_ho)
      p_ftree->lft_max_lid_ho = cl_ntoh16(p_sw->base_lid);
}

/***************************************************/

static void 
__osm_ftree_fabric_add_sw_by_tuple(
   IN  ftree_fabric_t * p_ftree, 
   IN  ftree_sw_t * p_sw)
{
   CL_ASSERT(__osm_ftree_tuple_assigned(p_sw->tuple));

   cl_qmap_insert(&p_ftree->sw_by_tuple_tbl,
                  __osm_ftree_tuple_to_key(p_sw->tuple),
                  &__osm_ftree_sw_tbl_element_create(p_sw)->map_item);
}

/***************************************************/

static ftree_sw_t * 
__osm_ftree_fabric_get_sw_by_tuple(
   IN  ftree_fabric_t * p_ftree, 
   IN  ftree_tuple_t tuple)
{
   ftree_sw_tbl_element_t * p_element;

   CL_ASSERT(__osm_ftree_tuple_assigned(tuple));

   __osm_ftree_tuple_to_key(tuple);

   p_element = (ftree_sw_tbl_element_t * )cl_qmap_get(&p_ftree->sw_by_tuple_tbl,
                                                      __osm_ftree_tuple_to_key(tuple));
   if (p_element == (ftree_sw_tbl_element_t * )cl_qmap_end(&p_ftree->sw_by_tuple_tbl))
      return NULL;

   return p_element->p_sw;
}

/***************************************************/

static void 
__osm_ftree_fabric_dump(ftree_fabric_t * p_ftree)
{
   uint32_t i;
   ftree_hca_t * p_hca;
   ftree_sw_t * p_sw;

   if (!osm_log_is_active(&p_ftree->p_osm->log,OSM_LOG_DEBUG))
      return;

   osm_log(&p_ftree->p_osm->log, OSM_LOG_DEBUG,"__osm_ftree_fabric_dump: \n"
           "                       |-------------------------------|\n"
           "                       |-  Full fabric topology dump  -|\n"
           "                       |-------------------------------|\n\n");

   osm_log(&p_ftree->p_osm->log, OSM_LOG_DEBUG,
           "__osm_ftree_fabric_dump: -- HCAs:\n");

   for ( p_hca = (ftree_hca_t *)cl_qmap_head(&p_ftree->hca_tbl);
         p_hca != (ftree_hca_t *)cl_qmap_end(&p_ftree->hca_tbl);
         p_hca = (ftree_hca_t *)cl_qmap_next(&p_hca->map_item) )
   {
      __osm_ftree_hca_dump(p_ftree, p_hca);
   }

   for (i = 0; i < __osm_ftree_fabric_get_rank(p_ftree); i++)
   {
      osm_log(&p_ftree->p_osm->log, OSM_LOG_DEBUG,
              "__osm_ftree_fabric_dump: -- Rank %u switches\n", i);
      for ( p_sw = (ftree_sw_t *)cl_qmap_head(&p_ftree->sw_tbl);
            p_sw != (ftree_sw_t *)cl_qmap_end(&p_ftree->sw_tbl);
            p_sw = (ftree_sw_t *)cl_qmap_next(&p_sw->map_item) )
      {
         if (p_sw->rank == i)
            __osm_ftree_sw_dump(p_ftree, p_sw);
      }
   }

   osm_log(&p_ftree->p_osm->log, OSM_LOG_DEBUG,"__osm_ftree_fabric_dump: \n"
           "                       |---------------------------------------|\n"
           "                       |- Full fabric topology dump completed -|\n"
           "                       |---------------------------------------|\n\n");
} /* __osm_ftree_fabric_dump() */

/***************************************************/

static void 
__osm_ftree_fabric_dump_general_info(
   IN  ftree_fabric_t * p_ftree)
{
   uint32_t i,j;
   ftree_sw_t * p_sw;
   char * addition_str;

   osm_log(&p_ftree->p_osm->log, OSM_LOG_INFO,
           "__osm_ftree_fabric_dump_general_info: "
           "General fabric topology info\n");
   osm_log(&p_ftree->p_osm->log, OSM_LOG_INFO,"__osm_ftree_fabric_dump_general_info: "
           "============================\n");

   osm_log(&p_ftree->p_osm->log, OSM_LOG_INFO,
           "__osm_ftree_fabric_dump_general_info: "
           "  - FatTree rank (switches only): %u\n",
           p_ftree->tree_rank);
   osm_log(&p_ftree->p_osm->log, OSM_LOG_INFO,
           "__osm_ftree_fabric_dump_general_info: "
           "  - Fabric has %u HCAs, %u switches\n",
           cl_qmap_count(&p_ftree->hca_tbl),
           cl_qmap_count(&p_ftree->sw_tbl));

   for (i = 0; i < __osm_ftree_fabric_get_rank(p_ftree); i++)
   {
      j = 0;
      for ( p_sw = (ftree_sw_t *)cl_qmap_head(&p_ftree->sw_tbl);
            p_sw != (ftree_sw_t *)cl_qmap_end(&p_ftree->sw_tbl);
            p_sw = (ftree_sw_t *)cl_qmap_next(&p_sw->map_item) )
      {
         if (p_sw->rank == i)
            j++;
      }
      if (i == 0)
         addition_str = " (root) ";
      else 
         if (i == (__osm_ftree_fabric_get_rank(p_ftree) - 1))
            addition_str = " (leaf) ";
         else
            addition_str = " ";
         osm_log(&p_ftree->p_osm->log, OSM_LOG_INFO,
                 "__osm_ftree_fabric_dump_general_info: "
                 "  - Fabric has %u rank %u%s switches\n",
                 j, i, addition_str);
   }

   if (osm_log_is_active(&p_ftree->p_osm->log, OSM_LOG_VERBOSE))
   {
      osm_log(&p_ftree->p_osm->log, OSM_LOG_VERBOSE,
              "__osm_ftree_fabric_dump_general_info: "
              "  - Root switches:\n");
      for ( p_sw = (ftree_sw_t *)cl_qmap_head(&p_ftree->sw_tbl);
            p_sw != (ftree_sw_t *)cl_qmap_end(&p_ftree->sw_tbl);
            p_sw = (ftree_sw_t *)cl_qmap_next(&p_sw->map_item) )
      {
         if (p_sw->rank == 0)
               osm_log(&p_ftree->p_osm->log, OSM_LOG_VERBOSE,
                       "__osm_ftree_fabric_dump_general_info: "
                       "      GUID: 0x%016" PRIx64 ", LID: 0x%x, Index %s\n",
                       cl_ntoh64(osm_node_get_node_guid(p_sw->p_osm_sw->p_node)),
                       cl_ntoh16(p_sw->base_lid),
                       __osm_ftree_tuple_to_str(p_sw->tuple));
      }

      osm_log(&p_ftree->p_osm->log, OSM_LOG_VERBOSE,
              "__osm_ftree_fabric_dump_general_info: "
              "  - Leaf switches (sorted by index):\n");
      for (i = 0; i < p_ftree->leaf_switches_num; i++)
      {
            osm_log(&p_ftree->p_osm->log, OSM_LOG_VERBOSE,
                    "__osm_ftree_fabric_dump_general_info: "
                    "      GUID: 0x%016" PRIx64 ", LID: 0x%x, Index %s\n",
                    cl_ntoh64(osm_node_get_node_guid(
                              p_ftree->leaf_switches[i]->p_osm_sw->p_node)),
                    cl_ntoh16(p_ftree->leaf_switches[i]->base_lid),
                    __osm_ftree_tuple_to_str(p_ftree->leaf_switches[i]->tuple));
      }
   }
} /* __osm_ftree_fabric_dump_general_info() */

/***************************************************/

static void 
__osm_ftree_fabric_dump_hca_ordering(
   IN  ftree_fabric_t * p_ftree)
{  
   ftree_hca_t        * p_hca;
   ftree_sw_t         * p_sw;
   ftree_port_group_t * p_group;
   uint32_t             i;
   uint32_t             j;

   char path[1024];
   FILE * p_hca_ordering_file;
   char * filename = "opensm-ftree-ca-order.dump";

   snprintf(path, sizeof(path), "%s/%s", 
            p_ftree->p_osm->subn.opt.dump_files_dir, filename);
   p_hca_ordering_file = fopen(path, "w");
   if (!p_hca_ordering_file) 
   {
      osm_log(&p_ftree->p_osm->log, OSM_LOG_ERROR,
              "__osm_ftree_fabric_dump_hca_ordering: ERR AB01: "
              "cannot open file \'%s\': %s\n",
               filename, strerror(errno));
      OSM_LOG_EXIT(&p_ftree->p_osm->log);
      return;
   }
   
   /* for each leaf switch (in indexing order) */
   for(i = 0; i < p_ftree->leaf_switches_num; i++)
   {
      p_sw = p_ftree->leaf_switches[i];
      /* for each real HCA connected to this switch */
      for (j = 0; j < p_sw->down_port_groups_num; j++)
      {
         p_group = p_sw->down_port_groups[j];
         p_hca = p_group->remote_hca_or_sw.remote_hca;

         fprintf(p_hca_ordering_file,"0x%x\t%s\n", 
                 cl_ntoh16(p_group->remote_base_lid),
                 p_hca->p_osm_node->print_desc);
      }

      /* now print dummy HCAs */
      for (j = p_sw->down_port_groups_num; j < p_ftree->max_hcas_per_leaf; j++)
      {
         fprintf(p_hca_ordering_file,"0xFFFF\tDUMMY\n");
      }

   }
   /* done going through all the leaf switches */

   fclose(p_hca_ordering_file);
} /* __osm_ftree_fabric_dump_hca_ordering() */

/***************************************************/

static void 
__osm_ftree_fabric_assign_tuple(
   IN   ftree_fabric_t * p_ftree,
   IN   ftree_sw_t * p_sw,
   IN   ftree_tuple_t new_tuple)
{
   memcpy(p_sw->tuple, new_tuple, FTREE_TUPLE_LEN);
   __osm_ftree_fabric_add_sw_by_tuple(p_ftree,p_sw);
}

/***************************************************/

static void 
__osm_ftree_fabric_assign_first_tuple(
   IN   ftree_fabric_t * p_ftree,
   IN   ftree_sw_t * p_sw)
{
   uint8_t i;
   ftree_tuple_t new_tuple;

   __osm_ftree_tuple_init(new_tuple);
   new_tuple[0] = p_sw->rank;
   for (i = 1; i <= p_sw->rank; i++)
      new_tuple[i] = 0;

   __osm_ftree_fabric_assign_tuple(p_ftree,p_sw,new_tuple);
}

/***************************************************/

static void
__osm_ftree_fabric_get_new_tuple(
   IN   ftree_fabric_t * p_ftree,
   OUT  ftree_tuple_t new_tuple,
   IN   ftree_tuple_t from_tuple,
   IN   ftree_direction_t direction)
{
   ftree_sw_t * p_sw;
   ftree_tuple_t temp_tuple;
   uint8_t var_index;
   uint8_t i;

   __osm_ftree_tuple_init(new_tuple);
   memcpy(temp_tuple, from_tuple, FTREE_TUPLE_LEN);

   if (direction == FTREE_DIRECTION_DOWN)
   {
      temp_tuple[0] ++;
      var_index = from_tuple[0] + 1;
   }
   else
   {
      temp_tuple[0] --;
      var_index = from_tuple[0];
   }

   for (i = 0; i < 0xFF; i++)
   {
      temp_tuple[var_index] = i;
      p_sw = __osm_ftree_fabric_get_sw_by_tuple(p_ftree,temp_tuple);
      if (p_sw == NULL) /* found free tuple */ 
         break;
   }

   if (i == 0xFF)
   {
      /* new tuple not found - there are more than 255 ports in one direction */
      return;
   }
   memcpy(new_tuple, temp_tuple, FTREE_TUPLE_LEN);

} /* __osm_ftree_fabric_get_new_tuple() */

/***************************************************/

static void
__osm_ftree_fabric_calculate_rank(
   IN  ftree_fabric_t * p_ftree)
{
   ftree_sw_t   * p_sw;
   ftree_sw_t   * p_next_sw;
   uint16_t       max_rank = 0;

   /* go over all the switches and find maximal switch rank */

   p_next_sw = (ftree_sw_t *)cl_qmap_head(&p_ftree->sw_tbl);
   while( p_next_sw != (ftree_sw_t *)cl_qmap_end(&p_ftree->sw_tbl) )
   {
      p_sw = p_next_sw;
      if(p_sw->rank > max_rank)
         max_rank = p_sw->rank;
      p_next_sw = (ftree_sw_t *)cl_qmap_next(&p_sw->map_item );
   }

   /* set FatTree rank */
   __osm_ftree_fabric_set_rank(p_ftree, max_rank + 1);
}

/***************************************************/

static void
__osm_ftree_fabric_make_indexing(
   IN   ftree_fabric_t * p_ftree)
{
   ftree_sw_t         * p_remote_sw;
   ftree_sw_t         * p_sw;
   ftree_sw_t         * p_next_sw;
   ftree_tuple_t        new_tuple;
   uint32_t             i;
   cl_list_t            bfs_list;
   ftree_sw_tbl_element_t * p_sw_tbl_element;

   OSM_LOG_ENTER(&p_ftree->p_osm->log, __osm_ftree_fabric_make_indexing);

   osm_log(&p_ftree->p_osm->log, OSM_LOG_VERBOSE,"__osm_ftree_fabric_make_indexing: "
           "Starting FatTree indexing\n");

   /* create array of leaf switches */
   p_ftree->leaf_switches = (ftree_sw_t **)
         malloc(cl_qmap_count(&p_ftree->sw_tbl) * sizeof(ftree_sw_t *));

   /* Looking for a leaf switch - the one that has rank equal to (tree_rank - 1).
      This switch will be used as a starting point for indexing algorithm. */

   p_next_sw = (ftree_sw_t *)cl_qmap_head(&p_ftree->sw_tbl);
   while( p_next_sw != (ftree_sw_t *)cl_qmap_end( &p_ftree->sw_tbl ) )
   {
      p_sw = p_next_sw;
      if(p_sw->rank == (__osm_ftree_fabric_get_rank(p_ftree) - 1))
         break;
      p_next_sw = (ftree_sw_t *)cl_qmap_next(&p_sw->map_item );
   }

   CL_ASSERT(p_next_sw != (ftree_sw_t *)cl_qmap_end(&p_ftree->sw_tbl));

   /* Assign the first tuple to the switch that is used as BFS starting point.
      The tuple will be as follows: [rank].0.0.0...
      This fuction also adds the switch it into the switch_by_tuple table. */
   __osm_ftree_fabric_assign_first_tuple(p_ftree,p_sw);

   osm_log(&p_ftree->p_osm->log, OSM_LOG_VERBOSE,
           "__osm_ftree_fabric_make_indexing: Indexing starting point:\n"
           "                                            - Switch rank  : %u\n"
           "                                            - Switch index : %s\n"
           "                                            - Node LID     : 0x%x\n"
           "                                            - Node GUID    : 0x%016" PRIx64 "\n",
           p_sw->rank,
           __osm_ftree_tuple_to_str(p_sw->tuple),
           cl_ntoh16(p_sw->base_lid),
           cl_ntoh64(osm_node_get_node_guid(p_sw->p_osm_sw->p_node)));

   /* 
    * Now run BFS and assign indexes to all switches
    * Pseudo code of the algorithm is as follows:
    *
    *  * Add first switch to BFS queue
    *  * While (BFS queue not empty)
    *      - Pop the switch from the head of the queue
    *      - Scan all the downward and upward ports
    *      - For each port
    *          + Get the remote switch
    *          + Assign index to the remote switch
    *          + Add remote switch to the BFS queue
    */

   cl_list_init(&bfs_list, cl_qmap_count(&p_ftree->sw_tbl));
   cl_list_insert_tail(&bfs_list, &__osm_ftree_sw_tbl_element_create(p_sw)->map_item);

   while (!cl_is_list_empty(&bfs_list))
   {
      p_sw_tbl_element = (ftree_sw_tbl_element_t *)cl_list_remove_head(&bfs_list);
      p_sw = p_sw_tbl_element->p_sw;
      __osm_ftree_sw_tbl_element_destroy(p_sw_tbl_element);

      /* Discover all the nodes from ports that are pointing down */

      if (p_sw->rank == (__osm_ftree_fabric_get_rank(p_ftree) - 1))
      {
         /* add switch to leaf switches array */
         p_ftree->leaf_switches[p_ftree->leaf_switches_num++] = p_sw;
         /* update the max_hcas_per_leaf value */
         if (p_sw->down_port_groups_num > p_ftree->max_hcas_per_leaf)
            p_ftree->max_hcas_per_leaf = p_sw->down_port_groups_num;
      }
      else
      {
         /* This is not the leaf switch, which means that all the
            ports that point down are taking us to another switches.
            No need to assign indexing to HCAs */
         for( i = 0; i < p_sw->down_port_groups_num; i++ ) 
         {
            p_remote_sw = p_sw->down_port_groups[i]->remote_hca_or_sw.remote_sw;
            if (__osm_ftree_tuple_assigned(p_remote_sw->tuple))
            {
               /* this switch has been already indexed */
               continue;
            }
            /* allocate new tuple */
            __osm_ftree_fabric_get_new_tuple(p_ftree,
                                             new_tuple,
                                             p_sw->tuple,
                                             FTREE_DIRECTION_DOWN);
            /* Assign the new tuple to the remote switch.
               This fuction also adds the switch into the switch_by_tuple table. */
            __osm_ftree_fabric_assign_tuple(p_ftree,
                                            p_remote_sw,
                                            new_tuple);

            /* add the newly discovered switch to the BFS queue */
            cl_list_insert_tail(&bfs_list, 
                                &__osm_ftree_sw_tbl_element_create(p_remote_sw)->map_item);
         }
         /* Done assigning indexes to all the remote switches 
            that are pointed by the downgoing ports. 
            Now sort port groups according to remote index. */
         qsort(p_sw->down_port_groups,                      /* array */
               p_sw->down_port_groups_num,                  /* number of elements */
               sizeof(ftree_port_group_t *),                /* size of each element */
               __osm_ftree_compare_port_groups_by_remote_switch_index); /* comparator */
      }

      /* Done indexing switches from ports that go down.
         Now do the same with ports that are pointing up. */

      if (p_sw->rank != 0)
      {
         /* This is not the root switch, which means that all the ports
            that are pointing up are taking us to another switches. */
         for( i = 0; i < p_sw->up_port_groups_num; i++ ) 
         {
            p_remote_sw = p_sw->up_port_groups[i]->remote_hca_or_sw.remote_sw;
            if (__osm_ftree_tuple_assigned(p_remote_sw->tuple))
               continue;
            /* allocate new tuple */
            __osm_ftree_fabric_get_new_tuple(p_ftree,
                                             new_tuple,
                                             p_sw->tuple,
                                             FTREE_DIRECTION_UP);
            /* Assign the new tuple to the remote switch.
               This fuction also adds the switch to the
               switch_by_tuple table. */
            __osm_ftree_fabric_assign_tuple(p_ftree,
                                            p_remote_sw,
                                            new_tuple);
            /* add the newly discovered switch to the BFS queue */
            cl_list_insert_tail(&bfs_list, 
                                &__osm_ftree_sw_tbl_element_create(p_remote_sw)->map_item);
         }
         /* Done assigning indexes to all the remote switches 
            that are pointed by the upgoing ports. 
            Now sort port groups according to remote index. */
         qsort(p_sw->up_port_groups,                        /* array */
               p_sw->up_port_groups_num,                    /* number of elements */
               sizeof(ftree_port_group_t *),                /* size of each element */
               __osm_ftree_compare_port_groups_by_remote_switch_index); /* comparator */
      }
      /* Done assigning indexes to all the switches that are directly connected 
         to the current switch - go to the next switch in the BFS queue */
   }

   /* sort array of leaf switches by index */
   qsort(p_ftree->leaf_switches,     /* array */
         p_ftree->leaf_switches_num, /* number of elements */
         sizeof(ftree_sw_t *),       /* size of each element */
         __osm_ftree_compare_switches_by_index); /* comparator */

   OSM_LOG_EXIT(&p_ftree->p_osm->log);
} /* __osm_ftree_fabric_make_indexing() */

/***************************************************/

static boolean_t
__osm_ftree_fabric_validate_topology(
   IN   ftree_fabric_t * p_ftree)
{
   ftree_port_group_t * p_group;
   ftree_port_group_t * p_ref_group;
   ftree_sw_t         * p_sw;
   ftree_sw_t         * p_next_sw;
   ftree_sw_t        ** reference_sw_arr;
   uint16_t             tree_rank = __osm_ftree_fabric_get_rank(p_ftree);
   boolean_t            res = TRUE;
   uint8_t              i;

   OSM_LOG_ENTER(&p_ftree->p_osm->log, __osm_ftree_fabric_validate_topology);

   osm_log(&p_ftree->p_osm->log, OSM_LOG_VERBOSE,
           "__osm_ftree_fabric_validate_topology: "
           "Validating fabric topology\n");

   reference_sw_arr = (ftree_sw_t **)malloc(tree_rank * sizeof(ftree_sw_t *));
   if ( reference_sw_arr == NULL )
   {
      osm_log(&p_ftree->p_osm->log, OSM_LOG_SYS,
              "Fat-tree routing: Memory allocation failed\n");
      return FALSE;
   }
   memset(reference_sw_arr, 0, tree_rank * sizeof(ftree_sw_t *));

   p_next_sw = (ftree_sw_t *)cl_qmap_head(&p_ftree->sw_tbl);
   while( res && 
          p_next_sw != (ftree_sw_t *)cl_qmap_end( &p_ftree->sw_tbl ) )
   {
      p_sw = p_next_sw;
      p_next_sw = (ftree_sw_t *)cl_qmap_next(&p_sw->map_item );

      if (!reference_sw_arr[p_sw->rank])
      {
         /* This is the first switch in the current level that 
            we're checking - use it as a reference */
         reference_sw_arr[p_sw->rank] = p_sw;
      }
      else
      {
         /* compare this switch properties to the reference switch */

         if ( reference_sw_arr[p_sw->rank]->up_port_groups_num != p_sw->up_port_groups_num )
         {
            osm_log(&p_ftree->p_osm->log, OSM_LOG_ERROR,
                    "__osm_ftree_fabric_validate_topology: "
                    "ERR AB09: Different number of upward port groups on switches:\n"
                    "       GUID 0x%016" PRIx64 ", LID 0x%x, Index %s - %u groups\n"
                    "       GUID 0x%016" PRIx64 ", LID 0x%x, Index %s - %u groups\n",
                    cl_ntoh64(osm_node_get_node_guid(reference_sw_arr[p_sw->rank]->p_osm_sw->p_node)),
                    cl_ntoh16(reference_sw_arr[p_sw->rank]->base_lid),
                    __osm_ftree_tuple_to_str(reference_sw_arr[p_sw->rank]->tuple),
                    reference_sw_arr[p_sw->rank]->up_port_groups_num,
                    cl_ntoh64(osm_node_get_node_guid(p_sw->p_osm_sw->p_node)),
                    cl_ntoh16(p_sw->base_lid),
                    __osm_ftree_tuple_to_str(p_sw->tuple),
                    p_sw->up_port_groups_num);
            res = FALSE;
            break;
         }

         if ( p_sw->rank != (__osm_ftree_fabric_get_rank(p_ftree) - 1) &&
              reference_sw_arr[p_sw->rank]->down_port_groups_num != p_sw->down_port_groups_num )
         {
            /* we're allowing some hca's to be missing */
            osm_log(&p_ftree->p_osm->log, OSM_LOG_ERROR,
                    "__osm_ftree_fabric_validate_topology: "
                    "ERR AB0A: Different number of downward port groups on switches:\n"
                    "       GUID 0x%016" PRIx64 ", LID 0x%x, Index %s - %u port groups\n"
                    "       GUID 0x%016" PRIx64 ", LID 0x%x, Index %s - %u port groups\n",
                    cl_ntoh64(osm_node_get_node_guid(reference_sw_arr[p_sw->rank]->p_osm_sw->p_node)),
                    cl_ntoh16(reference_sw_arr[p_sw->rank]->base_lid),
                    __osm_ftree_tuple_to_str(reference_sw_arr[p_sw->rank]->tuple),
                    reference_sw_arr[p_sw->rank]->down_port_groups_num,
                    cl_ntoh64(osm_node_get_node_guid(p_sw->p_osm_sw->p_node)),
                    cl_ntoh16(p_sw->base_lid),
                    __osm_ftree_tuple_to_str(p_sw->tuple),
                    p_sw->down_port_groups_num);
            res = FALSE;
            break;
         }

         if ( reference_sw_arr[p_sw->rank]->up_port_groups_num != 0 )
         {
            p_ref_group = reference_sw_arr[p_sw->rank]->up_port_groups[0];
            for (i = 0; i < p_sw->up_port_groups_num; i++)
            {
                p_group = p_sw->up_port_groups[i];
                if (cl_ptr_vector_get_size(&p_ref_group->ports) != cl_ptr_vector_get_size(&p_group->ports))
                {
                   osm_log(&p_ftree->p_osm->log, OSM_LOG_ERROR,
                           "__osm_ftree_fabric_validate_topology: "
                           "ERR AB0B: Different number of ports in an upward port group on switches:\n"
                           "       GUID 0x%016" PRIx64 ", LID 0x%x, Index %s - %u ports\n"
                           "       GUID 0x%016" PRIx64 ", LID 0x%x, Index %s - %u ports\n",
                           cl_ntoh64(osm_node_get_node_guid(reference_sw_arr[p_sw->rank]->p_osm_sw->p_node)),
                           cl_ntoh16(reference_sw_arr[p_sw->rank]->base_lid),
                           __osm_ftree_tuple_to_str(reference_sw_arr[p_sw->rank]->tuple),
                           cl_ptr_vector_get_size(&p_ref_group->ports),
                           cl_ntoh64(osm_node_get_node_guid(p_sw->p_osm_sw->p_node)),
                           cl_ntoh16(p_sw->base_lid),
                           __osm_ftree_tuple_to_str(p_sw->tuple),
                           cl_ptr_vector_get_size(&p_group->ports));
                   res = FALSE;
                   break;
                }
            }
         }
         if ( reference_sw_arr[p_sw->rank]->down_port_groups_num != 0 &&
              p_sw->rank != (tree_rank - 1) )
         {
            /* we're allowing some hca's to be missing */
            p_ref_group = reference_sw_arr[p_sw->rank]->down_port_groups[0];
            for (i = 0; i < p_sw->down_port_groups_num; i++)
            {
                p_group = p_sw->down_port_groups[0];
                if (cl_ptr_vector_get_size(&p_ref_group->ports) != cl_ptr_vector_get_size(&p_group->ports))
                {
                   osm_log(&p_ftree->p_osm->log, OSM_LOG_ERROR,
                           "__osm_ftree_fabric_validate_topology: "
                           "ERR AB0C: Different number of ports in an downward port group on switches:\n"
                           "       GUID 0x%016" PRIx64 ", LID 0x%x, Index %s - %u ports\n"
                           "       GUID 0x%016" PRIx64 ", LID 0x%x, Index %s - %u ports\n",
                           cl_ntoh64(osm_node_get_node_guid(reference_sw_arr[p_sw->rank]->p_osm_sw->p_node)),
                           cl_ntoh16(reference_sw_arr[p_sw->rank]->base_lid),
                           __osm_ftree_tuple_to_str(reference_sw_arr[p_sw->rank]->tuple),
                           cl_ptr_vector_get_size(&p_ref_group->ports),
                           cl_ntoh64(osm_node_get_node_guid(p_sw->p_osm_sw->p_node)),
                           cl_ntoh16(p_sw->base_lid),
                           __osm_ftree_tuple_to_str(p_sw->tuple),
                           cl_ptr_vector_get_size(&p_group->ports));
                   res = FALSE;
                   break;
                }
            }
         }
      } /* end of else */
   } /* end of while */

   if (res == TRUE)
      osm_log(&p_ftree->p_osm->log, OSM_LOG_VERBOSE,
              "__osm_ftree_fabric_validate_topology: "
              "Fabric topology has been identified as FatTree\n");
   else
      osm_log(&p_ftree->p_osm->log, OSM_LOG_ERROR,
              "__osm_ftree_fabric_validate_topology: "
              "ERR AB0D: Fabric topology hasn't been identified as FatTree\n");

   free(reference_sw_arr);
   OSM_LOG_EXIT(&p_ftree->p_osm->log);
   return res;
} /* __osm_ftree_fabric_validate_topology() */

/***************************************************
 ***************************************************/

static void
__osm_ftree_set_sw_fwd_table(
   IN  cl_map_item_t* const p_map_item, 
   IN  void *context)
{
   ftree_sw_t * p_sw = (ftree_sw_t * const) p_map_item;
   ftree_fabric_t * p_ftree = (ftree_fabric_t *)context;

   /* calculate lft length rounded up to a multiple of 64 (block length) */ 
   uint16_t lft_len = 64 * ((p_ftree->lft_max_lid_ho + 1 + 63) / 64);

   p_sw->p_osm_sw->max_lid_ho = p_ftree->lft_max_lid_ho;

   memcpy(p_ftree->p_osm->sm.ucast_mgr.lft_buf, 
          p_sw->lft_buf, 
          lft_len);
   osm_ucast_mgr_set_fwd_table(&p_ftree->p_osm->sm.ucast_mgr, p_sw->p_osm_sw);
}

/***************************************************
 ***************************************************/

/*  
 * Function: assign-up-going-port-by-descending-down
 * Given   : a switch and a LID
 * Pseudo code: 
 *    foreach down-going-port-group (in indexing order)
 *        skip this group if the LFT(LID) port is part of this group
 *        find the least loaded port of the group (scan in indexing order)
 *        r-port is the remote port connected to it
 *        assign the remote switch node LFT(LID) to r-port
 *        increase r-port usage counter
 *        assign-up-going-port-by-descending-down to r-port node (recursion)
 */

static void
__osm_ftree_fabric_route_upgoing_by_going_down(
   IN  ftree_fabric_t * p_ftree,
   IN  ftree_sw_t     * p_sw,
   IN  ftree_sw_t     * p_prev_sw,
   IN  ib_net16_t       target_lid,
   IN  uint8_t          target_rank,
   IN  boolean_t        is_real_lid,
   IN  boolean_t        is_main_path,
   IN  uint8_t          highest_rank_in_route)
{
   ftree_sw_t          * p_remote_sw;
   uint16_t              ports_num;
   ftree_port_group_t  * p_group;
   ftree_port_t        * p_port;
   ftree_port_t        * p_min_port;
   uint16_t              i;
   uint16_t              j;

   /* we shouldn't enter here if both real_lid and main_path are false */
   CL_ASSERT(is_real_lid || is_main_path);

   /* can't be here for leaf switch, */
   CL_ASSERT(p_sw->rank != (__osm_ftree_fabric_get_rank(p_ftree) - 1));

   /* if there is no down-going ports */
   if (p_sw->down_port_groups_num == 0) 
       return;

   /* foreach down-going port group (in indexing order) */
   for (i = 0; i < p_sw->down_port_groups_num; i++)
   {
      p_group = p_sw->down_port_groups[i];

      if ( p_prev_sw && (p_group->remote_base_lid == p_prev_sw->base_lid) ) 
      {
         /* This port group has a port that was used when we entered this switch,
            which means that the current group points to the switch where we were
            at the previous step of the algorithm (before going up).
            Skipping this group. */
            continue;
      }

      /* find the least loaded port of the group (in indexing order) */
      p_min_port = NULL;
      ports_num = (uint16_t)cl_ptr_vector_get_size(&p_group->ports);
      /* ToDo: no need to select a least loaded port for non-main path.
         Think about optimization. */
      for (j = 0; j < ports_num; j++) 
      {
          cl_ptr_vector_at(&p_group->ports, j, (void **)&p_port);
          if (!p_min_port)
          {
             /* first port that we're checking - set as port with the lowest load */
             p_min_port = p_port;
          }
          else if (p_port->counter_up < p_min_port->counter_up)
          {
             /* this port is less loaded - use it as min */
             p_min_port = p_port;
          }
      }
      /* At this point we have selected a port in this group with the 
         lowest load of upgoing routes.
         Set on the remote switch how to get to the target_lid -
         set LFT(target_lid) on the remote switch to the remote port */
      p_remote_sw = p_group->remote_hca_or_sw.remote_sw;

      if ( osm_switch_get_least_hops(p_remote_sw->p_osm_sw, 
                                     cl_ntoh16(target_lid)) != OSM_NO_PATH )
      {
         /* Loop in the fabric - we already routed the remote switch 
            on our way UP, and now we see it again on our way DOWN */
         osm_log(&p_ftree->p_osm->log, OSM_LOG_DEBUG,
                 "__osm_ftree_fabric_route_upgoing_by_going_down: "
                 "Loop of lenght %d in the fabric:\n                             "
                 "Switch %s (LID 0x%x) closes loop through switch %s (LID 0x%x)\n",
                 (p_remote_sw->rank - highest_rank_in_route) * 2,
                 __osm_ftree_tuple_to_str(p_remote_sw->tuple),
                 cl_ntoh16(p_group->base_lid),
                 __osm_ftree_tuple_to_str(p_sw->tuple),
                 cl_ntoh16(p_group->remote_base_lid));
         continue;
      }

      /* Four possible cases:
       *
       *  1. is_real_lid == TRUE && is_main_path == TRUE: 
       *      - going DOWN(TRUE,TRUE) through ALL the groups
       *         + promoting port counter
       *         + setting path in remote switch fwd tbl
       *         + setting hops in remote switch on all the ports of each group
       *      
       *  2. is_real_lid == TRUE && is_main_path == FALSE: 
       *      - going DOWN(TRUE,FALSE) through ALL the groups but only if
       *        the remote (upper) switch hasn't been already configured 
       *        for this target LID
       *         + NOT promoting port counter
       *         + setting path in remote switch fwd tbl if it hasn't been set yet
       *         + setting hops in remote switch on all the ports of each group
       *           if it hasn't been set yet
       *
       *  3. is_real_lid == FALSE && is_main_path == TRUE: 
       *      - going DOWN(FALSE,TRUE) through ALL the groups
       *         + promoting port counter
       *         + NOT setting path in remote switch fwd tbl
       *         + NOT setting hops in remote switch
       *
       *  4. is_real_lid == FALSE && is_main_path == FALSE: 
       *      - illegal state - we shouldn't get here
       */

      /* second case: skip the port group if the remote (upper)
         switch has been already configured for this target LID */
      if ( is_real_lid && !is_main_path &&
           __osm_ftree_sw_get_fwd_table_block(p_remote_sw,
                                              cl_ntoh16(target_lid)) != OSM_NO_PATH )
            continue;

      /* setting fwd tbl port only if this is real LID */
      if (is_real_lid)
      {
         __osm_ftree_sw_set_fwd_table_block(p_remote_sw,
                                            cl_ntoh16(target_lid),
                                            p_min_port->remote_port_num);
         osm_log(&p_ftree->p_osm->log, OSM_LOG_DEBUG,
                 "__osm_ftree_fabric_route_upgoing_by_going_down: "
                 "Switch %s: set path to HCA LID 0x%x through port %u\n",
                 __osm_ftree_tuple_to_str(p_remote_sw->tuple),
                 cl_ntoh16(target_lid),
                 p_min_port->remote_port_num);

         /* On the remote switch that is pointed by the p_group,
            set hops for ALL the ports in the remote group. */

         for (j = 0; j < ports_num; j++)
         {
            cl_ptr_vector_at(&p_group->ports, j, (void **)&p_port);

            __osm_ftree_sw_set_hops(p_remote_sw,
                                    p_ftree->lft_max_lid_ho,
                                    cl_ntoh16(target_lid),
                                    p_port->remote_port_num,
                                    ( (target_rank - highest_rank_in_route) +
                                      (p_remote_sw->rank - highest_rank_in_route) ));
         }


      }
   
      /* The number of upgoing routes is tracked in the 
         p_port->counter_up counter of the port that belongs to
         the upper side of the link (on switch with lower rank).
         Counter is promoted only if we're routing LID on the main
         path (whether it's a real LID or a dummy one). */
      if (is_main_path)
         p_min_port->counter_up++;

      /* Recursion step:
         Assign upgoing ports by stepping down, starting on REMOTE switch.
         Recursion stop condition - if the REMOTE switch is a leaf switch. */
      if (p_remote_sw->rank != (__osm_ftree_fabric_get_rank(p_ftree) - 1))
      {
         __osm_ftree_fabric_route_upgoing_by_going_down(
               p_ftree,
               p_remote_sw,   /* remote switch - used as a route-upgoing alg. start point */
               NULL,          /* prev. position - NULL to mark that we went down and not up */
               target_lid,    /* LID that we're routing to */
               target_rank,   /* rank of the LID that we're routing to */
               is_real_lid,   /* whether the target LID is real or dummy */
               is_main_path,  /* whether this is path to HCA that should by tracked by counters */
               highest_rank_in_route); /* highest visited point in the tree before going down */
      }
   }
   /* done scanning all the down-going port groups */

} /* __osm_ftree_fabric_route_upgoing_by_going_down() */

/***************************************************/

/*  
 * Function: assign-down-going-port-by-descending-up
 * Given   : a switch and a LID
 * Pseudo code: 
 *    find the least loaded port of all the upgoing groups (scan in indexing order)
 *    assign the LFT(LID) of remote switch to that port
 *    track that port usage
 *    assign-up-going-port-by-descending-down on CURRENT switch
 *    assign-down-going-port-by-descending-up on REMOTE switch (recursion)
 */

static void
__osm_ftree_fabric_route_downgoing_by_going_up(
   IN  ftree_fabric_t * p_ftree,
   IN  ftree_sw_t     * p_sw,
   IN  ftree_sw_t     * p_prev_sw,
   IN  ib_net16_t       target_lid,
   IN  uint8_t          target_rank,
   IN  boolean_t        is_real_lid,
   IN  boolean_t        is_main_path)
{
   ftree_sw_t          * p_remote_sw;
   uint16_t              ports_num;
   ftree_port_group_t  * p_group;
   ftree_port_t        * p_port;
   ftree_port_group_t  * p_min_group;
   ftree_port_t        * p_min_port;
   uint16_t              i;
   uint16_t              j;

   /* we shouldn't enter here if both real_lid and main_path are false */
   CL_ASSERT(is_real_lid || is_main_path);

   /* If this switch isn't a leaf switch:
      Assign upgoing ports by stepping down, starting on THIS switch. */
   if (p_sw->rank != (__osm_ftree_fabric_get_rank(p_ftree) - 1))
   {
      __osm_ftree_fabric_route_upgoing_by_going_down(
         p_ftree,
         p_sw,          /* local switch - used as a route-upgoing alg. start point */
         p_prev_sw,     /* switch that we went up from (NULL means that we went down) */
         target_lid,    /* LID that we're routing to */
         target_rank,   /* rank of the LID that we're routing to */
         is_real_lid,   /* whether this target LID is real or dummy */
         is_main_path,  /* whether this path to HCA should by tracked by counters */
         p_sw->rank);   /* the highest visited point in the tree before going down */
   }

   /* recursion stop condition - if it's a root switch, */
   if (p_sw->rank == 0)
      return;

   /* Find the least loaded port of all the upgoing port groups
      (in indexing order of the remote switches). */
   p_min_group = NULL;
   p_min_port = NULL;
   for (i = 0; i < p_sw->up_port_groups_num; i++)
   {
      p_group = p_sw->up_port_groups[i];

      ports_num = (uint16_t)cl_ptr_vector_get_size(&p_group->ports);
      for (j = 0; j < ports_num; j++)
      {
         cl_ptr_vector_at(&p_group->ports, j, (void **)&p_port);
         if (!p_min_group)
         {
            /* first port that we're checking - use
               it as a port with the lowest load */
            p_min_group = p_group;
            p_min_port = p_port;
         }
         else
         { 
            if ( p_port->counter_down < p_min_port->counter_down  )
            {
               /* this port is less loaded - use it as min */
               p_min_group = p_group;
               p_min_port = p_port;
            }
         }
      }
   }

   /* At this point we have selected a group and port with the 
      lowest load of downgoing routes.
      Set on the remote switch how to get to the target_lid -
      set LFT(target_lid) on the remote switch to the remote port */
   p_remote_sw = p_min_group->remote_hca_or_sw.remote_sw;

   /* Four possible cases:
    *
    *  1. is_real_lid == TRUE && is_main_path == TRUE: 
    *      - going UP(TRUE,TRUE) on selected min_group and min_port
    *         + promoting port counter
    *         + setting path in remote switch fwd tbl
    *         + setting hops in remote switch on all the ports of selected group
    *      - going UP(TRUE,FALSE) on rest of the groups, each time on port 0
    *         + NOT promoting port counter
    *         + setting path in remote switch fwd tbl if it hasn't been set yet
    *         + setting hops in remote switch on all the ports of each group
    *           if it hasn't been set yet
    *      
    *  2. is_real_lid == TRUE && is_main_path == FALSE: 
    *      - going UP(TRUE,FALSE) on ALL the groups, each time on port 0,
    *        but only if the remote (upper) switch hasn't been already 
    *        configured for this target LID
    *         + NOT promoting port counter
    *         + setting path in remote switch fwd tbl if it hasn't been set yet
    *         + setting hops in remote switch on all the ports of each group
    *           if it hasn't been set yet
    *
    *  3. is_real_lid == FALSE && is_main_path == TRUE: 
    *      - going UP(FALSE,TRUE) ONLY on selected min_group and min_port
    *         + promoting port counter
    *         + NOT setting path in remote switch fwd tbl
    *         + NOT setting hops in remote switch
    *
    *  4. is_real_lid == FALSE && is_main_path == FALSE: 
    *      - illegal state - we shouldn't get here
    */

   /* covering first half of case 1, and case 3 */
   if (is_main_path)
   {
      if (p_sw->rank == (__osm_ftree_fabric_get_rank(p_ftree) - 1))
      {
         osm_log(&p_ftree->p_osm->log, OSM_LOG_DEBUG,
                 "__osm_ftree_fabric_route_downgoing_by_going_up: "
                 " - Routing MAIN path for %s HCA LID 0x%x: %s --> %s\n",
                 (is_real_lid)? "real" : "DUMMY",
                 cl_ntoh16(target_lid),
                 __osm_ftree_tuple_to_str(p_sw->tuple),
                 __osm_ftree_tuple_to_str(p_remote_sw->tuple));
      }
      /* The number of downgoing routes is tracked in the 
         p_port->counter_down counter of the port that belongs to
         the lower side of the link (on switch with higher rank) */
      p_min_port->counter_down++;
      if (is_real_lid)
      {
         __osm_ftree_sw_set_fwd_table_block(p_remote_sw,
                                            cl_ntoh16(target_lid),
                                            p_min_port->remote_port_num);
         osm_log(&p_ftree->p_osm->log, OSM_LOG_DEBUG,
                 "__osm_ftree_fabric_route_downgoing_by_going_up: "
                 "Switch %s: set path to HCA LID 0x%x through port %u\n",
                 __osm_ftree_tuple_to_str(p_remote_sw->tuple),
                 cl_ntoh16(target_lid),p_min_port->remote_port_num);

         /* On the remote switch that is pointed by the min_group,
            set hops for ALL the ports in the remote group. */

         ports_num = (uint16_t)cl_ptr_vector_get_size(&p_min_group->ports);
         for (j = 0; j < ports_num; j++)
         {
            cl_ptr_vector_at(&p_min_group->ports, j, (void **)&p_port);
            __osm_ftree_sw_set_hops(p_remote_sw,
                                    p_ftree->lft_max_lid_ho,
                                    cl_ntoh16(target_lid),
                                    p_port->remote_port_num,
                                    target_rank - p_remote_sw->rank);
         }
      }

      /* Recursion step: 
         Assign downgoing ports by stepping up, starting on REMOTE switch. */
      __osm_ftree_fabric_route_downgoing_by_going_up(
            p_ftree,
            p_remote_sw,    /* remote switch - used as a route-downgoing alg. next step point */
            p_sw,           /* this switch - prev. position switch for the function */
            target_lid,     /* LID that we're routing to */
            target_rank,    /* rank of the LID that we're routing to */
            is_real_lid,    /* whether this target LID is real or dummy */
            is_main_path);  /* whether this is path to HCA that should by tracked by counters */
   }

   /* we're done for the third case */
   if (!is_real_lid)
      return;

   /* What's left to do at this point:
    *
    *  1. is_real_lid == TRUE && is_main_path == TRUE: 
    *      - going UP(TRUE,FALSE) on rest of the groups, each time on port 0, 
    *        but only if the remote (upper) switch hasn't been already 
    *        configured for this target LID
    *         + NOT promoting port counter
    *         + setting path in remote switch fwd tbl if it hasn't been set yet
    *         + setting hops in remote switch on all the ports of each group
    *           if it hasn't been set yet
    *      
    *  2. is_real_lid == TRUE && is_main_path == FALSE: 
    *      - going UP(TRUE,FALSE) on ALL the groups, each time on port 0,
    *        but only if the remote (upper) switch hasn't been already 
    *        configured for this target LID
    *         + NOT promoting port counter
    *         + setting path in remote switch fwd tbl if it hasn't been set yet
    *         + setting hops in remote switch on all the ports of each group
    *           if it hasn't been set yet
    *
    *  These two rules can be rephrased this way:
    *   - foreach UP port group
    *      + if remote switch has been set with the target LID
    *         - skip this port group
    *      + else
    *         - select port 0
    *         - do NOT promote port counter
    *         - set path in remote switch fwd tbl
    *         - set hops in remote switch on all the ports of this group
    *         - go UP(TRUE,FALSE) to the remote switch
    */

   for (i = 0; i < p_sw->up_port_groups_num; i++)
   {
      p_group = p_sw->up_port_groups[i];
      p_remote_sw = p_group->remote_hca_or_sw.remote_sw;

      /* skip if target lid has been already set on remote switch fwd tbl */
      if (__osm_ftree_sw_get_fwd_table_block(
                  p_remote_sw,cl_ntoh16(target_lid)) != OSM_NO_PATH)
         continue;

      if (p_sw->rank == (__osm_ftree_fabric_get_rank(p_ftree) - 1))
      {
         osm_log(&p_ftree->p_osm->log, OSM_LOG_DEBUG,
                 "__osm_ftree_fabric_route_downgoing_by_going_up: "
                 " - Routing SECONDARY path for LID 0x%x: %s --> %s\n",
                cl_ntoh16(target_lid),
                __osm_ftree_tuple_to_str(p_sw->tuple),
                __osm_ftree_tuple_to_str(p_remote_sw->tuple));
      }
    
      cl_ptr_vector_at(&p_group->ports, 0, (void **)&p_port);
      __osm_ftree_sw_set_fwd_table_block(p_remote_sw,
                                         cl_ntoh16(target_lid),
                                         p_port->remote_port_num);

      /* On the remote switch that is pointed by the p_group,
         set hops for ALL the ports in the remote group. */

      ports_num = (uint16_t)cl_ptr_vector_get_size(&p_group->ports);
      for (j = 0; j < ports_num; j++)
      {
         cl_ptr_vector_at(&p_group->ports, j, (void **)&p_port);

         __osm_ftree_sw_set_hops(p_remote_sw,
                                 p_ftree->lft_max_lid_ho,
                                 cl_ntoh16(target_lid),
                                 p_port->remote_port_num,
                                 target_rank - p_remote_sw->rank);
      }

      /* Recursion step: 
         Assign downgoing ports by stepping up, starting on REMOTE switch. */
      __osm_ftree_fabric_route_downgoing_by_going_up(
            p_ftree,
            p_remote_sw, /* remote switch - used as a route-downgoing alg. next step point */
            p_sw,        /* this switch - prev. position switch for the function */
            target_lid,  /* LID that we're routing to */
            target_rank, /* rank of the LID that we're routing to */
            TRUE,        /* whether the target LID is real or dummy */
            FALSE);      /* whether this is path to HCA that should by tracked by counters */
   }

} /* ftree_fabric_route_downgoing_by_going_up() */

/***************************************************/

/*  
 * Pseudo code: 
 *    foreach leaf switch (in indexing order)
 *       for each compute node (in indexing order)
 *          obtain the LID of the compute node
 *          set local LFT(LID) of the port connecting to compute node
 *          call assign-down-going-port-by-descending-up(TRUE,TRUE) on CURRENT switch
 *       for each MISSING compute node
 *          call assign-down-going-port-by-descending-up(FALSE,TRUE) on CURRENT switch
 */

static void
__osm_ftree_fabric_route_to_hcas(
   IN  ftree_fabric_t * p_ftree)
{
   ftree_sw_t         * p_sw;
   ftree_port_group_t * p_group;
   ftree_port_t       * p_port;
   uint32_t             i;
   uint32_t             j;
   ib_net16_t           remote_lid;

   OSM_LOG_ENTER(&p_ftree->p_osm->log, __osm_ftree_fabric_route_to_hcas);

   /* for each leaf switch (in indexing order) */
   for(i = 0; i < p_ftree->leaf_switches_num; i++)
   {
      p_sw = p_ftree->leaf_switches[i];

      /* for each HCA connected to this switch */
      for (j = 0; j < p_sw->down_port_groups_num; j++)
      {
         /* obtain the LID of HCA port */
         p_group = p_sw->down_port_groups[j];
         remote_lid = p_group->remote_base_lid;

         /* set local LFT(LID) to the port that is connected to HCA */
         cl_ptr_vector_at(&p_group->ports, 0, (void **)&p_port);
         __osm_ftree_sw_set_fwd_table_block(p_sw,
                                            cl_ntoh16(remote_lid),
                                            p_port->port_num);
         osm_log(&p_ftree->p_osm->log, OSM_LOG_DEBUG,
                 "__osm_ftree_fabric_route_to_hcas: "
                 "Switch %s: set path to HCA LID 0x%x through port %u\n",
                 __osm_ftree_tuple_to_str(p_sw->tuple),
                 cl_ntoh16(remote_lid),
                 p_port->port_num);

         /* set local min hop table(LID) to route to the CA */
         __osm_ftree_sw_set_hops(p_sw,
                                 p_ftree->lft_max_lid_ho,
                                 cl_ntoh16(remote_lid),
                                 p_port->port_num,
                                 1);

         /* assign downgoing ports by stepping up */
         __osm_ftree_fabric_route_downgoing_by_going_up(
               p_ftree,
               p_sw,       /* local switch - used as a route-downgoing alg. start point */
               NULL,       /* prev. position switch */
               remote_lid, /* LID that we're routing to */
               __osm_ftree_fabric_get_rank(p_ftree), /* rank of the LID that we're routing to */
               TRUE,       /* whether this HCA LID is real or dummy */
               TRUE);      /* whether this path to HCA should by tracked by counters */
      }

      /* We're done with the real HCAs. Now route the dummy HCAs that are missing.
         When routing to dummy HCAs we don't fill lid matrices. */

      if (p_ftree->max_hcas_per_leaf > p_sw->down_port_groups_num)
      {
         osm_log(&p_ftree->p_osm->log, OSM_LOG_DEBUG,"__osm_ftree_fabric_route_to_hcas: "
                 "Routing %u dummy HCAs\n",
                 p_ftree->max_hcas_per_leaf - p_sw->down_port_groups_num);
         for ( j = 0;
               ((int)j) < (p_ftree->max_hcas_per_leaf - p_sw->down_port_groups_num);
               j++)
         {
            /* assign downgoing ports by stepping up */
            __osm_ftree_fabric_route_downgoing_by_going_up(
                  p_ftree,
                  p_sw,    /* local switch - used as a route-downgoing alg. start point */
                  NULL,    /* prev. position switch */
                  0,       /* LID that we're routing to - ignored for dummy HCA */
                  0,       /* rank of the LID that we're routing to - ignored for dummy HCA */
                  FALSE,   /* whether this HCA LID is real or dummy */
                  TRUE);   /* whether this path to HCA should by tracked by counters */
         }
      }
   }
   /* done going through all the leaf switches */
   OSM_LOG_EXIT(&p_ftree->p_osm->log);
} /* __osm_ftree_fabric_route_to_hcas() */

/***************************************************/

/*  
 * Pseudo code: 
 *    foreach switch in fabric
 *       obtain its LID
 *       set local LFT(LID) to port 0
 *       call assign-down-going-port-by-descending-up(TRUE,FALSE) on CURRENT switch
 *
 * Routing to switch is similar to routing a REAL hca lid on SECONDARY path:
 *   - we should set fwd tables
 *   - we should NOT update port counters
 */

static void
__osm_ftree_fabric_route_to_switches(
   IN  ftree_fabric_t * p_ftree)
{
   ftree_sw_t         * p_sw;
   ftree_sw_t         * p_next_sw;

   OSM_LOG_ENTER(&p_ftree->p_osm->log, __osm_ftree_fabric_route_to_switches);

   p_next_sw = (ftree_sw_t *)cl_qmap_head(&p_ftree->sw_tbl);
   while( p_next_sw != (ftree_sw_t *)cl_qmap_end(&p_ftree->sw_tbl) )
   {
      p_sw = p_next_sw;
      p_next_sw = (ftree_sw_t *)cl_qmap_next(&p_sw->map_item );

      /* set local LFT(LID) to 0 (route to itself) */
      __osm_ftree_sw_set_fwd_table_block(p_sw,
                                         cl_ntoh16(p_sw->base_lid),
                                         0);

      osm_log(&p_ftree->p_osm->log, OSM_LOG_DEBUG,
              "__osm_ftree_fabric_route_to_switches: "
              "Switch %s (LID 0x%x): routing switch-to-switch pathes\n",
              __osm_ftree_tuple_to_str(p_sw->tuple),
              cl_ntoh16(p_sw->base_lid));

      /* set min hop table of the switch to itself */
      __osm_ftree_sw_set_hops(p_sw,
                              p_ftree->lft_max_lid_ho,
                              cl_ntoh16(p_sw->base_lid),
                              0, /* port_num */
                              0);/* hops     */

      __osm_ftree_fabric_route_downgoing_by_going_up(
            p_ftree,
            p_sw,           /* local switch - used as a route-downgoing alg. start point */
            NULL,           /* prev. position switch */
            p_sw->base_lid, /* LID that we're routing to */
            p_sw->rank,     /* rank of the LID that we're routing to */
            TRUE,           /* whether the target LID is a real or dummy */
            FALSE);         /* whether this path should by tracked by counters */
   }

   OSM_LOG_EXIT(&p_ftree->p_osm->log);
} /* __osm_ftree_fabric_route_to_switches() */

/***************************************************
 ***************************************************/

static int 
__osm_ftree_fabric_populate_switches(
   IN  ftree_fabric_t * p_ftree)
{
   osm_switch_t * p_osm_sw;
   osm_switch_t * p_next_osm_sw;

   OSM_LOG_ENTER(&p_ftree->p_osm->log, __osm_ftree_fabric_populate_switches);

   p_next_osm_sw = (osm_switch_t *)cl_qmap_head(&p_ftree->p_osm->subn.sw_guid_tbl);
   while( p_next_osm_sw != (osm_switch_t *)cl_qmap_end(&p_ftree->p_osm->subn.sw_guid_tbl) )
   {
      p_osm_sw = p_next_osm_sw;
      p_next_osm_sw = (osm_switch_t *)cl_qmap_next(&p_osm_sw->map_item );
      __osm_ftree_fabric_add_sw(p_ftree,p_osm_sw);
   }
   OSM_LOG_EXIT(&p_ftree->p_osm->log);
   return 0;
} /* __osm_ftree_fabric_populate_switches() */

/***************************************************
 ***************************************************/

static int 
__osm_ftree_fabric_populate_hcas(
   IN  ftree_fabric_t * p_ftree)
{
   osm_node_t   * p_osm_node;
   osm_node_t   * p_next_osm_node;

   OSM_LOG_ENTER(&p_ftree->p_osm->log, __osm_ftree_fabric_populate_hcas);

   p_next_osm_node = (osm_node_t *)cl_qmap_head(&p_ftree->p_osm->subn.node_guid_tbl);
   while( p_next_osm_node != (osm_node_t *)cl_qmap_end(&p_ftree->p_osm->subn.node_guid_tbl) )
   {
      p_osm_node = p_next_osm_node;
      p_next_osm_node = (osm_node_t *)cl_qmap_next(&p_osm_node->map_item);
      switch (osm_node_get_type(p_osm_node))
      {
         case IB_NODE_TYPE_CA:
            __osm_ftree_fabric_add_hca(p_ftree,p_osm_node);
            break;
         case IB_NODE_TYPE_ROUTER:
            break;
         case IB_NODE_TYPE_SWITCH:
            /* all the switches added separately */
            break;
         default:
            osm_log(&p_ftree->p_osm->log, OSM_LOG_ERROR,
                    "__osm_ftree_fabric_populate_hcas: ERR AB0E: "
                    "Node GUID 0x%016" PRIx64 " - Unknown node type: %s\n",
                    cl_ntoh64(osm_node_get_node_guid(p_osm_node)),
                    ib_get_node_type_str(osm_node_get_type(p_osm_node)));
            OSM_LOG_EXIT(&p_ftree->p_osm->log);
            return -1;
      }
   }

   OSM_LOG_EXIT(&p_ftree->p_osm->log);
   return 0;
} /* __osm_ftree_fabric_populate_hcas() */

/***************************************************
 ***************************************************/

static void
__osm_ftree_rank_from_switch(
   IN  ftree_fabric_t * p_ftree, 
   IN  ftree_sw_t *     p_starting_sw)
{
   ftree_sw_t   * p_sw;
   ftree_sw_t   * p_remote_sw;
   osm_node_t   * p_node;
   osm_node_t   * p_remote_node;
   osm_physp_t  * p_osm_port;
   uint8_t        i;
   cl_list_t      bfs_list;
   ftree_sw_tbl_element_t * p_sw_tbl_element = NULL;

   p_starting_sw->rank = 0;

   /* Run BFS scan of the tree, starting from this switch */

   cl_list_init(&bfs_list, cl_qmap_count(&p_ftree->sw_tbl));
   cl_list_insert_tail(&bfs_list, &__osm_ftree_sw_tbl_element_create(p_starting_sw)->map_item);

   while (!cl_is_list_empty(&bfs_list))
   {
      p_sw_tbl_element = (ftree_sw_tbl_element_t *)cl_list_remove_head(&bfs_list);
      p_sw = p_sw_tbl_element->p_sw;
      __osm_ftree_sw_tbl_element_destroy(p_sw_tbl_element);

      p_node = p_sw->p_osm_sw->p_node;

      /* note: skipping port 0 on switches */
      for (i = 1; i < osm_node_get_num_physp(p_node); i++)
      {
         p_osm_port = osm_node_get_physp_ptr(p_node,i);
         if (!osm_physp_is_valid(p_osm_port)) 
            continue;
         if (!osm_link_is_healthy(p_osm_port)) 
            continue;

         p_remote_node = osm_node_get_remote_node(p_node,i,NULL);
         if (!p_remote_node)
            continue;
         if (osm_node_get_type(p_remote_node) != IB_NODE_TYPE_SWITCH)
            continue;

         p_remote_sw = (ftree_sw_t *)cl_qmap_get(&p_ftree->sw_tbl,
                                                 osm_node_get_node_guid(p_remote_node));
         if (p_remote_sw == (ftree_sw_t *)cl_qmap_end(&p_ftree->sw_tbl))
         {
            /* remote node is not a switch */
            continue;
         }
         if (__osm_ftree_sw_ranked(p_remote_sw) && p_remote_sw->rank <= (p_sw->rank + 1))
            continue;

         /* rank the remote switch and add it to the BFS list */
         p_remote_sw->rank = p_sw->rank + 1;
         cl_list_insert_tail(&bfs_list, 
                             &__osm_ftree_sw_tbl_element_create(p_remote_sw)->map_item);
      }
   }
} /* __osm_ftree_rank_from_switch() */


/***************************************************
 ***************************************************/

static int 
__osm_ftree_rank_switches_from_hca(
   IN  ftree_fabric_t * p_ftree,
   IN  ftree_hca_t    * p_hca)
{
   ftree_sw_t     * p_sw;
   osm_node_t     * p_osm_node = p_hca->p_osm_node;
   osm_node_t     * p_remote_osm_node;
   osm_physp_t    * p_osm_port;
   static uint8_t   i = 0;
   int res = 0;

   OSM_LOG_ENTER(&p_ftree->p_osm->log, __osm_ftree_rank_switches_from_hca);

   for (i = 0; i < osm_node_get_num_physp(p_osm_node); i++)
   {
      p_osm_port = osm_node_get_physp_ptr(p_osm_node,i);
      if (!osm_physp_is_valid(p_osm_port)) 
         continue;
      if (!osm_link_is_healthy(p_osm_port)) 
         continue;

      p_remote_osm_node = osm_node_get_remote_node(p_osm_node,i,NULL);

      switch (osm_node_get_type(p_remote_osm_node))
      {
         case IB_NODE_TYPE_CA:
            /* HCA connected directly to another HCA - not FatTree */
            osm_log(&p_ftree->p_osm->log, OSM_LOG_ERROR,
                    "__osm_ftree_rank_switches_from_hca: ERR AB0F: "
                    "HCA conected directly to another HCA: "
                    "0x%016" PRIx64 " <---> 0x%016" PRIx64 "\n",
                    cl_ntoh64(osm_node_get_node_guid(p_hca->p_osm_node)),
                    cl_ntoh64(osm_node_get_node_guid(p_remote_osm_node)));
            res = -1;
            goto Exit;

         case IB_NODE_TYPE_ROUTER:
            /* leaving this port - proceeding to the next one */
            continue;

         case IB_NODE_TYPE_SWITCH:
            /* continue with this port */
            break;

         default:
            osm_log(&p_ftree->p_osm->log, OSM_LOG_ERROR,
                    "__osm_ftree_rank_switches_from_hca: ERR AB10: "
                    "Node GUID 0x%016" PRIx64 " - Unknown node type: %s\n",
                    cl_ntoh64(osm_node_get_node_guid(p_remote_osm_node)),
                    ib_get_node_type_str(osm_node_get_type(p_remote_osm_node)));
            res = -1;
            goto Exit;
      }

      /* remote node is switch */

      p_sw = (ftree_sw_t *)cl_qmap_get(&p_ftree->sw_tbl,
                                       p_osm_port->p_remote_physp->p_node->node_info.node_guid);

      CL_ASSERT(p_sw != (ftree_sw_t *)cl_qmap_end(&p_ftree->sw_tbl));

      if (__osm_ftree_sw_ranked(p_sw) && p_sw->rank == 0)
         continue;

      osm_log(&p_ftree->p_osm->log, OSM_LOG_DEBUG,
              "__osm_ftree_rank_switches_from_hca: "
              "Marking rank of switch that is directly connected to HCA:\n"
              "                                            - HCA guid   : 0x%016" PRIx64 "\n"
              "                                            - Switch guid: 0x%016" PRIx64 "\n"
              "                                            - Switch LID : 0x%x\n",
              cl_ntoh64(osm_node_get_node_guid(p_hca->p_osm_node)),
              cl_ntoh64(osm_node_get_node_guid(p_sw->p_osm_sw->p_node)),
              cl_ntoh16(p_sw->base_lid));
      __osm_ftree_rank_from_switch(p_ftree, p_sw);
   }

 Exit:
   OSM_LOG_EXIT(&p_ftree->p_osm->log);
   return res;
} /* __osm_ftree_rank_switches_from_hca() */

/***************************************************/

static void 
__osm_ftree_sw_reverse_rank(
   IN  cl_map_item_t* const p_map_item, 
   IN  void *context)
{
   ftree_fabric_t * p_ftree = (ftree_fabric_t *)context;
   ftree_sw_t     * p_sw = (ftree_sw_t * const) p_map_item;
   p_sw->rank = __osm_ftree_fabric_get_rank(p_ftree) - p_sw->rank - 1;
}

/***************************************************
 ***************************************************/

static int
__osm_ftree_fabric_construct_hca_ports(
   IN  ftree_fabric_t  * p_ftree, 
   IN  ftree_hca_t     * p_hca)
{
   ftree_sw_t      * p_remote_sw;
   osm_node_t      * p_node = p_hca->p_osm_node;
   osm_node_t      * p_remote_node;
   uint8_t           remote_node_type;
   ib_net64_t        remote_node_guid;
   osm_physp_t     * p_remote_osm_port;
   uint8_t           i;
   uint8_t           remote_port_num;
   int res = 0;

   for (i = 0; i < osm_node_get_num_physp(p_node); i++)
   {
      osm_physp_t * p_osm_port = osm_node_get_physp_ptr(p_node,i);

      if (!osm_physp_is_valid(p_osm_port)) 
         continue;
      if (!osm_link_is_healthy(p_osm_port)) 
         continue;

      p_remote_osm_port = osm_physp_get_remote(p_osm_port);
      p_remote_node = osm_node_get_remote_node(p_node,i,&remote_port_num);

      if (!p_remote_osm_port)
         continue;

      remote_node_type = osm_node_get_type(p_remote_node);
      remote_node_guid = osm_node_get_node_guid(p_remote_node);

      switch (remote_node_type)
      {
         case IB_NODE_TYPE_ROUTER:
            /* leaving this port - proceeding to the next one */
            continue;

         case IB_NODE_TYPE_CA:
            /* HCA connected directly to another HCA - not FatTree */
            osm_log(&p_ftree->p_osm->log, OSM_LOG_ERROR,
                    "__osm_ftree_fabric_construct_hca_ports: ERR AB11: "
                    "HCA conected directly to another HCA: "
                    "0x%016" PRIx64 " <---> 0x%016" PRIx64 "\n",
                    cl_ntoh64(osm_node_get_node_guid(p_node)),
                    cl_ntoh64(remote_node_guid));
            res = -1;
            goto Exit;

         case IB_NODE_TYPE_SWITCH:
            /* continue with this port */
            break;

         default:
            osm_log(&p_ftree->p_osm->log, OSM_LOG_ERROR,
                    "__osm_ftree_fabric_construct_hca_ports: ERR AB12: "
                    "Node GUID 0x%016" PRIx64 " - Unknown node type: %s\n",
                    cl_ntoh64(remote_node_guid),
                    ib_get_node_type_str(remote_node_type));
            res = -1;
            goto Exit;
      }

      /* remote node is switch */

      p_remote_sw = (ftree_sw_t *)cl_qmap_get(&p_ftree->sw_tbl,remote_node_guid);
      CL_ASSERT( p_remote_sw != (ftree_sw_t *)cl_qmap_end(&p_ftree->sw_tbl) );
      CL_ASSERT( (p_remote_sw->rank + 1) == __osm_ftree_fabric_get_rank(p_ftree) );

      __osm_ftree_hca_add_port(
            p_hca,                                     /* local ftree_hca object */
            i,                                         /* local port number */
            remote_port_num,                           /* remote port number */
            osm_node_get_base_lid(p_node, i),          /* local lid */
            osm_node_get_base_lid(p_remote_node, 0),   /* remote lid */
            osm_physp_get_port_guid(p_osm_port),       /* local port guid */
            osm_physp_get_port_guid(p_remote_osm_port),/* remote port guid */
            remote_node_guid,                          /* remote node guid */
            remote_node_type,                          /* remote node type */
            (void *) p_remote_sw);                     /* remote ftree_hca/sw object */
   }

 Exit:
   return res;
} /* __osm_ftree_fabric_construct_hca_ports() */

/***************************************************
 ***************************************************/

static int 
__osm_ftree_fabric_construct_sw_ports(
   IN  ftree_fabric_t  * p_ftree, 
   IN  ftree_sw_t      * p_sw)
{
   ftree_hca_t       * p_remote_hca;
   ftree_sw_t        * p_remote_sw;
   osm_node_t        * p_node = p_sw->p_osm_sw->p_node;
   osm_node_t        * p_remote_node;
   ib_net16_t          remote_base_lid;
   uint8_t             remote_node_type;
   ib_net64_t          remote_node_guid;
   osm_physp_t       * p_remote_osm_port;
   ftree_direction_t   direction;
   void              * p_remote_hca_or_sw;
   uint8_t             i;
   uint8_t             remote_port_num;
   int res = 0;

   CL_ASSERT(osm_node_get_type(p_node) == IB_NODE_TYPE_SWITCH);

   for (i = 0; i < osm_node_get_num_physp(p_node); i++)
   {
      osm_physp_t * p_osm_port = osm_node_get_physp_ptr(p_node,i);

      if (!osm_physp_is_valid(p_osm_port)) 
         continue;
      if (!osm_link_is_healthy(p_osm_port)) 
         continue;

      p_remote_osm_port = osm_physp_get_remote(p_osm_port);
      p_remote_node = osm_node_get_remote_node(p_node,i,&remote_port_num);

      if (!p_remote_osm_port)
         continue;

      remote_node_type = osm_node_get_type(p_remote_node);
      remote_node_guid = osm_node_get_node_guid(p_remote_node);

      switch (remote_node_type)
      {
         case IB_NODE_TYPE_ROUTER:
            /* leaving this port - proceeding to the next one */
            continue;

         case IB_NODE_TYPE_CA:
            /* switch connected to hca */

            CL_ASSERT((p_sw->rank + 1) == __osm_ftree_fabric_get_rank(p_ftree));

            p_remote_hca = (ftree_hca_t *)cl_qmap_get(&p_ftree->hca_tbl,remote_node_guid);
            CL_ASSERT(p_remote_hca != (ftree_hca_t *)cl_qmap_end(&p_ftree->hca_tbl));

            p_remote_hca_or_sw = (void *)p_remote_hca;
            direction = FTREE_DIRECTION_DOWN;

            remote_base_lid = osm_physp_get_base_lid(p_remote_osm_port);
            break;

         case IB_NODE_TYPE_SWITCH:
            /* switch connected to another switch */

            p_remote_sw = (ftree_sw_t *)cl_qmap_get(&p_ftree->sw_tbl,remote_node_guid);
            CL_ASSERT(p_remote_sw != (ftree_sw_t *)cl_qmap_end(&p_ftree->sw_tbl));
            p_remote_hca_or_sw = (void *)p_remote_sw;

            if (abs(p_sw->rank - p_remote_sw->rank) != 1)
            {
               osm_log(&p_ftree->p_osm->log, OSM_LOG_ERROR,
                       "__osm_ftree_fabric_construct_sw_ports: ERR AB16: "
                       "Illegal link between switches with ranks %u and %u:\n"
                       "       GUID 0x%016" PRIx64 ", LID 0x%x, rank %u\n"
                       "       GUID 0x%016" PRIx64 ", LID 0x%x, rank %u\n",
                       p_sw->rank,
                       p_remote_sw->rank,
                       cl_ntoh64(osm_node_get_node_guid(p_sw->p_osm_sw->p_node)),
                       cl_ntoh16(p_sw->base_lid),
                       p_sw->rank,
                       cl_ntoh64(osm_node_get_node_guid(p_remote_sw->p_osm_sw->p_node)),
                       cl_ntoh16(p_remote_sw->base_lid),
                       p_remote_sw->rank);
               res = -1;
               goto Exit;
            }

            if (p_sw->rank > p_remote_sw->rank)
               direction = FTREE_DIRECTION_UP;
            else
               direction = FTREE_DIRECTION_DOWN;

            /* switch LID is only in port 0 port_info structure */
            remote_base_lid = osm_node_get_base_lid(p_remote_node, 0);

            break;

         default:
            osm_log(&p_ftree->p_osm->log, OSM_LOG_ERROR,
                    "__osm_ftree_fabric_construct_sw_ports: ERR AB13: "
                    "Node GUID 0x%016" PRIx64 " - Unknown node type: %s\n",
                    cl_ntoh64(remote_node_guid),
                    ib_get_node_type_str(remote_node_type));
            res = -1;
            goto Exit;
      }
      __osm_ftree_sw_add_port(
            p_sw,                                       /* local ftree_sw object */     
            i,                                          /* local port number */          
            remote_port_num,                            /* remote port number */         
            p_sw->base_lid,                             /* local lid */                  
            remote_base_lid,                            /* remote lid */                 
            osm_physp_get_port_guid(p_osm_port),        /* local port guid */            
            osm_physp_get_port_guid(p_remote_osm_port), /* remote port guid */           
            remote_node_guid,                           /* remote node guid */           
            remote_node_type,                           /* remote node type */           
            p_remote_hca_or_sw,                         /* remote ftree_hca/sw object */ 
            direction);                                 /* port direction (up or down) */

      /* Track the max lid (in host order) that exists in the fabric */
      if (cl_ntoh16(remote_base_lid) > p_ftree->lft_max_lid_ho)
         p_ftree->lft_max_lid_ho = cl_ntoh16(remote_base_lid);
   }

 Exit:
   return res;
} /* __osm_ftree_fabric_construct_sw_ports() */

/***************************************************
 ***************************************************/

/* ToDo: improve ranking algorithm complexity
   by propogating BFS from more nodes */ 
static int
__osm_ftree_fabric_perform_ranking(
   IN  ftree_fabric_t * p_ftree)
{
   ftree_hca_t * p_hca;
   ftree_hca_t * p_next_hca;
   int res = 0;

   OSM_LOG_ENTER(&p_ftree->p_osm->log, __osm_ftree_fabric_perform_ranking);

   /* Mark REVERSED rank of all the switches in the subnet. 
      Start from switches that are connected to hca's, and 
      scan all the switches in the subnet. */
   p_next_hca = (ftree_hca_t *)cl_qmap_head(&p_ftree->hca_tbl);
   while( p_next_hca != (ftree_hca_t *)cl_qmap_end( &p_ftree->hca_tbl ) )
   {
      p_hca = p_next_hca;
      p_next_hca = (ftree_hca_t *)cl_qmap_next(&p_hca->map_item );
      if (__osm_ftree_rank_switches_from_hca(p_ftree,p_hca) != 0)
      {
         res = -1;
         osm_log(&p_ftree->p_osm->log, OSM_LOG_ERROR,
                 "__osm_ftree_fabric_perform_ranking: ERR AB14: "
                 "Subnet ranking failed - subnet is not FatTree");
         goto Exit;
      }
   }

   /* calculate and set FatTree rank */
   __osm_ftree_fabric_calculate_rank(p_ftree);
   osm_log(&p_ftree->p_osm->log, OSM_LOG_INFO,
           "__osm_ftree_fabric_perform_ranking: "
           "FatTree rank is %u\n", __osm_ftree_fabric_get_rank(p_ftree));
   
   /* fix ranking of the switches by reversing the ranking direction */
   cl_qmap_apply_func(&p_ftree->sw_tbl, __osm_ftree_sw_reverse_rank, (void *)p_ftree);

   if ( __osm_ftree_fabric_get_rank(p_ftree) > FAT_TREE_MAX_RANK ||
        __osm_ftree_fabric_get_rank(p_ftree) < FAT_TREE_MIN_RANK )
   {
      osm_log(&p_ftree->p_osm->log, OSM_LOG_ERROR, 
              "__osm_ftree_fabric_perform_ranking: ERR AB15: "
              "Tree rank is %u (should be between %u and %u)\n",
              __osm_ftree_fabric_get_rank(p_ftree),
              FAT_TREE_MIN_RANK,
              FAT_TREE_MAX_RANK);
      res = -1;
      goto Exit;
   }

  Exit:
   OSM_LOG_EXIT(&p_ftree->p_osm->log);
   return res;
} /* __osm_ftree_fabric_perform_ranking() */

/***************************************************
 ***************************************************/

static int
__osm_ftree_fabric_populate_ports(
   IN  ftree_fabric_t * p_ftree)
{
   ftree_hca_t * p_hca;
   ftree_hca_t * p_next_hca;
   ftree_sw_t * p_sw;
   ftree_sw_t * p_next_sw;
   int res = 0;

   OSM_LOG_ENTER(&p_ftree->p_osm->log, __osm_ftree_fabric_populate_ports);

   p_next_hca = (ftree_hca_t *)cl_qmap_head(&p_ftree->hca_tbl);
   while( p_next_hca != (ftree_hca_t *)cl_qmap_end( &p_ftree->hca_tbl ) )
   {
      p_hca = p_next_hca;
      p_next_hca = (ftree_hca_t *)cl_qmap_next(&p_hca->map_item );
      if (__osm_ftree_fabric_construct_hca_ports(p_ftree,p_hca) != 0)
      {
         res = -1;
         goto Exit;
      }
   }

   p_next_sw = (ftree_sw_t *)cl_qmap_head(&p_ftree->sw_tbl);
   while( p_next_sw != (ftree_sw_t *)cl_qmap_end( &p_ftree->sw_tbl ) )
   {
      p_sw = p_next_sw;
      p_next_sw = (ftree_sw_t *)cl_qmap_next(&p_sw->map_item );
      if (__osm_ftree_fabric_construct_sw_ports(p_ftree,p_sw) != 0)
      {
         res = -1;
         goto Exit;
      }
   }
 Exit:
   OSM_LOG_EXIT(&p_ftree->p_osm->log);
   return res;
} /* __osm_ftree_fabric_populate_ports() */

/***************************************************
 ***************************************************/

static int 
__osm_ftree_construct_fabric(
   IN  void * context)
{
   ftree_fabric_t * p_ftree = context;
   int status = 0;

   OSM_LOG_ENTER(&p_ftree->p_osm->log, __osm_ftree_construct_fabric);

   if (p_ftree->p_osm->subn.opt.lmc > 0)
   {
      osm_log(&p_ftree->p_osm->log, OSM_LOG_SYS,
              "LMC > 0 is not supported by fat-tree routing.\n"
              "Falling back to default routing.\n");
      status = -1;
      goto Exit;
   }

   if ( cl_qmap_count(&p_ftree->p_osm->subn.sw_guid_tbl) < 2 )
   {
      osm_log(&p_ftree->p_osm->log, OSM_LOG_SYS,
              "Fabric has %u switches - topology is not fat-tree.\n"
              "Falling back to default routing.\n",
              cl_qmap_count(&p_ftree->p_osm->subn.sw_guid_tbl));
      status = -1;
      goto Exit;
   }

   if ( (cl_qmap_count(&p_ftree->p_osm->subn.node_guid_tbl) - 
         cl_qmap_count(&p_ftree->p_osm->subn.sw_guid_tbl)) < 2)
   {
      osm_log(&p_ftree->p_osm->log, OSM_LOG_SYS,
              "Fabric has %u nodes (%u switches) - topology is not fat-tree.\n"
              "Falling back to default routing.\n",
              cl_qmap_count(&p_ftree->p_osm->subn.node_guid_tbl),
              cl_qmap_count(&p_ftree->p_osm->subn.sw_guid_tbl));
      status = -1;
      goto Exit;
   }

   osm_log(&p_ftree->p_osm->log, OSM_LOG_VERBOSE,"__osm_ftree_construct_fabric: \n"
           "                       |----------------------------------------|\n"
           "                       |- Starting FatTree fabric construction -|\n"
           "                       |----------------------------------------|\n\n");

   osm_log(&p_ftree->p_osm->log, OSM_LOG_VERBOSE,
           "__osm_ftree_construct_fabric: "
           "Populating FatTree switch table\n");
   /* ToDo: now that the pointer from node to switch exists,  
      no need to fill the switch table in a separate loop */
   if (__osm_ftree_fabric_populate_switches(p_ftree) != 0)
   {
      osm_log(&p_ftree->p_osm->log, OSM_LOG_SYS,
              "Fabric topology is not fat-tree - "
              "falling back to default routing\n");
      status = -1;
      goto Exit;
   }

   osm_log(&p_ftree->p_osm->log, OSM_LOG_VERBOSE,
           "__osm_ftree_construct_fabric: "
           "Populating FatTree HCA table\n");
   if (__osm_ftree_fabric_populate_hcas(p_ftree) != 0)
   {
      osm_log(&p_ftree->p_osm->log, OSM_LOG_SYS,
              "Fabric topology is not fat-tree - "
              "falling back to default routing\n");
      status = -1;
      goto Exit;
   }

   if (cl_qmap_count(&p_ftree->hca_tbl) < 2)
   {
      osm_log(&p_ftree->p_osm->log, OSM_LOG_SYS,
              "Fabric has %u HCAa - topology is not fat-tree.\n"
              "Falling back to default routing.\n",
              cl_qmap_count(&p_ftree->hca_tbl));
      status = -1;
      goto Exit;
   }

   osm_log(&p_ftree->p_osm->log, OSM_LOG_VERBOSE,
           "__osm_ftree_construct_fabric: Ranking FatTree\n");

   if (__osm_ftree_fabric_perform_ranking(p_ftree) != 0)
   {
      if (__osm_ftree_fabric_get_rank(p_ftree) > FAT_TREE_MAX_RANK)
         osm_log(&p_ftree->p_osm->log, OSM_LOG_SYS,
                 "Fabric rank is %u (>%u) - "
                 "fat-tree routing falls back to default routing\n",
                 __osm_ftree_fabric_get_rank(p_ftree), FAT_TREE_MAX_RANK);
      else if (__osm_ftree_fabric_get_rank(p_ftree) < FAT_TREE_MIN_RANK)
         osm_log(&p_ftree->p_osm->log, OSM_LOG_SYS,
                 "Fabric rank is %u (<%u) - "
                 "fat-tree routing falls back to default routing\n",
                 __osm_ftree_fabric_get_rank(p_ftree), FAT_TREE_MIN_RANK);
      status = -1;
      goto Exit;
   }

   /* For each hca and switch, construct array of ports.
      This is done after the whole FatTree data structure is ready, because
      we want the ports to have pointers to ftree_{sw,hca}_t objects.*/
   osm_log(&p_ftree->p_osm->log, OSM_LOG_VERBOSE,
           "__osm_ftree_construct_fabric: "
           "Populating HCA & switch ports\n");
   if (__osm_ftree_fabric_populate_ports(p_ftree) != 0)
   {
      osm_log(&p_ftree->p_osm->log, OSM_LOG_SYS,
              "Fabric topology is not a fat-tree - "
              "routing falls back to default routing\n");
      status = -1;
      goto Exit;
   }

   /* Assign index to all the switches and hca's in the fabric.
      This function also sorts all the port arrays of the switches
      by the remote switch index, creates a leaf switch array
      sorted by the switch index, and tracks the maximal number of
      hcas per leaf switch. */
   __osm_ftree_fabric_make_indexing(p_ftree);

   /* print general info about fabric topology */
   __osm_ftree_fabric_dump_general_info(p_ftree);

   /* dump full tree topology */
   if (osm_log_is_active(&p_ftree->p_osm->log, OSM_LOG_DEBUG))
       __osm_ftree_fabric_dump(p_ftree);

   if (! __osm_ftree_fabric_validate_topology(p_ftree))
   {
      osm_log(&p_ftree->p_osm->log, OSM_LOG_SYS,
              "Fabric topology is not a fat-tree - "
              "routing falls back to default routing\n");
      status = -1;
      goto Exit;
   }

   osm_log(&p_ftree->p_osm->log, OSM_LOG_VERBOSE,
           "__osm_ftree_construct_fabric: "
           "Max LID in switch LFTs (in host order): 0x%x\n",
           p_ftree->lft_max_lid_ho);

 Exit:
   if (status != 0)
   {
      osm_log(&p_ftree->p_osm->log, OSM_LOG_VERBOSE,
              "__osm_ftree_construct_fabric: "
             "Clearing FatTree Fabric data structures\n");
     __osm_ftree_fabric_clear(p_ftree);
   }
   else
      p_ftree->fabric_built = TRUE;

   osm_log(&p_ftree->p_osm->log, OSM_LOG_VERBOSE,
           "__osm_ftree_construct_fabric: \n"
           "                       |--------------------------------------------------|\n"
           "                       |- Done constructing FatTree fabric (status = %d) -|\n"
           "                       |--------------------------------------------------|\n\n",
           status);

   OSM_LOG_EXIT(&p_ftree->p_osm->log);
   return status;
} /* __osm_ftree_construct_fabric() */

/***************************************************
 ***************************************************/

static int 
__osm_ftree_do_routing(
   IN  void * context)
{
   ftree_fabric_t * p_ftree = context;

   OSM_LOG_ENTER(&p_ftree->p_osm->log, __osm_ftree_do_routing);

   if (!p_ftree->fabric_built)
      goto Exit;

   osm_log(&p_ftree->p_osm->log, OSM_LOG_VERBOSE,"__osm_ftree_do_routing: "
           "Starting FatTree routing\n");

   osm_log(&p_ftree->p_osm->log, OSM_LOG_VERBOSE,"__osm_ftree_do_routing: "
           "Filling switch forwarding tables for routes to HCAs\n");
   __osm_ftree_fabric_route_to_hcas(p_ftree);

   osm_log(&p_ftree->p_osm->log, OSM_LOG_VERBOSE,"__osm_ftree_do_routing: "
           "Filling switch forwarding tables for switch-to-switch pathes\n");
   __osm_ftree_fabric_route_to_switches(p_ftree);

   /* for each switch, set its fwd table */
   cl_qmap_apply_func(&p_ftree->sw_tbl, __osm_ftree_set_sw_fwd_table, (void *)p_ftree);

   /* write out hca ordering file */
   __osm_ftree_fabric_dump_hca_ordering(p_ftree);

   osm_log(&p_ftree->p_osm->log, OSM_LOG_VERBOSE,"__osm_ftree_do_routing: "
           "FatTree routing is done\n");

 Exit:
   OSM_LOG_EXIT(&p_ftree->p_osm->log);
   return 0;
}

/***************************************************
 ***************************************************/

static void 
__osm_ftree_delete(
   IN  void * context)
{
   if (!context)
      return;
   __osm_ftree_fabric_destroy((ftree_fabric_t *)context);
}

/***************************************************
 ***************************************************/

int osm_ucast_ftree_setup(osm_opensm_t * p_osm)
{
   ftree_fabric_t * p_ftree = __osm_ftree_fabric_create();
   if (!p_ftree)
      return -1;

   p_ftree->p_osm = p_osm;

   p_osm->routing_engine.context = (void *)p_ftree;
   p_osm->routing_engine.build_lid_matrices = __osm_ftree_construct_fabric;
   p_osm->routing_engine.ucast_build_fwd_tables = __osm_ftree_do_routing;
   p_osm->routing_engine.delete = __osm_ftree_delete;
   return 0;
}

/***************************************************
 ***************************************************/


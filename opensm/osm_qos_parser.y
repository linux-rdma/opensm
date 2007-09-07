%{
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
 *    Grammar of OSM QoS parser.
 *
 * Environment:
 *    Linux User Mode
 *
 * Author:
 *    Yevgeny Kliteynik, Mellanox
 */

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <opensm/osm_opensm.h>
#include <opensm/osm_qos_policy.h>
#include <opensm/osm_qos_parser_y.h>

#define OSM_QOS_POLICY_MAX_LINE_LEN         1024*10
#define OSM_QOS_POLICY_SL2VL_TABLE_LEN      IB_MAX_NUM_VLS
#define OSM_QOS_POLICY_MAX_VL_NUM           IB_MAX_NUM_VLS

typedef struct tmp_parser_struct_t_ {
    char       str[OSM_QOS_POLICY_MAX_LINE_LEN];
    uint64_t   num_pair[2];
    cl_list_t  str_list;
    cl_list_t  num_list;
    cl_list_t  num_pair_list;
} tmp_parser_struct_t;

static void __parser_tmp_struct_init();
static void __parser_tmp_struct_reset();
static void __parser_tmp_struct_destroy();

static char * __parser_strip_white(char * str);

static void __parser_str2uint64(uint64_t * p_val, char * str);

static void __parser_port_group_start();
static int __parser_port_group_end();

static void __parser_sl2vl_scope_start();
static int __parser_sl2vl_scope_end();

static void __parser_vlarb_scope_start();
static int __parser_vlarb_scope_end();

static void __parser_qos_level_start();
static int __parser_qos_level_end();

static void __parser_match_rule_start();
static int __parser_match_rule_end();

static void __rangelist2rangearr(
    cl_list_t    * p_list,
    uint64_t  ** * p_arr,
    unsigned     * p_arr_len);

static void __merge_rangearr(
    uint64_t  **   range_arr_1,
    unsigned       range_len_1,
    uint64_t  **   range_arr_2,
    unsigned       range_len_2,
    uint64_t  ** * p_arr,
    unsigned     * p_arr_len );

extern char * __qos_parser_text;
extern void __qos_parser_error (char *s);
extern int __qos_parser_lex (void);
extern FILE * __qos_parser_in;

#define RESET_BUFFER  __parser_tmp_struct_reset()

tmp_parser_struct_t tmp_parser_struct;

int column_num;
int line_num;

osm_qos_policy_t       * p_qos_policy = NULL;
osm_qos_port_group_t   * p_current_port_group = NULL;
osm_qos_sl2vl_scope_t  * p_current_sl2vl_scope = NULL;
osm_qos_vlarb_scope_t  * p_current_vlarb_scope = NULL;
osm_qos_level_t        * p_current_qos_level = NULL;
osm_qos_match_rule_t   * p_current_qos_match_rule = NULL;
osm_log_t              * p_qos_parser_osm_log;

/***************************************************/

%}

%token TK_NUMBER
%token TK_DASH
%token TK_DOTDOT
%token TK_COMMA
%token TK_ASTERISK
%token TK_TEXT

%token TK_PORT_GROUPS_START
%token TK_PORT_GROUPS_END
%token TK_PORT_GROUP_START
%token TK_PORT_GROUP_END

%token TK_QOS_SETUP_START
%token TK_QOS_SETUP_END
%token TK_VLARB_TABLES_START
%token TK_VLARB_TABLES_END
%token TK_VLARB_SCOPE_START
%token TK_VLARB_SCOPE_END

%token TK_SL2VL_TABLES_START
%token TK_SL2VL_TABLES_END
%token TK_SL2VL_SCOPE_START
%token TK_SL2VL_SCOPE_END

%token TK_QOS_LEVELS_START
%token TK_QOS_LEVELS_END
%token TK_QOS_LEVEL_START
%token TK_QOS_LEVEL_END

%token TK_QOS_MATCH_RULES_START
%token TK_QOS_MATCH_RULES_END
%token TK_QOS_MATCH_RULE_START
%token TK_QOS_MATCH_RULE_END

%token TK_NAME
%token TK_USE
%token TK_PORT_GUID
%token TK_PORT_NAME
%token TK_PARTITION
%token TK_NODE_TYPE
%token TK_GROUP
%token TK_ACROSS
%token TK_VLARB_HIGH
%token TK_VLARB_LOW
%token TK_VLARB_HIGH_LIMIT
%token TK_TO
%token TK_FROM
%token TK_ACROSS_TO
%token TK_ACROSS_FROM
%token TK_SL2VL_TABLE
%token TK_SL
%token TK_MTU_LIMIT
%token TK_RATE_LIMIT
%token TK_PACKET_LIFE
%token TK_PATH_BITS
%token TK_QOS_CLASS
%token TK_SOURCE
%token TK_DESTINATION
%token TK_SERVICE_ID
%token TK_QOS_LEVEL_NAME
%token TK_PKEY

%token TK_NODE_TYPE_ROUTER
%token TK_NODE_TYPE_CA
%token TK_NODE_TYPE_SWITCH
%token TK_NODE_TYPE_SELF
%token TK_NODE_TYPE_ALL


%start head

%%

head:               qos_policy_entries
                    ;

qos_policy_entries: /* empty */
                    | qos_policy_entries qos_policy_entry
                    ;

qos_policy_entry:     port_groups_section
                    | qos_setup_section
                    | qos_levels_section
                    | qos_match_rules_section
                    ;

    /*
     * Parsing port groups:
     * -------------------
     *  port-groups
     *       port-group
     *          name: Storage
     *          use: our SRP storage targets
     *          port-guid: 0x1000000000000001,0x1000000000000002
     *          ...
     *          port-name: vs1/HCA-1/P1
     *          ...
     *          partition: Part1
     *          ...
     *          node-type: ROUTER,CA,SWITCH,SELF,ALL
     *          ...
     *      end-port-group
     *      port-group
     *          ...
     *      end-port-group
     *  end-port-groups
     */


port_groups_section: TK_PORT_GROUPS_START port_groups TK_PORT_GROUPS_END
                     ;

port_groups:        port_group
                    | port_groups port_group
                    ;

port_group:         port_group_start port_group_entries port_group_end
                    ;

port_group_start:   TK_PORT_GROUP_START {
                        __parser_port_group_start();
                    }
                    ;

port_group_end:     TK_PORT_GROUP_END {
                        if ( __parser_port_group_end() )
                            return 1;
                    }
                    ;

port_group_entries: /* empty */
                    | port_group_entries port_group_entry
                    ;

port_group_entry:     port_group_name
                    | port_group_use
                    | port_group_port_guid
                    | port_group_port_name
                    | port_group_partition
                    | port_group_node_type
                    ;


    /*
     * Parsing qos setup:
     * -----------------
     *  qos-setup
     *      vlarb-tables
     *          vlarb-scope
     *              ...
     *          end-vlarb-scope
     *          vlarb-scope
     *              ...
     *          end-vlarb-scope
     *     end-vlarb-tables
     *     sl2vl-tables
     *          sl2vl-scope
     *              ...
     *         end-sl2vl-scope
     *         sl2vl-scope
     *              ...
     *          end-sl2vl-scope
     *     end-sl2vl-tables
     *  end-qos-setup
     */

qos_setup_section:  TK_QOS_SETUP_START qos_setup_items TK_QOS_SETUP_END
                    ;

qos_setup_items:    /* empty */
                    | qos_setup_items vlarb_tables
                    | qos_setup_items sl2vl_tables
                    ;

    /* Parsing vlarb-tables */

vlarb_tables:       TK_VLARB_TABLES_START vlarb_scope_items TK_VLARB_TABLES_END

vlarb_scope_items:  /* empty */
                    | vlarb_scope_items vlarb_scope
                    ;

vlarb_scope:        vlarb_scope_start vlarb_scope_entries vlarb_scope_end
                    ;

vlarb_scope_start:  TK_VLARB_SCOPE_START {
                        __parser_vlarb_scope_start();
                    }
                    ;

vlarb_scope_end:    TK_VLARB_SCOPE_END {
                        if ( __parser_vlarb_scope_end() )
                            return 1;
                    }
                    ;

vlarb_scope_entries:/* empty */
                    | vlarb_scope_entries vlarb_scope_entry
                    ;

    /*
     *          vlarb-scope
     *              group: Storage
     *              ...
     *              across: Storage
     *              ...
     *              vlarb-high: 0:255,1:127,2:63,3:31,4:15,5:7,6:3,7:1
     *              vlarb-low: 8:255,9:127,10:63,11:31,12:15,13:7,14:3
     *              vl-high-limit: 10
     *          end-vlarb-scope
     */

vlarb_scope_entry:    vlarb_scope_group
                    | vlarb_scope_across
                    | vlarb_scope_vlarb_high
                    | vlarb_scope_vlarb_low
                    | vlarb_scope_vlarb_high_limit
                    ;

    /* Parsing sl2vl-tables */

sl2vl_tables:       TK_SL2VL_TABLES_START sl2vl_scope_items TK_SL2VL_TABLES_END
                    ;

sl2vl_scope_items:  /* empty */
                    | sl2vl_scope_items sl2vl_scope
                    ;

sl2vl_scope:        sl2vl_scope_start sl2vl_scope_entries sl2vl_scope_end
                    ;

sl2vl_scope_start:  TK_SL2VL_SCOPE_START {
                        __parser_sl2vl_scope_start();
                    }
                    ;

sl2vl_scope_end:    TK_SL2VL_SCOPE_END {
                        if ( __parser_sl2vl_scope_end() )
                            return 1;
                    }
                    ;

sl2vl_scope_entries:/* empty */
                    | sl2vl_scope_entries sl2vl_scope_entry
                    ;

    /*
     *          sl2vl-scope
     *              group: Part1
     *              ...
     *              from: *
     *              ...
     *              to: *
     *              ...
     *              across-to: Storage2
     *              ...
     *              across-from: Storage1
     *              ...
     *              sl2vl-table: 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,7
     *          end-sl2vl-scope
     */

sl2vl_scope_entry:    sl2vl_scope_group
                    | sl2vl_scope_across
                    | sl2vl_scope_across_from
                    | sl2vl_scope_across_to
                    | sl2vl_scope_from
                    | sl2vl_scope_to
                    | sl2vl_scope_sl2vl_table
                    ;

    /*
     * Parsing qos-levels:
     * ------------------
     *  qos-levels
     *      qos-level
     *          name: qos_level_1
     *          use: for the lowest priority communication
     *          sl: 15
     *          mtu-limit: 1
     *          rate-limit: 1
     *          packet-life: 12
     *          path-bits: 2,4,8-32
     *          pkey: 0x00FF-0x0FFF
     *      end-qos-level
     *          ...
     *      qos-level
     *    end-qos-level
     *  end-qos-levels
     */


qos_levels_section: TK_QOS_LEVELS_START qos_levels TK_QOS_LEVELS_END
                    ;

qos_levels:         /* empty */
                    | qos_levels qos_level
                    ;

qos_level:          qos_level_start qos_level_entries qos_level_end
                    ;

qos_level_start:    TK_QOS_LEVEL_START {
                        __parser_qos_level_start();
                    }
                    ;

qos_level_end:      TK_QOS_LEVEL_END {
                        if ( __parser_qos_level_end() )
                            return 1;
                    }
                    ;

qos_level_entries:  /* empty */
                    | qos_level_entries qos_level_entry
                    ;

qos_level_entry:      qos_level_name
                    | qos_level_use
                    | qos_level_sl
                    | qos_level_mtu_limit
                    | qos_level_rate_limit
                    | qos_level_packet_life
                    | qos_level_path_bits
                    | qos_level_pkey
                    ;

    /*
     * Parsing qos-match-rules:
     * -----------------------
     *  qos-match-rules
     *      qos-match-rule
     *          use: low latency by class 7-9 or 11 and bla bla
     *          qos-class: 7-9,11
     *          qos-level-name: default
     *          source: Storage
     *          destination: Storage
     *          service-id: 22,4719-5000
     *          pkey: 0x00FF-0x0FFF
     *      end-qos-match-rule
     *      qos-match-rule
     *          ...
     *      end-qos-match-rule
     *  end-qos-match-rules
     */

qos_match_rules_section: TK_QOS_MATCH_RULES_START qos_match_rules TK_QOS_MATCH_RULES_END
                    ;

qos_match_rules:    /* empty */
                    | qos_match_rules qos_match_rule
                    ;

qos_match_rule:     qos_match_rule_start qos_match_rule_entries qos_match_rule_end
                    ;

qos_match_rule_start: TK_QOS_MATCH_RULE_START {
                        __parser_match_rule_start();
                    }
                    ;

qos_match_rule_end: TK_QOS_MATCH_RULE_END {
                        if ( __parser_match_rule_end() )
                            return 1;
                    }
                    ;

qos_match_rule_entries: /* empty */
                    | qos_match_rule_entries qos_match_rule_entry
                    ;

qos_match_rule_entry: qos_match_rule_use
                    | qos_match_rule_qos_class
                    | qos_match_rule_qos_level_name
                    | qos_match_rule_source
                    | qos_match_rule_destination
                    | qos_match_rule_service_id
                    | qos_match_rule_pkey
                    ;

    /*
     *  port_group_entry values:
     *      port_group_name
     *      port_group_use
     *      port_group_port_guid
     *      port_group_port_name
     *      port_group_partition
     *      port_group_node_type
     */

port_group_name:        port_group_name_start single_string {
                            /* 'name' of 'port-group' - one instance */
                            cl_list_iterator_t    list_iterator;
                            char                * tmp_str;

                            if (p_current_port_group->name)
                            {
                                __qos_parser_error("port-group has multiple 'name' tags");
                                cl_list_remove_all(&tmp_parser_struct.str_list);
                                return 1;
                            }

                            list_iterator = cl_list_head(&tmp_parser_struct.str_list);
                            if ( list_iterator != cl_list_end(&tmp_parser_struct.str_list) )
                            {
                                tmp_str = (char*)cl_list_obj(list_iterator);
                                if (tmp_str)
                                    p_current_port_group->name = tmp_str;
                            }
                            cl_list_remove_all(&tmp_parser_struct.str_list);
                        }
                        ;

port_group_name_start:  TK_NAME {
                            RESET_BUFFER;
                        }
                        ;

port_group_use:         port_group_use_start single_string {
                            /* 'use' of 'port-group' - one instance */
                            cl_list_iterator_t    list_iterator;
                            char                * tmp_str;

                            if (p_current_port_group->use)
                            {
                                __qos_parser_error("port-group has multiple 'use' tags");
                                cl_list_remove_all(&tmp_parser_struct.str_list);
                                return 1;
                            }

                            list_iterator = cl_list_head(&tmp_parser_struct.str_list);
                            if ( list_iterator != cl_list_end(&tmp_parser_struct.str_list) )
                            {
                                tmp_str = (char*)cl_list_obj(list_iterator);
                                if (tmp_str)
                                    p_current_port_group->use = tmp_str;
                            }
                            cl_list_remove_all(&tmp_parser_struct.str_list);
                        }
                        ;

port_group_use_start:   TK_USE {
                            RESET_BUFFER;
                        }
                        ;

port_group_port_name:   port_group_port_name_start string_list {
                            /* 'port-name' in 'port-group' - any num of instances */
                            cl_list_iterator_t    list_iterator;
                            char                * tmp_str;

                            list_iterator = cl_list_head(&tmp_parser_struct.str_list);
                            while( list_iterator != cl_list_end(&tmp_parser_struct.str_list) )
                            {
                                tmp_str = (char*)cl_list_obj(list_iterator);

                                /*
                                 * TODO: parse port name strings
                                 */

                                if (tmp_str)
                                    cl_list_insert_tail(&p_current_port_group->port_name_list,tmp_str);
                                list_iterator = cl_list_next(list_iterator);
                            }
                            cl_list_remove_all(&tmp_parser_struct.str_list);
                        }
                        ;

port_group_port_name_start: TK_PORT_NAME {
                            RESET_BUFFER;
                        }
                        ;

port_group_port_guid:   port_group_port_guid_start list_of_ranges {
                            /* 'port-guid' in 'port-group' - any num of instances */
                            /* list of guid ranges */
                            if (cl_list_count(&tmp_parser_struct.num_pair_list))
                            {
                                uint64_t ** range_arr;
                                unsigned range_len;

                                __rangelist2rangearr( &tmp_parser_struct.num_pair_list,
                                                      &range_arr,
                                                      &range_len );

                                if ( !p_current_port_group->guid_range_len )
                                {
                                    p_current_port_group->guid_range_arr = range_arr;
                                    p_current_port_group->guid_range_len = range_len;
                                }
                                else
                                {
                                    uint64_t ** new_range_arr;
                                    unsigned new_range_len;
                                    __merge_rangearr( p_current_port_group->guid_range_arr,
                                                      p_current_port_group->guid_range_len,
                                                      range_arr,
                                                      range_len,
                                                      &new_range_arr,
                                                      &new_range_len );
                                    p_current_port_group->guid_range_arr = new_range_arr;
                                    p_current_port_group->guid_range_len = new_range_len;
                                }
                            }
                        }
                        ;

port_group_port_guid_start: TK_PORT_GUID {
                            RESET_BUFFER;
                        }
                        ;

port_group_partition:  port_group_partition_start string_list {
                            /* 'partition' in 'port-group' - any num of instances */
                            cl_list_iterator_t    list_iterator;
                            char                * tmp_str;

                            list_iterator = cl_list_head(&tmp_parser_struct.str_list);
                            while( list_iterator != cl_list_end(&tmp_parser_struct.str_list) )
                            {
                                tmp_str = (char*)cl_list_obj(list_iterator);
                                if (tmp_str)
                                    cl_list_insert_tail(&p_current_port_group->partition_list,tmp_str);
                                list_iterator = cl_list_next(list_iterator);
                            }
                            cl_list_remove_all(&tmp_parser_struct.str_list);
                        }
                        ;

port_group_partition_start: TK_PARTITION {
                            RESET_BUFFER;
                        }
                        ;

port_group_node_type:   port_group_node_type_start port_group_node_type_list {
                            /* 'node-type' in 'port-group' - any num of instances */
                        }
                        ;

port_group_node_type_start: TK_NODE_TYPE {
                            RESET_BUFFER;
                        }
                        ;

port_group_node_type_list:  node_type_item
                        |   port_group_node_type_list TK_COMMA node_type_item
                        ;

node_type_item:           node_type_ca
                        | node_type_switch
                        | node_type_router
                        | node_type_all
                        | node_type_self
                        ;

node_type_ca:           TK_NODE_TYPE_CA {
                            p_current_port_group->node_type_ca = TRUE;;
                        }
                        ;

node_type_switch:       TK_NODE_TYPE_SWITCH {
                            p_current_port_group->node_type_switch = TRUE;
                        }
                        ;

node_type_router:       TK_NODE_TYPE_ROUTER {
                            p_current_port_group->node_type_router = TRUE;
                        }
                        ;

node_type_all:          TK_NODE_TYPE_ALL {
                            p_current_port_group->node_type_ca = TRUE;
                            p_current_port_group->node_type_switch = TRUE;
                            p_current_port_group->node_type_router = TRUE;
                        }
                        ;

node_type_self:         TK_NODE_TYPE_SELF {
                            p_current_port_group->node_type_self = TRUE;
                        }
                        ;

    /*
     *  vlarb_scope_entry values:
     *      vlarb_scope_group
     *      vlarb_scope_across
     *      vlarb_scope_vlarb_high
     *      vlarb_scope_vlarb_low
     *      vlarb_scope_vlarb_high_limit
     */



vlarb_scope_group:      vlarb_scope_group_start string_list {
                            /* 'group' in 'vlarb-scope' - any num of instances */
                            cl_list_iterator_t    list_iterator;
                            char                * tmp_str;

                            list_iterator = cl_list_head(&tmp_parser_struct.str_list);
                            while( list_iterator != cl_list_end(&tmp_parser_struct.str_list) )
                            {
                                tmp_str = (char*)cl_list_obj(list_iterator);
                                if (tmp_str)
                                    cl_list_insert_tail(&p_current_vlarb_scope->group_list,tmp_str);
                                list_iterator = cl_list_next(list_iterator);
                            }
                            cl_list_remove_all(&tmp_parser_struct.str_list);
                        }
                        ;

vlarb_scope_group_start: TK_GROUP {
                            RESET_BUFFER;
                        }
                        ;

vlarb_scope_across: vlarb_scope_across_start string_list {
                            /* 'across' in 'vlarb-scope' - any num of instances */
                            cl_list_iterator_t    list_iterator;
                            char                * tmp_str;

                            list_iterator = cl_list_head(&tmp_parser_struct.str_list);
                            while( list_iterator != cl_list_end(&tmp_parser_struct.str_list) )
                            {
                                tmp_str = (char*)cl_list_obj(list_iterator);
                                if (tmp_str)
                                    cl_list_insert_tail(&p_current_vlarb_scope->across_list,tmp_str);
                                list_iterator = cl_list_next(list_iterator);
                            }
                            cl_list_remove_all(&tmp_parser_struct.str_list);
                        }
                        ;

vlarb_scope_across_start: TK_ACROSS {
                            RESET_BUFFER;
                        }
                        ;

vlarb_scope_vlarb_high_limit:  vlarb_scope_vlarb_high_limit_start single_number {
                            /* 'vl-high-limit' in 'vlarb-scope' - one instance of one number */
                            cl_list_iterator_t    list_iterator;
                            uint64_t            * p_tmp_num;

                            list_iterator = cl_list_head(&tmp_parser_struct.num_list);
                            p_tmp_num = (uint64_t*)cl_list_obj(list_iterator);
                            if (p_tmp_num)
                            {
                                p_current_vlarb_scope->vl_high_limit = (uint32_t)(*p_tmp_num);
                                p_current_vlarb_scope->vl_high_limit_set = TRUE;
                                free(p_tmp_num);
                            }

                            cl_list_remove_all(&tmp_parser_struct.num_list);
                        }
                        ;

vlarb_scope_vlarb_high_limit_start: TK_VLARB_HIGH_LIMIT {
                            RESET_BUFFER;
                        }
                        ;

vlarb_scope_vlarb_high: vlarb_scope_vlarb_high_start num_list_with_dotdot {
                            /* 'vlarb-high' in 'vlarb-scope' - list of pairs of numbers with ':' and ',' */
                            cl_list_iterator_t    list_iterator;
                            uint64_t            * num_pair;

                            list_iterator = cl_list_head(&tmp_parser_struct.num_pair_list);
                            while( list_iterator != cl_list_end(&tmp_parser_struct.num_pair_list) )
                            {
                                num_pair = (uint64_t*)cl_list_obj(list_iterator);
                                if (num_pair)
                                    cl_list_insert_tail(&p_current_vlarb_scope->vlarb_high_list,num_pair);
                                list_iterator = cl_list_next(list_iterator);
                            }
                            cl_list_remove_all(&tmp_parser_struct.num_pair_list);
                        }
                        ;

vlarb_scope_vlarb_high_start: TK_VLARB_HIGH {
                            RESET_BUFFER;
                        }
                        ;

vlarb_scope_vlarb_low:  vlarb_scope_vlarb_low_start num_list_with_dotdot {
                            /* 'vlarb-low' in 'vlarb-scope' - list of pairs of numbers with ':' and ',' */
                            cl_list_iterator_t    list_iterator;
                            uint64_t            * num_pair;

                            list_iterator = cl_list_head(&tmp_parser_struct.num_pair_list);
                            while( list_iterator != cl_list_end(&tmp_parser_struct.num_pair_list) )
                            {
                                num_pair = (uint64_t*)cl_list_obj(list_iterator);
                                if (num_pair)
                                    cl_list_insert_tail(&p_current_vlarb_scope->vlarb_low_list,num_pair);
                                list_iterator = cl_list_next(list_iterator);
                            }
                            cl_list_remove_all(&tmp_parser_struct.num_pair_list);
                        }
                        ;

vlarb_scope_vlarb_low_start: TK_VLARB_LOW {
                            RESET_BUFFER;
                        }
                        ;

    /*
     *  sl2vl_scope_entry values:
     *      sl2vl_scope_group
     *      sl2vl_scope_across
     *      sl2vl_scope_across_from
     *      sl2vl_scope_across_to
     *      sl2vl_scope_from
     *      sl2vl_scope_to
     *      sl2vl_scope_sl2vl_table
     */

sl2vl_scope_group:      sl2vl_scope_group_start string_list {
                            /* 'group' in 'sl2vl-scope' - any num of instances */
                            cl_list_iterator_t    list_iterator;
                            char                * tmp_str;

                            list_iterator = cl_list_head(&tmp_parser_struct.str_list);
                            while( list_iterator != cl_list_end(&tmp_parser_struct.str_list) )
                            {
                                tmp_str = (char*)cl_list_obj(list_iterator);
                                if (tmp_str)
                                    cl_list_insert_tail(&p_current_sl2vl_scope->group_list,tmp_str);
                                list_iterator = cl_list_next(list_iterator);
                            }
                            cl_list_remove_all(&tmp_parser_struct.str_list);
                        }
                        ;

sl2vl_scope_group_start: TK_GROUP {
                            RESET_BUFFER;
                        }
                        ;

sl2vl_scope_across:     sl2vl_scope_across_start string_list {
                            /* 'across' in 'sl2vl-scope' - any num of instances */
                            cl_list_iterator_t    list_iterator;
                            char                * tmp_str;

                            list_iterator = cl_list_head(&tmp_parser_struct.str_list);
                            while( list_iterator != cl_list_end(&tmp_parser_struct.str_list) )
                            {
                                tmp_str = (char*)cl_list_obj(list_iterator);
                                if (tmp_str) {
                                    cl_list_insert_tail(&p_current_sl2vl_scope->across_from_list,tmp_str);
                                    cl_list_insert_tail(&p_current_sl2vl_scope->across_to_list,strdup(tmp_str));
                                }
                                list_iterator = cl_list_next(list_iterator);
                            }
                            cl_list_remove_all(&tmp_parser_struct.str_list);
                        }
                        ;

sl2vl_scope_across_start: TK_ACROSS {
                            RESET_BUFFER;
                        }
                        ;

sl2vl_scope_across_from:  sl2vl_scope_across_from_start string_list {
                            /* 'across-from' in 'sl2vl-scope' - any num of instances */
                            cl_list_iterator_t    list_iterator;
                            char                * tmp_str;

                            list_iterator = cl_list_head(&tmp_parser_struct.str_list);
                            while( list_iterator != cl_list_end(&tmp_parser_struct.str_list) )
                            {
                                tmp_str = (char*)cl_list_obj(list_iterator);
                                if (tmp_str)
                                    cl_list_insert_tail(&p_current_sl2vl_scope->across_from_list,tmp_str);
                                list_iterator = cl_list_next(list_iterator);
                            }
                            cl_list_remove_all(&tmp_parser_struct.str_list);
                        }
                        ;

sl2vl_scope_across_from_start: TK_ACROSS_FROM {
                            RESET_BUFFER;
                        }
                        ;

sl2vl_scope_across_to:  sl2vl_scope_across_to_start string_list {
                            /* 'across-to' in 'sl2vl-scope' - any num of instances */
                            cl_list_iterator_t    list_iterator;
                            char                * tmp_str;

                            list_iterator = cl_list_head(&tmp_parser_struct.str_list);
                            while( list_iterator != cl_list_end(&tmp_parser_struct.str_list) )
                            {
                                tmp_str = (char*)cl_list_obj(list_iterator);
                                if (tmp_str) {
                                    cl_list_insert_tail(&p_current_sl2vl_scope->across_to_list,tmp_str);
                                }
                                list_iterator = cl_list_next(list_iterator);
                            }
                            cl_list_remove_all(&tmp_parser_struct.str_list);
                        }
                        ;

sl2vl_scope_across_to_start: TK_ACROSS_TO {
                            RESET_BUFFER;
                        }
                        ;

sl2vl_scope_from:       sl2vl_scope_from_start sl2vl_scope_from_list_or_asterisk {
                            /* 'from' in 'sl2vl-scope' - any num of instances */
                        }
                        ;

sl2vl_scope_from_start: TK_FROM {
                            RESET_BUFFER;
                        }
                        ;

sl2vl_scope_to:         sl2vl_scope_to_start sl2vl_scope_to_list_or_asterisk {
                            /* 'to' in 'sl2vl-scope' - any num of instances */
                        }
                        ;

sl2vl_scope_to_start:   TK_TO {
                            RESET_BUFFER;
                        }
                        ;

sl2vl_scope_from_list_or_asterisk:  sl2vl_scope_from_asterisk
                                  | sl2vl_scope_from_list_of_ranges
                                  ;

sl2vl_scope_from_asterisk: TK_ASTERISK {
                            int i;
                            for (i = 0; i < OSM_QOS_POLICY_MAX_PORTS_ON_SWITCH; i++)
                                p_current_sl2vl_scope->from[i] = TRUE;
                        }
                        ;

sl2vl_scope_to_list_or_asterisk:  sl2vl_scope_to_asterisk
                                | sl2vl_scope_to_list_of_ranges
                                  ;

sl2vl_scope_to_asterisk: TK_ASTERISK {
                            int i;
                            for (i = 0; i < OSM_QOS_POLICY_MAX_PORTS_ON_SWITCH; i++)
                                p_current_sl2vl_scope->to[i] = TRUE;
                        }
                        ;

sl2vl_scope_from_list_of_ranges: list_of_ranges {
                            int i;
                            cl_list_iterator_t    list_iterator;
                            uint64_t            * num_pair;
                            uint8_t               num1, num2;

                            list_iterator = cl_list_head(&tmp_parser_struct.num_pair_list);
                            while( list_iterator != cl_list_end(&tmp_parser_struct.num_pair_list) )
                            {
                                num_pair = (uint64_t*)cl_list_obj(list_iterator);
                                if (num_pair)
                                {
                                    if ( num_pair[0] < 0 ||
                                         num_pair[1] >= OSM_QOS_POLICY_MAX_PORTS_ON_SWITCH )
                                    {
                                        __qos_parser_error("port number out of range 'from' list");
                                        free(num_pair);
                                        cl_list_remove_all(&tmp_parser_struct.num_pair_list);
                                        return 1;
                                    }
                                    num1 = (uint8_t)num_pair[0];
                                    num2 = (uint8_t)num_pair[1];
                                    free(num_pair);
                                    for (i = num1; i <= num2; i++)
                                        p_current_sl2vl_scope->from[i] = TRUE;
                                }
                                list_iterator = cl_list_next(list_iterator);
                            }
                            cl_list_remove_all(&tmp_parser_struct.num_pair_list);
                        }
                        ;

sl2vl_scope_to_list_of_ranges: list_of_ranges {
                            int i;
                            cl_list_iterator_t    list_iterator;
                            uint64_t            * num_pair;
                            uint8_t               num1, num2;

                            list_iterator = cl_list_head(&tmp_parser_struct.num_pair_list);
                            while( list_iterator != cl_list_end(&tmp_parser_struct.num_pair_list) )
                            {
                                num_pair = (uint64_t*)cl_list_obj(list_iterator);
                                if (num_pair)
                                {
                                    if ( num_pair[0] < 0 ||
                                         num_pair[1] >= OSM_QOS_POLICY_MAX_PORTS_ON_SWITCH )
                                    {
                                        __qos_parser_error("port number out of range 'to' list");
                                        free(num_pair);
                                        cl_list_remove_all(&tmp_parser_struct.num_pair_list);
                                        return 1;
                                    }
                                    num1 = (uint8_t)num_pair[0];
                                    num2 = (uint8_t)num_pair[1];
                                    free(num_pair);
                                    for (i = num1; i <= num2; i++)
                                        p_current_sl2vl_scope->to[i] = TRUE;
                                }
                                list_iterator = cl_list_next(list_iterator);
                            }
                            cl_list_remove_all(&tmp_parser_struct.num_pair_list);
                        }
                        ;


sl2vl_scope_sl2vl_table:  sl2vl_scope_sl2vl_table_start num_list {
                            /* 'sl2vl-table' - one instance of exactly
                               OSM_QOS_POLICY_SL2VL_TABLE_LEN numbers */
                            cl_list_iterator_t    list_iterator;
                            uint64_t              num;
                            uint64_t            * p_num;
                            int                   i = 0;

                            if (p_current_sl2vl_scope->sl2vl_table_set)
                            {
                                __qos_parser_error("sl2vl-scope has more than one sl2vl-table");
                                cl_list_remove_all(&tmp_parser_struct.num_list);
                                return 1;
                            }

                            if (cl_list_count(&tmp_parser_struct.num_list) != OSM_QOS_POLICY_SL2VL_TABLE_LEN)
                            {
                                __qos_parser_error("wrong number of values in 'sl2vl-table' (should be 16)");
                                cl_list_remove_all(&tmp_parser_struct.num_list);
                                return 1;
                            }

                            list_iterator = cl_list_head(&tmp_parser_struct.num_list);
                            while( list_iterator != cl_list_end(&tmp_parser_struct.num_list) )
                            {
                                p_num = (uint64_t*)cl_list_obj(list_iterator);
                                num = *p_num;
                                free(p_num);
                                if (num >= OSM_QOS_POLICY_MAX_VL_NUM)
                                {
                                    __qos_parser_error("wrong VL value in 'sl2vl-table' (should be 0 to 15)");
                                    cl_list_remove_all(&tmp_parser_struct.num_list);
                                    return 1;
                                }

                                p_current_sl2vl_scope->sl2vl_table[i++] = (uint8_t)num;
                                list_iterator = cl_list_next(list_iterator);
                            }
                            p_current_sl2vl_scope->sl2vl_table_set = TRUE;
                            cl_list_remove_all(&tmp_parser_struct.num_list);
                        }
                        ;

sl2vl_scope_sl2vl_table_start: TK_SL2VL_TABLE {
                            RESET_BUFFER;
                        }
                        ;

    /*
     *  qos_level_entry values:
     *      qos_level_name
     *      qos_level_use
     *      qos_level_sl
     *      qos_level_mtu_limit
     *      qos_level_rate_limit
     *      qos_level_packet_life
     *      qos_level_path_bits
     *      qos_level_pkey
     */

qos_level_name:         qos_level_name_start single_string {
                            /* 'name' of 'qos-level' - one instance */
                            cl_list_iterator_t    list_iterator;
                            char                * tmp_str;

                            if (p_current_qos_level->name)
                            {
                                __qos_parser_error("qos-level has multiple 'name' tags");
                                cl_list_remove_all(&tmp_parser_struct.str_list);
                                return 1;
                            }

                            list_iterator = cl_list_head(&tmp_parser_struct.str_list);
                            if ( list_iterator != cl_list_end(&tmp_parser_struct.str_list) )
                            {
                                tmp_str = (char*)cl_list_obj(list_iterator);
                                if (tmp_str)
                                    p_current_qos_level->name = tmp_str;
                            }
                            cl_list_remove_all(&tmp_parser_struct.str_list);
                        }
                        ;

qos_level_name_start:   TK_NAME {
                            RESET_BUFFER;
                        }
                        ;

qos_level_use:          qos_level_use_start single_string {
                            /* 'use' of 'qos-level' - one instance */
                            cl_list_iterator_t    list_iterator;
                            char                * tmp_str;

                            if (p_current_qos_level->use)
                            {
                                __qos_parser_error("qos-level has multiple 'use' tags");
                                cl_list_remove_all(&tmp_parser_struct.str_list);
                                return 1;
                            }

                            list_iterator = cl_list_head(&tmp_parser_struct.str_list);
                            if ( list_iterator != cl_list_end(&tmp_parser_struct.str_list) )
                            {
                                tmp_str = (char*)cl_list_obj(list_iterator);
                                if (tmp_str)
                                    p_current_qos_level->use = tmp_str;
                            }
                            cl_list_remove_all(&tmp_parser_struct.str_list);
                        }
                        ;

qos_level_use_start:    TK_USE {
                            RESET_BUFFER;
                        }
                        ;

qos_level_sl:           qos_level_sl_start single_number {
                            /* 'sl' in 'qos-level' - one instance */
                            cl_list_iterator_t   list_iterator;
                            uint64_t           * p_num;

                            if (p_current_qos_level->sl_set)
                            {
                                __qos_parser_error("'qos-level' has multiple 'sl' tags");
                                cl_list_remove_all(&tmp_parser_struct.num_list);
                                return 1;
                            }
                            list_iterator = cl_list_head(&tmp_parser_struct.num_list);
                            p_num = (uint64_t*)cl_list_obj(list_iterator);
                            p_current_qos_level->sl = (uint8_t)(*p_num);
                            free(p_num);
                            p_current_qos_level->sl_set = TRUE;
                            cl_list_remove_all(&tmp_parser_struct.num_list);
                        }
                        ;

qos_level_sl_start:     TK_SL {
                            RESET_BUFFER;
                        }
                        ;

qos_level_mtu_limit:    qos_level_mtu_limit_start single_number {
                            /* 'mtu-limit' in 'qos-level' - one instance */
                            cl_list_iterator_t   list_iterator;
                            uint64_t           * p_num;

                            if (p_current_qos_level->mtu_limit_set)
                            {
                                __qos_parser_error("'qos-level' has multiple 'mtu-limit' tags");
                                cl_list_remove_all(&tmp_parser_struct.num_list);
                                return 1;
                            }
                            list_iterator = cl_list_head(&tmp_parser_struct.num_list);
                            p_num = (uint64_t*)cl_list_obj(list_iterator);
                            p_current_qos_level->mtu_limit = (uint8_t)(*p_num);
                            free(p_num);
                            p_current_qos_level->mtu_limit_set = TRUE;
                            cl_list_remove_all(&tmp_parser_struct.num_list);
                        }
                        ;

qos_level_mtu_limit_start: TK_MTU_LIMIT {
                            /* 'mtu-limit' in 'qos-level' - one instance */
                            RESET_BUFFER;
                        }
                        ;

qos_level_rate_limit:    qos_level_rate_limit_start single_number {
                            /* 'rate-limit' in 'qos-level' - one instance */
                            cl_list_iterator_t   list_iterator;
                            uint64_t           * p_num;

                            if (p_current_qos_level->rate_limit_set)
                            {
                                __qos_parser_error("'qos-level' has multiple 'rate-limit' tags");
                                cl_list_remove_all(&tmp_parser_struct.num_list);
                                return 1;
                            }
                            list_iterator = cl_list_head(&tmp_parser_struct.num_list);
                            p_num = (uint64_t*)cl_list_obj(list_iterator);
                            p_current_qos_level->rate_limit = (uint8_t)(*p_num);
                            free(p_num);
                            p_current_qos_level->rate_limit_set = TRUE;
                            cl_list_remove_all(&tmp_parser_struct.num_list);
                        }
                        ;

qos_level_rate_limit_start: TK_RATE_LIMIT {
                            /* 'rate-limit' in 'qos-level' - one instance */
                            RESET_BUFFER;
                        }
                        ;

qos_level_packet_life:  qos_level_packet_life_start single_number {
                            /* 'packet-life' in 'qos-level' - one instance */
                            cl_list_iterator_t   list_iterator;
                            uint64_t           * p_num;

                            if (p_current_qos_level->pkt_life_set)
                            {
                                __qos_parser_error("'qos-level' has multiple 'packet-life' tags");
                                cl_list_remove_all(&tmp_parser_struct.num_list);
                                return 1;
                            }
                            list_iterator = cl_list_head(&tmp_parser_struct.num_list);
                            p_num = (uint64_t*)cl_list_obj(list_iterator);
                            p_current_qos_level->pkt_life = (uint8_t)(*p_num);
                            free(p_num);
                            p_current_qos_level->pkt_life_set= TRUE;
                            cl_list_remove_all(&tmp_parser_struct.num_list);
                        }
                        ;

qos_level_packet_life_start: TK_PACKET_LIFE {
                            /* 'packet-life' in 'qos-level' - one instance */
                            RESET_BUFFER;
                        }
                        ;

qos_level_path_bits:    qos_level_path_bits_start list_of_ranges {
                            /* 'path-bits' in 'qos-level' - any num of instances */
                            /* list of path bit ranges */

                            if (cl_list_count(&tmp_parser_struct.num_pair_list))
                            {
                                uint64_t ** range_arr;
                                unsigned range_len;

                                __rangelist2rangearr( &tmp_parser_struct.num_pair_list,
                                                      &range_arr,
                                                      &range_len );

                                if ( !p_current_qos_level->path_bits_range_len )
                                {
                                    p_current_qos_level->path_bits_range_arr = range_arr;
                                    p_current_qos_level->path_bits_range_len = range_len;
                                }
                                else
                                {
                                    uint64_t ** new_range_arr;
                                    unsigned new_range_len;
                                    __merge_rangearr( p_current_qos_level->path_bits_range_arr,
                                                      p_current_qos_level->path_bits_range_len,
                                                      range_arr,
                                                      range_len,
                                                      &new_range_arr,
                                                      &new_range_len );
                                    p_current_qos_level->path_bits_range_arr = new_range_arr;
                                    p_current_qos_level->path_bits_range_len = new_range_len;
                                }
                            }
                        }
                        ;

qos_level_path_bits_start: TK_PATH_BITS {
                            RESET_BUFFER;
                        }
                        ;

qos_level_pkey:         qos_level_pkey_start list_of_ranges {
                            /* 'pkey' in 'qos-level' - num of instances of list of ranges */
                            if (cl_list_count(&tmp_parser_struct.num_pair_list))
                            {
                                uint64_t ** range_arr;
                                unsigned range_len;

                                __rangelist2rangearr( &tmp_parser_struct.num_pair_list,
                                                      &range_arr,
                                                      &range_len );

                                if ( !p_current_qos_level->pkey_range_len )
                                {
                                    p_current_qos_level->pkey_range_arr = range_arr;
                                    p_current_qos_level->pkey_range_len = range_len;
                                }
                                else
                                {
                                    uint64_t ** new_range_arr;
                                    unsigned new_range_len;
                                    __merge_rangearr( p_current_qos_level->pkey_range_arr,
                                                      p_current_qos_level->pkey_range_len,
                                                      range_arr,
                                                      range_len,
                                                      &new_range_arr,
                                                      &new_range_len );
                                    p_current_qos_level->pkey_range_arr = new_range_arr;
                                    p_current_qos_level->pkey_range_len = new_range_len;
                                }
                            }
                        }
                        ;

qos_level_pkey_start:   TK_PKEY {
                            RESET_BUFFER;
                        }
                        ;

    /*
     *  qos_match_rule_entry values:
     *      qos_match_rule_use
     *      qos_match_rule_qos_class
     *      qos_match_rule_qos_level_name
     *      qos_match_rule_source
     *      qos_match_rule_destination
     *      qos_match_rule_service_id
     *      qos_match_rule_pkey
     */


qos_match_rule_use:     qos_match_rule_use_start single_string {
                            /* 'use' of 'qos-match-rule' - one instance */
                            cl_list_iterator_t    list_iterator;
                            char                * tmp_str;

                            if (p_current_qos_match_rule->use)
                            {
                                __qos_parser_error("'qos-match-rule' has multiple 'use' tags");
                                cl_list_remove_all(&tmp_parser_struct.str_list);
                                return 1;
                            }

                            list_iterator = cl_list_head(&tmp_parser_struct.str_list);
                            if ( list_iterator != cl_list_end(&tmp_parser_struct.str_list) )
                            {
                                tmp_str = (char*)cl_list_obj(list_iterator);
                                if (tmp_str)
                                    p_current_qos_match_rule->use = tmp_str;
                            }
                            cl_list_remove_all(&tmp_parser_struct.str_list);
                        }
                        ;

qos_match_rule_use_start: TK_USE {
                            RESET_BUFFER;
                        }
                        ;

qos_match_rule_qos_class: qos_match_rule_qos_class_start list_of_ranges {
                            /* 'qos-class' in 'qos-match-rule' - num of instances of list of ranges */
                            /* list of class ranges (QoS Class is 12-bit value) */
                            if (cl_list_count(&tmp_parser_struct.num_pair_list))
                            {
                                uint64_t ** range_arr;
                                unsigned range_len;

                                __rangelist2rangearr( &tmp_parser_struct.num_pair_list,
                                                      &range_arr,
                                                      &range_len );

                                if ( !p_current_qos_match_rule->qos_class_range_len )
                                {
                                    p_current_qos_match_rule->qos_class_range_arr = range_arr;
                                    p_current_qos_match_rule->qos_class_range_len = range_len;
                                }
                                else
                                {
                                    uint64_t ** new_range_arr;
                                    unsigned new_range_len;
                                    __merge_rangearr( p_current_qos_match_rule->qos_class_range_arr,
                                                      p_current_qos_match_rule->qos_class_range_len,
                                                      range_arr,
                                                      range_len,
                                                      &new_range_arr,
                                                      &new_range_len );
                                    p_current_qos_match_rule->qos_class_range_arr = new_range_arr;
                                    p_current_qos_match_rule->qos_class_range_len = new_range_len;
                                }
                            }
                        }
                        ;

qos_match_rule_qos_class_start: TK_QOS_CLASS {
                            RESET_BUFFER;
                        }
                        ;

qos_match_rule_source:  qos_match_rule_source_start string_list {
                            /* 'source' in 'qos-match-rule' - text */
                            cl_list_iterator_t    list_iterator;
                            char                * tmp_str;

                            list_iterator = cl_list_head(&tmp_parser_struct.str_list);
                            while( list_iterator != cl_list_end(&tmp_parser_struct.str_list) )
                            {
                                tmp_str = (char*)cl_list_obj(list_iterator);
                                if (tmp_str)
                                    cl_list_insert_tail(&p_current_qos_match_rule->source_list,tmp_str);
                                list_iterator = cl_list_next(list_iterator);
                            }
                            cl_list_remove_all(&tmp_parser_struct.str_list);
                        }
                        ;

qos_match_rule_source_start: TK_SOURCE {
                            RESET_BUFFER;
                        }
                        ;

qos_match_rule_destination: qos_match_rule_destination_start string_list {
                            /* 'destination' in 'qos-match-rule' - text */
                            cl_list_iterator_t    list_iterator;
                            char                * tmp_str;

                            list_iterator = cl_list_head(&tmp_parser_struct.str_list);
                            while( list_iterator != cl_list_end(&tmp_parser_struct.str_list) )
                            {
                                tmp_str = (char*)cl_list_obj(list_iterator);
                                if (tmp_str)
                                    cl_list_insert_tail(&p_current_qos_match_rule->destination_list,tmp_str);
                                list_iterator = cl_list_next(list_iterator);
                            }
                            cl_list_remove_all(&tmp_parser_struct.str_list);
                        }
                        ;

qos_match_rule_destination_start: TK_DESTINATION {
                            RESET_BUFFER;
                        }
                        ;

qos_match_rule_qos_level_name:  qos_match_rule_qos_level_name_start single_string {
                            /* 'qos-level-name' in 'qos-match-rule' - single string */
                            cl_list_iterator_t   list_iterator;
                            char               * tmp_str;

                            if (p_current_qos_match_rule->qos_level_name)
                            {
                                __qos_parser_error("qos-match-rule has multiple 'qos-level-name' tags");
                                cl_list_remove_all(&tmp_parser_struct.num_list);
                                return 1;
                            }

                            list_iterator = cl_list_head(&tmp_parser_struct.str_list);
                            if ( list_iterator != cl_list_end(&tmp_parser_struct.str_list) )
                            {
                                tmp_str = (char*)cl_list_obj(list_iterator);
                                if (tmp_str)
                                    p_current_qos_match_rule->qos_level_name = tmp_str;
                            }
                            cl_list_remove_all(&tmp_parser_struct.str_list);
                        }
                        ;

qos_match_rule_qos_level_name_start: TK_QOS_LEVEL_NAME {
                            RESET_BUFFER;
                        }
                        ;

qos_match_rule_service_id: qos_match_rule_service_id_start list_of_ranges {
                            /* 'service-id' in 'qos-match-rule' - num of instances of list of ranges */
                            if (cl_list_count(&tmp_parser_struct.num_pair_list))
                            {
                                uint64_t ** range_arr;
                                unsigned range_len;

                                __rangelist2rangearr( &tmp_parser_struct.num_pair_list,
                                                      &range_arr,
                                                      &range_len );

                                if ( !p_current_qos_match_rule->service_id_range_len )
                                {
                                    p_current_qos_match_rule->service_id_range_arr = range_arr;
                                    p_current_qos_match_rule->service_id_range_len = range_len;
                                }
                                else
                                {
                                    uint64_t ** new_range_arr;
                                    unsigned new_range_len;
                                    __merge_rangearr( p_current_qos_match_rule->service_id_range_arr,
                                                      p_current_qos_match_rule->service_id_range_len,
                                                      range_arr,
                                                      range_len,
                                                      &new_range_arr,
                                                      &new_range_len );
                                    p_current_qos_match_rule->service_id_range_arr = new_range_arr;
                                    p_current_qos_match_rule->service_id_range_len = new_range_len;
                                }
                            }
                        }
                        ;

qos_match_rule_service_id_start: TK_SERVICE_ID {
                            RESET_BUFFER;
                        }
                        ;

qos_match_rule_pkey:    qos_match_rule_pkey_start list_of_ranges {
                            /* 'pkey' in 'qos-match-rule' - num of instances of list of ranges */
                            if (cl_list_count(&tmp_parser_struct.num_pair_list))
                            {
                                uint64_t ** range_arr;
                                unsigned range_len;

                                __rangelist2rangearr( &tmp_parser_struct.num_pair_list,
                                                      &range_arr,
                                                      &range_len );

                                if ( !p_current_qos_match_rule->pkey_range_len )
                                {
                                    p_current_qos_match_rule->pkey_range_arr = range_arr;
                                    p_current_qos_match_rule->pkey_range_len = range_len;
                                }
                                else
                                {
                                    uint64_t ** new_range_arr;
                                    unsigned new_range_len;
                                    __merge_rangearr( p_current_qos_match_rule->pkey_range_arr,
                                                      p_current_qos_match_rule->pkey_range_len,
                                                      range_arr,
                                                      range_len,
                                                      &new_range_arr,
                                                      &new_range_len );
                                    p_current_qos_match_rule->pkey_range_arr = new_range_arr;
                                    p_current_qos_match_rule->pkey_range_len = new_range_len;
                                }
                            }
                        }
                        ;

qos_match_rule_pkey_start: TK_PKEY {
                            RESET_BUFFER;
                        }
                        ;


    /*
     * Common part
     */


single_string:      single_string_elems {
                        cl_list_insert_tail(&tmp_parser_struct.str_list,
                                            strdup(__parser_strip_white(tmp_parser_struct.str)));
                        tmp_parser_struct.str[0] = '\0';
                    }
                    ;

single_string_elems:  single_string_element
                    | single_string_elems single_string_element
                    ;

single_string_element: TK_TEXT {
                        strcat(tmp_parser_struct.str,$1);
                        free($1);
                    }
                    ;


string_list:        single_string
                    | string_list TK_COMMA single_string
                    ;



single_number:      number
                    ;

num_list:             number
                    | num_list TK_COMMA number
                    ;

number:             TK_NUMBER {
                        uint64_t * p_num = (uint64_t*)malloc(sizeof(uint64_t));
                        __parser_str2uint64(p_num,$1);
                        free($1);
                        cl_list_insert_tail(&tmp_parser_struct.num_list, p_num);
                    }
                    ;

num_list_with_dotdot: number_from_pair_1 TK_DOTDOT number_from_pair_2 {
                        uint64_t * num_pair = (uint64_t*)malloc(sizeof(uint64_t)*2);
                        num_pair[0] = tmp_parser_struct.num_pair[0];
                        num_pair[1] = tmp_parser_struct.num_pair[1];
                        cl_list_insert_tail(&tmp_parser_struct.num_pair_list, num_pair);
                    }
                    | num_list_with_dotdot TK_COMMA number_from_pair_1 TK_DOTDOT number_from_pair_2 {
                        uint64_t * num_pair = (uint64_t*)malloc(sizeof(uint64_t)*2);
                        num_pair[0] = tmp_parser_struct.num_pair[0];
                        num_pair[1] = tmp_parser_struct.num_pair[1];
                        cl_list_insert_tail(&tmp_parser_struct.num_pair_list, num_pair);
                    }
                    ;

number_from_pair_1:   TK_NUMBER {
                        __parser_str2uint64(&tmp_parser_struct.num_pair[0],$1);
                        free($1);
                    }
                    ;

number_from_pair_2:   TK_NUMBER {
                        __parser_str2uint64(&tmp_parser_struct.num_pair[1],$1);
                        free($1);
                    }
                    ;

list_of_ranges:     num_list_with_dash
                    ;

num_list_with_dash:   single_number_from_range {
                        uint64_t * num_pair = (uint64_t*)malloc(sizeof(uint64_t)*2);
                        num_pair[0] = tmp_parser_struct.num_pair[0];
                        num_pair[1] = tmp_parser_struct.num_pair[1];
                        cl_list_insert_tail(&tmp_parser_struct.num_pair_list, num_pair);
                    }
                    | number_from_range_1 TK_DASH number_from_range_2 {
                        uint64_t * num_pair = (uint64_t*)malloc(sizeof(uint64_t)*2);
                        if (tmp_parser_struct.num_pair[0] <= tmp_parser_struct.num_pair[1]) {
                            num_pair[0] = tmp_parser_struct.num_pair[0];
                            num_pair[1] = tmp_parser_struct.num_pair[1];
                        }
                        else {
                            num_pair[1] = tmp_parser_struct.num_pair[0];
                            num_pair[0] = tmp_parser_struct.num_pair[1];
                        }
                        cl_list_insert_tail(&tmp_parser_struct.num_pair_list, num_pair);
                    }
                    | num_list_with_dash TK_COMMA number_from_range_1 TK_DASH number_from_range_2 {
                        uint64_t * num_pair = (uint64_t*)malloc(sizeof(uint64_t)*2);
                        if (tmp_parser_struct.num_pair[0] <= tmp_parser_struct.num_pair[1]) {
                            num_pair[0] = tmp_parser_struct.num_pair[0];
                            num_pair[1] = tmp_parser_struct.num_pair[1];
                        }
                        else {
                            num_pair[1] = tmp_parser_struct.num_pair[0];
                            num_pair[0] = tmp_parser_struct.num_pair[1];
                        }
                        cl_list_insert_tail(&tmp_parser_struct.num_pair_list, num_pair);
                    }
                    | num_list_with_dash TK_COMMA single_number_from_range {
                        uint64_t * num_pair = (uint64_t*)malloc(sizeof(uint64_t)*2);
                        num_pair[0] = tmp_parser_struct.num_pair[0];
                        num_pair[1] = tmp_parser_struct.num_pair[1];
                        cl_list_insert_tail(&tmp_parser_struct.num_pair_list, num_pair);
                    }
                    ;

single_number_from_range:  TK_NUMBER {
                        __parser_str2uint64(&tmp_parser_struct.num_pair[0],$1);
                        __parser_str2uint64(&tmp_parser_struct.num_pair[1],$1);
                        free($1);
                    }
                    ;

number_from_range_1:  TK_NUMBER {
                        __parser_str2uint64(&tmp_parser_struct.num_pair[0],$1);
                        free($1);
                    }
                    ;

number_from_range_2:  TK_NUMBER {
                        __parser_str2uint64(&tmp_parser_struct.num_pair[1],$1);
                        free($1);
                    }
                    ;

%%

/***************************************************
 ***************************************************/

int osm_qos_parse_policy_file(IN osm_subn_t * const p_subn)
{
    int res = 0;
    struct stat statbuf;
    static boolean_t first_time = TRUE;
    p_qos_parser_osm_log = &p_subn->p_osm->log;

    OSM_LOG_ENTER(p_qos_parser_osm_log, osm_qos_parse_policy_file);

    osm_qos_policy_destroy(p_subn->p_qos_policy);
    p_subn->p_qos_policy = NULL;

    if (!stat(p_subn->opt.qos_policy_file, &statbuf)) {

        if (strcmp(p_subn->opt.qos_policy_file,OSM_DEFAULT_QOS_POLICY_FILE)) {
            osm_log(p_qos_parser_osm_log, OSM_LOG_ERROR,
                    "osm_qos_parse_policy_file: ERR AC01: "
                    "QoS policy file not found (%s)\n",
                    p_subn->opt.qos_policy_file);
            res = 1;
        }
        else
            osm_log(p_qos_parser_osm_log, OSM_LOG_VERBOSE,
                    "osm_qos_parse_policy_file: "
                    "QoS policy file not found (%s)\n",
                    p_subn->opt.qos_policy_file);

        goto Exit;
    }

    __qos_parser_in = fopen (p_subn->opt.qos_policy_file, "r");
    if (!__qos_parser_in)
    {
        osm_log(p_qos_parser_osm_log, OSM_LOG_ERROR,
                "osm_qos_parse_policy_file: ERR AC02: "
                "Failed opening QoS policy file (%s)\n",
                p_subn->opt.qos_policy_file);
        res = 1;
        goto Exit;
    }

    if (first_time)
    {
        first_time = FALSE;
        osm_log(p_qos_parser_osm_log, OSM_LOG_INFO,
                "osm_qos_parse_policy_file: Loading QoS policy file (%s)\n",
                p_subn->opt.qos_policy_file);
    }

    column_num = 1;
    line_num = 1;

    p_subn->p_qos_policy = osm_qos_policy_create(p_subn);

    __parser_tmp_struct_init();
    p_qos_policy = p_subn->p_qos_policy;

    res = __qos_parser_parse();

    __parser_tmp_struct_destroy();

    if (res != 0)
    {
        osm_log(p_qos_parser_osm_log, OSM_LOG_ERROR,
                "osm_qos_parse_policy_file: ERR AC03: "
                "Failed parsing QoS policy file (%s)\n",
                p_subn->opt.qos_policy_file);
        osm_qos_policy_destroy(p_subn->p_qos_policy);
        p_subn->p_qos_policy = NULL;
        res = 1;
        goto Exit;
    }

    if (osm_qos_policy_validate(p_subn->p_qos_policy,p_qos_parser_osm_log))
    {
        osm_log(p_qos_parser_osm_log, OSM_LOG_ERROR,
                "osm_qos_parse_policy_file: ERR AC04: "
                "Error(s) in QoS policy file (%s)\n",
                p_subn->opt.qos_policy_file);
        osm_qos_policy_destroy(p_subn->p_qos_policy);
        p_subn->p_qos_policy = NULL;
        res = 1;
        goto Exit;
    }

  Exit:
    if (__qos_parser_in)
        fclose(__qos_parser_in);
    OSM_LOG_EXIT(p_qos_parser_osm_log);
    return res;
}

/***************************************************
 ***************************************************/

int __qos_parser_wrap()
{
    return(1);
}

/***************************************************
 ***************************************************/

void __qos_parser_error (char *s)
{
    OSM_LOG_ENTER(p_qos_parser_osm_log, __qos_parser_error);
    osm_log(p_qos_parser_osm_log, OSM_LOG_ERROR,
            "__qos_parser_error: ERR AC05: "
            "Syntax error (line %d:%d): %s. "
            "Last text read: \"%s\"\n",
            line_num, column_num, s, __parser_strip_white(__qos_parser_text));
    OSM_LOG_EXIT(p_qos_parser_osm_log);
}

/***************************************************
 ***************************************************/

static char * __parser_strip_white(char * str)
{
   int i;
   for (i = (strlen(str)-1); i >= 0; i--)
   {
      if (isspace(str[i]))
          str[i] = '\0';
      else
         break;
   }
   for (i = 0; i < strlen(str); i++)
   {
      if (!isspace(str[i]))
         break;
   }
   return &(str[i]);
}

/***************************************************
 ***************************************************/

static void __parser_str2uint64(uint64_t * p_val, char * str)
{
#if __WORDSIZE == 64
   *p_val = strtoul(str, NULL, 0);
#else
   *p_val = strtoull(str, NULL, 0);
#endif
}

/***************************************************
 ***************************************************/

static void __parser_port_group_start()
{
    p_current_port_group = osm_qos_policy_port_group_create();
}

/***************************************************
 ***************************************************/

static int __parser_port_group_end()
{
    if(!p_current_port_group->name)
    {
        __qos_parser_error("port-group validation failed - no port group name specified");
        return -1;
    }

    cl_list_insert_tail(&p_qos_policy->port_groups,
                        p_current_port_group);
    p_current_port_group = NULL;
    return 0;
}

/***************************************************
 ***************************************************/

static void __parser_vlarb_scope_start()
{
    p_current_vlarb_scope = osm_qos_policy_vlarb_scope_create();
}

/***************************************************
 ***************************************************/

static int __parser_vlarb_scope_end()
{
    if ( !cl_list_count(&p_current_vlarb_scope->group_list) &&
         !cl_list_count(&p_current_vlarb_scope->across_list) )
    {
        __qos_parser_error("vlarb-scope validation failed - no port groups specified by 'group' or by 'across'");
        return -1;
    }

    cl_list_insert_tail(&p_qos_policy->vlarb_tables,
                        p_current_vlarb_scope);
    p_current_vlarb_scope = NULL;
    return 0;
}

/***************************************************
 ***************************************************/

static void __parser_sl2vl_scope_start()
{
    p_current_sl2vl_scope = osm_qos_policy_sl2vl_scope_create();
}

/***************************************************
 ***************************************************/

static int __parser_sl2vl_scope_end()
{
    if (!p_current_sl2vl_scope->sl2vl_table_set)
    {
        __qos_parser_error("sl2vl-scope validation failed - no sl2vl table specified");
        return -1;
    }
    if ( !cl_list_count(&p_current_sl2vl_scope->group_list) &&
         !cl_list_count(&p_current_sl2vl_scope->across_to_list) &&
         !cl_list_count(&p_current_sl2vl_scope->across_from_list) )
    {
        __qos_parser_error("sl2vl-scope validation failed - no port groups specified by 'group', 'across-to' or 'across-from'");
        return -1;
    }

    cl_list_insert_tail(&p_qos_policy->sl2vl_tables,
                        p_current_sl2vl_scope);
    p_current_sl2vl_scope = NULL;
    return 0;
}

/***************************************************
 ***************************************************/

static void __parser_qos_level_start()
{
    p_current_qos_level = osm_qos_policy_qos_level_create();
}

/***************************************************
 ***************************************************/

static int __parser_qos_level_end()
{
    if (!p_current_qos_level->sl_set)
    {
        __qos_parser_error("qos-level validation failed - no 'sl' specified");
        return -1;
    }
    if (!p_current_qos_level->name)
    {
        __qos_parser_error("qos-level validation failed - no 'name' specified");
        return -1;
    }

    cl_list_insert_tail(&p_qos_policy->qos_levels,
                        p_current_qos_level);
    p_current_qos_level = NULL;
    return 0;
}

/***************************************************
 ***************************************************/

static void __parser_match_rule_start()
{
    p_current_qos_match_rule = osm_qos_policy_match_rule_create();
}

/***************************************************
 ***************************************************/

static int __parser_match_rule_end()
{
    if (!p_current_qos_match_rule->qos_level_name)
    {
        __qos_parser_error("match-rule validation failed - no 'qos-level-name' specified");
        return -1;
    }

    cl_list_insert_tail(&p_qos_policy->qos_match_rules,
                        p_current_qos_match_rule);
    p_current_qos_match_rule = NULL;
    return 0;
}

/***************************************************
 ***************************************************/

static void __parser_tmp_struct_init()
{
    tmp_parser_struct.str[0] = '\0';
    cl_list_construct(&tmp_parser_struct.str_list);
    cl_list_init(&tmp_parser_struct.str_list, 10);
    cl_list_construct(&tmp_parser_struct.num_list);
    cl_list_init(&tmp_parser_struct.num_list, 10);
    cl_list_construct(&tmp_parser_struct.num_pair_list);
    cl_list_init(&tmp_parser_struct.num_pair_list, 10);
}

/***************************************************
 ***************************************************/

/*
 * Do NOT free objects from the temp struct.
 * Either they are inserted into the parse tree data
 * structure, or they are already freed when copying
 * their values to the parse tree data structure.
 */
static void __parser_tmp_struct_reset()
{
    tmp_parser_struct.str[0] = '\0';
    cl_list_remove_all(&tmp_parser_struct.str_list);
    cl_list_remove_all(&tmp_parser_struct.num_list);
    cl_list_remove_all(&tmp_parser_struct.num_pair_list);
}

/***************************************************
 ***************************************************/

static void __parser_tmp_struct_destroy()
{
    __parser_tmp_struct_reset();
    cl_list_destroy(&tmp_parser_struct.str_list);
    cl_list_destroy(&tmp_parser_struct.num_list);
    cl_list_destroy(&tmp_parser_struct.num_pair_list);
}

/***************************************************
 ***************************************************/

static int OSM_CDECL
__cmp_num_range(
    const void * p1,
    const void * p2)
{
    uint64_t * pair1 = *((uint64_t **)p1);
    uint64_t * pair2 = *((uint64_t **)p2);

    if (pair1[0] < pair2[0])
        return -1;
    if (pair1[0] > pair2[0])
        return 1;

    if (pair1[1] < pair2[1])
        return -1;
    if (pair1[1] > pair2[1])
        return 1;

    return 0;
}

/***************************************************
 ***************************************************/

static void __sort_reduce_rangearr(
    uint64_t  **   arr,
    unsigned       arr_len,
    uint64_t  ** * p_res_arr,
    unsigned     * p_res_arr_len )
{
    unsigned i = 0;
    unsigned j = 0;
    unsigned last_valid_ind = 0;
    unsigned valid_cnt = 0;
    uint64_t ** res_arr;
    boolean_t * is_valir_arr;

    *p_res_arr = NULL;
    *p_res_arr_len = 0;

    qsort(arr, arr_len, sizeof(uint64_t*), __cmp_num_range);

    is_valir_arr = (boolean_t *)malloc(arr_len * sizeof(boolean_t));
    is_valir_arr[last_valid_ind] = TRUE;
    valid_cnt++;
    for (i = 1; i < arr_len; i++)
    {
        if (arr[i][0] <= arr[last_valid_ind][1])
        {
            if (arr[i][1] > arr[last_valid_ind][1])
                arr[last_valid_ind][1] = arr[i][1];
            free(arr[i]);
            arr[i] = NULL;
            is_valir_arr[i] = FALSE;
        }
        else if ((arr[i][0] - 1) == arr[last_valid_ind][1])
        {
            arr[last_valid_ind][1] = arr[i][1];
            free(arr[i]);
            arr[i] = NULL;
            is_valir_arr[i] = FALSE;
        }
        else
        {
            is_valir_arr[i] = TRUE;
            last_valid_ind = i;
            valid_cnt++;
        }
    }

    res_arr = (uint64_t **)malloc(valid_cnt * sizeof(uint64_t *));
    for (i = 0; i < arr_len; i++)
    {
        if (is_valir_arr[i])
            res_arr[j++] = arr[i];
    }
    free(arr);

    *p_res_arr = res_arr;
    *p_res_arr_len = valid_cnt;
}

/***************************************************
 ***************************************************/

static void __rangelist2rangearr(
    cl_list_t    * p_list,
    uint64_t  ** * p_arr,
    unsigned     * p_arr_len)
{
    cl_list_iterator_t list_iterator;
    unsigned len = cl_list_count(p_list);
    unsigned i = 0;
    uint64_t ** tmp_arr;
    uint64_t ** res_arr = NULL;
    unsigned res_arr_len = 0;

    tmp_arr = (uint64_t **)malloc(len * sizeof(uint64_t *));

    list_iterator = cl_list_head(p_list);
    while( list_iterator != cl_list_end(p_list) )
    {
       tmp_arr[i++] = (uint64_t *)cl_list_obj(list_iterator);
       list_iterator = cl_list_next(list_iterator);
    }
    cl_list_remove_all(p_list);

    __sort_reduce_rangearr( tmp_arr,
                            len,
                            &res_arr,
                            &res_arr_len );
    *p_arr = res_arr;
    *p_arr_len = res_arr_len;
}

/***************************************************
 ***************************************************/

static void __merge_rangearr(
    uint64_t  **   range_arr_1,
    unsigned       range_len_1,
    uint64_t  **   range_arr_2,
    unsigned       range_len_2,
    uint64_t  ** * p_arr,
    unsigned     * p_arr_len )
{
    unsigned i = 0;
    unsigned j = 0;
    unsigned len = range_len_1 + range_len_2;
    uint64_t ** tmp_arr;
    uint64_t ** res_arr = NULL;
    unsigned res_arr_len = 0;

    *p_arr = NULL;
    *p_arr_len = 0;

    tmp_arr = (uint64_t **)malloc(len * sizeof(uint64_t *));

    for (i = 0; i < range_len_1; i++)
       tmp_arr[j++] = range_arr_1[i];
    for (i = 0; i < range_len_2; i++)
       tmp_arr[j++] = range_arr_2[i];
    free(range_arr_1);
    free(range_arr_2);

    __sort_reduce_rangearr( tmp_arr,
                            len,
                            &res_arr,
                            &res_arr_len );
    *p_arr = res_arr;
    *p_arr_len = res_arr_len;
}

/***************************************************
 ***************************************************/

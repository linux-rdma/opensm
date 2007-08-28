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
 *    Lexer of OSM QoS parser.
 *
 * Environment:
 *    Linux User Mode
 *
 * Author:
 *    Yevgeny Kliteynik, Mellanox
 */

#include <opensm/osm_qos_policy.h>
#include <opensm/osm_qos_parser_y.h>

#define HANDLE_IF_IN_DESCRIPTION   if (in_description) { __qos_parser_lval = strdup(__qos_parser_text); return TK_TEXT; }

#define SAVE_POS save_pos()
static void save_pos();

extern int column_num;
extern int line_num;
extern FILE * __qos_parser_in;

boolean_t in_description = FALSE;
boolean_t in_list_of_hex_num_ranges = FALSE;
boolean_t in_node_type = FALSE;
boolean_t in_list_of_numbers = FALSE;
boolean_t in_list_of_strings = FALSE;
boolean_t in_list_of_num_pairs = FALSE;
boolean_t in_asterisk_or_list_of_numbers = FALSE;
boolean_t in_list_of_num_ranges = FALSE;
boolean_t in_single_string = FALSE;
boolean_t in_single_number = FALSE;

static void reset_new_line_flags();
#define RESET_NEW_LINE_FLAGS reset_new_line_flags()

#define START_USE            {in_description = TRUE;}     /* list of strings including whitespace (description) */
#define START_PORT_GUID      {in_list_of_hex_num_ranges = TRUE;} /* comma-separated list of hex num ranges */
#define START_PORT_NAME      {in_list_of_strings = TRUE;} /* comma-separated list of following strings: ../../.. */
#define START_PARTITION      {in_single_string = TRUE;}   /* single string w/o whitespaces (partition name) */
#define START_NAME           {in_single_string = TRUE;}   /* single string w/o whitespaces (port group name) */
#define START_QOS_LEVEL_NAME {in_single_string = TRUE;}   /* single string w/o whitespaces (qos level name in match rule) */

#define START_NODE_TYPE     {in_node_type = TRUE;}       /* comma-separated list of node types (ROUTER,CA,...) */
#define START_SL2VL_TABLE   {in_list_of_numbers = TRUE;} /* comma-separated list of hex or dec numbers */

#define START_GROUP         {in_list_of_strings = TRUE;} /* list of strings w/o whitespaces (group names) */
#define START_ACROSS        {in_list_of_strings = TRUE;} /* list of strings w/o whitespaces (group names) */
#define START_ACROSS_TO     {in_list_of_strings = TRUE;} /* list of strings w/o whitespaces (group names) */
#define START_ACROSS_FROM   {in_list_of_strings = TRUE;} /* list of strings w/o whitespaces (group names) */
#define START_SOURCE        {in_list_of_strings = TRUE;} /* list of strings w/o whitespaces (group names) */
#define START_DESTINATION   {in_list_of_strings = TRUE;} /* list of strings w/o whitespaces (group names) */

#define START_VLARB_HIGH    {in_list_of_num_pairs = TRUE;} /* comma-separated list of hex or dec num pairs: "num1:num2" */
#define START_VLARB_LOW     {in_list_of_num_pairs = TRUE;} /* comma-separated list of hex or dec num pairs: "num1:num2" */

#define START_TO            {in_asterisk_or_list_of_numbers = TRUE;} /* (asterisk) or (comma-separated list of hex or dec numbers) */
#define START_FROM          {in_asterisk_or_list_of_numbers = TRUE;} /* (asterisk) or (comma-separated list of hex or dec numbers) */

#define START_PATH_BITS     {in_list_of_num_ranges = TRUE;} /* comma-separated list of hex or dec num ranges */
#define START_QOS_CLASS     {in_list_of_num_ranges = TRUE;} /* comma-separated list of hex or dec num ranges */
#define START_SERVICE_ID    {in_list_of_num_ranges = TRUE;} /* comma-separated list of hex or dec num ranges */
#define START_PKEY          {in_list_of_num_ranges = TRUE;} /* comma-separated list of hex or dec num ranges */

#define START_SL               {in_single_number = TRUE;}   /* single number */
#define START_VLARB_HIGH_LIMIT {in_single_number = TRUE;}   /* single number */
#define START_MTU_LIMIT        {in_single_number = TRUE;}   /* single number */
#define START_RATE_LIMIT       {in_single_number = TRUE;}   /* single number */
#define START_PACKET_LIFE      {in_single_number = TRUE;}   /* single number */



%}

%option nounput

PORT_GROUPS_START       port\-groups
PORT_GROUPS_END         end\-port\-groups
PORT_GROUP_START        port\-group
PORT_GROUP_END          end\-port\-group
NAME                    name
USE                     use
PORT_GUID               port\-guid
PORT_NAME               port\-name
PARTITION               partition
NODE_TYPE               node\-type
QOS_SETUP_START         qos\-setup
QOS_SETUP_END           end\-qos\-setup
VLARB_TABLES_START      vlarb\-tables
VLARB_TABLES_END        end\-vlarb\-tables
VLARB_SCOPE_START       vlarb\-scope
VLARB_SCOPE_END         end\-vlarb\-scope
GROUP                   group
ACROSS                  across
VLARB_HIGH              vlarb\-high
VLARB_LOW               vlarb\-low
VLARB_HIGH_LIMIT        vl\-high\-limit
SL2VL_TABLES_START      sl2vl\-tables
SL2VL_TABLES_END        end\-sl2vl\-tables
SL2VL_SCOPE_START       sl2vl\-scope
SL2VL_SCOPE_END         end\-sl2vl\-scope
TO                      to
FROM                    from
ACROSS_TO               across\-to
ACROSS_FROM             across\-from
SL2VL_TABLE             sl2vl\-table
QOS_LEVELS_START        qos\-levels
QOS_LEVELS_END          end\-qos\-levels
QOS_LEVEL_START         qos\-level
QOS_LEVEL_END           end\-qos\-level
SL                      sl
MTU_LIMIT               mtu\-limit
RATE_LIMIT              rate\-limit
PACKET_LIFE             packet\-life
PATH_BITS               path\-bits
QOS_MATCH_RULES_START   qos\-match\-rules
QOS_MATCH_RULES_END     end\-qos\-match\-rules
QOS_MATCH_RULE_START    qos\-match\-rule
QOS_MATCH_RULE_END      end\-qos\-match\-rule
QOS_CLASS               qos\-class
SOURCE                  source
DESTINATION             destination
SERVICE_ID              service\-id
PKEY                    pkey
QOS_LEVEL_NAME          qos\-level\-name

ROUTER                  [Rr][Oo][Uu][Tt][Ee][Rr]
CA                      [Cc][Aa]
SWITCH                  [Ss][Ww][Ii][Tt][Cc][Hh]
SELF                    [Ss][Ee][Ll][Ff]
ALL                     [Aa][Ll][Ll]

WHITE                   [ \t]+
NEW_LINE                \n
COMMENT		            \#.*\n
WHITE_DOTDOT_WHITE      [ \t]*:[ \t]*

%%


{COMMENT}               { SAVE_POS; RESET_NEW_LINE_FLAGS; } /* swallow comment */
{WHITE}{NEW_LINE}       { SAVE_POS; RESET_NEW_LINE_FLAGS; } /* trailing blanks with new line */
{WHITE}                 { SAVE_POS; HANDLE_IF_IN_DESCRIPTION; }
{NEW_LINE}              { SAVE_POS; RESET_NEW_LINE_FLAGS; }

{PORT_GROUPS_START}     { SAVE_POS; HANDLE_IF_IN_DESCRIPTION; return TK_PORT_GROUPS_START; }
{PORT_GROUPS_END}       { SAVE_POS; HANDLE_IF_IN_DESCRIPTION; return TK_PORT_GROUPS_END; }
{PORT_GROUP_START}      { SAVE_POS; HANDLE_IF_IN_DESCRIPTION; return TK_PORT_GROUP_START; }
{PORT_GROUP_END}        { SAVE_POS; HANDLE_IF_IN_DESCRIPTION; return TK_PORT_GROUP_END; }

{QOS_SETUP_START}       { SAVE_POS; HANDLE_IF_IN_DESCRIPTION; return TK_QOS_SETUP_START; }
{QOS_SETUP_END}         { SAVE_POS; HANDLE_IF_IN_DESCRIPTION; return TK_QOS_SETUP_END; }
{VLARB_TABLES_START}    { SAVE_POS; HANDLE_IF_IN_DESCRIPTION; return TK_VLARB_TABLES_START; }
{VLARB_TABLES_END}      { SAVE_POS; HANDLE_IF_IN_DESCRIPTION; return TK_VLARB_TABLES_END; }
{VLARB_SCOPE_START}     { SAVE_POS; HANDLE_IF_IN_DESCRIPTION; return TK_VLARB_SCOPE_START; }
{VLARB_SCOPE_END}       { SAVE_POS; HANDLE_IF_IN_DESCRIPTION; return TK_VLARB_SCOPE_END; }

{SL2VL_TABLES_START}    { SAVE_POS; HANDLE_IF_IN_DESCRIPTION; return TK_SL2VL_TABLES_START; }
{SL2VL_TABLES_END}      { SAVE_POS; HANDLE_IF_IN_DESCRIPTION; return TK_SL2VL_TABLES_END; }
{SL2VL_SCOPE_START}     { SAVE_POS; HANDLE_IF_IN_DESCRIPTION; return TK_SL2VL_SCOPE_START; }
{SL2VL_SCOPE_END}       { SAVE_POS; HANDLE_IF_IN_DESCRIPTION; return TK_SL2VL_SCOPE_END; }

{QOS_LEVELS_START}      { SAVE_POS; HANDLE_IF_IN_DESCRIPTION; return TK_QOS_LEVELS_START; }
{QOS_LEVELS_END}        { SAVE_POS; HANDLE_IF_IN_DESCRIPTION; return TK_QOS_LEVELS_END; }
{QOS_LEVEL_START}       { SAVE_POS; HANDLE_IF_IN_DESCRIPTION; return TK_QOS_LEVEL_START; }
{QOS_LEVEL_END}         { SAVE_POS; HANDLE_IF_IN_DESCRIPTION; return TK_QOS_LEVEL_END; }

{QOS_MATCH_RULES_START} { SAVE_POS; HANDLE_IF_IN_DESCRIPTION; return TK_QOS_MATCH_RULES_START; }
{QOS_MATCH_RULES_END}   { SAVE_POS; HANDLE_IF_IN_DESCRIPTION; return TK_QOS_MATCH_RULES_END; }
{QOS_MATCH_RULE_START}  { SAVE_POS; HANDLE_IF_IN_DESCRIPTION; return TK_QOS_MATCH_RULE_START; }
{QOS_MATCH_RULE_END}    { SAVE_POS; HANDLE_IF_IN_DESCRIPTION; return TK_QOS_MATCH_RULE_END; }

{PORT_GUID}{WHITE_DOTDOT_WHITE}        { SAVE_POS; HANDLE_IF_IN_DESCRIPTION; START_PORT_GUID;    return TK_PORT_GUID;  }
{PORT_NAME}{WHITE_DOTDOT_WHITE}        { SAVE_POS; HANDLE_IF_IN_DESCRIPTION; START_PORT_NAME;    return TK_PORT_NAME;  }
{PARTITION}{WHITE_DOTDOT_WHITE}        { SAVE_POS; HANDLE_IF_IN_DESCRIPTION; START_PARTITION;    return TK_PARTITION;  }
{NODE_TYPE}{WHITE_DOTDOT_WHITE}        { SAVE_POS; HANDLE_IF_IN_DESCRIPTION; START_NODE_TYPE;    return TK_NODE_TYPE;  }
{NAME}{WHITE_DOTDOT_WHITE}             { SAVE_POS; HANDLE_IF_IN_DESCRIPTION; START_NAME;         return TK_NAME;       }
{USE}{WHITE_DOTDOT_WHITE}              { SAVE_POS; HANDLE_IF_IN_DESCRIPTION; START_USE;          return TK_USE;        }
{GROUP}{WHITE_DOTDOT_WHITE}            { SAVE_POS; HANDLE_IF_IN_DESCRIPTION; START_GROUP;        return TK_GROUP;      }
{VLARB_HIGH}{WHITE_DOTDOT_WHITE}       { SAVE_POS; HANDLE_IF_IN_DESCRIPTION; START_VLARB_HIGH;   return TK_VLARB_HIGH; }
{VLARB_LOW}{WHITE_DOTDOT_WHITE}        { SAVE_POS; HANDLE_IF_IN_DESCRIPTION; START_VLARB_LOW;    return TK_VLARB_LOW;  }
{VLARB_HIGH_LIMIT}{WHITE_DOTDOT_WHITE} { SAVE_POS; HANDLE_IF_IN_DESCRIPTION; START_VLARB_HIGH_LIMIT; return TK_VLARB_HIGH_LIMIT;}
{TO}{WHITE_DOTDOT_WHITE}               { SAVE_POS; HANDLE_IF_IN_DESCRIPTION; START_TO;           return TK_TO;         }
{FROM}{WHITE_DOTDOT_WHITE}             { SAVE_POS; HANDLE_IF_IN_DESCRIPTION; START_FROM;         return TK_FROM;       }
{ACROSS_TO}{WHITE_DOTDOT_WHITE}        { SAVE_POS; HANDLE_IF_IN_DESCRIPTION; START_ACROSS_TO;    return TK_ACROSS_TO;  }
{ACROSS_FROM}{WHITE_DOTDOT_WHITE}      { SAVE_POS; HANDLE_IF_IN_DESCRIPTION; START_ACROSS_FROM;  return TK_ACROSS_FROM;}
{ACROSS}{WHITE_DOTDOT_WHITE}           { SAVE_POS; HANDLE_IF_IN_DESCRIPTION; START_ACROSS;       return TK_ACROSS;     }
{SL2VL_TABLE}{WHITE_DOTDOT_WHITE}      { SAVE_POS; HANDLE_IF_IN_DESCRIPTION; START_SL2VL_TABLE;  return TK_SL2VL_TABLE;}
{SL}{WHITE_DOTDOT_WHITE}               { SAVE_POS; HANDLE_IF_IN_DESCRIPTION; START_SL;           return TK_SL;         }
{MTU_LIMIT}{WHITE_DOTDOT_WHITE}        { SAVE_POS; HANDLE_IF_IN_DESCRIPTION; START_MTU_LIMIT;    return TK_MTU_LIMIT;  }
{RATE_LIMIT}{WHITE_DOTDOT_WHITE}       { SAVE_POS; HANDLE_IF_IN_DESCRIPTION; START_RATE_LIMIT;   return TK_RATE_LIMIT; }
{PACKET_LIFE}{WHITE_DOTDOT_WHITE}      { SAVE_POS; HANDLE_IF_IN_DESCRIPTION; START_PACKET_LIFE;  return TK_PACKET_LIFE;}
{PATH_BITS}{WHITE_DOTDOT_WHITE}        { SAVE_POS; HANDLE_IF_IN_DESCRIPTION; START_PATH_BITS;    return TK_PATH_BITS;  }
{QOS_CLASS}{WHITE_DOTDOT_WHITE}        { SAVE_POS; HANDLE_IF_IN_DESCRIPTION; START_QOS_CLASS;    return TK_QOS_CLASS;  }
{SOURCE}{WHITE_DOTDOT_WHITE}           { SAVE_POS; HANDLE_IF_IN_DESCRIPTION; START_SOURCE;       return TK_SOURCE;     }
{DESTINATION}{WHITE_DOTDOT_WHITE}      { SAVE_POS; HANDLE_IF_IN_DESCRIPTION; START_DESTINATION;  return TK_DESTINATION;}
{SERVICE_ID}{WHITE_DOTDOT_WHITE}       { SAVE_POS; HANDLE_IF_IN_DESCRIPTION; START_SERVICE_ID;   return TK_SERVICE_ID; }
{PKEY}{WHITE_DOTDOT_WHITE}             { SAVE_POS; HANDLE_IF_IN_DESCRIPTION; START_PKEY;         return TK_PKEY;       }
{QOS_LEVEL_NAME}{WHITE_DOTDOT_WHITE}   { SAVE_POS; HANDLE_IF_IN_DESCRIPTION; START_QOS_LEVEL_NAME; return TK_QOS_LEVEL_NAME;}

{ROUTER}                  { SAVE_POS; if (in_node_type) return TK_NODE_TYPE_ROUTER; __qos_parser_lval = strdup(__qos_parser_text); return TK_TEXT; }
{CA}                      { SAVE_POS; if (in_node_type) return TK_NODE_TYPE_CA;     __qos_parser_lval = strdup(__qos_parser_text); return TK_TEXT; }
{SWITCH}                  { SAVE_POS; if (in_node_type) return TK_NODE_TYPE_SWITCH; __qos_parser_lval = strdup(__qos_parser_text); return TK_TEXT; }
{SELF}                    { SAVE_POS; if (in_node_type) return TK_NODE_TYPE_SELF;   __qos_parser_lval = strdup(__qos_parser_text); return TK_TEXT; }
{ALL}                     { SAVE_POS; if (in_node_type) return TK_NODE_TYPE_ALL;    __qos_parser_lval = strdup(__qos_parser_text); return TK_TEXT; }

0[xX][0-9a-fA-F]+  {
                        SAVE_POS;
                        __qos_parser_lval = strdup(__qos_parser_text);
                        if (in_description || in_list_of_strings || in_single_string)
                            return TK_TEXT;
                        return TK_NUMBER;
                    }

[0-9]+              {
                        SAVE_POS;
                        __qos_parser_lval = strdup(__qos_parser_text);
                        if (in_description || in_list_of_strings || in_single_string)
                            return TK_TEXT;
                        return TK_NUMBER;
                    }


-                   {
                        SAVE_POS;
                        __qos_parser_lval = strdup(__qos_parser_text);
                        if (in_description || in_list_of_strings || in_single_string)
                            return TK_TEXT;
                        return TK_DASH;
                    }

:                   {
                        SAVE_POS;
                        __qos_parser_lval = strdup(__qos_parser_text);
                        if (in_description || in_list_of_strings || in_single_string)
                            return TK_TEXT;
                        return TK_DOTDOT;
                    }

,                   {
                        SAVE_POS;
                        __qos_parser_lval = strdup(__qos_parser_text);
                        if (in_description)
                            return TK_TEXT;
                        return TK_COMMA;
                    }

\*                  {
                        SAVE_POS;
                        __qos_parser_lval = strdup(__qos_parser_text);
                        if (in_description || in_list_of_strings || in_single_string)
                            return TK_TEXT;
                        return TK_ASTERISK;
                    }

.                   { SAVE_POS; __qos_parser_lval = strdup(__qos_parser_text); return TK_TEXT;}

%%


/*********************************************
 *********************************************/

static void save_pos()
{
    int i;
    for (i = 0; i < __qos_parser_leng; i++)
    {
        if (__qos_parser_text[i] == '\n')
        {
            line_num ++;
            column_num = 1;
        }
        else
            column_num ++;
    }
}

/*********************************************
 *********************************************/

static void reset_new_line_flags()
{
    in_description = FALSE;
    in_list_of_hex_num_ranges = FALSE;
    in_node_type = FALSE;
    in_list_of_numbers = FALSE;
    in_list_of_strings = FALSE;
    in_list_of_num_pairs = FALSE;
    in_asterisk_or_list_of_numbers = FALSE;
    in_list_of_num_ranges = FALSE;
    in_single_string = FALSE;
    in_single_number = FALSE;
}

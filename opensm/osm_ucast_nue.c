/*
 * Copyright (c) 2009-2016 ZIH, TU Dresden, Federal Republic of Germany. All rights reserved.
 * Copyright (C) 2012-2017 Tokyo Institute of Technology. All rights reserved.
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

/*!
 * \file osm_ucast_nue.c
 * \brief File containing a 100%-applicable, balanced, deadlock-free routing
 *
 * Abstract:
 *    Implementation of Nue routing for OpenSM. Nue is a deadlock-free routing
 *    engine which can be used for arbitrary network topologies and any number
 *    of virtual lanes (this includes the absense of VLs as well). The paper
 *    explaining the details of Nue routing is: [1] J. Domke, T. Hoefler and
 *    S. Matsuoka "Routing on the Dependency Graph: A New Approach to
 *    Deadlock-Free High-Performance Routing", HPDC'16. An in-depth explanation
 *    of Nue can be found in Chapter 6 of the similarly named dissertation:
 *    [2] J. Domke "Routing on the Channel Dependency Graph: A New Approach to
 *    Deadlock-Free, Destination-Based, High-Performance Routing for Lossless
 *    Interconnection Networks", 2017, Technische Universitaet Dresden
 *    (online: http://nbn-resolving.de/urn:nbn:de:bsz:14-qucosa-225902).
 *
 * \author Jens Domke
 */

#if HAVE_CONFIG_H
#include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <search.h>
#include <complib/cl_heap.h>
#include <opensm/osm_file_ids.h>
#define FILE_ID OSM_FILE_UCAST_NUE_C
#include <opensm/osm_ucast_mgr.h>
#include <opensm/osm_opensm.h>
#include <opensm/osm_node.h>
#include <opensm/osm_multicast.h>
#include <opensm/osm_mcast_mgr.h>
#if defined (ENABLE_METIS_FOR_NUE)
#include <metis.h>
#endif

/*! \def Macro for "infinity" to initialize distance in Dijkstra's algorithm. */
#define INFINITY      0x7FFFFFFF

/*! \enum Enum to identify node status in search for cycles. */
enum {
	WHITE = 0,	/*!< White color for undiscovered nodes. */
	GRAY,		/*!< Gray color for discovered nodes. */
	BLACK,		/*!< Black for nodes which cannot be part of cycle. */
};

/*! \enum Enum to identify first three statuses of nodes & edges in the cCDG. */
enum {
	BLOCKED = 0,		/*!< Forbidden, since it will induce a cycle. */
	UNUSED,			/*!< Not used by any path, yet. */
	ESCAPEPATHCOLOR,	/*!< Reserved for escape path in the cCDG. */
};

/*! \struct channel
 *  \brief Bit field identifying parts of a channel/link (lids in ib_net16_t).
 */
typedef struct channel {
	unsigned local_lid:16;	/*!< Node LID of the start point of a link. */
	unsigned local_port:8;	/*!< Node port of the start point of a link. */
	unsigned:0;		/* for alignment */
	unsigned remote_lid:16;	/*!< Node LID of the end point of a link. */
	unsigned remote_port:8; /*!< Node port of the end point of a link. */
} channel_t;

/*! \struct network_link
 *  \brief Network link with basic information and link weights for balancing.
 */
typedef struct network_link {
	channel_t link_info;	/*!< Identifies LID and port of both ends. */
	struct network_node *to_network_node;	/*!< Points to remote switch. */
	struct ccdg_node *corresponding_ccdg_node;	/*!< cCDG equivalent. */
	uint64_t weight;	/*!< Link weigths needed for path balancing. */
} network_link_t;

/*! \struct network_node
 *  \brief Network nodes are the internal representation of fabric switches.
 */
typedef struct network_node {
	/* informations of the fabric */
	ib_net16_t lid;		/*!< LID used as key to sort and to fill LFT. */
	ib_net64_t guid;	/*!< Identifier to get information from osm. */
	uint8_t num_base_terminals;	/*!< Numbers of CAs of this switch. */
	uint8_t num_terminals;	/*!< Virtual numbers of CAs (w/ lmc>0). */
	uint8_t num_links;	/*!< Number of switch-to-switch links. */
	network_link_t *links;	/*!< Array of outgoing sw-to-sw links. */
	osm_switch_t *sw;	/*!< Selfpointer into osm's switch struct. */
	boolean_t dropped;	/*!< Mark dropped switches (for ucast cache). */
	/* needed for Dijkstra's algorithm on the network */
	network_link_t *used_link;	/*!< Path found with Dijkstra's algo. */
	uint8_t hops;			/*!< Counting path length. */
	uint64_t distance;		/*!< Path length w.r.t edge weights. */
	size_t heap_index;		/*!< Helper index for the d-ary heap. */
	/* additionally needed for Dijkstra's on the cdg */
	network_link_t *escape_path;	/*!< Store fallback path for impasse. */
	uint8_t num_elem_in_link_stack;		/*!< Number links in stack. */
	network_link_t **stack_used_links;	/*!< Stack for backtracking. */
	int32_t found_after_backtracking_step;	/*!< Backtracking successful. */
	/* additionally needed for convex subgraph calculation */
	boolean_t in_convex_hull;	/*!< TRUE if switch in convex hull. */
	boolean_t processed;		/*!< Helper for graph traversal. */
	/* additionally needed for betweenness centrality calculation */
	double betw_centrality;		/*!< Measument of betweenness. */
	double delta;			/*!< Helper for betweenness calc. */
	uint64_t sigma;			/*!< Helper for betweenness calc. */
	uint8_t num_elem_in_Ps;		/*!< Helper for betweenness calc. */
	struct network_node **Ps;	/*!< Helper for betweenness calc. */
	uint8_t num_adj_terminals_in_convex_hull;	/*!< Helper for betw. */
	/* additionally needed for cCDG escape path assignment */
	boolean_t has_adj_destinations;	/*!< Add reverse path to escape path. */
} network_node_t;

/*! \struct network
 *  \brief Stores the internal subnet representation (diff. from osm internals).
 */
typedef struct network {
	uint16_t num_nodes;	/*!< Number of switches in the subnet. */
	network_node_t *nodes;	/*!< Array with all switches of the subnet. */
	cl_heap_t heap;		/*!< Heap object for faster Dijkstra's algo. */
} network_t;

/*! \struct color
 *  \brief Colors are used to identify disjoint acyclic subgraphs of the cCDG.
 */
typedef struct color {
	uint16_t color_id;	/*!< ID for the 'color' value. */
	struct color *real_col; /*!< Ptr to real color after merging graphs. */
} color_t;

/*! \struct ccdg_edge
 *  \brief Object representing an edge of the complete channel dependency graph.
 */
typedef struct ccdg_edge {
	channel_t to_channel_id;	/*!< Identifies tail vertex of edge. */
	struct ccdg_node *to_ccdg_node;	/*!< Pointer to tail vertex of edge. */
	/* color coding to easily identify if cycle search is needed */
	color_t *color;			/*!< Pointer to current coloring. */
	boolean_t wet_paint;		/*!< TRUE if color changed recently. */
} ccdg_edge_t;

/*! \struct ccdg_node
 *  \brief Object describing a vertex of the complete channel dependency graph.
 */
typedef struct ccdg_node {
	/* channel_id is similar to a guid of a node */
	channel_t channel_id;	/*!< Identifies LID and port of both ends. */
	uint8_t num_edges;	/*!< Number of edges attached to this vertex. */
	ccdg_edge_t *edges;	/*!< Array of edges or channel dependencies. */
	/* needed for dijkstra's algorithm on the cdg */
	network_link_t *corresponding_netw_link;	/*!< For fast access. */
	uint64_t distance;	/*!< Current path length w.r.t edge weights. */
	size_t heap_index;	/*!< Helper index for the d-ary heap. */
	/* color coding to easily identify if cycle search is needed */
	color_t *color;		/*!< Pointer to current coloring. */
	boolean_t wet_paint;	/*!< TRUE if color changed in this iteration. */
	/* for cycle search in cdg */
	uint8_t status;		/*!< Helper for iterative cycle search. */
	uint8_t next_edge_idx;	/*!< Save next edge to check after using pre. */
	struct ccdg_node *pre;	/*!< Track traversal in cycle search in cCDG. */
} ccdg_node_t;

/*! \struct ccdg
 *  \brief Stores the complete channel dependency graph (nodes, edges, etc).
 */
typedef struct ccdg {
	uint32_t num_nodes;	/*!< NUmber of nodes in the complete CDG. */
	ccdg_node_t *nodes;	/*!< Array storing nodes of the complete CDG. */
	uint32_t num_colors;	/*!< Size of the color array. */
	color_t *color_array;	/*!< Distinguish disjoint acyclic sub-CDGs. */
	cl_heap_t heap;		/*!< Heap object for faster Dijkstra's algo. */
} ccdg_t;

/*! \struct backtracking_candidate
 *  \brief Stores potential alternative paths in the local backtracking step.
 */
typedef struct backtracking_candidate {
	network_link_t *link_to_adj_netw_node;
	ccdg_node_t *orig_used_ccdg_node_for_adj_netw_node;
} backtracking_candidate_t;

/*! \struct nue_context
 *  \brief Primary structure for Nue (storing graph, cCDG, destinations, etc).
 */
typedef struct nue_context {
	/* external parts */
	osm_routing_engine_type_t routing_type;	/*!< Name of routing engine. */
	osm_ucast_mgr_t *mgr;	/*!< Pointer to osm management object. */
	/* internal parts */
	network_t network;	/*!< Network object storing fabric copy. */
	ccdg_t ccdg;		/*!< Complete CDG object for the fabric. */
	uint16_t num_destinations[IB_MAX_NUM_VLS];	/*!< Number of desti. */
	ib_net16_t *destinations[IB_MAX_NUM_VLS];	/*!< Array of desti. */
	uint8_t max_vl;		/*!< Highest common #VL supported by all. */
	uint8_t max_lmc;	/*!< Highest supported LMC across fabric. */
	uint8_t *dlid_to_vl_mapping;	/*!< Store VLs to serve path_sl requ. */
} nue_context_t;

#if defined (ENABLE_METIS_FOR_NUE)
/*! \struct metis_context
 *  \brief Complete information about fabric graph to perform partitioning.
 */
typedef struct metis_context {
	idx_t nvtxs[1];		/*!< Number of vertices in the graph. */
	idx_t ncon[1];		/*!< Number of balancing constraints. */
	idx_t *xadj;		/*!< Number of adjacent nodes per node. */
	idx_t *adjncy;		/*!< Array of adjacent nodes per node. */
	idx_t nparts[1];	/*!< Number of parts to split the graph into. */
	idx_t options[METIS_NOPTIONS];	/*!< Array of METIS options. */
	idx_t objval[1];	/*!< Stores the edge-cut of the partitioning. */
	idx_t *part;		/*!< Stores partitioning vector of the graph. */
} metis_context_t;
#endif

/*************** predefine all internal functions *********************
 **********************************************************************/
/*! \fn add_ccdg_edge_betw_nodes_to_colored_subccdg(const ccdg_t *,
 *                                                  const ccdg_node_t *,
 *                                                  const ccdg_node_t *,
 *                                                  ccdg_edge_t *)
 *  \brief This fn changes the color pointer of a cCDG edge to the same color
 *         as the input ccdg_node1 and sets its wet_paint flag to TRUE.
 *
 *  \param[in]     ccdg       The cCDG object.
 *  \param[in]     ccdg_node1 A vertex of the cCDG.
 *  \param[in]     ccdg_node2 An adjacent vertex in the cCDG.
 *  \param[in,out] ccdg_edge  The edge from ccdg_node1 to ccdg_node2 vertex, or
 *                            NULL which causes the fn to search for the edge.
 *                            The color of this edge will be changed.
 *  \return NONE
 */
static inline void
add_ccdg_edge_betw_nodes_to_colored_subccdg(const ccdg_t *,
					    const ccdg_node_t *,
					    const ccdg_node_t *,
					    ccdg_edge_t *);

/*! \fn add_ccdg_node_to_colored_subccdg(const ccdg_t *,
 *                                       const ccdg_node_t *,
 *                                       ccdg_node_t *)
 *  \brief This fn colors ccdg_node2 in the same color as ccdg_node1 by changing
 *         its color pointer, and also colors the edge in between them.
 *
 *  \param[in]     ccdg       The cCDG object.
 *  \param[in]     ccdg_node1 A vertex of the cCDG.
 *  \param[in,out] ccdg_node2 An adjacent vertex in the cCDG. The color of this
 *                            vertex will be changed.
 *  \return NONE
 */
static inline void
add_ccdg_node_to_colored_subccdg(const ccdg_t *,
				 const ccdg_node_t *,
				 ccdg_node_t *);

/*! \fn add_link_to_stack_of_used_links(network_node_t *,
 *                                      network_link_t *)
 *  \brief Appends the input link to the stack contained in the network node
 *         but only if the link is not already in the stack.
 *
 *  \param[in,out] network_node A network node (switch) object of Nue.
 *  \param[in]      link         A link whose tail ends in this network_node.
 *  \return NONE
 */
static inline void
add_link_to_stack_of_used_links(network_node_t *,
				network_link_t *);

/*! \fn attempt_local_backtracking(const osm_ucast_mgr_t *,
 *                                 const network_t *,
 *                                 const network_node_t *,
 *                                 const ccdg_t *,
 *                                 const int32_t)
 *  \brief Check alternative paths within a small radius to find and use valid
 *         channel dependencies which won't close a cycle in the cCDG.
 *
 *  Function description: A detailed description of this function can be found
 *  in Section 6.2.6.2 / Algorithm 6.5 of reference [2] (see abstract at the
 *  begining of this file).
 *
 *  \param[in] mgr              The management object of OpenSM.
 *  \param[in] network          Nue's network object storing the subnet.
 *  \param[in] source_netw_node Current source for Dijkstra's on cCDG should be
 *                              discarded from the list of candidates.
 *  \param[in] ccdg             Nue's internal object storing the complete CDG.
 *  \param[in] color            Color of current iteration to prevent cycles.
 *  \return Pointer to a cCDG node if the backtracking was successful, or NULL
 *          otherwise.
 */
static ccdg_node_t *
attempt_local_backtracking(const osm_ucast_mgr_t *,
			   const network_t *,
			   const network_node_t *,
			   const ccdg_t *,
			   const int32_t);

/*! \fn attempt_shortcut_discovery(const osm_ucast_mgr_t *,
 *                                 const network_t *,
 *                                 const network_node_t *,
 *                                 const ccdg_t *,
 *                                 const ccdg_node_t *,
 *                                 const int32_t color)
 *  \brief Check for alternative paths or shortcuts through the cCDG (w.r.t
 *         weight-based distance) after successfully solving an impasse.
 *
 *  Function description: A detailed description of this function can be found
 *  in Section 6.2.6.3 / Algorithm 6.6 of reference [2] (see abstract at the
 *  begining of this file).
 *
 *  \param[in] mgr     The management object of OpenSM.
 *  \param[in] network Nue's network object storing the subnet.
 *  \param[in] potential_shortcut_netw_node Former undiscovered node.
 *  \param[in] ccdg    Nue's internal object storing the complete CDG.
 *  \param[in] potential_shortcut_ccdg_node Used cCDG vertex to reach former
 *                                          undiscovered node.
 *  \param[in] color   Color of current iteration to prevent cycles.
 *  \return TRUE is a shortcut was found, or FALSE otherwise.
 */
static boolean_t
attempt_shortcut_discovery(const osm_ucast_mgr_t *,
			   const network_t *,
			   const network_node_t *,
			   const ccdg_t *,
			   const ccdg_node_t *,
			   const int32_t color);

/*! \fn build_complete_cdg(const osm_ucast_mgr_t *,
 *                         const network_t *,
 *                         ccdg_t *,
 *                         const uint32_t)
 *  \brief Parses the subnet and constructs the complete channel dependency
 *         graph for this subnet while considering all switch-to-switch links.
 *
 *  Function description: Conceptually, the complete channel dependency graph
 *  is very similar to the commonly known channel dependency graph (CDG), which
 *  is created by following calculated routes and connecting vertices (channels
 *  representing subnet links) if and only if the two corresponding links are
 *  used by a path from source to target. However, the complete CDG (cCDG) does
 *  not require actual paths (or assumes every possible path) and connects all
 *  pairs of cCDG vertices when the corresponding two links are attached to the
 *  same network switch.
 *
 *  \param[in]     mgr     The management object of OpenSM.
 *  \param[in]     network Nue's network object storing the subnet.
 *  \param[in,out] ccdg    Nue's internal object storing the complete CDG.
 *  \param[in]     total_num_sw_to_sw_links Number of switch-to-switch links.
 *  \return Integer 0 for sucessful cCDG creation, or any integer unequal to 0
 *          otherwise.
 */
static int
build_complete_cdg(const osm_ucast_mgr_t *,
		   const network_t *,
		   ccdg_t *,
		   const uint32_t);

/*! \fn calculate_convex_subnetwork(const osm_ucast_mgr_t *,
 *                                  const network_t *,
 *                                  ib_net16_t *,
 *                                  const uint16_t)
 *  \brief The fn determines the convex hull of a subset of network nodes.
 *
 *  Function description: The function determines the convex hull of a subset of
 *  nodes of the network. This convex hull is the enclosure of all shortest
 *  paths between these nodes, therefore we calculate a spanning tree from each
 *  node and which is traversed in the opposite direction to collect all nodes
 *  along the shortest paths. The result is the assignment of TRUE to the
 *  in_convex_hull flag for each node in the convex hull, and FALSE otherwise.
 *
 *  \param[in] mgr              The management object of OpenSM.
 *  \param[in] network          Nue's network object storing the subnet.
 *  \param[in] destinations     Destinations for the routing in the current VL.
 *  \param[in] num_destinations Number of destinations in the destination array.
 *  \return Integer 0 if calculation was sucessful, or any integer unequal to 0
 *          otherwise.
 */
static int
calculate_convex_subnetwork(const osm_ucast_mgr_t *,
			    const network_t *,
			    ib_net16_t *,
			    const uint16_t);

/*! \fn calculate_spanning_tree_in_network(const osm_ucast_mgr_t *,
 *                                         network_t *,
 *                                         network_node_t *)
 *  \brief Calculates a trivial spanning tree for the network.
 *
 *  Function description: The fn operates similar to Dijkstra's algorithm and
 *  the spanning tree calculation also includes the link weights. The reason
 *  to consider these link weights is that we don't end up with similar escape
 *  paths for each virtual layer, since weights change after each routing step.
 *  The spanning tree is temporarily stored in the escape_path parameter of
 *  each network node.
 *
 *  \param[in] mgr       The management object of OpenSM.
 *  \param[in] network   Nue's network object storing the subnet.
 *  \param[in] root_node A network node defining the root of the spanning tree.
 *  \return Integer 0 if calculation was sucessful, or any integer unequal to 0
 *          otherwise.
 */
static int
calculate_spanning_tree_in_network(const osm_ucast_mgr_t *,
				   network_t *,
				   network_node_t *);

/*! \fn change_fake_ccdg_node_color(const ccdg_t *,
 *                                  ccdg_node_t *,
 *                                  const int32_t)
 *  \brief Assigns the right color (colored subgraph) to a temporarily added
 *         fake channel in the cCDG, which acts as source for Dijkstra's algo.
 *
 *  \param[in] ccdg      Nue's internal object storing the complete CDG.
 *  \param[in] ccdg_node A fake node which acts as Dijkstra's source.
 *  \param[in] color     Color of current iteration to prevent cyclic subgraphs.
 *  \return NONE
 */
static inline void
change_fake_ccdg_node_color(const ccdg_t *,
			    ccdg_node_t *,
			    const int32_t);

/*! \fn compare_backtracking_candidates_by_distance(const void *,
 *                                                  const void *)
 *  \brief Comparator for backtracking candidates of cCDG vertices w.r.t their
 *         distance parameter. Assumed input type: `backtracking_candidate_t *'
 *
 *  \param[in] btc1 A cCDG vertex.
 *  \param[in] btc2 A second cCDG vertex.
 *  \return Negative value for param1 < param2, positive for '>', or 0 for '='.
 */
static inline int
compare_backtracking_candidates_by_distance(const void *,
					    const void *);

/*! \fn compare_ccdg_nodes_by_channel_id(const void *,
 *                                       const void *)
 *  \brief Comparator for cCDG vertices w.r.t their assigned channel identifyer
 *         (LIDs/ports combination). Assumed input type: `ccdg_node_t *'
 *
 *  \param[in] cn1 A cCDG vertex.
 *  \param[in] cn2 A second cCDG vertex.
 *  \return Negative value for param1 < param2, positive for '>', or 0 for '='.
 */
static inline int
compare_ccdg_nodes_by_channel_id(const void *,
				 const void *);

/*! \fn compare_lids(const void *,
 *                   const void *)
 *  \brief Comparator for two subnet local identifiers (LID). Assumed input
 *         type: `ib_net16_t *'
 *
 *  \param[in] l1 A LID of a network component (CA or switch).
 *  \param[in] l2 A second LID of a network component.
 *  \return Negative value for param1 < param2, positive for '>', or 0 for '='.
 */
static int
compare_lids(const void *,
	     const void *);

/*! \fn compare_network_nodes_by_lid(const void *,
 *                                   const void *)
 *  \brief Comparator of two network nodes (switches) w.r.t their LIDs. Assumed
 *         input type: `network_node_t *'
 *
 *  \param[in] n1 A network node of Nue's internal network object.
 *  \param[in] n2 A second network node.
 *  \return Negative value for param1 < param2, positive for '>', or 0 for '='.
 */
static int
compare_network_nodes_by_lid(const void *,
			     const void *);

/*! \fn compare_two_channel_id(const void *,
 *                             const void *)
 *  \brief Comparator of two channel IDs (bit field identifying all parts of a
 *         link). Assumed input type: `channel_t *'
 *
 *  \param[in] c1 A channel identifier.
 *  \param[in] c2 A second channel ID.
 *  \return Negative value for param1 < param2, positive for '>', or 0 for '='.
 */
static int
compare_two_channel_id(const void *,
		       const void *);

/*! \fn construct_ccdg(ccdg_t *)
 *  \brief Set all ccdg_t struct parameters to 0, and call cl_heap_construct
 *         for the heap element in the struct afterwards.
 *
 *  \param[in,out] ccdg Nue's internal object supposed to store a complete CDG.
 *  \return NONE
 */
static inline void
construct_ccdg(ccdg_t *);

/*! \fn construct_ccdg_edge(ccdg_edge_t *)
 *  \brief Set all ccdg_edge_t struct parameters to 0.
 *
 *  \param[in,out] edge An edge of the complete channel dependency graph.
 *  \return NONE
 */
static inline void
construct_ccdg_edge(ccdg_edge_t *);

/*! \fn construct_ccdg_node(ccdg_node_t *)
 *  \brief Set all ccdg_node_t struct parameters to 0.
 *
 *  \param[in,out] node A vertex of the complete channel dependency graph.
 *  \return NONE
 */
static inline void
construct_ccdg_node(ccdg_node_t *);

#if defined (ENABLE_METIS_FOR_NUE)
/*! \fn construct_metis_context(metis_context_t *)
 *  \brief Set all metis_context_t struct parameters to 0.
 *
 *  \param[in,out] metis_ctx The context which will hold the input for METIS.
 *  \return NONE
 */
static inline void
construct_metis_context(metis_context_t *);
#endif

/*! \fn construct_network_link(network_link_t *)
 *  \brief Set all network_link_t struct parameters to 0.
 *
 *  \param[in,out] link A link between two network nodes.
 *  \return NONE
 */
static inline void
construct_network_link(network_link_t *);

/*! \fn construct_network(network_t *)
 *  \brief Set all network_t struct parameters to 0, and call cl_heap_construct
 *         for the heap element in the struct afterwards.
 *
 *  \param[in,out] network Nue's network object supposed to store the subnet.
 *  \return NONE
 */
static inline void
construct_network(network_t *);

/*! \fn construct_network_node(network_node_t *)
 *  \brief Set all network_node_t struct parameters to 0.
 *
 *  \param[in,out] node A network node of Nue's internal subnet representation.
 *  \return NONE
 */
static inline void
construct_network_node(network_node_t *);

/*! \fn create_context(nue_context_t *)
 *  \brief This fn calls the constructors for the network and ccdg structs, as
 *         well as allocates arrays to store destinations and VL mappings.
 *
 *  \param[in,out] nue_ctx Nue's context storing graph, cCDG, destinations, etc.
 *  \return Integer 0 if context initialization was sucessful, or any integer
 *          unequal to 0 otherwise.
 */
static int
create_context(nue_context_t *);

/*! \fn destroy_ccdg(ccdg_t *)
 *  \brief All allocated memory within the ccdg_t struct is freed, and
 *         cl_heap_destroy is called for the heap element in the struct.
 *
 *  \param[in,out] ccdg Nue's internal object storing the complete CDG.
 *  \return NONE
 */
static inline void
destroy_ccdg(ccdg_t *);

/*! \fn destroy_ccdg_node(ccdg_node_t *)
 *  \brief All allocated memory within the ccdg_node_t struct is freed.
 *
 *  \param[in,out] node A vertex of the complete channel dependency graph.
 *  \return NONE
 */
static inline void
destroy_ccdg_node(ccdg_node_t *);

/*! \fn destroy_context(nue_context_t *)
 *  \brief All allocated memory within the nue_context_t struct is freed.
 *
 *  \param[in,out] nue_ctx Nue's context storing graph, cCDG, destinations, etc.
 *  \return NONE
 */
static void
destroy_context(nue_context_t *);

#if defined (ENABLE_METIS_FOR_NUE)
/*! \fn destroy_metis_context(metis_context_t *)
 *  \brief All allocated memory within the metis_context_t struct is freed.
 *
 *  \param[in,out] metis_ctx The context which holds the input arrays for METIS.
 *  \return NONE
 */
static inline void
destroy_metis_context(metis_context_t *);
#endif

/*! \fn destroy_network(network_t *)
 *  \brief All allocated memory within the network_t struct is freed, and
 *         cl_heap_destroy is called for the heap element in the struct.
 *
 *  \param[in,out] network Nue's network object storing the subnet.
 *  \return NONE
 */
static inline void
destroy_network(network_t *);

/*! \fn destroy_network_node(network_node_t *)
 *  \brief All allocated memory within the network_node_t struct is freed.
 *
 *  \param[in,out] node A network node (switch) object of Nue.
 *  \return NONE
 */
static inline void
destroy_network_node(network_node_t *);

/*! \fn determine_num_adj_terminals_in_convex_hull(const osm_ucast_mgr_t *,
 *                                                 const network_t *,
 *                                                 ib_net16_t *,
 *                                                 const uint16_t)
 *  \brief Counting the number of terminals (CAs) attached to each switch which
 *         itself is member of the convex hull (LMC ignored here).
 *
 *  \param[in] mgr              The management object of OpenSM.
 *  \param[in] network          Nue's network object storing the subnet.
 *  \param[in] destinations     Destinations for the routing in the current VL.
 *  \param[in] num_destinations Number of destinations in the destination array.
 *  \return NONE
 */
static void
determine_num_adj_terminals_in_convex_hull(const osm_ucast_mgr_t *,
					   const network_t *,
					   ib_net16_t *,
					   const uint16_t);

/*! \fn distribute_lids_onto_virtual_layers(nue_context_t *,
 *                                          const boolean_t)
 *  \brief This fn assigns destination LIDs to different virtual layers.
 *
 *  Function description: The fn is redirecting the distribution of routing
 *  destination to either distribute_lids_with_metis if METIS was found during
 *  OpenSM installation, or distribute_lids_semi_randomly otherwise.
 *
 *  \param[in] nue_ctx    Nue's context storing graph, cCDG, destinations, etc.
 *  \param[in] include_sw Whether or not to consider switches as destinations.
 *  \return Integer 0 if distribution was sucessful, or any integer unequal to 0
 *          otherwise.
 */
static inline int
distribute_lids_onto_virtual_layers(nue_context_t *,
				    const boolean_t);

#if defined (ENABLE_METIS_FOR_NUE)
/*! \fn distribute_lids_with_metis(nue_context_t *,
 *                                 const boolean_t)
 *  \brief This fn uses METIS to partition the subnet into #VL parts and then
 *         assigns LIDs to different layers according to the partitioning.
 *
 *  \param[in] nue_ctx    Nue's context storing graph, cCDG, destinations, etc.
 *  \param[in] include_sw Whether or not to consider switches as destinations.
 *  \return Integer 0 if distribution was sucessful, or any integer unequal to 0
 *          otherwise.
 */
static int
distribute_lids_with_metis(nue_context_t *,
			   const boolean_t);
#else
/*! \fn distribute_lids_semi_randomly(nue_context_t *,
 *                                    const boolean_t)
 *  \brief This fn randomly assigns destination to different virtual layers.
 *
 *  \param[in] nue_ctx    Nue's context storing graph, cCDG, destinations, etc.
 *  \param[in] include_sw Whether or not to consider switches as destinations.
 *  \return Integer 0 if distribution was sucessful, or any integer unequal to 0
 *          otherwise.
 */
static int
distribute_lids_semi_randomly(nue_context_t *,
			      const boolean_t);
#endif

/*! \fn dry_ccdg_edge_color_betw_nodes(const ccdg_t *,
 *                                     const ccdg_node_t *,
 *                                     const ccdg_node_t *)
 *  \brief Change the status of the color for a cCDG edge from temporarily to
 *         permanent, which we call 'drying' the color.
 *
 *  \param[in] ccdg       Nue's internal object storing the complete CDG.
 *  \param[in] ccdg_node1 A vertex of the cCDG.
 *  \param[in] ccdg_node2 An adjacent vertex in the cCDG.
 *  \return NONE
 */
static inline void
dry_ccdg_edge_color_betw_nodes(const ccdg_t *,
			       const ccdg_node_t *,
			       const ccdg_node_t *);

/*! \fn dry_ccdg_node_color(ccdg_node_t *)
 *  \brief Change the status of the color for a cCDG vertex from temporarily to
 *         permanent, which we call 'drying' the color.
 *
 *  \param[in,out] ccdg_node A vertex of the complete CDG.
 *  \return NONE
 */
static inline void
dry_ccdg_node_color(ccdg_node_t *);

/*! \fn fix_ccdg_colors(const osm_ucast_mgr_t *,
 *                      const network_t *,
 *                      const network_node_t *,
 *                      const ccdg_t *,
 *                      const ccdg_node_t *)
 *  \brief Change the status of all colors of cCDG vertices and edges, which are
 *         actually used after the routing step, from temporarily to permanent.
 *
 *  \param[in] mgr              The management object of OpenSM.
 *  \param[in] network          Nue's network object storing the subnet.
 *  \param[in] source_netw_node Current source node for Dijkstra's on the cCDG.
 *  \param[in] ccdg             Nue's internal object storing the complete CDG.
 *  \param[in] source_ccdg_node Current source cCDG vertex for Dijkstra's algo.
 *  \return NONE
 */
static void
fix_ccdg_colors(const osm_ucast_mgr_t *,
		const network_t *,
		const network_node_t *,
		const ccdg_t *,
		const ccdg_node_t *);

/*! \fn fix_ccdg_edge_color(ccdg_edge_t *)
 *  \brief Overwrite the (tmp) color pointer of the cCDG edge with the real
 *         color after merging individually colored cCDG subgraphs.
 *
 *  \param[in,out] ccdg_edge An edge of the cCDG.
 *  \return NONE
 */
static inline void
fix_ccdg_edge_color(ccdg_edge_t *);

/*! \fn fix_ccdg_node_color(ccdg_node_t *)
 *  \brief Overwrite the (tmp) color pointer of the cCDG vertex with the real
 *         color after merging individually colored cCDG subgraphs.
 *
 *  \param[in,out] ccdg_node A vertex of the cCDG.
 *  \return NONE
 */
static inline void
fix_ccdg_node_color(ccdg_node_t *);

/*! \fn found_path_between_ccdg_nodes_in_subgraph(const osm_ucast_mgr_t *,
 *                                                const ccdg_t *,
 *                                                ccdg_node_t *,
 *                                                const ccdg_node_t *,
 *                                                const int32_t)
 *  \brief Search an specifically colored subgraph of the complete CDG for a
 *         path from a given source vertex to an target vertex.
 *
 *  \param[in] mgr    The management object of OpenSM.
 *  \param[in] ccdg   Nue's internal object storing the complete CDG.
 *  \param[in] source A source vertex in the cCDG to start the search.
 *  \param[in] target A target vertex of the cCDG we are looking for.
 *  \param[in] color  The color of ID of the subgraph which should be searched.
 *  \return TRUE if path from source to target was found, or FALSE otherwise.
 */
static boolean_t
found_path_between_ccdg_nodes_in_subgraph(const osm_ucast_mgr_t *,
					  const ccdg_t *,
					  ccdg_node_t *,
					  const ccdg_node_t *,
					  const int32_t);

/*! \fn get_base_lids_and_number_of_lids(nue_context_t *)
 *  \brief Count the total number of CAs (or LIDs for lmc>0) in the fabric.
 *
 *  Function description: The resulting number even includes base/enhanced
 *  switch port 0 (base SP0 will have lmc=0), hence total number of LIDs.
 *  All found LIDs (regardless of CA or switch) are stored for later use in the
 *  nue_ctx->destinations[0] array which will be post-processed by other fn.
 *
 *  \param[in,out] nue_ctx Nue's context storing graph, cCDG, destinations, etc.
 *  \return Total number of LIDs in the fabric.
 */
static uint64_t
get_base_lids_and_number_of_lids(nue_context_t *);

/*! \fn get_ccdg_edge_betw_nodes(const ccdg_node_t *,
 *                               const ccdg_node_t *)
 *  \brief This is a helper fn to access cCDG edges, iterating over the input
 *         vertex1's edge ist searching for the input vertex2.
 *
 *  \param[in] ccdg_node1 A cCDG vertex.
 *  \param[in] ccdg_node2 A second cCDG vertex.
 *  \return Pointer to the cCDG edge between the input cCDG nodes, or NULL if
 *          none exist because these two nodes are not adjacent.
 */
static ccdg_edge_t *
get_ccdg_edge_betw_nodes(const ccdg_node_t *,
			 const ccdg_node_t *);

/*! \fn get_ccdg_edge_color_betw_nodes(const ccdg_t *,
 *                                     const ccdg_node_t *,
 *                                     const ccdg_node_t *,
 *                                     ccdg_edge_t *)
 *  \brief The fn determines the currently assigned color for an cCDG edge (if
 *         NULL, then search for the correct edge begtween the given vertices).
 *
 *  Function description: Caller must make sure the edge exists and/or the
 *  two input cCDG vertices are actually adjacent (with edge: vertex1->vertex2)
 *  since no verification is performed when OpenSM is executed in non-debug
 *  mode. Hence, a careless approach may lead to segmentation faults.
 *
 *  \param[in] ccdg       Nue's internal object storing the complete CDG.
 *  \param[in] ccdg_node1 A cCDG vertex.
 *  \param[in] ccdg_node2 A second cCDG vertex.
 *  \param[in] ccdg_edge  An edge of the complete CDG, or NULL.
 *  \return The color ID associated with the cCDG edge between the input nodes.
 */
static inline uint16_t
get_ccdg_edge_color_betw_nodes(const ccdg_t *,
			       const ccdg_node_t *,
			       const ccdg_node_t *,
			       ccdg_edge_t *);

/*! \fn get_ccdg_edge_color(const ccdg_t *,
 *                          const ccdg_edge_t *)
 *  \brief The fn returns the currently assigned color ID for an cCDG edge.
 *
 *  \param[in] ccdg      Nue's internal object storing the complete CDG.
 *  \param[in] ccdg_edge An edge of the complete CDG.
 *  \return The color ID associated with an input cCDG edge.
 */
static inline uint16_t
get_ccdg_edge_color(const ccdg_t *,
		    const ccdg_edge_t *);

/*! \fn get_ccdg_node_by_channel_id(const ccdg_t *,
 *                                  const channel_t)
 *  \brief This fn uses the stdlib to find (via binary search) a cCDG vertex
 *         in a sorted array of vertices.
 *
 *  \param[in] ccdg Nue's internal object storing the complete CDG.
 *  \param[in] c_id Channel ID bit field identifying all parts of a subnet link.
 *  \return Pointer to the cCDG node associated with the channe ID (LIDs/ports
 *          combination), or NULL if no match is found.
 */
static inline ccdg_node_t *
get_ccdg_node_by_channel_id(const ccdg_t *,
			    const channel_t);

/*! \fn get_ccdg_node_color(const ccdg_t *,
 *                          const ccdg_node_t *)
 *  \brief The fn returns the currently assigned color ID for a cCDG vertex.
 *
 *  \param[in] ccdg      Nue's internal object storing the complete CDG.
 *  \param[in] ccdg_node A vertex of the complete channel dependency graph.
 *  \return The color ID associated with an input cCDG node.
 */
static inline uint16_t
get_ccdg_node_color(const ccdg_t *,
		    const ccdg_node_t *);

/*! \fn get_central_node_wrt_subnetwork(const osm_ucast_mgr_t *,
 *                                      const network_t *,
 *                                      ib_net16_t *,
 *                                      const uint16_t,
 *                                      network_node_t **,
 *                                      uint16_t *)
 *  \brief This fn implements a slightly modified version of Brandes' algorithms
 *         for betweenness centrality.
 *
 *  Function description: The slightly modified calculation of the betweenness
 *  centrality problem is based on the algorithm described by U. Brandes "A
 *  Faster Algorithm for Betweenness Centrality" in Journal of Mathematical
 *  Sociology, 2001. We calculate the betweenness value only for switches of
 *  the subnet, since terminals shouldn't be the most central node w.r.t a
 *  convex hull anyways.
 *
 *  \param[in]  mgr                The management object of OpenSM.
 *  \param[in]  network            Nue's network object storing the subnet.
 *  \param[in]  destinations       Destinations for routing in the current VL.
 *  \param[in]  num_destinations   Number of destinations in the array.
 *  \param[out] central_node       Pointer to the network node determined as
 *                                 most central w.r.t the given convex hull.
 *  \param[out] central_node_index Index of this most central node in the node
 *                                 array of the network object.
 *  \return Integer 0 if calculation was sucessful, or any integer unequal to 0
 *          otherwise.
 */
static int
get_central_node_wrt_subnetwork(const osm_ucast_mgr_t *,
				const network_t *,
				ib_net16_t *,
				const uint16_t,
				network_node_t **,
				uint16_t *);

/*! \fn get_inverted_channel_id(const channel_t)
 *  \brief Swap the elements of an channel ID (LID1|P1->LID2|P2) to determine
 *         the reverse channel (LID2|P2->LID1|P1) of it.
 *
 *  \param[in] in_channel Channel ID bit field identifying all parts of a link.
 *  \return A new channel ID with inverted head/tail of the corresponding link.
 */
static inline channel_t
get_inverted_channel_id(const channel_t);

/*! \fn get_lid(const ib_net16_t *,
 *              const uint32_t,
 *              const ib_net16_t)
 *  \brief This fn uses the stdlib to find (via binary search) a subnet LID
 *         in a sorted array of (destinations) LIDs.
 *
 *  \param[in] lid_array Sorted array of LIDs to perform the search on.
 *  \param[in] num_lids  Number of LIDs in the given array.
 *  \param[in] lid       Target LID to search for.
 *  \return Pointer to a LID in the array of destinations.
 */
static inline ib_net16_t *
get_lid(const ib_net16_t *,
	const uint32_t,
	const ib_net16_t);

/*! \fn get_max_num_vls(const osm_ucast_mgr_t *)
 *  \brief This fn determines the larges number of VLs, supported by all nodes
 *  in the subnet, or returns the user supplied number (if it is smaller).
 *
 *  \param[in] mgr The management object of OpenSM.
 *  \return The larges common number of virtual lanes which is supported by all
 *          nodes in the subnet, or any smaller number requested by the user.
 */
static uint8_t
get_max_num_vls(const osm_ucast_mgr_t *);

/*! \fn get_network_node_by_lid(const network_t *,
 *                              const ib_net16_t)
 *  \brief This fn uses the stdlib to find (via binary search) a network node
 *         for a given LID in a sorted array of network nodes.
 *
 *  \param[in] network Nue's network object storing the subnet.
 *  \param[in] lid     Target LID of the network node to search for.
 *  \return Pointer to a network object (switch) assiciated with the input LID.
 */
static inline network_node_t *
get_network_node_by_lid(const network_t *,
			const ib_net16_t);

/*! \fn get_switch_lid(const osm_ucast_mgr_t *,
 *                     const ib_net16_t)
 *  \brief The fn returns the input LID, assuming it belongs to a subnet switch,
 *         or the LID of the adjacent switch, if the input belongs to a CA.
 *
 *  \param[in] mgr The management object of OpenSM.
 *  \param[in] lid Local identifier of a subnet component (either CA or switch).
 *  \return The LID of a switch in the subnet.
 */
static inline ib_net16_t
get_switch_lid(const osm_ucast_mgr_t *,
	       const ib_net16_t);

/*! \fn init_ccdg_colors(const ccdg_t *)
 *  \brief Iterates over all cCDG vertices and edges to set the color ID/Ptr
 *         into the UNUSED state.
 *
 *  \param[in] ccdg Nue's internal object storing the complete CDG.
 *  \return NONE
 */
static void
init_ccdg_colors(const ccdg_t *);

/*! \fn init_ccdg_edge(ccdg_edge_t *,
 *                     const channel_t)
 *  \brief Initialize the to_channel_id parameter of the cCDG edge with a given
 *         ID identifying the channel ID of the tail vertex of this edge.
 *
 *  \param[in,out] edge   An edge of the complete CDG.
 *  \param[in]     to_cid Channel ID bit field identifying all parts of a link.
 *  \return NONE
 */
static inline void
init_ccdg_edge(ccdg_edge_t *,
	       const channel_t);

/*! \fn init_ccdg_edge_color(const ccdg_t *,
 *                           ccdg_edge_t *)
 *  \brief Initialize a cCDG edge to set the color ID/Ptr into the UNUSED state.
 *
 *  \param[in]     ccdg      Nue's internal object storing the complete CDG.
 *  \param[in,out] ccdg_edge An edge of the complete CDG.
 *  \return NONE
 */
static inline void
init_ccdg_edge_color(const ccdg_t *,
		     ccdg_edge_t *);

/*! \fn init_ccdg_escape_path_edge_color_betw_nodes(const ccdg_t *,
 *                                                  const ccdg_node_t *,
 *                                                  const ccdg_node_t *)
 *  \brief Initialize a cCDG edge to set the color ID/Ptr into the
 *         ESCAPEPATHCOLOR state. The edge is determined by the given vertices.
 *
 *  \param[in] ccdg       Nue's internal object storing the complete CDG.
 *  \param[in] ccdg_node1 A cCDG vertex.
 *  \param[in] ccdg_node2 A second, adjacent cCDG vertex.
 *  \return NONE
 */
static inline void
init_ccdg_escape_path_edge_color_betw_nodes(const ccdg_t *,
					    const ccdg_node_t *,
					    const ccdg_node_t *);

/*! \fn init_ccdg_escape_path_node_color(const ccdg_t *,
 *                                       ccdg_node_t *)
 *  \brief Initialize a cCDG vertex to set the color ID/Ptr into the
 *         ESCAPEPATHCOLOR state.
 *
 *  \param[in] ccdg      Nue's internal object storing the complete CDG.
 *  \param[in] ccdg_node A complete CDG vertex.
 *  \return NONE
 */
static inline void
init_ccdg_escape_path_node_color(const ccdg_t *,
				 ccdg_node_t *);

/*! \fn init_ccdg_node(ccdg_node_t *,
 *                     const channel_t,
 *                     const uint8_t,
 *                     ccdg_edge_t *,
 *                     network_link_t *)
 *  \brief Initialize most parameters of a cCDG vertex describing the graph
 *         structure of the complete channel dependency graph.
 *
 *  \param[in,out] node       A complete CDG vertex.
 *  \param[in]     channel_id Channel ID bit field identifying parts of a link.
 *  \param[in]     num_edges  Number of outgoing cCDG edges from this vertex.
 *  \param[in]     edges      Array of outgoing cCDG edges.
 *  \param[in]     corresponding_netw_link Pointer to the network link which is
 *                                      equivalent to the CDG vertex.
 *  \return NONE
 */
static inline void
init_ccdg_node(ccdg_node_t *,
	       const channel_t,
	       const uint8_t,
	       ccdg_edge_t *,
	       network_link_t *);

/*! \fn init_ccdg_node_color(const ccdg_t *,
 *                           ccdg_node_t *)
 *  \brief Initialize a cCDG vertex to set the color ID/Ptr into the UNUSED
 *         state.
 *
 *  \param[in]     ccdg      Nue's internal object storing the complete CDG.
 *  \param[in,out] ccdg_node A complete CDG vertex.
 *  \return NONE
 */
static inline void
init_ccdg_node_color(const ccdg_t *,
		     ccdg_node_t *);

/*! \fn init_linear_forwarding_tables(const osm_ucast_mgr_t *,
 *                                    const network_t *)
 *  \brief Initialize the new_lft array of a given network node (and corresp.
 *  subnet switch) with OSM_NO_PATH, except for management port 0 of the switch.
 *
 *  \param[in] mgr     The management object of OpenSM.
 *  \param[in] network Nue's network object storing the subnet.
 *  \return NONE
 */
static void
init_linear_forwarding_tables(const osm_ucast_mgr_t *,
			      const network_t *);

#if defined (ENABLE_METIS_FOR_NUE)
/*! \fn init_metis_context(metis_context_t *,
 *                         const idx_t,
 *                         const idx_t,
 *                         const idx_t,
 *                         const idx_t)
 *  \brief Initialize basic information and options for a future METIS call to
 *         partition to subnet.
 *
 *  \param[in,out] metis_ctx The context which holds the input parameters and
 *                           arrays for METIS.
 *  \param[in]     nvtxs     The number of vertices in the graph.
 *  \param[in]     nparts    The number of parts to partition the graph.
 *  \param[in]     seed      Seed to overwrite METIS_OPTION_SEED.
 *  \param[in]     numbering Initializing value for METIS_OPTION_NUMBERING.
 *  \return NONE
 */
static inline void
init_metis_context(metis_context_t *,
		   const idx_t,
		   const idx_t,
		   const idx_t,
		   const idx_t);
#endif

/*! \fn init_network_link(network_link_t *,
 *                        const ib_net16_t,
 *                        const uint8_t,
 *                        const ib_net16_t,
 *                        const uint8_t,
 *                        const uint64_t)
 *  \brief Initialize the link information (associated LIDs/ports) and weight
 *         for the internal representation of a subnet link.
 *
 *  \param[in,out] link     A link between two network nodes.
 *  \param[in]     loc_lid  LID of the head side of the switch-to-switch link.
 *  \param[in]     loc_port Switch port of the head of the link.
 *  \param[in]     rem_lid  LID of the tail side of the switch-to-switch link.
 *  \param[in]     rem_port Switch port of the tail of the link.
 *  \param[in]     weight   Initializing balancing weight value for this link.
 *  \return NONE
 */
static inline void
init_network_link(network_link_t *,
		  const ib_net16_t,
		  const uint8_t,
		  const ib_net16_t,
		  const uint8_t,
		  const uint64_t);

/*! \fn init_network_node(network_node_t *,
 *                        const ib_net16_t,
 *                        const ib_net64_t,
 *                        const uint8_t,
 *                        const uint8_t,
 *                        const uint8_t,
 *                        network_link_t *,
 *                        osm_switch_t *)
 *  \brief Initialize most parameters of a network node (subnet switch) to store
 *         information such as attached CAs, links, LID, etc.
 *
 *  \param[in,out] node          Network node which represents a subnet switch.
 *  \param[in]     lid           The base local identifier (LID) of this switch.
 *  \param[in]     guid          The global unique identifier of this switch.
 *  \param[in]     num_base_lids Number of base LIDs of attached CAs.
 *  \param[in]     num_lids      Number of 'virtual' CAs (by considering LMC).
 *  \param[in]     num_links     NUmber of healthy switch-to-switch links.
 *  \return Integer 0 if initialization and memory allocation was sucessful, or
 *          any integer unequal to 0 otherwise.
 */
static inline int
init_network_node(network_node_t *,
		  const ib_net16_t,
		  const ib_net64_t,
		  const uint8_t,
		  const uint8_t,
		  const uint8_t,
		  network_link_t *,
		  osm_switch_t *);

/*! \fn mark_escape_paths(const osm_ucast_mgr_t *,
 *                        network_t *,
 *                        const ccdg_t *,
 *                        ib_net16_t *,
 *                        const uint16_t,
 *                        const boolean_t)
 *  \brief Calculates a set of 'escape paths' for Nue as fallback option,
 *         similar to an Up/Down routing tree, in case of unsuccessful routing.
 *
 *  Function description: The function derives the escape paths from a spanning
 *  tree rooted at the most central node w.r.t the destination nodes in the
 *  current virtual layer. Escape paths are initial channel dependencies which
 *  aren't to be 'broken', meaning: they are virtual paths building a backbone
 *  in case Nue runs into an impass and can't find all routes towards one
 *  destination LID. Obviously too many fallbacks would overload the backbone
 *  and result in worse network throughput similar to an Up/Down routing.
 *
 *  \param[in] mgr              The management object of OpenSM.
 *  \param[in] network          Nue's network object storing the subnet.
 *  \param[in] ccdg             Nue's internal object storing the complete CDG.
 *  \param[in] destinations     Destinations for the routing in the current VL.
 *  \param[in] num_destinations Number of destinations in the destination array.
 *  \param[in] verify_network_integrity If TRUE, a sanity check is performed to
 *                                      determine subnet connectivity issues.
 *  \return Integer 0 if calculation was sucessful, or any integer unequal to 0
 *          otherwise.
 */
static int
mark_escape_paths(const osm_ucast_mgr_t *,
		  network_t *,
		  const ccdg_t *,
		  ib_net16_t *,
		  const uint16_t,
		  const boolean_t);

/*! \fn mcast_cleanup(const network_t *,
 *                    cl_qlist_t *)
 *  \brief Reset is_mc_member and num_of_mcm of all network nodes for future
 *         computations and calls osm_mcast_drop_port_list for the mcast group.
 *
 *  \param[in] network            Nue's network object storing the subnet.
 *  \param[in] mcastgrp_port_list List of ports being member in the mcast group.
 *  \return NONE
 */
static inline void
mcast_cleanup(const network_t *,
	      cl_qlist_t *);

/*! \fn merge_two_colored_subccdg_by_nodes(const ccdg_t *,
 *                                         const ccdg_node_t *,
 *                                         ccdg_node_t *)
 *  \brief Merges two uniquely colored, disjoint, acyclic subgraphs of the cCDG
 *         into one acyclic subgraph (resulting color defined by ccdg_node1).
 *
 *  \param[in]     ccdg       Nue's internal object storing the complete CDG.
 *  \param[in]     ccdg_node1 A vertex of the cCDG.
 *  \param[in,out] ccdg_node2 An adjacent vertex in the cCDG.
 *  \return NONE
 */
static inline void
merge_two_colored_subccdg_by_nodes(const ccdg_t *,
				   const ccdg_node_t *,
				   ccdg_node_t *);

/*! \fn nue_create_context(const osm_opensm_t *,
 *                         const osm_routing_engine_type_t)
 *  \brief This fn allocats the context for Nue routing and assigns some initial
 *         parameters for the nue_context_t struct.
 *
 *  \param[in] osm          OpenSM's global context object.
 *  \param[in] routing_type Routing ID (should be OSM_ROUTING_ENGINE_TYPE_NUE).
 *  \return Pointer to the whole context object for Nue routing, or NULL if
 *          there was an issue during the allocation process.
 */
static nue_context_t *
nue_create_context(const osm_opensm_t *,
		   const osm_routing_engine_type_t);

/*! \fn nue_destroy_context(void *)
 *  \brief Interface fn exposed to OpenSM - all allocated memory within the
 *         nue_context_t struct is freed.
 *
 *  \param[in,out] context Pointer to a contex object (should be nue_context_t).
 *  \return NONE
 */
static void
nue_destroy_context(void *);

/*! \fn nue_discover_network(void *)
 *  \brief Interface fn exposed to OpenSM - Traverse subnet to gather info about
 *         connected switches used to create an internal network representation.
 *
 *  \param[in,out] context Pointer to a contex object (should be nue_context_t).
 *  \return Integer 0 if subnet discovery was sucessful, or any integer unequal
 *          to 0 otherwise.
 */
static int
nue_discover_network(void *);

/*! \fn nue_do_mcast_routing(void *,
 *                           osm_mgrp_box_t *)
 *  \brief Interface fn exposed to OpenSM - Calculates a spanning tree for a
 *         requested mcast group which will define the mcast forwarding tables.
 *
 *  \param[in,out] context Pointer to a contex object (should be nue_context_t).
 *  \param[in]     mbox    OpenSM's internal object for mcast requests.
 *  \return IB_SUCCESS if requested multicast routing tables where calculated
 *          sucessfully, or IB_ERROR otherwise.
 */
static ib_api_status_t
nue_do_mcast_routing(void *,
		     osm_mgrp_box_t *);

/*! \fn nue_do_ucast_routing(void *)
 *  \brief Interface fn exposed to OpenSM - Calculates the set of deadlock-free
 *         ucast forwarding tables for the fabric, regardless of #VL available.
 *
 *  Function description: A detailed description of this function can be found
 *  in Section 6.2.4 / Algorithm 6.3 of reference [2] (see abstract at the
 *  begining of this file). Here is a basic draft of what the function does and
 *  how the routing on the channel dependency graph works in pseudo code:
 *  -- Assume N is the set of destinations and k the number of available VLs
 *  -- Partition N into k disjoint subsets N_1 ,..., N_k of destinations
 *  -- foreach virtual layer L_i with i in {1 ,..., k} do:
 *  ----- Create a convex subgraph H_i for N_i
 *  ----- Identify central n_r in N_i of convex H_i via Brandes' algorithm
 *  ----- Create a new complete channel dependency graph D_i for layer L_i
 *  ----- Define escape paths D* in D_i for spanning tree root n_r
 *  ----- foreach destination node n in N_i do:
 *  -------- Identify all deadlock-free paths towards n
 *  -------- Store these paths in ucast forwarding tables
 *  -------- Update channel weights in D_i for these paths
 *
 *  \param[in,out] context Pointer to a contex object (should be nue_context_t).
 *  \return Integer 0 if unicast routing was sucessful, or any integer unequal
 *          to 0 otherwise.
 */
static int
nue_do_ucast_routing(void *);

/*! \fn nue_get_vl_for_path(void *,
 *                          const uint8_t,
 *                          const ib_net16_t,
 *                          const ib_net16_t)
 *  \brief Interface fn exposed to OpenSM - The fn returns the virtual layer to
 *         use for a path towards a given destination.
 *
 *  \param[in] context             Ptr to contex obj (should be nue_context_t).
 *  \param[in] hint_for_default_sl Desired/suggested SL in the path request.
 *  \param[in] slid                Source LID for the requested path record.
 *  \param[in] dlid                Destination LID for requested path record.
 *  \return A virtual lane for the path from source LID to destination LID.
 */
static uint8_t
nue_get_vl_for_path(void *,
		    const uint8_t,
		    const ib_net16_t,
		    const ib_net16_t);

/*! \fn osm_ucast_nue_setup(struct osm_routing_engine *,
 *                          osm_opensm_t *)
 *  \brief Interface fn exposed to OpenSM to initialize the Nue routing engine.
 *
 *  \param[in] r   OpenSM's internal struct for routings: fn pointers, etc.
 *  \param[in] osm OpenSM's global context object.
 *  \return ein int um spass
 */
int
osm_ucast_nue_setup(struct osm_routing_engine *,
		    osm_opensm_t *);

/*! \fn print_ccdg(const osm_ucast_mgr_t *,
 *                 const ccdg_t *,
 *                 const boolean_t)
 *  \brief Dump the internal cCDG into the OpenSM log file.
 *
 *  \param[in] mgr          The management object of OpenSM.
 *  \param[in] ccdg         Nue's internal object storing the complete CDG.
 *  \param[in] print_colors If TRUE, then cCDG vertex/edge colors are included.
 *  \return NONE
 */
static void
print_ccdg(const osm_ucast_mgr_t *,
	   const ccdg_t *,
	   const boolean_t);

/*! \fn print_ccdg_node(const osm_ucast_mgr_t *,
 *                      const ccdg_t *,
 *                      const ccdg_node_t *,
 *                      const uint32_t,
 *                      const boolean_t)
 *  \brief Dump vertex information and adjacent edges into the OpenSM log file.
 *
 *  \param[in] mgr          The management object of OpenSM.
 *  \param[in] ccdg         Nue's internal object storing the complete CDG.
 *  \param[in] ccdg_node    A vertex of the cCDG.
 *  \param[in] i            Index of the cCDG vertex in the array of vertices.
 *  \param[in] print_colors If TRUE, then cCDG vertex/edge colors are included.
 *  \return NONE
 */
static inline void
print_ccdg_node(const osm_ucast_mgr_t *,
		const ccdg_t *,
		const ccdg_node_t *,
		const uint32_t,
		const boolean_t);

/*! \fn print_channel_id(const osm_ucast_mgr_t *,
 *                       const channel_t,
 *                       const boolean_t)
 *  \brief Dump the channel ID of an cCDG vertex into the OpenSM log file or
 *         into the OpenSM console.
 *
 *  \param[in] mgr        The management object of OpenSM.
 *  \param[in] channel_id Channel ID bit field identifying parts of a link.
 *  \param[in] console    If TRUE, then ouput is directed to osm console.
 *  \return NONE
 */
static inline void
print_channel_id(const osm_ucast_mgr_t *,
		 const channel_t,
		 const boolean_t);

/*! \fn print_destination_distribution(const osm_ucast_mgr_t *,
 *                                     ib_net16_t **,
 *                                     const uint16_t *)
 *  \brief Dump the destination LIDs assigned to each virtual layer into the
 *         OpenSM log file.
 *
 *  \param[in] mgr          The management object of OpenSM.
 *  \param[in] destinations Array of destination arrays per virtual layer.
 *  \param[in] num_dest     Array containing the number of destinations assigned
 *                          to each virtual layer.
 *  \return NONE
 */
static void
print_destination_distribution(const osm_ucast_mgr_t *,
			       ib_net16_t **,
			       const uint16_t *);

/*! \fn print_network(const osm_ucast_mgr_t *,
 *                    const network_t *)
 *  \brief Dump the discovered, internal representation of the switch-based
 *         network with additional information into the OpenSM log file.
 *
 *  \param[in] mgr     The management object of OpenSM.
 *  \param[in] network Nue's network object storing the subnet.
 *  \return NONE
 */
static void
print_network(const osm_ucast_mgr_t *,
	      const network_t *);

/*! \fn print_network_link(const osm_ucast_mgr_t *,
 *                         const network_link_t *,
 *                         const uint8_t)
 *  \brief Dump the information of a switch-to-switch link, i.e. adjacent node
 *         names, LIDs, GUIDs, etc., into the OpenSM log file.
 *
 *  \param[in] mgr  The management object of OpenSM.
 *  \param[in] link A link between two network nodes.
 *  \param[in] i    Index of this link within the link array of a switch.
 *  \return NONE
 */
static inline void
print_network_link(const osm_ucast_mgr_t *,
		   const network_link_t *,
		   const uint8_t);

/*! \fn print_network_node(const osm_ucast_mgr_t *,
 *                         const network_node_t *,
 *                         const uint16_t,
 *                         const boolean_t)
 *  \brief Dump the information of a network node (i.e. switch) into the OpenSM
 *         log file, including all of its links to adjacent nodes.
 *
 *  \param[in] mgr         The management object of OpenSM.
 *  \param[in] node        A network node of the internal subnet representation.
 *  \param[in] i           Index of the node within the node array of network_t.
 *  \param[in] print_links If TRUE, the outgoing links of the node are printed.
 *  \return NONE
 */
static inline void
print_network_node(const osm_ucast_mgr_t *,
		   const network_node_t *,
		   const uint16_t,
		   const boolean_t);

/*! \fn print_routes(const osm_ucast_mgr_t *,
 *                   const network_t *,
 *                   const osm_port_t *,
 *                   const ib_net16_t)
 *  \brief Dump the calclated egress port (from ucast forwarding tables) for all
 *         network switches to route traffic towards the destination.
 *
 *  \param[in] mgr       The management object of OpenSM.
 *  \param[in] network   Nue's network object storing the subnet.
 *  \param[in] dest_port OpenSM's internal port object for the destination.
 *  \param[in] dlid      Destination LID of the current routing step.
 *  \return NONE
 */
static void
print_routes(const osm_ucast_mgr_t *,
	     const network_t *,
	     const osm_port_t *,
	     const ib_net16_t);

/*! \fn print_spanning_tree(const osm_ucast_mgr_t *,
 *                          const network_t *)
 *  \brief Dump the calclated spanning tree, which is the basis for the escape
 *         paths in the current virtual layer, into the OpenSM log file.
 *
 *  \param[in] mgr     The management object of OpenSM.
 *  \param[in] network Nue's network object storing the subnet.
 *  \return NONE
 */
static void
print_spanning_tree(const osm_ucast_mgr_t *,
		    const network_t *);

/*! \fn reset_ccdg_color_array(const osm_ucast_mgr_t *,
 *                             ccdg_t *,
 *                             const uint16_t *,
 *                             const uint8_t,
 *                             const uint8_t)
 *  \brief This fn allocates the color_array used by Nue to track cCDG subgraphs
 *         for the first call or just resets the color IDs/ptr in the array.
 *
 *  \param[in] mgr              The management object of OpenSM.
 *  \param[in,out] ccdg         Nue's internal object storing the complete CDG.
 *  \param[in] num_destinations Array of number of destinations assigned per VL.
 *  \param[in] max_vl           Number of virtual layers to be used by Nue.
 *  \param[in] max_lmc          Largest LMC assigned to any subnet component.
 *  \return Integer 0 if sucessful, or any integer unequal to 0 otherwise.
 */
static int
reset_ccdg_color_array(const osm_ucast_mgr_t *,
		       ccdg_t *,
		       const uint16_t *,
		       const uint8_t,
		       const uint8_t);

/*! \fn reset_ccdg_edge_color_betw_nodes(const ccdg_t *,
 *                                       const ccdg_node_t *,
 *                                       const ccdg_node_t *,
 *                                       ccdg_edge_t *)
 *  \brief This fn changes the color pointer of a cCDG edge into the default
 *         UNUSED state and sets its wet_paint flag to FALSE.
 *
 *  \param[in]     ccdg       Nue's internal object storing the complete CDG.
 *  \param[in]     ccdg_node1 A vertex of the cCDG.
 *  \param[in]     ccdg_node2 An adjacent vertex in the cCDG.
 *  \param[in,out] ccdg_edge  The edge from ccdg_node1 to ccdg_node2 vertex, or
 *                            NULL which causes the fn to search for the edge.
 *                            The color of this edge will be reset into UNUSED.
 *  \return NONE
 */
static inline void
reset_ccdg_edge_color_betw_nodes(const ccdg_t *,
				 const ccdg_node_t *,
				 const ccdg_node_t *,
				 ccdg_edge_t *);

/*! \fn reset_ccdg_edge_color(const ccdg_t *,
 *                            ccdg_edge_t *)
 *  \brief This fn changes the color pointer of a given cCDG edge into the
 *         UNUSED state and sets its wet_paint flag to FALSE.
 *
 *  \param[in]     ccdg      Nue's internal object storing the complete CDG.
 *  \param[in,out] ccdg_edge An edge of the complete channel dependency graph.
 *  \return NONE
 */
static inline void
reset_ccdg_edge_color(const ccdg_t *,
		      ccdg_edge_t *);

/*! \fn reset_ccdg_node_color(const ccdg_t *,
 *                            ccdg_node_t *)
 *  \brief This fn changes the color pointer of a given cCDG vertex into the
 *         UNUSED state and sets its wet_paint flag to FALSE.
 *
 *  \param[in]     ccdg      Nue's internal object storing the complete CDG.
 *  \param[in,out] ccdg_node A vertex of the complete channel dependency graph.
 *  \return NONE
 */
static inline void
reset_ccdg_node_color(const ccdg_t *,
		      ccdg_node_t *);

/*! \fn reset_delta_for_betw_centrality(const network_t *)
 *  \brief The fn iterates over all network nodes and sets the delta struct
 *         element of network_node_t back to 0.
 *
 *  \param[in,out] network Nue's network object storing the subnet.
 *  \return NONE
 */
static void
reset_delta_for_betw_centrality(const network_t *);

/*! \fn reset_mgrp_membership(const network_t *)
 *  \brief Reset is_mc_member and num_of_mcm of all network nodes for future
 *         multicast routing computations.
 *
 *  \param[in,out] network Nue's network object storing the subnet.
 *  \return NONE
 */
static void
reset_mgrp_membership(const network_t *);

/*! \fn reset_sigma_distance_Ps_for_betw_centrality(const network_t *)
 *  \brief The fn iterates over all network nodes and resets three struct
 *         elementsof network_node_t back to 0 or INFINITY, respectively.
 *
 *  \param[in,out] network Nue's network object storing the subnet.
 *  \return NONE
 */
static void
reset_sigma_distance_Ps_for_betw_centrality(const network_t *);

/*! \fn route_via_modified_dijkstra_on_ccdg(const osm_ucast_mgr_t *,
 *                                          const network_t *,
 *                                          ccdg_t *,
 *                                          const osm_port_t *,
 *                                          const ib_net16_t,
 *                                          const int32_t,
 *                                          boolean_t *)
 *  \brief The fn computes shortest paths from a source to all other netw nodes
 *         but in the complete CDG while complying to the cycle-free constraint.
 *
 *  Function description: A detailed description of this function can be found
 *  in Section 6.2.4 / Algorithm 6.2 of reference [2] (see abstract at the
 *  begining of this file).
 *
 *  \param[in]  mgr          The management object of OpenSM.
 *  \param[in]  network      Nue's network object storing the subnet.
 *  \param[in]  ccdg         Nue's internal object storing the complete CDG.
 *  \param[in]  dest_port    OpenSM's internal port object for the destination.
 *  \param[in]  dlid         Destination LID of the current routing step.
 *  \param[in]  source_color A color ID associated with the current subgraph.
 *  \param[out] fallback_to_escape_paths The fn sets it to TRUE if Nue routing
 *                                       encountered a unsolvable impasse.
 *  \return Integer 0 if modified Dijkstra's algorithm executed on the cCDG for
 *          a given input destination LID completed sucessfully, or any integer
 *          unequal to 0 otherwise.
 */
static int
route_via_modified_dijkstra_on_ccdg(const osm_ucast_mgr_t *,
				    const network_t *,
				    ccdg_t *,
				    const osm_port_t *,
				    const ib_net16_t,
				    const int32_t,
				    boolean_t *);

/*! \fn set_ccdg_edge_into_blocked_state(const ccdg_t *,
 *                                       ccdg_edge_t *)
 *  \brief Change a cCDG edge to set the color ID/Ptr into the BLOCKED state,
 *         and hence prevent any further use since it would close a cycle.
 *
 *  \param[in]     ccdg      Nue's internal object storing the complete CDG.
 *  \param[in,out] ccdg_edge An edge of the complete CDG.
 *  \return NONE
 */
static inline void
set_ccdg_edge_into_blocked_state(const ccdg_t *,
				 ccdg_edge_t *);

/*! \fn sort_backtracking_candidates_by_distance(backtracking_candidate_t *,
 *                                               const size_t)
 *  \brief Qsort the backtracking candidates by the distances determined during
 *         Dijkstra's algo to choose 'best' possible option for a replacement.
 *
 *  \param[in,out] backtracking_candidate_array Array to store candidates.
 *  \param[in]     num_elem_in_array            Number of candidates in array.
 *  \return NONE
 */
static inline void
sort_backtracking_candidates_by_distance(backtracking_candidate_t *,
					 const size_t);

/*! \fn sort_ccdg_nodes_by_channel_id(const ccdg_t *)
 *  \brief This fn uses the stdlib to sort (via qsort) the vertex array of the
 *         cCDG w.r.t the channel IDs.
 *
 *  \param[in,out] ccdg Nue's internal object storing the complete CDG.
 *  \return NONE
 */
static inline void
sort_ccdg_nodes_by_channel_id(const ccdg_t *);

/*! \fn sort_destinations_by_lid(ib_net16_t *,
 *                               const uint32_t)
 *  \brief This fn uses the stdlib to sort (via qsort) the destination array.
 *
 *  \param[in,out] lid_array Array of destination LIDs within the virtual layer.
 *  \param[in]     num_lids  Size of the given array.
 *  \return NONE
 */
static inline void
sort_destinations_by_lid(ib_net16_t *,
			 const uint32_t);

/*! \fn sort_network_nodes_by_lid(const network_t *)
 *  \brief This fn uses the stdlib to sort (via qsort) the network node array
 *         w.r.t the LIDs of the nodes.
 *
 *  \param[in,out] Nue's network object storing the subnet.
 *  \return NONE
 */
static inline void
sort_network_nodes_by_lid(const network_t *);

/*! \fn update_ccdg_heap_index(const void *,
 *                             const size_t)
 *  \brief Callback fn for the cl_heap to update the heap index of a complete
 *         CDG vertex.
 *
 *  \param[in,out] context   A cCDG vertex (assumed input type: `ccdg_node_t').
 *  \param[in]     new_index The new index of the vertex w.r.t the d-ary heap.
 *  \return NONE
 */
static void
update_ccdg_heap_index(const void *,
		       const size_t);

/*! \fn update_dlid_to_vl_mapping(uint8_t *,
 *                                const ib_net16_t,
 *                                const uint8_t)
 *  \brief Store a virtual layer for a destination LID for later requests via
 *         the path_sl interface function.
 *
 *  \param[in,out] dlid_to_vl_mapping Array of virtual layer assignments.
 *  \param[in]     dlid               Destination LID for current routing step.
 *  \param[in]     virtual_layer      Virtual layer all paths towards dlid use.
 *  \return NONE
 */
static inline void
update_dlid_to_vl_mapping(uint8_t *,
			  const ib_net16_t,
			  const uint8_t);

/*! \fn update_linear_forwarding_tables(const osm_ucast_mgr_t *,
 *                                      const network_t *,
 *                                      const osm_port_t *,
 *                                      const ib_net16_t)
 *  \brief Update the ucast linear forwarding tables of all switches towards a
 *         given destination LID based on the calculated paths.
 *
 *  \param[in]     mgr       The management object of OpenSM.
 *  \param[in,out] network   Nue's network object storing the subnet.
 *  \param[in]     dest_port OpenSM's internal port object for the destination.
 *  \param[in]     dlid      Destination LID for current routing step.
 *  \return NONE
 */
static void
update_linear_forwarding_tables(const osm_ucast_mgr_t *,
				const network_t *,
				const osm_port_t *,
				const ib_net16_t);

/*! \fn update_mcast_forwarding_tables(const osm_ucast_mgr_t *,
 *                                     const network_t *,
 *                                     const uint16_t,
 *                                     const cl_qmap_t *,
 *                                     const network_node_t *)
 *  \brief Update the mcast linear forwarding tables of switches participating
 *         in the mcast for a given multicast LID based on the calculated paths.
 *
 *  \param[in]     mgr       The management object of OpenSM.
 *  \param[in,out] network   Nue's network object storing the subnet.
 *  \param[in]     mlid_ho   The multicast LID in host order.
 *  \param[in]     port_map  All subnet ports participating in the multicast.
 *  \param[in]     root_node Network node acting as root for the spanning tree.
 *  \return NONE
 */
static void
update_mcast_forwarding_tables(const osm_ucast_mgr_t *,
			       const network_t *,
			       const uint16_t,
			       const cl_qmap_t *,
			       const network_node_t *);

/*! \fn update_mgrp_membership(cl_qlist_t *)
 *  \brief The fn updates the multicast group membership information to identify
 *         whether a switch needs to be processed or not in later iterations.
 *
 *  \param[in,out] port_list OpenSM's internal object storing a 'list' of ports.
 *  \return NONE
 */
static void
update_mgrp_membership(cl_qlist_t *);

/*! \fn update_netw_heap_index(const void *,
 *                             const size_t)
 *  \brief Callback fn for the cl_heap to update the heap index of a network
 *         node.
 *
 *  \param[in,out] context   A netw node (assumed input type: `network_node_t').
 *  \param[in]     new_index The new index of the node w.r.t the d-ary heap.
 *  \return NONE
 */
static void
update_netw_heap_index(const void *,
		       const size_t);

/*! \fn update_network_link_weights(const osm_ucast_mgr_t *,
 *                                  const network_t *,
 *                                  const ib_net16_t)
 *  \brief Update the edge weights along the path towards to the destination of
 *         the current routing step.
 *
 *  \param[in]     mgr     The management object of OpenSM.
 *  \param[in,out] network Nue's network object storing the subnet.
 *  \param[in]     desti   LID of the switch adjacent to the real destination,
 *                         if it's a terminal, or LID of the destination switch.
 *  \return NONE
 */
static void
update_network_link_weights(const osm_ucast_mgr_t *,
			    const network_t *,
			    const ib_net16_t);

/*! \fn use_escape_paths_to_solve_impass(const osm_ucast_mgr_t *,
 *                                       const network_t *,
 *                                       const osm_port_t *,
 *                                       const ib_net16_t)
 *  \brief In the rare event of an unsolvable routing impass, this fn sets the
 *         'calculated' routes for a destination to pre-determined escape path.
 *
 *  \param[in]     mgr       The management object of OpenSM.
 *  \param[in,out] network   Nue's network object storing the subnet.
 *  \param[in]     dest_port OpenSM's internal port object for the destination.
 *  \param[in]     dlid      Destination LID of the current routing step.
 *  \return NONE
 */
static void
use_escape_paths_to_solve_impass(const osm_ucast_mgr_t *,
				 const network_t *,
				 const osm_port_t *,
				 const ib_net16_t);

/*! \fn using_edge_induces_cycle_in_ccdg(const osm_ucast_mgr_t *,
 *                                       const ccdg_t *,
 *                                       const ccdg_node_t *,
 *                                       ccdg_edge_t *,
 *                                       const int32_t)
 *  \brief Verify that using a cCDG edge as part of a route does not induce any
 *         cycles in the complete CDG in combination with existing paths.
 *
 *  Function description: A detailed description of this function can be found
 *  in Section 6.2.6.1 / Algorithm 6.4 of reference [2] (see abstract at the
 *  begining of this file).
 *
 *  \param[in]     mgr       The management object of OpenSM.
 *  \param[in]     ccdg      Nue's internal object storing the complete CDG.
 *  \param[in]     head      The head side (cCDG vertex) of the ccdg_edge.
 *  \param[in,out] ccdg_edge An edge of the cCDG, which color may be adjusted.
 *  \param[in]     color     Color ID of current iteration to prevent cycles.
 *  \return TRUE if adding the given cCDG edge would close a cycle in the
 *          uniquely colored subgraph of the cCDG, or FALSE otherwise.
 */
static boolean_t
using_edge_induces_cycle_in_ccdg(const osm_ucast_mgr_t *,
				 const ccdg_t *,
				 const ccdg_node_t *,
				 ccdg_edge_t *,
				 const int32_t);

#if defined (_DEBUG_)
/*! \fn deep_cpy_ccdg(const osm_ucast_mgr_t *,
 *                    const ccdg_t *,
 *                    ccdg_t *)
 *  \brief Allocate struct elements or reset (based on out_ccdg input status)
 *         a verification cCDG and perform a deep copy of the cCDG escape paths.
 *
 *  \param[in]     mgr      The management object of OpenSM.
 *  \param[in]     in_ccdg  Nue's internal object storing the complete CDG.
 *  \param[in,out] out_ccdg A 1-to-1 deep copy of Nue's internal cCDG object.
 *  \return TRUE if copying the cCDG was successful, or FALSE otherwise.
 */
static boolean_t
deep_cpy_ccdg(const osm_ucast_mgr_t *,
	      const ccdg_t *,
	      ccdg_t *);

/*! \fn is_channel_id_in_verify_ccdg_node_list(const ccdg_t *,
 *                                             const channel_t *,
 *                                             ccdg_node_t **)
 *  \brief This fn iterates over the cCDG vertex array to search for a given
 *         channel ID and returns a pointer to the vertex if found, or NULL.
 *
 *  \param[in]  ccdg          A 1-to-1 deep copy of Nue's internal cCDG object.
 *  \param[in]  channel_id    Channel ID bit field identifying parts of a link.
 *  \param[out] out_ccdg_node Pointer to a vertex of a (copied) complete CDG.
 *  \return TRUE if a given channel ID is part of the cCDG vertex list, or FALSE
 *          otherwise.
 */
static boolean_t
is_channel_id_in_verify_ccdg_node_list(const ccdg_t *,
				       const channel_t *,
				       ccdg_node_t **);

/*! \fn is_channel_id_in_verify_ccdg_edge_list(const ccdg_node_t *,
 *                                             const channel_t *,
 *                                             ccdg_edge_t **)
 *  \brief This fn iterates over the edge list of a cCDG vertex to search/verify
 *         a given channel ID / neighbor vertex and returns the edge, or NULL.
 *
 *  \param[in]  ccdg_node     A vertex of a (copied) complete CDG.
 *  \param[in]  channel_id    Channel ID bit field identifying parts of a link.
 *  \param[out] out_ccdg_edge Pointer to an edge of a (copied) complete CDG.
 *  \return TRUE if a given channel ID is part of the cCDG edge list, or FALSE
 *          otherwise.
 */
static boolean_t
is_channel_id_in_verify_ccdg_edge_list(const ccdg_node_t *,
				       const channel_t *,
				       ccdg_edge_t **);

/*! \fn add_paths_to_verify_ccdg(const osm_ucast_mgr_t *,
 *                               const network_t *,
 *                               const ib_net16_t,
 *                               const ccdg_t *,
 *                               ccdg_t *,
 *                               const boolean_t)
 *  \brief The fn traverses the network from all switches towards the routing
 *         desti and adds the channel dependencies to the verification cCDG.
 *
 *  \param[in] mgr         The management object of OpenSM.
 *  \param[in] network     Nue's network object storing the subnet.
 *  \param[in] desti       The destination LID of the current routing step.
 *  \param[in] ccdg        Nue's internal object storing the complete CDG.
 *  \param[in,out] verify_ccdg A copy of the cCDG for validation purposes.
 *  \param[in] fallback_to_escape_paths Whether or not the real routing ended up
 *                                      in an unsolvable impasse for the desti.
 *  \return TRUE if a set of newly calculated routes are added to the cCDG
 *          successfully, or FALSE otherwise.
 */
static boolean_t
add_paths_to_verify_ccdg(const osm_ucast_mgr_t *,
			 const network_t *,
			 const ib_net16_t,
			 const ccdg_t *,
			 ccdg_t *,
			 const boolean_t);

/*! \fn is_ccdg_cycle_free(const osm_ucast_mgr_t *,
 *                         const ccdg_t *)
 *  \brief Perform a search for cycles in the verification (c)CDG which would
 *         indicate a broken, not deadlock-free Nue routing.
 *
 *  Function description: This fn is a 'naive' implementaton to thoroughly check
 *  the CDG for the absence of cycles, and hence it is SLOW and shouldn't be
 *  called too often or for gigantic topologies.
 *
 *  \param[in] mgr  The management object of OpenSM.
 *  \param[in] ccdg A copy of the cCDG for validation purposes.
 *  \return TRUE if the verification CDG is cycle-free, or FALSE otherwise.
 */
static boolean_t
is_ccdg_cycle_free(const osm_ucast_mgr_t *,
		   const ccdg_t *);
#endif

/**********************************************************************
 **********************************************************************/

/************ construct/init/destroy functions for structs ************
 **********************************************************************/
static inline void construct_network_link(network_link_t * link)
{
	CL_ASSERT(link);
	memset(link, 0, sizeof(network_link_t));
}

static inline void init_network_link(network_link_t * link,
				     const ib_net16_t loc_lid,
				     const uint8_t loc_port,
				     const ib_net16_t rem_lid,
				     const uint8_t rem_port,
				     const uint64_t weight)
{
	CL_ASSERT(link);

	link->link_info.local_lid = loc_lid;
	link->link_info.local_port = loc_port;
	link->link_info.remote_lid = rem_lid;
	link->link_info.remote_port = rem_port;
	link->weight = weight;
}

static inline void construct_network_node(network_node_t * node)
{
	CL_ASSERT(node);
	memset(node, 0, sizeof(network_node_t));
}

static inline int init_network_node(network_node_t * node,
				    const ib_net16_t lid,
				    const ib_net64_t guid,
				    const uint8_t num_base_lids,
				    const uint8_t num_lids,
				    const uint8_t num_links,
				    network_link_t * links, osm_switch_t * sw)
{
	CL_ASSERT(node);

	node->lid = lid;
	node->guid = guid;
	node->num_base_terminals = num_base_lids;
	node->num_terminals = num_lids;
	node->num_links = num_links;
	node->links = links;
	node->stack_used_links =
	    (network_link_t **) malloc(num_links * sizeof(network_link_t *));
	if (num_links && !node->stack_used_links)
		return -1;
	node->sw = sw;
	node->dropped = FALSE;

	return 0;
}

static inline void destroy_network_node(network_node_t * node)
{
	CL_ASSERT(node);

	if (node->links) {
		free(node->links);
		node->links = NULL;
	}
	if (node->stack_used_links) {
		free(node->stack_used_links);
		node->stack_used_links = NULL;
	}
	if (node->Ps) {
		free(node->Ps);
		node->Ps = NULL;
	}
}

static inline void construct_network(network_t * network)
{
	CL_ASSERT(network);
	memset(network, 0, sizeof(network_t));
	cl_heap_construct(&(network->heap));
}

static inline void destroy_network(network_t * network)
{
	network_node_t *netw_node_iter = NULL;
	uint32_t i = 0;

	CL_ASSERT(network);

	if (network->nodes) {
		for (i = 0, netw_node_iter = network->nodes;
		     i < network->num_nodes; i++, netw_node_iter++) {
			destroy_network_node(netw_node_iter);
		}
		free(network->nodes);
		network->nodes = NULL;
	}
	if (cl_is_heap_inited(&(network->heap)))
		cl_heap_destroy(&(network->heap));
}

static inline void construct_ccdg_edge(ccdg_edge_t * edge)
{
	CL_ASSERT(edge);
	memset(edge, 0, sizeof(ccdg_edge_t));
}

static inline void init_ccdg_edge(ccdg_edge_t * edge, const channel_t to_cid)
{
	CL_ASSERT(edge);
	edge->to_channel_id = to_cid;
}

static inline void construct_ccdg_node(ccdg_node_t * node)
{
	CL_ASSERT(node);
	memset(node, 0, sizeof(ccdg_node_t));
}

static inline void init_ccdg_node(ccdg_node_t * node,
				  const channel_t channel_id,
				  const uint8_t num_edges, ccdg_edge_t * edges,
				  network_link_t * corresponding_netw_link)
{
	CL_ASSERT(node);

	node->channel_id = channel_id;
	node->num_edges = num_edges;
	node->edges = edges;
	node->corresponding_netw_link = corresponding_netw_link;
	node->status = WHITE;
	node->next_edge_idx = 0;
	node->pre = NULL;
}

static inline void destroy_ccdg_node(ccdg_node_t * node)
{
	CL_ASSERT(node);
	if (node->edges) {
		free(node->edges);
		node->edges = NULL;
	}
}

static inline void construct_ccdg(ccdg_t * ccdg)
{
	CL_ASSERT(ccdg);
	memset(ccdg, 0, sizeof(ccdg_t));
	cl_heap_construct(&(ccdg->heap));
}

static inline void destroy_ccdg(ccdg_t * ccdg)
{
	ccdg_node_t *ccdg_node_iter = NULL;
	uint32_t i = 0;

	CL_ASSERT(ccdg);

	if (ccdg->nodes) {
		for (i = 0, ccdg_node_iter = ccdg->nodes; i < ccdg->num_nodes;
		     i++, ccdg_node_iter++) {
			destroy_ccdg_node(ccdg_node_iter);
		}
		free(ccdg->nodes);
		ccdg->nodes = NULL;
	}
	if (ccdg->color_array) {
		free(ccdg->color_array);
		ccdg->color_array = NULL;
	}
	if (cl_is_heap_inited(&(ccdg->heap)))
		cl_heap_destroy(&(ccdg->heap));
}

#if defined (ENABLE_METIS_FOR_NUE)
static inline void construct_metis_context(metis_context_t * metis_ctx)
{
	CL_ASSERT(metis_ctx);
	memset(metis_ctx, 0, sizeof(metis_context_t));
}

static inline void init_metis_context(metis_context_t * metis_ctx,
				      const idx_t nvtxs, const idx_t nparts,
				      const idx_t seed, const idx_t numbering)
{
	CL_ASSERT(metis_ctx);

	*(metis_ctx->nvtxs) = nvtxs;
	*(metis_ctx->ncon) = 1;
	*(metis_ctx->nparts) = nparts;

	METIS_SetDefaultOptions(metis_ctx->options);
	metis_ctx->options[METIS_OPTION_SEED] = seed;
	metis_ctx->options[METIS_OPTION_NUMBERING] = numbering;
}

static inline void destroy_metis_context(metis_context_t * metis_ctx)
{
	CL_ASSERT(metis_ctx);

	if (metis_ctx->xadj) {
		free(metis_ctx->xadj);
		metis_ctx->xadj = NULL;
	}
	if (metis_ctx->adjncy) {
		free(metis_ctx->adjncy);
		metis_ctx->adjncy = NULL;
	}
	if (metis_ctx->part) {
		free(metis_ctx->part);
		metis_ctx->part = NULL;
	}
}
#endif

/**********************************************************************
 **********************************************************************/

/******** helper functions to sort/access destinations by lid *********
 **********************************************************************/
static int compare_lids(const void *l1, const void *l2)
{
	ib_net16_t *lid1 = (ib_net16_t *) l1;
	ib_net16_t *lid2 = (ib_net16_t *) l2;

	CL_ASSERT(lid1 && lid2);

	if (*lid1 < *lid2)
		return -1;
	else if (*lid1 > *lid2)
		return 1;
	else
		return 0;
}

/* use stdlib to sort the lid array */
static inline void sort_destinations_by_lid(ib_net16_t * lid_array,
					    const uint32_t num_lids)
{
	CL_ASSERT(lid_array);
	qsort(lid_array, num_lids, sizeof(ib_net16_t), compare_lids);
}

/* use stdlib to find (binary search) a lid in a sorted array */
static inline ib_net16_t *get_lid(const ib_net16_t * lid_array,
				  const uint32_t num_lids, const ib_net16_t lid)
{
	CL_ASSERT(lid_array);
	return bsearch(&lid, lid_array, num_lids, sizeof(ib_net16_t),
		       compare_lids);
}

/**********************************************************************
 **********************************************************************/

/******** helper functions to sort/access network nodes by lids *******
 **********************************************************************/
static int compare_network_nodes_by_lid(const void *n1, const void *n2)
{
	network_node_t *node1 = (network_node_t *) n1;
	network_node_t *node2 = (network_node_t *) n2;

	CL_ASSERT(node1 && node2);
	return compare_lids((void *)&(node1->lid), (void *)&(node2->lid));
}

/* use stdlib to sort the node array */
static inline void sort_network_nodes_by_lid(const network_t * network)
{
	CL_ASSERT(network);
	qsort(network->nodes, network->num_nodes, sizeof(network_node_t),
	      compare_network_nodes_by_lid);
}

/* use stdlib to find (binary search) a node in a sorted array */
static inline network_node_t *get_network_node_by_lid(const network_t * network,
						      const ib_net16_t lid)
{
	network_node_t key;

	CL_ASSERT(network);

	construct_network_node(&key);
	key.lid = lid;
	return bsearch(&key, network->nodes, network->num_nodes,
		       sizeof(network_node_t), compare_network_nodes_by_lid);
}

/**********************************************************************
 **********************************************************************/

/****** helper functions to sort/access ccdg nodes by channel_id ******
 **********************************************************************/
static inline channel_t get_inverted_channel_id(const channel_t in_channel)
{
	channel_t out_channel;

	out_channel.local_lid = in_channel.remote_lid;
	out_channel.local_port = in_channel.remote_port;
	out_channel.remote_lid = in_channel.local_lid;
	out_channel.remote_port = in_channel.local_port;

	return out_channel;
}

static int compare_two_channel_id(const void *c1, const void *c2)
{
	channel_t *channel_id1 = (channel_t *) c1;
	channel_t *channel_id2 = (channel_t *) c2;
	uint64_t key1 = 0, key2 = 0;
	ib_net16_t l_lid_1 = 0, l_lid_2 = 0, r_lid_1 = 0, r_lid_2 = 0;
	uint8_t l_port_1 = 0, l_port_2 = 0, r_port_1 = 0, r_port_2 = 0;

	CL_ASSERT(channel_id1 && channel_id2);

	l_lid_1 = (ib_net16_t) channel_id1->local_lid;
	l_port_1 = (uint8_t) channel_id1->local_port;
	r_lid_1 = (ib_net16_t) channel_id1->remote_lid;
	r_port_1 = (uint8_t) channel_id1->remote_port;

	l_lid_2 = (ib_net16_t) channel_id2->local_lid;
	l_port_2 = (uint8_t) channel_id2->local_port;
	r_lid_2 = (ib_net16_t) channel_id2->remote_lid;
	r_port_2 = (uint8_t) channel_id2->remote_port;

	key1 =
	    (((uint64_t) l_lid_1) << 48) + (((uint64_t) l_port_1) << 32) +
	    (((uint64_t) r_lid_1) << 16) + ((uint64_t) r_port_1);

	key2 =
	    (((uint64_t) l_lid_2) << 48) + (((uint64_t) l_port_2) << 32) +
	    (((uint64_t) r_lid_2) << 16) + ((uint64_t) r_port_2);

	if (key1 < key2)
		return -1;
	else if (key1 > key2)
		return 1;
	else
		return 0;
}

static inline int compare_ccdg_nodes_by_channel_id(const void *cn1,
						   const void *cn2)
{
	ccdg_node_t *ccdg_node1 = (ccdg_node_t *) cn1;
	ccdg_node_t *ccdg_node2 = (ccdg_node_t *) cn2;

	CL_ASSERT(ccdg_node1 && ccdg_node2);
	return compare_two_channel_id(&(ccdg_node1->channel_id),
				      &(ccdg_node2->channel_id));
}

/* use stdlib to sort the node array w.r.t the channel id */
static inline void sort_ccdg_nodes_by_channel_id(const ccdg_t * ccdg)
{
	CL_ASSERT(ccdg);
	qsort(ccdg->nodes, ccdg->num_nodes, sizeof(ccdg_node_t),
	      compare_ccdg_nodes_by_channel_id);
}

/* use stdlib to find (binary search) a node in a sorted array */
static inline ccdg_node_t *get_ccdg_node_by_channel_id(const ccdg_t * ccdg,
						       const channel_t c_id)
{
	ccdg_node_t key;

	CL_ASSERT(ccdg);

	construct_ccdg_node(&key);
	key.channel_id = c_id;
	return bsearch(&key, ccdg->nodes, ccdg->num_nodes, sizeof(ccdg_node_t),
		       compare_ccdg_nodes_by_channel_id);
}

/**********************************************************************
 **********************************************************************/

/***************** helper function to access ccdg edges ***************
 **********************************************************************/
static ccdg_edge_t *get_ccdg_edge_betw_nodes(const ccdg_node_t * ccdg_node1,
					     const ccdg_node_t * ccdg_node2)
{
	ccdg_edge_t *ccdg_edge_iter = NULL;
	uint8_t i = 0;

	CL_ASSERT(ccdg_node1 && ccdg_node2);

	for (i = 0, ccdg_edge_iter = ccdg_node1->edges;
	     i < ccdg_node1->num_edges; i++, ccdg_edge_iter++) {
		if (ccdg_edge_iter->to_ccdg_node == ccdg_node2)
			return ccdg_edge_iter;
	}
	return NULL;
}

/**********************************************************************
 **********************************************************************/

/******* helper functions to compare and sort candidate channels*******
 **** for local backtracking in case of a temporary routing impasse ***/
static inline int compare_backtracking_candidates_by_distance(const void *btc1,
							      const void *btc2)
{
	backtracking_candidate_t *backtracking_candidate1 =
	    (backtracking_candidate_t *) btc1;
	backtracking_candidate_t *backtracking_candidate2 =
	    (backtracking_candidate_t *) btc2;
	ccdg_node_t *ccdg_node1 = NULL, *ccdg_node2 = NULL;

	CL_ASSERT(backtracking_candidate1 && backtracking_candidate2);

	ccdg_node1 =
	    backtracking_candidate1->orig_used_ccdg_node_for_adj_netw_node;
	ccdg_node2 =
	    backtracking_candidate2->orig_used_ccdg_node_for_adj_netw_node;
	CL_ASSERT(ccdg_node1 && ccdg_node2);

	if (ccdg_node1->distance < ccdg_node2->distance)
		return -1;
	else if (ccdg_node1->distance > ccdg_node2->distance)
		return 1;
	else
		return 0;
}

/* use stdlib to sort the node array w.r.t the channel distance (dijkstra) */
static inline void
sort_backtracking_candidates_by_distance(backtracking_candidate_t *
					 backtracking_candidate_array,
					 const size_t num_elem_in_array)
{
	CL_ASSERT(backtracking_candidate_array);
	qsort(backtracking_candidate_array, num_elem_in_array,
	      sizeof(backtracking_candidate_t),
	      compare_backtracking_candidates_by_distance);
}

/**********************************************************************
 **********************************************************************/

/****** helper functions to manage disjoint subgraphs of the ccdg *****
 ******* init colors, set/get routines, merge of subgraphs, etc. ******/
static inline void init_ccdg_node_color(const ccdg_t * ccdg,
					ccdg_node_t * ccdg_node)
{
	CL_ASSERT(ccdg && ccdg->color_array && ccdg_node);
	ccdg_node->color = &(ccdg->color_array[UNUSED]);
	ccdg_node->wet_paint = FALSE;
}

static inline void init_ccdg_edge_color(const ccdg_t * ccdg,
					ccdg_edge_t * ccdg_edge)
{
	CL_ASSERT(ccdg && ccdg->color_array && ccdg_edge);
	ccdg_edge->color = &(ccdg->color_array[UNUSED]);
	ccdg_edge->wet_paint = FALSE;
}

static void init_ccdg_colors(const ccdg_t * ccdg)
{
	ccdg_node_t *ccdg_node_iter = NULL;
	ccdg_edge_t *ccdg_edge_iter = NULL;
	uint32_t i = 0, j = 0;

	CL_ASSERT(ccdg);

	for (i = 0, ccdg_node_iter = ccdg->nodes; i < ccdg->num_nodes;
	     i++, ccdg_node_iter++) {
		init_ccdg_node_color(ccdg, ccdg_node_iter);
		for (j = 0, ccdg_edge_iter = ccdg_node_iter->edges;
		     j < ccdg_node_iter->num_edges; j++, ccdg_edge_iter++) {
			init_ccdg_edge_color(ccdg, ccdg_edge_iter);
		}
	}
}

static int reset_ccdg_color_array(const osm_ucast_mgr_t * mgr, ccdg_t * ccdg,
				  const uint16_t * num_destinations,
				  const uint8_t max_vl, const uint8_t max_lmc)
{
	int32_t max_destinations = 1;
	uint8_t vl = 0;
	color_t *color_iter = NULL;
	uint16_t i = 0;

	CL_ASSERT(mgr && ccdg && num_destinations);

	if (!ccdg->color_array) {
		for (vl = 0; vl < max_vl; vl++) {
			if (num_destinations[vl] > max_destinations)
				max_destinations = num_destinations[vl];
		}
		/* worst case: multiple routing steps per base lid (lmc>0) */
		/* 1 color for each destination for the cCDG color coding */
		max_destinations *= (1 << max_lmc);
		/* plus 3 colors for statuses: blocked, unused, escape paths */
		max_destinations += 3;
		ccdg->color_array =
		    (color_t *) malloc(max_destinations * sizeof(color_t));
		if (!ccdg->color_array) {
			OSM_LOG(mgr->p_log, OSM_LOG_ERROR,
				"ERR NUE19: cannot allocate memory for ccdg color array\n");
			return -1;
		}
		ccdg->num_colors = max_destinations;
	}

	memset(ccdg->color_array, 0, ccdg->num_colors * sizeof(color_t));
	for (i = 0, color_iter = ccdg->color_array; i < ccdg->num_colors;
	     i++, color_iter++) {
		color_iter->color_id = i;
		color_iter->real_col = color_iter;
	}

	return 0;
}

static inline void init_ccdg_escape_path_node_color(const ccdg_t * ccdg,
						    ccdg_node_t * ccdg_node)
{
	CL_ASSERT(ccdg && ccdg->color_array && ccdg_node);
	ccdg_node->color = &(ccdg->color_array[ESCAPEPATHCOLOR]);
}

static inline void init_ccdg_escape_path_edge_color_betw_nodes(const ccdg_t *
							       ccdg,
							       const ccdg_node_t
							       * ccdg_node1,
							       const ccdg_node_t
							       * ccdg_node2)
{
	ccdg_edge_t *ccdg_edge = NULL;

	CL_ASSERT(ccdg && ccdg->color_array && ccdg_node1 && ccdg_node2);
	ccdg_edge = get_ccdg_edge_betw_nodes(ccdg_node1, ccdg_node2);
	CL_ASSERT(ccdg_edge);
	ccdg_edge->color = &(ccdg->color_array[ESCAPEPATHCOLOR]);
}

static inline void set_ccdg_edge_into_blocked_state(const ccdg_t * ccdg,
						    ccdg_edge_t * ccdg_edge)
{
	CL_ASSERT(ccdg && ccdg_edge);
	ccdg_edge->color = &(ccdg->color_array[BLOCKED]);
}

static inline uint16_t get_ccdg_node_color(const ccdg_t * ccdg,
					   const ccdg_node_t * ccdg_node)
{
	CL_ASSERT(ccdg && ccdg_node && ccdg_node->color);
	return ccdg_node->color->real_col->color_id;
}

static inline uint16_t get_ccdg_edge_color(const ccdg_t * ccdg,
					   const ccdg_edge_t * ccdg_edge)
{
	CL_ASSERT(ccdg_edge && ccdg_edge->color);
	return ccdg_edge->color->real_col->color_id;
}

static inline uint16_t get_ccdg_edge_color_betw_nodes(const ccdg_t * ccdg,
						      const ccdg_node_t *
						      ccdg_node1,
						      const ccdg_node_t *
						      ccdg_node2,
						      ccdg_edge_t * ccdg_edge)
{
	CL_ASSERT(ccdg && ccdg_node1 && ccdg_node2);

	if (!ccdg_edge) {
		ccdg_edge = get_ccdg_edge_betw_nodes(ccdg_node1, ccdg_node2);
	}
	CL_ASSERT(ccdg_edge && ccdg_edge->to_ccdg_node == ccdg_node2);
	return get_ccdg_edge_color(ccdg, ccdg_edge);
}

// only allowed to be called from specific fake channels in the init phase of dijk
static inline void change_fake_ccdg_node_color(const ccdg_t * ccdg,
					       ccdg_node_t * ccdg_node,
					       const int32_t color)
{
	CL_ASSERT(ccdg && ccdg->color_array && ccdg_node);
	CL_ASSERT(ccdg_node->channel_id.local_lid ==
		  ccdg_node->channel_id.remote_lid
		  && (0 ==
		      (ccdg_node->channel_id.local_port | ccdg_node->channel_id.
		       remote_port)));

	if (get_ccdg_node_color(ccdg, ccdg_node) > UNUSED) {
		ccdg_node->color->real_col = &(ccdg->color_array[color]);
	} else {
		ccdg_node->color = &(ccdg->color_array[color]);
	}
}

static inline void reset_ccdg_node_color(const ccdg_t * ccdg,
					 ccdg_node_t * ccdg_node)
{
	CL_ASSERT(ccdg && ccdg_node && ccdg_node->color);

	if (ccdg_node->wet_paint) {
		ccdg_node->color = &(ccdg->color_array[UNUSED]);
		ccdg_node->wet_paint = FALSE;
	}
}

static inline void reset_ccdg_edge_color(const ccdg_t * ccdg,
					 ccdg_edge_t * ccdg_edge)
{
	CL_ASSERT(ccdg && ccdg_edge && ccdg_edge->color);
	if (ccdg_edge->wet_paint) {
		CL_ASSERT(BLOCKED != get_ccdg_edge_color(ccdg, ccdg_edge));
		ccdg_edge->color = &(ccdg->color_array[UNUSED]);
		ccdg_edge->wet_paint = FALSE;
	}
}

static inline void reset_ccdg_edge_color_betw_nodes(const ccdg_t * ccdg,
						    const ccdg_node_t *
						    ccdg_node1,
						    const ccdg_node_t *
						    ccdg_node2,
						    ccdg_edge_t * ccdg_edge)
{
	CL_ASSERT(ccdg && ccdg_node1 && ccdg_node2);

	if (!ccdg_edge) {
		ccdg_edge = get_ccdg_edge_betw_nodes(ccdg_node1, ccdg_node2);
	}
	CL_ASSERT(ccdg_edge && ccdg_edge->to_ccdg_node == ccdg_node2);
	reset_ccdg_edge_color(ccdg, ccdg_edge);
}

static inline void add_ccdg_edge_betw_nodes_to_colored_subccdg(const ccdg_t *
							       ccdg,
							       const ccdg_node_t
							       * ccdg_node1,
							       const ccdg_node_t
							       * ccdg_node2,
							       ccdg_edge_t *
							       ccdg_edge)
{
	CL_ASSERT(ccdg && ccdg_node1 && ccdg_node2);
	CL_ASSERT(get_ccdg_node_color(ccdg, ccdg_node1) ==
		  get_ccdg_node_color(ccdg, ccdg_node2));

	if (!ccdg_edge) {
		ccdg_edge = get_ccdg_edge_betw_nodes(ccdg_node1, ccdg_node2);
	}
	CL_ASSERT(ccdg_edge && ccdg_edge->to_ccdg_node == ccdg_node2
		  && UNUSED == get_ccdg_edge_color(ccdg, ccdg_edge));
	ccdg_edge->color = ccdg_node1->color;
	ccdg_edge->wet_paint = TRUE;
}

static inline void add_ccdg_node_to_colored_subccdg(const ccdg_t * ccdg,
						    const ccdg_node_t *
						    ccdg_node1,
						    ccdg_node_t * ccdg_node2)
{

	CL_ASSERT(ccdg && ccdg_node1 && ccdg_node2);
	CL_ASSERT(get_ccdg_node_color(ccdg, ccdg_node1) > UNUSED
		  && get_ccdg_node_color(ccdg, ccdg_node2) == UNUSED);

	ccdg_node2->color = ccdg_node1->color;
	ccdg_node2->wet_paint = TRUE;
	add_ccdg_edge_betw_nodes_to_colored_subccdg(ccdg, ccdg_node1,
						    ccdg_node2, NULL);
}

static inline void merge_two_colored_subccdg_by_nodes(const ccdg_t * ccdg,
						      const ccdg_node_t *
						      ccdg_node1,
						      ccdg_node_t * ccdg_node2)
{
	CL_ASSERT(ccdg && ccdg_node1 && ccdg_node2);
	CL_ASSERT(get_ccdg_node_color(ccdg, ccdg_node1) > UNUSED
		  && get_ccdg_node_color(ccdg, ccdg_node2) > UNUSED
		  && get_ccdg_node_color(ccdg,
					 ccdg_node1) > get_ccdg_node_color(ccdg,
									   ccdg_node2));

	ccdg_node2->color->real_col = ccdg_node1->color;
	add_ccdg_edge_betw_nodes_to_colored_subccdg(ccdg, ccdg_node1,
						    ccdg_node2, NULL);
}

static inline void dry_ccdg_node_color(ccdg_node_t * ccdg_node)
{
	CL_ASSERT(ccdg_node);
	if (ccdg_node->wet_paint)
		ccdg_node->wet_paint = FALSE;
}

static inline void dry_ccdg_edge_color_betw_nodes(const ccdg_t * ccdg,
						  const ccdg_node_t *
						  ccdg_node1,
						  const ccdg_node_t *
						  ccdg_node2)
{
	ccdg_edge_t *ccdg_edge = NULL;

	CL_ASSERT(ccdg && ccdg_node1 && ccdg_node2);
	CL_ASSERT(get_ccdg_node_color(ccdg, ccdg_node1) ==
		  get_ccdg_node_color(ccdg, ccdg_node2));

	ccdg_edge = get_ccdg_edge_betw_nodes(ccdg_node1, ccdg_node2);
	/* the following assertion (color equivalence) doesn't hold if we had a
	   reset to escap paths in combination with the fake channels -> filter
	 */
	CL_ASSERT(ccdg_edge
		  && (get_ccdg_node_color(ccdg, ccdg_node1) ==
		      get_ccdg_edge_color(ccdg, ccdg_edge)
		      || 0 ==
		      (ccdg_node1->channel_id.local_port | ccdg_node1->
		       channel_id.remote_port)));
	if (ccdg_edge->wet_paint)
		ccdg_edge->wet_paint = FALSE;
}

static inline void fix_ccdg_node_color(ccdg_node_t * ccdg_node)
{
	CL_ASSERT(ccdg_node && ccdg_node->color);
	if (ccdg_node->color->real_col != ccdg_node->color)
		ccdg_node->color = ccdg_node->color->real_col;
}

static inline void fix_ccdg_edge_color(ccdg_edge_t * ccdg_edge)
{
	CL_ASSERT(ccdg_edge && ccdg_edge->color);
	if (ccdg_edge->color->real_col != ccdg_edge->color)
		ccdg_edge->color = ccdg_edge->color->real_col;
}

static void fix_ccdg_colors(const osm_ucast_mgr_t * mgr,
			    const network_t * network,
			    const network_node_t * source_netw_node,
			    const ccdg_t * ccdg,
			    const ccdg_node_t * source_ccdg_node)
{
	network_node_t *network_node = NULL, *netw_node_iter = NULL;
	ccdg_node_t *ccdg_node = NULL, *pre_ccdg_node = NULL;
	ccdg_node_t *ccdg_node_iter = NULL;
	ccdg_edge_t *ccdg_edge_iter = NULL;
	uint16_t i = 0;
	uint32_t j = 0;
	uint8_t k = 0;

	CL_ASSERT(mgr && network && source_netw_node && ccdg
		  && source_ccdg_node);

	/* dry all colors of ccdg nodes/edges which are actually used */
	for (i = 0, netw_node_iter = network->nodes; i < network->num_nodes;
	     i++, netw_node_iter++) {
		if (!netw_node_iter->used_link) {
			CL_ASSERT(netw_node_iter == source_netw_node);
			continue;
		}
		ccdg_node = netw_node_iter->used_link->corresponding_ccdg_node;
		CL_ASSERT(ccdg_node);
		network_node =
		    get_network_node_by_lid(network,
					    ccdg_node->channel_id.local_lid);
		CL_ASSERT(network_node);

		dry_ccdg_node_color(ccdg_node);
		if (network_node == source_netw_node) {
			dry_ccdg_edge_color_betw_nodes(ccdg, source_ccdg_node,
						       ccdg_node);
		} else {
			CL_ASSERT(network_node->used_link);
			pre_ccdg_node =
			    network_node->used_link->corresponding_ccdg_node;
			CL_ASSERT(pre_ccdg_node);
			dry_ccdg_edge_color_betw_nodes(ccdg, pre_ccdg_node,
						       ccdg_node);
		}
	}

	/* everything else which is still wet now can be reset and afterwards
	   we simply fix the colors, meaning subgraph merges are made official
	 */
	for (j = 0, ccdg_node_iter = ccdg->nodes; j < ccdg->num_nodes;
	     j++, ccdg_node_iter++) {
		reset_ccdg_node_color(ccdg, ccdg_node_iter);
		for (k = 0, ccdg_edge_iter = ccdg_node_iter->edges;
		     k < ccdg_node_iter->num_edges; k++, ccdg_edge_iter++) {
			reset_ccdg_edge_color_betw_nodes(ccdg, ccdg_node_iter,
							 ccdg_edge_iter->
							 to_ccdg_node,
							 ccdg_edge_iter);
			fix_ccdg_edge_color(ccdg_edge_iter);
		}
		fix_ccdg_node_color(ccdg_node_iter);
	}
}

/**********************************************************************
 **********************************************************************/

/****** debugging functions to print the network or complete cdg ******
 ************* or whatever else we need to print **********************/
static inline void print_network_link(const osm_ucast_mgr_t * mgr,
				      const network_link_t * link,
				      const uint8_t i)
{
	network_node_t *adj_node = NULL;

	CL_ASSERT(mgr && link);

	adj_node = link->to_network_node;
	if (adj_node) {
		OSM_LOG(mgr->p_log, OSM_LOG_INFO,
			"  link[%" PRIu8 "][name, lid, guid]" " = [%s, %" PRIu16
			", 0x%016" PRIx64 "]\n", i,
			adj_node->sw->p_node->print_desc,
			cl_ntoh16(adj_node->lid), cl_ntoh64(adj_node->guid));
	}
}

static inline void print_network_node(const osm_ucast_mgr_t * mgr,
				      const network_node_t * node,
				      const uint16_t i,
				      const boolean_t print_links)
{
	channel_t *channel_id = NULL;
	uint8_t j = 0;

	CL_ASSERT(mgr && node);

	OSM_LOG(mgr->p_log, OSM_LOG_INFO, "node[%" PRIu32 "] at %p:\n", i,
		node);
	OSM_LOG(mgr->p_log, OSM_LOG_INFO,
		"  [name, lid, guid, num_terminals, switch_pointer]" " = [%s, %"
		PRIu16 ", 0x%016" PRIx64 ", %" PRIu8 ", %p]\n",
		node->sw->p_node->print_desc, cl_ntoh16(node->lid),
		cl_ntoh64(node->guid), node->num_terminals, node->sw);
	if (print_links) {
		for (j = 0; j < node->num_links; j++)
			print_network_link(mgr, &(node->links[j]), j);
	}
	if (print_links && node->escape_path) {
		channel_id =
		    (channel_t *) & (node->escape_path->
				     corresponding_ccdg_node->channel_id);
		OSM_LOG(mgr->p_log, OSM_LOG_INFO,
			"  [escape_path] = [(%" PRIu16 ",%" PRIu8 ")->(%" PRIu16
			",%" PRIu8 ")]\n", cl_ntoh16(channel_id->local_lid),
			channel_id->local_port,
			cl_ntoh16(channel_id->remote_lid),
			channel_id->remote_port);
	}
}

static void print_network(const osm_ucast_mgr_t * mgr,
			  const network_t * network)
{
	uint32_t i = 0;
	network_node_t *netw_node_iter = NULL;

	CL_ASSERT(mgr && network);
	OSM_LOG_ENTER(mgr->p_log);

	for (i = 0, netw_node_iter = network->nodes; i < network->num_nodes;
	     i++, netw_node_iter++)
		print_network_node(mgr, netw_node_iter, i, TRUE);

	OSM_LOG_EXIT(mgr->p_log);
}

static inline void print_ccdg_node(const osm_ucast_mgr_t * mgr,
				   const ccdg_t * ccdg,
				   const ccdg_node_t * ccdg_node,
				   const uint32_t i,
				   const boolean_t print_colors)
{
	uint8_t j = 0;
	ccdg_node_t *adj_ccdg_node = NULL;
	channel_t *channel_id = NULL;
	ccdg_edge_t *ccdg_edge_iter = NULL;

	CL_ASSERT(mgr && ccdg && ccdg_node);

	channel_id = (channel_t *) & (ccdg_node->channel_id);
	OSM_LOG(mgr->p_log, OSM_LOG_DEBUG, "ccdg[%" PRIu32 "] at %p:\n", i,
		ccdg_node);
	if (print_colors) {
		OSM_LOG(mgr->p_log, OSM_LOG_DEBUG,
			"  [local_lid, local_port, remote_lid, remote_port, color]"
			" = [%" PRIu16 ", %" PRIu8 ", %" PRIu16 ", %" PRIu8
			", %" PRIi32 "]\n", cl_ntoh16(channel_id->local_lid),
			channel_id->local_port,
			cl_ntoh16(channel_id->remote_lid),
			channel_id->remote_port, get_ccdg_node_color(ccdg,
								     ccdg_node));
	} else {
		OSM_LOG(mgr->p_log, OSM_LOG_DEBUG,
			"  [local_lid, local_port, remote_lid, remote_port]"
			" = [%" PRIu16 ", %" PRIu8 ", %" PRIu16 ", %" PRIu8
			"]\n", cl_ntoh16(channel_id->local_lid),
			channel_id->local_port,
			cl_ntoh16(channel_id->remote_lid),
			channel_id->remote_port);
	}

	for (j = 0, ccdg_edge_iter = ccdg_node->edges; j < ccdg_node->num_edges;
	     j++, ccdg_edge_iter++) {
		adj_ccdg_node = ccdg_edge_iter->to_ccdg_node;
		CL_ASSERT(adj_ccdg_node);
		if (print_colors) {
			OSM_LOG(mgr->p_log, OSM_LOG_DEBUG,
				"  edge_towards[%" PRIu8
				"][local_lid, local_port, remote_lid, remote_port, edge_color]"
				" = [%" PRIu16 ", %" PRIu8 ", %" PRIu16 ", %"
				PRIu8 ", %" PRIi32 "]\n", j,
				cl_ntoh16(adj_ccdg_node->channel_id.local_lid),
				adj_ccdg_node->channel_id.local_port,
				cl_ntoh16(adj_ccdg_node->channel_id.remote_lid),
				adj_ccdg_node->channel_id.remote_port,
				get_ccdg_edge_color(ccdg, ccdg_edge_iter));
		} else {
			OSM_LOG(mgr->p_log, OSM_LOG_DEBUG,
				"  edge_towards[%" PRIu8
				"][local_lid, local_port, remote_lid, remote_port]"
				" = [%" PRIu16 ", %" PRIu8 ", %" PRIu16 ", %"
				PRIu8 "]\n", j,
				cl_ntoh16(adj_ccdg_node->channel_id.local_lid),
				adj_ccdg_node->channel_id.local_port,
				cl_ntoh16(adj_ccdg_node->channel_id.remote_lid),
				adj_ccdg_node->channel_id.remote_port);
		}
	}
}

static void print_ccdg(const osm_ucast_mgr_t * mgr, const ccdg_t * ccdg,
		       const boolean_t print_colors)
{
	uint32_t i = 0;
	ccdg_node_t *ccdg_node_iter = NULL;

	CL_ASSERT(mgr && ccdg);
	OSM_LOG_ENTER(mgr->p_log);

	for (i = 0, ccdg_node_iter = ccdg->nodes; i < ccdg->num_nodes;
	     i++, ccdg_node_iter++)
		print_ccdg_node(mgr, ccdg, ccdg_node_iter, i, print_colors);

	OSM_LOG_EXIT(mgr->p_log);
}

static void print_destination_distribution(const osm_ucast_mgr_t * mgr,
					   ib_net16_t ** destinations,
					   const uint16_t * num_dest)
{
	uint16_t i = 0;
	uint8_t vl = 0;
	ib_net16_t *dlid_iter = NULL;
	osm_port_t *dest_port = NULL;

	CL_ASSERT(mgr && destinations && num_dest);
	OSM_LOG_ENTER(mgr->p_log);

	for (vl = 0; vl < IB_MAX_NUM_VLS; vl++) {
		if (!destinations[vl])
			continue;
		OSM_LOG(mgr->p_log, OSM_LOG_INFO,
			"destination lids (base lid) for vl %" PRIu8 ":\n", vl);
		dlid_iter = (ib_net16_t *) destinations[vl];
		for (i = 0; i < num_dest[vl]; i++, dlid_iter++) {
			dest_port =
			    osm_get_port_by_lid(mgr->p_subn, *dlid_iter);
			OSM_LOG(mgr->p_log, OSM_LOG_INFO,
				"  %" PRIu16 " (%s)\n", cl_ntoh16(*dlid_iter),
				dest_port->p_node->print_desc);
		}
	}

	OSM_LOG_EXIT(mgr->p_log);
}

static void print_spanning_tree(const osm_ucast_mgr_t * mgr,
				const network_t * network)
{
	uint32_t i = 0;
	network_node_t *local_node = NULL, *netw_node_iter = NULL;
	network_link_t *link = NULL;

	CL_ASSERT(mgr && network);
	OSM_LOG_ENTER(mgr->p_log);

	for (i = 0, netw_node_iter = network->nodes; i < network->num_nodes;
	     i++, netw_node_iter++) {
		print_network_node(mgr, netw_node_iter, i, FALSE);
		link = netw_node_iter->escape_path;
		if (!link) {
			OSM_LOG(mgr->p_log, OSM_LOG_DEBUG,
				" no link; is root of the spanning tree\n");
			continue;
		}
		local_node =
		    get_network_node_by_lid(network, link->link_info.local_lid);
		CL_ASSERT(local_node);
		OSM_LOG(mgr->p_log, OSM_LOG_DEBUG,
			" link to get here: [name=%s, lid=%" PRIu16 ", port=%"
			PRIu8 "]" " --> [name=%s, lid=%" PRIu16 ", port=%" PRIu8
			"]\n", local_node->sw->p_node->print_desc,
			cl_ntoh16(link->link_info.local_lid),
			link->link_info.local_port,
			netw_node_iter->sw->p_node->print_desc,
			cl_ntoh16(link->link_info.remote_lid),
			link->link_info.remote_port);
	}

	OSM_LOG_EXIT(mgr->p_log);
}

static void print_routes(const osm_ucast_mgr_t * mgr, const network_t * network,
			 const osm_port_t * dest_port, const ib_net16_t dlid)
{
	network_node_t *curr_node = NULL, *netw_node_iter = NULL;
	uint8_t rem_port = 0;
	ib_net16_t r_lid = 0;
	uint16_t i = 0;

	CL_ASSERT(mgr && network && dest_port && dlid > 0);

	for (i = 0, netw_node_iter = network->nodes; i < network->num_nodes;
	     i++, netw_node_iter++) {
		curr_node = netw_node_iter;

		OSM_LOG(mgr->p_log, OSM_LOG_DEBUG,
			"Route from switch 0x%016" PRIx64
			" (%s) to destination 0x%016" PRIx64 " (%s):\n",
			curr_node->guid, curr_node->sw->p_node->print_desc,
			cl_ntoh64(osm_node_get_node_guid(dest_port->p_node)),
			dest_port->p_node->print_desc);

		while (curr_node->used_link) {
			OSM_LOG(mgr->p_log, OSM_LOG_DEBUG,
				"   0x%016" PRIx64 " (%s) routes thru port %"
				PRIu8 "\n", curr_node->guid,
				curr_node->sw->p_node->print_desc,
				curr_node->used_link->link_info.remote_port);

			r_lid =
			    (ib_net16_t) curr_node->used_link->link_info.
			    local_lid;
			curr_node = get_network_node_by_lid(network, r_lid);
			CL_ASSERT(curr_node);
		}
		if (osm_node_get_type(dest_port->p_node) == IB_NODE_TYPE_CA) {
			(void)osm_node_get_remote_node(dest_port->p_node,
						       dest_port->p_physp->
						       port_num, &rem_port);
			OSM_LOG(mgr->p_log, OSM_LOG_DEBUG,
				"   0x%016" PRIx64 " (%s) routes thru port %"
				PRIu8 "\n", curr_node->guid,
				curr_node->sw->p_node->print_desc, rem_port);
		}
	}
}

static inline void print_channel_id(const osm_ucast_mgr_t * mgr,
				    const channel_t channel_id,
				    const boolean_t console)
{
	CL_ASSERT(mgr);

	if (console) {
		printf("Channel Info [(LID,Port) -> (LID,Port)] = [(%" PRIu16
		       ",%" PRIu8 ") -> (%" PRIu16 ",%" PRIu8 ")]\n",
		       channel_id.local_lid, channel_id.local_port,
		       channel_id.remote_lid, channel_id.remote_port);
	} else {
		OSM_LOG(mgr->p_log, OSM_LOG_INFO,
			"Channel Info [(LID,Port) -> (LID,Port)] = [(%" PRIu16
			",%" PRIu8 ") -> (%" PRIu16 ",%" PRIu8 ")]\n",
			channel_id.local_lid, channel_id.local_port,
			channel_id.remote_lid, channel_id.remote_port);
	}
}

/**********************************************************************
 **********************************************************************/

/* get the larges number of virtual lanes which is supported by all nodes
   in the subnet, or use user supplied number (if it is smaller)
*/
static uint8_t get_max_num_vls(const osm_ucast_mgr_t * mgr)
{
	uint32_t i = 0;
	uint8_t vls_avail = 0xFF, port_vls_avail = 0;
	cl_qmap_t *switch_tbl = NULL;
	cl_map_item_t *item = NULL;
	osm_switch_t *sw = NULL;

	CL_ASSERT(mgr);
	OSM_LOG_ENTER(mgr->p_log);

	/* traverse all switches to get the number of available virtual lanes
	   in the subnet
	 */
	switch_tbl = &(mgr->p_subn->sw_guid_tbl);
	for (item = cl_qmap_head(switch_tbl); item != cl_qmap_end(switch_tbl);
	     item = cl_qmap_next(item)) {
		sw = (osm_switch_t *) item;

		/* include management port 0 only in case a TCA is attached
		   (this assumes that p_physp->p_remote_physp is only valid with
		   TCA attached and NULL otherwise); it is neccessary because
		   without TCA the port only shows VL0 in VLCap/OperVLs
		 */
		for (i = 0; i < osm_node_get_num_physp(sw->p_node); i++) {
			osm_physp_t *p_physp =
			    osm_node_get_physp_ptr(sw->p_node, i);

			if (p_physp && p_physp->p_remote_physp) {
				port_vls_avail =
				    ib_port_info_get_op_vls(&p_physp->
							    port_info);
				if (port_vls_avail
				    && port_vls_avail < vls_avail)
					vls_avail = port_vls_avail;
			}
		}
	}

	/* ib_port_info_get_op_vls gives values 1 ... 5 (s. IBAS 14.2.5.6) */
	vls_avail = 1 << (vls_avail - 1);

	/* set boundaries (s. IBAS 3.5.7) */
	if (vls_avail > 15)
		vls_avail = 15;
	if (vls_avail < 1)
		vls_avail = 1;

	/* now check if the user requested a different maximum #VLs */
	if (mgr->p_subn->opt.nue_max_num_vls) {
		if (mgr->p_subn->opt.nue_max_num_vls <= vls_avail)
			vls_avail = mgr->p_subn->opt.nue_max_num_vls;
		else
			OSM_LOG(mgr->p_log, OSM_LOG_INFO,
				"WRN NUE47: user requested maximum #VLs is larger than supported #VLs\n");
	}

	OSM_LOG_EXIT(mgr->p_log);
	return vls_avail;
}

static int create_context(nue_context_t * nue_ctx)
{
	uint16_t max_lid_ho = 0;

	CL_ASSERT(nue_ctx);

	/* and now initialize internals, i.e. network and ccdg */
	construct_network(&(nue_ctx->network));
	construct_ccdg(&(nue_ctx->ccdg));

	/* we also need an array of all lids to distribute across VLs */
	CL_ASSERT(IB_MAX_NUM_VLS);
	max_lid_ho = nue_ctx->mgr->p_subn->max_ucast_lid_ho;
	memset(nue_ctx->num_destinations, 0, IB_MAX_NUM_VLS * sizeof(uint16_t));
	memset(nue_ctx->destinations, 0, IB_MAX_NUM_VLS * sizeof(ib_net16_t *));
	nue_ctx->num_destinations[0] = max_lid_ho;
	nue_ctx->destinations[0] =
	    (ib_net16_t *) malloc(max_lid_ho * sizeof(ib_net16_t));
	if (!nue_ctx->destinations[0]) {
		OSM_LOG(nue_ctx->mgr->p_log, OSM_LOG_ERROR,
			"ERR NUE07: cannot allocate dlid array\n");
		destroy_context(nue_ctx);
		return -1;
	}

	/* and an array for the mapping of src/dest path to VL */
	nue_ctx->dlid_to_vl_mapping =
	    (uint8_t *) malloc(max_lid_ho * sizeof(uint8_t));
	if (!nue_ctx->dlid_to_vl_mapping) {
		OSM_LOG(nue_ctx->mgr->p_log, OSM_LOG_ERROR,
			"ERR NUE06: cannot allocate dlid_to_vl_mapping\n");
		destroy_context(nue_ctx);
		return -1;
	}
	memset(nue_ctx->dlid_to_vl_mapping, OSM_DEFAULT_SL,
	       max_lid_ho * sizeof(uint8_t));

	return 0;
}

static nue_context_t *nue_create_context(const osm_opensm_t * osm,
					 const osm_routing_engine_type_t
					 routing_type)
{
	nue_context_t *nue_ctx = NULL;
	int err = 0;

	CL_ASSERT(osm);

	/* allocate memory for nue context */
	nue_ctx = (nue_context_t *) malloc(sizeof(nue_context_t));
	if (nue_ctx) {
		/* set initial values with stuff provided by caller */
		nue_ctx->routing_type = routing_type;
		nue_ctx->mgr = (osm_ucast_mgr_t *) & (osm->sm.ucast_mgr);
		err = create_context(nue_ctx);
		if (err) {
			free(nue_ctx);
			return NULL;
		}
	} else {
		OSM_LOG(osm->sm.ucast_mgr.p_log, OSM_LOG_ERROR,
			"ERR NUE01: cannot allocate memory for nue_ctx\n");
		return NULL;
	}

	return nue_ctx;
}

/* count the total number of Hca/Tca (or LIDs for lmc>0) in the fabric
   (even include base/enhanced switch port 0; base SP0 will have lmc=0);
   and while we are already on it, we save the base lids for later
*/
static uint64_t get_base_lids_and_number_of_lids(nue_context_t * nue_ctx)
{
	cl_qmap_t *port_tbl = NULL;
	cl_map_item_t *item = NULL;
	osm_port_t *port = NULL;
	uint64_t total_num_destination_lids = 0;
	uint8_t lmc = 0, max_lmc = 0, ntype = 0;
	uint16_t total_num_base_lids = 0;
	ib_net16_t *dlid_iter = NULL;
	ib_net64_t port_guid = 0;

	CL_ASSERT(nue_ctx);
	OSM_LOG_ENTER(nue_ctx->mgr->p_log);

	dlid_iter = (ib_net16_t *) nue_ctx->destinations[0];
	port_tbl = (cl_qmap_t *) & (nue_ctx->mgr->p_subn->port_guid_tbl);
	for (item = cl_qmap_head(port_tbl); item != cl_qmap_end(port_tbl);
	     item = cl_qmap_next(item)) {
		port = (osm_port_t *) item;
		ntype = osm_node_get_type(port->p_node);
		/* check if link is healthy, otherwise ignore CA */
		if (ntype == IB_NODE_TYPE_CA && port->p_physp &&
		    !osm_link_is_healthy(port->p_physp)) {
			port_guid = osm_node_get_node_guid(port->p_node);
			OSM_LOG(nue_ctx->mgr->p_log, OSM_LOG_INFO,
				"WRN NUE44: ignoring CA 0x%016" PRIx64
				" due to unhealthy to/from adjacent switch\n",
				cl_ntoh64(port_guid));
		}
		if (ntype == IB_NODE_TYPE_CA || ntype == IB_NODE_TYPE_SWITCH) {
			/* count num destinations to get initial link weight */
			lmc = osm_port_get_lmc(port);
			total_num_destination_lids += (1 << lmc);
			if (lmc > max_lmc)
				max_lmc = lmc;

			/* and store the base lids */
			*dlid_iter++ = osm_port_get_base_lid(port);
			total_num_base_lids++;
		}
	}
	nue_ctx->num_destinations[0] = total_num_base_lids;
	nue_ctx->max_lmc = max_lmc;

	/* we skip a realloc of nue_ctx->destinations[0] since it will be done
	   in the distribution function later anyways
	 */

	OSM_LOG_EXIT(nue_ctx->mgr->p_log);
	return total_num_destination_lids;
}

static int build_complete_cdg(const osm_ucast_mgr_t * mgr,
			      const network_t * network, ccdg_t * ccdg,
			      const uint32_t total_num_sw_to_sw_links)
{
	uint64_t i = 0, j = 0, k = 0;
	channel_t channel_id;
	network_node_t *adj_netw_node = NULL, *netw_node_iter = NULL;
	network_link_t *link = NULL, *netw_link_iter = NULL;
	ccdg_node_t *ccdg_node_iter = NULL;
	ccdg_edge_t *ccdg_edge = NULL, *ccdg_edges = NULL;
	ccdg_edge_t *ccdg_edge_iter = NULL;
	ib_net16_t l_lid = 0, r_lid = 0;
	uint8_t l_port = 0, r_port = 0, num_edges = 0;

	CL_ASSERT(mgr && network && ccdg);
	OSM_LOG_ENTER(mgr->p_log);

	OSM_LOG(mgr->p_log, OSM_LOG_INFO,
		"Building complete channel dependency graph for nue routing\n");

	/* we have two types of ccdg nodes, real channels and fake entries,
	   the fake entries are needed as source ccdg node for the routing
	 */
	ccdg->num_nodes = total_num_sw_to_sw_links + network->num_nodes;
	ccdg->nodes =
	    (ccdg_node_t *) malloc(ccdg->num_nodes * sizeof(ccdg_node_t));
	if (!ccdg->nodes) {
		OSM_LOG(mgr->p_log, OSM_LOG_ERROR,
			"ERR NUE05: can't allocate memory for ccdg nodes\n");
		return -1;
	}
	for (i = 0, ccdg_node_iter = ccdg->nodes; i < ccdg->num_nodes;
	     i++, ccdg_node_iter++)
		construct_ccdg_node(ccdg_node_iter);

	ccdg_node_iter = (ccdg_node_t *) ccdg->nodes;
	for (i = 0, netw_node_iter = network->nodes; i < network->num_nodes;
	     i++, netw_node_iter++) {
		/* first we add the fake channel */
		channel_id.local_lid = netw_node_iter->lid;
		channel_id.local_port = 0;
		channel_id.remote_lid = netw_node_iter->lid;
		channel_id.remote_port = 0;

		/* the fake channel connects to all real channels of
		   this node
		 */
		num_edges = netw_node_iter->num_links;
		ccdg_edges =
		    (ccdg_edge_t *) malloc(num_edges * sizeof(ccdg_edge_t));
		if (num_edges && !ccdg_edges) {
			OSM_LOG(mgr->p_log, OSM_LOG_ERROR,
				"ERR NUE35: cannot allocate memory for"
				" ccdg edges of fake channel\n");
			return -1;
		}
		for (k = 0, ccdg_edge_iter = ccdg_edges; k < num_edges;
		     k++, ccdg_edge_iter++)
			construct_ccdg_edge(ccdg_edge_iter);

		/* init ccdg edges for this fake ccdg node */
		for (k = 0, ccdg_edge_iter = ccdg_edges; k < num_edges;
		     k++, ccdg_edge_iter++) {
			link = (network_link_t *) & (netw_node_iter->links[k]);
			init_ccdg_edge(ccdg_edge_iter, link->link_info);
		}

		init_ccdg_node(ccdg_node_iter++, channel_id, num_edges,
			       ccdg_edges, NULL);

		/* and afterwards the real channels */
		for (j = 0, netw_link_iter = netw_node_iter->links;
		     j < netw_node_iter->num_links;
		     j++, netw_link_iter++, ccdg_node_iter++) {
			channel_id = netw_link_iter->link_info;
			l_lid = channel_id.local_lid;
			l_port = channel_id.local_port;
			adj_netw_node = netw_link_iter->to_network_node;
			CL_ASSERT(adj_netw_node && adj_netw_node->num_links);

			/* we can ignore reverse path, so it is #links - 1 */
			num_edges = adj_netw_node->num_links - 1;
			ccdg_edges =
			    (ccdg_edge_t *) malloc(num_edges *
						   sizeof(ccdg_edge_t));
			if (!ccdg_edges) {
				OSM_LOG(mgr->p_log, OSM_LOG_ERROR,
					"ERR NUE08: can't allocate memory for"
					" ccdg edges\n");
				return -1;
			}
			for (k = 0, ccdg_edge_iter = ccdg_edges; k < num_edges;
			     k++, ccdg_edge_iter++)
				construct_ccdg_edge(ccdg_edge_iter);

			/* init ccdg edges for this ccdg node */
			for (k = 0, ccdg_edge_iter = ccdg_edges;
			     k < adj_netw_node->num_links; k++) {
				/* filter the reverse path */
				link =
				    (network_link_t *) & (adj_netw_node->
							  links[k]);
				r_lid = link->link_info.remote_lid;
				r_port = link->link_info.remote_port;
				/* theoretically, we could ignore every reverse
				   path (for multigraphs), not only the one with
				   the same port => room for future optimization
				 */
				if (l_lid == r_lid && l_port == r_port)
					continue;

				init_ccdg_edge(ccdg_edge_iter++,
					       link->link_info);
			}

			init_ccdg_node(ccdg_node_iter, channel_id, num_edges,
				       ccdg_edges, netw_link_iter);
		}
	}

	/* sort the node array to find individual nodes easier with bsearch */
	sort_ccdg_nodes_by_channel_id(ccdg);

	/* now we need to add the last piece of information to the ccdg edge
	   and connect the ccdg_nodes and corresponding network links
	 */
	ccdg_node_iter = (ccdg_node_t *) ccdg->nodes;
	for (i = 0; i < ccdg->num_nodes; i++, ccdg_node_iter++) {
		for (j = 0; j < ccdg_node_iter->num_edges; j++) {
			ccdg_edge =
			    (ccdg_edge_t *) & (ccdg_node_iter->edges[j]);
			channel_id = ccdg_edge->to_channel_id;
			ccdg_edge->to_ccdg_node =
			    get_ccdg_node_by_channel_id(ccdg, channel_id);
			CL_ASSERT(ccdg_edge->to_ccdg_node);
		}

		if (ccdg_node_iter->corresponding_netw_link)
			ccdg_node_iter->corresponding_netw_link->
			    corresponding_ccdg_node = ccdg_node_iter;
		else {
			/* make sure it's a fake channel otherwise */
			CL_ASSERT(ccdg_node_iter->channel_id.local_lid ==
				  ccdg_node_iter->channel_id.remote_lid
				  && (0 ==
				      (ccdg_node_iter->channel_id.
				       local_port | ccdg_node_iter->channel_id.
				       remote_port)));
		}
	}

	OSM_LOG_EXIT(mgr->p_log);
	return 0;
}

/* traverse subnet to gather information about the connected switches */
static int nue_discover_network(void *context)
{
	nue_context_t *nue_ctx = (nue_context_t *) context;
	osm_ucast_mgr_t *mgr = NULL;
	cl_qmap_t *switch_tbl = NULL;
	cl_map_item_t *item = NULL;
	osm_switch_t *sw = NULL;
	osm_physp_t *physp_ptr = NULL;
	network_t *network = NULL;
	uint64_t i = 0, j = 0;
	uint64_t total_num_destination_lids = 0, total_num_switches = 0;
	uint32_t total_num_sw_to_sw_links = 0;
	uint64_t init_weight = 0;
	ib_net16_t lid = 0, r_lid = 0;
	ib_net64_t guid = 0;
	uint8_t lmc = 0;
	uint8_t num_base_terminals = 0, num_terminals = 0;
	uint8_t num_sw_to_sw_links = 0;
	network_node_t *netw_node_iter = NULL;
	network_link_t *link = NULL, *links = NULL, *realloc_links = NULL;
	network_link_t *netw_link_iter = NULL;
	osm_node_t *r_node = NULL;
	uint8_t port = 0, r_port = 0;
	boolean_t has_fdr10 = FALSE;
	int err = 0;

	if (nue_ctx)
		mgr = (osm_ucast_mgr_t *) nue_ctx->mgr;
	else
		return -1;
	has_fdr10 = (1 == mgr->p_subn->opt.fdr10) ? TRUE : FALSE;

	OSM_LOG_ENTER(mgr->p_log);
	OSM_LOG(mgr->p_log, OSM_LOG_INFO,
		"Building network graph for nue routing\n");

	/* if this pointer isn't NULL, this is a reroute step;
	   old context will be destroyed and we set up a new/clean context
	 */
	if (nue_ctx->network.nodes) {
		destroy_context(nue_ctx);
		create_context(nue_ctx);
	}

	/* acquire basic information about the network */
	nue_ctx->max_vl = get_max_num_vls(mgr);
	if (nue_ctx->max_vl != 1 && !(mgr->p_subn->opt.qos)) {
		OSM_LOG(mgr->p_log, OSM_LOG_INFO,
			"WRN NUE48: Nue routing with nue_max_num_vls == %" PRIu8
			" should enable QoS for valid SL2VL mapping, "
			" using nue_max_num_vls 1\n",
			nue_ctx->max_vl);
		nue_ctx->max_vl = 1;
	}
	total_num_destination_lids = get_base_lids_and_number_of_lids(nue_ctx);
	init_weight = total_num_destination_lids * total_num_destination_lids;

	switch_tbl = &(mgr->p_subn->sw_guid_tbl);
	total_num_switches = cl_qmap_count(switch_tbl);

	network = (network_t *) & (nue_ctx->network);
	network->num_nodes = total_num_switches;
	network->nodes =
	    (network_node_t *) malloc(total_num_switches *
				      sizeof(network_node_t));
	if (!network->nodes) {
		OSM_LOG(mgr->p_log, OSM_LOG_ERROR,
			"ERR NUE02: can't allocate memory for network nodes\n");
		destroy_context(context);
		return -1;
	}
	for (i = 0, netw_node_iter = network->nodes; i < total_num_switches;
	     i++, netw_node_iter++)
		construct_network_node(netw_node_iter);

	netw_node_iter = (network_node_t *) network->nodes;
	for (item = cl_qmap_head(switch_tbl); item != cl_qmap_end(switch_tbl);
	     item = cl_qmap_next(item)) {
		sw = (osm_switch_t *) item;
		guid = osm_node_get_node_guid(sw->p_node);
		OSM_LOG(mgr->p_log, OSM_LOG_DEBUG,
			"Processing switch with GUID 0x%016" PRIx64 "\n",
			cl_ntoh64(guid));

		lid = osm_node_get_base_lid(sw->p_node, 0);
		num_base_terminals = 0;

		/* add SP0 to number of CA conneted to a switch */
		lmc = osm_node_get_lmc(sw->p_node, 0);
		num_terminals = (1 << lmc);

		/* we start with the maximum and resize the link array later */
		links =
		    (network_link_t *) malloc(sw->num_ports *
					      sizeof(network_link_t));
		if (!links) {
			OSM_LOG(mgr->p_log, OSM_LOG_ERROR,
				"ERR NUE03: cannot allocate memory for link\n");
			destroy_context(context);
			return -1;
		}
		for (i = 0, netw_link_iter = links; i < sw->num_ports;
		     i++, netw_link_iter++)
			construct_network_link(netw_link_iter);

		/* iterate over all ports (including management port 0) */
		for (port = 0, i = 0; port < sw->num_ports; port++) {
			/* get the remote node behind this port */
			r_node =
			    osm_node_get_remote_node(sw->p_node, port, &r_port);
			/* if there is no remote node on this port or it is
			   the same switch, then try next port
			 */
			if (!r_node || r_node->sw == sw)
				continue;
			/* make sure the link is healthy */
			physp_ptr = osm_node_get_physp_ptr(sw->p_node, port);
			if (!physp_ptr || !osm_link_is_healthy(physp_ptr))
				continue;
			/* if there is a Hca connected, then count and cycle */
			if (!r_node->sw) {
				num_base_terminals++;
				lmc =
				    osm_node_get_lmc(r_node, (uint32_t) r_port);
				num_terminals += (1 << lmc);
				continue;
			}
			/* filter out throttled links to improve performance */
			if (mgr->p_subn->opt.avoid_throttled_links &&
			    osm_link_is_throttled(physp_ptr, has_fdr10)) {
				OSM_LOG(mgr->p_log, OSM_LOG_INFO,
					"Detected and ignoring throttled link:"
					" 0x%016" PRIx64 "/P%" PRIu8
					" <--> 0x%016" PRIx64 "/P%" PRIu8 "\n",
					cl_ntoh64(osm_node_get_node_guid(sw->p_node)),
					port,
					cl_ntoh64(osm_node_get_node_guid(r_node)),
					r_port);
				continue;
			}
			/* initialize link with all we know right now */
			r_lid = osm_node_get_base_lid(r_node, 0);
			init_network_link(&(links[i++]), lid, port, r_lid,
					  r_port, init_weight);
		}
		num_sw_to_sw_links = (uint8_t) i;
		total_num_sw_to_sw_links += (uint32_t) num_sw_to_sw_links;

		/* we don't increase in size, so omit check of the return val */
		if (num_sw_to_sw_links < sw->num_ports) {
			realloc_links =
			    realloc(links,
				    num_sw_to_sw_links *
				    sizeof(network_link_t));

			if (num_sw_to_sw_links && !realloc_links)
				OSM_LOG(mgr->p_log, OSM_LOG_INFO,
					"WRN NUE42: cannot resize memory for links\n");
			else if (!num_sw_to_sw_links)
				links = NULL;
			else
				links = realloc_links;
		}

		/* initialize everything for the internal node representation */
		err = init_network_node(netw_node_iter++, lid, guid,
					num_base_terminals, num_terminals,
					num_sw_to_sw_links, links, sw);
		if (err) {
			OSM_LOG(mgr->p_log, OSM_LOG_INFO,
				"ERR NUE46: cannot allocate memory for stack_used_links\n");
			destroy_context(context);
			return -1;
		}
	}

	/* sort the node array to find individual nodes easier with bsearch */
	sort_network_nodes_by_lid(network);

	/* now we need to add the last piece of information to the links */
	for (i = 0, netw_node_iter = network->nodes; i < network->num_nodes;
	     i++, netw_node_iter++) {
		for (j = 0, netw_link_iter = netw_node_iter->links;
		     j < netw_node_iter->num_links; j++, netw_link_iter++) {
			link = netw_link_iter;
			lid = link->link_info.remote_lid;
			link->to_network_node =
			    get_network_node_by_lid(network, lid);
			CL_ASSERT(link->to_network_node);
		}
	}

	/* print the discovered network graph */
	if (OSM_LOG_IS_ACTIVE_V2(mgr->p_log, OSM_LOG_DEBUG))
		print_network(mgr, network);

	err =
	    build_complete_cdg(mgr, &(nue_ctx->network), &(nue_ctx->ccdg),
			       total_num_sw_to_sw_links);
	if (err) {
		destroy_context(context);
		return -1;
	}

	/* print the constructed complete channel dependency graph */
	if (OSM_LOG_IS_ACTIVE_V2(mgr->p_log, OSM_LOG_DEBUG)) {
		OSM_LOG(mgr->p_log, OSM_LOG_DEBUG,
			"Complete channel dependency graph of the discovered network\n");
		print_ccdg(mgr, &(nue_ctx->ccdg), FALSE);
	}

	OSM_LOG_EXIT(mgr->p_log);
	return 0;
}

#if defined (ENABLE_METIS_FOR_NUE)
static int distribute_lids_with_metis(nue_context_t * nue_ctx,
				      const boolean_t include_sw)
{
	osm_switch_t *sw = NULL;
	osm_node_t *r_node = NULL;
	osm_port_t *port = NULL;
	osm_physp_t *physp_ptr = NULL;
	uint8_t ntype = 0;
	ib_net16_t *desti_arr = NULL, *dlid_iter = NULL;
	ib_net16_t *dlid_arr_iter[IB_MAX_NUM_VLS];
	ib_net16_t r_lid = 0;
	uint16_t i = 0, num_desti = 0;
	uint8_t num_adj = 0, l_port = 0, r_port = 0;
	uint32_t total_num_adjnc = 0;
	network_t *network = NULL;
	network_node_t *netw_node_iter = NULL;
	metis_context_t metis_ctx;
	idx_t *xadj_iter = NULL, *adjncy_iter = NULL;
	uint8_t partition = 0;
	int ret = METIS_OK;

	CL_ASSERT(nue_ctx);

	desti_arr = (ib_net16_t *) nue_ctx->destinations[0];
	num_desti = nue_ctx->num_destinations[0];

	construct_metis_context(&metis_ctx);
	init_metis_context(&metis_ctx, (idx_t) num_desti,
			   (idx_t) nue_ctx->max_vl, (idx_t) - 1, (idx_t) 0);

	/* theoretically, sorting this array might be not the best idea for
	   later iterations over the destinations for each routing step with
	   Dijkstra's since we might lose temporal locality of HCAs; at least
	   for dfsssp processing all CAs at the same switch before jumping to
	   the next sw yields better results => room for future optimizations
	 */
	sort_destinations_by_lid(desti_arr, (uint32_t) num_desti);

	/* count the number links (sw<->sw and ca<->sw) in the subnet */
	network = (network_t *) & (nue_ctx->network);
	for (i = 0, netw_node_iter = network->nodes, total_num_adjnc = 0;
	     i < network->num_nodes; i++, netw_node_iter++)
		total_num_adjnc +=
		    (netw_node_iter->num_base_terminals +
		     netw_node_iter->num_links);

	metis_ctx.xadj =
	    (idx_t *) malloc((*(metis_ctx.nvtxs) + 1) * sizeof(idx_t));
	if (!metis_ctx.xadj) {
		OSM_LOG(nue_ctx->mgr->p_log, OSM_LOG_ERROR,
			"ERR NUE10: can't allocate memory for xadj\n");
		destroy_metis_context(&metis_ctx);
		return -1;
	}
	metis_ctx.xadj[0] = 0;

	metis_ctx.adjncy =
	    (idx_t *) malloc(2 * total_num_adjnc * sizeof(idx_t));
	if (total_num_adjnc && !metis_ctx.adjncy) {
		OSM_LOG(nue_ctx->mgr->p_log, OSM_LOG_ERROR,
			"ERR NUE11: can't allocate memory for adjncy\n");
		destroy_metis_context(&metis_ctx);
		return -1;
	}

	metis_ctx.part = (idx_t *) malloc(*(metis_ctx.nvtxs) * sizeof(idx_t));
	if (!metis_ctx.part) {
		OSM_LOG(nue_ctx->mgr->p_log, OSM_LOG_ERROR,
			"ERR NUE12: can't allocate memory for partition\n");
		destroy_metis_context(&metis_ctx);
		return -1;
	}

	/* fill up the xadj and adjncy arrays */
	dlid_iter = (ib_net16_t *) desti_arr;
	xadj_iter = (idx_t *) metis_ctx.xadj;
	adjncy_iter = (idx_t *) metis_ctx.adjncy;
	for (i = 0; i < nue_ctx->num_destinations[0];
	     i++, dlid_iter++, xadj_iter++) {
		port = osm_get_port_by_lid(nue_ctx->mgr->p_subn, *dlid_iter);
		ntype = osm_node_get_type(port->p_node);
		/* if base dlid is a CA then adjncy is only a switch */
		if (ntype == IB_NODE_TYPE_CA) {
			r_node =
			    osm_node_get_remote_node(port->p_node,
						     port->p_physp->port_num,
						     &r_port);
			if (!r_node
			    || osm_node_get_type(r_node) !=
			    IB_NODE_TYPE_SWITCH) {
				OSM_LOG(nue_ctx->mgr->p_log, OSM_LOG_ERROR,
					"ERR NUE13: found CA attached to"
					" something other than a switch; nue"
					" cannot handle this case\n");
				destroy_metis_context(&metis_ctx);
				return -1;
			}
			r_lid = osm_node_get_base_lid(r_node, 0);

			xadj_iter[1] = xadj_iter[0] + 1;
			*adjncy_iter++ =
			    get_lid(desti_arr, num_desti, r_lid) - desti_arr;
		} /* otherwise we have to check a bunch of ports */
		else if (ntype == IB_NODE_TYPE_SWITCH) {
			sw = port->p_node->sw;
			num_adj = 0;
			for (l_port = 0; l_port < sw->num_ports; l_port++) {
				/* get the remote node behind this port */
				r_node =
				    osm_node_get_remote_node(sw->p_node, l_port,
							     &r_port);
				/* if there is no remote node on this port
				   or it is the same switch, then try next port
				 */
				if (!r_node || r_node->sw == sw)
					continue;
				/* make sure the link is healthy */
				physp_ptr =
				    osm_node_get_physp_ptr(sw->p_node, l_port);
				if (!physp_ptr
				    || !osm_link_is_healthy(physp_ptr))
					continue;

				ntype = osm_node_get_type(r_node);
				if (ntype == IB_NODE_TYPE_CA)
					r_lid =
					    osm_node_get_base_lid(r_node,
								  r_port);
				else if (ntype == IB_NODE_TYPE_SWITCH)
					r_lid =
					    osm_node_get_base_lid(r_node, 0);
				*adjncy_iter++ =
				    get_lid(desti_arr, num_desti,
					    r_lid) - desti_arr;
				num_adj++;
			}
			xadj_iter[1] = xadj_iter[0] + num_adj;
		}
	}

	/* metis doesnt like nparts == 1 so we fake it if needed */
	if (*(metis_ctx.nparts) == 1)
		memset(metis_ctx.part, 0, *(metis_ctx.nvtxs) * sizeof(idx_t));
	else
		ret =
		    METIS_PartGraphKway(metis_ctx.nvtxs, metis_ctx.ncon,
					metis_ctx.xadj, metis_ctx.adjncy, NULL,
					NULL, NULL, metis_ctx.nparts, NULL,
					NULL, metis_ctx.options,
					metis_ctx.objval, metis_ctx.part);
	if (ret != METIS_OK) {
		OSM_LOG(nue_ctx->mgr->p_log, OSM_LOG_ERROR,
			"ERR NUE20: metis partitioning failed (ret=%d)\n", ret);
		destroy_metis_context(&metis_ctx);
		return -1;
	}

	memset(nue_ctx->num_destinations, 0, IB_MAX_NUM_VLS * sizeof(uint16_t));
	for (i = 0; i < *(metis_ctx.nvtxs); i++)
		nue_ctx->num_destinations[metis_ctx.part[i]]++;

	for (i = 0; i < *(metis_ctx.nparts); i++) {
		nue_ctx->destinations[i] =
		    (ib_net16_t *) malloc(nue_ctx->num_destinations[i] *
					  sizeof(ib_net16_t));
		if (nue_ctx->num_destinations[i] && !nue_ctx->destinations[i]) {
			OSM_LOG(nue_ctx->mgr->p_log, OSM_LOG_ERROR,
				"ERR NUE14: cannot allocate dlid array\n");
			destroy_metis_context(&metis_ctx);
			free(desti_arr);
			return -1;
		}
	}

	memset(nue_ctx->num_destinations, 0, IB_MAX_NUM_VLS * sizeof(uint16_t));
	memcpy(dlid_arr_iter, nue_ctx->destinations,
	       IB_MAX_NUM_VLS * sizeof(ib_net16_t *));
	for (i = 0; i < *(metis_ctx.nvtxs); i++) {
		if (!include_sw) {
			port =
			    osm_get_port_by_lid(nue_ctx->mgr->p_subn,
						desti_arr[i]);
			ntype = osm_node_get_type(port->p_node);
			if (ntype == IB_NODE_TYPE_SWITCH)
				continue;
		}
		partition = (uint8_t) metis_ctx.part[i];
		*dlid_arr_iter[partition] = desti_arr[i];
		dlid_arr_iter[partition]++;
		nue_ctx->num_destinations[partition]++;
	}

	destroy_metis_context(&metis_ctx);
	free(desti_arr);
	return 0;
}
#else
static int distribute_lids_semi_randomly(nue_context_t * nue_ctx,
					 const boolean_t include_sw)
{
	uint8_t vl = 0;
	uint16_t num_dest = 0, temp_sum = 0, i = 0, max_num_desti_per_layer = 0;
	ib_net16_t *all_dest = NULL, *dlid_iter = NULL, *partition = NULL;
	osm_port_t *dest_port = NULL;
	uint8_t ntype = 0;

	CL_ASSERT(nue_ctx && nue_ctx->destinations[0]);

	all_dest = nue_ctx->destinations[0];
	num_dest = nue_ctx->num_destinations[0];

	for (vl = nue_ctx->max_vl - 1; vl > 0; vl--) {
		nue_ctx->num_destinations[vl] = num_dest / nue_ctx->max_vl;
		temp_sum += nue_ctx->num_destinations[vl];
		if (max_num_desti_per_layer < nue_ctx->num_destinations[vl])
			max_num_desti_per_layer = nue_ctx->num_destinations[vl];
	}
	nue_ctx->num_destinations[0] = num_dest - temp_sum;
	if (max_num_desti_per_layer < nue_ctx->num_destinations[0])
		max_num_desti_per_layer = nue_ctx->num_destinations[0];

	for (vl = 0; vl < nue_ctx->max_vl; vl++) {
		nue_ctx->destinations[vl] =
		    (ib_net16_t *) malloc(max_num_desti_per_layer *
					  sizeof(ib_net16_t));
		if (!nue_ctx->destinations[vl]) {
			OSM_LOG(nue_ctx->mgr->p_log, OSM_LOG_ERROR,
				"ERR NUE09: cannot allocate memory for"
				" destinations[%" PRIu8 "]\n", vl);
			return -1;
		}
	}

	memset(nue_ctx->num_destinations, 0, IB_MAX_NUM_VLS * sizeof(uint16_t));
	dlid_iter = (ib_net16_t *) all_dest;
	for (i = 0, vl = 0; i < num_dest; i++, dlid_iter++) {
		if (!include_sw) {
			dest_port =
			    osm_get_port_by_lid(nue_ctx->mgr->p_subn,
						*dlid_iter);
			ntype = osm_node_get_type(dest_port->p_node);
			if (ntype == IB_NODE_TYPE_SWITCH)
				continue;
		}
		partition = (ib_net16_t *) nue_ctx->destinations[vl];
		partition[nue_ctx->num_destinations[vl]] = *dlid_iter;
		nue_ctx->num_destinations[vl]++;
		vl = (vl + 1) % nue_ctx->max_vl;
	}
	free(all_dest);

	return 0;
}
#endif

static inline int distribute_lids_onto_virtual_layers(nue_context_t * nue_ctx,
						      const boolean_t
						      include_sw)
{
	CL_ASSERT(nue_ctx);
	OSM_LOG(nue_ctx->mgr->p_log, OSM_LOG_INFO,
		"Distributing destination lids onto available VLs\n");

#if defined (ENABLE_METIS_FOR_NUE)
	return distribute_lids_with_metis(nue_ctx, include_sw);
#else
	return distribute_lids_semi_randomly(nue_ctx, include_sw);
#endif
}

/* returns the input lid if it belongs to a switch or the lid of the adjacent
   switch otherwise
 */
static inline ib_net16_t get_switch_lid(const osm_ucast_mgr_t * mgr,
					const ib_net16_t lid)
{
	osm_node_t *o_rem_node = NULL;
	osm_port_t *o_port = NULL;
	uint8_t rem_port = 0;
	ib_net16_t switch_lid = 0;

	CL_ASSERT(mgr && lid > 0);

	switch_lid = lid;
	o_port = osm_get_port_by_lid(mgr->p_subn, lid);
	CL_ASSERT(o_port);
	if (osm_node_get_type(o_port->p_node) == IB_NODE_TYPE_CA) {
		o_rem_node =
		    osm_node_get_remote_node(o_port->p_node,
					     o_port->p_physp->port_num,
					     &rem_port);
		CL_ASSERT(o_rem_node);
		switch_lid = osm_node_get_base_lid(o_rem_node, 0);
	}
	return switch_lid;
}

/* the function determines the convex hull of a subset of nodes of the network,
   this convex hull is the enclosure of all shortest paths between these nodes,
   therefore we calculate a spanning tree from each node and which is traversed
   in the opposite direction to collect all nodes along the shortest paths
 */
static int calculate_convex_subnetwork(const osm_ucast_mgr_t * mgr,
				       const network_t * network,
				       ib_net16_t * destinations,
				       const uint16_t num_destinations)
{
	network_node_t *netw_node_iter1 = NULL, *netw_node_iter2 = NULL;
	network_node_t *network_node1 = NULL, *network_node2 = NULL;
	network_node_t *nodeU = NULL, *nodeV = NULL;
	network_node_t **fifoQ = NULL, **fifoQ_head = NULL, **fifoQ_tail = NULL;
	network_link_t *netw_link_iter = NULL;
	ib_net16_t *desti_iter = NULL;
	ib_net16_t dlid = 0;
	uint16_t i = 0, j = 0, k = 0;

	CL_ASSERT(mgr && network && destinations && num_destinations > 0);
	OSM_LOG_ENTER(mgr->p_log);

	fifoQ =
	    (network_node_t **) calloc(network->num_nodes,
				       sizeof(network_node_t *));
	if (!fifoQ) {
		OSM_LOG(mgr->p_log, OSM_LOG_ERROR,
			"ERR NUE21: cannot allocate memory for the fifo queue\n");
		return -1;
	}

	for (i = 0, netw_node_iter1 = network->nodes; i < network->num_nodes;
	     i++, netw_node_iter1++) {
		netw_node_iter1->in_convex_hull = FALSE;
		netw_node_iter1->has_adj_destinations = FALSE;
	}

	/* switches adjacent to terminals in the desti_array are definitely
	   in the convex hull as well
	 */
	for (i = 0, desti_iter = destinations; i < num_destinations;
	     i++, desti_iter++) {
		dlid = get_switch_lid(mgr, *desti_iter);
		network_node1 = get_network_node_by_lid(network, dlid);
		CL_ASSERT(network_node1);
		network_node1->in_convex_hull = TRUE;
		network_node1->has_adj_destinations = TRUE;
	}

	for (i = 0, netw_node_iter1 = network->nodes; i < network->num_nodes;
	     i++, netw_node_iter1++) {
		network_node1 = netw_node_iter1;

		if (!network_node1->in_convex_hull)
			continue;

		for (j = 0, netw_node_iter2 = network->nodes;
		     j < network->num_nodes; j++, netw_node_iter2++) {
			netw_node_iter2->distance = INFINITY;
			netw_node_iter2->processed = FALSE;
		}

		network_node1->distance = 0;
		network_node1->processed = TRUE;

		fifoQ_head = fifoQ_tail = fifoQ;
		*fifoQ_tail++ = network_node1;

		while (fifoQ_tail - fifoQ_head > 0) {
			nodeU = *fifoQ_head++;
			for (j = 0, netw_link_iter = nodeU->links;
			     j < nodeU->num_links; j++, netw_link_iter++) {
				nodeV = netw_link_iter->to_network_node;
				if (nodeV->distance == INFINITY) {
					nodeV->distance = nodeU->distance + 1;
					*fifoQ_tail++ = nodeV;
				}
			}
		}

		for (j = 0, netw_node_iter2 = network->nodes;
		     j < network->num_nodes; j++, netw_node_iter2++) {
			network_node2 = netw_node_iter2;

			if (!network_node2->in_convex_hull
			    || network_node2->processed)
				continue;

			network_node2->processed = TRUE;

			fifoQ_head = fifoQ_tail = fifoQ;
			*fifoQ_tail++ = network_node2;

			while (fifoQ_tail - fifoQ_head > 0) {
				nodeV = *fifoQ_head++;
				for (k = 0, netw_link_iter = nodeV->links;
				     k < nodeV->num_links;
				     k++, netw_link_iter++) {
					nodeU = netw_link_iter->to_network_node;
					if (nodeU->processed)
						continue;
					if (nodeU->distance + 1 ==
					    nodeV->distance) {
						nodeU->in_convex_hull = TRUE;
						nodeU->processed = TRUE;
						*fifoQ_tail++ = nodeU;
					}
				}
			}
		}
	}

	free(fifoQ);

	OSM_LOG_EXIT(mgr->p_log);
	return 0;
}

static void determine_num_adj_terminals_in_convex_hull(const osm_ucast_mgr_t *
						       mgr,
						       const network_t *
						       network,
						       ib_net16_t *
						       destinations,
						       const uint16_t
						       num_destinations)
{
	ib_net16_t dlid = 0, *desti_iter = NULL;
	network_node_t *network_node = NULL;
	uint16_t i = 0;

	CL_ASSERT(mgr && network && destinations && num_destinations > 0);

	for (i = 0, desti_iter = destinations; i < num_destinations;
	     i++, desti_iter++) {
		dlid = get_switch_lid(mgr, *desti_iter);
		if (dlid != *desti_iter) {
			network_node = get_network_node_by_lid(network, dlid);
			CL_ASSERT(network_node && network_node->in_convex_hull);
			network_node->num_adj_terminals_in_convex_hull += 1;
		}
	}
}

static void reset_delta_for_betw_centrality(const network_t * network)
{
	network_node_t *netw_node_iter = NULL;
	uint16_t i = 0;

	CL_ASSERT(network);

	for (i = 0, netw_node_iter = network->nodes; i < network->num_nodes;
	     i++, netw_node_iter++)
		netw_node_iter->delta = 0.0;
}

static void reset_sigma_distance_Ps_for_betw_centrality(const network_t *
							network)
{
	network_node_t *netw_node_iter = NULL;
	uint16_t i = 0;

	CL_ASSERT(network);

	for (i = 0, netw_node_iter = network->nodes; i < network->num_nodes;
	     i++, netw_node_iter++) {
		netw_node_iter->num_elem_in_Ps = 0;
		netw_node_iter->sigma = 0;
		netw_node_iter->distance = INFINITY;
	}
}

/* the function implements a slightly modified version of Brandes' algorithms
   for betweenness centrality; we calculate this value only for switches, since
   terminals shouldn't be the most central node w.r.t. a convex hull anyways
 */
static int get_central_node_wrt_subnetwork(const osm_ucast_mgr_t * mgr,
					   const network_t * network,
					   ib_net16_t * destinations,
					   const uint16_t num_destinations,
					   network_node_t ** central_node,
					   uint16_t * central_node_index)
{
	network_node_t *netw_node_iter = NULL;
	network_node_t *network_node = NULL, *nodeU = NULL, *nodeV = NULL;
	network_node_t **fifoQ = NULL, **fifoQ_head = NULL, **fifoQ_tail = NULL;
	network_node_t **lifoQ = NULL, **lifoQ_head =
	    NULL, **restore_lifoQ_head = NULL;
	network_link_t *netw_link_iter = NULL;
	double max_betw_centrality = -1.0;
	uint16_t i = 0, j = 0, k = 0, update_for_adj = 0;

	CL_ASSERT(mgr && network);
	OSM_LOG_ENTER(mgr->p_log);

	fifoQ =
	    (network_node_t **) calloc(network->num_nodes,
				       sizeof(network_node_t *));
	if (!fifoQ) {
		OSM_LOG(mgr->p_log, OSM_LOG_ERROR,
			"ERR NUE22: cannot allocate memory for the fifo queue\n");
		return -1;
	}
	lifoQ =
	    (network_node_t **) calloc(network->num_nodes,
				       sizeof(network_node_t *));
	if (!lifoQ) {
		OSM_LOG(mgr->p_log, OSM_LOG_ERROR,
			"ERR NUE23: cannot allocate memory for the lifo queue\n");
		free(fifoQ);
		return -1;
	}

	for (i = 0, netw_node_iter = network->nodes; i < network->num_nodes;
	     i++, netw_node_iter++) {
		netw_node_iter->betw_centrality = 0.0;

		netw_node_iter->num_adj_terminals_in_convex_hull = 0;
		netw_node_iter->num_elem_in_Ps = 0;

		/* Ps holds a list of all shortest paths thru this node */
		if (netw_node_iter->Ps) {
			memset(netw_node_iter->Ps, 0,
			       netw_node_iter->num_links *
			       sizeof(network_node_t *));
		} else {
			netw_node_iter->Ps =
			    (network_node_t **) calloc(netw_node_iter->
						       num_links,
						       sizeof(network_node_t
							      *));
			if (netw_node_iter->num_links && !netw_node_iter->Ps) {
				OSM_LOG(mgr->p_log, OSM_LOG_ERROR,
					"ERR NUE24: cannot allocate memory for Ps array\n");
				while (netw_node_iter != network->nodes) {
					netw_node_iter--;
					free(netw_node_iter->Ps);
				}
				free(fifoQ);
				free(lifoQ);
				return -1;
			}
		}
	}

	determine_num_adj_terminals_in_convex_hull(mgr, network, destinations,
						   num_destinations);

	for (i = 0, netw_node_iter = network->nodes; i < network->num_nodes;
	     i++, netw_node_iter++) {
		network_node = netw_node_iter;
		if (!network_node->in_convex_hull)
			continue;

		reset_sigma_distance_Ps_for_betw_centrality(network);

		network_node->sigma = 1;
		network_node->distance = 0;

		lifoQ_head = lifoQ;
		fifoQ_head = fifoQ_tail = fifoQ;
		*fifoQ_tail++ = network_node;

		while (fifoQ_tail - fifoQ_head > 0) {
			nodeU = *fifoQ_head++;
			*lifoQ_head++ = nodeU;

			for (k = 0, netw_link_iter = nodeU->links;
			     k < nodeU->num_links; k++, netw_link_iter++) {
				nodeV = netw_link_iter->to_network_node;
				if (!nodeV->in_convex_hull)
					continue;
				if (nodeV->distance == INFINITY) {
					nodeV->distance = nodeU->distance + 1;
					*fifoQ_tail++ = nodeV;
				}
				if (nodeV->distance == nodeU->distance + 1) {
					nodeV->sigma += nodeU->sigma;
					/* if it crashes here then nodeU gets
					   added multiple times => we have to
					   redesign Ps
					 */
					CL_ASSERT(nodeV->num_elem_in_Ps <
						  nodeV->num_links);
					nodeV->Ps[nodeV->num_elem_in_Ps++] =
					    nodeU;
				}
			}
		}

		/* since we don't have the terminals stored we have to execute
		   the following loop multipe times (1x for the switch and
		   1x for each terminal which is in the convex hull)
		 */
		restore_lifoQ_head = lifoQ_head;
		for (j = 0;
		     j < network_node->num_adj_terminals_in_convex_hull + 1;
		     j++) {
			reset_delta_for_betw_centrality(network);

			lifoQ_head = restore_lifoQ_head;
			while (lifoQ_head - lifoQ > 0) {
				nodeV = *(--lifoQ_head);

				if (network_node != nodeV)
					update_for_adj =
					    nodeV->
					    num_adj_terminals_in_convex_hull;
				else if (j == 0)
					update_for_adj =
					    nodeV->
					    num_adj_terminals_in_convex_hull;
				else if (nodeV->
					 num_adj_terminals_in_convex_hull > 0)
					update_for_adj =
					    nodeV->
					    num_adj_terminals_in_convex_hull -
					    1;

				/* following not part of original Brandes' algo
				   but needed because we don't have terminals:
				   - delta(terminal)=0.0 => omit last term;
				   - sigma(terminal) is always sigma(adjSW)
				 */
				for (k = 0; k < update_for_adj; k++)
					nodeV->delta +=
					    (1.0 * nodeV->sigma) / nodeV->sigma;

				for (k = 0; k < nodeV->num_elem_in_Ps; k++) {
					nodeU = nodeV->Ps[k];
					nodeU->delta +=
					    (1.0 * nodeU->sigma) /
					    nodeV->sigma * (1 + nodeV->delta);
				}

				/* if j>0 then we simulate an terminal => means
				   we have to update the betw_centrality of
				   it's adjacent switch
				 */
				if (j > 0 || network_node != nodeV)
					nodeV->betw_centrality += nodeV->delta;
			}
		}
	}

	*central_node = NULL;
	*central_node_index = 0;
	for (i = 0, netw_node_iter = network->nodes; i < network->num_nodes;
	     i++, netw_node_iter++) {
		if (!netw_node_iter->in_convex_hull)
			continue;

		if (max_betw_centrality < netw_node_iter->betw_centrality) {
			*central_node = netw_node_iter;
			*central_node_index = i;
			max_betw_centrality = netw_node_iter->betw_centrality;
		}
	}

	free(fifoQ);
	free(lifoQ);

	OSM_LOG_EXIT(mgr->p_log);
	return 0;
}

/* callback function for the cl_heap to update the heap index */
static void update_netw_heap_index(const void *context, const size_t new_index)
{
	network_node_t *heap_elem = (network_node_t *) context;
	if (heap_elem)
		heap_elem->heap_index = new_index;
}

/* trivial spanning tree calculation for the network (similar to Dijkstra's
   algorithm) which includes the link weights, too, so that we don't end up
   with similar escape paths for each virtual layer
 */
static int calculate_spanning_tree_in_network(const osm_ucast_mgr_t * mgr,
					      network_t * network,
					      network_node_t * root_node)
{
	network_node_t *curr_node = NULL, *adj_node = NULL, *netw_node_iter =
	    NULL;
	network_link_t *curr_link = NULL, *netw_link_iter = NULL;
	cl_status_t ret = CL_SUCCESS;
	uint64_t new_distance = 0;
	uint16_t i = 0;

	CL_ASSERT(mgr && network && root_node);
	OSM_LOG_ENTER(mgr->p_log);

	/* build an 4-ary heap to find the node with minimum distance */
	if (!cl_is_heap_inited(&network->heap))
		ret =
		    cl_heap_init(&network->heap, (size_t) network->num_nodes, 4,
				 &update_netw_heap_index, NULL);
	else
		ret =
		    cl_heap_resize(&network->heap, (size_t) network->num_nodes);
	if (CL_SUCCESS != ret) {
		OSM_LOG(mgr->p_log, OSM_LOG_ERROR,
			"ERR NUE15: cannot allocate memory or resize heap\n");
		return -1;
	}

	for (i = 0, netw_node_iter = network->nodes; i < network->num_nodes;
	     i++, netw_node_iter++) {
		netw_node_iter->distance = INFINITY;
		netw_node_iter->escape_path = NULL;

		ret = cl_heap_insert(&network->heap, INFINITY, netw_node_iter);
		if (CL_SUCCESS != ret) {
			OSM_LOG(mgr->p_log, OSM_LOG_ERROR,
				"ERR NUE16: cl_heap_insert failed\n");
			return -1;
		}
	}

	/* we use the root_node as source in Dijkstra's algorithm to caluclate
	   a spanning tree for the network
	 */
	root_node->distance = 0;
	ret =
	    cl_heap_modify_key(&network->heap, root_node->distance,
			       root_node->heap_index);
	if (CL_SUCCESS != ret) {
		OSM_LOG(mgr->p_log, OSM_LOG_ERROR,
			"ERR NUE17: index out of bounds in cl_heap_modify_key\n");
		return -1;
	}

	curr_node = (network_node_t *) cl_heap_extract_root(&network->heap);
	while (curr_node) {
		/* add/update nodes which aren't discovered but accessible */
		for (i = 0, netw_link_iter = curr_node->links;
		     i < curr_node->num_links; i++, netw_link_iter++) {
			curr_link = netw_link_iter;
			adj_node = curr_link->to_network_node;
			new_distance = curr_node->distance + curr_link->weight;
			if (new_distance < adj_node->distance) {
				adj_node->escape_path = curr_link;
				adj_node->distance = new_distance;
				ret =
				    cl_heap_modify_key(&network->heap,
						       adj_node->distance,
						       adj_node->heap_index);
				if (CL_SUCCESS != ret) {
					OSM_LOG(mgr->p_log, OSM_LOG_ERROR,
						"ERR NUE18: index out of bounds in cl_heap_modify_key\n");
					return -1;
				}
			}
		}

		curr_node =
		    (network_node_t *) cl_heap_extract_root(&network->heap);
	}

	OSM_LOG_EXIT(mgr->p_log);
	return 0;
}

/* escape paths are initial channel dependencies which aren't to be 'broken',
   meaning: they are virtual paths building a backbone in case Nue runs into
   a impass and can't find all routes towards one destination
   escape paths are derived from a spanning tree rooted at the most central
   node w.r.t. the destination nodes in the current virtual layer
 */
static int mark_escape_paths(const osm_ucast_mgr_t * mgr, network_t * network,
			     const ccdg_t * ccdg, ib_net16_t * destinations,
			     const uint16_t num_destinations,
			     const boolean_t verify_network_integrity)
{
	network_node_t *central_node = NULL, *netw_node_iter = NULL;
	network_node_t *network_node1 = NULL, *network_node2 = NULL;
	network_link_t *curr_link = NULL, *next_link = NULL, *adj_link = NULL;
	network_link_t **links_going_into_central_node = NULL;
	channel_t channel_id;
	ccdg_node_t *curr_ccdg_node = NULL, *next_ccdg_node = NULL;
	ccdg_node_t *rev_curr_ccdg_node = NULL, *rev_next_ccdg_node = NULL;
	ib_net16_t lid = 0;
	uint16_t i = 0, j = 0, undiscovered = 0, central_node_index = 0;
	int err = 0;

	CL_ASSERT(mgr && network && ccdg && destinations
		  && num_destinations > 0);
	OSM_LOG_ENTER(mgr->p_log);
	OSM_LOG(mgr->p_log, OSM_LOG_INFO,
		"Initialize complete CDG with escape paths\n");

	err =
	    calculate_convex_subnetwork(mgr, network, destinations,
					num_destinations);
	if (err) {
		OSM_LOG(mgr->p_log, OSM_LOG_ERROR,
			"ERR NUE25: calculation of the convex subgraph failed;"
			" unable to proceed\n");
		return -1;
	}

	err =
	    get_central_node_wrt_subnetwork(mgr, network, destinations,
					    num_destinations, &central_node,
					    &central_node_index);
	if (err) {
		OSM_LOG(mgr->p_log, OSM_LOG_ERROR,
			"ERR NUE26: unable to find a central node; unable to"
			" proceed\n");
		return -1;
	}
	OSM_LOG(mgr->p_log, OSM_LOG_INFO, "central node:\n");
	print_network_node(mgr, central_node, central_node_index, FALSE);

	err = calculate_spanning_tree_in_network(mgr, network, central_node);
	if (err) {
		OSM_LOG(mgr->p_log, OSM_LOG_ERROR,
			"ERR NUE27: spanning tree algorithm for the escape"
			" paths failed; unable to proceed\n");
		return -1;
	} else if (verify_network_integrity) {
		/* sanity check to determine connectivity issues */
		for (i = 0, netw_node_iter = network->nodes;
		     i < network->num_nodes; i++, netw_node_iter++)
			undiscovered += (netw_node_iter->escape_path) ? 0 : 1;
		/* escape_path is not initialied for the central_node, but for
		   the rest it must be, or otherwise the network is bisected
		 */
		if (undiscovered > 1) {
			OSM_LOG(mgr->p_log, OSM_LOG_ERROR,
				"ERR NUE45: unsupported network state (detached"
				" and inaccessible switches found; gracefully"
				" shutdown this routing engine)\n");
			return -1;
		}
	}
	/* print the network after the spanning tree has been caluclated */
	if (OSM_LOG_IS_ACTIVE_V2(mgr->p_log, OSM_LOG_DEBUG))
		print_spanning_tree(mgr, network);

	links_going_into_central_node =
	    (network_link_t **) calloc(central_node->num_links,
				       sizeof(network_link_t *));
	if (central_node->num_links && !links_going_into_central_node) {
		OSM_LOG(mgr->p_log, OSM_LOG_ERROR,
			"ERR NUE28: cannot allocate memory for"
			" links_going_into_central_node array\n");
		return -1;
	}

	/* mark the escape paths in the complete CDG towards/from the root */
	for (i = 0, netw_node_iter = network->nodes; i < network->num_nodes;
	     i++, netw_node_iter++) {
		network_node1 = netw_node_iter;

		curr_link = network_node1->escape_path;
		while (curr_link) {
			lid = curr_link->link_info.local_lid;
			network_node2 = get_network_node_by_lid(network, lid);
			CL_ASSERT(network_node2);

			/* color the curr_link in the ccdg (i.e., ccdg node) */
			curr_ccdg_node = curr_link->corresponding_ccdg_node;
			CL_ASSERT(curr_ccdg_node);
			init_ccdg_escape_path_node_color(ccdg, curr_ccdg_node);

			next_link = network_node2->escape_path;
			if (!next_link) {
				/* all nodes should have an escape path, except
				   the root node
				 */
				CL_ASSERT(lid == central_node->lid);

				/* a hashmap may be better to do this job of
				   tracking which links are used and which are
				   not, but for now it does the trick and isn't
				   performance-critical => room for future
				   optimizations
				 */
				for (j = 0; j < central_node->num_links; j++) {
					if (!links_going_into_central_node[j]) {
						links_going_into_central_node[j]
						    = curr_link;
						break;
					} else
					    if (links_going_into_central_node[j]
						== curr_link)
						break;
				}

				break;
			}

			/* color the next_link in the ccdg (i.e., ccdg node) */
			next_ccdg_node = next_link->corresponding_ccdg_node;
			CL_ASSERT(next_ccdg_node);
			init_ccdg_escape_path_node_color(ccdg, next_ccdg_node);

			/* and we have to color the edge between next & curr */
			init_ccdg_escape_path_edge_color_betw_nodes(ccdg,
								    next_ccdg_node,
								    curr_ccdg_node);

			/* check if we have to add the reverse path as well */
			if (network_node1->has_adj_destinations) {
				channel_id =
				    get_inverted_channel_id(curr_link->
							    link_info);
				rev_curr_ccdg_node =
				    get_ccdg_node_by_channel_id(ccdg,
								channel_id);
				CL_ASSERT(rev_curr_ccdg_node);
				channel_id =
				    get_inverted_channel_id(next_link->
							    link_info);
				rev_next_ccdg_node =
				    get_ccdg_node_by_channel_id(ccdg,
								channel_id);
				CL_ASSERT(rev_next_ccdg_node);

				/* set ccdg node color of the reverse path */
				init_ccdg_escape_path_node_color(ccdg,
								 rev_curr_ccdg_node);
				init_ccdg_escape_path_node_color(ccdg,
								 rev_next_ccdg_node);
				/* including ccdg edges */
				init_ccdg_escape_path_edge_color_betw_nodes
				    (ccdg, rev_curr_ccdg_node,
				     rev_next_ccdg_node);

				/* and even color turns if there are any */
				for (j = 0; j < network_node2->num_links; j++) {
					adj_link =
					    network_node2->links[j].
					    to_network_node->escape_path;
					if (!adj_link)
						continue;
					if (curr_link->link_info.local_lid ==
					    adj_link->link_info.local_lid
					    && curr_link->link_info.
					    remote_lid !=
					    adj_link->link_info.remote_lid) {
						init_ccdg_escape_path_edge_color_betw_nodes
						    (ccdg, rev_curr_ccdg_node,
						     adj_link->
						     corresponding_ccdg_node);
					}
				}
			}

			curr_link = next_link;
		}
	}

	/* mark escape paths around central node */
	for (i = 0;
	     i < central_node->num_links && links_going_into_central_node[i];
	     i++) {
		curr_link = links_going_into_central_node[i];
		for (j = 0;
		     j < central_node->num_links
		     && links_going_into_central_node[j]; j++) {
			next_link = links_going_into_central_node[j];
			if (curr_link == next_link)
				continue;

			CL_ASSERT(curr_link && next_link);
			curr_ccdg_node = curr_link->corresponding_ccdg_node;
			rev_next_ccdg_node =
			    get_ccdg_node_by_channel_id(ccdg,
							get_inverted_channel_id
							(next_link->link_info));
			CL_ASSERT(curr_ccdg_node && rev_next_ccdg_node);

			/* set color of links going into/out of central node */
			init_ccdg_escape_path_node_color(ccdg, curr_ccdg_node);
			init_ccdg_escape_path_node_color(ccdg,
							 rev_next_ccdg_node);
			/* including ccdg edges */
			init_ccdg_escape_path_edge_color_betw_nodes(ccdg,
								    rev_next_ccdg_node,
								    curr_ccdg_node);
		}
	}

	free(links_going_into_central_node);

	OSM_LOG_EXIT(mgr->p_log);
	return 0;
}

#if defined (_DEBUG_)
static boolean_t deep_cpy_ccdg(const osm_ucast_mgr_t * mgr,
			       const ccdg_t * in_ccdg, ccdg_t * out_ccdg)
{
	ccdg_node_t *in_ccdg_node_iter = NULL, *out_ccdg_node_iter = NULL;
	ccdg_node_t *ccdg_node_iter = NULL;
	ccdg_edge_t *in_ccdg_edge_iter = NULL, *out_ccdg_edge_iter = NULL;
	uint32_t i = 0, j = 0, k = 0;

	CL_ASSERT(in_ccdg && in_ccdg->nodes);
	OSM_LOG_ENTER(mgr->p_log);

	if (!out_ccdg->nodes) {
		out_ccdg->nodes =
		    (ccdg_node_t *) malloc(in_ccdg->num_nodes *
					   sizeof(ccdg_node_t));
		if (!out_ccdg->nodes) {
			OSM_LOG(mgr->p_log, OSM_LOG_ERROR,
				"ERR NUE29: cannot allocate memory for ccdg nodes\n");
			return FALSE;
		}
	} else {
		for (i = 0, out_ccdg_node_iter = out_ccdg->nodes;
		     i < out_ccdg->num_nodes; i++, out_ccdg_node_iter++) {
			if (out_ccdg_node_iter->edges)
				free(out_ccdg_node_iter->edges);
		}
	}
	memset(out_ccdg->nodes, 0, in_ccdg->num_nodes * sizeof(ccdg_node_t));
	out_ccdg->num_nodes = 0;

	for (i = 0, in_ccdg_node_iter = in_ccdg->nodes, out_ccdg_node_iter =
	     out_ccdg->nodes; i < in_ccdg->num_nodes;
	     i++, in_ccdg_node_iter++) {
		if (get_ccdg_node_color(in_ccdg, in_ccdg_node_iter) <
		    ESCAPEPATHCOLOR)
			continue;

		out_ccdg_node_iter->channel_id = in_ccdg_node_iter->channel_id;
		out_ccdg_node_iter->status = WHITE;
		out_ccdg_node_iter->edges =
		    (ccdg_edge_t *) calloc(in_ccdg_node_iter->num_edges,
					   sizeof(ccdg_edge_t));
		if (!out_ccdg_node_iter->edges) {
			OSM_LOG(mgr->p_log, OSM_LOG_ERROR,
				"ERR NUE30: cannot allocate memory for ccdg edges\n");
			destroy_ccdg(out_ccdg);
			return FALSE;
		}

		for (j = 0, in_ccdg_edge_iter =
		     in_ccdg_node_iter->edges, out_ccdg_edge_iter =
		     out_ccdg_node_iter->edges;
		     j < in_ccdg_node_iter->num_edges;
		     j++, in_ccdg_edge_iter++) {
			if (get_ccdg_edge_color(in_ccdg, in_ccdg_edge_iter) <
			    ESCAPEPATHCOLOR)
				continue;

			out_ccdg_edge_iter->to_channel_id =
			    in_ccdg_edge_iter->to_channel_id;

			out_ccdg_edge_iter++;
			out_ccdg_node_iter->num_edges++;
		}

		out_ccdg_node_iter++;
		out_ccdg->num_nodes++;
	}

	for (i = 0, out_ccdg_node_iter = out_ccdg->nodes;
	     i < out_ccdg->num_nodes; i++, out_ccdg_node_iter++) {
		for (j = 0, out_ccdg_edge_iter = out_ccdg_node_iter->edges;
		     j < out_ccdg_node_iter->num_edges;
		     j++, out_ccdg_edge_iter++) {
			for (k = 0, ccdg_node_iter = out_ccdg->nodes;
			     k < out_ccdg->num_nodes; k++, ccdg_node_iter++) {
				if (0 ==
				    compare_two_channel_id(&
							   (out_ccdg_edge_iter->
							    to_channel_id),
							   &(ccdg_node_iter->
							     channel_id))) {
					out_ccdg_edge_iter->to_ccdg_node =
					    ccdg_node_iter;
					break;
				}
			}
		}
	}

	OSM_LOG_EXIT(mgr->p_log);
	return TRUE;
}

static boolean_t is_channel_id_in_verify_ccdg_node_list(const ccdg_t * ccdg,
							const channel_t *
							channel_id,
							ccdg_node_t **
							out_ccdg_node)
{
	ccdg_node_t *ccdg_node_iter = NULL;
	uint32_t i = 0;

	CL_ASSERT(ccdg && ccdg->nodes && channel_id);
	*out_ccdg_node = NULL;

	for (i = 0, ccdg_node_iter = ccdg->nodes; i < ccdg->num_nodes;
	     i++, ccdg_node_iter++) {
		if (0 ==
		    compare_two_channel_id(channel_id,
					   &(ccdg_node_iter->channel_id))) {
			*out_ccdg_node = ccdg_node_iter;
			return TRUE;
		}
	}

	return FALSE;
}

static boolean_t is_channel_id_in_verify_ccdg_edge_list(const ccdg_node_t *
							ccdg_node,
							const channel_t *
							channel_id,
							ccdg_edge_t **
							out_ccdg_edge)
{
	ccdg_edge_t *ccdg_edge_iter = NULL;
	uint8_t i = 0;

	CL_ASSERT(ccdg_node && ccdg_node->edges && channel_id);
	*out_ccdg_edge = NULL;
	for (i = 0, ccdg_edge_iter = ccdg_node->edges; i < ccdg_node->num_edges;
	     i++, ccdg_edge_iter++) {
		if (0 ==
		    compare_two_channel_id(channel_id,
					   &(ccdg_edge_iter->to_channel_id))) {
			*out_ccdg_edge = ccdg_edge_iter;
			return TRUE;
		}
	}

	return FALSE;
}

static boolean_t add_paths_to_verify_ccdg(const osm_ucast_mgr_t * mgr,
					  const network_t * network,
					  const ib_net16_t desti,
					  const ccdg_t * ccdg,
					  ccdg_t * verify_ccdg,
					  const boolean_t
					  fallback_to_escape_paths)
{
	network_node_t *network_node = NULL, *netw_node_iter = NULL;
	ccdg_node_t *ccdg_node = NULL;
	ccdg_edge_t *ccdg_edge = NULL;
	uint16_t i = 0;
	channel_t *route[64];	/* can't have more than 64 hops, see IB specs */
	uint8_t num_hops = 0, j = 0;
	ib_net16_t curr_lid = 0;
	channel_t *channel_id1 = NULL, *channel_id2 = NULL;

	CL_ASSERT(mgr && network && network->nodes && ccdg && ccdg->nodes
		  && verify_ccdg && verify_ccdg->nodes);
	OSM_LOG_ENTER(mgr->p_log);

	for (i = 0, netw_node_iter = network->nodes; i < network->num_nodes;
	     i++, netw_node_iter++) {
		if (netw_node_iter->lid == desti)
			continue;

		num_hops = 0;
		curr_lid = netw_node_iter->lid;

		/* desti is the switch's lid if original desti is a terminal */
		do {
			network_node =
			    get_network_node_by_lid(network, curr_lid);
			CL_ASSERT(network_node && network_node->used_link);
			route[num_hops] = &(network_node->used_link->link_info);
			curr_lid = route[num_hops]->local_lid;
			num_hops++;
		} while (curr_lid != desti);
		if (num_hops < 2)
			continue;

		for (j = num_hops - 1; j > 0; j--) {
			channel_id1 = route[j];
			channel_id2 = route[j - 1];

			if (!is_channel_id_in_verify_ccdg_node_list
			    (verify_ccdg, channel_id2, &ccdg_node)) {
				CL_ASSERT(ccdg->num_nodes >
					  verify_ccdg->num_nodes);
				verify_ccdg->nodes[verify_ccdg->num_nodes].
				    channel_id = *channel_id2;
				verify_ccdg->nodes[verify_ccdg->num_nodes].
				    status = WHITE;
				ccdg_node =
				    get_ccdg_node_by_channel_id(ccdg,
								*channel_id2);
				CL_ASSERT(ccdg_node);
				verify_ccdg->nodes[verify_ccdg->num_nodes].
				    edges =
				    (ccdg_edge_t *) calloc(ccdg_node->num_edges,
							   sizeof(ccdg_edge_t));
				if (!verify_ccdg->nodes[verify_ccdg->num_nodes].
				    edges) {
					OSM_LOG(mgr->p_log, OSM_LOG_ERROR,
						"ERR NUE31: cannot allocate memory for ccdg edges\n");
					destroy_ccdg(verify_ccdg);
					return FALSE;
				}
				verify_ccdg->nodes[verify_ccdg->num_nodes].
				    num_edges = 0;
				verify_ccdg->num_nodes++;
			}

			if (is_channel_id_in_verify_ccdg_node_list
			    (verify_ccdg, channel_id1, &ccdg_node)) {
				if (!is_channel_id_in_verify_ccdg_edge_list
				    (ccdg_node, channel_id2, &ccdg_edge)) {
					/* escape_paths must not add anything
					   to the verify_ccdg
					 */
					CL_ASSERT(FALSE == fallback_to_escape_paths);

					ccdg_node->edges[ccdg_node->num_edges].
					    to_channel_id = *channel_id2;
					CL_ASSERT
					    (is_channel_id_in_verify_ccdg_node_list
					     (verify_ccdg, channel_id2,
					      &(ccdg_node->
						edges[ccdg_node->num_edges].
						to_ccdg_node)));
					ccdg_node->num_edges++;
				}
			} else {
				CL_ASSERT(ccdg->num_nodes >
					  verify_ccdg->num_nodes);
				verify_ccdg->nodes[verify_ccdg->num_nodes].
				    channel_id = *channel_id1;
				verify_ccdg->nodes[verify_ccdg->num_nodes].
				    status = WHITE;
				ccdg_node =
				    get_ccdg_node_by_channel_id(ccdg,
								*channel_id1);
				CL_ASSERT(ccdg_node);
				verify_ccdg->nodes[verify_ccdg->num_nodes].
				    edges =
				    (ccdg_edge_t *) calloc(ccdg_node->num_edges,
							   sizeof(ccdg_edge_t));
				if (!verify_ccdg->nodes[verify_ccdg->num_nodes].
				    edges) {
					OSM_LOG(mgr->p_log, OSM_LOG_ERROR,
						"ERR NUE32: cannot allocate memory for ccdg edges\n");
					destroy_ccdg(verify_ccdg);
					return FALSE;
				}
				verify_ccdg->nodes[verify_ccdg->num_nodes].
				    num_edges = 0;
				verify_ccdg->nodes[verify_ccdg->num_nodes].
				    edges[0].to_channel_id = *channel_id2;
				CL_ASSERT(is_channel_id_in_verify_ccdg_node_list
					  (verify_ccdg, channel_id2,
					   &(verify_ccdg->
					     nodes[verify_ccdg->num_nodes].
					     edges[0].to_ccdg_node)));
				verify_ccdg->nodes[verify_ccdg->num_nodes].
				    num_edges++;
				verify_ccdg->num_nodes++;
			}
		}
	}

	OSM_LOG_EXIT(mgr->p_log);
	return TRUE;
}

static boolean_t is_ccdg_cycle_free(const osm_ucast_mgr_t * mgr,
				    const ccdg_t * ccdg)
{
	ccdg_node_t *ccdg_node_iter = NULL, *curr_node = NULL, *next_node =
	    NULL;
	ccdg_edge_t *ccdg_edge_iter = NULL;
	uint32_t i = 0, j = 0;

	CL_ASSERT(mgr && ccdg && ccdg->nodes);
	OSM_LOG_ENTER(mgr->p_log);

	for (i = 0, ccdg_node_iter = ccdg->nodes; i < ccdg->num_nodes;
	     i++, ccdg_node_iter++) {
		ccdg_node_iter->status = WHITE;
		ccdg_node_iter->pre = NULL;
	}

	for (i = 0, ccdg_node_iter = ccdg->nodes; i < ccdg->num_nodes;
	     i++, ccdg_node_iter++) {
		CL_ASSERT(ccdg_node_iter->status != GRAY);
		if (ccdg_node_iter->status == BLACK)
			continue;

		ccdg_node_iter->status = GRAY;

		curr_node = ccdg_node_iter;
		while (curr_node) {
			next_node = NULL;
			for (j = 0, ccdg_edge_iter = curr_node->edges;
			     j < curr_node->num_edges; j++, ccdg_edge_iter++) {
				if (ccdg_edge_iter->to_ccdg_node->status ==
				    WHITE) {
					next_node =
					    ccdg_edge_iter->to_ccdg_node;
					next_node->status = GRAY;
					next_node->pre = curr_node;
					break;
				} else if (ccdg_edge_iter->to_ccdg_node->
					   status == GRAY)
					return FALSE;
			}
			if (!next_node) {
				curr_node->status = BLACK;
				curr_node = curr_node->pre;
			} else
				curr_node = next_node;
		}
	}

	OSM_LOG_EXIT(mgr->p_log);
	return TRUE;
}
#endif

/* callback function for the cl_heap to update the heap index of a ccdg node */
static void update_ccdg_heap_index(const void *context, const size_t new_index)
{
	ccdg_node_t *heap_elem = (ccdg_node_t *) context;
	if (heap_elem)
		heap_elem->heap_index = new_index;
}

/* we reached an impass and we have to use the escape paths as fallback to
   have valid paths towards the current destination
 */
static void use_escape_paths_to_solve_impass(const osm_ucast_mgr_t * mgr,
					     const network_t * network,
					     const osm_port_t * dest_port,
					     const ib_net16_t dlid)
{
	network_node_t *network_node1 = NULL, *network_node2 = NULL;
	network_node_t *netw_node_iter = NULL;
	network_link_t *curr_link = NULL, *reverse_link = NULL;
	network_link_t *netw_link_iter = NULL;
	ib_net16_t r_lid = 0;
	channel_t reverse_channel_id;
	uint16_t i = 0;
	uint8_t j = 0;

	CL_ASSERT(mgr && network && dlid > 0);
	OSM_LOG_ENTER(mgr->p_log);

	/* first let's copy all pre-computed escape paths into the used_links */
	for (i = 0, netw_node_iter = network->nodes; i < network->num_nodes;
	     i++, netw_node_iter++)
		netw_node_iter->used_link = netw_node_iter->escape_path;

	/* get the source node (or adj switch) of the current routing step */
	network_node1 =
	    get_network_node_by_lid(network, get_switch_lid(mgr, dlid));
	CL_ASSERT(network_node1);
	/* if the used_link is NULL, then dlid (or adj switch) is also the
	   root of the spanning tree for the escape paths (and all links are
	   in the correct direction)
	 */
	if (network_node1->used_link) {
		/* otherwise we have to reverse a few of the used_links */
		curr_link = network_node1->used_link;
		network_node1->used_link = NULL;
		/* we only have to reverse until the real spanning tree root */
		while (curr_link) {
			r_lid = (ib_net16_t) curr_link->link_info.local_lid;
			network_node2 = get_network_node_by_lid(network, r_lid);
			CL_ASSERT(network_node2);
			reverse_channel_id =
			    get_inverted_channel_id(curr_link->link_info);
			/* search the reverse link */
			reverse_link = NULL;
			for (j = 0, netw_link_iter = network_node1->links;
			     j < network_node1->num_links;
			     j++, netw_link_iter++) {
				if (0 ==
				    compare_two_channel_id(&reverse_channel_id,
							   &(netw_link_iter->
							     link_info)))
					reverse_link = netw_link_iter;
			}
			CL_ASSERT(reverse_link);
			curr_link = network_node2->used_link;
			network_node2->used_link = reverse_link;
			network_node1 = network_node2;
		}
	}

	OSM_LOG_EXIT(mgr->p_log);
}

// check if we find a way from the source to the target -> yes: cycle
static boolean_t found_path_between_ccdg_nodes_in_subgraph(const osm_ucast_mgr_t
							   * mgr,
							   const ccdg_t * ccdg,
							   ccdg_node_t * source,
							   const ccdg_node_t *
							   target,
							   const int32_t color)
{
	ccdg_node_t *curr_ccdg_node = NULL, *next_ccdg_node = NULL;
	ccdg_node_t *ccdg_node_iter = NULL;
	ccdg_edge_t *ccdg_edge_iter = NULL;
	uint32_t i = 0;
	uint8_t j = 0;
	boolean_t found_path = FALSE;

	CL_ASSERT(mgr && ccdg && source && target && source != target);
	OSM_LOG_ENTER(mgr->p_log);

	curr_ccdg_node = source;
	curr_ccdg_node->next_edge_idx = 0;
	curr_ccdg_node->pre = NULL;

	do {
		next_ccdg_node = NULL;
		for (j = curr_ccdg_node->next_edge_idx, ccdg_edge_iter =
		     curr_ccdg_node->edges + curr_ccdg_node->next_edge_idx;
		     j < curr_ccdg_node->num_edges; j++, ccdg_edge_iter++) {
			CL_ASSERT(ccdg_edge_iter->to_ccdg_node);
			if (BLACK == ccdg_edge_iter->to_ccdg_node->status
			    || get_ccdg_edge_color(ccdg,
						   ccdg_edge_iter) <= UNUSED)
				continue;
			CL_ASSERT(color ==
				  get_ccdg_edge_color(ccdg, ccdg_edge_iter));

			if (ccdg_edge_iter->to_ccdg_node == target) {
				found_path = TRUE;
			} else {
				next_ccdg_node = ccdg_edge_iter->to_ccdg_node;
				curr_ccdg_node->next_edge_idx = j + 1;
			}
			break;
		}

		if (next_ccdg_node) {
			next_ccdg_node->next_edge_idx = 0;
			next_ccdg_node->pre = curr_ccdg_node;
		} else {
			curr_ccdg_node->status = BLACK;
			next_ccdg_node = curr_ccdg_node->pre;
		}
		curr_ccdg_node = next_ccdg_node;
	} while (!found_path && curr_ccdg_node);

	/* reset changed status fields */
	for (i = 0, ccdg_node_iter = ccdg->nodes; i < ccdg->num_nodes;
	     i++, ccdg_node_iter++)
		if (ccdg_node_iter->status == BLACK)
			ccdg_node_iter->status = WHITE;

	OSM_LOG_EXIT(mgr->p_log);
	return found_path;
}

static boolean_t using_edge_induces_cycle_in_ccdg(const osm_ucast_mgr_t * mgr,
						  const ccdg_t * ccdg,
						  const ccdg_node_t * head,
						  ccdg_edge_t * ccdg_edge,
						  const int32_t color)
{
	ccdg_node_t *tail = NULL;
	boolean_t cycle_induced = TRUE;

	CL_ASSERT(mgr && ccdg && head && ccdg_edge && color > 0);
	OSM_LOG_ENTER(mgr->p_log);

	tail = ccdg_edge->to_ccdg_node;
	CL_ASSERT(tail && get_ccdg_edge_color(ccdg, ccdg_edge) != BLOCKED);

	if (get_ccdg_edge_color(ccdg, ccdg_edge) > UNUSED) {
		cycle_induced = FALSE;
		CL_ASSERT(get_ccdg_node_color(ccdg, head) ==
			  get_ccdg_node_color(ccdg, tail)
			  && get_ccdg_node_color(ccdg,
						 head) ==
			  get_ccdg_edge_color(ccdg, ccdg_edge));
	} else {
		if (color == get_ccdg_node_color(ccdg, tail)) {
			/* trying to add an edge to a acyclic subgraph */
			if (found_path_between_ccdg_nodes_in_subgraph
			    (mgr, ccdg, tail, head, color)) {
				set_ccdg_edge_into_blocked_state(ccdg,
								 ccdg_edge);
				cycle_induced = TRUE;
			} else {
				add_ccdg_edge_betw_nodes_to_colored_subccdg
				    (ccdg, head, tail, ccdg_edge);
				cycle_induced = FALSE;
			}
		} else {
			/* connecting two disjoint acyclic subgraphs */
			if (UNUSED == get_ccdg_node_color(ccdg, tail)) {
				add_ccdg_node_to_colored_subccdg(ccdg, head,
								 tail);
			} else {
				merge_two_colored_subccdg_by_nodes(ccdg, head,
								   tail);
			}
			cycle_induced = FALSE;
		}
	}

	OSM_LOG_EXIT(mgr->p_log);
	return cycle_induced;
}

/* only add the link if it's not yet in the stack */
static inline void add_link_to_stack_of_used_links(network_node_t *
						   network_node,
						   network_link_t * link)
{
	uint8_t i = 0;

	CL_ASSERT(network_node && link);
	for (i = 0; i < network_node->num_elem_in_link_stack; i++) {
		if (link == network_node->stack_used_links[i])
			break;
	}
	/* add link if not found in stack */
	if (i == network_node->num_elem_in_link_stack)
		network_node->stack_used_links[network_node->
					       num_elem_in_link_stack++] = link;
}

/* check alternative paths within a small radius to find/use valid channel
   dependencies which won't close a cycle in the CCDG
 */
static ccdg_node_t *attempt_local_backtracking(const osm_ucast_mgr_t * mgr,
					       const network_t * network,
					       const network_node_t *
					       source_netw_node,
					       const ccdg_t * ccdg,
					       const int32_t color)
{
	network_node_t *unreachable_netw_node = NULL, *network_node = NULL;
	network_node_t *adj_netw_node = NULL, *netw_node_iter = NULL;
	network_link_t *netw_link_iter1 = NULL, *netw_link_iter2 = NULL;
	uint16_t i = 0;
	uint8_t j = 0, k = 0, m = 0;
	ccdg_node_t *depended_channels[UINT8_MAX];	/* max = switch radix */
	backtracking_candidate_t *potential_candidates =
	    NULL, *realloc_candidates = NULL;
	uint32_t max_elem_in_candidates_array = (uint32_t) UINT8_MAX;
	uint32_t num_potential_candidates = 0, n = 0;
	uint8_t num_depended_channels = 0;
	ccdg_node_t *ccdg_node = NULL, *depended_ccdg_node = NULL;
	ccdg_node_t *pre_ccdg_node = NULL, *pre_pre_ccdg_node = NULL;
	ccdg_edge_t *pre_ccdg_edge = NULL, *ccdg_edge = NULL;
	boolean_t was_wet_before = FALSE;
	ccdg_node_t *new_channel_to_unreachable_netw_node = NULL;

	CL_ASSERT(mgr && network && ccdg);
	OSM_LOG_ENTER(mgr->p_log);

	potential_candidates =
	    (backtracking_candidate_t *) malloc(max_elem_in_candidates_array *
						sizeof
						(backtracking_candidate_t));
	if (!potential_candidates) {
		OSM_LOG(mgr->p_log, OSM_LOG_INFO,
			"WRN NUE40: cannot allocate memory for potential channel candidates; skipping local backtracking\n");
		return NULL;
	}

	/* search for nodes which don't have been found by the function
	   route_via_modified_dijkstra_on_ccdg yet
	 */
	for (i = 0, netw_node_iter = network->nodes; i < network->num_nodes;
	     i++, netw_node_iter++) {
		if (INFINITY != netw_node_iter->distance)
			continue;

		unreachable_netw_node = netw_node_iter;

		num_potential_candidates = 0;

		for (j = 0, netw_link_iter1 = unreachable_netw_node->links;
		     j < unreachable_netw_node->num_links;
		     j++, netw_link_iter1++) {
			adj_netw_node = netw_link_iter1->to_network_node;
			if (INFINITY == adj_netw_node->distance)
				continue;

			num_depended_channels = 0;

			/* search for depended nodes surrounding the
			   adj_netw_node, i.e., whether any of them receive
			   traffic from adj_netw_node or not
			 */
			for (k = 0, netw_link_iter2 = adj_netw_node->links;
			     k < adj_netw_node->num_links;
			     k++, netw_link_iter2++) {
				if (!netw_link_iter2->to_network_node->
				    used_link)
					continue;
				/* if true, then we found a 'dependent' node */
				if (netw_link_iter2->to_network_node->
				    used_link->link_info.local_lid ==
				    adj_netw_node->lid) {
					depended_channels[num_depended_channels]
					    =
					    netw_link_iter2->to_network_node->
					    used_link->corresponding_ccdg_node;
					CL_ASSERT(depended_channels
						  [num_depended_channels]);
					num_depended_channels++;
				}
			}

			/* check if any of the channels in stack of used_links
			   does not violate the current dependent condition,
			   meaning we can savely replace used_link of
			   adj_netw_node with one link stored in the stack
			 */
			for (k = 0; k < adj_netw_node->num_elem_in_link_stack;
			     k++) {
				CL_ASSERT(adj_netw_node->stack_used_links
					  && adj_netw_node->
					  stack_used_links[k]);
				ccdg_node =
				    adj_netw_node->stack_used_links[k]->
				    corresponding_ccdg_node;
				CL_ASSERT(ccdg_node);
				for (m = 0; m < num_depended_channels; m++) {
					depended_ccdg_node =
					    depended_channels[m];

					/* filter reverse channels */
					if (ccdg_node->channel_id.local_lid ==
					    depended_ccdg_node->channel_id.
					    remote_lid
					    && ccdg_node->channel_id.
					    remote_lid ==
					    depended_ccdg_node->channel_id.
					    local_lid)
						break;

					/* we only want channels which have a
					   'real' color (>= escape_path_color)
					 */
					if (get_ccdg_edge_color_betw_nodes
					    (ccdg, ccdg_node,
					     depended_ccdg_node,
					     NULL) < ESCAPEPATHCOLOR)
						break;
				}
				/* if we checked all and nothing discards us
				   from switching to ccdg_node for adj_netw_node
				   then store ccdg_node as potential candidate
				 */
				if (m == num_depended_channels) {
					if (num_potential_candidates ==
					    max_elem_in_candidates_array) {
						max_elem_in_candidates_array *=
						    2;
						CL_ASSERT
						    (max_elem_in_candidates_array);
						realloc_candidates =
						    realloc
						    (potential_candidates,
						     max_elem_in_candidates_array
						     *
						     sizeof
						     (backtracking_candidate_t));
						if (!realloc_candidates) {
							OSM_LOG(mgr->p_log,
								OSM_LOG_INFO,
								"WRN NUE41: cannot allocate memory for potential channel candidates; skipping local backtracking\n");
							free(potential_candidates);
							return NULL;
						} else {
							potential_candidates =
							    realloc_candidates;
						}
					}
					potential_candidates
					    [num_potential_candidates].
					    link_to_adj_netw_node =
					    netw_link_iter1;
					potential_candidates
					    [num_potential_candidates].
					    orig_used_ccdg_node_for_adj_netw_node
					    = ccdg_node;
					num_potential_candidates++;
				}
			}
		}

		/* jump to next network node if this one has no candidates */
		if (!num_potential_candidates)
			continue;

		/* sort the candidates by dijkstra's distance to prefer
		   the 'best' possible option for a replacement
		 */
		sort_backtracking_candidates_by_distance(potential_candidates,
							 (size_t)
							 num_potential_candidates);

		for (n = 0; n < num_potential_candidates; n++) {
			pre_ccdg_node =
			    potential_candidates[n].
			    orig_used_ccdg_node_for_adj_netw_node;
			CL_ASSERT(pre_ccdg_node);
			ccdg_node =
			    get_ccdg_node_by_channel_id(ccdg,
							get_inverted_channel_id
							(potential_candidates
							 [n].
							 link_to_adj_netw_node->
							 link_info));
			CL_ASSERT(ccdg_node);

			/* check if using the node tripple (x)->adj->unreachable
			   is possible or if the channel dep. is BLOCKED already
			 */
			if (BLOCKED ==
			    get_ccdg_edge_color_betw_nodes(ccdg, pre_ccdg_node,
							   ccdg_node, NULL))
				continue;

			network_node =
			    get_network_node_by_lid(network,
						    pre_ccdg_node->channel_id.
						    local_lid);
			CL_ASSERT(network_node);
			if (network_node == source_netw_node)
				continue;
			CL_ASSERT(network_node->used_link);
			pre_pre_ccdg_node =
			    network_node->used_link->corresponding_ccdg_node;

			/* still a slim chance for reverse channel -> filter */
			if (pre_ccdg_node->channel_id.local_lid ==
			    pre_pre_ccdg_node->channel_id.remote_lid
			    && pre_ccdg_node->channel_id.remote_lid ==
			    pre_pre_ccdg_node->channel_id.local_lid)
				continue;

			pre_ccdg_edge =
			    get_ccdg_edge_betw_nodes(pre_pre_ccdg_node,
						     pre_ccdg_node);
			CL_ASSERT(pre_ccdg_edge && pre_ccdg_edge->color);

			/* filter BLOCKED dependencies */
			if (BLOCKED ==
			    get_ccdg_edge_color_betw_nodes(ccdg,
							   pre_pre_ccdg_node,
							   pre_ccdg_node,
							   pre_ccdg_edge))
				continue;

			/* check if we can use this pre_ccdg_edge, or start
			   over but leave the color as is
			 */
			was_wet_before = pre_ccdg_edge->wet_paint;
			if (using_edge_induces_cycle_in_ccdg
			    (mgr, ccdg, pre_pre_ccdg_node, pre_ccdg_edge,
			     color)) {
				continue;
			}

			ccdg_edge =
			    get_ccdg_edge_betw_nodes(pre_ccdg_node, ccdg_node);
			CL_ASSERT(ccdg_edge);
			/* now check the next edge, but reset the previous if
			   the check for this one fails
			 */
			if (using_edge_induces_cycle_in_ccdg
			    (mgr, ccdg, pre_ccdg_node, ccdg_edge, color)) {
				/* only reset if it was UNSED before or colored
				   in a previous routing step (but not in the
				   current step for the current destination
				 */
				if (!was_wet_before) {
					reset_ccdg_edge_color(ccdg,
							      pre_ccdg_edge);
				}
				continue;
			}

			/* if we came this far, then we have a viable candidate
			   and can update the dijkstra's distance and used_link
			   information for the previously unreachable node;
			   if we find multiple options then we add them to the
			   stack as usual
			 */
			if (!new_channel_to_unreachable_netw_node) {
				ccdg_node->distance =
				    pre_ccdg_node->distance +
				    ccdg_node->corresponding_netw_link->weight;
				unreachable_netw_node->used_link =
				    ccdg_node->corresponding_netw_link;
				unreachable_netw_node->distance =
				    ccdg_node->distance;

				potential_candidates[n].link_to_adj_netw_node->
				    to_network_node->used_link =
				    pre_ccdg_node->corresponding_netw_link;
				potential_candidates[n].link_to_adj_netw_node->
				    to_network_node->distance =
				    pre_ccdg_node->distance;

				new_channel_to_unreachable_netw_node =
				    ccdg_node;
			} else {
				ccdg_node->distance =
				    pre_ccdg_node->distance +
				    ccdg_node->corresponding_netw_link->weight;
				add_link_to_stack_of_used_links
				    (unreachable_netw_node,
				     ccdg_node->corresponding_netw_link);
			}
		}

		/* leave early when we found a suitable way into an unreachable
		   network node, route_via_modified_dijkstra_on_ccdg handles
		   the rest and might call the backtracking again
		 */
		if (new_channel_to_unreachable_netw_node)
			break;
	}

	/* we don't need the potential candidates anymore */
	free(potential_candidates);

	if (new_channel_to_unreachable_netw_node)
		OSM_LOG(mgr->p_log, OSM_LOG_DEBUG,
			"backtracking worked; found path to LID %" PRIu16
			" (%s)\n", cl_ntoh16(unreachable_netw_node->lid),
			unreachable_netw_node->sw->p_node->print_desc);

	OSM_LOG_EXIT(mgr->p_log);
	return new_channel_to_unreachable_netw_node;
}

static boolean_t attempt_shortcut_discovery(const osm_ucast_mgr_t * mgr,
					    const network_t * network,
					    const network_node_t *
					    potential_shortcut_netw_node,
					    const ccdg_t * ccdg,
					    const ccdg_node_t *
					    potential_shortcut_ccdg_node,
					    const int32_t color)
{
	network_node_t *network_node = NULL;
	ccdg_node_t *pre_old_ccdg_node = NULL, *old_ccdg_node = NULL;
	ccdg_edge_t *ccdg_edge = NULL;
	ccdg_edge_t *dependent_edges[UINT8_MAX];
	boolean_t was_wet_before[UINT8_MAX];
	uint8_t num_dependent_edges = 0;
	network_link_t *netw_link_iter = NULL;
	uint8_t i = 0, reset_until_break_point = 0;
	boolean_t valid_shortcut = TRUE;

	CL_ASSERT(mgr && network && potential_shortcut_netw_node && ccdg
		  && potential_shortcut_ccdg_node
		  && potential_shortcut_netw_node->used_link
		  && color > ESCAPEPATHCOLOR);
	OSM_LOG_ENTER(mgr->p_log);

	old_ccdg_node =
	    potential_shortcut_netw_node->used_link->corresponding_ccdg_node;
	CL_ASSERT(old_ccdg_node
		  && old_ccdg_node != potential_shortcut_ccdg_node);

	network_node =
	    get_network_node_by_lid(network,
				    old_ccdg_node->channel_id.local_lid);
	CL_ASSERT(network_node && network_node->used_link);
	pre_old_ccdg_node = network_node->used_link->corresponding_ccdg_node;
	CL_ASSERT(pre_old_ccdg_node);

	/* find dependent netw nodes, meaning nodes which the potential_shortcut
	   will relay traffic to
	 */
	for (i = 0, netw_link_iter = potential_shortcut_netw_node->links;
	     i < potential_shortcut_netw_node->num_links;
	     i++, netw_link_iter++) {
		if (!netw_link_iter->to_network_node->used_link)
			continue;
		if (potential_shortcut_netw_node->lid ==
		    netw_link_iter->to_network_node->used_link->link_info.
		    local_lid) {
			dependent_edges[num_dependent_edges] =
			    get_ccdg_edge_betw_nodes
			    (potential_shortcut_ccdg_node,
			     netw_link_iter->to_network_node->used_link->
			     corresponding_ccdg_node);
			CL_ASSERT(dependent_edges[num_dependent_edges]);
			num_dependent_edges++;
		}
	}

	/* save the colors for later, in case we have to reset them */
	for (i = 0; i < num_dependent_edges; i++) {
		was_wet_before[i] = dependent_edges[i]->wet_paint;
	}

	/* verify that using potential_shortcut_ccdg_node doesn't induce
	   any cycles in the complete CDG in combination with existing paths;
	   otherwise we break out and reset to previous state
	 */
	for (i = 0; i < num_dependent_edges; i++) {
		ccdg_edge = dependent_edges[i];
		if (BLOCKED == get_ccdg_edge_color(ccdg, ccdg_edge)) {
			valid_shortcut = FALSE;
			reset_until_break_point = i;
			break;
		} else
		    if (using_edge_induces_cycle_in_ccdg
			(mgr, ccdg, potential_shortcut_ccdg_node, ccdg_edge,
			 color)) {
			valid_shortcut = FALSE;
			reset_until_break_point = i;
			break;
		}
	}

	if (valid_shortcut) {
		/* if the shortcut is valid and no new deadlock scenarios arise,
		   then we can reset the channel dependencies which were in
		   place before the shortcut was discovered for the path which
		   led to this node (essentially reverting a minor part of the
		   induced dependencies in the cCDG around the node whose
		   distance was shortened by the shortcut; see Section 6.2.6.3
		   of reference [2] for more details)
		 */
		reset_ccdg_edge_color_betw_nodes(ccdg, pre_old_ccdg_node,
						 old_ccdg_node, NULL);
		for (i = 0; i < num_dependent_edges; i++) {
			reset_ccdg_edge_color_betw_nodes(ccdg, old_ccdg_node,
							 dependent_edges[i]->
							 to_ccdg_node,
							 dependent_edges[i]);
		}
	} else {
		/* due to some BLOCKED edge we can't use the potential shortcut,
		   and hence we have to reset the color to whatever it was
		   previous; however we don't reset the BLOCKED edge itself,
		   since it's very likely that it will "re-block" later again
		 */
		for (i = 0; i < reset_until_break_point; i++) {
			if (BLOCKED !=
			    get_ccdg_edge_color(ccdg, dependent_edges[i])) {
				if (!was_wet_before[i]) {
					reset_ccdg_edge_color(ccdg,
							      dependent_edges
							      [i]);
				}
			}
		}
	}

	OSM_LOG_EXIT(mgr->p_log);
	return valid_shortcut;
}

static int route_via_modified_dijkstra_on_ccdg(const osm_ucast_mgr_t * mgr,
					       const network_t * network,
					       ccdg_t * ccdg,
					       const osm_port_t * dest_port,
					       const ib_net16_t dlid,
					       const int32_t source_color,
					       boolean_t *
					       fallback_to_escape_paths)
{
	network_node_t *network_node = NULL, *source_netw_node = NULL;
	network_node_t *new_discovered_netw_node = NULL, *netw_node_iter = NULL;
	ccdg_node_t *source_ccdg_node = NULL;
	ccdg_node_t *curr_ccdg_node = NULL, *next_ccdg_node = NULL;
	ccdg_node_t *old_ccdg_node = NULL, *pre_old_ccdg_node = NULL;
	ccdg_edge_t *ccdg_edge_iter = NULL;
	ib_net16_t dijk_source_lid = dlid;
	channel_t source_channel_id;
	uint64_t new_distance = 0;
	cl_status_t ret = CL_SUCCESS;
	uint32_t i = 0;
	uint8_t j = 0;
	uint16_t num_netw_nodes_found = 0;
	int32_t last_active_backtracking_step = 0;
	boolean_t iterate_over_used = FALSE;

	CL_ASSERT(mgr && network && ccdg && dlid > 0);
	OSM_LOG_ENTER(mgr->p_log);

	*fallback_to_escape_paths = FALSE;

	for (i = 0, netw_node_iter = network->nodes; i < network->num_nodes;
	     i++, netw_node_iter++) {
		netw_node_iter->distance = INFINITY;
		netw_node_iter->used_link = NULL;
		netw_node_iter->num_elem_in_link_stack = 0;
		/* resetting stack of used_links not really necessary, but good
		   to sanitize memory for eventual debugging
		 */
		memset(netw_node_iter->stack_used_links, 0,
		       netw_node_iter->num_links * sizeof(network_link_t *));
		netw_node_iter->hops = 0;
		netw_node_iter->found_after_backtracking_step = -1;
	}

	/* build a 4-ary heap to find the ccdg node with minimum distance */
	if (!cl_is_heap_inited(&ccdg->heap))
		ret =
		    cl_heap_init(&ccdg->heap, (size_t) ccdg->num_nodes, 4,
				 &update_ccdg_heap_index, NULL);
	else
		ret = cl_heap_resize(&ccdg->heap, (size_t) ccdg->num_nodes);
	if (CL_SUCCESS != ret) {
		OSM_LOG(mgr->p_log, OSM_LOG_ERROR,
			"ERR NUE33: cannot allocate memory or resize heap\n");
		return -1;
	}

	/* get the first switch, i.e,, 'source' of the dijkstra step w.r.t the
	   network, and initialize some values
	 */
	source_netw_node =
	    get_network_node_by_lid(network, get_switch_lid(mgr, dlid));
	CL_ASSERT(source_netw_node);
	source_netw_node->distance = 0;
	source_netw_node->hops =
	    (osm_node_get_type(dest_port->p_node) ==
	     IB_NODE_TYPE_SWITCH) ? 0 : 1;
	num_netw_nodes_found = 1;

	/* do the same for the real 'source', i.e., the ccdg node */
	source_channel_id.local_lid = source_netw_node->lid;
	source_channel_id.local_port = 0;
	source_channel_id.remote_lid = source_netw_node->lid;
	source_channel_id.remote_port = 0;
	source_ccdg_node = get_ccdg_node_by_channel_id(ccdg, source_channel_id);
	CL_ASSERT(source_ccdg_node);
	change_fake_ccdg_node_color(ccdg, source_ccdg_node, source_color);
	source_ccdg_node->distance = 0;

	curr_ccdg_node = source_ccdg_node;
	do {
		/* first iterate over the unused and then the used edges */
		iterate_over_used = TRUE;
		for (j = 0, ccdg_edge_iter = curr_ccdg_node->edges;
		     j < 2 * curr_ccdg_node->num_edges; j++, ccdg_edge_iter++) {
			/* reset edge iterator and now check unused */
			if (j == curr_ccdg_node->num_edges) {
				ccdg_edge_iter = curr_ccdg_node->edges;
				iterate_over_used = FALSE;
			}

			if (iterate_over_used) {
				if (get_ccdg_edge_color(ccdg, ccdg_edge_iter) <
				    ESCAPEPATHCOLOR)
					continue;
			} else {
				if (UNUSED !=
				    get_ccdg_edge_color(ccdg, ccdg_edge_iter))
					continue;
			}

			next_ccdg_node = ccdg_edge_iter->to_ccdg_node;
			new_distance =
			    curr_ccdg_node->distance +
			    next_ccdg_node->corresponding_netw_link->weight;
			new_discovered_netw_node =
			    next_ccdg_node->corresponding_netw_link->
			    to_network_node;

			if (new_distance < new_discovered_netw_node->distance) {
				/* verify, that the ccdg_edge will not close a
				   cycle, or block the edge indefinitely
				 */
				if (using_edge_induces_cycle_in_ccdg
				    (mgr, ccdg, curr_ccdg_node, ccdg_edge_iter,
				     source_color))
					continue;

				if (last_active_backtracking_step
				    && new_discovered_netw_node->used_link
				    && last_active_backtracking_step !=
				    new_discovered_netw_node->
				    found_after_backtracking_step) {
					/* check for shortcuts only for nodes
					   we have discovered before the first
					   backtracking step
					 */
					if (attempt_shortcut_discovery
					    (mgr, network,
					     new_discovered_netw_node, ccdg,
					     next_ccdg_node, source_color)) {
						OSM_LOG(mgr->p_log,
							OSM_LOG_INFO,
							"found new shortcut towards LID %"
							PRIu16
							" (%s) after successful backtracking\n",
							new_discovered_netw_node->
							lid,
							new_discovered_netw_node->
							sw->p_node->print_desc);
					} else {
						continue;
					}
				} else {

					/* check if this node was discovered on
					   a different path, then clean up the
					   heap (-> remove outdated ccdg_node)
					 */
					if (new_discovered_netw_node->used_link) {
						old_ccdg_node =
						    new_discovered_netw_node->
						    used_link->
						    corresponding_ccdg_node;
						CL_ASSERT(cl_is_stored_in_heap
							  (&ccdg->heap,
							   old_ccdg_node,
							   old_ccdg_node->
							   heap_index));
						old_ccdg_node =
						    cl_heap_delete(&ccdg->heap,
								   old_ccdg_node->
								   heap_index);
						network_node =
						    get_network_node_by_lid
						    (network,
						     old_ccdg_node->channel_id.
						     local_lid);
						CL_ASSERT(network_node);
						if (network_node->used_link) {
							pre_old_ccdg_node =
							    network_node->
							    used_link->
							    corresponding_ccdg_node;
							CL_ASSERT
							    (pre_old_ccdg_node);
							reset_ccdg_edge_color_betw_nodes
							    (ccdg,
							     pre_old_ccdg_node,
							     old_ccdg_node,
							     NULL);
						} else {
							CL_ASSERT(network_node->
								  lid ==
								  source_netw_node->
								  lid);
						}
					} else {
						num_netw_nodes_found++;
						new_discovered_netw_node->
						    found_after_backtracking_step
						    =
						    last_active_backtracking_step;
					}

					/* update the heap with the new
					   ccdg_node (only for non-shortcuts)
					 */
					next_ccdg_node->distance = new_distance;
					ret =
					    cl_heap_insert(&ccdg->heap,
							   next_ccdg_node->
							   distance,
							   next_ccdg_node);
					if (CL_SUCCESS != ret) {
						OSM_LOG(mgr->p_log,
							OSM_LOG_ERROR,
							"ERR NUE34: cl_heap_insert failed\n");
						return -1;
					}
				}

				/* write new distance, used_link, hops, etc. */
				new_discovered_netw_node->distance =
				    new_distance;
				new_discovered_netw_node->used_link =
				    next_ccdg_node->corresponding_netw_link;
				if (source_ccdg_node != curr_ccdg_node)
					new_discovered_netw_node->hops =
					    curr_ccdg_node->
					    corresponding_netw_link->
					    to_network_node->hops + 1;
				else
					new_discovered_netw_node->hops =
					    source_netw_node->hops + 1;

			} else if (get_ccdg_edge_color(ccdg, ccdg_edge_iter) >
				   UNUSED) {
				if (last_active_backtracking_step
				    && new_discovered_netw_node->used_link)
					continue;
				next_ccdg_node->distance = new_distance;
				add_link_to_stack_of_used_links
				    (new_discovered_netw_node,
				     next_ccdg_node->corresponding_netw_link);
			}
		}
		CL_ASSERT((source_ccdg_node ==
			   curr_ccdg_node) ? TRUE : (uint64_t) curr_ccdg_node->
			  corresponding_netw_link->to_network_node->used_link);

		curr_ccdg_node =
		    (ccdg_node_t *) cl_heap_extract_root(&ccdg->heap);

		if (!curr_ccdg_node) {
			/* verify that all netw_nodes have been discovered,
			   and if NOT, we have to performe a local backtracking
			   or even worse: fall back to the escape paths
			 */
			if (num_netw_nodes_found != network->num_nodes) {
				curr_ccdg_node =
				    attempt_local_backtracking(mgr, network,
							       source_netw_node,
							       ccdg,
							       source_color);
				if (curr_ccdg_node) {
					num_netw_nodes_found++;
					last_active_backtracking_step += 1;
				} else {
					OSM_LOG(mgr->p_log, OSM_LOG_INFO,
						"unsolvable impass reached; fallback to escape paths for destination LID %"
						PRIu16 " (%s)\n",
						cl_ntoh16(dlid),
						dest_port->p_node->print_desc);
					use_escape_paths_to_solve_impass(mgr,
									 network,
									 dest_port,
									 dijk_source_lid);
					*fallback_to_escape_paths = TRUE;
					last_active_backtracking_step = 0;
				}
			}
		}
	} while (curr_ccdg_node);

	if (last_active_backtracking_step) {
		OSM_LOG(mgr->p_log, OSM_LOG_INFO,
			"backtracking worked for destination LID %" PRIu16
			" (%s)\n", cl_ntoh16(dlid),
			dest_port->p_node->print_desc);
	}

	/* fix the colors in the ccdg, i.e., overwrite real colors with the
	   backup colors, for the next iteration
	 */
	fix_ccdg_colors(mgr, network, source_netw_node, ccdg, source_ccdg_node);

	OSM_LOG_EXIT(mgr->p_log);
	return 0;
}

/* update the edge weights along the path towards to the destination of the
   current routing step; the parameter desti is assumed to be a switch lid even
   if the real desti is a terminal (then desti is the adjacent switch's lid)
 */
static void update_network_link_weights(const osm_ucast_mgr_t * mgr,
					const network_t * network,
					const ib_net16_t desti)
{
	network_node_t *network_node = NULL, *netw_node_iter = NULL;
	network_link_t *network_link = NULL;
	ib_net16_t curr_lid = 0;
	uint8_t additional_weight = 0;
	uint16_t i = 0;

	CL_ASSERT(mgr && network && desti > 0);
	OSM_LOG_ENTER(mgr->p_log);

	/* desti is the switch's lid if original desti is a terminal */
	for (i = 0, netw_node_iter = network->nodes; i < network->num_nodes;
	     i++, netw_node_iter++) {
		if (netw_node_iter->lid == desti)
			continue;

		/* num_terminals includes already the switch itself */
		additional_weight = netw_node_iter->num_terminals;
		curr_lid = netw_node_iter->lid;

		do {
			network_node =
			    get_network_node_by_lid(network, curr_lid);
			CL_ASSERT(network_node);
			network_link = network_node->used_link;
			network_link->weight += additional_weight;
			curr_lid = network_link->link_info.local_lid;
		} while (curr_lid != desti);
	}

	OSM_LOG_EXIT(mgr->p_log);
}

static void init_linear_forwarding_tables(const osm_ucast_mgr_t * mgr,
					  const network_t * network)
{
	network_node_t *netw_node_iter = NULL;
	osm_switch_t *sw = NULL;
	uint16_t i = 0, lid = 0, min_lid_ho = 0, max_lid_ho = 0;

	CL_ASSERT(mgr && network);
	OSM_LOG_ENTER(mgr->p_log);

	for (i = 0, netw_node_iter = network->nodes; i < network->num_nodes;
	     i++, netw_node_iter++) {
		sw = (osm_switch_t *) netw_node_iter->sw;
		/* initialize LIDs in buffer to invalid port number */
		memset(sw->new_lft, OSM_NO_PATH, sw->max_lid_ho + 1);
		/* initialize LFT and hop count for bsp0/esp0 of the switch */
		min_lid_ho = cl_ntoh16(osm_node_get_base_lid(sw->p_node, 0));
		max_lid_ho =
		    min_lid_ho + (1 << osm_node_get_lmc(sw->p_node, 0)) - 1;
		for (lid = min_lid_ho; lid <= max_lid_ho; lid++) {
			/* for each switch the port to the 'self'lid is the
			   management port 0
			 */
			sw->new_lft[lid] = 0;
			/* and the hop count to the 'self'lid is 0 */
			osm_switch_set_hops(sw, lid, 0, 0);
		}
	}

	OSM_LOG_EXIT(mgr->p_log);
}

/* update the linear forwarding tables of all switches with the informations
   from the last routing step performed with our modified dijkstra on the ccdg
*/
static void update_linear_forwarding_tables(const osm_ucast_mgr_t * mgr,
					    const network_t * network,
					    const osm_port_t * dest_port,
					    const ib_net16_t dlid)
{
	network_node_t *netw_node_iter = NULL;
	osm_switch_t *sw = NULL;
	uint8_t hops = 0, exit_port = 0;
	osm_physp_t *phys_port = NULL;
	boolean_t is_ignored_by_port_prof = FALSE;
	uint16_t i = 0;
	cl_status_t ret = CL_SUCCESS;

	CL_ASSERT(mgr && network && dest_port && dlid > 0);
	OSM_LOG_ENTER(mgr->p_log);

	for (i = 0, netw_node_iter = network->nodes; i < network->num_nodes;
	     i++, netw_node_iter++) {
		/* if no route goes thru this switch, then it must the
		   adjacent switch for a terminal
		 */
		if (!netw_node_iter->used_link) {
			CL_ASSERT(netw_node_iter->lid ==
				  get_switch_lid(mgr, dlid));
			/* the 'route' to port 0 was configured already in
			   our init_linear_forwarding_tables function
			 */
			if (osm_node_get_type(dest_port->p_node) ==
			    IB_NODE_TYPE_SWITCH)
				continue;

			(void)osm_node_get_remote_node(dest_port->p_node,
						       dest_port->p_physp->
						       port_num, &exit_port);
		} else {
			exit_port =
			    netw_node_iter->used_link->link_info.remote_port;
		}

		sw = netw_node_iter->sw;
		hops = netw_node_iter->hops;
		/* the used_link is the link that was used in dijkstra to reach
		   this node, so the remote_port is the local port on this node
		 */

		OSM_LOG(mgr->p_log, OSM_LOG_DEBUG,
			"Routing LID %" PRIu16 " thru port %" PRIu8
			" for switch 0x%016" PRIx64 "\n", cl_ntoh16(dlid),
			exit_port,
			cl_ntoh64(osm_node_get_node_guid(sw->p_node)));

		phys_port = osm_node_get_physp_ptr(sw->p_node, exit_port);

		/* we would like to optionally ignore this port in equalization
		   as in the case of the Mellanox Anafa Internal PCI TCA port
		 */
		is_ignored_by_port_prof = phys_port->is_prof_ignored;

		/* We also would ignore this route if the target lid is of
		   a switch and the port_profile_switch_node is not TRUE
		 */
		if (!mgr->p_subn->opt.port_profile_switch_nodes) {
			is_ignored_by_port_prof |=
			    (osm_node_get_type(dest_port->p_node) ==
			     IB_NODE_TYPE_SWITCH);
		}

		/* set port in LFT, but switches use host byte order */
		sw->new_lft[cl_ntoh16(dlid)] = exit_port;

		/* update the number of path routing thru this port */
		if (!is_ignored_by_port_prof)
			osm_switch_count_path(sw, exit_port);

		/* set the hop count from this switch to the dlid */
		ret = osm_switch_set_hops(sw, cl_ntoh16(dlid), exit_port, hops);
		if (CL_SUCCESS != ret) {
			OSM_LOG(mgr->p_log, OSM_LOG_ERROR,
				"ERR NUE04: cannot set hops for LID %" PRIu16
				" for switch 0x%016" PRIx64 "\n",
				cl_ntoh16(dlid),
				cl_ntoh64(osm_node_get_node_guid(sw->p_node)));
		}
	}

	OSM_LOG_EXIT(mgr->p_log);
}

static inline void update_dlid_to_vl_mapping(uint8_t * dlid_to_vl_mapping,
					     const ib_net16_t dlid,
					     const uint8_t virtual_layer)
{
	CL_ASSERT(dlid_to_vl_mapping && dlid > 0);
	dlid_to_vl_mapping[cl_ntoh16(dlid)] = virtual_layer;
}

static int nue_do_ucast_routing(void *context)
{
	nue_context_t *nue_ctx = (nue_context_t *) context;
	osm_ucast_mgr_t *mgr = NULL;
	osm_port_t *dest_port = NULL;
	boolean_t include_switches = FALSE;
	ib_net16_t *dlid_iter = NULL;
	uint16_t lid = 0, min_lid_ho = 0, max_lid_ho = 0;
	uint16_t i = 0;
	uint8_t vl = 0;
	uint8_t ntype = 0;
	int err = 0;
	int32_t color = 0;
	boolean_t process_sw = FALSE, fallback_to_escape_paths = FALSE;
	network_node_t *netw_node_iter = NULL;
#if defined (_DEBUG_)
	ccdg_t verify_ccdg = {.num_nodes = 0, .nodes = NULL, .num_colors = 0,
			      .color_array = NULL};
#endif

	if (nue_ctx)
		mgr = (osm_ucast_mgr_t *) nue_ctx->mgr;
	else
		return -1;

	OSM_LOG_ENTER(mgr->p_log);
	OSM_LOG(mgr->p_log, OSM_LOG_INFO, "Start routing process with nue\n");

	init_linear_forwarding_tables(mgr, &(nue_ctx->network));

	if (mgr->p_subn->opt.nue_include_switches) {
		OSM_LOG(mgr->p_log, OSM_LOG_INFO,
			" ...and consider switches as traffic sinks\n");
		include_switches = mgr->p_subn->opt.nue_include_switches;
	}

	/* assign destination lids to different virtual layers */
	err = distribute_lids_onto_virtual_layers(nue_ctx, include_switches);
	if (err) {
		destroy_context(nue_ctx);
		return -1;
	}
	if (OSM_LOG_IS_ACTIVE_V2(mgr->p_log, OSM_LOG_DEBUG))
		print_destination_distribution(mgr, nue_ctx->destinations,
					       nue_ctx->num_destinations);

	for (vl = 0; vl < nue_ctx->max_vl; vl++) {
		OSM_LOG(mgr->p_log, OSM_LOG_DEBUG,
			"Processing virtual layer %" PRIu8 "\n", vl);

		if (!nue_ctx->num_destinations[vl]) {
			OSM_LOG(mgr->p_log, OSM_LOG_INFO,
				"WRN NUE43: no desti in this VL; skipping\n");
			continue;
		}

		color = ESCAPEPATHCOLOR + 1;
		err =
		    reset_ccdg_color_array(mgr, &(nue_ctx->ccdg),
					   nue_ctx->num_destinations,
					   nue_ctx->max_vl, nue_ctx->max_lmc);
		if (err) {
			destroy_context(nue_ctx);
			return -1;
		}
		init_ccdg_colors(&(nue_ctx->ccdg));

		err =
		    mark_escape_paths(mgr, &(nue_ctx->network),
				      &(nue_ctx->ccdg),
				      nue_ctx->destinations[vl],
				      nue_ctx->num_destinations[vl],
				      (0 == vl) ? TRUE : FALSE);
		if (err) {
			destroy_context(nue_ctx);
			return -1;
		}
		if (OSM_LOG_IS_ACTIVE_V2(mgr->p_log, OSM_LOG_DEBUG)) {
			OSM_LOG(mgr->p_log, OSM_LOG_DEBUG,
				"Complete CDG including escape paths for"
				" virtual layer %" PRIu8 "\n", vl);
			print_ccdg(mgr, &(nue_ctx->ccdg), TRUE);
		}

		/* in the debug mode we monitor the correctness more closely */
		CL_ASSERT(deep_cpy_ccdg(mgr, &(nue_ctx->ccdg), &verify_ccdg));

		process_sw = FALSE;
		do {
			dlid_iter = (ib_net16_t *) nue_ctx->destinations[vl];
			for (i = 0; i < nue_ctx->num_destinations[vl];
			     i++, dlid_iter++) {
				dest_port =
				    osm_get_port_by_lid(mgr->p_subn,
							*dlid_iter);
				ntype = osm_node_get_type(dest_port->p_node);
				if (ntype == IB_NODE_TYPE_CA) {
					if (process_sw)
						continue;
					OSM_LOG(mgr->p_log, OSM_LOG_DEBUG,
						"Processing Hca with GUID"
						" 0x%016" PRIx64 "\n",
						cl_ntoh64(osm_node_get_node_guid
							  (dest_port->p_node)));
				} else if (ntype == IB_NODE_TYPE_SWITCH) {
					if (!process_sw)
						continue;
					OSM_LOG(mgr->p_log, OSM_LOG_DEBUG,
						"Processing switch with GUID"
						" 0x%016" PRIx64 "\n",
						cl_ntoh64(osm_node_get_node_guid
							  (dest_port->p_node)));
				}

				/* distribute the LID range across the ports
				   that can reach those LIDs to have disjoint
				   paths for one destination port with lmc>0;
				   for switches with bsp0: min=max; with esp0:
				   max>min if lmc>0
				 */
				osm_port_get_lid_range_ho(dest_port,
							  &min_lid_ho,
							  &max_lid_ho);
				for (lid = min_lid_ho; lid <= max_lid_ho; lid++) {
					/* search a path from all nodes to dlid
					   without closing a cycle in the ccdg
					 */
					err =
					    route_via_modified_dijkstra_on_ccdg
					    (mgr, &(nue_ctx->network),
					     &(nue_ctx->ccdg), dest_port,
					     cl_hton16(lid), color++,
					     &fallback_to_escape_paths);
					if (err) {
						destroy_context(nue_ctx);
						return -1;
					}
					/* check intermediate steps for cycles
					   in the complete cdg
					 */
					CL_ASSERT(add_paths_to_verify_ccdg
						  (mgr, &(nue_ctx->network),
						   get_switch_lid(mgr,
								  cl_hton16
								  (lid)),
						   &(nue_ctx->ccdg),
						   &verify_ccdg,
						   fallback_to_escape_paths));
					CL_ASSERT(is_ccdg_cycle_free
						  (mgr, &verify_ccdg));
					/* print the updated complete cdg after
					   the routing for this desti is done
					 */
					if (OSM_LOG_IS_ACTIVE_V2
					    (mgr->p_log, OSM_LOG_DEBUG)) {
						OSM_LOG(mgr->p_log,
							OSM_LOG_DEBUG,
							"Complete CDG after routing destination LID %"
							PRIu16
							" for virtual layer %"
							PRIu8 "\n", lid, vl);
						print_ccdg(mgr,
							   &(nue_ctx->ccdg),
							   TRUE);
					}

					/* and print the calculated routes */
					if (OSM_LOG_IS_ACTIVE_V2
					    (mgr->p_log, OSM_LOG_DEBUG)) {
						OSM_LOG(mgr->p_log,
							OSM_LOG_DEBUG,
							"Calculated paths towards destination LID %"
							PRIu16 "\n", lid);
						print_routes(mgr,
							     &(nue_ctx->
							       network),
							     dest_port,
							     cl_hton16(lid));
					}

					/* update linear forwarding tables of
					   all switches towards this desti
					 */
					update_linear_forwarding_tables(mgr,
									&
									(nue_ctx->
									 network),
									dest_port,
									cl_hton16
									(lid));

					/* traverse the calculated paths and
					   update link weights for the next
					   step to increase the path balancing
					 */
					update_network_link_weights(mgr,
								    &(nue_ctx->
								      network),
								    get_switch_lid
								    (mgr,
								     cl_hton16
								     (lid)));

					/* and finally update the mapping of
					   'destination to virtual layer'
					 */
					update_dlid_to_vl_mapping(nue_ctx->
								  dlid_to_vl_mapping,
								  cl_hton16
								  (lid), vl);
				}
			}
			if (!process_sw && include_switches)
				process_sw = TRUE;
			else
				break;
		} while (TRUE);

		/* do a final check if ccdg is acyclic after processing all */
		CL_ASSERT(is_ccdg_cycle_free(mgr, &verify_ccdg));
	}

	/* if switches haven't been included in the original destinations set
	   then it's only because they send no real data traffic and therefore
	   aren't considered for deadlock-free routing, meaning we have to add
	   switch<->switch paths separately but can use a more simpler version
	   of Dijkstra's algo on the network, and don't have to use the
	   route_via_modified_dijkstra_on_ccdg function on the complete CDG
	 */
	if (!include_switches) {
		for (i = 0, netw_node_iter = nue_ctx->network.nodes;
		     i < nue_ctx->network.num_nodes; i++, netw_node_iter++) {
			dest_port =
			    osm_get_port_by_lid(mgr->p_subn,
						netw_node_iter->lid);
			OSM_LOG(mgr->p_log, OSM_LOG_DEBUG,
				"Processing switch with GUID 0x%016" PRIx64
				"\n",
				cl_ntoh64(osm_node_get_node_guid
					  (dest_port->p_node)));

			osm_port_get_lid_range_ho(dest_port, &min_lid_ho,
						  &max_lid_ho);
			for (lid = min_lid_ho; lid <= max_lid_ho; lid++) {
				/* use our simple multi-graph Dijkstra's algo */
				err =
				    calculate_spanning_tree_in_network(mgr,
								       &
								       (nue_ctx->
									network),
								       netw_node_iter);
				if (err) {
					destroy_context(nue_ctx);
					return -1;
				}

				/* the previous fucntion uses the escape path
				   variable to store the actual path, so we
				   have to copy it to the used_link variable
				 */
				use_escape_paths_to_solve_impass(mgr,
								 &(nue_ctx->
								   network),
								 dest_port,
								 cl_hton16
								 (lid));

				/* and now we can proceed with the usual, i.e.,
				   updating link weights and forwarding tables
				 */
				update_linear_forwarding_tables(mgr,
								&(nue_ctx->
								  network),
								dest_port,
								cl_hton16(lid));
				update_network_link_weights(mgr,
							    &(nue_ctx->network),
							    cl_hton16(lid));

				/* and we add them to VL0 */
				update_dlid_to_vl_mapping(nue_ctx->
							  dlid_to_vl_mapping,
							  cl_hton16(lid), 0);
			}
		}
	}

	OSM_LOG_EXIT(mgr->p_log);
	return 0;
}

/* reset is_mc_member and num_of_mcm for future computations */
static void reset_mgrp_membership(const network_t * network)
{
	network_node_t *netw_node_iter = NULL;
	uint16_t i = 0;

	CL_ASSERT(network && network->nodes);
	for (i = 0, netw_node_iter = network->nodes; i < network->num_nodes;
	     i++, netw_node_iter++) {
		CL_ASSERT(netw_node_iter->sw);

		if (netw_node_iter->dropped)
			continue;

		netw_node_iter->sw->is_mc_member = 0;
		netw_node_iter->sw->num_of_mcm = 0;
	}
}

static inline void mcast_cleanup(const network_t * network,
				 cl_qlist_t * mcastgrp_port_list)
{
	reset_mgrp_membership(network);
	osm_mcast_drop_port_list(mcastgrp_port_list);
}

/* the function updates the multicast group membership information
   similar to create_mgrp_switch_map (see osm_mcast_mgr.c)
   => with it we can identify whether a switch needs to be processed or not
   in our update_mcft function
 */
static void update_mgrp_membership(cl_qlist_t * port_list)
{
	osm_mcast_work_obj_t *work_obj = NULL;
	osm_port_t *osm_port = NULL;
	osm_node_t *rem_node = NULL;
	cl_list_item_t *item = NULL;
	uint8_t rem_port = 0;

	CL_ASSERT(port_list);
	for (item = cl_qlist_head(port_list); item != cl_qlist_end(port_list);
	     item = cl_qlist_next(item)) {
		work_obj = cl_item_obj(item, work_obj, list_item);
		osm_port = work_obj->p_port;
		if (IB_NODE_TYPE_CA == osm_node_get_type(osm_port->p_node)) {
			rem_node =
			    osm_node_get_remote_node(osm_port->p_node,
						     osm_port->p_physp->
						     port_num, &rem_port);
			CL_ASSERT(rem_node && rem_node->sw);
			rem_node->sw->num_of_mcm++;
		} else {
			CL_ASSERT(osm_port->p_node->sw);
			osm_port->p_node->sw->is_mc_member = 1;
		}
	}
}

/* update the multicast forwarding tables of all switches with the information
   from the previous mcast routing step for the current mlid
 */
static void update_mcast_forwarding_tables(const osm_ucast_mgr_t * mgr,
					   const network_t * network,
					   const uint16_t mlid_ho,
					   const cl_qmap_t * port_map,
					   const network_node_t * root_node)
{
	network_node_t *network_node = NULL, *netw_node_iter = NULL;
	osm_switch_t *sw = NULL;
	osm_mcast_tbl_t *mcast_tbl = NULL;
	uint16_t i = 0;
	osm_node_t *rem_node = NULL;
	uint8_t port = 0, rem_port = 0, upstream_port = 0, downstream_port = 0;
	osm_physp_t *osm_physp = NULL;
	ib_net64_t guid = 0;

	CL_ASSERT(mgr && network && network->nodes && port_map && root_node);
	OSM_LOG_ENTER(mgr->p_log);

	for (i = 0, netw_node_iter = network->nodes; i < network->num_nodes;
	     i++, netw_node_iter++) {
		CL_ASSERT(netw_node_iter->sw);

		network_node = netw_node_iter;
		sw = network_node->sw;

		if (network_node->dropped)
			continue;

		OSM_LOG(mgr->p_log, OSM_LOG_VERBOSE,
			"Processing switch 0x%016" PRIx64 " (%s) for MLID 0x%"
			PRIX16 "\n", cl_ntoh64(network_node->guid),
			sw->p_node->print_desc, mlid_ho);

		/* if switch does not support mcast or no ports of this switch
		   are part or the mcast group, then jump to the next switch
		 */
		if (FALSE == osm_switch_supports_mcast(sw)
		    || (0 == sw->num_of_mcm && !(sw->is_mc_member)))
			continue;

		mcast_tbl = osm_switch_get_mcast_tbl_ptr(network_node->sw);

		/* add all ports of this switch to the mcast table,
		   if these are part of the mcast group
		 */
		if (sw->is_mc_member)
			osm_mcast_tbl_set(mcast_tbl, mlid_ho, 0);
		for (port = 1; port < sw->num_ports; port++) {
			/* get the node behind the port */
			rem_node =
			    osm_node_get_remote_node(sw->p_node, port,
						     &rem_port);
			/* check if connected and it's not the same switch */
			if (!rem_node || sw == rem_node->sw)
				continue;
			/* make sure the link is healthy */
			osm_physp = osm_node_get_physp_ptr(sw->p_node, port);
			if (!osm_physp || !osm_link_is_healthy(osm_physp))
				continue;
			/* we do not add upstream ports in this step */
			if (IB_NODE_TYPE_CA != osm_node_get_type(rem_node))
				continue;

			/* add the exit port to the mcast forwarding table */
			guid =
			    osm_physp_get_port_guid(osm_node_get_physp_ptr
						    (rem_node, rem_port));
			if (cl_qmap_get(port_map, guid) !=
			    cl_qmap_end(port_map))
				osm_mcast_tbl_set(mcast_tbl, mlid_ho, port);
		}

		/* now we have to add the upstream port of 'this' switch and
		   the downstream port of the next switch to the mcast table
		   until we reach the root_sw
		 */
		while (network_node != root_node) {
			/* the escape_path variable holds the link that was
			   used in the spanning tree calculation to reach
			   this node, so remote_port in link_info is the
			   local (upstream) port for on network_node->sw
			 */
			upstream_port =
			    network_node->escape_path->link_info.remote_port;
			osm_mcast_tbl_set(mcast_tbl, mlid_ho, upstream_port);

			/* now we go one step in direction root_sw and add the
			   downstream port for the spanning tree
			 */
			downstream_port =
			    network_node->escape_path->link_info.local_port;
			network_node =
			    get_network_node_by_lid(network, network_node->
						    escape_path->link_info.
						    local_lid);
			CL_ASSERT(network_node);
			mcast_tbl =
			    osm_switch_get_mcast_tbl_ptr(network_node->sw);
			osm_mcast_tbl_set(mcast_tbl, mlid_ho, downstream_port);
		}
	}

	OSM_LOG_EXIT(mgr->p_log);
}

/* Nue configures multicast forwarding tables by utilizing a spanning tree
   calculation routed at a subnet switch suggested by OpenSM's internal
   osm_mcast_mgr_find_root_switch(...) fn; however, Nue routing currently does
   not guarantee deadlock-freedom for the set of multicast routes on all
   topologies, nor for the combination of deadlock-free unicast routes with
   the additional multicast routes
 */
static ib_api_status_t nue_do_mcast_routing(void *context,
					    osm_mgrp_box_t * mbox)
{
	nue_context_t *nue_ctx = (nue_context_t *) context;
	osm_ucast_mgr_t *mgr = NULL;
	cl_qlist_t mcastgrp_port_list;
	cl_qmap_t mcastgrp_port_map;
	uint16_t num_mcast_ports = 0, i = 0;
	osm_switch_t *root_sw = NULL, *osm_sw = NULL;
	network_t *network = NULL;
	network_node_t *root_node = NULL, *network_node = NULL;
	network_node_t *netw_node_iter = NULL;
	ib_net64_t guid = 0;
	int err = 0;

	if (nue_ctx)
		mgr = (osm_ucast_mgr_t *) nue_ctx->mgr;
	else
		return IB_ERROR;

	CL_ASSERT(mgr && mbox);
	OSM_LOG_ENTER(mgr->p_log);

	/* using the ucast cache feature with nue might mean that a leaf sw
	   got removed (and got back) without calling nue_discover_network and
	   therefore the stored netw (and pointers to osm's internal switches)
	   could be outdated (here we have no knowledge if it has happened, so
	   unfortunately a check is necessary... still better than rebuilding
	   nue_ctx->network every time we arrive here)
	 */
	if (mgr->p_subn->opt.use_ucast_cache && mgr->cache_valid) {
		network = (network_t *) & (nue_ctx->network);
		for (i = 0, netw_node_iter = network->nodes;
		     i < network->num_nodes; i++, netw_node_iter++) {
			CL_ASSERT(netw_node_iter->sw);

			network_node = netw_node_iter;
			guid = network_node->guid;
			osm_sw = osm_get_switch_by_guid(mgr->p_subn, guid);
			if (osm_sw) {
				/* check if switch came back from the dead */
				if (network_node->dropped)
					network_node->dropped = FALSE;

				/* verify that sw object has not been moved
				   (this can happen for a leaf switch, if it
				   was dropped and came back later without a
				   rerouting), otherwise we have to update
				   nue's internal switch pointer with the new
				   sw pointer
				 */
				if (osm_sw == network_node->sw)
					continue;
				else
					network_node->sw = osm_sw;
			} else {
				/* if a switch from adj_list is not in the
				   sw_guid_tbl anymore, then the only reason is
				   that it was a leaf switch and opensm dropped
				   it without calling a rerouting
				   -> calling calculate_spanning_tree_in_network
				      is no problem, since it is a leaf and
				      different from root_sw
				   -> only update_mcast_forwarding_tables and
				      reset_mgrp_membership need to be aware of
				      these dropped switches
				 */
				if (!network_node->dropped)
					network_node->dropped = TRUE;
			}
		}
	}

	/* create a map and a list of all ports which are member in the mcast
	   group (a map to search elements and a list for iteration)
	 */
	err =
	    osm_mcast_make_port_list_and_map(&mcastgrp_port_list,
					     &mcastgrp_port_map, mbox);
	if (err) {
		OSM_LOG(mgr->p_log, OSM_LOG_ERROR,
			"ERR NUE36: insufficient memory to make port list for"
			" MLID 0x%" PRIX16 "\n", mbox->mlid);
		mcast_cleanup(&(nue_ctx->network), &mcastgrp_port_list);
		return IB_ERROR;
	}

	num_mcast_ports = (uint16_t) cl_qlist_count(&mcastgrp_port_list);
	if (num_mcast_ports < 2) {
		OSM_LOG(mgr->p_log, OSM_LOG_VERBOSE,
			"MLID 0x%" PRIX16 " has %" PRIu16
			" member; nothing to do\n", mbox->mlid,
			num_mcast_ports);
		mcast_cleanup(&(nue_ctx->network), &mcastgrp_port_list);
		return IB_SUCCESS;
	}

	/* find the root switch for the spanning tree, which has the smallest
	   hops count to all LIDs in the mcast group
	 */
	root_sw = osm_mcast_mgr_find_root_switch(mgr->sm, &mcastgrp_port_list);
	if (!root_sw) {
		OSM_LOG(mgr->p_log, OSM_LOG_ERROR,
			"ERR NUE37: unable to locate a suitable root switch for"
			" MLID 0x%" PRIX16 "\n", mbox->mlid);
		mcast_cleanup(&(nue_ctx->network), &mcastgrp_port_list);
		return IB_ERROR;
	}

	/* find the root_sw in Nue's internal network node list */
	root_node =
	    get_network_node_by_lid(&(nue_ctx->network),
				    osm_node_get_base_lid(root_sw->p_node, 0));
	if (!root_node) {
		OSM_LOG(mgr->p_log, OSM_LOG_ERROR,
			"ERR NUE38: cannot find root_sw with LID %" PRIu16
			" in network node list while processing MLID 0x%" PRIX16
			"\n",
			cl_ntoh16(osm_node_get_base_lid(root_sw->p_node, 0)),
			mbox->mlid);
		mcast_cleanup(&(nue_ctx->network), &mcastgrp_port_list);
		return IB_ERROR;
	}

	/* calculate_spanning_tree_in_network does a bit more than needed
	   for the current problem, since we potentially only need a spanning
	   tree for a subgraph of the fabric, but performing the simple
	   Dijkstra's algorithm from the root_sw does not take too long;
	   we can reuse the subnet structure from the ucast routing, and
	   do not even have to reset the link weights (therefore the mcast
	   panning tree will use less 'crowded' links in the network);
	   only issue: calculate_spanning_tree_in_network encodes the tree
	   in the escape_path variable and not in the used_link of the nodes,
	   which is important for update_mcast_forwarding_tables function
	 */
	err =
	    calculate_spanning_tree_in_network(mgr, &(nue_ctx->network),
					       root_node);
	if (err) {
		OSM_LOG(mgr->p_log, OSM_LOG_ERROR,
			"ERR NUE39: failed to calculate spanning tree for"
			" MLID 0x%" PRIX16 "\n", mbox->mlid);
		mcast_cleanup(&(nue_ctx->network), &mcastgrp_port_list);
		return IB_ERROR;
	}

	/* set mcast group membership again for update_mcft, because for
	   some reason it has been reset by osm_mcast_mgr_find_root_switch fn
	 */
	update_mgrp_membership(&mcastgrp_port_list);

	/* update the mcast forwarding tables of the switches in the fabric */
	update_mcast_forwarding_tables(mgr, &(nue_ctx->network), mbox->mlid,
				       &mcastgrp_port_map, root_node);

	mcast_cleanup(&(nue_ctx->network), &mcastgrp_port_list);
	OSM_LOG_EXIT(mgr->p_log);
	return IB_SUCCESS;
}

static uint8_t nue_get_vl_for_path(void *context,
				   const uint8_t hint_for_default_sl,
				   const ib_net16_t slid, const ib_net16_t dlid)
{
	nue_context_t *nue_ctx = (nue_context_t *) context;
	osm_port_t *dest_port = NULL;
	osm_ucast_mgr_t *mgr = NULL;

	if (nue_ctx && nue_ctx->routing_type == OSM_ROUTING_ENGINE_TYPE_NUE)
		mgr = (osm_ucast_mgr_t *) nue_ctx->mgr;
	else
		return hint_for_default_sl;

	/* Assuming Nue was only allowed to use one virtual layer, then the
	   actual path-to-vl mapping is irrelevant, since all paths can be
	   assigned to any VL without creating credit loops. Hence, we can just
	   return the suggested/hinted SL to support various QoS levels.
	 */
	if (1 == nue_ctx->max_vl)
		return hint_for_default_sl;

	dest_port = osm_get_port_by_lid(mgr->p_subn, dlid);
	if (!dest_port)
		return hint_for_default_sl;

	if (!nue_ctx->dlid_to_vl_mapping)
		return hint_for_default_sl;

	return nue_ctx->dlid_to_vl_mapping[cl_ntoh16(dlid)];
}

static void destroy_context(nue_context_t * nue_ctx)
{
	uint8_t i = 0;

	destroy_network(&(nue_ctx->network));
	destroy_ccdg(&(nue_ctx->ccdg));

	for (i = 0; i < IB_MAX_NUM_VLS; i++) {
		if (nue_ctx->destinations[i]) {
			free(nue_ctx->destinations[i]);
			nue_ctx->destinations[i] = NULL;
		}
	}

	if (nue_ctx->dlid_to_vl_mapping) {
		free(nue_ctx->dlid_to_vl_mapping);
		nue_ctx->dlid_to_vl_mapping = NULL;
	}
}

static void nue_destroy_context(void *context)
{
	nue_context_t *nue_ctx = (nue_context_t *) context;
	if (!nue_ctx)
		return;
	destroy_context(nue_ctx);
	free(context);
}

int osm_ucast_nue_setup(struct osm_routing_engine *r, osm_opensm_t * osm)
{
	/* create context container and add ucast management object */
	nue_context_t *nue_context =
	    nue_create_context(osm, OSM_ROUTING_ENGINE_TYPE_NUE);
	if (!nue_context)
		return 1;	/* alloc failed -> skip this routing */

	/* reset function pointers to nue routines */
	r->context = (void *)nue_context;
	r->build_lid_matrices = nue_discover_network;
	r->ucast_build_fwd_tables = nue_do_ucast_routing;
	r->ucast_dump_tables = NULL;
	r->update_sl2vl = NULL;
	r->update_vlarb = NULL;
	r->path_sl = nue_get_vl_for_path;
	r->mcast_build_stree = nue_do_mcast_routing;
	r->destroy = nue_destroy_context;

	return 0;
}

/* basic_graph.h                                                   -*- C++ -*-
   Jeremy Barnes, 15 September, 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   Basic graph data structure.
*/

#ifndef __jgraph__basic_graph_h__
#define __jgraph__basic_graph_h__

#include <string>
#include <vector>
#include "attribute.h"
#include "utils/hash_map.h"

namespace JGraph {


/*****************************************************************************/
/* BASICGRAPH                                                                */
/*****************************************************************************/

struct BasicGraph {
    /// Create the given graph with the given name
    BasicGraph(const std::string & name);

    /// Set the attribute on the given node
    void setNodeAttr(int node_handle, const Attribute & attr);

    /// Add the given node type to the metadata; returns its handle
    int addNodeType(const std::string & name);

    /// Add the given edge type to the metadata; returns its handle
    int addEdgeType(const std::string & name,
                    const EdgeBehavior & behavior);

    int addNodeAttributeType(const std::string & name);
                             

    /// Create a new node, returning its handle
    int createNode(int type_handle);

    /// Create a new node, returning its handle
    int getOrCreateNode(int type_handle,
                        const Attribute & attribute);

    /// Create a new edge, returning its handle
    int getOrCreateEdge(int from_handle,
                        int to_handle,
                        int type_handle);
    
private:
    int handle;
    std::string name;

    struct Metadata {
        struct Entry {
            std::string name;
            int id;
        };

        typedef std::hash_map<std::string, int> Index;
        Index index;
        std::vector<Entry> entries;

        int getOrCreate(const std::string & name);
    };

    Metadata node_metadata, edge_metadata;

    struct Edge {
        int type;
        int adjacent_node;
    };

    struct Node {
        int type;
        std::vector<Attribute> attributes;
        std::vector<Edge> edges;
    };

    std::vector<Node> nodes;
                             
};

} // namespace JGraph


#endif /* __jgraph__basic_graph_h__ */

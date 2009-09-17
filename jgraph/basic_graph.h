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
#include "utils/compact_vector.h"
#include <boost/shared_ptr.hpp>
#include "utils/less.h"
#include <typeinfo>


namespace JGraph {


/*****************************************************************************/
/* BASICGRAPH                                                                */
/*****************************************************************************/

struct BasicGraph {
    typedef int NodeHandle;
    typedef int EdgeHandle;

    /// Create the given graph with the given name
    BasicGraph(const std::string & name);

    /// Set the attribute on the given node
    void setNodeAttr(int node_type, int node_handle, const Attribute & attr);

    /// Add the given node type to the metadata; returns its handle
    int addNodeType(const std::string & name);

    /// Add the given edge type to the metadata; returns its handle
    int addEdgeType(const std::string & name,
                    const EdgeBehavior & behavior);

    /// Add a new attribute type for the given node.  The traits give a
    /// suggestion for the traits object to be used; it may not be the one
    /// finally used).
    std::pair<int, AttributeTraits *>
    addNodeAttributeType(const std::string & name, int node_type,
                         boost::shared_ptr<AttributeTraits> candidate_traits);
                             

    /// Create a new node, returning its handle
    int createNode(int type_handle);

    /// Create a new node, returning its handle
    int getOrCreateNode(int type_handle,
                        const Attribute & attribute);

    /// Create a new edge, returning its handle
    int getOrCreateEdge(int from_node_type,
                        int from_node_handle,
                        int to_node_type,
                        int to_node_handle,
                        int edge_type_handle);

private:
    int handle;
    std::string name;

    struct Metadata {
        struct Entry {
            std::string name;
            int id;
            boost::shared_ptr<AttributeTraits> traits;
        };

        typedef std::hash_map<std::string, int> Index;
        Index index;
        std::vector<Entry> entries;

        int getOrCreate(const std::string & name);
    };

    Metadata node_metadata, edge_metadata;

    Metadata node_attr_metadata, edge_attr_metadata;

    typedef ML::compact_vector<AttributeRef, 1> AttributeSet;

    // TODO: compact...
    struct EdgeRef {
        EdgeRef(bool forward = false, int type = 0, int dest = -1,
                int index = -1)
            : forward(forward), type(type), dest(dest), index(index)
        {
        }

        bool forward;
        int type;   // type of the edge
        int dest;   // destination of the edge
        int index;  // number in the collection
 
        bool operator < (const EdgeRef & other) const
        {
            return ML::less_all(forward, other.forward,
                                type, other.type,
                                dest, other.dest,
                                index, other.index);
        }
    };
 
    typedef ML::compact_vector<EdgeRef, 2> EdgeRefList;

    struct Node {
        AttributeSet attributes;
        EdgeRefList edges;
    };

    struct Edge {
        int from;
        int from_type;
        int to;
        int to_type;
        AttributeSet attributes;
    };

    struct EdgeCollection {
        int type;
        std::vector<Edge> edges;
    };

    struct NodeCollection {
        std::vector<Node> nodes;
        std::hash_map<AttributeRef, int> id_index;
    };
    
    std::vector<boost::shared_ptr<NodeCollection> > nodes_of_type;
    std::vector<boost::shared_ptr<EdgeCollection> > edges_of_type;

    NodeCollection & getNodeCollection(int node_type);
    EdgeCollection & getEdgeCollection(int edge_type);
};

} // namespace JGraph


#endif /* __jgraph__basic_graph_h__ */

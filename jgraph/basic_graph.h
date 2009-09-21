/* basic_graph.h                                                   -*- C++ -*-
   Jeremy Barnes, 15 September, 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   Basic graph data structure.
*/

#ifndef __jgraph__basic_graph_h__
#define __jgraph__basic_graph_h__

#include "jgraph_fwd.h"
#include <string>
#include <vector>
#include "attribute.h"
#include "utils/hash_map.h"
#include "utils/compact_vector.h"
#include <boost/shared_ptr.hpp>
#include "utils/less.h"
#include <typeinfo>
#include <boost/function.hpp>
#include "utils/unnamed_bool.h"


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

#if 0
    struct NodeSetGenerator {
    private:
        boost::function<bool (void *)> increment;
        boost::function<int (void *)> generate;
        boost::function<void (void *)> done;
        int node_type;
        void * data;
        bool valid;
    public:
        JML_IMPLEMENT_OPERATOR_BOOL(!valid);
        bool next() const;// { return increment(data); }
        int operator * () const;
    };
#else
    // Generate nodes from a set
    struct NodeSetGenerator {
        typedef NodeT<BasicGraph> ResultType;

        template<class Iterator>
        NodeSetGenerator(BasicGraph * graph, int node_type,
                         Iterator first, Iterator last)
            : graph(graph), node_type(node_type)
        {
            if (first == last) {
                current = -1;
                index = 0;
                return;
            }

            // If possible (a single result) don't create a vector
            current = *first;
            index = 0;

            ++first;
            if (first != last) {
                values.reset(new std::vector<int>());
                values->push_back(current);
                values->insert(values->end(),
                               first, last);
                std::sort(values->begin(), values->end());
                current = (*values)[0];
                index = 0;
            }
        }

        JML_IMPLEMENT_OPERATOR_BOOL(current != -1);

        NodeT<BasicGraph> curr() const;

        bool next();

    private:
        BasicGraph * graph;
        int node_type;
        boost::shared_ptr<std::vector<int> > values;
        int current;
        int index;
    };
#endif

    /// Query nodes of the given type with the given attribute
    NodeSetGenerator
    nodesMatchingAttr(int node_type, const Attribute & attr) const;

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

    typedef ML::compact_vector<AttributeRef, 1> AttributeSetBase;

    struct AttributeSet : public AttributeSetBase {
        const AttributeRef & find(int type) const;
    };

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

    typedef std::hash_map<AttributeRef, int> AttributeIndexBase;
    struct AttributeIndex : AttributeIndexBase {
        int getUnique(const Attribute & attr) const
        {
            std::pair<const_iterator, const_iterator> range
                = equal_range(attr);
            if (range.first == range.second) return -1;
            int result = range.first->second;
            ++range.first;
            if (range.first != range.second)
                throw ML::Exception("AttributeIndex::getUnique(): multiple");
            return result;
        }
    };

    struct NodeCollection {
        std::vector<Node> nodes;

        // For each attribute, an index of the value of the attribute
        std::hash_map<int, boost::shared_ptr<AttributeIndex> > attribute_index;

        AttributeIndex & getAttributeIndex(int attr_num);
    };
    
    mutable std::vector<boost::shared_ptr<NodeCollection> > nodes_of_type;
    mutable std::vector<boost::shared_ptr<EdgeCollection> > edges_of_type;

    NodeCollection & getNodeCollection(int node_type) const;
    EdgeCollection & getEdgeCollection(int edge_type) const;
};

} // namespace JGraph


#endif /* __jgraph__basic_graph_h__ */

/* basic_graph.h                                                   -*- C++ -*-
   Jeremy Barnes, 15 September, 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   Basic graph data structure.
*/

#ifndef __jgraph__basic_graph_h__
#define __jgraph__basic_graph_h__

#include "jgraph.h"
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
#include <boost/iterator/iterator_facade.hpp>


namespace JGraph {


/*****************************************************************************/
/* BASICGRAPH                                                                */
/*****************************************************************************/

struct BasicGraph {
    typedef int NodeHandle;
    typedef int EdgeHandle;

    /// Create the given graph with the given name
    BasicGraph(const std::string & name);

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

    /// Set the attribute on the given node
    void setNodeAttr(int node_type, int node_handle, const Attribute & attr);

    /// Set the attribute on the given node, ensuring that there is only a
    /// single one for each node
    void setOrReplaceNodeAttr(int node_type, int node_handle,
                              const Attribute & attr);

    /// Get the (single) value of the attribute on the given node
    AttributeRef
    getNodeAttr(int node_type, int node_handle, int attr_type) const;

    /// Print the contents of a node
    std::string printNode(int node_type, int node_handle) const;

    /// Create a new edge, returning its handle
    std::pair<int, EdgeDirection>
    getOrCreateEdge(int from_node_type,
                    int from_node_handle,
                    int to_node_type,
                    int to_node_handle,
                    int edge_type_handle);

    // Generate nodes from a set, with a coherent node type
    struct CoherentNodeSetGenerator {
        typedef NodeT<BasicGraph> ResultType;

        CoherentNodeSetGenerator()
            : graph(0), node_type(-1), current(-1), index(0), max_index(0)
        {
        }

        template<class Iterator>
        CoherentNodeSetGenerator(BasicGraph * graph, int node_type,
                                 Iterator first, Iterator last)
            : graph(graph), node_type(node_type)
        {
            if (first == last) {
                current = -1;
                index = 0;
                max_index = 0;
                return;
            }

            // If possible (a single result) don't create a vector
            current = *first;
            index = 0;
            max_index = 1;

            ++first;
            if (first != last) {
                values.reset(new std::vector<int>());
                values->push_back(current);
                values->insert(values->end(),
                               first, last);
                std::sort(values->begin(), values->end());
                current = (*values)[0];
                index = 0;
                max_index = values->size();
            }
        }
        
        CoherentNodeSetGenerator(BasicGraph * graph, int node_type,
                                 int num_nodes)
            : graph(graph), node_type(node_type)
        {
            if (num_nodes == 0) {
                current = -1;
                index = 0;
                return;
            }

            current = 0;
            max_index = num_nodes;
            index = 0;
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
        int max_index;
    };

    /// Query nodes of the given type with the given attribute
    CoherentNodeSetGenerator
    nodesMatchingAttr(int node_type, const Attribute & attr) const;

    /// Return all nodes in the graph of the given type
    CoherentNodeSetGenerator
    allNodesOfType(int node_type) const;

    /// Return the type of the node with the given name
    int nodeTypeFromName(const std::string & name) const;

    /// Return the number of nodes of the given type in the graph
    int numNodesOfType(int node_type) const;

    /// Return the maximum index value of nodes of the given type in the
    /// graph
    int maxIndexOfType(int node_type) const;

private:
    int handle;
    std::string name;

    struct MetadataEntry {
        std::string name;
        int id;
        boost::shared_ptr<AttributeTraits> traits;
    };

    typedef MetadataEntry NodeMetadataEntry;

    struct EdgeMetadataEntry : MetadataEntry {
        EdgeBehavior behavior;
    };

    typedef MetadataEntry AttrMetadataEntry;

    template<class Entry>
    struct Metadata {
        typedef std::hash_map<std::string, int> Index;
        Index index;
        std::vector<Entry> entries;

        int getOrCreate(const std::string & name);
        int getOrError(const std::string & name) const;
    };

    Metadata<NodeMetadataEntry> node_metadata;
    Metadata<EdgeMetadataEntry> edge_metadata;
    Metadata<AttrMetadataEntry> node_attr_metadata, edge_attr_metadata;

    typedef ML::compact_vector<AttributeRef, 1> AttributeSetBase;

    struct AttributeSet : public AttributeSetBase {
        const AttributeRef & find(int type) const;

        /// Find or add the given attribute.  Returns the old value if it was
        /// modified, or a null attribute if not.
        AttributeRef replace(const Attribute & attr);
    };

    // TODO: compact...
    struct EdgeRef {
        EdgeRef(EdgeDirection direction = ED_FORWARDS, int edge_type = 0,
                int dest_type = -1, int dest_node = -1,
                int index = -1)
            : direction(direction), edge_type(edge_type), dest_type(dest_type),
              dest_node(dest_node), index(index)
        {
        }

        EdgeDirection direction;
        int edge_type;   // type of the edge
        int dest_type;   // type of destination of the edge
        int dest_node;   // node index of destination of the edge
        int index;       // number in the collection
 
        bool operator < (const EdgeRef & other) const
        {
            return ML::less_all(direction, other.direction,
                                edge_type, other.edge_type,
                                dest_type, other.dest_type,
                                dest_node, other.dest_node,
                                index, other.index);
        }
    };
 
    typedef ML::compact_vector<EdgeRef, 2> EdgeRefListBase;

    struct EdgeRefList {
        EdgeRefList()
            : sorted(true)
        {
        }

        void sort() const
        {
            std::sort(edges.begin(), edges.end());
            sorted = true;
        }

        typedef EdgeRefListBase::const_iterator const_iterator;

        const_iterator begin() const
        {
            return edges.begin();
        }

        const_iterator end() const
        {
            return edges.end();
        }

        void insert(const EdgeRef & ref)
        {
            bool new_sorted = false;
            if (!empty() && sorted)
                new_sorted = edges.back() < ref;
            edges.push_back(ref);
            sorted = new_sorted;
        }

        bool empty() const { return edges.empty(); }
        size_t size() const { return edges.size(); }

        mutable EdgeRefListBase edges;
        mutable bool sorted;
    };

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

public:
    struct IncidentEdgeIterator
        : public boost::iterator_facade<IncidentEdgeIterator,
                                        EdgeT<BasicGraph>,
                                        boost::bidirectional_traversal_tag,
                                        EdgeT<BasicGraph> > {
        typedef EdgeRefList::const_iterator Underlying;
    public:
        IncidentEdgeIterator()
            : graph(0)
        {
        }

        IncidentEdgeIterator(const Underlying & it,
                             BasicGraph * graph,
                             int from_type, int from_handle)
            : it(it), graph(graph),
              from_type(from_type), from_handle(from_handle)
        {
        }

private:
        Underlying it;
        BasicGraph * graph;
        int from_type;
        int from_handle;
        friend class boost::iterator_core_access;

        EdgeT<BasicGraph> dereference() const
        {
            return EdgeT<BasicGraph>(graph, it->edge_type, it->index,
                                     it->direction, from_type, from_handle,
                                     it->dest_type, it->dest_node);
        }

        bool equal(const IncidentEdgeIterator & other) const
        {
            return it == other.it;
        }

        void increment()
        {
            ++it;
        }

        void decrement()
        {
            --it;
        }
    };

    std::pair<IncidentEdgeIterator, IncidentEdgeIterator>
    getIncidentEdges(int node_type, int node_handle,
                     int edge_type, bool out_edges) const;

    int getIncidentEdgeCount(int node_type, int node_handle,
                             int edge_type, bool out_edges) const;
};

} // namespace JGraph


#endif /* __jgraph__basic_graph_h__ */

/* basic_graph_boost.h                                             -*- C++ -*-
   Jeremy Barnes, 21 September 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   Boost adaptor for basic graph.
*/

#ifndef __jgraph__basic_graph_boost_h__
#define __jgraph__basic_graph_boost_h__

#include <boost/config.hpp>
#include <boost/iterator/iterator_facade.hpp>
#include <boost/graph/graph_traits.hpp>
#include <boost/graph/properties.hpp>

#include "basic_graph.h"

namespace JGraph {

template<class EdgeToFollow>
struct BoostGraphAdaptor {
    BoostGraphAdaptor(const EdgeToFollow & edge)
        : edge(edge)
    {
    }

    const EdgeToFollow & edge;
};

template<class UnderlyingGenerator>
struct NodeSetIterator;

template<class UnderlyingGenerator>
struct NodeSetIterator
    : public boost::iterator_facade<
          NodeSetIterator<UnderlyingGenerator>,
          typename UnderlyingGenerator::ResultType,
          boost::forward_traversal_tag,
          typename UnderlyingGenerator::ResultType,
          typename UnderlyingGenerator::ResultType *>,
      public UnderlyingGenerator {
public:
    NodeSetIterator()
    {
    }

    NodeSetIterator(const UnderlyingGenerator & gen)
        : UnderlyingGenerator(gen)
    {
    }

private:
    friend class boost::iterator_core_access;
    typename UnderlyingGenerator::ResultType dereference() const
    {
        return this->curr();
    }

    // The only interesting case is to see if they're both finished
    bool equal(const NodeSetIterator & other) const
    {
        if (!(*this) && !other) return true;
        return false;
    }

    void increment() { this->next(); }
};

template<class EdgeToFollow>
std::pair<NodeSetIterator<typename EdgeToFollow::GraphType::CoherentNodeSetGenerator>,
          NodeSetIterator<typename EdgeToFollow::GraphType::CoherentNodeSetGenerator> >
vertices(const BoostGraphAdaptor<EdgeToFollow> & adaptor)
{
    typedef typename EdgeToFollow::GraphType::CoherentNodeSetGenerator Gen;
    Gen generator
        = adaptor.edge.graph()->allNodesOfType(adaptor.edge.node_type());
    return make_pair(NodeSetIterator<Gen>(generator),
                     NodeSetIterator<Gen>());
}

template<class EdgeToFollow>
int
num_vertices(const BoostGraphAdaptor<EdgeToFollow> & adaptor)
{
    return adaptor.edge.graph()->numNodesOfType(adaptor.edge.node_type());
}


} // namespace JGraph

// Boost graph specializations
namespace boost {

template <class EdgeToFollow>
struct graph_traits<JGraph::BoostGraphAdaptor<EdgeToFollow> > {
    typedef typename EdgeToFollow::GraphType Graph;
    typedef typename JGraph::NodeT<Graph> vertex_descriptor;
    typedef typename JGraph::EdgeT<Graph> edge_descriptor;
    // iterator typedefs...
    
    vertex_descriptor null_vertex() const { return vertex_descriptor(); }

    typedef directed_tag directed_category;
    typedef allow_parallel_edge_tag edge_parallel_category;

    struct traversal_category
        : public bidirectional_graph_tag,
          public adjacency_graph_tag,
          public vertex_list_graph_tag {};

    typedef int vertices_size_type;
    typedef int edges_size_type;
    typedef int degree_size_type;
};





#if 0
template <>
struct graph_traits<JGraph::BasicGraph> {
    typedef JGraph::NodeT<JGraph::BasicGraph> vertex_descriptor;
    typedef JGraph::EdgeT<BasicGraph> edge_descriptor;
    typedef JGraph::BasicGraph::
    // iterator typedefs...
    
    vertex_descriptor null_vertex() const { return vertex_descriptor(); }

    typedef directed_tag directed_category;
    typedef allow_parallel_edge_tag edge_parallel_category;

    struct traversal_category
        : public bidirectional_graph_tag,
          public adjacency_graph_tag,
          public vertex_list_graph_tag
    {
    };

    typedef int vertices_size_type;
    typedef int edges_size_type;
    typedef int degree_size_type;
};

inline
NodeT<BasicGraph>
source(EdgeT<BasicGraph> & e, const BasicGraph & g)
{
    return e.from();
}

inline
NodeT<BasicGraph>
target(EdgeT<BasicGraph> & e, const BasicGraph & g)
{
    return e.to();
}

#endif

} // namespace boost

#endif /* __jgraph__basic_graph_boost_h__ */

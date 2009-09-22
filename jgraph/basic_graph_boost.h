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

// Boost graph specializations
namespace boost {

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

} // namespace boost

#endif /* __jgraph__basic_graph_boost_h__ */

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
class VertexIndexPropertyMap
    : public boost::put_get_helper<int, VertexIndexPropertyMap<EdgeToFollow> > {
public:
    typedef boost::readable_property_map_tag category;
    typedef int value_type;
    typedef int reference;
    typedef NodeT<typename EdgeToFollow::GraphType> key_type;
    
    template <class T>
    long operator[](const T & node) const
    {
        return node.handle;
    }
};

template<class EdgeToFollow>
VertexIndexPropertyMap<EdgeToFollow>
get(boost::vertex_index_t&, const BoostGraphAdaptor<EdgeToFollow> & gr)
{
    return VertexIndexPropertyMap<EdgeToFollow>();
}

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
    return adaptor.edge.graph()->maxIndexOfType(adaptor.edge.node_type());
}

template<class EdgeToFollow>
std::pair<typename EdgeToFollow::GraphType::IncidentEdgeIterator,
          typename EdgeToFollow::GraphType::IncidentEdgeIterator>
out_edges(const NodeT<typename EdgeToFollow::GraphType> & node,
          const BoostGraphAdaptor<EdgeToFollow> & adaptor)
{
    return node.graph->getIncidentEdges(node.node_type, node.handle,
                                        adaptor.edge.edge_type(),
                                        true);
}

template<class EdgeToFollow>
int
out_degree(const NodeT<typename EdgeToFollow::GraphType> & node,
           const BoostGraphAdaptor<EdgeToFollow> & adaptor)
{
    return node.graph->getIncidentEdgeCount(node.node_type, node.handle,
                                            adaptor.edge.edge_type(),
                                            true);
}

template<class EdgeToFollow>
NodeT<typename EdgeToFollow::GraphType>
target(const EdgeT<typename EdgeToFollow::GraphType> & edge,
       const BoostGraphAdaptor<EdgeToFollow> & adaptor)
{
    return edge.to();
}

template<class EdgeToFollow>
NodeT<typename EdgeToFollow::GraphType>
source(const EdgeT<typename EdgeToFollow::GraphType> & edge,
       const BoostGraphAdaptor<EdgeToFollow> & adaptor)
{
    return edge.from();
}


} // namespace JGraph

// Boost graph specializations
namespace boost {

template <class EdgeToFollow>
struct graph_traits<JGraph::BoostGraphAdaptor<EdgeToFollow> > {
    typedef typename EdgeToFollow::GraphType Graph;
    typedef typename JGraph::NodeT<Graph> vertex_descriptor;
    typedef typename JGraph::EdgeT<Graph> edge_descriptor;
    typedef typename JGraph::NodeSetIterator<typename Graph::CoherentNodeSetGenerator> vertex_iterator;
    // iterator typedefs...
    
    typedef typename Graph::IncidentEdgeIterator out_edge_iterator;
    typedef typename Graph::IncidentEdgeIterator in_edge_iterator;

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

template <class EdgeToFollow>
struct property_map<JGraph::BoostGraphAdaptor<EdgeToFollow>, vertex_index_t> {
    typedef JGraph::VertexIndexPropertyMap<EdgeToFollow> type;
    typedef const JGraph::VertexIndexPropertyMap<EdgeToFollow> const_type;
};


#if 0

Tagleda::GRAPH<vtype, etype>, Tag> {
    typedef typename 
      leda_property_map<Tag>::template bind_<vtype, etype> map_gen;
    typedef typename map_gen::type type;
    typedef typename map_gen::const_type const_type;
  };



  template <class vtype, class etype>
  inline leda_graph_id_map
  get(vertex_index_t, const leda::GRAPH<vtype, etype>& g) {
    return leda_graph_id_map();
  }
  template <class vtype, class etype>
  inline leda_graph_id_map
  get(edge_index_t, const leda::GRAPH<vtype, etype>& g) {
    return leda_graph_id_map();
  }

  template <class Tag>
  struct leda_property_map { };

  template <>
  struct leda_property_map<vertex_index_t> {
    template <class vtype, class etype>
    struct bind_ {
      typedef leda_graph_id_map type;
      typedef leda_graph_id_map const_type;
    };
  };
  template <>
  struct leda_property_map<edge_index_t> {
    template <class vtype, class etype>
    struct bind_ {
      typedef leda_graph_id_map type;
      typedef leda_graph_id_map const_type;
    };
  };




  template <class vtype, class etype, class Tag>
  struct property_map<leda::GRAPH<vtype, etype>, Tag> {
    typedef typename 
      leda_property_map<Tag>::template bind_<vtype, etype> map_gen;
    typedef typename map_gen::type type;
    typedef typename map_gen::const_type const_type;
  };

  template <class vtype, class etype, class PropertyTag, class Key>
  inline
  typename boost::property_traits<
    typename boost::property_map<leda::GRAPH<vtype, etype>,PropertyTag>::const_type
::value_type
  get(PropertyTag p, const leda::GRAPH<vtype, etype>& g, const Key& key) {
    return get(get(p, g), key);
  }

  template <class vtype, class etype, class PropertyTag, class Key,class Value>
  inline void
  put(PropertyTag p, leda::GRAPH<vtype, etype>& g, 
      const Key& key, const Value& value)
  {
    typedef typename property_map<leda::GRAPH<vtype, etype>, PropertyTag>::type Map;
    Map pmap = get(p, g);
    put(pmap, key, value);
  }


#endif

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

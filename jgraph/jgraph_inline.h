/* jgraph_inline.h                                                 -*- C++ -*-
   Jeremy Barnes, 15 September, 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   Inline functions for the jgraph class.
*/

#ifndef __jgraph__jgraph_inline_h__
#define __jgraph__jgraph_inline_h__


#include "compiler/compiler.h"
#include <typeinfo>
#include "utils/smart_ptr_utils.h"


namespace JGraph {


using ML::Exception;


/// Throws the exception; here so that it is out of line
void throw_uninitialized_exception(const char * object);

/// Internal function to check that value is true and if not, throw an
/// exception.
JML_ALWAYS_INLINE void check_initialized_impl(bool value,
                                              const char * object)
{
    if (JML_UNLIKELY(!value))
        throw_uninitialized_exception(object);
}


/*****************************************************************************/
/* NODET                                                                     */
/*****************************************************************************/

template<class Graph>
NodeT<Graph>::
NodeT()
    : graph(0), handle(-1), node_type(-1)
{
}

template<class Graph>
NodeT<Graph>::
NodeT(Graph * graph, int node_type, int handle)
    : graph(graph), handle(handle), node_type(node_type)
{
}

template<class Graph>
void
NodeT<Graph>::
setAttr(const Attribute & attr)
{
    check_initialized();
    graph->setNodeAttr(node_type, handle, attr);
}

template<class Graph>
void
NodeT<Graph>::
setOrReplaceAttr(const Attribute & attr)
{
    check_initialized();
    graph->setOrReplaceNodeAttr(node_type, handle, attr);
}

template<class Graph>
template<class Schema>
typename Schema::PayloadType
NodeT<Graph>::
getAttr(const Schema & schema) const
{
    check_initialized();
    AttributeRef attr
        = graph->getNodeAttr(node_type, handle, schema.attr_handle);
    if (!attr)
        throw Exception("attribute not found");
    return schema.traits->decode(attr);
}

template<class Graph>
template<class Schema>
typename Schema::PayloadType
NodeT<Graph>::
getAttrOrDefault(const Schema & schema,
                 const typename Schema::PayloadType & if_not_found) const
{
    check_initialized();
    AttributeRef attr
        = graph->getNodeAttr(node_type, handle, schema.attr_handle);
    if (!attr) return if_not_found;
    return schema.traits->decode(attr);
}

template<class Graph>
template<class EdgeSchema>
std::pair<typename Graph::IncidentEdgeIterator,
          typename Graph::IncidentEdgeIterator>
NodeT<Graph>::
outEdges(const EdgeSchema & edge_schema) const
{
    return graph->getIncidentEdges(node_type, handle, edge_schema.edge_type(),
                                   true /* out */);
}

template<class Graph>
template<class EdgeSchema>
std::pair<typename Graph::IncidentEdgeIterator,
          typename Graph::IncidentEdgeIterator>
NodeT<Graph>::
inEdges(const EdgeSchema & edge_schema) const
{
    return graph->getIncidentEdges(node_type, handle, edge_schema.edge_type(),
                                   false /* out */);
}

template<class Graph>
void
NodeT<Graph>::
check_initialized() const
{
    check_initialized_impl(graph, "NodeT");
}

template<class Graph>
std::string
NodeT<Graph>::
print() const
{
    return graph->printNode(node_type, handle);
}

template<class Graph>
std::ostream &
operator << (std::ostream & stream, const NodeT<Graph> & node)
{
    return stream << node.print();
}


/*****************************************************************************/
/* EDGET                                                                     */
/*****************************************************************************/

template<class Graph>
EdgeT<Graph>::
EdgeT()
    : graph(0), edge_type(-1), edge_handle(-1), direction_(ED_FORWARDS),
      from_type(-1), from_handle(-1),
      to_type(-1), to_handle(-1)
{
}

template<class Graph>
EdgeT<Graph>::
EdgeT(Graph * graph, int edge_type, int edge_handle,
      EdgeDirection direction,
      int from_type, int from_handle, int to_type, int to_handle)
    : graph(graph), edge_type(edge_type), edge_handle(edge_handle),
      direction_(direction),
      from_type(from_type), from_handle(from_handle),
      to_type(to_type), to_handle(to_handle)
{
}

template<class Graph>
NodeT<Graph>
EdgeT<Graph>::
from() const
{
    return NodeT<Graph>(graph, from_type, from_handle);
}

template<class Graph>
NodeT<Graph>
EdgeT<Graph>::
to() const
{
    return NodeT<Graph>(graph, to_type, to_handle);
}

template<class Graph>
EdgeDirection
EdgeT<Graph>::
direction() const
{
    return direction_;
}

template<class Graph>
std::string
EdgeT<Graph>::
print() const
{
    return graph->printEdge(edge_type, edge_handle);
}

template<class Graph>
std::ostream &
operator << (std::ostream & stream, const EdgeT<Graph> & edge)
{
    return stream << edge.print();
}


/*****************************************************************************/
/* SCHEMAT                                                                   */
/*****************************************************************************/

template<class Graph>
SchemaT<Graph>::
SchemaT()
    : graph_(0)
{
}

template<class Graph>
SchemaT<Graph>::
SchemaT(Graph & graph)
    : graph_(&graph)
{
}


/*****************************************************************************/
/* NODESCHEMAT                                                               */
/*****************************************************************************/

template<class Graph>
NodeSchemaT<Graph>::
NodeSchemaT(Graph & graph, const std::string & name)
    : SchemaT<Graph>(graph)
{
    this->node_type_ = graph.addNodeType(name);
}

template<class Graph>
NodeT<Graph>
NodeSchemaT<Graph>::
operator () () const
{
    return NodeT<Graph>(graph(), node_type_,
                        graph()->createNode(node_type_));
}

template<class Graph>
NodeT<Graph>
NodeSchemaT<Graph>::
operator () (const Attribute & attr1) const
{
    return NodeT<Graph>(graph(), node_type_,
                        graph()->getOrCreateNode(node_type_, attr1));
}

template<class Graph>
template<class Filter>
SelectNodes<Graph, Filter>
NodeSchemaT<Graph>::
operator [] (const Filter & filter) const
{
    return SelectNodes<Graph, Filter>(filter);
}

template<class Graph>
void
NodeSchemaT<Graph>::
check_initialized() const
{
    check_initialized_impl(this->graph, "NodeSchemaT");
}


/*****************************************************************************/
/* BIPARTITEEDGESCHEMAT                                                      */
/*****************************************************************************/

template<class Graph>
BipartiteEdgeSchemaT<Graph>::
BipartiteEdgeSchemaT(Graph & graph, const std::string & name,
                     const NodeSchemaT<Graph> & from_node_schema,
                     const NodeSchemaT<Graph> & to_node_schema,
                     EdgeBehavior behavior)
    : SchemaT<Graph>(graph),
      from_node_schema_(&from_node_schema),
      to_node_schema_(&to_node_schema)
{
    edge_type_ = graph.addEdgeType(name, behavior);
}

template<class Graph>
EdgeT<Graph>
BipartiteEdgeSchemaT<Graph>::
operator () (const NodeT<Graph> & from,
             const NodeT<Graph> & to) const
{
    if (from.graph != this->graph() || to.graph != this->graph())
        throw Exception("attempt to create edge between graphs");

    if (from.node_type != from_node_schema_->node_type())
        throw Exception("attempt to create edge from wrong node type");

    if (to.node_type != to_node_schema_->node_type())
        throw Exception("attempt to create edge to wrong node type");

    std::pair<int, EdgeDirection> edge_params
        = graph()->getOrCreateEdge(from.node_type, from.handle,
                                 to.node_type, to.handle,
                                   this->edge_type_);
        
    return EdgeT<Graph>(graph(),
                        this->edge_type_,
                        edge_params.first,
                        edge_params.second,
                        from.node_type, from.handle,
                        to.node_type, to.handle);
}

template<class Graph>
void
BipartiteEdgeSchemaT<Graph>::
check_initialized() const
{
    check_initialized_impl(graph, "BipartiteEdgeSchemaT");
}


/*****************************************************************************/
/* UNIPARTITEEDGESCHEMAT                                                     */
/*****************************************************************************/

template<class Graph>
UnipartiteEdgeSchemaT<Graph>::
UnipartiteEdgeSchemaT(Graph & graph, const std::string & name,
                      const NodeSchemaT<Graph> & node_schema,
                      EdgeBehavior behavior)
    : SchemaT<Graph>(graph), node_schema_(&node_schema)
{
    edge_type_ = graph.addEdgeType(name, behavior);
}

template<class Graph>
EdgeT<Graph>
UnipartiteEdgeSchemaT<Graph>::
operator () (const NodeT<Graph> & from,
             const NodeT<Graph> & to) const
{
    if (from.graph != this->graph() || to.graph != this->graph())
        throw Exception("attempt to create edge between graphs");

    std::pair<int, EdgeDirection> edge_params
        = graph()->getOrCreateEdge(from.node_type, from.handle,
                                   to.node_type, to.handle,
                                   this->edge_type_);
    
    return EdgeT<Graph>(graph(),
                        this->edge_type_,
                        edge_params.first,
                        edge_params.second,
                        from.node_type, from.handle,
                        to.node_type, to.handle);
}

template<class Graph>
void
UnipartiteEdgeSchemaT<Graph>::
check_initialized() const
{
    check_initialized_impl(graph, "UnipartiteEdgeSchemaT");
}


/*****************************************************************************/
/* ATTRIBUTESCHEMA                                                           */
/*****************************************************************************/

template<class Payload, class Traits>
template<class Graph>
AttributeSchema<Payload, Traits>::
AttributeSchema(const std::string & name,
                const NodeSchemaT<Graph> & node_schema)
{
    std::pair<int, AttributeTraits *> result
        = node_schema.graph()->addNodeAttributeType(name,
                                                    node_schema.node_type(),
                                                    make_sp(new Traits()));

    attr_handle = result.first;
    traits = dynamic_cast<Traits *>(result.second);
}

template<class Payload, class Traits>
template<class Graph>
AttributeSchema<Payload, Traits>::
AttributeSchema(const std::string & name,
                const EdgeSchemaT<Graph> & edge_schema)
{
    std::pair<int, AttributeTraits *> result
        = edge_schema.graph->addEdgeAttributeType(name,
                                                  edge_schema.edge_type(),
                                                  make_sp(new Traits()));
    attr_handle = result.first;
    traits = dynamic_cast<Traits *>(result.second);
}

template<class Payload, class Traits>
AttributeRef
AttributeSchema<Payload, Traits>::
operator () (const Payload & val) const
{
    return traits->encode(val);
}


template<class Payload, class Traits>
template<typename Other>
AttributeRef
AttributeSchema<Payload, Traits>::
operator () (const Other & other) const
{
    return traits->encode(other);
}


/*****************************************************************************/
/* NODEATTRIBUTESCHEMA                                                       */
/*****************************************************************************/

template<class Graph, class Payload, class Traits>
NodeAttributeSchema<Graph, Payload, Traits>::
NodeAttributeSchema(const std::string & name,
                    const NodeSchemaT<Graph> & node_schema,
                    Uniqueness unique)
    : AttributeSchema<Payload, Traits>(name, node_schema),
      node_schema(node_schema), unique(unique)
{
}

template<class Graph, class Payload, class Traits>
AttributeRef
NodeAttributeSchema<Graph, Payload, Traits>::
operator () (const NodeT<Graph> & node,
             const Payload & val) const
{
    AttributeRef attr = traits->encode(val);
    if (unique == UNIQUE)
        node.graph->setOrReplaceNodeAttr(node_schema.node_type(), node.handle, attr);
    else node.graph->setNodeAttr(node_schema.node_type(), node.handle, attr);
    return attr;
}

template<class Graph, class Payload, class Traits>
template<typename Other>
AttributeRef
NodeAttributeSchema<Graph, Payload, Traits>::
operator () (const NodeT<Graph> & node,
             const Other & val) const
{
    AttributeRef attr = traits->encode(val);
    if (unique == UNIQUE)
        node.graph->setOrReplaceNodeAttr(node_schema.node_type(), node.handle, attr);
    else node.graph->setNodeAttr(node_schema.node_type(), node.handle, attr);
    return attr;
}


/*****************************************************************************/
/* EDGEATTRIBUTESCHEMA                                                       */
/*****************************************************************************/

template<class Graph, class Payload, class Traits>
EdgeAttributeSchema<Graph, Payload, Traits>::
EdgeAttributeSchema(const std::string & name,
                    const EdgeSchemaT<Graph> & edge_schema)
    : AttributeSchema<Payload, Traits>(name, edge_schema)
{
}


/*****************************************************************************/
/* NODESCHEMA1KEYT                                                           */
/*****************************************************************************/

template<class Graph, typename Key1, class Traits1>
NodeSchema1KeyT<Graph, Key1, Traits1>::
NodeSchema1KeyT(Graph & graph,
                const std::string & node_name,
                const std::string & key1_name)
    : NodeSchemaT<Graph>(graph, node_name),
      attr1(key1_name, *this)
{
}

template<class Graph, typename Key1, class Traits1>
template<class Value>
NodeT<Graph>
NodeSchema1KeyT<Graph, Key1, Traits1>::
operator () (const Value & key1) const
{
    return NodeSchemaT<Graph>::operator () (attr1(key1));
}



} // namespace JGraph

#endif /* __jgraph__jgraph_inline_h__ */

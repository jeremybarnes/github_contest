/* jgraph_inline.h                                                 -*- C++ -*-
   Jeremy Barnes, 15 September, 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   Inline functions for the jgraph class.
*/

#ifndef __jgraph__jgraph_inline_h__
#define __jgraph__jgraph_inline_h__


#include "compiler/compiler.h"


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
    : graph(0), handle(-1)
{
}

template<class Graph>
void
NodeT<Graph>::
setAttr(const Attribute & attr)
{
    check_initialized();
    graph->setNodeAttr(handle, attr);
}

template<class Graph>
void
NodeT<Graph>::
check_initialized() const
{
    check_initialized_impl(graph, "NodeT");
}


/*****************************************************************************/
/* EDGET                                                                     */
/*****************************************************************************/

template<class Graph>
EdgeT<Graph>::
EdgeT()
    : graph(0), handle(-1)
{
}


/*****************************************************************************/
/* SCHEMAT                                                                   */
/*****************************************************************************/

template<class Graph>
SchemaT<Graph>::
SchemaT()
    : graph(0), handle(0), object_type(0)
{
}

template<class Graph>
SchemaT<Graph>::
SchemaT(Graph & graph, ObjectType type)
    : graph(&graph), handle(0), object_type(type)
{
}


/*****************************************************************************/
/* NODESCHEMAT                                                               */
/*****************************************************************************/

template<class Graph>
NodeSchemaT<Graph>::
NodeSchemaT(Graph & graph, const std::string & name)
    : SchemaT<Graph>(graph, OT_NODE)
{
    this->handle = graph.addNodeType(name);
}

template<class Graph>
NodeT<Graph>
NodeSchemaT<Graph>::
operator () () const
{
    return NodeT<Graph>(graph, graph->createNode(handle));
}

template<class Graph>
NodeT<Graph>
NodeSchemaT<Graph>::
operator () (const Attribute & attr1) const
{
    return NodeT<Graph>(graph, graph->getOrCreateNode(handle, attr1));
}

template<class Graph>
void
NodeSchemaT<Graph>::
check_initialized() const
{
    check_initialized_impl(this->graph, "NodeSchemaT");
}


/*****************************************************************************/
/* EDGESCHEMAT                                                               */
/*****************************************************************************/

template<class Graph>
EdgeSchemaT<Graph>::
EdgeSchemaT(Graph & graph, const std::string & name,
            EdgeBehavior behavior)
    : SchemaT<Graph>(graph, OT_EDGE)
{
    handle = graph.addEdgeType(name, behavior);
}

template<class Graph>
EdgeT<Graph>
EdgeSchemaT<Graph>::
operator () (const NodeT<Graph> & from,
             const NodeT<Graph> & to) const
{
    if (from.graph != this->graph || to.graph != this->graph)
        throw Exception("attempt to create edge between graphs");
    return EdgeT<Graph>(graph,
                        graph->getOrCreateEdge(from.handle, to.handle,
                                               this->handle));
}

template<class Graph>
void
EdgeSchemaT<Graph>::
check_initialized() const
{
    check_initialized_impl(graph, "EdgeSchemaT");
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
    attr_handle = node_schema.graph->addAttributeNode(name, node_schema.handle);
}

template<class Payload, class Traits>
template<class Graph>
AttributeSchema<Payload, Traits>::
AttributeSchema(const std::string & name,
                const EdgeSchemaT<Graph> & edge_schema)
{
    attr_handle = edge_schema.graph->addAttributeEdge(name, edge_schema.handle);
}

template<class Payload, class Traits>
AttributeRef
AttributeSchema<Payload, Traits>::
operator () (const Payload & val) const
{
    return traits.encode(val);
}


template<class Payload, class Traits>
template<typename Other>
AttributeRef
AttributeSchema<Payload, Traits>::
operator () (const Other & other) const
{
    return traits.encode(other);
}

} // namespace JGraph

#endif /* __jgraph__jgraph_inline_h__ */

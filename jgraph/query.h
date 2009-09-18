/* query.h                                                         -*- C++ -*-
   Jeremy Barnes, 17 September 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   Query object over a graph.
*/

#ifndef __jgraph__query_h__
#define __jgraph__query_h__

#include "jgraph.h"

namespace JGraph {

template<class Result>
struct QueryResultSet {
    Result unique() const
    {
        return Result();
    }
};

template<class Graph, typename Payload, class Traits>
struct AttributeEqualityQuery {
    typedef QueryResultSet<NodeT<Graph> > ResultType;

    AttributeEqualityQuery(const Attribute & attr,
                           int node_type,
                           Graph * graph)
        : attr(attr), node_type(node_type), graph(graph)
    {
    }

    ResultType execute(const NodeSchemaT<Graph> & node) const
    {
        ResultType result;
        return result;
    }

    AttributeRef attr;
    int node_type;
    Graph * graph;
};

template<class Graph, typename Payload, class Traits, typename Value>
AttributeEqualityQuery<Graph, Payload, Traits>
operator == (const NodeAttributeSchema<Graph, Payload, Traits> & node_attr,
             const Value & val)
{
    return AttributeEqualityQuery<Graph, Payload, Traits>
        (node_attr(val), node_attr.node_type(), node_attr.graph());
}


} // namespace JGraph

#endif /* __jgraph__query_h__ */

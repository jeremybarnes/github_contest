/* query.h                                                         -*- C++ -*-
   Jeremy Barnes, 17 September 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   Query object over a graph.
*/

#ifndef __jgraph__query_h__
#define __jgraph__query_h__

#include "jgraph.h"

namespace JGraph {

template<class Underlying>
struct UniqueQueryResult {
    typedef typename Underlying::ResultType::ResultType ResultType; 

    UniqueQueryResult(const Underlying & underlying)
        : underlying(underlying)
    {
    }

    Underlying underlying;

    operator ResultType() const
    {
        // Here, we execute the query and return the result

        typename Underlying::ResultType executed = underlying.execute();

        if (!executed) return ResultType();

        ResultType result = executed.curr();

        if (executed.next()) {

            throw ML::Exception("more than one result but unique() used");
        }

        return result;
    }
};


template<class Underlying>
UniqueQueryResult<Underlying>
unique(const Underlying & underlying)
{
    return UniqueQueryResult<Underlying>(underlying);
}

template<class Graph>
struct NodeQueryResult {
    typedef NodeT<Graph> ResultType;
};

template<class Graph, typename Payload, class Traits>
struct NodeAttributeEqualityPredicate {

    NodeAttributeEqualityPredicate(const Attribute & attr,
                                   int node_type,
                                   Graph * graph)
        : attr(attr), node_type(node_type), graph(graph)
    {
    }
    
    // Filter (when filtering already generated values)
    bool operator () (const NodeT<Graph> & node) const
    {
        return node.hasAttrValue(attr);
    }

    typename Graph::CoherentNodeSetGenerator execute() const
    {
        return graph->nodesMatchingAttr(node_type, attr);
    }

    AttributeRef attr;
    int node_type;
    Graph * graph;
};

template<class Graph, class Predicate>
struct SelectNodes {
    typedef typename Graph::CoherentNodeSetGenerator ResultType;
    SelectNodes(const Predicate & predicate)
        : predicate(predicate)
    {
    }

    Predicate predicate;
    
    ResultType execute() const
    {
        return predicate.execute();
    }
};


template<class Graph, typename Payload, class Traits, typename Value>
NodeAttributeEqualityPredicate<Graph, Payload, Traits>
operator == (const NodeAttributeSchema<Graph, Payload, Traits> & node_attr,
             const Value & val)
{
    return NodeAttributeEqualityPredicate<Graph, Payload, Traits>
        (node_attr(val), node_attr.node_type(), node_attr.graph());
}



} // namespace JGraph

#endif /* __jgraph__query_h__ */

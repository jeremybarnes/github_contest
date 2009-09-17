/* jgraph.h                                                        -*- C++ -*-
    Jeremy Barnes, 14 September 2009
    Copyright (c) 2009 Jeremy Barnes.  All rights reserved.
*/

#ifndef __jgraph__jgraph_h__
#define __jgraph__jgraph_h__

#include "jgraph_fwd.h"

#include <stdint.h>
#include <string>
#include "arch/exception.h"
#include "attribute.h"
#include "attribute_traits.h"

namespace JGraph {

// Basic data structures


/*****************************************************************************/
/* NODET                                                                     */
/*****************************************************************************/

// Lightweight handle to access and manipulate a node
template<class Graph>
struct NodeT {
    NodeT();
    NodeT(Graph * graph, int node_type, int handle);

    /// Set the given attribute
    void setAttr(const Attribute & value);

    /// Debugging code to make sure node is initialized before we call any
    /// functions on it.  Compiling with NDEBUG will make this check a NOP.
    void check_initialized() const;
    
    Graph * graph;
    typename Graph::NodeHandle handle;
    int node_type;
};

/*****************************************************************************/
/* EDGET                                                                     */
/*****************************************************************************/

// Lightweight handle to access and manipulate an edge
// Can be one-way or two-way
template<class Graph>
struct EdgeT {
    EdgeT();
    EdgeT(Graph * graph, int edge_type, int handle);

    Graph * graph;
    typename Graph::EdgeHandle handle;
    int edge_type;
};


/*****************************************************************************/
/* SCHEMAT                                                                   */
/*****************************************************************************/

template<class Graph>
struct SchemaT {
    SchemaT();
    SchemaT(Graph & graph, ObjectType type);

protected:
    Graph * graph;
    int handle;
    ObjectType object_type;
};


/*****************************************************************************/
/* NODESCHEMAT                                                               */
/*****************************************************************************/

template<class Graph>
struct NodeSchemaT : public SchemaT<Graph> {
    NodeSchemaT(Graph & graph, const std::string & name);

    // Factory for nodes
    NodeT<Graph> operator () () const;

    NodeT<Graph> operator () (const Attribute & attr1) const;

private:
    /// Debugging code to make sure node is initialized before we call any
    /// functions on it.  Compiling with NDEBUG will make this check a NOP.
    void check_initialized() const;

    using SchemaT<Graph>::graph;
    using SchemaT<Graph>::handle;
    using SchemaT<Graph>::object_type;
};


/*****************************************************************************/
/* EDGESCHEMAT                                                               */
/*****************************************************************************/

template<class Graph>
struct EdgeSchemaT : public SchemaT<Graph> {
    EdgeSchemaT(Graph & graph, const std::string & name,
               EdgeBehavior behavior = ED_DOUBLE);
    
    // Create an edge
    EdgeT<Graph> operator () (const NodeT<Graph> & from,
                              const NodeT<Graph> & to) const;

private:
    /// Debugging code to make sure node is initialized before we call any
    /// functions on it.  Compiling with NDEBUG will make this check a NOP.
    void check_initialized() const;

    using SchemaT<Graph>::graph;
    using SchemaT<Graph>::handle;
    using SchemaT<Graph>::object_type;
};

template<typename Payload, class Graph>
struct DefaultGraphAttributeTraits {
    typedef typename DefaultAttributeTraits<Payload>::Type Type;
};


/*****************************************************************************/
/* ATTRIBUTESCHEMA                                                           */
/*****************************************************************************/

template<typename Payload,
         class Traits = typename DefaultAttributeTraits<Payload>::Type>
struct AttributeSchema {
    template<class Graph>
    AttributeSchema(const std::string & name,
                    const NodeSchemaT<Graph> & node);

    template<class Graph>
    AttributeSchema(const std::string & name,
                    const EdgeSchemaT<Graph> & edge);

    AttributeRef operator () (const Payload & val) const;

    template<typename Other>
    AttributeRef operator () (const Other & other) const;
    
    int attr_handle;
    const Traits * traits;
};


/*****************************************************************************/
/* NODEATTRIBUTESCHEMA                                                       */
/*****************************************************************************/

// Now for the node and edge specific ones
template<class Graph, class Payload,
         class Traits = typename DefaultGraphAttributeTraits<Payload, Graph>::Type>
struct NodeAttributeSchema
    : AttributeSchema<Payload, Traits> {
    NodeAttributeSchema(const std::string & name,
                        const NodeSchemaT<Graph> & node_schema);

    using AttributeSchema<Payload>::operator ();
    using AttributeSchema<Payload>::traits;

    AttributeRef operator () (const NodeT<Graph> & node,
                              const Payload & val) const;

    template<typename Other>
    AttributeRef operator () (const NodeT<Graph> & node,
                              const Other & val) const;

    const NodeSchemaT<Graph> & node_schema;
};


/*****************************************************************************/
/* EDGEATTRIBUTESCHEMA                                                       */
/*****************************************************************************/

template<class Graph, class Payload,
         class Traits = typename DefaultGraphAttributeTraits<Payload, Graph>::type>
struct EdgeAttributeSchema
    : AttributeSchema<Payload, Traits> {
    EdgeAttributeSchema(const std::string & name,
                        const EdgeSchemaT<Graph> & edge_schema);

    using AttributeSchema<Payload>::operator ();
    using AttributeSchema<Payload>::traits;
};


} // namespace JGraph

#include "jgraph_inline.h"

#endif /* __jgraph__jgraph_h__ */

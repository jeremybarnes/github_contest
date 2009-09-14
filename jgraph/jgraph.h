/** jgraph.h                                                        -*- C++ -*-
    Jeremy Barnes, 14 September 2009
    Copyright (c) 2009 Jeremy Barnes.  All rights reserved.
*/

#ifndef __jgraph__jgraph_h__
#define __jgraph__jgraph_h__

namespace JGDS {

// Basic data structures


class Graph;
class StringMap;

// Lightweight handle to access an attribute and its value.  Opaque type.
struct Attribute {
private:
    Graph * graph;
    int type;
    union {
        void * ptr;  // type dependent; opaque
        uint64_t val;
        float f;
        double d;
    };
};


// Lightweight handle to access and manipulate a node
struct Node {
    Graph * graph;
    int handle;

    void setAttr(const Attribute & attr);
};

// Lightweight handle to access and manipulate an edge
// Can be one-way or two-way
struct Edge {
    Graph * graph;
    int handle;
};


// Atomic string, etc
struct Atom {
    StringMap * stringmap;
    int handle;
};

// Date to be put in the graph
struct Date {
    double val;
};

struct Schema {
private:
    Graph * graph;
    int handle;
    int object_type;
};

struct NodeSchema : public Schema {
    NodeSchema(Graph & graph, const std::string & name);

    // Factory for nodes
    Node operator () () const;

    Node operator () (const Attribute & attr1) const;
};

enum EdgeBehavior {
    ED_SINGLE,     ///< Directional; only the thing at one end knows about it
    ED_DOUBLE,     ///< Directional; both ends know about it
    ED_SYMMETRIC   ///< Non-directional; both ends know about it
};

struct EdgeSchema : public Schema {
    EdgeSchema(Graph & graph, const std::string & name,
               EdgeBehavior behavior = ED_DOUBLE);
    
    // Create an edge
    Edge operator () (const Node & from, const Node & to) const;
};

template<class Payload>
struct AttributeSchema {
    AttributeSchema(const std::string & name,
                    const NodeSchema & node);
    AttributeSchema(const std::string & name,
                    const EdgeSchema & edge);

    Attribute operator () (const Payload & val) const;

    template<typename Other>
    Attribute operator () (const Other & other) const;
};

template<class Key, class Value>
struct Index {
};

struct Graph {
    /// Create the given graph with the given name
    Graph(const std::string & name);

    int handle;
};


} // namespace JGDS


#endif /* __jgraph__jgraph_h__ */

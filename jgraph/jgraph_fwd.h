/* jgraph_fwd.h                                                    -*- C++ -*-
   Jeremy Barnes, 15 September 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   Forward declarations for the jgraph structures.
*/

#ifndef __jgraph__jgraph_fwd_h__
#define __jgraph__jgraph_fwd_h__


#include <stdint.h>
#include "compiler/compiler.h"

namespace JGraph {

class StringMap;
class Attribute;
class AttributeRef;
template<class Graph> struct NodeT;
template<class Graph> struct EdgeT;
struct Atom;
struct Date;

template<class Graph> struct SchemaT;
template<class Graph> struct NodeSchemaT;
template<class Graph> struct EdgeSchemaT;

template<typename Payload, class Traits> struct AttributeSchema;

class AttributeTraits;

template<typename Payload>
struct DefaultAttributeTraits;


enum ObjectType {
    OT_NODE,
    OT_EDGE
};

enum EdgeBehavior {
    EB_SINGLE,     ///< Directional; only the thing at one end knows about it
    EB_DOUBLE,     ///< Directional; both ends know about it
    EB_SYMMETRIC   ///< Non-directional; both ends know about it
};

enum EdgeDirection {
    ED_FORWARDS,     ///< Edge goes forwards
    ED_BACKWARDS,    ///< Edge goes backwards
    ED_BIDIRECTIONAL ///< Edge goes forwards and backwards
};

inline EdgeDirection defaultDirection(EdgeBehavior behavior)
{
    if (behavior == EB_SINGLE || behavior == EB_DOUBLE) return ED_FORWARDS;
    return ED_BIDIRECTIONAL;
}

inline bool targetNodeKnowsEdge(EdgeBehavior behavior)
{
    return behavior != EB_SINGLE;
}

void throw_bad_edge_direction(const EdgeDirection & ed) JML_NORETURN;

inline
EdgeDirection operator ! (const EdgeDirection & ed)
{
    switch (ed) {
    case ED_FORWARDS: return ED_BACKWARDS;
    case ED_BACKWARDS: return ED_FORWARDS;
    case ED_BIDIRECTIONAL: return ED_BIDIRECTIONAL;
    }
    throw_bad_edge_direction(ed);
}

#if 0
void throw_bad_edge_direction_comparison(const EdgeDirection & ed1,
                                         const EdgeDirection & ed2)
    JML_NORETURN;

inline bool operator < (const EdgeDirection & e1, const EdgeDirection & e2)
{
    if (e1 == ED_BIDIRECTIONAL || e2 == ED_BIDIRECTIONAL) {
        if (e1 != e2)
            throw_bad_edge_direction_comparison(e1, e2);
        return false;
    }
    if (e1 == ED_FORWARDS && e2 == ED_BACKWARDS) return true;
    return false;
}
#endif

// Flags for attributes
enum {
    AFL_REFCOUNTED    = 1 << 0,
    AFL_BINCOMPARABLE = 1 << 1,
    AFL_BINSTABLE     = 1 << 2,
    AFL_BINHASHABLE   = 1 << 3
};

// A naked attribute value.  Contains no reference or type information.  This
// is the most basic bit of information stored.  No comparison functions, etc.
typedef uint64_t AttributeValue;


template<class Graph, class Predicate> struct SelectNodes;



struct BasicGraph;

} // namespace JGraph

#endif /* __jgraph__jgraph_fwd_h__ */

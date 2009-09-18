/* jgraph_fwd.h                                                    -*- C++ -*-
   Jeremy Barnes, 15 September 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   Forward declarations for the jgraph structures.
*/

#ifndef __jgraph__jgraph_fwd_h__
#define __jgraph__jgraph_fwd_h__


#include <stdint.h>


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
    ED_SINGLE,     ///< Directional; only the thing at one end knows about it
    ED_DOUBLE,     ///< Directional; both ends know about it
    ED_SYMMETRIC   ///< Non-directional; both ends know about it
};

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

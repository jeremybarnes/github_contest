/* jgraph.cc
   Jeremy Barnes, 14 September 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   Jeremy's Graph Data Structure.
*/

#include "jgraph.h"
#include "arch/exception.h"
#include "utils/string_functions.h"


using namespace ML;

namespace JGraph {

std::string print(ObjectType ot)
{
    switch (ot) {
    case OT_NODE: return "NODE";
    case OT_EDGE: return "EDGE";
    default: return format("ObjectType(%d)", ot);
    }
}

std::ostream & operator << (std::ostream & stream, ObjectType ot)
{
    return stream << print(ot);
}

std::string print(EdgeBehavior eb)
{
    switch (eb) {
    case EB_SINGLE: return "SINGLE";
    case EB_DOUBLE: return "DOUBLE";
    case EB_SYMMETRIC: return "SYMMETRIC";
    default: return format("EdgeBehavior(%d)", eb);
    }
}

std::ostream & operator << (std::ostream & stream, EdgeBehavior eb)
{
    return stream << print(eb);
}

std::string print(EdgeDirection ed)
{
    switch (ed) {
    case ED_FORWARDS: return "FORWARDS";
    case ED_BACKWARDS: return "BACKWARDS";
    case ED_BIDIRECTIONAL: return "BIDIRECTIONAL";
    default: return format("EdgeDirection(%d)", ed);
    }
}

std::string printPreposition(EdgeDirection ed)
{
    switch (ed) {
    case ED_FORWARDS: return "to";
    case ED_BACKWARDS: return "from";
    case ED_BIDIRECTIONAL: return "with";
    default: return format("EdgeDirection(%d)", ed);
    }
}

std::ostream & operator << (std::ostream & stream, EdgeDirection ed)
{
    return stream << print(ed);
}

void throw_bad_edge_direction(const EdgeDirection & ed)
{
    throw Exception("bad edge direction");
}



} // namespace JGDS

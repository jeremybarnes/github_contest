/* jgraph.cc
   Jeremy Barnes, 14 September 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   Jeremy's Graph Data Structure.
*/

#include "jgraph.h"
#include "arch/exception.h"

using namespace ML;

namespace JGraph {

void throw_bad_edge_direction(const EdgeDirection & ed)
{
    throw Exception("bad edge direction");
}



} // namespace JGDS

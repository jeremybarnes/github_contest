/* string_map.h                                                    -*- C++ -*-
   Jeremy Barnes, 16 September 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   String map class.
*/

#ifndef __jgraph__string_map_h__
#define __jgraph__string_map_h__

#include "utils/hash_map.h"

namespace JGraph {

struct StringMap {

    int operator [] (const std::string & s);
    std::string operator [] (int i);

    std::hash_map<std::string, int> string_to_int;
    std::vector<std::string> int_to_string;
};

} // namespace JGraph

#endif /* __jgraph__string_map_h__ */

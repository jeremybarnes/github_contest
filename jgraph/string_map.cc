/* string_map.cc
   Jeremy Barnes, 16 September 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   String map class.
*/

#include "string_map.h"
#include "arch/exception.h"


using namespace std;
using namespace ML;


namespace JGraph {

int
StringMap::
operator [] (const std::string & s)
{
    hash_map<string, int>::const_iterator it
        = string_to_int.find(s);
    if (it == string_to_int.end()) {
        string_to_int[s] = int_to_string.size();
        int_to_string.push_back(s);
        return int_to_string.size() - 1;
    }
    return it->second;
}

std::string
StringMap::
operator [] (int i)
{
    if (i < 0 || i >= int_to_string.size())
        throw Exception("StringMap::operator []: invalid id");
    return int_to_string[i];
}

} // namespace JGraph

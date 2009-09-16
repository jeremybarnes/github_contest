/* attribute_basic_types.cc                                        -*- C++ -*-
   Jeremy Barnes, 16 September 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   Basic attribute types.
*/

#include "attribute_basic_types.h"
#include "utils/string_functions.h"
#include "attribute.h"


using namespace ML;


namespace JGraph {

/*****************************************************************************/
/* INTTRAITS                                                                 */
/*****************************************************************************/

IntTraits::~IntTraits()
{
}

std::string
IntTraits::
print(const Attribute & attr) const
{
    return format("%zd", getValue(attr));
}


} // namespace JGraph

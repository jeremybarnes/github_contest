/* attribute_basic_types.cc                                        -*- C++ -*-
   Jeremy Barnes, 16 September 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   Basic attribute types.
*/

#include "attribute_basic_types.h"
#include "utils/string_functions.h"
#include "attribute.h"
#include "utils/hash_specializations.h"


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


/*****************************************************************************/
/* STRINGTRAITS                                                              */
/*****************************************************************************/

StringTraits::
~StringTraits()
{
}

size_t
StringTraits::
hash(const Attribute & a) const
{
    return JML_HASH_NS::hash<const char *>()(getObject(a).c_str());
}

size_t
StringTraits::
stableHash(const Attribute & a) const
{
    return JML_HASH_NS::hash<const char *>()(getObject(a).c_str());
}

std::string
StringTraits::
print(const Attribute & a) const
{
    return getObject(a);
}

} // namespace JGraph

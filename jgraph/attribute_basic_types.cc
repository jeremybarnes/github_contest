/* attribute_basic_types.cc                                        -*- C++ -*-
   Jeremy Barnes, 16 September 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   Basic attribute types.
*/

#include "attribute_basic_types.h"
#include "utils/string_functions.h"
#include "attribute.h"
#include "utils/hash_specializations.h"
#include "utils/less.h"


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


/*****************************************************************************/
/* ATOMTRAITS                                                                */
/*****************************************************************************/

AtomTraits::
~AtomTraits()
{
}

AttributeRef
AtomTraits::
encode(const std::string & val) const
{
    return createScalarAttribute(string_map[val], FLAGS);
}

AttributeRef
AtomTraits::
encode(const Atom & atom) const
{
    return createScalarAttribute(atom.handle, FLAGS);
}

bool
AtomTraits::
equal(const Attribute & a1, const Attribute & a2) const
{
    return getValue(a1) == getValue(a2);
}

int
AtomTraits::
less(const Attribute & a1, const Attribute & a2) const
{
    return getValue(a1) < getValue(a2);
}

bool
AtomTraits::
stableLess(const Attribute & a1, const Attribute & a2) const
{
    return print(a1) < print(a2);
}

int
AtomTraits::
compare(const Attribute & a1, const Attribute & a2) const
{
    return compare_3way(getValue(a1), getValue(a2));
}

int
AtomTraits::
stableCompare(const Attribute & a1, const Attribute & a2) const
{
    return compare_3way(print(a1), print(a2));
}

size_t
AtomTraits::
hash(const Attribute & a) const
{
    return getValue(a);
}

size_t
AtomTraits::
stableHash(const Attribute & a) const
{
    return std::hash<std::string>()(print(a));
}

std::string
AtomTraits::
print(const Attribute & attr) const
{
    return string_map[getValue(attr)];
}

} // namespace JGraph

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
#include <boost/date_time/gregorian/gregorian.hpp>


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
/* BOOLTRAITS                                                                */
/*****************************************************************************/

BoolTraits::~BoolTraits()
{
}

std::string
BoolTraits::
print(const Attribute & attr) const
{
    return getValue(attr) ? "true" : "false";
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

/*****************************************************************************/
/* DATETRAITS                                                                */
/*****************************************************************************/

AttributeRef
DateTraits::
encode(const Date & val) const
{
    return createScalarAttribute(reinterpret_as_int(val.seconds), FLAGS);
}

size_t
DateTraits::
hash(const Attribute & a) const
{
    // TODO: do better
    return JML_HASH_NS::hash<const char *>()(print(a).c_str());
}

size_t
DateTraits::
stableHash(const Attribute & a) const
{
    // TODO: do better
    return JML_HASH_NS::hash<const char *>()(print(a).c_str());
}

std::string
DateTraits::
print(const Attribute & attr) const
{
    double d = reinterpret_as_double(getValue(attr));
    return Date(d).print();
}


/*****************************************************************************/
/* DATE                                                                      */
/*****************************************************************************/


static const boost::gregorian::date epoch(2007, 1, 1);

Date::
Date(double seconds)
    : seconds(seconds)
{
}

Date::
Date(const std::string & str)
{
    boost::gregorian::date date
        = boost::gregorian::from_simple_string(str);
    
    seconds = (date - epoch).days();
}

std::string
Date::
print() const
{
    boost::gregorian::date date
        = epoch + boost::gregorian::date_duration(seconds);
    return to_simple_string(date);
}

std::ostream &
operator << (std::ostream & stream, const Date & date)
{
    return stream << date.print();
}

} // namespace JGraph

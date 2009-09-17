/* attribute_traits.cc                                             -*- C++ -*-
   Jeremy Barnes, 16 September 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   Implementation of basic attribute traits functions.
*/

#include "attribute_traits.h"
#include "attribute.h"
#include "arch/exception.h"


using namespace ML;
using namespace std;


namespace JGraph {


/*****************************************************************************/
/* ATTRIBUTETRAITS                                                           */
/*****************************************************************************/

AttributeTraits::
AttributeTraits(int refCountOffset)
    : refCountOffset(refCountOffset)
{
}

AttributeTraits::~AttributeTraits()
{
}

bool
AttributeTraits::
equal(const Attribute & a1, const Attribute & a2) const
{
    throw Exception("traits should have overridden method "
                    "or set flags differently");
}

int
AttributeTraits::
less(const Attribute & a1, const Attribute & a2) const
{
    throw Exception("traits should have overridden method "
                    "or set flags differently");
}

bool
AttributeTraits::
stableLess(const Attribute & a1, const Attribute & a2) const
{
    throw Exception("traits should have overridden method "
                    "or set flags differently");
}

int
AttributeTraits::
compare(const Attribute & a1, const Attribute & a2) const
{
    throw Exception("traits should have overridden method "
                    "or set flags differently");
}

int
AttributeTraits::
stableCompare(const Attribute & a1, const Attribute & a2) const
{
    throw Exception("traits should have overridden method "
                    "or set flags differently");
}

size_t
AttributeTraits::
hash(const Attribute & a) const
{
    throw Exception("traits should have overridden method "
                    "or set flags differently");
}

size_t
AttributeTraits::
stableHash(const Attribute & a) const
{
    throw Exception("traits should have overridden method "
                    "or set flags differently");
}

void
AttributeTraits::
deleteObject(const Attribute & a) const
{
    throw Exception("traits should have overridden method "
                    "or set flags differently");
}


/*****************************************************************************/
/* SCALARATTRIBUTETRAITS                                                     */
/*****************************************************************************/

ScalarAttributeTraits::~ScalarAttributeTraits()
{
}

bool
ScalarAttributeTraits::
equal(const Attribute & a1, const Attribute & a2) const
{
    return getValue(a1) == getValue(a2);
}

int
ScalarAttributeTraits::
less(const Attribute & a1, const Attribute & a2) const
{
    return getValue(a1) < getValue(a2);
}

bool
ScalarAttributeTraits::
stableLess(const Attribute & a1, const Attribute & a2) const
{
    return getValue(a1) < getValue(a2);
}

int
ScalarAttributeTraits::
compare(const Attribute & a1, const Attribute & a2) const
{
    return (getValue(a1) < getValue(a2)
            ? -1
            : (getValue(a1) == getValue(a2) ? 0 : 1));
}

int
ScalarAttributeTraits::
stableCompare(const Attribute & a1, const Attribute & a2) const
{
    return (getValue(a1) < getValue(a2)
            ? -1
            : (getValue(a1) == getValue(a2) ? 0 : 1));
}

size_t
ScalarAttributeTraits::
hash(const Attribute & a) const
{
    return getValue(a);
}

size_t
ScalarAttributeTraits::
stableHash(const Attribute & a) const
{
    return getValue(a);
}


/*****************************************************************************/
/* NULLTRAITS                                                                */
/*****************************************************************************/

NullTraits::~NullTraits()
{
}

bool
NullTraits::
equal(const Attribute & a1, const Attribute & a2) const
{
    return true;
}

int
NullTraits::
less(const Attribute & a1, const Attribute & a2) const
{
    return false;
}

bool
NullTraits::
stableLess(const Attribute & a1, const Attribute & a2) const
{
    return false;
}

int
NullTraits::
compare(const Attribute & a1, const Attribute & a2) const
{
    return 0;
}

int
NullTraits::
stableCompare(const Attribute & a1, const Attribute & a2) const
{
    return 0;
}

size_t
NullTraits::
hash(const Attribute & a) const
{
    return 0;
}

size_t
NullTraits::
stableHash(const Attribute & a) const
{
    return 0;
}

std::string
NullTraits::
print(const Attribute & a) const
{
    return "<<<NULL>>>";
}

} // namespace JGraph

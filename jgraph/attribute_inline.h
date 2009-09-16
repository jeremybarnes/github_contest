/* attribute_inline.h                                              -*- C++ -*-
   Jeremy Barnes, 15 September 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   Inline functions for the attributes.
*/

#ifndef __jgraph__attribute_inline_h__
#define __jgraph__attribute_inline_h__

#include "attribute_traits.h"

namespace JGraph {


/*****************************************************************************/
/* ATTRIBUTE                                                                 */
/*****************************************************************************/

inline bool
Attribute::
operator == (const Attribute & other) const
{
    if (value == other.value) return true;
    if (bincomparable()) return false;
    return traits->equal(*this, other);
}

inline bool
Attribute::
operator != (const Attribute & other) const
{
    return ! operator == (other);
}

inline bool
Attribute::
operator < (const Attribute & other) const
{
    if (value == other.value) return false;
    if (bincomparable()) return value < other.value;
    return traits->less(*this, other);
}

inline int
Attribute::
compare(const Attribute & other) const
{
    if (value == other.value) return 0;
    if (bincomparable()) return (value < other.value ? -1 : 1);
    return traits->compare(*this, other);
}

inline bool
Attribute::
stableLess(const Attribute & other) const
{
    if (value == other.value) return false;
    if (binstable()) return operator < (other);
    return traits->stableLess(*this, other);
}

inline int
Attribute::
stableCompare(const Attribute & other) const
{
    if (value == other.value) return 0;
    if (binstable()) return compare(other);
    return traits->stableCompare(*this, other);
}

inline size_t
Attribute::
hash() const
{
    if (binhashable()) return value;
    return traits->hash(*this);
}

inline size_t
Attribute::
stableHash() const
{
    if (binstable()) return hash();
    return traits->stableHash(*this);
}

inline
Attribute::
Attribute(const Attribute & other)
    : value(other.value), traits(other.traits), flags(other.flags)
{
}

inline
Attribute::
Attribute(const AttributeTraits * traits,
          AttributeValue value,
          uint32_t flags)
    : value(value), traits(traits), flags(flags)
{
}

inline Attribute &
Attribute::
operator = (const Attribute & other)
{
    value = other.value;
    traits = other.traits;
    flags = other.flags;
    return *this;
}

inline void
Attribute::
swap(Attribute & other)
{
    std::swap(value, other.value);
    std::swap(traits, other.traits);
    std::swap(flags, other.flags);
}


/*****************************************************************************/
/* ATTRIBUTEREF                                                              */
/*****************************************************************************/

inline
AttributeRef::
AttributeRef(const AttributeTraits * traits,
             AttributeValue value,
             uint32_t flags)
    : Attribute(traits, value, flags)
{
}

} // namespace JGraph

#endif /* __jgraph__attribute_inline_h__ */

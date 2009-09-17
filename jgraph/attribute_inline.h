/* attribute_inline.h                                              -*- C++ -*-
   Jeremy Barnes, 15 September 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   Inline functions for the attributes.
*/

#ifndef __jgraph__attribute_inline_h__
#define __jgraph__attribute_inline_h__


#include "attribute_traits.h"
#include "arch/exception.h"


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

inline
Attribute::
Attribute(const AttributeTraits * traits,
          void * obj,
          uint32_t flags)
    : ptr(reinterpret_cast<char *>(obj)), traits(traits), flags(flags)
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

    // no references change...
}


/*****************************************************************************/
/* ATTRIBUTEREF                                                              */
/*****************************************************************************/

inline
AttributeRef::
AttributeRef()
{
}

inline
AttributeRef::
AttributeRef(const AttributeRef & other)
    : Attribute(other)
{
    if (refcounted()) incReferences();
}

inline
AttributeRef::
AttributeRef(const Attribute & other)
    : Attribute(other)
{
    if (refcounted()) incReferences();
}

inline
AttributeRef::
~AttributeRef()
{
    if (refcounted()) decReferences();
}

inline AttributeRef &
AttributeRef::
operator = (const Attribute & other)
{
    AttributeRef new_me(other);
    swap(new_me);
    return *this;
}

inline void
AttributeRef::
swap(AttributeRef & other)
{
    Attribute::swap(other);
}

inline
AttributeRef::
AttributeRef(const AttributeTraits * traits,
             AttributeValue value,
             uint32_t flags)
    : Attribute(traits, value, flags)
{
}

inline
AttributeRef::
AttributeRef(const AttributeTraits * traits,
             void * obj,
             uint32_t flags)
    : Attribute(traits, obj, flags)
{
    incReferences();
}

inline
int *
AttributeRef::
refcount() const
{
    if (!refcounted())
        throw ML::Exception("refcount called on non-reference counted "
                            "attribute");
    return reinterpret_cast<int *>
        (reinterpret_cast<char *>(ptr) + traits->refCountOffset);
}

inline
void
AttributeRef::
incReferences()
{
    __sync_add_and_fetch(refcount(), 1);
}

inline
void
AttributeRef::
decReferences()
{
    int newref = __sync_sub_and_fetch(refcount(), 1);
    if (JML_UNLIKELY(newref < 0))
        throw ML::Exception("reference counting error");
    if (newref == 0)
        traits->deleteObject(*this);
}

} // namespace JGraph

#endif /* __jgraph__attribute_inline_h__ */

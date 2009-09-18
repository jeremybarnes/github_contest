/* attribute_traits_inline.h                                       -*- C++ -*-
   Jeremy Barnes, 16 September, 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   Inline functions for attribute traits.
*/

#ifndef __jgraph__attribute_traits_inline_h__
#define __jgraph__attribute_traits_inline_h__

#include "attribute.h"
#include "compiler/compiler.h"
#include <memory>
#include <boost/static_assert.hpp>
#include "arch/exception.h"


namespace JGraph {


/*****************************************************************************/
/* ATTRIBUTETRAITS                                                           */
/*****************************************************************************/

JML_ALWAYS_INLINE AttributeValue
AttributeTraits::
getValue(const Attribute & attr)
{
    return attr.value;
}

template<typename As>
JML_ALWAYS_INLINE 
const As *
AttributeTraits::
getObject(const Attribute & attr)
{
    return reinterpret_cast<const As *>(attr.ptr);
}

JML_ALWAYS_INLINE AttributeRef
AttributeTraits::
createScalarAttribute(AttributeValue value,
                      uint32_t flags) const
{
    return AttributeRef(this, value, flags);
}

// TODO: catch when the refcount and the obj aren't adjacent and either fix
// or throw (depends upon the alignment of obj)
template<class Obj>
struct RefCountedObj {
    RefCountedObj(const Obj & obj)
        : obj(obj)
    {
        std::fill((unsigned *)&refcount, (unsigned *)(&this->obj), 0);
    }

    unsigned refcount;
    // TODO: sometimes, based upon alignment of subobjects, there is space
    // here.  We should arrange so that there isn't any.
    Obj obj;

    static int refCountOffset();
};

template<class Obj>
int
RefCountedObj<Obj>::
refCountOffset()
{
    const RefCountedObj<Obj> * rco = 0;
    return -reinterpret_cast<ssize_t>(&rco->obj);
}

template<typename Obj>
AttributeRef
AttributeTraits::
createRefCountedAttribute(const Obj & obj) const
{
    std::auto_ptr<RefCountedObj<Obj> > new_obj(new RefCountedObj<Obj>(obj));
    return AttributeRef(this, &new_obj.release()->obj, AFL_REFCOUNTED);
}

template<typename Obj>
void
AttributeTraits::
destroyRefCountedAttribute(const Attribute & a) const
{
    RefCountedObj<Obj> * ptr
        = reinterpret_cast<RefCountedObj<Obj> *>(a.ptr + refCountOffset);
    delete ptr;
}


/*****************************************************************************/
/* NULLTRAITS                                                                */
/*****************************************************************************/

inline AttributeRef
NullTraits::
encode() const
{
    return createScalarAttribute(0, FLAGS);
}


/*****************************************************************************/
/* REFCOUNTEDATTRIBUTETRAITS                                                 */
/*****************************************************************************/

template<typename Underlying>
RefCountedAttributeTraits<Underlying>::
RefCountedAttributeTraits()
    : AttributeTraits(RefCountedObj<Underlying>::refCountOffset())
{
}

template<typename Underlying>
RefCountedAttributeTraits<Underlying>::
~RefCountedAttributeTraits()
{
}

template<typename Underlying>
const Underlying &
RefCountedAttributeTraits<Underlying>::
getObject(const Attribute & attr) const
{
    return *AttributeTraits::getObject<Underlying>(attr);
}

template<typename Underlying>
AttributeRef
RefCountedAttributeTraits<Underlying>::
encode(const Underlying & val) const
{
    return createRefCountedAttribute(val);
}

template<typename Underlying>
bool
RefCountedAttributeTraits<Underlying>::
equal(const Attribute & a1, const Attribute & a2) const
{
    return getObject(a1) == getObject(a2);
}

template<typename Underlying>
int
RefCountedAttributeTraits<Underlying>::
less(const Attribute & a1, const Attribute & a2) const
{
    return getObject(a1) < getObject(a2);
}

template<typename Underlying>
bool
RefCountedAttributeTraits<Underlying>::
stableLess(const Attribute & a1, const Attribute & a2) const
{
    return less(a1, a2);
}

template<typename Underlying>
int
RefCountedAttributeTraits<Underlying>::
compare(const Attribute & a1, const Attribute & a2) const
{
    return (getObject(a1) < getObject(a2)
            ? -1
            : (getObject(a1) == getObject(a2) ? 0 : 1));
}

template<typename Underlying>
int
RefCountedAttributeTraits<Underlying>::
stableCompare(const Attribute & a1, const Attribute & a2) const
{
    return compare(a1, a2);
}

template<typename Underlying>
void
RefCountedAttributeTraits<Underlying>::
deleteObject(const Attribute & a) const
{
    destroyRefCountedAttribute<Underlying>(a);
}
    
} // namespace JGraph

#endif /* __jgraph__attribute_traits_inline_h__ */

   

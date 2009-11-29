/* attribute_traits.h                                              -*- C++ -*-
   Jeremy Barnes, 16 September 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   Attribute traits class.
*/

#ifndef __jgraph__attribute_traits_h__
#define __jgraph__attribute_traits_h__

#include "jgraph_fwd.h"
#include <string>

namespace JGraph {


/*****************************************************************************/
/* ATTRIBUTETRAITS                                                           */
/*****************************************************************************/

/** Abstract base class for the traits for an attribute.  Defines all of the
    functions that need to be implemented to treat it generically.
*/

struct AttributeTraits {
    AttributeTraits(int refCountOffset = 0, int type = -1,
                    const std::string & name = "");
    virtual ~AttributeTraits();

    /// Combine the two together, checking that the two objects are compatible
    /// and throwing an exception if not.
    virtual void combine(const AttributeTraits & other);

    virtual bool equal(const Attribute & a1, const Attribute & a2) const;
    virtual int less(const Attribute & a1, const Attribute & a2) const;
    virtual bool stableLess(const Attribute & a1, const Attribute & a2) const;
    virtual int compare(const Attribute & a1, const Attribute & a2) const;
    virtual int stableCompare(const Attribute & a1, const Attribute & a2) const;
    virtual size_t hash(const Attribute & a) const;
    virtual size_t stableHash(const Attribute & a) const;
    virtual std::string print(const Attribute & attr) const = 0;

    // For reference counted objects only
    virtual void deleteObject(const Attribute & a) const;

    virtual void setType(int type);
    int type() const { return type_; }

    virtual void setName(const std::string & name);
    const std::string & name() const { return name_; }

protected:
    // Allow descendents to access the value field of an attribute
    static AttributeValue getValue(const Attribute & attr);

    template<typename As>
    static const As * getObject(const Attribute & attr);

    AttributeRef createScalarAttribute(AttributeValue value,
                                       uint32_t flags) const;

    template<typename Obj>
    AttributeRef createRefCountedAttribute(const Obj & obj) const;

    template<typename Obj>
    void destroyRefCountedAttribute(const Attribute & attr) const;

    // Data member, giving the offset (in bytes) between the returned object
    // and the reference count.  It is used by the attributes to find their
    // reference count.  Can be used to implement, for example, an intrusive
    // reference count within an object.
    int refCountOffset;

    // Data member, giving the type of the attribute according to the
    // controlling class.
    int type_;

    // Data member, giving the name of the attribute according to the
    // controlling class.
    std::string name_;

    friend class Attribute;
    friend class AttributeRef;
};


/*****************************************************************************/
/* SCALARATTRIBUTETRAITS                                                     */
/*****************************************************************************/

/** Base class for scalar attributes.  These have a fixed bit pattern stored
    that is direcly associated with their value.
*/

struct ScalarAttributeTraits : public AttributeTraits {
    virtual ~ScalarAttributeTraits();

    virtual bool equal(const Attribute & a1, const Attribute & a2) const;
    virtual int less(const Attribute & a1, const Attribute & a2) const;
    virtual bool stableLess(const Attribute & a1, const Attribute & a2) const;
    virtual int compare(const Attribute & a1, const Attribute & a2) const;
    virtual int stableCompare(const Attribute & a1, const Attribute & a2) const;
    virtual size_t hash(const Attribute & a) const;
    virtual size_t stableHash(const Attribute & a) const;

    enum {
        FLAGS = AFL_BINCOMPARABLE | AFL_BINSTABLE | AFL_BINHASHABLE
    };
};


/*****************************************************************************/
/* REFCOUNTEDATTRIBUTETRAITS                                                 */
/*****************************************************************************/

/** Base class for reference counted attributes.  These attributes have another
    object stored somewhere that is pointed to by the Attribute object.  This
    other object has a reference count so that the attribute can be destroyed
    when it is no longer referenced.
*/

template<class Underlying>
struct RefCountedAttributeTraits : public AttributeTraits {
    RefCountedAttributeTraits();
    virtual ~RefCountedAttributeTraits();

    AttributeRef encode(const Underlying & val) const;
    const Underlying & decode(const Attribute & val) const
    {
        return getObject(val);
    }

    virtual bool equal(const Attribute & a1, const Attribute & a2) const;
    virtual int less(const Attribute & a1, const Attribute & a2) const;
    virtual bool stableLess(const Attribute & a1, const Attribute & a2) const;
    virtual int compare(const Attribute & a1, const Attribute & a2) const;
    virtual int stableCompare(const Attribute & a1, const Attribute & a2) const;

    virtual void deleteObject(const Attribute & a) const;

    const Underlying & getObject(const Attribute & attr) const;
};


/*****************************************************************************/
/* NULLTRAITS                                                                */
/*****************************************************************************/

/** The null attribute's traits.  Used to allow a default when creating
    attributes.
*/

struct NullTraits : public AttributeTraits {
    virtual ~NullTraits();
    AttributeRef encode() const;
    virtual bool equal(const Attribute & a1, const Attribute & a2) const;
    virtual int less(const Attribute & a1, const Attribute & a2) const;
    virtual bool stableLess(const Attribute & a1, const Attribute & a2) const;
    virtual int compare(const Attribute & a1, const Attribute & a2) const;
    virtual int stableCompare(const Attribute & a1, const Attribute & a2) const;
    virtual size_t hash(const Attribute & a) const;
    virtual size_t stableHash(const Attribute & a) const;
    virtual std::string print(const Attribute & attr) const;

    enum {
        FLAGS = AFL_BINCOMPARABLE | AFL_BINSTABLE | AFL_BINHASHABLE
    };
};


/*****************************************************************************/
/* DEFAULTATTRIBUTETRAITS                                                    */
/*****************************************************************************/

template<>
struct DefaultAttributeTraits<void> {
    typedef NullTraits Type;
};


} // namespace JGraph

#include "attribute_traits_inline.h"


#endif /* __jgraph__attribute_traits_h__ */

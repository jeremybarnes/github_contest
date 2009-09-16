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

struct AttributeTraits {
    virtual ~AttributeTraits();

    virtual bool equal(const Attribute & a1, const Attribute & a2) const;
    virtual int less(const Attribute & a1, const Attribute & a2) const;
    virtual bool stableLess(const Attribute & a1, const Attribute & a2) const;
    virtual int compare(const Attribute & a1, const Attribute & a2) const;
    virtual int stableCompare(const Attribute & a1, const Attribute & a2) const;
    virtual size_t hash(const Attribute & a) const;
    virtual size_t stableHash(const Attribute & a) const;
    virtual std::string print(const Attribute & attr) const = 0;

protected:
    // Allow descendents to access the value field of an attribute
    static AttributeValue getValue(const Attribute & attr);

    AttributeRef createScalarAttribute(AttributeValue value,
                                       uint32_t flags) const;

};


/*****************************************************************************/
/* SCALARATTRIBUTETRAITS                                                     */
/*****************************************************************************/

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
/* NULLTRAITS                                                                */
/*****************************************************************************/

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

/* attribute_basic_types.h                                         -*- C++ -*-
   Jeremy Barnes, 16 September 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   Basic types for attributes.
*/

#ifndef __jgraph__attribute_basic_types_h__
#define __jgraph__attribute_basic_types_h__


#include "jgraph_fwd.h"
#include "attribute_traits.h"
#include "string_map.h"

namespace JGraph {



// Atomic string, etc
struct Atom {

private:
    StringMap * stringmap;
    int handle;
};

/*****************************************************************************/
/* INTTRAITS                                                                 */
/*****************************************************************************/

struct IntTraits : public ScalarAttributeTraits {
    virtual ~IntTraits();

    AttributeRef encode(int val) const;
    virtual std::string print(const Attribute & attr) const;
};


struct StringTraits : public AttributeTraits {
    AttributeRef encode(const std::string & val) const;

    virtual bool equal(const Attribute & a1, const Attribute & a2) const;
    virtual int less(const Attribute & a1, const Attribute & a2) const;
    virtual bool stableLess(const Attribute & a1, const Attribute & a2) const;
    virtual int compare(const Attribute & a1, const Attribute & a2) const;
    virtual int stableCompare(const Attribute & a1, const Attribute & a2) const;
    virtual size_t hash(const Attribute & a) const;
    virtual size_t stableHash(const Attribute & a) const;
    virtual std::string print(const Attribute & attr) const;
};

struct AtomTraits : public AttributeTraits {
    virtual ~AtomTraits();
    AttributeRef encode(const std::string & val) const;
    AttributeRef encode(const Atom & atom) const;

    virtual bool equal(const Attribute & a1, const Attribute & a2) const;
    virtual int less(const Attribute & a1, const Attribute & a2) const;
    virtual bool stableLess(const Attribute & a1, const Attribute & a2) const;
    virtual int compare(const Attribute & a1, const Attribute & a2) const;
    virtual int stableCompare(const Attribute & a1, const Attribute & a2) const;
    virtual size_t hash(const Attribute & a) const;
    virtual size_t stableHash(const Attribute & a) const;
    virtual std::string print(const Attribute & attr) const;

private:
    StringMap string_map;
};

struct DateTraits : public AttributeTraits {
    AttributeRef encode(const std::string & val) const;

    virtual bool equal(const Attribute & a1, const Attribute & a2) const;
    virtual int less(const Attribute & a1, const Attribute & a2) const;
    virtual bool stableLess(const Attribute & a1, const Attribute & a2) const;
    virtual int compare(const Attribute & a1, const Attribute & a2) const;
    virtual int stableCompare(const Attribute & a1, const Attribute & a2) const;
    virtual size_t hash(const Attribute & a) const;
    virtual size_t stableHash(const Attribute & a) const;
    virtual std::string print(const Attribute & attr) const;
};

// Date to be put in the graph
struct Date {
    double val;
};


template<>
struct DefaultAttributeTraits<Atom> {
    typedef AtomTraits Type;
};

template<>
struct DefaultAttributeTraits<int> {
    typedef IntTraits Type;
};

template<>
struct DefaultAttributeTraits<Date> {
    typedef DateTraits Type;
};


} // namespace JGraph

#include "attribute_basic_types_inline.h"

#endif /* __jgraph__attribute_basic_types_h__ */


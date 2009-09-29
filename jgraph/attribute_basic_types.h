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
    Atom(StringMap * string_map, int handle);
    Atom(StringMap * string_map, const std::string & value);

private:
    StringMap * string_map;
    int handle;
    friend class AtomTraits;
};


/*****************************************************************************/
/* INTTRAITS                                                                 */
/*****************************************************************************/

struct IntTraits : public ScalarAttributeTraits {
    virtual ~IntTraits();

    AttributeRef encode(int val) const;
    int decode(const Attribute & attr) const;
    virtual std::string print(const Attribute & attr) const;
};


/*****************************************************************************/
/* BOOLTRAITS                                                                 */
/*****************************************************************************/

struct BoolTraits : public ScalarAttributeTraits {
    virtual ~BoolTraits();

    AttributeRef encode(bool val) const;
    bool decode(const Attribute & attr) const;
    virtual std::string print(const Attribute & attr) const;
};


/*****************************************************************************/
/* STRINGTRAITS                                                              */
/*****************************************************************************/

struct StringTraits : public RefCountedAttributeTraits<std::string> {
    virtual ~StringTraits();
    virtual std::string print(const Attribute & attr) const;
    virtual size_t hash(const Attribute & a) const;
    virtual size_t stableHash(const Attribute & a) const;
};


/*****************************************************************************/
/* ATOMTRAITS                                                                */
/*****************************************************************************/

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

    enum {
        FLAGS = AFL_BINCOMPARABLE | AFL_BINHASHABLE
    };

private:
    mutable StringMap string_map;
};


/*****************************************************************************/
/* DATETRAITS                                                                */
/*****************************************************************************/

struct DateTraits : public ScalarAttributeTraits {
    AttributeRef encode(const Date & val) const;

    // override these as the least significant bits aren't that good by
    // themselves
    virtual size_t hash(const Attribute & a) const;
    virtual size_t stableHash(const Attribute & a) const;

    virtual std::string print(const Attribute & attr) const;
};


/*****************************************************************************/
/* DATE                                                                      */
/*****************************************************************************/

// Date to be put in the graph
struct Date {
    Date(double seconds);
    Date(const std::string & str);
    double seconds;

    std::string print() const;
};

std::ostream &
operator << (std::ostream & stream, const Date & date);


template<>
struct DefaultAttributeTraits<Atom> {
    typedef AtomTraits Type;
};

template<>
struct DefaultAttributeTraits<int> {
    typedef IntTraits Type;
};

template<>
struct DefaultAttributeTraits<bool> {
    typedef BoolTraits Type;
};

template<>
struct DefaultAttributeTraits<Date> {
    typedef DateTraits Type;
};

template<>
struct DefaultAttributeTraits<std::string> {
    typedef StringTraits Type;
};


} // namespace JGraph

#include "attribute_basic_types_inline.h"

#endif /* __jgraph__attribute_basic_types_h__ */


/* attribute.h                                                     -*- C++ -*-
   Jeremy Barnes, 15 September 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   Attribute data structure.
*/

#ifndef __jgraph__attribute_h__
#define __jgraph__attribute_h__

#include "jgraph_fwd.h"
#include <stdint.h>
#include <string>


namespace JGraph {

// Lightweight handle to access an attribute and its value.  Opaque type.
struct Attribute {
private:
    int type;
    union {
        void * ptr;  // type dependent; opaque
        uint64_t val;
        float f;
        double d;
    };
};

// Atomic string, etc
struct Atom {

private:
    StringMap * stringmap;
    int handle;
};


struct AtomTraits {
    Attribute encode(const std::string & val) const;
    Attribute encode(const Atom & atom) const;
};

struct IntTraits {
    Attribute encode(int val) const;
};

struct DateTraits {
    Attribute encode(const std::string & val) const;
};

// Date to be put in the graph
struct Date {
    double val;
};


template<typename Payload>
struct DefaultAttributeTraits {
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

#include "attribute_inline.h"

#endif /* __jgraph__attribute_h__ */


/* attribute.h                                                     -*- C++ -*-
   Jeremy Barnes, 15 September 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   Attribute data structure.
*/

#ifndef __jgraph__attribute_h__
#define __jgraph__attribute_h__

#include "jgraph_fwd.h"
#include "db/persistent_fwd.h"
#include "utils/hash_specializations.h"
#include <stdint.h>
#include <string>


namespace JGraph {



/*****************************************************************************/
/* ATTRIBUTE                                                                 */
/*****************************************************************************/

// An attribute with type metadata.  Has the full range of functions available
// to it, but nothing to control its lifecycle.  One of the others should be
// used in order to guarantee the lifecycle.
//
// Note that it is invalid to compare attributes that have different types.
// Attributes that have different owners are comparible.  This requirement is
// not checked.

struct Attribute {

    // Comparison operators
    bool operator == (const Attribute & other) const;
    bool operator != (const Attribute & other) const;
    bool operator < (const Attribute & other) const;
    int compare(const Attribute & other) const;

    // Stable comparison operators
    bool stableLess(const Attribute & other) const;
    int stableCompare(const Attribute & other) const;

    // Hash function
    size_t hash() const;
    size_t stableHash() const;

    // IO operators
    std::string print() const;

private:
    union {
        AttributeValue value;
        char * ptr;
    };
    const AttributeTraits * traits;
    uint32_t flags;

    bool refcounted() const { return flags & AFL_REFCOUNTED; }
    bool bincomparable() const { return flags & AFL_BINCOMPARABLE; }
    bool binstable() const { return flags & AFL_BINSTABLE; }
    bool binhashable() const { return flags & AFL_BINHASHABLE; }

    // All of these are private as only the concrete values can be created.
    // This forces the attribute to be passed by reference, which is almost
    // always exactly what is required.
    Attribute();
    Attribute(const Attribute & other);
    Attribute(const AttributeTraits * traits,
              AttributeValue value,
              uint32_t flags);
    Attribute(const AttributeTraits * traits,
              void * ptr,
              uint32_t flags);
    Attribute & operator = (const Attribute & other);
    void swap(Attribute & other);

    friend class AttributeRef;
    friend class AttributeTraits;
};

std::ostream & operator << (std::ostream & stream,
                            const Attribute & attribute);


/*****************************************************************************/
/* ATTRIBUTEREF                                                              */
/*****************************************************************************/

// A reference to an attribute.  Makes sure that all of the reference counting
// is done properly.
struct AttributeRef : public Attribute {

    AttributeRef();
    AttributeRef(const AttributeRef & other);
    AttributeRef(const Attribute & other);
    ~AttributeRef();

    AttributeRef & operator = (const Attribute & other);
    void swap(AttributeRef & other);

private:
    AttributeRef(const AttributeTraits * traits,
                 AttributeValue value,
                 uint32_t flags);

    AttributeRef(const AttributeTraits * traits,
                 void * obj,
                 uint32_t flags);

    int * refcount() const;

    void incReferences();
    void decReferences();

    friend class AttributeTraits;
};

// Comparison objects
//
// These objects are used in data structures that contain values that are
// attributes.  For each, there are two possibilities: stable and unstable.
// The stable objects will always compare the same, on different machines and
// on different runs of the program, but are slower.  They should be used where
// it is important that the order of iteration be exactly the same in all
// circumstances.  The unstable objects are faster but the iteration order is
// not guaranteed to be correct on different machines nor even on different
// runs on the same machine.


// Comparison objects for ordered containers (map, ...)
// Note that the default used by maps (if no comparison object is specified)
// will lead to the unstable version being used; the stable version must be
// asked for explicitly.

struct UnstableLessAttribute {
    bool operator () (const Attribute & attr1,
                      const Attribute & attr2) const
    {
        return attr1 < attr2;
    }
};

struct StableLessAttribute {
    bool operator () (const Attribute & attr1,
                      const Attribute & attr2) const
    {
        return attr1.stableLess(attr2);
    }
};

// Comparison objects for unordered containers (hash_map, ...)
// Note that the default used by hash_maps (if no hash object is specified)
// will lead to the unstable version being used; the stable version must be
// asked for explicitly.

struct UnstableHashAttribute {
    size_t operator () (const Attribute & attr) const
    {
        return attr.hash();
    }
};

struct StableHashAttribute {
    size_t operator () (const Attribute & attr) const
    {
        return attr.stableHash();
    }
};


} // namespace JGraph


// Specialization to allow hash_map<Attribute, ...> to work.  Note that the
// hash map will use the *unstable* hash function.

namespace JML_HASH_NS {

template<>
struct hash<JGraph::Attribute> {
    size_t operator () (const JGraph::Attribute & attr) const
    {
        return attr.hash();
    }

};

} // namespace JML_HASH_NS


#include "attribute_inline.h"

#endif /* __jgraph__attribute_h__ */


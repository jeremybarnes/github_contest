/* attribute_traits_inline.h                                       -*- C++ -*-
   Jeremy Barnes, 16 September, 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   Inline functions for attribute traits.
*/

#ifndef __jgraph__attribute_traits_inline_h__
#define __jgraph__attribute_traits_inline_h__

#include "attribute.h"
#include "compiler/compiler.h"

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

JML_ALWAYS_INLINE AttributeRef
AttributeTraits::
createScalarAttribute(AttributeValue value,
                      uint32_t flags) const
{
    return AttributeRef(this, value, flags);
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

} // namespace JGraph

#endif /* __jgraph__attribute_traits_inline_h__ */

   

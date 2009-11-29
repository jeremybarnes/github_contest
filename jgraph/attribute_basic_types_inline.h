/* attribute_basic_types_inline.h                                  -*- C++ -*-
   Jeremy Barnes, 16 September 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   Inline functions for basic attribute types.
*/

#ifndef __jgraph__attribute_basic_types_inline_h__
#define __jgraph__attribute_basic_types_inline_h__

namespace JGraph {


/*****************************************************************************/
/* INTTRAITS                                                                 */
/*****************************************************************************/

inline AttributeRef
IntTraits::
encode(int val) const
{
    return createScalarAttribute(val, FLAGS);
}

inline int
IntTraits::
decode(const Attribute & attr) const
{
    return getValue(attr);
}


/*****************************************************************************/
/* BOOLTRAITS                                                                */
/*****************************************************************************/

inline AttributeRef
BoolTraits::
encode(bool val) const
{
    return createScalarAttribute(val, FLAGS);
}

inline bool
BoolTraits::
decode(const Attribute & attr) const
{
    return getValue(attr);
}


} // namespace JGraph

#endif /* __jgraph__attribute_basic_types_inline_h__ */

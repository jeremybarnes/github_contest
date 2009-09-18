/* attribute.cc
   Jeremy Barnes, 15 September 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.
*/

#include "attribute.h"
#include "arch/atomic_init.h"
#include "utils/string_functions.h"
#include <iostream>

using namespace ML;
using namespace std;

namespace JGraph {


namespace {

// Thread-safe and initialization order safe default traits object for null
// attributes

NullTraits * default_null_traits = 0;

NullTraits * default_traits_instance()
{
    if (JML_LIKELY(default_null_traits != 0)) return default_null_traits;
    NullTraits * instance = new NullTraits();
    atomic_init(default_null_traits, instance);
    return default_null_traits;
}


} // file scope


/*****************************************************************************/
/* ATTRIBUTE                                                                 */
/*****************************************************************************/

Attribute::
Attribute()
{
    *this = default_traits_instance()->encode();
}

std::string
Attribute::
print() const
{
    return traits->print(*this);
}


std::ostream &
operator << (std::ostream & stream,
             const Attribute & attribute)
{
    return stream << attribute.print();
}

} // namespace JGraph

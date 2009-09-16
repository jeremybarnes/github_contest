/* attribute_test.cc                                               -*- C++ -*-
   Jeremy Barnes, 16 September 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   Test of attributes for jgraph.
*/

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#include "jgraph/attribute.h"
#include "jgraph/attribute_basic_types.h"
#include <boost/test/unit_test.hpp>
#include <boost/bind.hpp>
#include <sstream>
#include <fstream>

using namespace ML;
using namespace JGraph;
using namespace std;

using boost::unit_test::test_suite;

BOOST_AUTO_TEST_CASE( test1 )
{
    IntTraits traits;

    AttributeRef attr = traits.encode(1);

    BOOST_CHECK_EQUAL(attr.print(), "1");
}

#if 0
BOOST_AUTO_TEST_CASE( test2 )
{
    StringTraits traits;

    AttributeRef attr = traits.encode("hello");

    BOOST_CHECK_EQUAL(attr.print(), "hello");
}

BOOST_AUTO_TEST_CASE( test3 )
{
    AtomTraits traits;

    AttributeRef attr = traits.encode("hello");

    BOOST_CHECK_EQUAL(attr.print(), "hello");
}

#endif

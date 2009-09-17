/* attribute_test.cc                                               -*- C++ -*-
   Jeremy Barnes, 16 September 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   Test of attributes for jgraph.
*/

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#include "jgraph/attribute.h"
#include "jgraph/attribute_basic_types.h"
#include "utils/string_functions.h"
#include <boost/test/unit_test.hpp>
#include <boost/bind.hpp>
#include <sstream>
#include <fstream>

using namespace ML;
using namespace JGraph;
using namespace std;

using boost::unit_test::test_suite;

// Object that keeps track of the number of times constructed/destroyed so
// that we can check that it's not been leaked, etc

size_t constructed = 0, destroyed = 0;

int GOOD = 0xfeedbac4;
int BAD  = 0xdeadbeef;

struct TestObj {
    TestObj()
        : val(0)
    {
        //cerr << "default construct at " << this << endl;
        ++constructed;
        magic = GOOD;
    }

    TestObj(int val)
        : val(val)
    {
        //cerr << "value construct at " << this << endl;
        ++constructed;
        magic = GOOD;
    }

   ~TestObj()
    {
        //cerr << "destroying at " << this << endl;
        ++destroyed;
        if (magic == BAD)
            throw Exception("object destroyed twice");

        if (magic != GOOD)
            throw Exception("object never initialized in destructor");

        magic = BAD;
    }

    TestObj(const TestObj & other)
        : val(other.val)
    {
        //cerr << "copy construct at " << this << endl;
        ++constructed;
        magic = GOOD;
    }

    TestObj & operator = (int val)
    {
        if (magic == BAD)
            throw Exception("assigned to destroyed object");

        if (magic != GOOD)
            throw Exception("assigned to object never initialized in assign");

        this->val = val;
        return *this;
    }

    int val;
    int magic;

    operator int () const
    {
        if (magic == BAD)
            throw Exception("read destroyed object");

        if (magic != GOOD)
            throw Exception("read from uninitialized object");

        return val;
    }
};


// Scalar attribute (integer)
BOOST_AUTO_TEST_CASE( test1 )
{
    IntTraits traits;

    AttributeRef attr = traits.encode(1);

    BOOST_CHECK_EQUAL(attr.print(), "1");
    BOOST_CHECK_EQUAL(attr, attr);
    BOOST_CHECK_EQUAL(attr < attr, false);
    BOOST_CHECK_EQUAL(attr != attr, false);
    BOOST_CHECK_EQUAL(attr.compare(attr), 0);
}

// Reference counted attribute (string)
BOOST_AUTO_TEST_CASE( test2 )
{
    StringTraits traits;

    AttributeRef attr = traits.encode("hello");

    BOOST_CHECK_EQUAL(attr.print(), "hello");
    BOOST_CHECK_EQUAL(attr, attr);
    BOOST_CHECK_EQUAL(attr < attr, false);
    BOOST_CHECK_EQUAL(attr != attr, false);
    BOOST_CHECK_EQUAL(attr.compare(attr), 0);

    AttributeRef attr2 = traits.encode("bonus");

    BOOST_CHECK_EQUAL(attr2.print(), "bonus");
    BOOST_CHECK_EQUAL(attr2, attr2);
    BOOST_CHECK_EQUAL(attr2 < attr2, false);
    BOOST_CHECK_EQUAL(attr2 != attr2, false);
    BOOST_CHECK_EQUAL(attr2.compare(attr2), 0);

    BOOST_CHECK_EQUAL(attr == attr2, false);
    BOOST_CHECK_EQUAL(attr2 == attr, false);
    BOOST_CHECK_EQUAL(attr < attr2,  false);
    BOOST_CHECK_EQUAL(attr2 < attr,  true);
    BOOST_CHECK_EQUAL(attr.stableLess(attr2),  false);
    BOOST_CHECK_EQUAL(attr2.stableLess(attr),  true);
}

// Reference counted attribute, checking

struct TestObjTraits : public RefCountedAttributeTraits<TestObj> {

    virtual std::string print(const Attribute & attr) const
    {
        return format("%d", getObject(attr).operator int());
    }

    virtual size_t hash(const Attribute & a) const
    {
        return getObject(a).operator int();
    }

    virtual size_t stableHash(const Attribute & a) const
    {
        return getObject(a).operator int();
    }

};

// Reference counted attribute (string)
BOOST_AUTO_TEST_CASE( test3 )
{
    TestObjTraits traits;

    {
        AttributeRef attr = traits.encode(3);
        
        BOOST_CHECK_EQUAL(attr.print(), "3");
        BOOST_CHECK_EQUAL(attr, attr);
        BOOST_CHECK_EQUAL(attr < attr, false);
        BOOST_CHECK_EQUAL(attr != attr, false);
        BOOST_CHECK_EQUAL(attr.compare(attr), 0);


        BOOST_CHECK_EQUAL(destroyed + 1, constructed);
    }

    BOOST_CHECK_EQUAL(destroyed, constructed);
}


// Dictionary attribute (atom)
BOOST_AUTO_TEST_CASE( test4 )
{
    AtomTraits traits;

    AttributeRef attr = traits.encode("hello");

    BOOST_CHECK_EQUAL(attr.print(), "hello");
    BOOST_CHECK_EQUAL(attr, attr);
    BOOST_CHECK_EQUAL(attr < attr, false);
    BOOST_CHECK_EQUAL(attr != attr, false);
    BOOST_CHECK_EQUAL(attr.compare(attr), 0);

    AttributeRef attr2 = traits.encode("bonus");

    BOOST_CHECK_EQUAL(attr2.print(), "bonus");
    BOOST_CHECK_EQUAL(attr2, attr2);
    BOOST_CHECK_EQUAL(attr2 < attr2, false);
    BOOST_CHECK_EQUAL(attr2 != attr2, false);
    BOOST_CHECK_EQUAL(attr2.compare(attr2), 0);

    BOOST_CHECK_EQUAL(attr == attr2, false);
    BOOST_CHECK_EQUAL(attr2 == attr, false);
    BOOST_CHECK_EQUAL(attr < attr2,  true);
    BOOST_CHECK_EQUAL(attr2 < attr,  false);
    BOOST_CHECK_EQUAL(attr.stableLess(attr2),  false);
    BOOST_CHECK_EQUAL(attr2.stableLess(attr),  true);
}

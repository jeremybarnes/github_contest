/* basic_graph_boost_test.cc
   Jeremy Barnes, 22 September 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   Test for the boost wrapper on the basic graph.
*/

#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#include "jgraph/attribute.h"
#include "jgraph/attribute_basic_types.h"
#include "utils/string_functions.h"
#include <boost/test/unit_test.hpp>
#include <boost/bind.hpp>
#include <iostream>

#include "jgraph/basic_graph_boost.h"
#include <boost/graph/graph_traits.hpp>
#include <boost/graph/topological_sort.hpp>

using namespace ML;
using namespace JGraph;
using namespace std;

using boost::unit_test::test_suite;


BOOST_AUTO_TEST_CASE( test1 )
{
    BasicGraph graph("graph");
    NodeSchema1KeyT<BasicGraph, int> node(graph, "node", "id");
    UnipartiteEdgeSchemaT<BasicGraph> edge(graph, "edge", node);

    typedef NodeT<BasicGraph> Node;

    Node n1 = node(1);
    Node n2 = node(2);
    Node n3 = node(3);
    edge(n1, n2);
    edge(n2, n3);

    cerr << "n1 = " << n1 << endl;
    cerr << "n2 = " << n2 << endl;
    cerr << "n3 = " << n3 << endl;

    vector<Node> search_results;
    BoostGraphAdaptor<UnipartiteEdgeSchemaT<BasicGraph> >
        boost_graph(edge);
    boost::topological_sort(boost_graph, back_inserter(search_results));

    // Note: order is reversed

    BOOST_REQUIRE_EQUAL(search_results.size(), 3);
    BOOST_CHECK_EQUAL(search_results[0].getAttr(node.attr1), 3);
    BOOST_CHECK_EQUAL(search_results[1].getAttr(node.attr1), 2);
    BOOST_CHECK_EQUAL(search_results[2].getAttr(node.attr1), 1);

    edge(n1, n3);
    search_results.clear();
    boost::topological_sort(boost_graph, back_inserter(search_results));
    
    BOOST_REQUIRE_EQUAL(search_results.size(), 3);
    BOOST_CHECK_EQUAL(search_results[0].getAttr(node.attr1), 3);
    BOOST_CHECK_EQUAL(search_results[1].getAttr(node.attr1), 2);
    BOOST_CHECK_EQUAL(search_results[2].getAttr(node.attr1), 1);

    // Create a back edge so it's no longer a DAG
    edge(n3, n1);
    search_results.clear();

    cerr << "*** exception expected here" << endl;
    BOOST_CHECK_THROW(boost::topological_sort(boost_graph, back_inserter(search_results)), std::exception);
    cerr << "*** end exception expected here" << endl;
}
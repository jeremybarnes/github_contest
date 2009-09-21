/* basic_graph.cc
   Jeremy Barnes, 15 September 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   Basic graph structure.
*/

#include "basic_graph.h"
#include "utils/pair_utils.h"
#include "jgraph.h"
#include "utils/string_functions.h"


using namespace std;
using namespace ML;


namespace JGraph {


/*****************************************************************************/
/* BASICGRAPH                                                                */
/*****************************************************************************/

BasicGraph::
BasicGraph(const std::string & name)
    : handle(0), name(name)
{
}

void
BasicGraph::
setNodeAttr(int node_type, int node_handle, const Attribute & attr)
{
    NodeCollection & ncoll = getNodeCollection(node_type);

    if (node_handle < 0 || node_handle >= ncoll.nodes.size())
        throw Exception("BasicGraph::setNodeAttr: invalid node handle");
    Node & node = ncoll.nodes[node_handle];
    node.attributes.push_back(attr);

    if (ncoll.attribute_index[attr.type()]) {
        AttributeIndex & aindex = *ncoll.attribute_index[attr.type()];
        aindex.insert(make_pair(AttributeRef(attr), node_handle));
    }
}

int
BasicGraph::
addNodeType(const std::string & name)
{
    return node_metadata.getOrCreate(name);
}

int
BasicGraph::
addEdgeType(const std::string & name,
            const EdgeBehavior & behavior)
{
    int result = edge_metadata.getOrCreate(name);
    return result;
}

std::pair<int, AttributeTraits *>
BasicGraph::
addNodeAttributeType(const std::string & name, int node_type,
                     boost::shared_ptr<AttributeTraits> suggested_traits)
{
    // TODO: do something with node_type
    int val = node_attr_metadata.getOrCreate(name);
    Metadata::Entry & entry = node_attr_metadata.entries[val];
    suggested_traits->setType(val);
    suggested_traits->setName(name);
    if (entry.traits)
        entry.traits->combine(*suggested_traits);
    else entry.traits = suggested_traits;
    return make_pair(val, entry.traits.get());
}

int
BasicGraph::
createNode(int type_handle)
{
    throw Exception("createNode: not done yet");
}

int
BasicGraph::
getOrCreateNode(int type_handle,
                const Attribute & attribute)
{
    NodeCollection & ncoll = getNodeCollection(type_handle);

    AttributeIndex & index = ncoll.getAttributeIndex(attribute.type());

    int id = index.getUnique(attribute);

    if (id == -1) {
        id = ncoll.nodes.size();
        ncoll.nodes.push_back(Node());
        ncoll.nodes.back().attributes.push_back(AttributeRef(attribute));
        index.insert(make_pair(AttributeRef(attribute), id));
    }
    return id;
}

int
BasicGraph::
getOrCreateEdge(int from_node_type,
                int from_node_handle,
                int to_node_type,
                int to_node_handle,
                int edge_type_handle)
{
    EdgeCollection & ecoll = getEdgeCollection(edge_type_handle);
    
    /* Find the from node, and look for the edge there */
    
    NodeCollection & ncoll_from = getNodeCollection(from_node_type);

    Node & from_node = ncoll_from.nodes.at(from_node_handle);
    
    // TODO: various optimizations possible...

    for (EdgeRefList::const_iterator
             it = from_node.edges.begin(),
             end = from_node.edges.end();
         it != end;  ++it) {
        if (it->forward == true
            && it->edge_type == edge_type_handle
            && it->dest_type == to_node_type
            && it->dest_node == to_node_handle) {
            // found
            return it->index;
        }
    }

    int result = ecoll.edges.size();
    Edge new_edge;
    new_edge.from = from_node_handle;
    new_edge.from_type = from_node_type;
    new_edge.to = to_node_handle;
    new_edge.to_type = to_node_type;
    ecoll.edges.push_back(new_edge);

    from_node.edges.push_back(EdgeRef(true, // forward
                                      edge_type_handle,
                                      to_node_type,
                                      to_node_handle,
                                      result));

    NodeCollection & ncoll_to = getNodeCollection(to_node_type);
    
    Node & to_node = ncoll_to.nodes.at(to_node_handle);
    
    to_node.edges.push_back(EdgeRef(false, // forward
                                    edge_type_handle,
                                    from_node_type,
                                    from_node_handle,
                                    result));

    return result;
}

std::string
BasicGraph::
printNode(int node_type, int node_handle) const
{
    const NodeCollection & ncoll = getNodeCollection(node_type);
    const Node & node = ncoll.nodes.at(node_handle);

    std::string id_name, id_value;
    const AttributeSet & attributes = node.attributes;

    if (!attributes.empty()) {
        id_name = attributes[0].name();
        id_value = attributes[0].print();
    }
    
    std::string result;
    result = format("node \"%s:%s\" type \"%s\" (%d), handle %d, %zd attr, %zd edges",
                    id_name.c_str(), id_value.c_str(),
                    node_metadata.entries.at(node_type).name.c_str(),
                    node_type,
                    node_handle,
                    (size_t)attributes.size(),
                    (size_t)node.edges.size());
    
    for (unsigned i = 0;  i < attributes.size();  ++i) {
        result += format("\n    %s:%s",
                         attributes[i].name().c_str(),
                         attributes[i].print().c_str());
    }

    for (unsigned i = 0;  i < node.edges.size();  ++i) {
        const EdgeRef & edge = node.edges[i];
        string edge_type = edge_metadata.entries.at(edge.edge_type).name;
        string tofrom = (edge.forward ? "TO" : "FROM");

        const NodeCollection & dest_ncoll = getNodeCollection(edge.dest_type);

        const Node & dest_node = dest_ncoll.nodes.at(edge.dest_node);

        std::string id_name, id_value;
        const AttributeSet & attributes = dest_node.attributes;
        
        if (!attributes.empty()) {
            id_name = attributes[0].name();
            id_value = attributes[0].print();
        }
        
        string desttype = node_metadata.entries.at(edge.dest_type).name.c_str();
        
        result += format("\n    %s %s %s (%s:%s)",
                         edge_type.c_str(), tofrom.c_str(),
                         desttype.c_str(), id_name.c_str(), id_value.c_str());
    }

    return result;
}

BasicGraph::NodeSetGenerator
BasicGraph::
nodesMatchingAttr(int node_type, const Attribute & attr) const
{
    NodeCollection & ncoll = getNodeCollection(node_type);

    AttributeIndex & index = ncoll.getAttributeIndex(attr.type());

    std::pair<AttributeIndex::const_iterator,
              AttributeIndex::const_iterator> range
        = index.equal_range(attr);

    // TODO: iterator invalidation... need to lock the index while using
    // TODO: deal properly with const/non-const graph semantics...
    return NodeSetGenerator(const_cast<BasicGraph *>(this),
                            node_type,
                            second_extractor(range.first),
                            second_extractor(range.second)
                            /*, lock the index for reading and release when done*/);
}

BasicGraph::NodeCollection &
BasicGraph::
getNodeCollection(int node_type) const
{
    if (node_type < 0 || node_type >= node_metadata.entries.size())
        throw Exception("getNodeCollection: type out of range");
    if (nodes_of_type.size() <= node_type)
        nodes_of_type.resize(node_type + 1);
    if (!nodes_of_type[node_type])
        nodes_of_type[node_type].reset(new NodeCollection());
    
    return  *nodes_of_type[node_type];
}

BasicGraph::EdgeCollection &
BasicGraph::
getEdgeCollection(int edge_type) const
{
    if (edge_type < 0 || edge_type >= edge_metadata.entries.size())
        throw Exception("getEdgeCollection: type out of range");
    if (edges_of_type.size() <= edge_type)
        edges_of_type.resize(edge_type + 1);
    if (!edges_of_type[edge_type])
        edges_of_type[edge_type].reset(new EdgeCollection());
    
    return  *edges_of_type[edge_type];
}

const AttributeRef &
BasicGraph::AttributeSet::
find(int type) const
{
    static const AttributeRef none;
    for (const_iterator it = begin(), e = end();
         it != e;  ++it)
        if (it->type() == type) return *it;
    return none;
}

int
BasicGraph::Metadata::
getOrCreate(const std::string & name)
{
    int result;
    Index::const_iterator it
        = index.find(name);
    if (it == index.end()) {
        result = entries.size();
        index[name] = result;
        Entry entry;
        entry.name = name;
        entry.id = result;
        entries.push_back(entry);
    }
    else result = it->second;

    return result;
}

BasicGraph::AttributeIndex &
BasicGraph::NodeCollection::
getAttributeIndex(int attr_num)
{
    boost::shared_ptr<AttributeIndex> & res
        = attribute_index[attr_num];
    if (!res) {
        // Scan through all nodes and index their attributes
        // Would be greatly improved with indexed attributes...
        res.reset(new AttributeIndex());
        for (unsigned i = 0;  i < nodes.size();  ++i) {
            const AttributeRef & attr = nodes[i].attributes.find(attr_num);
            if (attr) res->insert(make_pair(attr, i));
        }
    }
    return *res;
}

NodeT<BasicGraph>
BasicGraph::NodeSetGenerator::
curr() const
{
    if (current == -1)
        throw ML::Exception("NodeSetGenerator: no nodes");
    return NodeT<BasicGraph>(graph, node_type, current);
}

bool
BasicGraph::NodeSetGenerator::
next()
{
    if (!values || index == values->size() - 1) {
        current = -1;
        return false;
    }
    current = (*values)[++index];
    return current != -1;
}

} // namespace JGraph

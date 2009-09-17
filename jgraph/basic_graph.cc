/* basic_graph.cc
   Jeremy Barnes, 15 September 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   Basic graph structure.
*/

#include "basic_graph.h"


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
    return edge_metadata.getOrCreate(name);
}

std::pair<int, AttributeTraits *>
BasicGraph::
addNodeAttributeType(const std::string & name, int node_type,
                     boost::shared_ptr<AttributeTraits> suggested_traits)
{
    // TODO: do something with node_type
    int val = node_attr_metadata.getOrCreate(name);
    Metadata::Entry & entry = node_attr_metadata.entries[val];
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

    std::hash_map<AttributeRef, int>::const_iterator it
        = ncoll.id_index.find(attribute);
    if (it == ncoll.id_index.end()) {
        int result = ncoll.nodes.size();
        ncoll.nodes.push_back(Node());
        ncoll.id_index[attribute] = result;
        return result;
    }
    else return it->second;
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
            && it->type == edge_type_handle
            && it->dest == to_node_handle) {
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

    from_node.edges.push_back(EdgeRef(edge_type_handle,
                                      true, // forward
                                      to_node_handle,
                                      result));

    NodeCollection & ncoll_to = getNodeCollection(to_node_type);
    
    Node & to_node = ncoll_to.nodes.at(to_node_handle);
    
    to_node.edges.push_back(EdgeRef(edge_type_handle,
                                    false, // forward
                                    from_node_handle,
                                    result));

    return result;
}

BasicGraph::NodeCollection &
BasicGraph::
getNodeCollection(int node_type)
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
getEdgeCollection(int edge_type)
{
    if (edge_type < 0 || edge_type >= edge_metadata.entries.size())
        throw Exception("getEdgeCollection: type out of range");
    if (edges_of_type.size() <= edge_type)
        edges_of_type.resize(edge_type + 1);
    if (!edges_of_type[edge_type])
        edges_of_type[edge_type].reset(new EdgeCollection());
    
    return  *edges_of_type[edge_type];
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

} // namespace JGraph

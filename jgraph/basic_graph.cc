/* basic_graph.cc
   Jeremy Barnes, 15 September 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   Basic graph structure.
*/

#include "basic_graph.h"


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
setNodeAttr(int node_handle, const Attribute & attr)
{
    if (node_handle < 0 || node_handle >= nodes.size())
        throw Exception("BasicGraph::setNodeAttr: invalid node handle");
    Node & node = nodes[node_handle];
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

int
BasicGraph::
createNode(int type_handle)
{
    int result = nodes.size();
    nodes.push_back(Node());
    nodes.back().type = type_handle;
    return result;
}

int
BasicGraph::
getOrCreateNode(int type_handle,
                const Attribute & attribute)
{
    
}

int
BasicGraph::
getOrCreateEdge(int from_handle,
                int to_handle,
                int type_handle)
{
    
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
    return result;
}

} // namespace JGraph

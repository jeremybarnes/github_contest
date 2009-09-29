/* import.cc
   Jeremy Barnes, 14 September 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

*/

#include "data.h"
#include "jgraph/jgraph.h"
#include "jgraph/basic_graph.h"
#include "jgraph/attribute_basic_types.h"
#include "jgraph/query.h"

#include "utils/parse_context.h"
#include "utils/string_functions.h"
#include "utils/vector_utils.h"
#include "utils/pair_utils.h"
#include "stats/distribution_simd.h"
#include "utils/hash_map.h"

#include "utils/pair_utils.h"
#include "utils/vector_utils.h"
#include "utils/less.h"
#include "arch/exception.h"
#include "math/xdiv.h"
#include "stats/distribution_simd.h"


#include <boost/assign/list_of.hpp>

#include <fstream>

#include <boost/program_options/cmdline.hpp>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/positional_options.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/variables_map.hpp>

#include "jgraph/basic_graph_boost.h"
#include <boost/graph/graph_traits.hpp>
#include <boost/graph/topological_sort.hpp>

using namespace std;
using namespace ML;

using namespace JGraph;
typedef BasicGraph Graph;
typedef NodeT<Graph> Node;
typedef EdgeT<Graph> Edge;
typedef UnipartiteEdgeSchemaT<Graph> UniEdgeSchema;
typedef BipartiteEdgeSchemaT<Graph> BiEdgeSchema;

std::string unescape_json_string(const std::string & str)
{
    if (str.empty() || str == "\"\"") return "";
    else if (str[0] == '\"') {
        if (str[str.size() - 1] != '\"')
            throw Exception("invalid json string: " + str);

        string result;

        for (unsigned i = 1; i < str.size() - 1;  ++i) {
            char c = str[i];

            if (c == '\\') {
                if (i == str.size() - 2)
                    throw Exception("invalid backslash in json string: " + str);
                ++i;
                c = str[i];
            }

            result += c;
        }
        
        return result;
    }

    return str;
}

void import_github()
{
    Graph graph("github");

    NodeSchema1KeyT<Graph, int>  repo_node(graph, "repo", "id");
    NodeSchema1KeyT<Graph, Atom> author_node(graph, "author", "name");

    BiEdgeSchema authorof_edge(graph, "authorof", author_node, repo_node);
    UniEdgeSchema parentof_edge(graph, "parentof", repo_node);
    
    NodeAttributeSchema<Graph, Atom> repo_name_attr("name", repo_node, UNIQUE);
    NodeAttributeSchema<Graph, Date> repo_date_attr("date", repo_node, UNIQUE);
    NodeAttributeSchema<Graph, int> repo_depth_attr("depth", repo_node, UNIQUE);
    NodeAttributeSchema<Graph, string> repo_fullname_attr("fullname", repo_node, UNIQUE);

    Parse_Context repo_file("download/repos.txt");

    while (repo_file) {
        int repo_id = repo_file.expect_int();

        repo_file.expect_literal(':');
        string author_name = repo_file.expect_text('/', true /* line 14444 has no user */);

        Node repo, author;
        
        authorof_edge(author = author_node(author_name),
                      repo = repo_node(repo_id));
        
        repo_file.expect_literal('/');
        string repo_name = repo_file.expect_text(',', false);
        repo_file.expect_literal(',');
        string date_str = repo_file.expect_text("\n,", false);

        repo_name_attr(repo, repo_name);
        repo_date_attr(repo, date_str);
        repo_fullname_attr(repo, author_name + "/" + repo_name);

        int depth = 0;

        if (repo_file.match_literal(',')) {
            int parent_id = repo_file.expect_int();
            parentof_edge(repo_node(parent_id), repo);
            depth = -1;
        }

        repo_depth_attr(repo, depth);

        repo_file.expect_eol();

        //full_repo_name_to_index[author_name + "/" + repo.name] = repo_id;
    }

    NodeAttributeSchema<Graph, string> repo_desc_attr("desc", repo_node);

    Parse_Context repo_desc_file("repo_descriptions.txt");

    int found = 0, notfound = 0;

    while (repo_desc_file) {
        string full_repo_name = repo_desc_file.expect_text(':', false);
        repo_desc_file.expect_literal(':');
        string repo_desc = repo_desc_file.expect_text('\n', true);
        repo_desc = unescape_json_string(repo_desc);
        repo_desc_file.expect_eol();

        Node repo = unique(repo_node[repo_fullname_attr == full_repo_name]);

        if (!repo) { ++notfound; continue; }
        ++found;

        repo_desc_attr(repo, repo_desc);
    }

    cerr << "desc: found " << found << " notfound " << notfound << endl;

    Parse_Context author_file("authors.txt");

    NodeAttributeSchema<Graph, int>
        author_num_following_attr("num_following", author_node, UNIQUE);
    NodeAttributeSchema<Graph, int>
        author_num_followers_attr("num_followers", author_node, UNIQUE);
    NodeAttributeSchema<Graph, Date>
        author_date_joined_attr("date_joined", author_node, UNIQUE);

    while (author_file) {
        string author_name = author_file.expect_text(':', true);
        author_file.expect_literal(':');
        int num_following = author_file.expect_int();
        author_file.expect_literal(',');
        author_file.expect_int();  // github ID; unused
        author_file.expect_literal(',');
        int num_followers = author_file.expect_int();
        author_file.expect_literal(',');
        string date_joined = author_file.expect_text("\n,", false);
        author_file.expect_eol();

        Node author = author_node(author_name);

        author_num_following_attr(author, num_following);
        author_num_followers_attr(author, num_followers);
        author_date_joined_attr(author, date_joined);
    }

    // Use boost::graph to calculate depth
    vector<Node> node_order;
    BoostGraphAdaptor<UniEdgeSchema> boost_graph(parentof_edge);
    boost::topological_sort(boost_graph, back_inserter(node_order));

    int max_depth = 0;
    Node repo_max_depth;

    for (vector<Node>::reverse_iterator
             it = node_order.rbegin(),
             end = node_order.rend();
         it != end;  ++it) {
        const Node & node = *it;

        // Find all parents
        BasicGraph::IncidentEdgeIterator jt, jend;
        boost::tie(jt, jend) = node.inEdges(parentof_edge);
        // TODO: should be possible to know that there is just one (by the
        // schema).

        if (std::distance(jt, jend) == 0)
            continue;  // no parents

        if (std::distance(jt, jend) != 1)
            throw Exception("distance is wrong");
        
        Node parent = jt->to();
        int depth = parent.getAttr(repo_depth_attr) + 1;
        
        repo_depth_attr(node, depth);

        if (depth > max_depth) {
            max_depth = std::max(max_depth, depth);
            repo_max_depth = node;
        }
    }

    cerr << "max_depth = " << max_depth << " repo " << repo_max_depth
         << endl;


#if 0
    Parse_Context lang_file("download/lang.txt");

    languages.reserve(1000);

    while (lang_file) {
        int repo_id = lang_file.expect_int();
        if (repo_id < 1 || repo_id >= repos.size())
            lang_file.exception("invalid repo ID in languages file");

        Repo & repo_entry = repos[repo_id];

        while (!lang_file.match_eol()) {
            string lang = lang_file.expect_text(';', false);
            lang_file.expect_literal(';');
            int lines = lang_file.expect_int();

            int lang_id;
            if (!language_to_id.count(lang)) {
                Language new_lang;
                new_lang.id = languages.size();
                new_lang.name = lang;
                languages.push_back(new_lang);
                lang_id = new_lang.id;
                language_to_id[lang] = new_lang.id;
            }
            else lang_id = language_to_id[lang];

            Language & lang_entry = languages[lang_id];
            
            lang_entry.repos_loc[repo_id] = lines;
            repo_entry.languages[lang_id] = lines;
            repo_entry.total_loc += lines;
            lang_entry.total_loc += lines;

            if (lang_file.match_eol()) break;
            lang_file.expect_literal(',');
        }
    }

#endif

#if 0
    int nlang = languages.size();

    // Convert the repo's languages into a distribution
    for (unsigned i = 0;  i < repos.size();  ++i) {
        Repo & repo = repos[i];
        if (repo.invalid()) continue;

        repo.language_vec.clear();
        repo.language_vec.resize(nlang);

        for (Repo::LanguageMap::const_iterator
                 it = repo.languages.begin(),
                 end = repo.languages.end();
             it != end;  ++it) {
            repo.language_vec[it->first] = it->second;
        }

        if (repo.total_loc != 0) repo.language_vec /= repo.total_loc;

        repo.language_2norm = repo.language_vec.two_norm();
        
    }
#endif
                     
    NodeSchema1KeyT<Graph, int> user_node(graph, "user", "id");
    BiEdgeSchema watching_edge(graph, "watching", user_node, repo_node);

    Parse_Context data_file("download/data.txt");

    while (data_file) {
        int user_id = data_file.expect_int();
        data_file.expect_literal(':');
        int repo_id = data_file.expect_int();
        data_file.expect_eol();

        watching_edge(user_node(user_id), repo_node(repo_id));
    }

    NodeAttributeSchema<Graph, bool> repo_incomplete_attr("incomplete",
                                                          user_node);
    NodeAttributeSchema<Graph, bool> repo_test_attr("test",
                                                    user_node);
    
    BiEdgeSchema answer_edge(graph, "answer", user_node, repo_node);
    
    Parse_Context test_file("download/test.txt");

    while (test_file) {
        int user_id = test_file.expect_int();

        Node user = user_node(user_id);
        repo_incomplete_attr(user, true);
        repo_test_attr(user, true);

        if (test_file.match_literal(':'))
            answer_edge(user, repo_node(test_file.expect_int()));
        
        test_file.expect_eol();
    }

    Parse_Context fork_file("download/repo_forks.txt");

    NodeAttributeSchema<Graph, int> repo_forks_attr("forks", repo_node);

    while (fork_file) {
        int repo_id = fork_file.expect_int();
        fork_file.expect_whitespace();

        repo_forks_attr(repo_node(repo_id), fork_file.expect_int());
        fork_file.expect_eol();
    }

    Parse_Context watch_file("download/repo_watch.txt");

    NodeAttributeSchema<Graph, int> repo_watches_attr("watches", repo_node);

    while (watch_file) {
        int repo_id = watch_file.expect_int();
        watch_file.expect_whitespace();
        repo_watches_attr(repo_node(repo_id), watch_file.expect_int());
        watch_file.expect_eol();
    }

    Parse_Context collab_file("download/repo_col.txt");

    BiEdgeSchema collaborates_edge(graph, "collaborates_on", author_node, repo_node);

    while (collab_file) {
        int repo_id = collab_file.expect_int();

        collab_file.expect_whitespace();
        string name = collab_file.expect_text("\n ");
        collab_file.skip_whitespace();
        if (collab_file.match_eol()) continue;

        Node repo = repo_node(repo_id);

        while (!collab_file.match_eol()) {
            string author_name = collab_file.expect_text(" \n");
            collab_file.skip_whitespace();

            Node author = author_node(author_name);

            collaborates_edge(author, repo);
        }
    }
    
    Parse_Context follow_file("download/follow.txt");
    
    UniEdgeSchema follows_edge(graph, "follows", user_node);

    while (follow_file) {
        int follower_id = follow_file.expect_int();
        follow_file.expect_whitespace();
        int followed_id = follow_file.expect_int();
        follow_file.expect_eol();

        follows_edge(user_node(follower_id), user_node(followed_id));
    }

    // Print some: repo ID 407
    cerr << Node(unique(repo_node[repo_node.attr1 == 407])) << endl;
    cerr << Node(unique(repo_node[repo_node.attr1 == 408])) << endl;
    cerr << Node(unique(repo_node[repo_node.attr1 == 409])) << endl;
    cerr << Node(unique(repo_node[repo_node.attr1 == 18407])) << endl;
    cerr << Node(unique(user_node[user_node.attr1 == 407])) << endl;
    cerr << Node(unique(author_node[author_node.attr1 == "petdance"])) << endl;
}


int main(int argc, char ** argv)
{
    {
        using namespace boost::program_options;

        options_description all_opt;
        all_opt.add_options()
            ("help,h", "print this message");
        
        variables_map vm;
        store(command_line_parser(argc, argv)
              .options(all_opt)
              .run(),
              vm);
        notify(vm);

        if (vm.count("help")) {
            cout << all_opt << endl;
            return 1;
        }
    }

    cerr << "loading data...";
    import_github();
    cerr << " done." << endl;
}

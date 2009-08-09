/* data.cc
   Jeremy Barnes, 8 August 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   Implementation of the data class.
*/

#include "data.h"

#include "utils/parse_context.h"
#include "utils/string_functions.h"
#include "utils/vector_utils.h"
#include "utils/pair_utils.h"

using namespace std;
using namespace ML;

Data::Data()
{
}

void Data::load()
{
    Parse_Context repo_file("download/repos.txt");

    repos.resize(125000);

    authors.resize(60000);

    while (repo_file) {
        Repo repo;
        repo.id = repo_file.expect_int();
        repo_file.expect_literal(':');
        string author_name = repo_file.expect_text('/', true /* line 14444 has no user */);
        if (author_name == "")
            repo.author = -1;
        else {
            if (!author_name_to_id.count(author_name)) {
                // Unseen author... add it
                repo.author = author_name_to_id[author_name] = authors.size();
                Author new_author;
                new_author.name = author_name;
                new_author.id = repo.author;
                authors.push_back(new_author);
            }
            else repo.author = author_name_to_id[author_name];

            authors[repo.author].repositories.insert(repo.id);
        }
            
        repo_file.expect_literal('/');
        repo.name = repo_file.expect_text(',', false);
        repo_file.expect_literal(',');
        repo.date = repo_file.expect_text("\n,", false);
        if (repo_file.match_literal(',')) {
            repo.parent = repo_file.expect_int();
            repo.depth = -1;
        }
        else {
            repo.parent = -1;
            repo.depth = 0;
        }

        repo_file.expect_eol();

        if (repo.id < 1 || repo.id >= repos.size())
            throw Exception("invalid repo number " + ostream_format(repo.id));
        
        repos[repo.id] = repo;
    }


    languages.reserve(1000);

    /* Expand all parents */
    cerr << "expanding parents..." << endl;
    bool need_another = true;
    int depth;

    for (depth = 0;  need_another;  ++depth) {
        need_another = false;
        for (unsigned i = 0;  i < repos.size();  ++i) {
            Repo & repo = repos[i];
            if (repo.depth != -1) continue;
            if (repo.parent == -1)
                throw Exception("logic error: parent invalid");

            Repo & parent = repos[repo.parent];
            if (parent.depth == -1) {
                need_another = true;
                continue;
            }

            repo.depth = parent.depth + 1;
            repo.ancestors = parent.ancestors;
            repo.ancestors.push_back(repo.parent);
            repo.all_ancestors.insert(repo.ancestors.begin(),
                                      repo.ancestors.end());
        }
    }

    cerr << "max parent depth was " << depth << endl;


    Parse_Context lang_file("download/lang.txt");

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

    cerr << "total of " << languages.size() << " languages" << endl;

    Parse_Context data_file("download/data.txt");

    users.resize(60000);

    while (data_file) {
        int user_id = data_file.expect_int();
        data_file.expect_literal(':');
        int repo_id = data_file.expect_int();
        data_file.expect_eol();

        if (user_id < 0 || user_id >= users.size())
            data_file.exception("invalid user ID");
        if (repo_id <= 0 || repo_id >= repos.size())
            data_file.exception("invalid repository ID");
        
        Repo & repo_entry = repos[repo_id];
        User & user_entry = users[user_id];

        repo_entry.watchers.insert(user_id);
        user_entry.watching.insert(repo_id);
    }

    // Sort repository in order of number of watchers to see which are the
    // most watched
    for (unsigned i = 0;  i < repos.size();  ++i) {
        if (repos[i].watchers.empty()) continue;
        num_watchers.push_back(make_pair(i, repos[i].watchers.size()));
    }
    sort_on_second_descending(num_watchers);


    users_to_test.reserve(5000);

    Parse_Context test_file("download/test.txt");

    while (test_file) {
        int user_id = test_file.expect_int();

        if (user_id < 0 || user_id >= users.size())
            test_file.exception("invalid user ID");

        test_file.expect_eol();

        users_to_test.push_back(user_id);
        users[user_id].incomplete = true;
    }


}

template<class Iterator>
vector<int>
Data::
rank_repos_by_popularity(Iterator first, Iterator last) const
{
    vector<pair<int, int> > results;  // repo, num watchers

    for (; first != last;  ++first) {
        int repo = *first;
        results.push_back(make_pair(repo, repos[repo].watchers.size()));
    }
    
    sort_on_second_descending(results);
    
    vector<int> result(first_extractor(results.begin()),
                       first_extractor(results.end()));
    
    return result;
}

std::vector<int>
Data::
rank_repos_by_popularity(const std::set<int> & repos) const
{
    return rank_repos_by_popularity(repos.begin(), repos.end());
}

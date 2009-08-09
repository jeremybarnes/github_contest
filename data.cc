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

    calc_popularity();

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

void
Data::
calc_popularity()
{
    num_watchers.clear();

    // Sort repository in order of number of watchers to see which are the
    // most watched
    for (unsigned i = 0;  i < repos.size();  ++i) {
        if (repos[i].watchers.empty()) continue;
        num_watchers.push_back(make_pair(i, repos[i].watchers.size()));
    }
    sort_on_second_descending(num_watchers);

    // Now go through and get popularity rank
    int last_num_watchers = -1;
    int last_rank = -1;

    for (unsigned i = 0;  i < num_watchers.size();  ++i) {
        int rank = (last_num_watchers == num_watchers[i].second
                    ? last_rank : i);
        last_rank = rank;

        repos[num_watchers[i].first].popularity_rank = rank;
    }
}

void
Data::
setup_fake_test(int nusers)
{
    /* Problems:
       1.  We shouldn't allow a repo to lose all of its watchers.  Currently,
           that can happen which makes it rather difficult.
    */

    // First, go through and select the users
    vector<int> candidate_users;
    candidate_users.reserve(users.size());
    for (unsigned i = 0;  i < users.size();  ++i) {
        // To be a candidate, a user must:
        // a) not be incomplete
        // b) have more than one watched repository

        const User & user = users[i];
        if (user.incomplete) continue;
        if (user.watching.size() < 2) continue;

        candidate_users.push_back(i);
    }

    if (candidate_users.size() <= nusers)
        throw Exception("tried to fake test on too many users");

    // Re-order randomly
    std::random_shuffle(candidate_users.begin(),
                        candidate_users.end());
    
    // Select the first N
    candidate_users.erase(candidate_users.begin() + nusers,
                          candidate_users.end());

    std::sort(candidate_users.begin(), candidate_users.end());
    
    answers.resize(nusers);
    users_to_test = candidate_users;

    int badly_removed = 0;

    // Modify the users, one by one
    for (unsigned i = 0;  i < candidate_users.size();  ++i) {
        int user_id = candidate_users[i];
        User & user = users[user_id];

        user.incomplete = true;

        // Select a repo to remove
        vector<int> all_watched(user.watching.begin(),
                                user.watching.end());

        std::random_shuffle(all_watched.begin(),
                            all_watched.end());

        bool found = false;

        for (unsigned j = 0; j < all_watched.size() && !found;  ++j) {
            int repo_id = all_watched[j];
            Repo & repo = repos[repo_id];

            // Don't remove if only one watcher, unless it's the last one in
            // which case we have to remove something...
            if (repo.watchers.size() < 2 && j != all_watched.size() - 1)
                continue;

            if (repo.watchers.size() < 2) ++badly_removed;

            repo.watchers.erase(user_id);
            user.watching.erase(repo_id);

            answers[i] = repo_id;
            found = true;
        }
    }

    cerr << "created test with " << nusers << " users but "
         << badly_removed << " badly removed" << endl;

    // Re-calculate derived data structures
    calc_popularity();
}

set<int>
Data::
get_most_popular_repos(int n) const
{
    if (n > repos.size())
        throw Exception("get_most_popular_repos: too many requested");

    set<int> top_n;

    for (int i = 0;  i < n;  ++i) {
        int repo_id = num_watchers[i].first;
        top_n.insert(repo_id);
    }

    return top_n;
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

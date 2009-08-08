/* github.cc                                                       -*- C++ -*-
   Jeremy Barnes, 6 August 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   Code to implement a basic github recommendation engine.
*/

#include <map>
#include <set>
#include <vector>
#include <string>
#include "utils/parse_context.h"
#include "utils/string_functions.h"
#include <boost/assign/list_of.hpp>
#include <fstream>
#include "utils/vector_utils.h"
#include "utils/pair_utils.h"


using namespace std;
using namespace ML;

template<typename X>
std::ostream & operator << (std::ostream & stream, const std::set<X> & s)
{
    stream << "{ ";
    std::copy(s.begin(), s.end(),
              std::ostream_iterator<X>(stream, " "));
    return stream << "}";
}

struct Repo {
    int id;
    int author;
    string name;
    string date;
    int parent;
    int depth;
    vector<int> ancestors;
    set<int> all_ancestors;
    map<int, size_t> languages;
    size_t total_loc;
    set<int> watchers;
};

struct Language {
    int id;
    string name;
    map<int, size_t> repos_loc;
    size_t total_loc;
};

// NOTE: users and authors are the same, but we are explicitly not allowed to
// map them onto each other.

struct User {
    int id;
    set<int> watching;
};

struct Author {
    int id;
    string name;
    set<int> repositories;
};

template<class Iterator>
vector<int>
rank_repos(Iterator first, Iterator last,
           const vector<Repo> & repos)
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

int main(int argc, char ** argv)
{
    // Load up the repos

    Parse_Context repo_file("download/repos.txt");

    vector<Repo> repos;
    repos.resize(125000);

    map<string, int> author_name_to_id;
    vector<Author> authors;
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


    map<string, int> language_to_id;
    vector<Language> languages;
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

    vector<User> users;
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
    vector<pair<int, int> > num_watchers;
    for (unsigned i = 0;  i < repos.size();  ++i) {
        if (repos[i].watchers.empty()) continue;
        num_watchers.push_back(make_pair(i, repos[i].watchers.size()));
    }
    sort_on_second_descending(num_watchers);


    vector<int> users_to_test;
    users_to_test.reserve(5000);

    Parse_Context test_file("download/test.txt");

    while (test_file) {
        int user_id = test_file.expect_int();

        if (user_id < 0 || user_id >= users.size())
            test_file.exception("invalid user ID");

        test_file.expect_eol();

        users_to_test.push_back(user_id);
    }


#if 0
    for (unsigned i = 0;  i < users_to_test.size();  ++i) {
        int user_id = users_to_test[i];
        
        cerr << "user " << user_id << " is watching "
             << users[user_id].watching.size() << " repositories"
             << ": " << users[user_id].watching
             << endl;
    }
#endif


    ofstream out("results.txt");

    for (unsigned i = 0;  i < users_to_test.size();  ++i) {

        int user_id = users_to_test[i];
        const User & user = users[user_id];

        set<int> user_results;

        /* Like everyone else, see which parents and ancestors weren't
           watched */
        set<int> parents_of_watched;
        set<int> ancestors_of_watched;
        set<int> authors_of_watched_repos;

        for (set<int>::const_iterator
                 it = user.watching.begin(),
                 end = user.watching.end();
             it != end;  ++it) {
            int watched_id = *it;
            const Repo & watched = repos[watched_id];

            if (watched.author != -1)
                authors_of_watched_repos.insert(watched.author);

            if (watched.parent == -1) continue;

            parents_of_watched.insert(watched.parent);
            ancestors_of_watched.insert(watched.ancestors.begin(),
                                        watched.ancestors.end());
        }

        // Make them exclusive
        for (set<int>::const_iterator
                 it = parents_of_watched.begin(),
                 end = parents_of_watched.end();
             it != end;  ++it)
            ancestors_of_watched.erase(*it);

        for (set<int>::const_iterator
                 it = user.watching.begin(),
                 end = user.watching.end();
             it != end;  ++it) {
            parents_of_watched.erase(*it);
            ancestors_of_watched.erase(*it);
        }

        // Now generate the results

        vector<int> ranked_parents
            = rank_repos(parents_of_watched.begin(),
                         parents_of_watched.end(),
                         repos);

        // First: parents of watched repos
        for (vector<int>::const_iterator
                 it = ranked_parents.begin(),
                 end = ranked_parents.end();
             it != end && user_results.size() < 10;  ++it) {
            int repo_id = *it;
            if (user.watching.count(repo_id)) continue;
            user_results.insert(repo_id);
        }

#if 0
        // Next: watched authors
        set<int> repos_by_watched_authors;
        for (set<int>::const_iterator
                 it = authors_of_watched_repos.begin(),
                 end = authors_of_watched_repos.end();
             it != end;  ++it)
            repos_by_watched_authors.insert(authors[*it].repositories.begin(),
                                            authors[*it].repositories.end());
        
        vector<int> ranked_by_watched
            = rank_repos(repos_by_watched_authors.begin(),
                         repos_by_watched_authors.end(),
                         repos);

        for (vector<int>::const_iterator
                 it = ranked_by_watched.begin(),
                 end = ranked_by_watched.end();
             it != end && user_results.size() < 10;  ++it) {
            int repo_id = *it;
            if (user.watching.count(repo_id)) continue;
            user_results.insert(repo_id);
        }
#endif

        vector<int> ranked_ancestors
            = rank_repos(ancestors_of_watched.begin(),
                         ancestors_of_watched.end(),
                         repos);

        // Second: ancestors of watched repos
        for (vector<int>::const_iterator
                 it = ranked_ancestors.begin(),
                 end = ranked_ancestors.end();
             it != end && user_results.size() < 10;  ++it) {
            int repo_id = *it;
            if (user.watching.count(repo_id)) continue;
            user_results.insert(repo_id);
        }

        // Third: by popularity
        for (int i = 0;  user_results.size() < 10;  ++i) {
            int repo_id = num_watchers[i].first;

            // Don't add one already watched
            if (users[user_id].watching.count(repo_id)) continue;
            
            user_results.insert(repo_id);
        }

        out << user_id << ":";
        int j = 0;
        for (set<int>::const_iterator
                 it = user_results.begin(),
                 end = user_results.end();
             it != end;  ++it, ++j) {
            out << *it;
            if (j != user_results.size() - 1)
                out << ',';
        }
        out << endl;
    }
}

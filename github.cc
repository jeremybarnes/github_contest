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
    string user;
    string name;
    string date;
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

struct User {
    int id;
    string name;
    set<int> watching;
};


int main(int argc, char ** argv)
{
    // Load up the repos

    Parse_Context repo_file("download/repos.txt");

    vector<Repo> repos;
    repos.resize(125000);

    while (repo_file) {
        Repo repo;
        repo.id = repo_file.expect_int();
        repo_file.expect_literal(':');
        repo.user = repo_file.expect_text('/', true /* line 14444 has no user */);
        repo_file.expect_literal('/');
        repo.name = repo_file.expect_text(',', false);
        repo_file.expect_literal(',');
        repo.date = repo_file.expect_text('\n', false);
        repo_file.expect_eol();

        if (repo.id < 1 || repo.id >= repos.size())
            throw Exception("invalid repo number " + ostream_format(repo.id));
        
        repos[repo.id] = repo;
    }


    map<string, int> language_to_id;
    vector<Language> languages;
    languages.reserve(1000);


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


    for (unsigned i = 0;  i < users_to_test.size();  ++i) {
        int user_id = users_to_test[i];
        
        cerr << "user " << user_id << " is watching "
             << users[user_id].watching.size() << " repositories"
             << ": " << users[user_id].watching
             << endl;
    }


    ofstream out("results.txt");

    for (unsigned i = 0;  i < users_to_test.size();  ++i) {

        int user_id = users_to_test[i];

        vector<int> user_results;

        for (unsigned i = 0;  i < 10;  ++i)
            user_results.push_back(num_watchers[i].first);

        //user_results = boost::assign::list_of(17)(302)(654)(76)(616)(58)(8)(866)(29)(84);

        out << user_id << ":";
        for (unsigned j = 0;  j < user_results.size();  ++j) {
            out << user_results[j];
            if (j != user_results.size() - 1)
                out << ',';
        }
        out << endl;
    }
}

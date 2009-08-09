/* github.cc                                                       -*- C++ -*-
   Jeremy Barnes, 6 August 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   Code to implement a basic github recommendation engine.
*/

#include "data.h"

#include <fstream>
#include <iterator>
#include <iostream>

#include "arch/exception.h"
#include "utils/string_functions.h"

#include "boost/program_options/cmdline.hpp"
#include "boost/program_options/options_description.hpp"
#include "boost/program_options/positional_options.hpp"
#include "boost/program_options/parsers.hpp"
#include "boost/program_options/variables_map.hpp"


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

/** Add the given set of results to the user results, ranking before doing so */
void rank_and_add(const set<int> & to_add,
                  set<int> & user_results,
                  const User & user,
                  const Data & data)
{
    if (user_results.size() >= 10) return;

    vector<int> ranked
            = data.rank_repos_by_popularity(to_add);
    
    // First: parents of watched repos
    for (vector<int>::const_iterator
             it = ranked.begin(),
             end = ranked.end();
         it != end && user_results.size() < 10;  ++it) {
        int repo_id = *it;
        if (user.watching.count(repo_id)) continue;
        user_results.insert(repo_id);
    }
}

int main(int argc, char ** argv)
{
    // Do we perform a fake test (where we test different users than the ones
    // from the real test) but it works locally.
    bool fake_test = false;


    {
        using namespace boost::program_options;

        options_description options("Options");
        options.add_options()
            ("fake-test,f", value<bool>(&fake_test)->zero_tokens(),
             "run a fake local test instead of generating real results");

        //positional_options_description p;
        //p.add("dataset", -1);

        options_description all_opt;
        all_opt
            .add(options);
        all_opt.add_options()
            ("help,h", "print this message");
        
        variables_map vm;
        store(command_line_parser(argc, argv)
              .options(all_opt)
              //.positional(p)
              .run(),
              vm);
        notify(vm);

        if (vm.count("help")) {
            cout << all_opt << endl;
            return 1;
        }
    }

    // Load up the data
    Data data;
    data.load();

    if (fake_test)
        data.setup_fake_test();

    set<int> top_ten = data.get_most_popular_repos(10);

    vector<set<int> > results;
    results.reserve(data.users_to_test.size());

    for (unsigned i = 0;  i < data.users_to_test.size();  ++i) {

        int user_id = data.users_to_test[i];
        const User & user = data.users[user_id];

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
            const Repo & watched = data.repos[watched_id];

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

        rank_and_add(parents_of_watched, user_results, user, data);

        // Next: watched authors
        set<int> repos_by_watched_authors;
        for (set<int>::const_iterator
                 it = authors_of_watched_repos.begin(),
                 end = authors_of_watched_repos.end();
             it != end;  ++it)
            repos_by_watched_authors
                .insert(data.authors[*it].repositories.begin(),
                        data.authors[*it].repositories.end());

        rank_and_add(repos_by_watched_authors, user_results, user, data);

        // Next: ancestors (more distant than parents)
        rank_and_add(ancestors_of_watched, user_results, user, data);

        // Finally: by popularity
        rank_and_add(top_ten, user_results, user, data);

        results.push_back(user_results);
    }

    if (results.size() != data.users_to_test.size())
        throw Exception("wrong number of results");


    if (fake_test) {
        // We now perform the evaluation
        size_t correct = 0;
        for (unsigned i = 0;  i < results.size();  ++i) {
            if (results[i].size() > 10)
                throw Exception("invalid result");
            correct += results[i].count(data.answers[i]);
        }

        cerr << format("fake test results: correct %d/%d = %6.2f%%",
                       correct, results.size(),
                       100.0 * correct / results.size())
             << endl;

        // don't write results
        return 0;
    }


    // Write out results file
    ofstream out("results.txt");

    for (unsigned i = 0;  i < data.users_to_test.size();  ++i) {

        int user_id = data.users_to_test[i];

        const set<int> & user_results = results[i];

        // Write to results file
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

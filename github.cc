/* github.cc                                                       -*- C++ -*-
   Jeremy Barnes, 6 August 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   Code to implement a basic github recommendation engine.
*/

#include "data.h"

#include <fstream>
#include <iterator>

using namespace std;

template<typename X>
std::ostream & operator << (std::ostream & stream, const std::set<X> & s)
{
    stream << "{ ";
    std::copy(s.begin(), s.end(),
              std::ostream_iterator<X>(stream, " "));
    return stream << "}";
}


int main(int argc, char ** argv)
{
    Data data;
    data.load();

    // Load up the repos

#if 0
    for (unsigned i = 0;  i < data.users_to_test.size();  ++i) {
        int user_id = data.users_to_test[i];
        
        cerr << "user " << user_id << " is watching "
             << users[user_id].watching.size() << " repositories"
             << ": " << users[user_id].watching
             << endl;
    }
#endif


    ofstream out("results.txt");

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

        vector<int> ranked_parents
            = data.rank_repos_by_popularity(parents_of_watched);

        // First: parents of watched repos
        for (vector<int>::const_iterator
                 it = ranked_parents.begin(),
                 end = ranked_parents.end();
             it != end && user_results.size() < 10;  ++it) {
            int repo_id = *it;
            if (user.watching.count(repo_id)) continue;
            user_results.insert(repo_id);
        }

        // Next: watched authors
        set<int> repos_by_watched_authors;
        for (set<int>::const_iterator
                 it = authors_of_watched_repos.begin(),
                 end = authors_of_watched_repos.end();
             it != end;  ++it)
            repos_by_watched_authors
                .insert(data.authors[*it].repositories.begin(),
                        data.authors[*it].repositories.end());
        
        vector<int> ranked_by_watched
            = data.rank_repos_by_popularity(repos_by_watched_authors);

        for (vector<int>::const_iterator
                 it = ranked_by_watched.begin(),
                 end = ranked_by_watched.end();
             it != end && user_results.size() < 10;  ++it) {
            int repo_id = *it;
            if (user.watching.count(repo_id)) continue;
            user_results.insert(repo_id);
        }

        vector<int> ranked_ancestors
            = data.rank_repos_by_popularity(ancestors_of_watched);

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
            int repo_id = data.num_watchers[i].first;

            // Don't add one already watched
            if (user.watching.count(repo_id)) continue;
            
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

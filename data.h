/* data.h                                                          -*- C++ -*-
   Jeremy Barnes, 8 August 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   Class containing data for github contest.
*/

#ifndef __github__data_h__
#define __github__data_h__

#include <map>
#include <set>
#include <vector>
#include <string>

struct Repo {
    int id;
    int author;
    std::string name;
    std::string date;
    int parent;
    int depth;
    std::vector<int> ancestors;
    std::set<int> all_ancestors;
    std::map<int, size_t> languages;
    size_t total_loc;
    std::set<int> watchers;
};

struct Language {
    int id;
    std::string name;
    std::map<int, size_t> repos_loc;
    size_t total_loc;
};

// NOTE: users and authors are the same, but we aren't given the mapping.

struct User {
    User()
        : id(-1), incomplete(false)
    {
    }

    int id;
    std::set<int> watching;

    /// Is there a watch missing from this user?  True for users being tested.
    /// In this case, we should be careful about using negative evidence.
    bool incomplete;
};

struct Author {
    int id;
    std::string name;
    std::set<int> repositories;
};

struct Data {
    Data();

    void load();

    std::vector<Repo> repos;
    std::map<std::string, int> author_name_to_id;
    std::vector<Author> authors;
    std::map<std::string, int> language_to_id;
    std::vector<Language> languages;
    std::vector<User> users;

    std::vector<std::pair<int, int> > num_watchers;

    std::vector<int> users_to_test;

    std::vector<int>
    rank_repos_by_popularity(const std::set<int> & repos) const;

private:
    template<class Iterator>
    std::vector<int>
    rank_repos_by_popularity(Iterator first, Iterator last) const;
};


#endif /* __github__data_h__ */

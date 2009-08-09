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
    int popularity_rank;
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

    /// Answers, for when running a fake test
    std::vector<int> answers;

    void calc_popularity();

    std::vector<int>
    rank_repos_by_popularity(const std::set<int> & repos) const;

    std::set<int> get_most_popular_repos(int n = 10) const;
    

    /* Fake testing */

    /** Setup a different test, where we use different users than the ones
        in the testing file, and perform a real scoring process. */
    void setup_fake_test(int nusers = 4788);

    /** Score the fake test */
    void score_fake_test(const std::vector<std::set<int> > & results) const;

private:
    template<class Iterator>
    std::vector<int>
    rank_repos_by_popularity(Iterator first, Iterator last) const;
};


#endif /* __github__data_h__ */

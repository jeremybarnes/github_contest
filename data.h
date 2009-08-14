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

#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/multi_array.hpp>
#include "stats/distribution.h"

using ML::Stats::distribution;

struct Repo {
    Repo()
        : id(-1), author(-1), parent(-1), depth(-1), total_loc(0),
          popularity_rank(-1),
          repos_watched_by_watchers_initialized(false)
    {
    }

    int id;
    int author;
    std::string name;
    boost::gregorian::date date;
    int parent;
    int depth;
    std::vector<int> ancestors;
    std::set<int> all_ancestors;

    typedef std::map<int, size_t> LanguageMap;
    LanguageMap languages;

    size_t total_loc;
    std::set<int> watchers;
    int popularity_rank;
    distribution<float> language_vec;
    float language_2norm;

    mutable std::map<int, int> repos_watched_by_watchers;
    mutable bool repos_watched_by_watchers_initialized;

    bool invalid() const { return id == -1; }
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
    distribution<float> language_vec;
    float language_2norm;

    /// Is there a watch missing from this user?  True for users being tested.
    /// In this case, we should be careful about using negative evidence.
    bool incomplete;

    bool invalid() const { return id == -1; }
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

    // Two density matrices offset by 1/2
    boost::multi_array<unsigned, 2> density1, density2;

    typedef std::map<std::string, std::vector<int> > Repo_Name_To_Repos;
    Repo_Name_To_Repos repo_name_to_repos;

    const std::vector<int> & name_to_repos(const std::string & name) const;

    std::vector<int> users_to_test;

    /// Answers, for when running a fake test
    std::vector<int> answers;

    void calc_popularity();

    void calc_density();

    void calc_languages();

    float density(int user_id, int repo_id) const;

    std::vector<int>
    rank_repos_by_popularity(const std::set<int> & repos) const;

    std::set<int> get_most_popular_repos(int n = 10) const;
    

    /* Fake testing */

    /** Setup a different test, where we use different users than the ones
        in the testing file, and perform a real scoring process. */
    void setup_fake_test(int nusers = 4788, int seed = 0);

    /** Score the fake test */
    void score_fake_test(const std::vector<std::set<int> > & results) const;

private:
    template<class Iterator>
    std::vector<int>
    rank_repos_by_popularity(Iterator first, Iterator last) const;
};


#endif /* __github__data_h__ */

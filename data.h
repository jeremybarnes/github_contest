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
#include "utils/vector_utils.h"

using ML::Stats::distribution;

// Sorted vector of integer IDs
class IdSet {

    // Note: not thread safe
    typedef std::vector<int> Vals;
    mutable Vals vals;
    mutable int sorted;  // 1 = yes, 0 = no, -1 = in progress

    void sort() const
    {
        ML::make_vector_set(vals);
        sorted = true;
    }

public:

    IdSet()
        : sorted(true)
    {
    }

    void insert(int id)
    {
        vals.push_back(id);
        if (sorted && (vals.empty() || vals.back() < id)) return;
        sorted = false;
    }

    typedef __gnu_cxx::__normal_iterator<Vals::const_iterator, const IdSet>
        const_iterator;

    typedef Vals::pointer pointer;

    const_iterator begin() const
    {
        if (!sorted) sort();
        return const_iterator(vals.begin());
    }
    
    const_iterator end() const
    {
        if (!sorted) sort();
        return const_iterator(vals.end());
    }

    bool count(int id) const
    {
        if (!sorted) sort();
        return std::binary_search(vals.begin(), vals.end(), id);
    }
    
    void erase(int id) const
    {
        if (!sorted) sort();
        Vals::iterator it
            = std::lower_bound(vals.begin(), vals.end(), id);
        if (it != vals.end() && *it == id)
            vals.erase(it);
    }

    template<class Iterator>
    void insert(Iterator first, Iterator last)
    {
        //if (std::is_sorted(first, last)) {
        //    insert_sorted(first, last);
        //    return;
        //}
        vals.insert(vals.end(), first, last);
        sorted = false;
    }

    template<class Iterator>
    void insert_sorted(Iterator first, Iterator last)
    {
        // TODO: could do a merge here...
        vals.insert(vals.end(), first, last);
        sorted = false;
    }

    void insert(const_iterator first, const_iterator last)
    {
        insert_sorted(first, last);
    }

    void insert(std::set<int>::const_iterator first,
                std::set<int>::const_iterator last)
    {
        insert_sorted(first, last);
    }

    size_t size() const { return vals.size(); }
    bool empty() const { return vals.empty(); }
};


struct Repo {
    Repo()
        : id(-1), author(-1), parent(-1), depth(-1), total_loc(0),
          popularity_rank(-1),
          repo_prob(0.0), repo_prob_rank(-1), repo_prob_percentile(0.0)
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
    std::set<int> children;

    typedef std::map<int, size_t> LanguageMap;
    LanguageMap languages;

    size_t total_loc;
    IdSet watchers;
    int popularity_rank;
    distribution<float> language_vec;
    float language_2norm;

    float repo_prob;
    int repo_prob_rank;
    float repo_prob_percentile;

    distribution<float> singular_vec;
    float singular_2norm;

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
        : id(-1),
          user_prob(0.0), user_prob_rank(-1), user_prob_percentile(0.0),
          incomplete(false)
    {
    }

    int id;
    IdSet watching;
    distribution<float> language_vec;
    float language_2norm;

    float user_prob;
    int user_prob_rank;
    float user_prob_percentile;

    distribution<float> singular_vec;
    float singular_2norm;

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

    /* Data for the stochastic random walk simulation */
    distribution<double> repo_prob;
    distribution<double> user_prob;

    /* Perform the stochastic random walk */
    void stochastic_random_walk();

    /* Fake testing */

    /** Setup a different test, where we use different users than the ones
        in the testing file, and perform a real scoring process. */
    void setup_fake_test(int nusers = 4788, int seed = 0);

    /** Score the fake test */
    void score_fake_test(const std::vector<std::set<int> > & results) const;

    distribution<float> singular_values;

private:
    template<class Iterator>
    std::vector<int>
    rank_repos_by_popularity(Iterator first, Iterator last) const;
};


#endif /* __github__data_h__ */

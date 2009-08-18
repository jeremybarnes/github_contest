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
#include "stats/distribution_simd.h"

#include <boost/assign/list_of.hpp>

using namespace std;
using namespace ML;

enum { DENSITY_REPO_STEP = 200, DENSITY_USER_STEP=100 };

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
        string date_str = repo_file.expect_text("\n,", false);
        repo.date = boost::gregorian::from_simple_string(date_str);

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
        repo_name_to_repos[repo.name].push_back(repo.id);
    }


    // Children.  Only direct ones for the moment.
    for (unsigned i = 0;  i < repos.size();  ++i) {
        Repo & repo = repos[i];
        if (repo.id == -1) continue;  // invalid repo
        if (repo.parent == -1) continue;  // no parent
        repos[repo.parent].children.insert(i);
    }

    /* Expand all parents */
    bool need_another = true;
    int depth = 0;

    for (;  need_another;  ++depth) {
        need_another = false;
        for (unsigned i = 0;  i < repos.size();  ++i) {
            Repo & repo = repos[i];
            if (repo.id == -1) continue;  // invalid repo
            if (repo.depth != -1) continue;
            if (repo.parent == -1) {
                cerr << "repo id: " << i << endl;
                cerr << "repo: " << repo.name << endl;
                cerr << "depth: " << repo.depth << endl;
                cerr << "mydepth: " << depth << endl;
                throw Exception("logic error: parent invalid");
            }

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

#if 0
            if (depth > 1)
                cerr << "repo " << repo.id << " " << repo.name
                     << " has ancestors "
                     << repo.ancestors << endl;
#endif
        }
    }


    Parse_Context lang_file("download/lang.txt");

    languages.reserve(1000);

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

    int nlang = languages.size();

    // Convert the repo's languages into a distribution
    for (unsigned i = 0;  i < repos.size();  ++i) {
        Repo & repo = repos[i];
        if (repo.invalid()) continue;

        repo.language_vec.clear();
        repo.language_vec.resize(nlang);

        for (Repo::LanguageMap::const_iterator
                 it = repo.languages.begin(),
                 end = repo.languages.end();
             it != end;  ++it) {
            repo.language_vec[it->first] = it->second;
        }

        if (repo.total_loc != 0) repo.language_vec /= repo.total_loc;

        repo.language_2norm = repo.language_vec.two_norm();
        
    }

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
        user_entry.id = user_id;
    }

    calc_languages();

    calc_popularity();

    calc_density();

    users_to_test.reserve(5000);

    Parse_Context test_file("download/test.txt");

    while (test_file) {
        int user_id = test_file.expect_int();

        if (user_id < 0 || user_id >= users.size())
            test_file.exception("invalid user ID");

        test_file.expect_eol();

        users_to_test.push_back(user_id);
        users[user_id].incomplete = true;
        users[user_id].id = user_id;
    }

    stochastic_random_walk();

    frequency_stats();
}

struct FreqStats {
    distribution<int> buckets;
    distribution<int> counts;

    FreqStats()
    {
        buckets = boost::assign::list_of<int>(0)(1)(2)(3)(4)(5)(6)(7)(8)(9)(10)(12)(14)(16)(20)(40)(60)(100)(200)(500)(1000);
        counts.resize(buckets.size());
    }

    void add(int val, int count = 1)
    {
        unsigned i;
        for (i = 0;  i < buckets.size() && buckets[i] < val;  ++i) ;
        counts[i] += count;
    }

    void print(std::ostream & out) const
    {
        int total = counts.total();
        
        if (total == 0) return;

        int max = counts.max();
        for (unsigned i = 0;  i < buckets.size();  ++i) {
            out << format(" %4d %6d %6.3f%% %s\n",
                          buckets[i], counts[i], 100.0 * counts[i] / total,
                          string(50.0 * counts[i] / max, '*').c_str());
        }
    }
};

void
Data::
frequency_stats()
{
    set<int> test_users(users_to_test.begin(),
                        users_to_test.end());

    FreqStats users_with_n_repos;
    FreqStats incomplete_users_with_n_repos;
    FreqStats tested_users_with_n_repos;

    for (unsigned i = 0;  i < users.size();  ++i) {
        if (users[i].invalid()) continue;
        int nrepos = users[i].watching.size();
        users_with_n_repos.add(nrepos, 1);

        if (users[i].incomplete)
            incomplete_users_with_n_repos.add(nrepos, 1);

        if (test_users.count(i))
            tested_users_with_n_repos.add(nrepos, 1);
    }

    
    cerr << "users with n repos: " << endl;
    users_with_n_repos.print(cerr);
    cerr << endl;

    cerr << "incomplete users with n repos: " << endl;
    incomplete_users_with_n_repos.print(cerr);
    cerr << endl;

    cerr << "tested users with n repos: " << endl;
    tested_users_with_n_repos.print(cerr);
    cerr << endl;

    FreqStats repos_with_n_watchers;

    for (unsigned i = 0;  i < repos.size();  ++i) {
        if (repos[i].invalid()) continue;
        int nwatchers = repos[i].watchers.size();
        repos_with_n_watchers.add(nwatchers, 1);
    }

    cerr << "repos with n watchers: " << endl;
    repos_with_n_watchers.print(cerr);
    cerr << endl;
}

const vector<int> &
Data::
name_to_repos(const std::string & name) const
{
    Repo_Name_To_Repos::const_iterator found
        = repo_name_to_repos.find(name);
    if (found == repo_name_to_repos.end())
        throw Exception("repo name not in index");
    
    return found->second;
}

void
Data::
calc_languages()
{
    // Get the user's language preferences from his repos
    for (unsigned i = 0;  i < users.size();  ++i) {
        User & user = users[i];

        user.language_vec.clear();
        user.language_vec.resize(languages.size());

        for (IdSet::const_iterator
                 it = user.watching.begin(),
                 end = user.watching.end();
             it != end;  ++it) {
            const distribution<float> & repo_lang
                = repos[*it].language_vec;
            user.language_vec += repo_lang / user.watching.size();
        }

        user.language_2norm = user.language_vec.two_norm();
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
calc_density()
{
    int nusers = users.size();
    int nrepos = repos.size();

    int susers = nusers / DENSITY_USER_STEP;
    int srepos = nrepos / DENSITY_REPO_STEP;


    // Size the matrices
    density1.resize(boost::extents[susers + 2][srepos + 2]);
    density2.resize(boost::extents[susers + 2][srepos + 2]);

    // Clear counts
    for (unsigned i = 0;  i < susers + 2;  ++i)
        for (unsigned j = 0;  j < srepos + 2;  ++j)
            density1[i][j] = density2[i][j] = 0;

    unsigned max_count = 0;

    for (unsigned i = 0;  i < users.size();  ++i) {
        const User & user = users[i];
        //if (user.invalid()) continue;

        int user_id = i;

        int xuser1 = user_id / DENSITY_USER_STEP;
        int xuser2 = (user_id + DENSITY_USER_STEP / 2) / DENSITY_USER_STEP;

        for (IdSet::const_iterator
                 it = user.watching.begin(),
                 end = user.watching.end();
             it != end;  ++it) {
            int repo_id = *it;
            int yrepo1 = repo_id / DENSITY_REPO_STEP;
            int yrepo2 = (repo_id + DENSITY_REPO_STEP / 2) / DENSITY_REPO_STEP;

            max_count = max(max_count, density1[xuser1][yrepo1] += 1);
            max_count = max(max_count, density2[xuser2][yrepo2] += 1);
        }
    }

    cerr << "max_count = " << max_count << endl;
}

float
Data::
density(int user_id, int repo_id) const
{
    int xuser1 = user_id / DENSITY_USER_STEP;
    int xuser2 = (user_id + DENSITY_USER_STEP / 2) / DENSITY_USER_STEP;
    int yrepo1 = repo_id / DENSITY_REPO_STEP;
    int yrepo2 = (repo_id + DENSITY_REPO_STEP / 2) / DENSITY_REPO_STEP;

    return std::max(density1[xuser1][yrepo1], 
                    density2[xuser2][yrepo2]);
}

void
Data::
stochastic_random_walk()
{
    /* Get the baseline probabilities */

    distribution<double> repo_base, user_base;

    user_base.resize(users.size());
    for (unsigned i = 0;  i < users.size();  ++i) {
        //if (users[i].invalid()) continue;
        user_base[i] = 1.0;
    }

    double total_users = user_base.total();
    user_base /= total_users;

    repo_base.resize(repos.size());
    for (unsigned i = 0;  i < repos.size();  ++i) {
        if (repos[i].invalid()) continue;
        repo_base[i] = 1.0;
    }

    double total_repos = repo_base.total();
    repo_base /= total_repos;

    /* Initial conditions: equally probable to be on any user */
    user_prob = user_base;

    int niter = 20;

    for (int iter = 0;  iter < niter; ++iter) {
        //cerr << "iter " << iter << endl;

        // Calculate repo probabilities.  Each user has an equal probability
        // to go to each of the repos that (s)he watches.

        // Probability that they go to a random repo instead
        double prob_random_repo = 0.00;

        repo_prob = prob_random_repo * repo_base;

        for (unsigned i = 0;  i < users.size();  ++i) {
            const User & user = users[i];
            //if (user.invalid()) continue;
            if (user.watching.empty()) continue;

            // Amount to add for each of the user's repos
            double factor
                = (1.0 - prob_random_repo) * user_prob[i]
                / user.watching.size();
            
            for (IdSet::const_iterator
                     it = user.watching.begin(),
                     end = user.watching.end();
                 it != end;  ++it) {
                int repo_id = *it;
                repo_prob[repo_id] += factor;
            }
        }

        // Normalize in case of lost probability
        repo_prob.normalize();

        // TODO: exploit parent/child relationships between repos

        // Calculate the user probabilities.  Each repo has an even chance to
        // go to each of the watchers.

        double prob_random_user = 0.25;

        user_prob = user_base * prob_random_user;

        for (unsigned i = 0;  i < repos.size();  ++i) {
            const Repo & repo = repos[i];
            if (repo.invalid()) continue;
            if (repo.watchers.empty()) continue;

            // Amount to add for each of the repo's users
            double factor
                = (1.0 - prob_random_user) * repo_prob[i]
                / repo.watchers.size();
            
            for (IdSet::const_iterator
                     it = repo.watchers.begin(),
                     end = repo.watchers.end();
                 it != end;  ++it) {
                int user_id = *it;
                user_prob[user_id] += factor;
            }
        }

        user_prob.normalize();

#if 0
        cerr << "repos: max " << repo_prob.max() * total_repos
             << " min: " << repo_prob.min() * total_repos
             << endl;
        cerr << "users: max " << user_prob.max() * total_users
             << " min: " << user_prob.min() * total_users
             << endl;
#endif
    }

    vector<pair<int, double> > repos_ranked;
    for (unsigned i = 0;  i < repos.size();  ++i) {
        if (repos[i].invalid()) continue;
        repos_ranked.push_back(make_pair(i, repo_prob[i]));
    }

    sort_on_second_descending(repos_ranked);
    
#if 0
    cerr << "top 100 repos: " << endl;
    for (unsigned i = 0;  i < 100 && i < repos_ranked.size();  ++i) {
        int repo_id = repos_ranked[i].first;
        const Repo & repo = repos[repo_id];
        cerr << format("%3d %5d %6zd %8.6f %6d %s/%s\n",
                       i,
                       repo.popularity_rank,
                       repo.watchers.size(),
                       repos_ranked[i].second * total_repos,
                       repo_id,
                       authors[repo.author].name.c_str(),
                       repo.name.c_str());
    }
    cerr << endl;
#endif

    for (unsigned i = 0;  i < repos_ranked.size();  ++i) {
        Repo & repo = repos[i];
        repo.repo_prob = repos_ranked[i].second * total_users;
        repo.repo_prob_rank = i;
        repo.repo_prob_percentile = 1.0 * i / repos_ranked.size();
    }

    vector<pair<int, double> > users_ranked;
    for (unsigned i = 0;  i < users.size();  ++i) {
        //if (users[i].invalid()) continue;
        users_ranked.push_back(make_pair(i, user_prob[i]));
    }
    sort_on_second_descending(users_ranked);

#if 0    
    cerr << "top 100 users: " << endl;
    for (unsigned i = 0;  i < 100 && i < users_ranked.size();  ++i) {
        int user_id = users_ranked[i].first;
        const User & user = users[user_id];
        cerr << format("%3d %6zd %8.6f %6d\n",
                       i,
                       user.watching.size(),
                       users_ranked[i].second * total_users,
                       user_id);
    }
    cerr << endl;
#endif

    for (unsigned i = 0;  i < users_ranked.size();  ++i) {
        User & user = users[i];
        user.user_prob = users_ranked[i].second * total_users;
        user.user_prob_rank = i;
        user.user_prob_percentile = 1.0 * i / users_ranked.size();
    }
}

void
Data::
setup_fake_test(int nusers, int seed)
{
    srand(seed);

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
    
    vector<pair<int, int> > accum;

    // Modify the users, one by one
    for (unsigned i = 0;
         i < candidate_users.size() && accum.size() < nusers;
         ++i) {
        int user_id = candidate_users[i];
        User & user = users[user_id];
        user.id = user_id;

        // Select a repo to remove
        vector<int> all_watched(user.watching.begin(),
                                user.watching.end());

        std::random_shuffle(all_watched.begin(),
                            all_watched.end());

        for (unsigned j = 0; j < all_watched.size();  ++j) {
            int repo_id = all_watched[j];
            Repo & repo = repos[repo_id];

            // Don't remove if only one watcher
            if (repo.watchers.size() < 2)
                continue;

            repo.watchers.erase(user_id);
            user.watching.erase(repo_id);
            user.incomplete = true;

            accum.push_back(make_pair(user_id, repo_id));
            break;
        }
    }

    // Put them in user number order, in case that helps something...

    std::sort(accum.begin(), accum.end());

    answers.clear();
    answers.insert(answers.end(),
                   second_extractor(accum.begin()),
                   second_extractor(accum.end()));

    users_to_test.clear();
    users_to_test.insert(users_to_test.end(),
                         first_extractor(accum.begin()),
                         first_extractor(accum.end()));

    // Re-calculate derived data structures
    calc_popularity();
    calc_density();

    frequency_stats();
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

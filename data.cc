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


#include "utils/pair_utils.h"
#include "utils/vector_utils.h"
#include "utils/less.h"
#include "arch/exception.h"
#include "math/xdiv.h"
#include "stats/distribution_simd.h"


#include <backward/hash_map>
#include <boost/assign/list_of.hpp>

#include <fstream>


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

    authors.reserve(60000);

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

    Parse_Context author_file("authors.txt");

    while (author_file) {
        string author_name = author_file.expect_text(':', true);
        if (author_name == "") continue;

        int author_id = -1;

        if (!author_name_to_id.count(author_name)) {
            cerr << "warning: unseen author in file: " << author_name
                 << endl;
            // Unseen author... add it
            author_id = author_name_to_id[author_name] = authors.size();
            Author new_author;
            new_author.name = author_name;
            new_author.id = author_id;
            authors.push_back(new_author);
        }
        else author_id = author_name_to_id[author_name];
        
        if (author_id == -1)
            throw Exception("author not found");

        Author & author = authors[author_id];
    
        author_file.expect_literal(':');
        author.num_following = author_file.expect_int();
        author_file.expect_literal(',');
        author_file.expect_int();  // github ID; unused
        author_file.expect_literal(',');
        author.num_followers = author_file.expect_int();
        author_file.expect_literal(',');
        string date_str = author_file.expect_text("\n,", false);

        author.date = boost::gregorian::from_simple_string(date_str);
        //cerr << "date_str " << date_str << " date " << author.date
        //     << endl;

        author_file.expect_eol();
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

    calc_author_stats();
    
    infer_from_ids();

    calc_languages();

    calc_popularity();

    calc_density();

    calc_cooccurrences();

    stochastic_random_walk();

    frequency_stats();
}

struct FreqStats {
    distribution<int> buckets;
    distribution<int> counts;

    FreqStats()
    {
        buckets = boost::assign::list_of<int>(0)(1)(2)(3)(4)(5)(6)(7)(8)(9)(10)(12)(14)(16)(20)(40)(60)(100)(200)(500)(1000);
        counts.resize(buckets.size() + 1);
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
        for (unsigned i = 0;  i <= buckets.size();  ++i) {
            string range;
            if (i == 0) range = "0";
            else if (i == buckets.size())
                range = format("%d-", buckets[i - 1] + 1);
            else if (buckets[i - 1] + 1 == buckets[i]) {
                range = format("%d", buckets[i]);
            }
            else range = format("%d-%d", buckets[i - 1] + 1, buckets[i]);

            out << format(" %10s %6d %6.3f%% %s\n",
                          range.c_str(), counts[i], 100.0 * counts[i] / total,
                          string(50.0 * counts[i] / max, '*').c_str());
        }
    }
};

void
Data::
frequency_stats()
{
    return;

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

#if 0    
    cerr << "users with n repos: " << endl;
    users_with_n_repos.print(cerr);
    cerr << endl;

    cerr << "incomplete users with n repos: " << endl;
    incomplete_users_with_n_repos.print(cerr);
    cerr << endl;
#endif

    cerr << "tested users with n repos: " << endl;
    tested_users_with_n_repos.print(cerr);
    cerr << endl;

    FreqStats repos_with_n_watchers;

    for (unsigned i = 0;  i < repos.size();  ++i) {
        if (repos[i].invalid()) continue;
        int nwatchers = repos[i].watchers.size();
        repos_with_n_watchers.add(nwatchers, 1);
    }

#if 0
    cerr << "repos with n watchers: " << endl;
    repos_with_n_watchers.print(cerr);
    cerr << endl;
#endif
}

const Data::Name_Info &
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

void
Cooccurrences::
finish()
{
    std::sort(begin(), end());

    int unique = 0, last = -1;

    for (const_iterator it = this->begin(), end = this->end();
         it != end;  ++it) {
        if (it->with != last) {
            ++unique;
            last = it->with;
        }
    }

    // Create a new object so that the excess memory for duplicates will be
    // returned to the system
    Cooccurrences new_me;
    new_me.reserve(unique);

    for (const_iterator it = this->begin(), end = this->end();
         it != end; /* no inc */) {
        int key = it->with;

        double accum = 0.0;

        for (; it != end && it->with == key;  ++it)
            accum += it->score;

        new_me.add(key, accum);
    }

    if (new_me.size() != unique) {
        cerr << "size() = " << size() << endl;
        cerr << "unique = " << unique << endl;
        cerr << "new_me.size() = " << new_me.size() << endl;
        cerr << "input " << endl;
        for (const_iterator it = this->begin(), end = this->end();
             it != end;  ++it)
            cerr << "  " << it->with << "  " << it->score << endl;
        cerr << endl;
        cerr << "output" << endl;
        for (const_iterator it = new_me.begin(), end = new_me.end();
             it != end;  ++it)
            cerr << "  " << it->with << "  " << it->score << endl;
        
        throw Exception("logic error in cooccurrences");
    }

    swap(new_me);
}

void
Cooccurrences::
add(int with, float weight)
{
    push_back(Cooc_Entry(with, weight));
}

void
Data::
calc_cooccurrences()
{
    // Clear all of the cooccurrence sets
    for (unsigned i = 0;  i < users.size();  ++i) {
        users[i].cooc.clear();
        users[i].cooc2.clear();
    }

    // For each, find those that cooccur within the same set of predictions
    // Ignore those users that have too many predictions or those repos that
    // have too many watchers
    for (unsigned i = 0;  i < repos.size();  ++i) {
        const Repo & repo = repos[i];
        if (repo.invalid()) continue;
        if (repo.watchers.empty()) continue;

        // More than 20 means 1/400th or less of a point for each of 400 or
        // more, which uses lots of memory and doesn't make much difference.
        // So we simply skip these ones.
        if (repo.watchers.size() > 50) continue;

        // Weight it so that we give out a total of one point for each repo
        double wt1 = 1.0 / (repo.watchers.size() * repo.watchers.size());
        double wt2 = 1.0 / repo.watchers.size();
        
        for (IdSet::const_iterator
                 it = repo.watchers.begin(),
                 end = repo.watchers.end();
             it != end;  ++it) {
            int user_id1 = *it;
            for (IdSet::const_iterator
                     jt = boost::next(it);
                 jt != end;  ++jt) {
                int user_id2 = *jt;
                
                if (repo.watchers.size() <= 20) {
                    users[user_id1].cooc.add(user_id2, wt1);
                    users[user_id2].cooc.add(user_id1, wt1);
                }
                users[user_id1].cooc2.add(user_id2, wt2);
                users[user_id2].cooc2.add(user_id1, wt2);
            }
        }
    }

    // Finish all of the cooccurrence sets
    for (unsigned i = 0;  i < users.size();  ++i) {
        users[i].cooc.finish();
        users[i].cooc2.finish();
    }

    // Clear all of the cooccurrence sets
    for (unsigned i = 0;  i < repos.size();  ++i) {
        repos[i].cooc.clear();
        repos[i].cooc2.clear();
    }

    // For each, find those that cooccur within the same set of predictions
    // Ignore those repos that have too many predictions or those users that
    // have too many watchers
    for (unsigned i = 0;  i < users.size();  ++i) {
        const User & user = users[i];
        if (user.invalid()) continue;
        if (user.watching.empty()) continue;

        // More than 20 means 1/400th or less of a point for each of 400 or
        // more, which uses lots of memory and doesn't make much difference.
        // So we simply skip these ones.
        if (user.watching.size() > 50) continue;

        // Weight it so that we give out a total of one point for each user
        double wt1 = 1.0 / (user.watching.size() * user.watching.size());
        double wt2 = 1.0 / user.watching.size();

        for (IdSet::const_iterator
                 it = user.watching.begin(),
                 end = user.watching.end();
             it != end;  ++it) {
            int repo_id1 = *it;
            for (IdSet::const_iterator
                     jt = boost::next(it);
                 jt != end;  ++jt) {
                int repo_id2 = *jt;
                
                if (user.watching.size() <= 20) {
                    repos[repo_id1].cooc.add(repo_id2, wt1);
                    repos[repo_id2].cooc.add(repo_id1, wt1);
                }

                repos[repo_id1].cooc2.add(repo_id2, wt2);
                repos[repo_id2].cooc2.add(repo_id1, wt2);
            }
        }
    }

    // Finish all of the cooccurrence sets
    for (unsigned i = 0;  i < repos.size();  ++i) {
        repos[i].cooc.finish();
        repos[i].cooc2.finish();
    }
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
calc_author_stats()
{
    // NOTE: this isn't part of a real recommendation engine; it uses
    // characteristics of the provided GitHub dataset to infer a
    // correspondence between users and authors.

    for (unsigned i = 0;  i < authors.size();  ++i)
        authors[i].num_watchers = 0;
    
    for (unsigned i = 0;  i < repos.size();  ++i) {
        if (repos[i].invalid()) continue;
        if (repos[i].author == -1) continue;
        authors[repos[i].author].num_watchers += repos[i].watchers.size();
    }

    int valid_users = 0, inferred_users = 0, multiple_users = 0,
        total_multiple = 0;

    for (unsigned i = 0;  i < users.size();  ++i) {
        User & user = users[i];
        user.inferred_authors.clear();
        if (users[i].invalid()) continue;

        ++valid_users;

        hash_map<int, int> inferred_authors;

        for (IdSet::const_iterator
                 it = user.watching.begin(),
                 end = user.watching.end();
             it != end;  ++it) {
            const Repo & repo = repos[*it];
            if (repo.watchers.size() == 1)
                inferred_authors[repo.author] += 1;
        }

        if (inferred_authors.empty()) continue;

        if (inferred_authors.size() == 1)
            ++inferred_users;
        else {
            ++multiple_users;
            total_multiple += inferred_authors.size();
        }
        for (hash_map<int, int>::const_iterator
                 it = inferred_authors.begin(),
                 end = inferred_authors.end();
             it != end;  ++it)
            user.inferred_authors.insert(it->first);
    }

    for (Repo_Name_To_Repos::iterator
             it = repo_name_to_repos.begin(),
             end = repo_name_to_repos.end();
         it != end;  ++it) {
        it->second.num_watchers = 0;
        for (Name_Info::const_iterator
                 jt = it->second.begin(),
                 end = it->second.end();
             jt != end;  ++jt)
            it->second.num_watchers += repos[*jt].watchers.size();
    }
    
    cerr << "user inferring: valid " << valid_users
         << " inferred " << inferred_users
         << " multiple " << multiple_users
         << " average " << (1.0 * total_multiple / multiple_users) << endl;
}

// Cost-based way of doing it?  Including backtracking?


void
Data::
infer_from_ids()
{
    cerr << "inferring from IDs" << endl;

    /* There should be a monotonically increasing user ID that runs through
       the repos.

       If we look at the data by taking the lowest user ID for each of the
       repositories, we get something that looks like this:

       cat download/data.txt | \
       tr ':' ' ' \
       | awk '{ printf("%06d %06d\n", $2, $1); }' \
       | sort \
       | awk 'BEGIN { last = -1; } $1 != last { print; last = $1; }' \
       > lowest_user_for_each_repo.csv

       The data must have been created using the following algorithm:

       - Go through the repos in random order, assigning them IDs from 1 onwards
       - for each of the repos (in ID order 1, 2, ...)
           - Go through the watching users in a random order
           - If the watching user already had an ID, then add this ID to the
             repo's watchers list
           - Otherwise, create a new user ID that is one greater than the
             last one, and add this ID to the repo's watchers list

       If we look in this file, from line 1025 onwards (annotated with other
       information from data.txt):

         repo lowest other users watching
       ------ ------ --------------------
       001029 001080
       001030 000016  001081
       001031 000083  001082
       001032 001082
       001033 000055  (user 001083 is NOT watching 001033) <--- here
       001034 001595  (user 001083 is NOT watching 001034) <--- here
       001035 001084
       001036 000437  001085 
       001037 000212  001085 001086 (1087??)
       001038 000024  001088
       001039 000015  001088 001089
       001040 001090
       001041 001091  (1092??)
       001042 001093
       001043 001095
       001044 000091  001096 (1097?? 1098?? 1099?)
       001045 001100
       001046 000016  001101

       Users 1082, 1083, 1088 and 1094 are missing a repo in this sample.

       Note that user 1083 is missing from the list.  However, user 1084
       could not have been created without user 1083 being first created, so
       we can deduce that user 1083 must have been removed from either
       repo 1033 or repo 1034.

       We can further see that user 1083 must have been watching repo 1034, as
       if its lowest user number was 1595, then it could not possibly have been
       assigned the id 1034.

       Finally, there appear to be more watches missing than those that are
       just in the testing file that was provided.  Maybe there has been a
       (sneaky) extra set removed for further testing of the final solutions,
       or maybe there is something else that I don't know about.
    */
       
#if 0
    // Print repos from 1029 to 1046
    cerr << "repos" << endl;
    for (unsigned i = 1;  i <= 100;  ++i) {
        int repo_id = i;
        const Repo & repo = repos[repo_id];
        cerr << format("%6zd %6d %-30s",
                       repo.watchers.size(),
                       repo_id,
                       (authors[repo.author].name + "/" + repo.name).c_str());

        for (IdSet::const_iterator
                 it = repo.watchers.begin(),
                 end = repo.watchers.end();
             it != end;  ++it) {
            if (*it >= 1 && *it <= 100)
                cerr << " " << *it;
        }
        cerr << endl;
    }
    cerr << endl;

    // Print users from 1082 to 1100
    cerr << "users" << endl;
    for (unsigned i = 1;  i <= 100;  ++i) {
        const User & user = users[i];
        cerr << format("%4d %5d ", i, user.watching.size());

        if (user.incomplete) cerr << "* { ";
        else cerr << "  { ";

        for (IdSet::const_iterator
                 it = user.inferred_authors.begin(),
                 end = user.inferred_authors.end();
             it != end;  ++it) {
            cerr << authors.at(*it).name << " ";
        }
        cerr << "} ";

        for (IdSet::const_iterator
                 it = user.watching.begin(),
                 end = user.watching.end();
             it != end;  ++it) {
            if (*it >= 1 && *it <= 100)
                cerr << " " << *it;
        }
        cerr << endl;
    }

    cerr << endl;
#endif

    vector<int> user_for_repo(repos.size(), -1);

    // First, look for points where there is only one user watching the repo

    int last_user = 0, last_repo = 0;

    // Slope of the line.  It goes from 1 repo/user at the start to 2.5
    // repos/user at the end.
    // So, assume that it's 2.
    double slope = 2.0;

    int total_gap = 0, max_gap = 0, valid = 0, found = 0;

    for (unsigned i = 0;  i < repos.size();  ++i) {
        Repo & repo = repos[i];
        if (repo.invalid()) continue;
        ++valid;
        if (repo.watchers.size() != 1) continue;

        int u = *repo.watchers.begin();

        if (u <= last_user) continue;
        
        int predicted_u = last_user + (i - last_repo) / slope;

        //int u_diff = u - predicted_u;

        if (u > predicted_u + 50) continue;

        ++found;

        //cerr << "repo " << i << " user " << u << endl;

        int gap = i - last_repo;
        total_gap += gap;
        max_gap = std::max(gap, max_gap);

        User & user = users[u];

        user.corresponding_repo.insert(i);
        repo.corresponding_user.insert(u);

        //refine_mapping(last_user, user


        for (unsigned u2 = last_user;  u2 <= u;  ++u2) {
            users[u2].min_repo = last_repo;
            users[u2].max_repo = i;
        }
        user.min_repo = i;

        for (unsigned i2 = last_repo;  i2 <= i;  ++i2) {
            repos[i2].min_user = last_user;
            repos[i2].max_user = u;
        }
        repo.min_user = u;

        last_user = u;  last_repo = i;
    }
    
    cerr << format("found %d/%d=%.2f%%, gap max %d avg %.2f",
                   found, valid, 100.0 * found / valid,
                   max_gap, total_gap * 1.0 / found)
         << endl;

    ofstream out("match_results.txt");

    for (unsigned i = 0;  i < users_to_test.size();  ++i) {
        vector<int> results;

        const User & user = users[users_to_test[i]];
        cerr << "user " << user.id << " min_repo " << user.min_repo
             << " max_repo " << user.max_repo << " min watcher "
             << (user.watching.size() ? *user.watching.begin() : -1)
             << endl;

        // Find which of the repos could match up
        for (unsigned r = user.min_repo;  r <= user.max_repo;  ++r) {
            const Repo & repo = repos[r];
            cerr << "    repo " << repo.id << " min_user " << repo.min_user
                 << " max_user " << repo.max_user << " min watching "
                 << (repo.watchers.size() ? *repo.watchers.begin() : -1);
            if (repo.watchers.size()
                && *repo.watchers.begin() > repo.max_user) {
                cerr << " ******* ";
                results.push_back(r);
            }
            cerr << endl;
        }

        if (results.empty() && user.watching.size()
            && *user.watching.begin() <= user.max_repo)
            continue;

        for (unsigned r = user.min_repo;  r <= user.max_repo && results.size() < 10;  ++r) {
            const Repo & repo = repos[r];
            if (repo.watchers.size()
                && *repo.watchers.begin() > repo.max_user) {
                // do nothing; results already there
            }
            else results.push_back(r);
        }

        if (results.size() > 10)
            results.erase(results.begin() + 10, results.end());

        if (results.empty()) continue;

        out << users_to_test[i] << ":";
        for (unsigned i = 0;  i < results.size();  ++i) {
            if (i != 0) out << ",";
            out << results[i];
        }
        out << endl;
    }
    
    exit(0);  // for now...
    
#if 0
    last_user = 0;  last_repo = 0;

    for (unsigned i = 0;  i < users.size();  ++i) {
        const User & user = users[i];
        if (user.invalid()) continue;
        if (user.watching.size() != 1) continue;

        int r = *user.watching.begin();

        if (r <= last_repo) continue;
        
        int predicted_r = last_repo + (i - last_user) * slope;

        //int u_diff = u - predicted_u;

        if (r > predicted_r + 50) continue;

        cerr << "user " << i << " repo " << r << endl;

        last_user = i;  last_repo = r;
    }
#endif

#if 0

    // Look for known points
    for (unsigned i = 0;  i < repos.size();  ++i) {
        if (i % 1000 == 0) cerr << i << endl;
        const Repo & repo = repos[i];
        if (repo.invalid()) continue;
        if (repo.watchers.empty()) continue;

        bool debug = true;//(i > 2000);

        if (debug) {
            cerr << endl;
            
            cerr << i << " " << u << " nnf " << num_not_found << endl;
            
            cerr << format("          repo %6d %6zd %-30s",
                           i, 
                           repo.watchers.size(),
                           (authors[repo.author].name + "/" + repo.name).c_str());
            
            for (IdSet::const_iterator
                     it = repo.watchers.begin(),
                     end = repo.watchers.end();
                 it != end;  ++it) {
                if (*it >= u - 20 && *it <= u + 20)
                    cerr << " " << *it;
            }
            cerr << endl;
            
            const User & user = users[u];
            cerr << format("          user %6d %6d ", u, user.watching.size());
            
            if (user.incomplete) cerr << "* { ";
            else cerr << "  { ";
            
            for (IdSet::const_iterator
                     it = user.inferred_authors.begin(),
                     end = user.inferred_authors.end();
                 it != end;  ++it) {
                cerr << authors.at(*it).name << " ";
            }
            cerr << "} ";
            
            for (IdSet::const_iterator
                     it = user.watching.begin(),
                     end = user.watching.end();
                 it != end;  ++it) {
                if (*it >= (i - 20) && *it <= (i + 20))
                    cerr << " " << *it;
            }
            cerr << endl;
            
            cerr << "         ";
        }

        // Find the first user ID at or above the current one
        const IdSet & rusers = repo.watchers;

        IdSet::const_iterator it
            = std::lower_bound(rusers.begin(), rusers.end(), u);

        if (*it - u <= 5 && *it - u >= 5) {
            // If the lowest entry is higher than the current one by not too
            // much, then we can be pretty sure that the user was introduced
            // here.
            if (debug) cerr << " found new entry " << *it;
            u = *it;
            num_not_found = 0;
            restart_point = i;

            user_for_repo[i] = u;
        }
        else {
            // Not found
            if (debug) {
                cerr << " not found";
                if (it != rusers.end())
                    cerr << " " << *it;
            }
            ++num_not_found;

            if (*it - u >= 100) {
                if (debug) cerr << " *** incomplete ***";
            }

            if (num_not_found == 5) {
                // restart with next
                i = restart_point - 1;
                ++u;
                num_not_found = 0;
                if (debug) cerr << " *** restart *** ";
            }
        }

        if (it != rusers.begin()) {
            --it;
            if (debug) cerr << " prev: " << *it;
        }
        if (debug) cerr << endl;
    }

    cerr << "at end: u = " << u << endl;

#elif 0

    for (unsigned i = 0;  i < repos.size() && i < 1100;  ++i) {
        const Repo & repo = repos[i];
        if (repo.invalid()) continue;
        if (repo.watchers.empty()) continue;

        cerr << i << " " << u << " nnf " << num_not_found;

        // Find the first user ID at or above the current one
        const IdSet & rusers = repo.watchers;

        IdSet::const_iterator it
            = std::lower_bound(rusers.begin(), rusers.end(), u);

        if (it != rusers.end() && *it == u) {
            // If we found it, we are OK
            cerr << " found " << *it;
            ++u;
            while (u < users.size() && users[u].invalid()) ++u;
            num_not_found = 0;
            restart_point = u;
        }
        else if (it == rusers.begin() && *it - u <= 5) {
            // If the lowest entry is higher than the current one by not too
            // much, then we can be pretty sure that the user was introduced
            // here.
            cerr << " found new entry " << *it;
            u = *it;
            num_not_found = 0;
            restart_point = i;
        }
        else {
            // Not found
            cerr << " not found";
            if (it != rusers.end())
                cerr << " " << *it;
            ++num_not_found;

            if (num_not_found == 5) {
                // restart with next
                i = restart_point - 1;
                ++u;
                num_not_found = 0;
                cerr << " *** restart *** ";
            }
        }

        if (it != rusers.begin()) {
            --it;
            cerr << " prev: " << *it;
        }
        cerr << endl;
    }
#endif
}

void
Data::
setup_fake_test(int nusers, int seed)
{
    srand(seed);

#if 0
    // First, we put all watches in a list
    vector<pair<int, int> > all_watches;
    all_watches.reserve(500000);

    for (unsigned i = 0;  i < users.size();  ++i) {
        const User & user = users[i];
        if (user.incomplete) continue;
        
        for (IdSet::const_iterator
                 it = user.watching.begin(),
                 end = user.watching.end();
             it != end;  ++it) {
            const Repo & repo = repos[*it];

            // Don't allow it to lose all watchers
            if (repo.watchers.size() == 1)
                continue;
            
            all_watches.push_back(make_pair(i, *it));
        }
    }
    
    std::random_shuffle(all_watches.begin(), all_watches.end());

    set<int> test_users;
    vector<pair<int, int> > accum;
    
    for (unsigned i = 0;  i < all_watches.size() && test_users.size() < nusers;
         ++i) {
        int user_id = all_watches[i].first;

        // Don't take more than one out from a user
        if (test_users.count(user_id)) continue;
        
        User & user = users[user_id];

        int repo_id = all_watches[i].second;

        Repo & repo = repos[repo_id];
        
        repo.watchers.erase(user_id);
        user.watching.erase(repo_id);
        user.incomplete = true;

        accum.push_back(make_pair(user_id, repo_id));
        test_users.insert(user_id);
    }
#else

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
#endif

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
    calc_author_stats();
    calc_cooccurrences();
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

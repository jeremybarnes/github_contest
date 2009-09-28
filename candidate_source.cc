/* candidate_source.cc
   Jeremy Barnes, 26 August 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   Source for candidates, implementation.
*/

#include "candidate_source.h"
#include "utils/less.h"
#include "math/xdiv.h"
#include "ranker.h"
#include "utils/hash_set.h"


using namespace std;
using namespace ML;


/*****************************************************************************/
/* RANKED                                                                    */
/*****************************************************************************/

Ranked::
Ranked(const IdSet & idset)
{
    for (IdSet::const_iterator it = idset.begin(), end = idset.end();
         it != end;  ++it) {
        Ranked_Entry entry;
        entry.repo_id = *it;
        push_back(entry);
    }
}

struct Compare_Ranked_Entries {
    bool operator () (const Ranked_Entry & e1,
                      const Ranked_Entry & e2)
    {
        if (!isfinite(e1.score) || !isfinite(e2.score))
            throw Exception("sorting non-finite values");

        return less_all(e2.score, e1.score,
                        e2.repo_id, e1.repo_id);
    }
};

void
Ranked::
sort()
{
    std::sort(begin(), end(), Compare_Ranked_Entries());

    // Put in the rank
    float last_score = INFINITY;
    int start_with_score = -1;

    for (unsigned i = 0;  i < size();  ++i) {
        Ranked_Entry & entry = operator [] (i);
        
        if (entry.score == last_score) {
            // same score
            entry.min_rank = start_with_score;
        }
        else {
            assert(entry.score < last_score);
            // Fill in max rank
            for (int j = start_with_score;  j < i && j > 0;  ++j)
                operator [] (j).max_rank = i;

            // Start of a range of scores
            last_score = entry.score;
            start_with_score = i;
            entry.min_rank = start_with_score;
        }
    }

    // Fill in max rank for those at the end
    for (int j = start_with_score;  j < size() && j > 0;  ++j)
        operator [] (j).max_rank = size();
}



/*****************************************************************************/
/* CANDIDATE_SOURCE                                                          */
/*****************************************************************************/

Candidate_Source::
Candidate_Source(const std::string & type, int id)
    : type_(type), id_(id), load_data(false)
{
}

Candidate_Source::
~Candidate_Source()
{
}

void
Candidate_Source::
configure(const ML::Configuration & config_,
          const std::string & name)
{
    Configuration config(config_, name, Configuration::PREFIX_APPEND);

    this->name_ = name;

    config.require(classifier_file, "classifier_file");
    
    load_data = true;
    config.find(load_data, "load_data");

    max_entries = 100;
    config.find(max_entries, "max_entries");

    min_prob = 0.0;
    config.find(min_prob, "min_prob");
}

void
Candidate_Source::
init()
{
    our_fs = feature_space();

    if (load_data) {
        
        classifier.load(classifier_file);
        
        classifier_fs = classifier.feature_space<ML::Dense_Feature_Space>();
        
        opt_info = classifier.impl->optimize(classifier_fs->features());
        
        classifier_fs->create_mapping(*our_fs, mapping);
        
        vector<ML::Feature> classifier_features
            = classifier.all_features();
    }
}

ML::Dense_Feature_Space
Candidate_Source::
specific_feature_space() const
{
    //By default, no specific features

    Dense_Feature_Space result;
    return result;
}

Dense_Feature_Space
Candidate_Source::
common_feature_space()
{
    ML::Dense_Feature_Space result;

    result.add_feature("density", REAL);
    result.add_feature("user_id", REAL);
    result.add_feature("user_repo_id_ratio", REAL);
    result.add_feature("user_watched_repos", REAL);
    result.add_feature("repo_watched_users", REAL);
    result.add_feature("repo_lines_of_code", REAL);
    result.add_feature("user_prob", REAL);
    result.add_feature("user_prob_rank", REAL);
    result.add_feature("repo_prob", REAL);
    result.add_feature("repo_prob_rank", REAL);
    result.add_feature("user_repo_prob", REAL);
    result.add_feature("repo_has_parent", REAL);
    result.add_feature("repo_num_children", REAL);
    result.add_feature("repo_num_ancestors", REAL);
    result.add_feature("repo_num_siblings", REAL);
    result.add_feature("repo_parent_watchers", REAL);

    return result;
}

boost::shared_ptr<const ML::Dense_Feature_Space>
Candidate_Source::
feature_space() const
{
    boost::shared_ptr<ML::Dense_Feature_Space> 
        result(new ML::Dense_Feature_Space());
    result->add(common_feature_space());
    result->add(specific_feature_space());
    return result;
}

void
Candidate_Source::
common_features(distribution<float> & result,
                int user_id, int repo_id, const Data & data,
                Candidate_Data & candidate_data)
{
    const User & user = data.users[user_id];
    const Repo & repo = data.repos[repo_id];

    result.clear();

    result.push_back(data.density(user_id, repo_id));
    result.push_back(user_id);
    result.push_back(xdiv<float>(user_id, repo_id));

    result.push_back(user.watching.size());
    result.push_back(repo.watchers.size());
    result.push_back(log(repo.total_loc + 1));
    
    result.push_back(user.user_prob);
    result.push_back(user.user_prob_rank);
    result.push_back(repo.repo_prob);
    result.push_back(repo.repo_prob_rank);
    result.push_back(user.user_prob * repo.repo_prob);
    
    result.push_back(repo.parent != -1);
    result.push_back(repo.children.size());
    result.push_back(repo.ancestors.size());

    if (repo.parent == -1) {
        result.push_back(0);
        result.push_back(-1);
    }
    else {
        result.push_back(data.repos[repo.parent].children.size());
        result.push_back(data.repos[repo.parent].watchers.size());
    }
}

namespace {

struct Source_Stats {
    Source_Stats()
        : total_size(0), n(0), correct(0), max_size(0),
          total_corr(0.0), total_incorr(0)
    {
    }

    size_t total_size;
    size_t n;
    size_t correct;
    size_t max_size;
    size_t already_watched;
    double total_corr, total_incorr;
};

map<string, Source_Stats> source_stats;
Lock stats_lock;

struct PrintSourceStats {
    ~PrintSourceStats()
    {
        cerr << "candidate source      nfired  correct already      nentries maxsz"
             << endl;
        for (map<string, Source_Stats>::const_iterator
                 it = source_stats.begin(),
                 end = source_stats.end();
             it != end;  ++it) {
            const Source_Stats & s = it->second;
            cerr << format("%-30s %7zd %5zd(%5.2f%%) %5zd %7zd(%7.2f) %7.4f %7zd\n",
                           it->first.c_str(),
                           s.n,
                           s.correct,
                           100.0 * s.correct / s.n,
                           s.already_watched,
                           s.total_size,
                           1.0 * s.total_size / s.n,
                           100.0 * (s.correct + s.already_watched) / s.total_size,
                           s.max_size);
        }
        cerr << endl;
    }
} print_source_stats;

} // file scope

void
Candidate_Source::
gen_candidates(Ranked & entries, int user_id, const Data & data,
               Candidate_Data & candidate_data) const
{
    // Get them, unranked
    candidate_set(entries, user_id, data, candidate_data);

    int ncorrect = 0, nalready = 0;

    // For each, get the features and run the classifier
    for (unsigned i = 0;  i < entries.size();  ++i) {
        
        if (entries[i].repo_id == correct_repo) ++ncorrect;
        if (watching && watching->count(entries[i].repo_id)) ++nalready;
        
        distribution<float> features;
        features.reserve(our_fs->variable_count());
        common_features(features, user_id, entries[i].repo_id, data,
                        candidate_data);

        features.insert(features.end(),
                        entries[i].features.begin(),
                        entries[i].features.end());
       
        float encoded[classifier_fs->variable_count()];
        classifier_fs->encode(&features[0], encoded, *our_fs, mapping);
        float score = classifier.impl->predict(1, encoded, opt_info);
        entries[i].score = score;
    }
    
    // Rank them
    entries.sort();

    for (unsigned i = 0;  i < entries.size();  ++i)
        entries[i].keep = i < max_entries && entries[i].score >= min_prob;

    Guard guard(stats_lock);
    Source_Stats & stats = source_stats[name()];

    stats.total_size += entries.size();
    if (!entries.empty())
        ++stats.n;

    stats.correct += ncorrect;
    stats.already_watched += nalready;
}


/*****************************************************************************/
/* CANDIDATE_SOURCE IMPLEMENTATIONS                                          */
/*****************************************************************************/

struct Ancestors_Of_Watched_Source : public Candidate_Source {
    Ancestors_Of_Watched_Source()
        : Candidate_Source("ancestors_of_watched", 0)
    {
    }

    virtual void candidate_set(Ranked & result, int user_id, const Data & data,
                               Candidate_Data & candidate_data) const
    {
        const User & user = data.users[user_id];

        IdSet ancestors;
        IdSet parents;

        for (IdSet::const_iterator
                 it = user.watching.begin(),
                 end = user.watching.end();
             it != end;  ++it) {
            int watched_id = *it;
            const Repo & watched = data.repos[watched_id];
            if (watched.parent == -1) continue;
            parents.insert(watched.parent);
        }

        for (IdSet::const_iterator
                 it = user.watching.begin(),
                 end = user.watching.end();
             it != end;  ++it) {
            int watched_id = *it;
            const Repo & watched = data.repos[watched_id];
            if (watched.parent == -1) continue;
            ancestors.insert(watched.all_ancestors.begin(),
                             watched.all_ancestors.end());
        }

        ancestors.erase(parents);

        ancestors.finish();

        result = ancestors;
    }
};

struct Authored_By_Me_Source : public Candidate_Source {
    Authored_By_Me_Source()
        : Candidate_Source("authored_by_me", 12)
    {
    }

    virtual void candidate_set(Ranked & result, int user_id, const Data & data,
                               Candidate_Data & candidate_data) const
    {
        const User & user = data.users[user_id];

        IdSet authors;

        for (IdSet::const_iterator
                 it = user.inferred_authors.begin(),
                 end = user.inferred_authors.end();
             it != end;  ++it) {
            const Author & author = data.authors[*it];

            for (IdSet::const_iterator
                     jt = author.repositories.begin(),
                     jend = author.repositories.end();
                 jt != jend;  ++jt)
                if (!user.watching.count(*jt))
                    authors.insert(*jt);
        }

        authors.finish();

        result = authors;
    }
};

struct Children_Of_Watched_Source : public Candidate_Source {
    Children_Of_Watched_Source()
        : Candidate_Source("children_of_watched", 1)
    {
    }

    virtual void candidate_set(Ranked & result, int user_id, const Data & data,
                               Candidate_Data & candidate_data) const
    {
        const User & user = data.users[user_id];

        IdSet watched_children;

        for (IdSet::const_iterator
                 it = user.watching.begin(),
                 end = user.watching.end();
             it != end;  ++it) {
            int watched_id = *it;
            const Repo & watched = data.repos[watched_id];
    
            watched_children.insert(watched.children.begin(),
                                    watched.children.end());
        }

        watched_children.finish();

        result = watched_children;
    }
};

struct Cooc_Source : public Candidate_Source {
    Cooc_Source()
        : Candidate_Source("cooc", 2), source(1)
    {
        called = total_coocs = max_coocs = 0;
    }

    ~Cooc_Source()
    {
#if 0
        cerr << "cooc source " << name() << endl;
        cerr << "called " << called << " times" << endl;
        cerr << "total coocs " << total_coocs << endl;
        cerr << "avg coocs " << 1.0 * total_coocs / called << " coocs"
             << endl;
#endif
    }

    int source;
    mutable int called, total_coocs, max_coocs;

    virtual void configure(const ML::Configuration & config_,
                           const std::string & name)
    {
        Candidate_Source::configure(config_, name);

        Configuration config(config_, name, Configuration::PREFIX_APPEND);
        source = 1;
        config.get(source, "source");

        cerr << "candidate source name=" << this->name() << " source = "
             << source << endl;
    }

    virtual ML::Dense_Feature_Space
    specific_feature_space() const
    {
        Dense_Feature_Space result;
        result.add_feature("cooc_total_score", REAL);
        result.add_feature("cooc_max_score", REAL);
        result.add_feature("cooc_avg_score", REAL);
        result.add_feature("cooc_num_scores", REAL);
        return result;
    }

    struct Cooc_Info {
        Cooc_Info()
            : total_score(0.0f), max_score(0.0f), n(0)
        {
        }

        float total_score;
        float max_score;
        int n;

        void operator += (float other_score)
        {
            n += 1;
            total_score += other_score;
            max_score = std::max(max_score, other_score);
        }
    };

    virtual void candidate_set(Ranked & result, int user_id, const Data & data,
                               Candidate_Data & candidate_data) const
    {
        const User & user = data.users[user_id];

        ++called;

        // Find cooccurring with the most specicivity

        hash_map<int, Cooc_Info> coocs_map;

        hash_set<string> watched_repo_names;
        hash_set<int> watched_authors;
        
        for (IdSet::const_iterator
                 it = user.watching.begin(),
                 end = user.watching.end();
             it != end;  ++it) {
            int repo_id = *it;
            const Repo & repo = data.repos[repo_id];

            watched_repo_names.insert(repo.name);
            watched_authors.insert(repo.author);

            const Cooccurrences & cooc
                = (source == 1 ? repo.cooc : repo.cooc2);
            
            for (Cooccurrences::const_iterator
                     jt = cooc.begin(), end = cooc.end();
                 jt != end;  ++jt) {
                if (data.repos[jt->with].watchers.size() < 2) continue;
                coocs_map[jt->with] += jt->score;
            }
        }

        result.reserve(coocs_map.size());

        for (hash_map<int, Cooc_Info>::const_iterator
                 it = coocs_map.begin(),
                 end = coocs_map.end();
             it != end;  ++it) {

            const Repo & repo = data.repos[it->first];
            if (watched_repo_names.count(repo.name)) continue;  // will be handled by same name
            if (watched_authors.count(repo.author)) continue;   // will be handled by same author

            result.push_back(Ranked_Entry());

            Ranked_Entry & entry = result.back();
            entry.repo_id = it->first;
            entry.features.reserve(4);
            entry.features.push_back(it->second.total_score);
            entry.features.push_back(it->second.max_score);
            entry.features.push_back(it->second.total_score / it->second.n);
            entry.features.push_back(it->second.n);
        }

        total_coocs += coocs_map.size();
        max_coocs = std::max<int>(max_coocs, coocs_map.size());
    }
};
 
struct In_Cluster_Repo_Source : public Candidate_Source {
    In_Cluster_Repo_Source()
        : Candidate_Source("in_cluster_repo", 3)
    {
    }

    virtual ML::Dense_Feature_Space
    specific_feature_space() const
    {
        Dense_Feature_Space result;
        result.add_feature("rcluster_num_watched_in_cluster",
                           REAL);
        result.add_feature("rcluster_prop_watched_in_cluster",
                           REAL);
        result.add_feature("rcluster_rank_in_cluster",
                           REAL);
        result.add_feature("rcluster_best_dp_in_cluster",
                           REAL);
        result.add_feature("rcluster_best_norm_dp_in_cluster",
                           REAL);
        result.add_feature("rcluster_best_keyword_dp_in_cluster",
                           REAL);
        result.add_feature("rcluster_best_norm_keyword_dp_in_cluster",
                           REAL);
        return result;
    }

    virtual void candidate_set(Ranked & result,
                               int user_id, const Data & data,
                               Candidate_Data & candidate_data) const
    {
        const User & user = data.users[user_id];

        // Number of entries for this user in each cluster
        hash_map<int, IdSet> clusters;

        hash_set<string> watched_repo_names;
        hash_set<int> watched_authors;

        for (IdSet::const_iterator
                 it = user.watching.begin(),
                 end = user.watching.end();
             it != end;  ++it) {
            int repo_id = *it;
            const Repo & repo = data.repos[repo_id];
            int cluster_id = repo.kmeans_cluster;

            watched_repo_names.insert(repo.name);
            watched_authors.insert(repo.author);

            if (cluster_id == -1) continue;
            clusters[cluster_id].insert(repo_id);
        }

        for (hash_map<int, IdSet>::iterator
                 it = clusters.begin(),
                 end = clusters.end();
             it != end;  ++it) {

            int cluster_id = it->first;

            const Cluster & cluster = data.repo_clusters[cluster_id];

            for (unsigned i = 0;  i < cluster.top_members.size();  ++i) {
                int repo_id = cluster.members[i];
                if (repo_id == -1) continue;
                if (data.repos[repo_id].invalid()) continue;
                if (user.watching.count(repo_id)) continue;

                const Repo & repo = data.repos[repo_id];
                if (watched_repo_names.count(repo.name)) continue;  // will be handled by same name
                if (watched_authors.count(repo.author)) continue;   // will be handled by same author

                result.push_back(Ranked_Entry());
                Ranked_Entry & entry = result.back();
                entry.score = data.repos[repo_id].watchers.size();
                entry.repo_id = repo_id;
                entry.features.reserve(5);
                entry.features.push_back(it->second.size());
                entry.features.push_back(xdiv<float>(it->second.size(),
                                                     user.watching.size()));
                entry.features.push_back(i);

                float best_dp = -2.0, best_dp_norm = -2.0;
                // Find the best DP with a cluster member

                float best_dp_kw = -2.0, best_dp_norm_kw = -2.0;

                for (IdSet::const_iterator
                         jt = it->second.begin(),
                         jend = it->second.end();
                     jt != jend;  ++jt) {
                    if (*jt == -1) continue;
                    const Repo & repo2 = data.repos[*jt];
                    float dp = repo.singular_vec.dotprod(repo2.singular_vec);
                    float dp_norm
                        = xdiv(dp, repo.singular_2norm * repo2.singular_2norm);
                    best_dp = max(best_dp, dp);
                    best_dp_norm = max(best_dp_norm, dp_norm);

                    dp = repo.keyword_vec.dotprod(repo2.keyword_vec);
                    dp_norm
                        = xdiv(dp, repo.keyword_vec_2norm * repo2.keyword_vec_2norm);
                    best_dp_kw = max(best_dp_kw, dp);
                    best_dp_norm_kw = max(best_dp_norm_kw, dp_norm);
                }

                entry.features.push_back(best_dp);
                entry.features.push_back(best_dp_norm);
                entry.features.push_back(best_dp_kw);
                entry.features.push_back(best_dp_norm_kw);
            }
        }

        // Add the top 2000 only
        result.sort();

        if (result.size() > 2000)
            result.erase(result.begin() + 2000, result.end());
    }
};

struct In_Cluster_User_Source : public Candidate_Source {
    In_Cluster_User_Source()
        : Candidate_Source("in_cluster_user", 4)
    {
    }

    virtual ML::Dense_Feature_Space
    specific_feature_space() const
    {
        Dense_Feature_Space result;
        result.add_feature("ucluster_num_watchers", REAL);
        result.add_feature("ucluster_watcher_score", REAL);
        result.add_feature("ucluster_highest_dp", REAL);
        result.add_feature("ucluster_highest_dp_norm", REAL);
        return result;
    }

    struct Rank_Info {
        Rank_Info()
            : num_watched(0), watched_score(0.0f), highest_dp(-2.0f),
              highest_dp_norm(-2.0f)
        {
        }

        bool operator < (const Rank_Info & other) const
        {
            return num_watched > other.num_watched;
        }

        int num_watched;
        float watched_score;
        float highest_dp;
        float highest_dp_norm;
    };

    virtual void candidate_set(Ranked & result, int user_id,
                               const Data & data,
                               Candidate_Data & candidate_data) const
    {
        const User & user = data.users[user_id];
        IdSet in_cluster_user;

        // Find repos watched by other users in the same cluster
        hash_map<int, Rank_Info> watched_by_cluster_user;

        int clusterno = user.kmeans_cluster;

        if (clusterno == -1) return;

        hash_set<string> watched_repo_names;
        hash_set<int> watched_authors;

        for (IdSet::const_iterator
                 it = user.watching.begin(),
                 end = user.watching.end();
             it != end;  ++it) {
            int repo_id = *it;
            const Repo & repo = data.repos[repo_id];
            watched_repo_names.insert(repo.name);
            watched_authors.insert(repo.author);
        }

        const Cluster & cluster = data.user_clusters[clusterno];

        for (unsigned i = 0;  i < cluster.members.size();  ++i) {
            int user_id2 = cluster.members[i];
            if (user_id2 == user_id) continue;

            const User & user2 = data.users[user_id2];
            if (user2.invalid()) continue;
            
            float dp = user.singular_vec.dotprod(user2.singular_vec);
            float dp_norm
                = xdiv<float>(dp, user.singular_2norm * user2.singular_2norm);

            for (IdSet::const_iterator
                     it = user2.watching.begin(),
                     end = user2.watching.end();
                 it != end;  ++it) {

                const Repo & repo = data.repos[*it];
                if (watched_repo_names.count(repo.name)) continue;  // will be handled by same name
                if (watched_authors.count(repo.author)) continue;   // will be handled by same author

                Rank_Info & entry = watched_by_cluster_user[*it];
                entry.num_watched += 1;
                entry.watched_score += 1.0 / user2.watching.size();
                entry.highest_dp = max(entry.highest_dp, dp);
                entry.highest_dp_norm = max(entry.highest_dp_norm, dp_norm);
            }
        }

        vector<pair<int, Rank_Info> > ranked(watched_by_cluster_user.begin(),
                                             watched_by_cluster_user.end());

        sort_on_second_ascending(ranked);
        
        result.reserve(min<int>(2000, ranked.size()));

        for (unsigned i = 0;
             result.size() < 2000 && i < ranked.size();
             ++i) {
            int repo_id = ranked[i].first;
            if (repo_id == -1) continue;  // just in case...
            const Rank_Info & info = ranked[i].second;

            if (user.watching.count(repo_id)) continue;

            result.push_back(Ranked_Entry());
            Ranked_Entry & entry = result.back();
            entry.repo_id = repo_id;
            entry.features.reserve(4);
            entry.features.push_back(info.num_watched);
            entry.features.push_back(info.watched_score);
            entry.features.push_back(info.highest_dp);
            entry.features.push_back(info.highest_dp_norm);
        }
    }
};

struct In_Id_Range_Source : public Candidate_Source {
    In_Id_Range_Source()
        : Candidate_Source("in_id_range", 5)
    {
    }

    virtual void candidate_set(Ranked & result, int user_id, const Data & data,
                               Candidate_Data & candidate_data) const
    {
        const User & user = data.users[user_id];

        IdSet in_id_range;

        // Find which of the repos could match up
        for (int r = user.min_repo;  r <= user.max_repo;  ++r) {
            if (r == -1) break;
            const Repo & repo = data.repos[r];
            if (repo.invalid()) continue;
            in_id_range.insert(r);
        }

        in_id_range.finish();

        result = in_id_range;
    }
};

struct Parents_Of_Watched_Source : public Candidate_Source {
    Parents_Of_Watched_Source()
        : Candidate_Source("parents_of_watched", 6)
    {
    }

    virtual void candidate_set(Ranked & result, int user_id, const Data & data,
                               Candidate_Data & candidate_data) const
    {
        const User & user = data.users[user_id];

        IdSet parents_of_watched;

        for (IdSet::const_iterator
                 it = user.watching.begin(),
                 end = user.watching.end();
             it != end;  ++it) {
            int watched_id = *it;
            const Repo & watched = data.repos[watched_id];
    
            if (watched.parent == -1) continue;
        
            parents_of_watched.insert(watched.parent);
        }

        parents_of_watched.finish();

        result = parents_of_watched;
    }
};

struct By_Watched_Author_Source : public Candidate_Source {
    By_Watched_Author_Source()
        : Candidate_Source("by_watched_author", 7)
    {
    }

    virtual ML::Dense_Feature_Space
    specific_feature_space() const
    {
        Dense_Feature_Space result;
        result.add_feature("author_already_watched_num", REAL);
        result.add_feature("author_unwatched_num", REAL);
        result.add_feature("author_already_watched_prop",
                           REAL);
        result.add_feature("author_num_watchers_already",
                           REAL);
        result.add_feature("author_prop_watchers_already",
                           REAL);
        result.add_feature("author_abs_rank", REAL);
        result.add_feature("author_abs_percentile", REAL);
        result.add_feature("author_unwatched_rank", REAL);
        result.add_feature("author_unwatched_percentile", REAL);

        return result;
    }

    virtual void candidate_set(Ranked & result, int user_id, const Data & data,
                               Candidate_Data & candidate_data) const
    {
        const User & user = data.users[user_id];

        IdSet authors_of_watched_repos;

        for (IdSet::const_iterator
                 it = user.watching.begin(),
                 end = user.watching.end();
             it != end;  ++it)
            if (data.repos[*it].author != -1)
                authors_of_watched_repos.insert(data.repos[*it].author);
        
        result.clear();

        for (IdSet::const_iterator
                 it = authors_of_watched_repos.begin(),
                 end = authors_of_watched_repos.end();
             it != end;  ++it) {

            const Author & author = data.authors[*it];

            size_t author_num_watchers = author.num_watchers;
            
            int n_already_watched = 0, watchers_already_watched = 0,
                n_unwatched = 0;

            Ranked author_entries;

            for (IdSet::const_iterator
                     jt = author.repositories.begin(),
                     jend = author.repositories.end();
                 jt != jend;  ++jt) {

                if (*jt == -1) continue;
                if (data.repos[*jt].invalid()) continue;

                int nwatchers = data.repos[*jt].watchers.size();
                
                if (user.watching.count(*jt)) {
                    ++n_already_watched;
                    watchers_already_watched += nwatchers;
                }
                else ++n_unwatched;

                author_entries.push_back(Ranked_Entry());
                Ranked_Entry & entry = author_entries.back();
                entry.score = nwatchers;
                entry.repo_id = *jt;
            }

            author_entries.sort();

            int rank = 0;
            for (unsigned i = 0;  i < author_entries.size();  ++i) {
                if (user.watching.count(author_entries[i].repo_id))
                    continue;

                result.push_back(author_entries[i]);
                Ranked_Entry & entry = result.back();
                entry.index = result.size() - 1;
                entry.features.push_back(n_already_watched);
                entry.features.push_back(n_unwatched);
                entry.features.push_back(xdiv<float>(n_already_watched, author.repositories.size()));
                entry.features.push_back(author_num_watchers);
                entry.features.push_back(xdiv<float>(watchers_already_watched, author_num_watchers));
                entry.features.push_back(entry.min_rank);
                entry.features.push_back(xdiv<float>(entry.min_rank, author_entries.size()));
                entry.features.push_back(rank);
                entry.features.push_back(xdiv<float>(rank, n_unwatched));
                
                ++rank;
            }
        }

    }
};

struct Authored_By_Collaborator_Source : public Candidate_Source {
    Authored_By_Collaborator_Source()
        : Candidate_Source("authored_by_collaborator", 10)
    {
    }

    virtual void candidate_set(Ranked & result, int user_id, const Data & data,
                               Candidate_Data & candidate_data) const
    {
        // Collaborators are users that watch at least one of our repos whilst
        // we watch at least one of theirs.  Only works where we were able to
        // identify which author number that we are.

        const User & user = data.users[user_id];

        IdSet collaborating_authors;
        for (IdSet::const_iterator
                 it = user.collaborators.begin(),
                 end = user.collaborators.end();
             it != end;  ++it) {
            collaborating_authors.insert(data.users[*it].inferred_authors.begin(),
                                         data.users[*it].inferred_authors.end());
        }

        // Find all other repos by authors of watched repos
        IdSet repos_by_collaborating_authors;

        for (IdSet::const_iterator
                 it = collaborating_authors.begin(),
                 end = collaborating_authors.end();
             it != end;  ++it) {
            repos_by_collaborating_authors
                .insert(data.users[*it].watching.begin(),
                        data.users[*it].watching.end());
        }
        
        repos_by_collaborating_authors.finish();

        result = repos_by_collaborating_authors;
    }
};

struct Watched_By_Collaborator_Source : public Candidate_Source {
    Watched_By_Collaborator_Source()
        : Candidate_Source("watched_by_collaborator", 10)
    {
    }

    virtual void candidate_set(Ranked & result, int user_id, const Data & data,
                               Candidate_Data & candidate_data) const
    {
        // Collaborators are users that watch at least one of our repos whilst
        // we watch at least one of theirs.  Only works where we were able to
        // identify which author number that we are.

        const User & user = data.users[user_id];

        // Find all other repos by authors of watched repos
        IdSet watched_by_collaborating_authors;

        for (IdSet::const_iterator
                 it = user.collaborators.begin(),
                 end = user.collaborators.end();
             it != end;  ++it) {
            watched_by_collaborating_authors
                .insert(data.users[*it].watching.begin(),
                        data.users[*it].watching.end());
        }
        
        watched_by_collaborating_authors.finish();

        result = watched_by_collaborating_authors;
    }
};

struct Same_Name_Source : public Candidate_Source {
    Same_Name_Source()
        : Candidate_Source("same_name", 8)
    {
    }

    virtual ML::Dense_Feature_Space
    specific_feature_space() const
    {
        Dense_Feature_Space result;
        result.add_feature("same_name_already_watched_num", REAL);
        result.add_feature("same_name_unwatched_num", REAL);
        result.add_feature("same_name_already_watched_prop",
                           REAL);
        result.add_feature("same_name_num_watchers_already",
                           REAL);
        result.add_feature("same_name_prop_watchers_already",
                           REAL);
        result.add_feature("same_name_abs_rank", REAL);
        result.add_feature("same_name_abs_percentile", REAL);
        result.add_feature("same_name_unwatched_rank", REAL);
        result.add_feature("same_name_unwatched_percentile",
                           REAL);

        return result;
    }

    virtual void candidate_set(Ranked & result, int user_id, const Data & data,
                               Candidate_Data & candidate_data) const
    {
        const User & user = data.users[user_id];

        set<string> name_set;

        for (IdSet::const_iterator
                 it = user.watching.begin(),
                 end = user.watching.end();
             it != end;  ++it) {
            int watched_id = *it;
            const Repo & watched = data.repos[watched_id];
            
            name_set.insert(watched.name);
        }

        result.clear();

        for (set<string>::const_iterator
                 it = name_set.begin(),
                 end = name_set.end();
             it != end;  ++it) {

            const Data::Name_Info & with_same_name
                = data.name_to_repos(*it);
            
            size_t name_num_watchers = with_same_name.num_watchers;
            
            int n_already_watched = 0, watchers_already_watched = 0,
                n_unwatched = 0;

            Ranked name_entries;

            for (Data::Name_Info::const_iterator
                     jt = with_same_name.begin(),
                     jend = with_same_name.end();
                 jt != jend;  ++jt) {

                int nwatchers = data.repos[*jt].watchers.size();

                if (user.watching.count(*jt)) {
                    ++n_already_watched;
                    watchers_already_watched += nwatchers;
                } else ++n_unwatched;

                name_entries.push_back(Ranked_Entry());
                Ranked_Entry & entry = name_entries.back();
                entry.score = nwatchers;
                entry.repo_id = *jt;
            }

            name_entries.sort();

            int rank = 0;
            for (unsigned i = 0;  i < name_entries.size();  ++i) {

                if (user.watching.count(name_entries[i].repo_id))
                    continue;

                result.push_back(name_entries[i]);

                Ranked_Entry & entry = result.back();
                entry.index = result.size() - 1;
                entry.features.push_back(n_already_watched);
                entry.features.push_back(n_unwatched);
                entry.features.push_back
                    (xdiv<float>(n_already_watched, with_same_name.size()));
                entry.features.push_back(name_num_watchers);
                entry.features.push_back
                    (xdiv<float>(watchers_already_watched, name_num_watchers));
                entry.features.push_back(entry.min_rank);
                entry.features.push_back
                    (xdiv<float>(entry.min_rank, with_same_name.size()));
                entry.features.push_back(rank);
                entry.features.push_back(xdiv<float>(rank, n_unwatched));
                
                ++rank;
            }
        }
    }
};

struct Most_Watched_Source : public Candidate_Source {
    Most_Watched_Source()
        : Candidate_Source("most_watched", 9)
    {
    }

    virtual void candidate_set(Ranked & result, int user_id, const Data & data,
                               Candidate_Data & candidate_data) const
    {
        set<int> top_n
            = data.get_most_popular_repos(max_entries);

        IdSet result_set;
        result_set.insert(top_n.begin(), top_n.end());

        result_set.finish();

        result = result_set;
    }
};

struct Probability_Propagation_Source : public Candidate_Source {
    Probability_Propagation_Source()
        : Candidate_Source("probability_propagation", 9)
    {
    }

    virtual ML::Dense_Feature_Space
    specific_feature_space() const
    {
        Dense_Feature_Space result;
        result.add_feature("prob_prop_total_prob", REAL);
        result.add_feature("prob_prop_nusers", REAL);
        result.add_feature("prob_prop_prop_per_user", REAL);
        return result;
    }

    struct Prob_Info {
        Prob_Info()
            : total(0.0), nwatchers(0)
        {
        }
        double total;
        int nwatchers;

        void operator += (double amount)
        {
            total += amount;
            nwatchers += 1;
        }
    };

    virtual void candidate_set(Ranked & result, int user_id, const Data & data,
                               Candidate_Data & candidate_data) const
    {
        const User & user = data.users[user_id];

        // Step 1: propagate to users that watch more than one repo
        hash_map<int, double> user_probs;
        double total_prob = 0.0;

        for (IdSet::const_iterator
                 it = user.watching.begin(),
                 end = user.watching.end();
             it != end;  ++it) {
            const Repo & repo = data.repos[*it];
            if (repo.watchers.size() < 2) continue;

            // the -1 is for the current user
            double nwatchers_inverse = 1.0 / (repo.watchers.size() - 1);

            for (IdSet::const_iterator
                     jt = repo.watchers.begin(),
                     jend = repo.watchers.end();
                 jt != jend;  ++jt) {
                if (*jt == user_id) continue;
                user_probs[*jt] += nwatchers_inverse;
                total_prob += 1.0;
            }
        }

        double prob_inverse = 1.0 / total_prob;

        // Step 2: propagate back to repos watched by those users
        hash_map<int, Prob_Info> repo_probs;
        for (hash_map<int, double>::const_iterator
                 it = user_probs.begin(),
                 end = user_probs.end();
             it != end;  ++it) {

            const User & user = data.users[it->first];

            double nwatching_inverse = 1.0 / user.watching.size();

            for (IdSet::const_iterator 
                     jt = user.watching.begin(),
                     jend = user.watching.end();
                 jt != jend;  ++jt)
                repo_probs[*jt]
                    += it->second * prob_inverse * nwatching_inverse;
        }

        // Step 3: rank, cut off, generate features, return results
        for (hash_map<int, Prob_Info>::const_iterator
                 it = repo_probs.begin(),
                 end = repo_probs.end();
             it != end;  ++it) {

            if (user.watching.count(it->first)) continue;
            
            result.push_back(Ranked_Entry());
            
            Ranked_Entry & entry = result.back();
            entry.score = it->second.total;
            entry.repo_id = it->first;
            entry.features.push_back(it->second.total);
            entry.features.push_back(it->second.nwatchers);
            entry.features.push_back(it->second.total / it->second.nwatchers);
        }
    }
};


/*****************************************************************************/
/* FACTORY                                                                   */
/*****************************************************************************/

boost::shared_ptr<Candidate_Source>
get_candidate_source(const ML::Configuration & config_,
                     const std::string & name)
{
    Configuration config(config_, name, Configuration::PREFIX_APPEND);

    //cerr << "config.prefix = " << config_.prefix() << endl;
    //cerr << "name = " << name << endl;
    
    string type;
    config.require(type, "type");
    
    boost::shared_ptr<Candidate_Source> result;

    if (type == "ancestors_of_watched") {
        result.reset(new Ancestors_Of_Watched_Source());
    }
    else if (type == "children_of_watched") {
        result.reset(new Children_Of_Watched_Source());
    }
    else if (type == "coocs") {
        result.reset(new Cooc_Source());
    }
    else if (type == "in_cluster_repo") {
        result.reset(new In_Cluster_Repo_Source());
    }
    else if (type == "in_cluster_user") {
        result.reset(new In_Cluster_User_Source());
    }
    else if (type == "in_id_range") {
        result.reset(new In_Id_Range_Source());
    }
    else if (type == "parents_of_watched") {
        result.reset(new Parents_Of_Watched_Source());
    }
    else if (type == "by_watched_authors") {
        result.reset(new By_Watched_Author_Source());
    }
    else if (type == "same_name") {
        result.reset(new Same_Name_Source());
    }
    else if (type == "most_watched") {
        result.reset(new Most_Watched_Source());
    }
    else if (type == "authored_by_me") {
        result.reset(new Authored_By_Me_Source());
    }
    else if (type == "authored_by_collaborator") {
        result.reset(new Authored_By_Collaborator_Source());
    }
    else if (type == "watched_by_collaborator") {
        result.reset(new Watched_By_Collaborator_Source());
    }
    else if (type == "probability_propagation") {
        result.reset(new Probability_Propagation_Source());
    }
    else throw Exception("Source of type " + type + " doesn't exist");

    result->configure(config_, name);
    result->init();

    return result;
}


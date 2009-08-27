/* candidate_source.cc
   Jeremy Barnes, 26 August 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   Source for candidates, implementation.
*/

#include "candidate_source.h"
#include "utils/less.h"
#include "math/xdiv.h"

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

    result.add_feature("density", Feature_Info::REAL);
    result.add_feature("user_id", Feature_Info::REAL);
    result.add_feature("user_repo_id_ratio", Feature_Info::REAL);
    result.add_feature("user_watched_repos", Feature_Info::REAL);
    result.add_feature("repo_watched_users", Feature_Info::REAL);
    result.add_feature("repo_lines_of_code", Feature_Info::REAL);
    result.add_feature("user_prob", Feature_Info::REAL);
    result.add_feature("user_prob_rank", Feature_Info::REAL);
    result.add_feature("repo_prob", Feature_Info::REAL);
    result.add_feature("repo_prob_rank", Feature_Info::REAL);
    result.add_feature("user_repo_prob", Feature_Info::REAL);
    result.add_feature("repo_has_parent", Feature_Info::REAL);
    result.add_feature("repo_num_children", Feature_Info::REAL);
    result.add_feature("repo_num_ancestors", Feature_Info::REAL);
    result.add_feature("repo_num_siblings", Feature_Info::REAL);
    result.add_feature("repo_parent_watchers", Feature_Info::REAL);

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

void
Candidate_Source::
gen_candidates(Ranked & entries, int user_id, const Data & data,
               Candidate_Data & candidate_data) const
{
    // Get them, unranked
    candidate_set(entries, user_id, data, candidate_data);

    // For each, get the features and run the classifier
    for (unsigned i = 0;  i < entries.size();  ++i) {
        distribution<float> features;
        common_features(features, user_id, entries[i].repo_id, data,
                        candidate_data);

        features.insert(features.end(),
                        entries[i].features.begin(),
                        entries[i].features.end());
       
        boost::shared_ptr<Mutable_Feature_Set> encoded
            = classifier_fs->encode(features, *our_fs, mapping);

        float score = classifier.impl->predict(1, *encoded, opt_info);
        entries[i].score = score;
    }
    
    // Rank them
    entries.sort();

    for (unsigned i = 0;  i < entries.size();  ++i)
        entries[i].keep = i < max_entries && entries[i].score >= min_prob;
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

        result = ancestors;
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

        result = watched_children;
    }
};

struct Cooc_Source : public Candidate_Source {
    Cooc_Source()
        : Candidate_Source("cooc", 2)
    {
    }

    virtual void candidate_set(Ranked & result, int user_id, const Data & data,
                               Candidate_Data & candidate_data) const
    {
        const User & user = data.users[user_id];
        IdSet coocs;

        // Find cooccurring with the most specicivity

        {
            hash_map<int, float> coocs_map;
        
            for (IdSet::const_iterator
                     it = user.watching.begin(),
                     end = user.watching.end();
                 it != end;  ++it) {
                int repo_id = *it;
                const Repo & repo = data.repos[repo_id];
            
                const Cooccurrences & cooc = repo.cooc;
            
                for (Cooccurrences::const_iterator
                         jt = cooc.begin(), end = cooc.end();
                     jt != end;  ++jt) {
                    if (data.repos[jt->with].watchers.size() < 2) continue;
                    coocs_map[jt->with] += jt->score;
                }
            }

            vector<pair<int, float> > coocs_sorted(coocs_map.begin(), coocs_map.end());
        
            sort_on_second_descending(coocs_sorted);

            for (unsigned i = 0;  i < 100 && i < coocs_sorted.size();  ++i)
                coocs.insert(coocs_sorted[i].first);
        }

        {
            hash_map<int, float> coocs_map;
        
            for (IdSet::const_iterator
                     it = user.watching.begin(),
                     end = user.watching.end();
                 it != end;  ++it) {
                int repo_id = *it;
                const Repo & repo = data.repos[repo_id];
            
                const Cooccurrences & cooc = repo.cooc2;
            
                for (Cooccurrences::const_iterator
                         jt = cooc.begin(), end = cooc.end();
                     jt != end;  ++jt) {
                    if (data.repos[jt->with].watchers.size() < 2) continue;
                    coocs_map[jt->with] += jt->score;
                }
            }

            vector<pair<int, float> > coocs_sorted(coocs_map.begin(), coocs_map.end());
        
            sort_on_second_descending(coocs_sorted);

            for (unsigned i = 0;  i < 100 && i < coocs_sorted.size();  ++i)
                coocs.insert(coocs_sorted[i].first);
        }

        result = coocs;
    }
};
 
struct In_Cluster_Repo_Source : public Candidate_Source {
    In_Cluster_Repo_Source()
        : Candidate_Source("in_cluster_repo", 3)
    {
    }

    virtual void candidate_set(Ranked & result,
                               int user_id, const Data & data,
                               Candidate_Data & candidate_data) const
    {
        const User & user = data.users[user_id];
        IdSet in_cluster_repo;

        for (IdSet::const_iterator
                 it = user.watching.begin(),
                 end = user.watching.end();
             it != end;  ++it) {
            int repo_id = *it;
            const Repo & repo = data.repos[repo_id];
            int cluster_id = repo.kmeans_cluster;

            if (cluster_id == -1) continue;

            IdSet repo_ids;

            const Cluster & cluster = data.repo_clusters[cluster_id];
            for (unsigned i = 0;
                 i < cluster.top_members.size() && i < 100;
                 ++i) {
                int repo_id = cluster.top_members[i];
                if (data.repos[repo_id].invalid()) continue;
                repo_ids.insert(repo_id);
            }

            // Rank by popularity
            vector<pair<int, int> > ranked;
            for (IdSet::const_iterator
                     it = repo_ids.begin(),
                     end = repo_ids.end();
                 it != end;  ++it) {
                ranked.push_back(make_pair(*it, data.repos[*it].repo_prob));
            }

            sort_on_second_descending(ranked);

            for (unsigned i = 0;  i < 100 && i < ranked.size();  ++i)
                in_cluster_repo.insert(ranked[i].first);
        }

        vector<pair<int, int> > ranked2;
        for (IdSet::const_iterator
                 it = in_cluster_repo.begin(),
                 end = in_cluster_repo.end();
             it != end;  ++it) {
            ranked2.push_back(make_pair(*it, data.repos[*it].repo_prob));
        }

        sort_on_second_descending(ranked2);

        in_cluster_repo.clear();

        for (unsigned i = 0;  i < 200 && i < ranked2.size();  ++i)
            in_cluster_repo.insert(ranked2[i].first);

        result = in_cluster_repo;
    }
};

struct In_Cluster_User_Source : public Candidate_Source {
    In_Cluster_User_Source()
        : Candidate_Source("in_cluster_user", 4)
    {
    }

    virtual void candidate_set(Ranked & result, int user_id,
                               const Data & data,
                               Candidate_Data & candidate_data) const
    {
        const User & user = data.users[user_id];
        IdSet in_cluster_user;

        // Find repos watched by other users in the same cluster
        hash_map<int, int> watched_by_cluster_user;

        int clusterno = user.kmeans_cluster;
        if (clusterno != -1) {
            const Cluster & cluster = data.user_clusters[clusterno];
            for (unsigned i = 0;  i < cluster.members.size();  ++i) {
                int user_id = cluster.members[i];
                const User & user = data.users[user_id];
            
                if (user.invalid()) continue;

                for (IdSet::const_iterator
                         it = user.watching.begin(),
                         end = user.watching.end();
                     it != end;  ++it) {
                    watched_by_cluster_user[*it] += 1;
                }
            }

            vector<pair<int, int> > ranked;

            for (hash_map<int, int>::const_iterator
                     it = watched_by_cluster_user.begin(),
                     end = watched_by_cluster_user.end();
                 it != end;  ++it) {
                if (it->second > 1)
                    ranked.push_back(*it);
            }

            sort_on_second_descending(ranked);
        
            for (unsigned i = 0;  i < 200 && i < ranked.size();  ++i)
                in_cluster_user.insert(ranked[i].first);
        }

        result = in_cluster_user;
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

        result = parents_of_watched;
    }
};

struct By_Watched_Author_Source : public Candidate_Source {
    By_Watched_Author_Source()
        : Candidate_Source("by_watched_author", 7)
    {
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
        
        // Find all other repos by authors of watched repos
        IdSet repos_by_watched_authors;

        for (IdSet::const_iterator
                 it = authors_of_watched_repos.begin(),
                 end = authors_of_watched_repos.end();
             it != end;  ++it) {
            
            repos_by_watched_authors
                .insert(data.authors[*it].repositories.begin(),
                        data.authors[*it].repositories.end());
        }

        result = repos_by_watched_authors;
    }
};

#if 0 // later
struct By_Collaborator_Source : public Candidate_Source {
    By_Collaborator_Source()
        : Candidate_Source("by_collaborator", 10)
    {
    }

    virtual void candidate_set(Ranked & result, int user_id, const Data & data,
                               Candidate_Data & candidate_data) const
    {
        // Collaborators are users that watch at least one of our repos whilst
        // we watch at least one of theirs.  Only works where we were able to
        // identify which author number that we are.

        const User & user = data.users[user_id];

        IdSet authors_of_watched_repos;

        for (IdSet::const_iterator
                 it = user.watching.begin(),
                 end = user.watching.end();
             it != end;  ++it)
            if (*it != -1)
                authors_of_watched_repos.insert(*it);
        
        // Find all other repos by authors of watched repos
        IdSet repos_by_watched_authors;

        for (IdSet::const_iterator
                 it = authors_of_watched_repos.begin(),
                 end = authors_of_watched_repos.end();
             it != end;  ++it)
            repos_by_watched_authors
                .insert(data.authors[*it].repositories.begin(),
                        data.authors[*it].repositories.end());

        return repos_by_watched_authors;
    }
};
#endif // later

struct Same_Name_Source : public Candidate_Source {
    Same_Name_Source()
        : Candidate_Source("same_name", 8)
    {
    }

    virtual void candidate_set(Ranked & result, int user_id, const Data & data,
                               Candidate_Data & candidate_data) const
    {
        const User & user = data.users[user_id];

        IdSet repos_with_same_name;

        for (IdSet::const_iterator
                 it = user.watching.begin(),
                 end = user.watching.end();
             it != end;  ++it) {
            int watched_id = *it;
            const Repo & watched = data.repos[watched_id];
            
            // Find repos with the same name
            const vector<int> & with_same_name
                = data.name_to_repos(watched.name);
            for (unsigned j = 0;  j < with_same_name.size();  ++j)
                if (with_same_name[j] != watched_id)
                    repos_with_same_name.insert(with_same_name[j]);
        }

        result = repos_with_same_name;
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


        result = result_set;
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

    cerr << "config.prefix = " << config_.prefix() << endl;
    cerr << "name = " << name << endl;
    
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
    else throw Exception("Source of type " + type + " doesn't exist");

    result->configure(config_, name);
    result->init();

    return result;
}


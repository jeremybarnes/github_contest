/* ranker.cc
   Jeremy Barnes, 9 August 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   Implementation of rankers.
*/

#include "ranker.h"

#include "utils/pair_utils.h"
#include "utils/vector_utils.h"
#include "utils/less.h"
#include "arch/exception.h"
#include "math/xdiv.h"
#include "stats/distribution_simd.h"
#include <backward/hash_map>

#include "boosting/dense_features.h"


using namespace std;
using namespace ML;


/*****************************************************************************/
/* CANDIDATE_GENERATOR                                                       */
/*****************************************************************************/

Candidate_Generator::
~Candidate_Generator()
{
}

void
Candidate_Generator::
configure(const ML::Configuration & config,
          const std::string & name)
{
}

void
Candidate_Generator::
init()
{
}

boost::shared_ptr<const ML::Dense_Feature_Space>
Candidate_Generator::
feature_space() const
{
    boost::shared_ptr<ML::Dense_Feature_Space> result;
    result.reset(new ML::Dense_Feature_Space());

    // repo id?
    result->add_feature("parent_of_watched", Feature_Info::BOOLEAN);
    result->add_feature("by_author_of_watched_repo", Feature_Info::BOOLEAN);
    result->add_feature("ancestor_of_watched", Feature_Info::BOOLEAN);
    result->add_feature("same_name", Feature_Info::BOOLEAN);
    result->add_feature("also_watched_by_people_who_watched",
                        Feature_Info::BOOLEAN);
    result->add_feature("also_watched_rank", Feature_Info::REAL);
    result->add_feature("also_watched_percentile", Feature_Info::REAL);
    result->add_feature("num_also_watched", Feature_Info::REAL);
    result->add_feature("repo_id", Feature_Info::REAL);
    result->add_feature("repo_rank", Feature_Info::REAL);
    result->add_feature("repo_watchers", Feature_Info::REAL);
    result->add_feature("child_of_watched", Feature_Info::BOOLEAN);
    // also watched min repos
    // also watched average repos

    return result;
}

std::vector<ML::distribution<float> >
Candidate_Generator::
features(int user_id,
         const std::vector<Candidate> & candidates,
         const Candidate_Data & candidate_data,
         const Data & data) const
{
    vector<distribution<float> > results(candidates.size());

    for (unsigned i = 0;  i < candidates.size();  ++i) {
        const Candidate & candidate = candidates[i];
        distribution<float> & result = results[i];

        result.push_back(candidate.parent_of_watched);
        result.push_back(candidate.by_author_of_watched_repo);
        result.push_back(candidate.ancestor_of_watched);
        result.push_back(candidate.same_name);
        result.push_back(candidate.also_watched_by_people_who_watched);
        result.push_back(candidate.also_watched_rank);
        result.push_back(candidate.also_watched_percentile);
        result.push_back(candidate.num_also_watched);
        result.push_back(candidate.repo_id);
        
        const Repo & repo = data.repos[candidate.repo_id];
        result.push_back(repo.popularity_rank);
        result.push_back(repo.watchers.size());

        result.push_back(candidate.child_of_watched);
    }

    return results;
}

std::pair<std::vector<Candidate>,
          boost::shared_ptr<Candidate_Data> >
Candidate_Generator::
candidates(const Data & data, int user_id) const
{
    const User & user = data.users[user_id];

    vector<Candidate> candidates;
    boost::shared_ptr<Candidate_Data> data_ptr(new Candidate_Data());
    Candidate_Data & candidate_data = *data_ptr;

    IdSet possible_choices;
    
    /* Like everyone else, see which parents and ancestors weren't
       watched */
    IdSet & parents_of_watched = candidate_data.parents_of_watched;
    IdSet & ancestors_of_watched = candidate_data.ancestors_of_watched;
    IdSet & authors_of_watched_repos
        = candidate_data.authors_of_watched_repos;
    IdSet & repos_with_same_name
        = candidate_data.repos_with_same_name;
    hash_map<int, int> also_watched_by_people_who_watched;
    IdSet & children_of_watched_repos
        = candidate_data.children_of_watched_repos;
    
    for (IdSet::const_iterator
             it = user.watching.begin(),
             end = user.watching.end();
         it != end;  ++it) {
        int watched_id = *it;
        const Repo & watched = data.repos[watched_id];
    
        children_of_watched_repos.insert(watched.children.begin(),
                                         watched.children.end());
    
        if (watched.author != -1)
            authors_of_watched_repos.insert(watched.author);
        
        if (watched.parent != -1) {
        
            parents_of_watched.insert(watched.parent);
            ancestors_of_watched.insert(watched.ancestors.begin(),
                                        watched.ancestors.end());
        }
        
        // Find repos with the same name
        const vector<int> & with_same_name
            = data.name_to_repos(watched.name);
        for (unsigned j = 0;  j < with_same_name.size();  ++j)
            if (with_same_name[j] != watched_id)
                repos_with_same_name.insert(with_same_name[j]);
    }

    // Find all other repos by authors of watched repos
    IdSet repos_by_watched_authors;
    for (IdSet::const_iterator
             it = authors_of_watched_repos.begin(),
             end = authors_of_watched_repos.end();
         it != end;  ++it)
        repos_by_watched_authors
            .insert(data.authors[*it].repositories.begin(),
                    data.authors[*it].repositories.end());

    possible_choices.insert(parents_of_watched.begin(),
                            parents_of_watched.end());
    possible_choices.insert(ancestors_of_watched.begin(),
                            ancestors_of_watched.end());
    possible_choices.insert(repos_by_watched_authors.begin(),
                            repos_by_watched_authors.end());
    possible_choices.insert(repos_with_same_name.begin(),
                            repos_with_same_name.end());
    possible_choices.insert(children_of_watched_repos.begin(),
                            children_of_watched_repos.end());

    for (IdSet::const_iterator
             it = user.watching.begin(),
             end = user.watching.end();
         it != end && possible_choices.size() < 100;  ++it) {

        int watched_id = *it;
        const Repo & watched = data.repos[watched_id];
        
        // Find those also watched by those that watched this one
        for (IdSet::const_iterator
                 jt = watched.watchers.begin(),
                 jend = watched.watchers.end();
             jt != jend;  ++jt) {
            int watcher_id = *jt;
            if (watcher_id == user_id) continue;
            
            const User & watcher = data.users[watcher_id];
            
            for (IdSet::const_iterator
                     kt = watcher.watching.begin(),
                     kend = watcher.watching.end();
                 kt != kend;  ++kt) {
                if (*kt == watched_id) continue;
                also_watched_by_people_who_watched[*kt] += 1;
            }
        }
    }
    
    if (also_watched_by_people_who_watched.size() < 3000)
        possible_choices.insert(first_extractor(also_watched_by_people_who_watched.begin()),
                                first_extractor(also_watched_by_people_who_watched.end()));
    
    vector<pair<int, int> > also_watched_ranked(also_watched_by_people_who_watched.begin(),
                                                also_watched_by_people_who_watched.end());
    sort_on_second_descending(also_watched_ranked);

    map<int, int> awranks;
    for (unsigned i = 0;  i < also_watched_ranked.size();  ++i) {
        awranks[also_watched_ranked[i].first] = i;
    }

    set<int> top_twenty
        = data.get_most_popular_repos(20);
    possible_choices.insert(top_twenty.begin(),
                            top_twenty.end());

    candidates.reserve(possible_choices.size());

    for (IdSet::const_iterator
             it = possible_choices.begin(),
             end = possible_choices.end();
         it != end;  ++it) {

        int repo_id = *it;
        Candidate c(repo_id);

        c.parent_of_watched = parents_of_watched.count(repo_id);
        c.by_author_of_watched_repo = repos_by_watched_authors.count(repo_id);
        c.ancestor_of_watched = ancestors_of_watched.count(repo_id);
        c.same_name = repos_with_same_name.count(repo_id);
        c.child_of_watched = children_of_watched_repos.count(repo_id);
        
        if (also_watched_by_people_who_watched.count(repo_id)) {
            c.also_watched_by_people_who_watched = true;
            c.num_also_watched
                = also_watched_by_people_who_watched[repo_id];
            c.also_watched_rank = awranks[repo_id];
            c.also_watched_percentile
                = c.also_watched_rank * 1.0 / awranks.size();
        }
        else {
            c.also_watched_by_people_who_watched = false;
            c.also_watched_rank = awranks.size();
            c.also_watched_percentile = 0.0;
        }
        c.top_ten = data.repos[repo_id].popularity_rank < 10;

        candidates.push_back(c);
    }

    return make_pair(candidates, data_ptr);
}


/*****************************************************************************/
/* RANKER                                                                    */
/*****************************************************************************/

struct Compare_Ranked_Entries {
    bool operator () (const Ranked_Entry & e1,
                      const Ranked_Entry & e2)
    {
        return less_all(e2.score, e1.score,
                        e2.repo_id, e1.repo_id);
    }
};

void Ranked::sort()
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

Ranker::~Ranker()
{
}

void
Ranker::
configure(const ML::Configuration & config,
          const std::string & name)
{
}

void
Ranker::
init(boost::shared_ptr<Candidate_Generator> generator)
{
    this->generator = generator;
}

boost::shared_ptr<const ML::Dense_Feature_Space>
Ranker::
feature_space() const
{
    boost::shared_ptr<ML::Dense_Feature_Space> result;
    result.reset(new ML::Dense_Feature_Space(*generator->feature_space()));

    result->add_feature("heuristic_score", Feature_Info::REAL);
    result->add_feature("heuristic_rank",  Feature_Info::REAL);
    result->add_feature("heuristic_percentile", Feature_Info::REAL);

    result->add_feature("density", Feature_Info::REAL);
    result->add_feature("user_id", Feature_Info::REAL);
    
    result->add_feature("user_repo_id_ratio", Feature_Info::REAL);
    
    result->add_feature("user_watched_repos", Feature_Info::REAL);

    result->add_feature("language_dprod", Feature_Info::REAL);
    result->add_feature("language_cosine", Feature_Info::REAL);

    result->add_feature("repo_lines_of_code", Feature_Info::REAL);

    result->add_feature("user_prob", Feature_Info::REAL);
    result->add_feature("user_prob_rank", Feature_Info::REAL);

    result->add_feature("repo_prob", Feature_Info::REAL);
    result->add_feature("repo_prob_rank", Feature_Info::REAL);

    result->add_feature("user_repo_prob", Feature_Info::REAL);
    
    return result;
}

std::vector<ML::distribution<float> >
Ranker::
features(int user_id,
         const std::vector<Candidate> & candidates,
         const Candidate_Data & candidate_data,
         const Data & data) const
{
    vector<distribution<float> > results;
    
    results = (*generator).features(user_id, candidates, candidate_data, data);
    
    Ranked heuristic
        = Ranker::rank(user_id, candidates, candidate_data, data);
    heuristic.sort();

    const User & user = data.users[user_id];

    for (unsigned i = 0;  i < heuristic.size();  ++i) {
        distribution<float> & result = results[heuristic[i].index];

        int repo_id = heuristic[i].repo_id;
        const Repo & repo = data.repos[repo_id];

        result.push_back(heuristic[i].score);
        result.push_back((heuristic[i].min_rank + heuristic[i].max_rank) * 0.5);
        result.push_back(result.back() / heuristic.size());

        result.push_back(data.density(user_id, repo_id));
        result.push_back(user_id);
        result.push_back(user_id * 1.0 / repo_id);

        result.push_back(user.watching.size());

        float dp = (repo.language_vec * user.language_vec).total();

        result.push_back(dp);
        result.push_back(xdiv(dp, repo.language_2norm * user.language_2norm));

        if (!finite(result.back())) {
            throw Exception("not finite dp");
            cerr << "dp = " << dp << endl;
            cerr << "r = " << repo.language_vec << endl;
            cerr << "u = " << user.language_vec << endl;
            cerr << "r2 = " << repo.language_2norm << endl;
            cerr << "u2 = " << user.language_2norm << endl;
        }

        result.push_back(log(repo.total_loc + 1));

        result.push_back(user.user_prob);
        result.push_back(user.user_prob_rank);
        result.push_back(repo.repo_prob);
        result.push_back(repo.repo_prob_rank);

        result.push_back(user.user_prob * repo.repo_prob);
    }

    return results;
}

Ranked
Ranker::
rank(int user_id,
     const std::vector<Candidate> & candidates,
     const Candidate_Data & candidate_data,
     const Data & data) const
{
    Ranked result;

    for (unsigned i = 0;  i < candidates.size();  ++i) {
        const Candidate & c = candidates[i];

        int repo_id = c.repo_id;
        const Repo & repo = data.repos[repo_id];

        float score = 0.0;

        float popularity
            = (data.repos.size() - repo.popularity_rank)
            * 1.0 / data.repos.size();

        score += c.parent_of_watched * 200;
        score += c.by_author_of_watched_repo * 100;
        //score += c.ancestor_of_watched * 10;
        score += c.same_name * 20;
        score += c.also_watched_by_people_who_watched * 5;
        //score += c.also_watched_percentile * 5;

        // Add in a popularity score
        score += popularity * 20;

        Ranked_Entry entry;
        entry.index = i;
        entry.repo_id = repo_id;
        entry.score = score;

        result.push_back(entry);
    }

    return result;
}


/*****************************************************************************/
/* CLASSIFIER_RANKER                                                         */
/*****************************************************************************/

Classifier_Ranker::~Classifier_Ranker()
{
}

void
Classifier_Ranker::
configure(const ML::Configuration & config_,
          const std::string & name)
{
    Ranker::configure(config_, name);

    Configuration config(config_, name, Configuration::PREFIX_APPEND);

    cerr << "name = " << name << " config.prefix() = " << config.prefix()
         << " config_.prefix() = " << config_.prefix() << endl;

    config.require(classifier_file, "classifier_file");
}

void
Classifier_Ranker::
init(boost::shared_ptr<Candidate_Generator> generator)
{
    Ranker::init(generator);

    classifier.load(classifier_file);

    ranker_fs = feature_space();
    classifier_fs = classifier.feature_space<ML::Dense_Feature_Space>();

    classifier_fs->create_mapping(*ranker_fs, mapping);

    vector<ML::Feature> classifier_features
        = classifier.all_features();

    // TODO: check for missing features
    //for (unsigned i = 0;  i < classifier_features.size();  ++i) {
    //}
}

boost::shared_ptr<const ML::Dense_Feature_Space>
Classifier_Ranker::
feature_space() const
{
    return Ranker::feature_space();
}

std::vector<ML::distribution<float> >
Classifier_Ranker::
features(int user_id,
         const std::vector<Candidate> & candidates,
         const Candidate_Data & candidate_data,
         const Data & data) const
{
    return Ranker::features(user_id, candidates, candidate_data, data);
}

Ranked
Classifier_Ranker::
rank(int user_id,
     const std::vector<Candidate> & candidates,
     const Candidate_Data & candidate_data,
     const Data & data) const
{
    Ranked result;

    vector<distribution<float> > features
        = this->features(user_id, candidates, candidate_data, data);


    for (unsigned i = 0;  i < candidates.size();  ++i) {
        const Candidate & c = candidates[i];
        int repo_id = c.repo_id;

        boost::shared_ptr<Mutable_Feature_Set> encoded
            = classifier_fs->encode(features[i], *ranker_fs, mapping);

        //cerr << "features = " << features << endl;

        //cerr << "features.size() = " << features.size() << endl;
        //cerr << "encoded->size() = " << encoded->size() << endl;

        //for (Mutable_Feature_Set::const_iterator it = encoded->begin();
        //     it != encoded->end();  ++it) {
        //    cerr << "feature " << it->first << " value "
        //         << it->second << endl;
        //}


        float score = classifier.predict(1, *encoded);

        Ranked_Entry entry;
        entry.index = i;
        entry.repo_id = repo_id;
        entry.score = score;

        result.push_back(entry);
    }

    return result;
}


boost::shared_ptr<Candidate_Generator>
get_candidate_generator(const Configuration & config,
                        const std::string & name)
{
    boost::shared_ptr<Candidate_Generator> result;
    result.reset(new Candidate_Generator());
    result->configure(config, name);
    result->init();
    return result;
}

boost::shared_ptr<Ranker>
get_ranker(const Configuration & config_,
           const std::string & name,
           boost::shared_ptr<Candidate_Generator> generator)
{
    Configuration config(config_, name, Configuration::PREFIX_APPEND);

    string type;
    config.require(type, "type");

    boost::shared_ptr<Ranker> result;

    if (type == "default") {
        result.reset(new Ranker());
    }
    else if (type == "classifier") {
        result.reset(new Classifier_Ranker());
    }
    else throw Exception("Ranker of type " + type + " doesn't exist");

    result->configure(config_, name);
    result->init(generator);

    return result;
}

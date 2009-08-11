/* ranker.cc
   Jeremy Barnes, 9 August 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   Implementation of rankers.
*/

#include "ranker.h"

#include "utils/pair_utils.h"
#include "utils/vector_utils.h"
#include "arch/exception.h"

#include "boosting/dense_features.h"


using namespace std;
using namespace ML;

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

boost::shared_ptr<const ML::Feature_Space>
Candidate_Generator::
feature_space() const
{
    boost::shared_ptr<ML::Feature_Space> result;
    result.reset(new ML::Dense_Feature_Space());
    return result;
}

std::vector<Candidate>
Candidate_Generator::
candidates(const Data & data, int user_id) const
{
    const User & user = data.users[user_id];

    vector<Candidate> candidates;
    
    set<int> user_results;
    set<int> possible_choices;
    
    /* Like everyone else, see which parents and ancestors weren't
       watched */
    set<int> parents_of_watched;
    set<int> ancestors_of_watched;
    set<int> authors_of_watched_repos;
    set<int> repos_with_same_name;
    map<int, int> also_watched_by_people_who_watched;
    
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
        
        // Find repos with the same name
        
        Data::Repo_Name_To_Repos::const_iterator found
            = data.repo_name_to_repos.find(watched.name);
        if (found == data.repo_name_to_repos.end())
            throw Exception("repo name not in index");

        const vector<int> & with_same_name = found->second;

        repos_with_same_name.insert(with_same_name.begin(),
                                    with_same_name.end());
        repos_with_same_name.erase(watched_id);

#if 0
        map<int, int> & repos_watched_by_watchers
            = watched.repos_watched_by_watchers;

        if (!watched.repos_watched_by_watchers_initialized) {
        
            // Find those also watched by those that watched this one
            for (set<int>::const_iterator
                     jt = watched.watchers.begin(),
                     jend = watched.watchers.end();
                 jt != jend;  ++jt) {
                int watcher_id = *jt;
                
                const User & watcher = data.users[watcher_id];
                
                for (set<int>::const_iterator
                         kt = watcher.watching.begin(),
                         kend = watcher.watching.end();
                     kt != kend;  ++kt) {
                    if (*kt == watched_id) continue;
                    
                    repos_watched_by_watchers[*kt] += 1;
                }
            }

            watched.repos_watched_by_watchers_initialized = true;
        }

        for (map<int, int>::const_iterator
                 jt = repos_watched_by_watchers.begin(),
                 jend = repos_watched_by_watchers.end();
             jt != jend;  ++jt) {
            also_watched_by_people_who_watched[jt->first] += jt->second;
        }
#else
        // Find those also watched by those that watched this one
        for (set<int>::const_iterator
                 jt = watched.watchers.begin(),
                 jend = watched.watchers.end();
             jt != jend;  ++jt) {
            int watcher_id = *jt;
            if (watcher_id == user_id) continue;
            
            const User & watcher = data.users[watcher_id];
            
            for (set<int>::const_iterator
                     kt = watcher.watching.begin(),
                     kend = watcher.watching.end();
                 kt != kend;  ++kt) {
                if (*kt == watched_id) continue;
                also_watched_by_people_who_watched[*kt] += 1;
            }
        }
#endif
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
    
    // Find all other repos by authors of watched repos
    set<int> repos_by_watched_authors;
    for (set<int>::const_iterator
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
    
    possible_choices.insert(first_extractor(also_watched_by_people_who_watched.begin()),
                            first_extractor(also_watched_by_people_who_watched.end()));

    vector<pair<int, int> > also_watched_ranked(also_watched_by_people_who_watched.begin(),
                                                also_watched_by_people_who_watched.end());
    sort_on_second_descending(also_watched_ranked);

    map<int, int> awranks;
    for (unsigned i = 0;  i < also_watched_ranked.size();  ++i) {
        awranks[also_watched_ranked[i].first] = i;
    }

#if 0
    set<int> top_ten = data.get_most_popular_repos(10);

    possible_choices.insert(top_ten.begin(),
                            top_ten.end());
#endif

    candidates.reserve(possible_choices.size());

    for (set<int>::const_iterator
             it = possible_choices.begin(),
             end = possible_choices.end();
         it != end;  ++it) {

        int repo_id = *it;
        Candidate c(repo_id);

        c.parent_of_watched = parents_of_watched.count(repo_id);
        c.by_author_of_watched_repo = repos_by_watched_authors.count(repo_id);
        c.ancestor_of_watched = ancestors_of_watched.count(repo_id);
        c.same_name = repos_with_same_name.count(repo_id);
        
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

    return candidates;
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
}

boost::shared_ptr<const ML::Feature_Space>
Ranker::
feature_space() const
{
    boost::shared_ptr<ML::Feature_Space> result;
    result.reset(new ML::Dense_Feature_Space());
    return result;
}

Ranked
Ranker::
rank(const Data & data, int user_id,
     const std::vector<Candidate> & candidates) const
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

        result.push_back(make_pair(repo_id, score));
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
get_ranker(const Configuration & config,
           const std::string & name,
           boost::shared_ptr<Candidate_Generator> generator)
{
    boost::shared_ptr<Ranker> result;
    result.reset(new Ranker());
    result->configure(config, name);
    result->init(generator);
    return result;
}

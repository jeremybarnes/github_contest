/* ranker.h                                                        -*- C++ -*-
   Jeremy Barnes, 9 August 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   A ranker for a set of candidates from the GitHub contest.  Abstract
   interface.
*/

#ifndef __github__ranker_h__
#define __github__ranker_h__

#include "data.h"
#include "utils/configuration.h"

#include "boosting/feature_space.h"

// Records a candidate for ranking
struct Candidate {

    Candidate(int repo_id = -1)
        : repo_id(repo_id),
          parent_of_watched(false),
          by_author_of_watched_repo(false),
          ancestor_of_watched(false),
          same_name(false),
          also_watched_by_people_who_watched(false),
          top_ten(false),
          also_watched_rank(-1),
          also_watched_percentile(-1.0f),
          num_also_watched(0)
    {
    }

    int repo_id;

    // Where does this come from?
    bool parent_of_watched;
    bool by_author_of_watched_repo;
    bool ancestor_of_watched;
    bool same_name;
    bool also_watched_by_people_who_watched;
    bool top_ten;

    int also_watched_rank;
    float also_watched_percentile;
    int num_also_watched;
};

// Base class for a candidate generator
struct Candidate_Generator {

    virtual ~Candidate_Generator();

    virtual void configure(const ML::Configuration & config,
                      const std::string & name);

    virtual void init();

    virtual boost::shared_ptr<const ML::Feature_Space>
    feature_space() const;

    /// Generates a set of candidates to be ranked for the given user
    virtual std::vector<Candidate>
    candidates(const Data & data, int user_id) const;
};

typedef std::vector<std::pair<int, float> > Ranked;

/// Base class for a candidate ranker
struct Ranker {

    virtual ~Ranker();

    virtual void configure(const ML::Configuration & config,
                           const std::string & name);


    virtual void init(boost::shared_ptr<Candidate_Generator> generator);

    virtual boost::shared_ptr<const ML::Feature_Space>
    feature_space() const;

    virtual Ranked
    rank(const Data & data, int user_id,
         const std::vector<Candidate> & candidates) const;

};


// Factory methods

boost::shared_ptr<Candidate_Generator>
get_candidate_generator(const ML::Configuration & config,
                        const std::string & name);

boost::shared_ptr<Ranker>
get_ranker(const ML::Configuration & config,
           const std::string & name,
           boost::shared_ptr<Candidate_Generator> generator);

#endif /* __github__ranker_h__ */


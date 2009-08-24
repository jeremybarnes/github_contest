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

#include "boosting/dense_features.h"
#include "boosting/classifier.h"

// Records a candidate for ranking
struct Candidate {

    Candidate(int repo_id = -1)
        : repo_id(repo_id),
          parent_of_watched(false),
          by_author_of_watched_repo(false),
          ancestor_of_watched(false),
          same_name(false),
          top_ten(false),
          child_of_watched(false),
          watched_by_cluster_user(0),
          in_cluster_repo(0),
          in_id_range(0)
    {
    }

    int repo_id;

    // Where does this come from?
    bool parent_of_watched;
    bool by_author_of_watched_repo;
    bool ancestor_of_watched;
    bool same_name;
    bool top_ten;
    bool child_of_watched;
    int  watched_by_cluster_user;
    bool in_cluster_repo; 
    bool in_id_range;
};

struct Candidate_Data {

    IdSet parents_of_watched;
    IdSet ancestors_of_watched;
    IdSet children_of_watched;
    IdSet authors_of_watched_repos;
    IdSet repos_with_same_name;
    IdSet children_of_watched_repos;
    IdSet in_id_range;

    virtual ~Candidate_Data()
    {
    }
};

// Base class for a candidate generator
struct Candidate_Generator {

    virtual ~Candidate_Generator();

    virtual void configure(const ML::Configuration & config,
                      const std::string & name);

    virtual void init();

    virtual boost::shared_ptr<const ML::Dense_Feature_Space>
    feature_space() const;

    virtual std::vector<ML::distribution<float> >
    features(int user_id,
             const std::vector<Candidate> & candidates,
             const Candidate_Data & candidate_data,
             const Data & data) const;

    /// Generates a set of candidates to be ranked for the given user
    virtual std::pair<std::vector<Candidate>,
                      boost::shared_ptr<Candidate_Data> >
    candidates(const Data & data, int user_id) const;
};

struct Ranked_Entry {
    Ranked_Entry()
        : index(-1), repo_id(-1), score(0.0), min_rank(-1), max_rank(-1),
          filtered(false)
    {
    }

    int index;
    int repo_id;
    float score;
    int min_rank;
    int max_rank;
    bool filtered;
};

struct Ranked : std::vector<Ranked_Entry> {
    void sort();
};

/// Base class for a candidate ranker
struct Ranker {

    virtual ~Ranker();

    virtual void configure(const ML::Configuration & config,
                           const std::string & name);


    virtual void init(boost::shared_ptr<Candidate_Generator> generator);

    virtual boost::shared_ptr<const ML::Dense_Feature_Space>
    feature_space() const;

    virtual std::vector<ML::distribution<float> >
    features(int user_id,
             const std::vector<Candidate> & candidates,
             const Candidate_Data & candidate_data,
             const Data & data) const;

    virtual Ranked
    rank(int user_id,
         const std::vector<Candidate> & candidates,
         const Candidate_Data & candidate_data,
         const Data & data) const;

    boost::shared_ptr<Candidate_Generator> generator;
};

struct Classifier_Ranker : public Ranker {
    virtual ~Classifier_Ranker();

    virtual void configure(const ML::Configuration & config,
                           const std::string & name);


    virtual void init(boost::shared_ptr<Candidate_Generator> generator);

    virtual boost::shared_ptr<const ML::Dense_Feature_Space>
    feature_space() const;

    virtual std::vector<ML::distribution<float> >
    features(int user_id,
             const std::vector<Candidate> & candidates,
             const Candidate_Data & candidate_data,
             const Data & data) const;

    virtual Ranked
    classify(int user_id,
             const std::vector<Candidate> & candidates,
             const Candidate_Data & candidate_data,
             const Data & data,
             const std::vector<ML::distribution<float> > & features) const;
    
    virtual Ranked
    rank(int user_id,
         const std::vector<Candidate> & candidates,
         const Candidate_Data & candidate_data,
         const Data & data) const;

    std::string classifier_file;
    ML::Classifier classifier;
    boost::shared_ptr<const ML::Dense_Feature_Space> ranker_fs;
    boost::shared_ptr<const ML::Dense_Feature_Space> classifier_fs;
    ML::Dense_Feature_Space::Mapping mapping;
    ML::Optimization_Info opt_info;
    bool load_data;
};

struct Classifier_Reranker : public Classifier_Ranker {
    virtual ~Classifier_Reranker();

    virtual void configure(const ML::Configuration & config,
                           const std::string & name);


    virtual void init(boost::shared_ptr<Candidate_Generator> generator);

    virtual boost::shared_ptr<const ML::Dense_Feature_Space>
    feature_space() const;

    virtual std::vector<ML::distribution<float> >
    features(int user_id,
             const std::vector<Candidate> & candidates,
             const Candidate_Data & candidate_data,
             const Data & data) const;

    virtual Ranked
    rank(int user_id,
         const std::vector<Candidate> & candidates,
         const Candidate_Data & candidate_data,
         const Data & data) const;

    virtual Ranked
    classify(int user_id,
             const std::vector<Candidate> & candidates,
             const Candidate_Data & candidate_data,
             const Data & data,
             const std::vector<ML::distribution<float> > & features) const;

    Classifier_Ranker phase1;
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


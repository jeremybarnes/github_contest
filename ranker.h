/* ranker.h                                                        -*- C++ -*-
   Jeremy Barnes, 9 August 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   A ranker for a set of candidates from the GitHub contest.  Abstract
   interface.
*/

#ifndef __github__ranker_h__
#define __github__ranker_h__

#include "data.h"
#include "candidate_source.h"
#include "utils/configuration.h"

#include "boosting/dense_features.h"
#include "boosting/classifier.h"

// Global variables for statistics
extern int correct_repo;
extern const IdSet * watching;


/*****************************************************************************/
/* CANDIDATE_GENERATOR                                                       */
/*****************************************************************************/

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
             const Ranked & candidates,
             const Candidate_Data & candidate_data,
             const Data & data) const;

    /// Generates a set of candidates to be ranked for the given user
    virtual std::pair<Ranked, boost::shared_ptr<Candidate_Data> >
    candidates(const Data & data, int user_id) const;

    std::vector<boost::shared_ptr<Candidate_Source> > sources;
};


/*****************************************************************************/
/* RANKER                                                                    */
/*****************************************************************************/

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
             const Ranked & candidates,
             const Candidate_Data & candidate_data,
             const Data & data) const;

    virtual Ranked
    rank(int user_id,
         const Ranked & candidates,
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
             const Ranked & candidates,
             const Candidate_Data & candidate_data,
             const Data & data) const;

    virtual Ranked
    classify(int user_id,
             const Ranked & candidates,
             const Candidate_Data & candidate_data,
             const Data & data,
             const std::vector<ML::distribution<float> > & features) const;
    
    virtual Ranked
    rank(int user_id,
         const Ranked & candidates,
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
             const Ranked & candidates,
             const Candidate_Data & candidate_data,
             const Data & data) const;

    virtual Ranked
    rank(int user_id,
         const Ranked & candidates,
         const Candidate_Data & candidate_data,
         const Data & data) const;

    virtual Ranked
    classify(int user_id,
             const Ranked & candidates,
             const Candidate_Data & candidate_data,
             const Data & data,
             const std::vector<ML::distribution<float> > & features) const;

    Classifier_Ranker phase1;
};

// Factory methods

boost::shared_ptr<Candidate_Source>
get_candidate_source(const ML::Configuration & config,
                     const std::string & name);

boost::shared_ptr<Candidate_Generator>
get_candidate_generator(const ML::Configuration & config,
                        const std::string & name);

boost::shared_ptr<Ranker>
get_ranker(const ML::Configuration & config,
           const std::string & name,
           boost::shared_ptr<Candidate_Generator> generator);

#endif /* __github__ranker_h__ */


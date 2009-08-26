/* candidate_source.h                                              -*- C++ -*-
   Jeremy Barnes, 26 August 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   Candidate source class.
*/

#ifndef __github__candidate_source_h__
#define __github__candidate_source_h__


#include "data.h"
#include "utils/configuration.h"
#include "boosting/dense_features.h"
#include "boosting/classifier.h"

#include <map>

struct Ranked_Entry {
    Ranked_Entry()
        : index(-1), repo_id(-1), score(0.0), min_rank(-1), max_rank(-1),
          keep(false)
    {
    }

    int index;
    int repo_id;
    float score;
    int min_rank;
    int max_rank;
    ML::distribution<float> features;
    bool keep;
};

struct Ranked : std::vector<Ranked_Entry> {
    Ranked() {}
    Ranked(const IdSet & idset);
    void sort();
};


struct Candidate_Data {
    virtual ~Candidate_Data()
    {
    }

    // Information about each candidate source from each repo
    // Access with: info[repo_id][source_id]
    std::map<int, std::map<int, Ranked_Entry> > info;
};

/*****************************************************************************/
/* CANDIDATE_SOURCE                                                          */
/*****************************************************************************/
// A source of candidates; one for each of the different types.  The goal is to
// generate a set of possible candidates, and to then add only a limited number
// to the final set.
struct Candidate_Source {
    Candidate_Source(const std::string & type, int id);

    virtual ~Candidate_Source();

    std::string type() const { return type_; }

    std::string name() const { return name_; }

    int id() const { return id_; }

    virtual void configure(const ML::Configuration & config,
                           const std::string & name);

    virtual void init();

    /// Generate feature space specific to this candidate
    virtual boost::shared_ptr<const ML::Dense_Feature_Space>
    feature_space() const;

    /// Feature space that's common to all features
    static ML::Dense_Feature_Space
    common_feature_space();

    static void
    common_features(distribution<float> & result,
                    int user_id, int repo_id, const Data & data,
                    Candidate_Data & candidate_data);

    /// Feature space containing features specific to this candidate source
    virtual ML::Dense_Feature_Space specific_feature_space() const;

    virtual void
    gen_candidates(Ranked & result, int user_id, const Data & data,
                   Candidate_Data & candidate_data) const;

    /// Generate the very basic set of candidates with features but no
    /// ranking information
    virtual void
    candidate_set(Ranked & results, int user_id, const Data & data,
                  Candidate_Data & candidate_data) const = 0;

    std::string name_;
    std::string type_;
    int id_;

    int max_entries; ///< Max number of entries to generate for this one
    float min_prob;  ///< Minimum probability to generate for

    // Classifier, etc to perform the ranking
    std::string classifier_file;
    ML::Classifier classifier;
    boost::shared_ptr<const ML::Dense_Feature_Space> our_fs;
    boost::shared_ptr<const ML::Dense_Feature_Space> classifier_fs;
    ML::Dense_Feature_Space::Mapping mapping;
    ML::Optimization_Info opt_info;
    bool load_data;
};


/*****************************************************************************/
/* FACTORY                                                                   */
/*****************************************************************************/

boost::shared_ptr<Candidate_Source>
get_candidate_source(const ML::Configuration & config,
                     const std::string & name);


#endif /* __github__candidate_source_h__ */

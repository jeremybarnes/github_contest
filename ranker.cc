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
#include "utils/hash_map.h"

#include "boosting/dense_features.h"
#include <limits>

using namespace std;
using namespace ML;

namespace {
const float NaN = numeric_limits<float>::quiet_NaN();
};

/*****************************************************************************/
/* CANDIDATE_GENERATOR                                                       */
/*****************************************************************************/

Candidate_Generator::
~Candidate_Generator()
{
}

void
Candidate_Generator::
configure(const ML::Configuration & config_,
          const std::string & name)
{
    Configuration config(config_, name, Configuration::PREFIX_APPEND);

    string sources;
    config.require(sources, "sources");

    vector<string> source_names = split(sources, ',');

    this->sources.clear();

    for (unsigned i = 0;  i < source_names.size();  ++i) {
        boost::shared_ptr<Candidate_Source> source
            = get_candidate_source(config, source_names[i]);
        this->sources.push_back(source);
    }
}

void
Candidate_Generator::
init()
{

    for (unsigned i = 0;  i < sources.size();  ++i)
        sources[i]->init();

    source_num_features.resize(sources.size());

    for (unsigned i = 0;  i < sources.size();  ++i) {
        Dense_Feature_Space source_fs
            = sources[i]->specific_feature_space();
        source_num_features[i] = source_fs.variable_count();
    }
}

boost::shared_ptr<const ML::Dense_Feature_Space>
Candidate_Generator::
feature_space() const
{
    boost::shared_ptr<ML::Dense_Feature_Space> result;
    result.reset(new ML::Dense_Feature_Space());

    ML::Dense_Feature_Space common_feature_space
        = Candidate_Source::common_feature_space();
    result->add(common_feature_space);

    for (unsigned i = 0;  i < sources.size();  ++i) {
        string name = sources[i]->name();
        Dense_Feature_Space source_fs
            = sources[i]->specific_feature_space();
        result->add(source_fs, name + "_");
        result->add_feature(name + "_rank", Feature_Info::REAL);
        result->add_feature(name + "_percentile", Feature_Info::REAL);
        result->add_feature(name + "_score", Feature_Info::REAL);
    }

    result->add_feature("gen_total_rank", Feature_Info::REAL);
    result->add_feature("gen_min_rank", Feature_Info::REAL);
    result->add_feature("gen_max_rank", Feature_Info::REAL);
    result->add_feature("gen_num_in", Feature_Info::REAL);
    result->add_feature("gen_avg_rank", Feature_Info::REAL);
    result->add_feature("gen_total_score", Feature_Info::REAL);
    result->add_feature("gen_min_score", Feature_Info::REAL);
    result->add_feature("gen_max_score", Feature_Info::REAL);
    result->add_feature("gen_avg_score", Feature_Info::REAL);

    return result;
}  

void
Candidate_Generator::
features(std::vector<ML::distribution<float> > & results,
         int user_id,
         const Ranked & candidates,
         const Candidate_Data & candidate_data,
         const Data & data) const
{
    results.resize(candidates.size());
    // We just copy them over...

    for (unsigned i = 0;  i < candidates.size();  ++i) {
        const Ranked_Entry & candidate = candidates[i];
        distribution<float> & result = results[i];
        result = candidate.features;
    }
}

namespace {

struct Name_Stats {
    Name_Stats()
        : total_size(0), n(0), correct(0), incremental_size(0),
          incremental_correct(0), max_size(0), already_watched(0)
    {
    }

    size_t total_size;
    size_t n;
    size_t correct;
    size_t incremental_size;
    size_t incremental_correct;
    size_t max_size;
    size_t already_watched;
};

map<string, Name_Stats> stats;
Lock stats_lock;

struct PrintStats {
    ~PrintStats()
    {
        cerr << "generator                        fired   corr "
             << endl;
        for (map<string, Name_Stats>::const_iterator
                 it = stats.begin(),
                 end = stats.end();
             it != end;  ++it) {
            const Name_Stats & s = it->second;
            cerr << format("%-30s %7zd %5zd(%5.2f%%) %7zd(%7.2f) %5zd(%5.2f%%) %7zd(%7.2f) %7zd\n",
                           it->first.c_str(),
                           s.n,
                           s.correct,
                           100.0 * s.correct / s.n,
                           s.total_size,
                           1.0 * s.total_size / s.n,
                           s.incremental_correct,
                           100.0 * s.incremental_correct / s.n,
                           s.incremental_size,
                           1.0 * s.incremental_size / s.n,
                           s.max_size);
        }
    }
} printstats;

}

__thread int correct_repo = -1;
__thread const IdSet * watching = 0;

template<class Set>
void
insert_choices(IdSet & possible_choices, const Set & s,
               const std::string & name)
{
    IdSet filtered_choices;

    if (watching) {
        set<int> res;
        std::set_difference(s.begin(), s.end(),
                            watching->begin(), watching->end(),
                            inserter(res, res.end()));
        filtered_choices.insert(res.begin(), res.end());
    }
    else filtered_choices.insert(s.begin(), s.end());

    bool correct_before = possible_choices.count(correct_repo);
    size_t before = possible_choices.size();
    possible_choices.insert(filtered_choices.begin(), filtered_choices.end());
    bool correct_after = possible_choices.count(correct_repo);
    size_t after = possible_choices.size();

    Guard guard(stats_lock);
    Name_Stats & st = stats[name];
    if (!s.empty()) ++st.n;
    st.total_size += filtered_choices.size();
    st.correct += filtered_choices.count(correct_repo);
    st.incremental_size += (after - before);
    st.incremental_correct += (correct_after && !correct_before);
    st.max_size = std::max(st.max_size, filtered_choices.size());
}

void
Candidate_Generator::
candidates(Ranked & candidates, Candidate_Data & candidate_data,
           const Data & data, int user_id) const
{
    IdSet possible_choices;

    vector<Ranked> source_ranked(sources.size());
    vector<int> num_kept(sources.size());

    candidates.clear();

    // First, generate a set of those that we want to keep
    for (unsigned i = 0;  i < sources.size();  ++i) {
        Ranked & source_entries = source_ranked[i];

        sources[i]->gen_candidates(source_entries, user_id, data,
                                   candidate_data);

        num_kept[i] = source_entries.size();

        IdSet to_keep;
        for (unsigned j = 0;  j < source_entries.size();  ++j)
            if (source_entries[j].keep)
                to_keep.insert(source_entries[j].repo_id);

        insert_choices(possible_choices, to_keep, sources[i]->name());
    }

    map<int, int> repo_to_result;

    // Add to the data all of the information that we need to keep about them,
    for (unsigned i = 0;  i < sources.size();  ++i) {
        const Ranked & source_entries = source_ranked[i];

        for (unsigned j = 0;  j < source_entries.size();  ++j) {
            int repo_id = source_entries[j].repo_id;

            if (repo_id == -1 || repo_id >= data.repos.size()
                || data.repos[repo_id].invalid())
                throw Exception("source " + sources[i]->name()
                                + " produced invalid repo num "
                                + ostream_format(repo_id));

            if (!possible_choices.count(repo_id)) continue;

            if (!repo_to_result.count(repo_id)) {
                repo_to_result[repo_id] = candidates.size();
                candidates.push_back(Ranked_Entry());
                candidates.back().repo_id = repo_id;
            }

            candidate_data.info[source_entries[j].repo_id][i]
                = source_entries[j];
        }
    }

    // Finally, go through and calculate the features
    for (unsigned i = 0;  i < candidates.size();  ++i) {
        Ranked_Entry & entry = candidates[i];
        int repo_id = entry.repo_id;

        distribution<float> & features = entry.features;

        map<int, Ranked_Entry> & info_entry
            = candidate_data.info[repo_id];

        features.clear();
        Candidate_Source::common_features(features, user_id, repo_id,
                                          data, candidate_data);

        int total_rank = 0, min_rank = 10000, max_rank = 0, num_in = 0;
        float total_score = 0.0, min_score = 2.0, max_score = -1.0;

        // Go for each source
        for (unsigned j = 0;  j < sources.size();  ++j) {
            if (!info_entry.count(j)) {
                features.insert(features.end(), source_num_features[j], NaN);
                features.push_back(1000);   // rank
                features.push_back(2.0);    // percentile
                features.push_back(-1.0);   // score
                total_rank += num_kept[j] + 1;
                continue;
            }

            Ranked_Entry & source_entry = info_entry[j];
            ++num_in;
            total_rank += source_entry.min_rank;
            min_rank = std::min(min_rank, source_entry.min_rank);
            max_rank = std::max(max_rank, source_entry.min_rank);
            total_score += source_entry.score;
            min_score = std::min(min_score, source_entry.score);
            max_score = std::max(max_score, source_entry.score);

            if (source_entry.features.size() != source_num_features[j])
                throw Exception("num features for " + sources[j]->name()
                                + " doesn't match");

            features.insert(features.end(),
                            source_entry.features.begin(),
                            source_entry.features.end());
            features.push_back(source_entry.min_rank);
            features.push_back(1.0f * source_entry.min_rank
                               / source_ranked[j].size());
            features.push_back(source_entry.score);
        }

        features.push_back(total_rank);
        features.push_back(min_rank);
        features.push_back(max_rank);
        features.push_back(num_in);
        features.push_back(1.0 * total_rank / num_in);
        features.push_back(total_score);
        features.push_back(min_score);
        features.push_back(max_score);
        features.push_back(1.0 * total_score / num_in);

        entry.score = total_score;
    }

    // Sort them so that they're in a reasonable order
    candidates.sort();

    for (unsigned i = 0;  i < candidates.size();  ++i) {
        Ranked_Entry & entry = candidates[i];
        entry.index = i;
    }
}


/*****************************************************************************/
/* RANKER                                                                    */
/*****************************************************************************/

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

    result->add_feature("language_dprod", Feature_Info::REAL);
    result->add_feature("language_cosine", Feature_Info::REAL);

    result->add_feature("user_repo_singular_dp", Feature_Info::REAL);
    result->add_feature("user_repo_singular_unscaled_dp", Feature_Info::REAL);
    result->add_feature("user_repo_singular_unscaled_dp_max",
                        Feature_Info::REAL);
    result->add_feature("user_repo_singular_unscaled_dp_max_norm",
                        Feature_Info::REAL);
    result->add_feature("user_repo_centroid_repo_cosine", Feature_Info::REAL);

    result->add_feature("repo_name_contains_user", Feature_Info::BOOLEAN);
    result->add_feature("user_name_contains_repo", Feature_Info::BOOLEAN);
    result->add_feature("repos_authored_by", Feature_Info::REAL);
    result->add_feature("author_has_watchers", Feature_Info::REAL);
    result->add_feature("num_repos_with_same_name", Feature_Info::REAL);
    result->add_feature("num_watchers_of_repos_with_same_name", Feature_Info::REAL);

    result->add_feature("user_name_inferred", Feature_Info::BOOLEAN);
    result->add_feature("user_num_inferred_authors", Feature_Info::BOOLEAN);

    result->add_feature("user_repo_cooccurrences", Feature_Info::REAL);
    result->add_feature("user_repo_cooccurrences_avg", Feature_Info::REAL);
    result->add_feature("user_repo_cooccurrences_max", Feature_Info::REAL);
    result->add_feature("user_num_cooccurrences", Feature_Info::REAL);

    result->add_feature("user_repo_cooccurrences2", Feature_Info::REAL);
    result->add_feature("user_repo_cooccurrences_avg2", Feature_Info::REAL);
    result->add_feature("user_repo_cooccurrences_max2", Feature_Info::REAL);
    result->add_feature("user_num_cooccurrences2", Feature_Info::REAL);

    result->add_feature("repo_user_cooccurrences", Feature_Info::REAL);
    result->add_feature("repo_user_cooccurrences_avg", Feature_Info::REAL);
    result->add_feature("repo_user_cooccurrences_max", Feature_Info::REAL);
    result->add_feature("repo_num_cooccurrences", Feature_Info::REAL);

    result->add_feature("repo_user_cooccurrences2", Feature_Info::REAL);
    result->add_feature("repo_user_cooccurrences_avg2", Feature_Info::REAL);
    result->add_feature("repo_user_cooccurrences_max2", Feature_Info::REAL);
    result->add_feature("repo_num_cooccurrences2", Feature_Info::REAL);

    result->add_feature("repo_date", Feature_Info::REAL);
    result->add_feature("author_date", Feature_Info::REAL);
    result->add_feature("author_repo_date_difference", Feature_Info::REAL);
    result->add_feature("author_num_followers", Feature_Info::REAL);
    result->add_feature("author_num_following", Feature_Info::REAL);

    result->add_feature("user_date", Feature_Info::REAL);
    result->add_feature("user_repo_date_difference", Feature_Info::REAL);
    result->add_feature("user_author_date_difference", Feature_Info::REAL);
    result->add_feature("user_num_followers", Feature_Info::REAL);
    result->add_feature("user_num_following", Feature_Info::REAL);

    result->add_feature("repo_in_id_range", Feature_Info::BOOLEAN);
    result->add_feature("user_in_id_range", Feature_Info::BOOLEAN);
    result->add_feature("repo_id_range_size", Feature_Info::REAL);
    result->add_feature("user_id_range_size", Feature_Info::REAL);
    result->add_feature("id_range_suspicious_repo", Feature_Info::BOOLEAN);
    result->add_feature("id_range_suspicious_user", Feature_Info::BOOLEAN);
    result->add_feature("id_range_score", Feature_Info::REAL);

    result->add_feature("keyword_overlap_score", Feature_Info::REAL);
    result->add_feature("keyword_overlap_score_norm", Feature_Info::REAL);
    result->add_feature("keyword_overlap_idf", Feature_Info::REAL);
    result->add_feature("keyword_overlap_idf_norm", Feature_Info::REAL);
    result->add_feature("keyword_overlap_count", Feature_Info::REAL);
    result->add_feature("user_nkeywords", Feature_Info::REAL);
    result->add_feature("user_keyword_factor", Feature_Info::REAL);
    result->add_feature("user_keyword_idf_factor", Feature_Info::REAL);
    result->add_feature("repo_nkeywords", Feature_Info::REAL);
    result->add_feature("repo_keyword_factor", Feature_Info::REAL);
    result->add_feature("repo_keyword_idf_factor", Feature_Info::REAL);

    result->add_feature("author_user_dp", Feature_Info::REAL);
    result->add_feature("author_user_dp_norm", Feature_Info::REAL);
    result->add_feature("max_dp_with_watched", Feature_Info::REAL);
    result->add_feature("max_dp_with_watched_norm", Feature_Info::REAL);

    return result;
}

void
Ranker::
features(std::vector<ML::distribution<float> > & results,
         int user_id,
         const Ranked & candidates,
         const Candidate_Data & candidate_data,
         const Data & data) const
{
    generator->features(results, user_id, candidates, candidate_data, data);

    Ranked heuristic = candidates;
    Ranker::rank(heuristic, user_id, candidate_data, data);
    heuristic.sort();

    const User & user = data.users[user_id];

    // Get cooccurrences for all repos
    Cooccurrences user_keywords, user_keywords_idf;

    for (IdSet::const_iterator
             it = user.watching.begin(), end = user.watching.end();
         it != end;  ++it) {
        user_keywords.add(data.repos[*it].keywords);
        user_keywords_idf.add(data.repos[*it].keywords_idf);
    }

    user_keywords.finish();
    user_keywords_idf.finish();

    float user_keywords_2norm
        = sqrt(user_keywords.overlap(user_keywords).first);
    float user_keywords_idf_2norm
        = sqrt(user_keywords_idf.overlap(user_keywords_idf).first);

    for (unsigned i = 0;  i < heuristic.size();  ++i) {
        distribution<float> & result = results[heuristic[i].index];

        int repo_id = heuristic[i].repo_id;
        const Repo & repo = data.repos[repo_id];

        result.push_back(heuristic[i].score);
        result.push_back((heuristic[i].min_rank + heuristic[i].max_rank) * 0.5);
        result.push_back(result.back() / heuristic.size());

        float dp = repo.language_vec.dotprod(user.language_vec);

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

        dp = (repo.singular_vec * data.singular_values)
             .dotprod(user.singular_vec);

        result.push_back(dp);

        distribution<float> dpvec = (repo.singular_vec * user.singular_vec);

        result.push_back(dpvec.total());
        result.push_back(dpvec.max());
        result.push_back(dpvec.max() / dpvec.total());

        dp = -1.0;
        if (user.repo_centroid.size() && repo.singular_vec.size())
            dp = repo.singular_vec.dotprod(user.repo_centroid)
                / repo.singular_2norm;

        result.push_back(dp);

        bool valid_author
            = repo.author >= 0 && repo.author < data.authors.size();

        string author_name
            = (valid_author ? data.authors[repo.author].name : string());

        const Data::Name_Info & name_info = data.name_to_repos(repo.name);

        result.push_back(author_name.find(repo.name) != string::npos);
        result.push_back(repo.name.find(author_name) != string::npos);

        if (valid_author) {
            result.push_back(data.authors[repo.author].repositories.size());
            result.push_back(data.authors[repo.author].num_watchers);
        }
        else {
            result.push_back(-1);
            result.push_back(-1);
        }

        result.push_back(name_info.size());
        result.push_back(name_info.num_watchers);
        result.push_back(user.inferred_authors.count(repo.author));
        result.push_back(user.inferred_authors.size());

        // Find num cooc for this repo with each repo already watched
        double total_cooc = 0.0, max_cooc = 0.0;
        double total_cooc2 = 0.0, max_cooc2 = 0.0;

        boost::tie(total_cooc, max_cooc)
            = repo.cooc.overlap(user.watching);
        boost::tie(total_cooc2, max_cooc2)
            = repo.cooc2.overlap(user.watching);
        
        result.push_back(total_cooc);
        result.push_back(total_cooc / user.watching.size());
        result.push_back(max_cooc);
        result.push_back(user.cooc.size());

        result.push_back(total_cooc2);
        result.push_back(total_cooc2 / user.watching.size());
        result.push_back(max_cooc2);
        result.push_back(user.cooc2.size());

        // Find num cooc with each repo already watched
        total_cooc = max_cooc = total_cooc2 = max_cooc2 = 0.0;
        boost::tie(total_cooc, max_cooc)
            = user.cooc.overlap(repo.watchers);
        boost::tie(total_cooc2, max_cooc2)
            = user.cooc2.overlap(repo.watchers);

        result.push_back(total_cooc);
        result.push_back(total_cooc / repo.watchers.size());
        result.push_back(max_cooc);
        result.push_back(repo.cooc.size());

        result.push_back(total_cooc2);
        result.push_back(total_cooc2 / repo.watchers.size());
        result.push_back(max_cooc2);
        result.push_back(repo.cooc2.size());

        
        static const boost::gregorian::date epoch(2007, 1, 1);

        long repo_date = (repo.date - epoch).days();

        result.push_back(repo_date);

        long author_date = 0;

        if (repo.author != -1 && data.authors[repo.author].num_followers != -1){
            author_date = (data.authors[repo.author].date - epoch).days();
        }

        result.push_back(author_date);
        result.push_back(repo_date - author_date);

        if (repo.author != -1) {
            result.push_back(data.authors[repo.author].num_followers);
            result.push_back(data.authors[repo.author].num_following);
        }
        else {
            result.push_back(-1);
            result.push_back(-1);
        }

        long user_date = 10000;
        int author_num_followers = -1;
        int author_num_following = -1;
        for (IdSet::const_iterator
                 it = user.inferred_authors.begin(),
                 end = user.inferred_authors.end();
             it != end;  ++it) {
            if (data.authors[*it].num_followers != -1) {
                author_num_followers = max(author_num_followers,
                                           data.authors[*it].num_followers);
                author_num_following = max(author_num_following,
                                           data.authors[*it].num_following);
                user_date = min(user_date,
                                (data.authors[*it].date - epoch).days());
            }
        }

        result.push_back(user_date);
        result.push_back(repo_date - user_date);
        result.push_back(author_date - user_date);
        result.push_back(author_num_followers);
        result.push_back(author_num_following);

        bool repo_in_id_range
            = repo_id >= user.min_repo && repo_id <= user.max_repo;
        bool user_in_id_range
            = user_id >= repo.min_user && user_id <= repo.max_user;
        bool suspicious_user
            = user.watching.empty()
            || *user.watching.begin() > user.max_repo;
        bool suspicious_repo
            = repo.watchers.empty()
            || *repo.watchers.begin() > repo.max_user;

        result.push_back(repo_in_id_range);
        result.push_back(user_in_id_range);
        result.push_back(repo.max_user - repo.min_user);
        result.push_back(user.max_repo - user.min_repo);
        result.push_back(suspicious_user);
        result.push_back(suspicious_repo);

        // Little heuristic score to represent suspicious users and repos
        {
            int score = 0;
            if (repo_in_id_range || user_in_id_range) {
                score += repo_in_id_range;
                score += user_in_id_range;
                score += 2 * suspicious_user;
                score += 2 * suspicious_repo;
                score += 2 * (suspicious_user && suspicious_repo);
            }
            result.push_back(score);
        }

        {
            float score, count;
            boost::tie(score, count)
                = repo.keywords.overlap(user_keywords);

            result.push_back(score);

            float norm = repo.keywords_2norm * user_keywords_2norm;
            if (norm == 0.0)
                result.push_back(-2.0);
            else result.push_back(score / norm);


            boost::tie(score, count)
                = repo.keywords_idf.overlap(user_keywords_idf);

            result.push_back(score);

            norm = repo.keywords_idf_2norm * user_keywords_idf_2norm;
            if (norm == 0.0)
                result.push_back(-2.0);
            else result.push_back(score / norm);
            
            result.push_back(count);

            result.push_back(user_keywords.size());
            result.push_back(user_keywords_2norm);
            result.push_back(user_keywords_idf_2norm);
            result.push_back(repo.keywords.size());
            result.push_back(repo.keywords_2norm);
            result.push_back(repo.keywords_idf_2norm);
        }

        int author = repo.author;
        if (author != -1) {
            float best_dp = -2.0, best_dp_norm = -2.0;

            for (IdSet::const_iterator
                     jt = data.authors[author].possible_users.begin(),
                     jend = data.authors[author].possible_users.end();
                 jt != jend;  ++jt) {

                if (*jt == -1) continue;

                const User & user2 = data.users[*jt];

                float dp = user.singular_vec.dotprod(user2.singular_vec);
                float dp_norm
                    = xdiv(dp, user.singular_2norm * user2.singular_2norm);
                best_dp = max(best_dp, dp);
                best_dp_norm = max(best_dp_norm, dp_norm);
            }

            result.push_back(best_dp);
            result.push_back(best_dp_norm);
        }
        else {
            result.push_back(NaN);
            result.push_back(NaN);
        }
        
        float best_dp = -2.0, best_dp_norm = -2.0;
        
        for (IdSet::const_iterator
                 jt = user.watching.begin(),
                 jend = user.watching.end();
             jt != jend;  ++jt) {
            if (*jt == -1) continue;
            const Repo & repo2 = data.repos[*jt];
            float dp = repo.singular_vec.dotprod(repo2.singular_vec);
            float dp_norm
                = xdiv(dp, repo.singular_2norm * repo2.singular_2norm);
            best_dp = max(best_dp, dp);
            best_dp_norm = max(best_dp_norm, dp_norm);
        }

        result.push_back(best_dp);
        result.push_back(best_dp_norm);
    }
}

void
Ranker::
rank(Ranked & candidates,
     int user_id,
     const Candidate_Data & candidate_data,
     const Data & data) const
{
    // Assume that the score is OK
    return;

#if 0 // old heuristic score
    for (unsigned i = 0;  i < candidates.size();  ++i) {
        const Ranked_Entry & c = candidates[i];

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

        // Add in a popularity score
        score += popularity * 20;

        Ranked_Entry entry;
        entry.index = i;
        entry.repo_id = repo_id;
        entry.score = score;

        result.push_back(entry);
    }
#endif
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

    config.require(classifier_file, "classifier_file");

    load_data = true;
    config.get(load_data, "load_data");
}

void
Classifier_Ranker::
init(boost::shared_ptr<Candidate_Generator> generator)
{
    Ranker::init(generator);

    ranker_fs = feature_space();

    if (!load_data) return;

    classifier.load(classifier_file);

    classifier_fs = classifier.feature_space<ML::Dense_Feature_Space>();

    opt_info = classifier.impl->optimize(classifier_fs->features());

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

void
Classifier_Ranker::
features(std::vector<ML::distribution<float> > & features,
         int user_id,
         const Ranked & candidates,
         const Candidate_Data & candidate_data,
         const Data & data) const
{
    Ranker::features(features, user_id, candidates, candidate_data, data);
}

void
Classifier_Ranker::
classify(Ranked & candidates,
         int user_id,
         const Candidate_Data & candidate_data,
         const Data & data,
         const std::vector<ML::distribution<float> > & features) const
{
    for (unsigned i = 0;  i < candidates.size();  ++i) {
        Ranked_Entry & entry = candidates[i];
        int repo_id = entry.repo_id;

        float encoded[classifier_fs->variable_count()];
        classifier_fs->encode(&features[i][0], encoded, *ranker_fs, mapping);
        float score = classifier.impl->predict(1, encoded, opt_info);

        entry.index = i;
        entry.repo_id = repo_id;
        entry.score = score;
    }
}

void
Classifier_Ranker::
rank(Ranked & candidates,
     int user_id,
     const Candidate_Data & candidate_data,
     const Data & data) const
{
    vector<distribution<float> > features;
    this->features(features, user_id, candidates, candidate_data, data);

    classify(candidates, user_id, candidate_data, data, features);
}


/*****************************************************************************/
/* CLASSIFIER_RERANKER                                                       */
/*****************************************************************************/

Classifier_Reranker::~Classifier_Reranker()
{
}

void
Classifier_Reranker::
configure(const ML::Configuration & config_,
          const std::string & name)
{
    Classifier_Ranker::configure(config_, name);

    Configuration config(config_, name, Configuration::PREFIX_APPEND);

    phase1.configure(config, "phase1");
}

void
Classifier_Reranker::
init(boost::shared_ptr<Candidate_Generator> generator)
{
    phase1.generator = generator;
    this->generator = generator;
    phase1.init(generator);
    Classifier_Ranker::init(generator);
}

boost::shared_ptr<const ML::Dense_Feature_Space>
Classifier_Reranker::
feature_space() const
{
    boost::shared_ptr<ML::Dense_Feature_Space> result;
    result.reset(new ML::Dense_Feature_Space(*phase1.feature_space()));

    // NOTE: these features might not be a good idea, as it might be able to
    // model how the FV are selected in order to figure out what the correct
    // one is (for example, if prerank_rank > 20 then we can be sure that
    // it's the correct one as we only include a maximum of 20 incorrect
    // examples, and these are the top ranked ones.
    result->add_feature("prerank_score", Feature_Info::REAL);
    result->add_feature("prerank_rank", Feature_Info::REAL);
    result->add_feature("prerank_percentile", Feature_Info::REAL);
    return result;
}

void
Classifier_Reranker::
features(std::vector<ML::distribution<float> > & result,
         int user_id,
         const Ranked & candidates,
         const Candidate_Data & candidate_data,
         const Data & data) const
{
    phase1.features(result, user_id, candidates, candidate_data, data);

    // First, we rank with the phase 1 classifier
    Ranked ranked = candidates;
    phase1.classify(ranked, user_id, candidate_data, data, result);
    ranked.sort();

#if 0
    hash_map<int, int> id_to_index;
    for (unsigned i = 0;  i < ranked.size();  ++i)
        id_to_index[ranked[i].repo_id] = ranked[i].index;

    // For each of the generation algorithms, we look at the rank within that
    // set
    const IdSet & ids = candidate_data.parents_of_watched;
    vector<pair<int, float> > ranked;
    ranked.reserve(ids.size());
    for (IdSet::const_iterator it = ids.begin(), end = ids.end();
         it != end;  ++it)
        ranked.push_back(make_pair(*it, ranked[id_to_index[*it]].score));
    sort_on_second_descending(ranked);
#endif    


    // Now, go through and add the extra features in
    for (unsigned i = 0;  i < ranked.size();  ++i) {
        distribution<float> & fv = result[ranked[i].index];
        fv.push_back(ranked[i].score);
        fv.push_back((ranked[i].min_rank + ranked[i].max_rank) * 0.5);
        fv.push_back(fv.back() / ranked.size());
    }
}

void
Classifier_Reranker::
rank(Ranked & candidates,
     int user_id,
     const Candidate_Data & candidate_data,
     const Data & data) const
{
    if (!load_data || true) {
        phase1.rank(candidates, user_id, candidate_data, data);
        return;
    }

    Classifier_Ranker::rank(candidates, user_id, candidate_data, data);
}

void
Classifier_Reranker::
classify(Ranked & candidates,
         int user_id,
         const Candidate_Data & candidate_data,
         const Data & data,
         const std::vector<ML::distribution<float> > & features) const
{
    hash_map<int, vector<pair<int, float> > > rank_per_author;

    for (unsigned i = 0;  i < candidates.size();  ++i) {
        Ranked_Entry & entry = candidates[i];
        int repo_id = entry.repo_id;

        float score = features[i][features[i].size() - 3];

        rank_per_author[data.repos[repo_id].author]
            .push_back(make_pair(repo_id, score));
    }
    
    for (hash_map<int, vector<pair<int, float> > >::iterator
             it = rank_per_author.begin(), end = rank_per_author.end();
         it != end;  ++it)
        sort_on_second_descending(it->second);

    for (unsigned i = 0;  i < candidates.size();  ++i) {
        Ranked_Entry & entry = candidates[i];

        float score;

#if 1
        int rank = features[i][features[i].size() - 2];
        if (rank > 200) score = 0.0;
        else {
            float encoded[classifier_fs->variable_count()];
            classifier_fs->encode(&features[i][0], encoded, *ranker_fs,
                                  mapping);
            score = classifier.impl->predict(1, encoded, opt_info);
        }
#else

        // Enforce diversity in authors
        int n_per_author = 50;

        const vector<pair<int, float> > & author_ranks
            = rank_per_author[data.repos[repo_id].author];
        bool found = false;
        for (int j = 0;\
             j < author_ranks.size() && j < n_per_author && !found; ++j) {
            found = (author_ranks[j].first == repo_id);
        }
        
        if (found) score = features[i][features[i].size() - 3];
        else score = 0.0;
#endif
        entry.score = score;
    }
}


/*****************************************************************************/
/* FACTORY                                                                   */
/*****************************************************************************/

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
    else if (type == "reranker") {
        result.reset(new Classifier_Reranker());
    }
    else throw Exception("Ranker of type " + type + " doesn't exist");

    result->configure(config_, name);
    result->init(generator);

    return result;
}

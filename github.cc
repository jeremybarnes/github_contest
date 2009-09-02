/* github.cc                                                       -*- C++ -*-
   Jeremy Barnes, 6 August 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   Code to implement a basic github recommendation engine.
*/

#include "data.h"
#include "ranker.h"
#include "decompose.h"
#include "keywords.h"

#include <fstream>
#include <iterator>
#include <iostream>

#include "arch/exception.h"
#include "utils/string_functions.h"
#include "utils/pair_utils.h"
#include "utils/vector_utils.h"
#include "utils/filter_streams.h"
#include "utils/configuration.h"
#include "arch/timers.h"
#include "utils/guard.h"

#include "boosting/worker_task.h"

#include <boost/program_options/cmdline.hpp>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/positional_options.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/variables_map.hpp>
#include <boost/progress.hpp>
#include <boost/tuple/tuple.hpp>
#include <boost/bind.hpp>


using namespace std;
using namespace ML;


struct Result_Stats {
    size_t n_correct;
    size_t n_in_set;
    size_t n_choices;
    size_t n_results;

    Result_Stats()
        : n_correct(0), n_in_set(0), n_choices(0), n_results(0)
    {
    }

    void add(bool correct, bool possible, size_t choices)
    {
        n_correct += correct;
        n_in_set  += possible;
        n_choices += choices;
        n_results += 1;
    }

    std::string print() const
    {
        return format("     total:      real: %4zd/%4zd = %6.2f%%  "
                      "poss: %4zd/%4zd = %6.2f%%  avg num: %5.1f\n",
                      n_correct, n_results,
                      100.0 * n_correct / n_results,
                      n_in_set, n_results,
                      100.0 * n_in_set / n_results,
                      n_choices * 1.0 / n_results);
    }
};

struct Global_Info {
    const Data & data;

    bool dump_source_data;
    bool dump_merger_data;
    bool dump_predictions;
    bool dump_results;

    bool train_discriminative;
    bool possible_only;
    bool include_all_correct;

    boost::shared_ptr<const Candidate_Source> source;
    boost::shared_ptr<const Candidate_Generator> generator;
    boost::shared_ptr<const Ranker> ranker;
    boost::shared_ptr<const Dense_Feature_Space> source_fs, ranker_fs;


    // This lock protects everything below this point
    Lock lock;
    ostream & out;

    int up_to_job;
    map<int, string> jobs_waiting;

    Global_Info(const Data & data,
                ostream & out,
                bool dump_source_data,
                bool dump_merger_data,
                bool dump_predictions,
                bool dump_results,
                bool train_discriminative,
                bool possible_only,
                bool include_all_correct,
                boost::shared_ptr<const Candidate_Source> source,
                boost::shared_ptr<const Candidate_Generator> generator,
                boost::shared_ptr<const Ranker> ranker,
                boost::shared_ptr<const Dense_Feature_Space> source_fs,
                boost::shared_ptr<const Dense_Feature_Space> ranker_fs)
        : data(data), dump_source_data(dump_source_data),
          dump_merger_data(dump_merger_data),
          dump_predictions(dump_predictions),
          dump_results(dump_results),
          train_discriminative(train_discriminative),
          possible_only(possible_only),
          include_all_correct(include_all_correct),
          source(source), generator(generator), ranker(ranker),
          source_fs(source_fs), ranker_fs(ranker_fs),
          out(out)
    {
        up_to_job = 0;
    }
};

// A job to be performed by a worker task to work on a user
struct Do_User_Job {

    Global_Info & info;
    int job_num;

    boost::shared_ptr<ostringstream> out_ptr;
    ostringstream & out;

    vector<int> user_ids;
    vector<int> correct_repo_ids;
    vector<set<int> *> all_results;
    vector<vector<int> *> all_possible_choices;
    vector<vector<int> *> all_non_zero;

    boost::progress_display & progress;

    Do_User_Job(Global_Info & info,
                int job_num,
                const vector<int> & user_ids,
                const vector<int> & correct_repo_ids,
                const vector<set<int> *> & all_results,
                const vector<vector<int> *> & all_possible_choices,
                const vector<vector<int> *> & all_non_zero,
                boost::progress_display & progress)
        : info(info), job_num(job_num),
          out_ptr(new ostringstream()), out(*out_ptr),
          user_ids(user_ids),
          correct_repo_ids(correct_repo_ids),
          all_results(all_results),
          all_possible_choices(all_possible_choices),
          all_non_zero(all_non_zero),
          progress(progress)
    {
    }

    void operator () ()
    {
        for (unsigned i = 0;  i < user_ids.size();  ++i) {
            do_user(user_ids[i], correct_repo_ids[i],
                    *all_results[i], *all_possible_choices[i],
                    *all_non_zero[i]);
        }
        
        // Now, re-assemble the text written for the output
        Guard guard(info.lock);

        progress += user_ids.size();

        // enqueue our results
        info.jobs_waiting[job_num] = out.str();
        
        // Catch up any other jobs
        for (; info.jobs_waiting.count(info.up_to_job);  ++info.up_to_job) {
            info.out << info.jobs_waiting[info.up_to_job];
            info.jobs_waiting.erase(info.up_to_job);
        }
    }

    void do_user(int user_id, int correct_repo_id,
                 set<int> & results,
                 vector<int> & result_possible_choices,
                 vector<int> & result_non_zero)
    {
        const Data & data = info.data;

        const User & user = data.users[user_id];

        correct_repo = correct_repo_id;
        watching = &user.watching;

        if (info.dump_source_data) {
            
            Candidate_Data candidate_data;

            Ranked candidates;
            info.source->candidate_set(candidates, user_id, data,
                                       candidate_data);

            if (candidates.empty()) return;

            set<int> possible_choices;
            for (unsigned j = 0;  j < candidates.size();  ++j)
                possible_choices.insert(candidates[j].repo_id);

            out << "# user_id " << user_id << " correct " << correct_repo_id
                << " ncandidates " << candidates.size() << " possible "
                << possible_choices.count(correct_repo_id)
                << endl;
            
            // Divide into two sets: those that predict a watched repo,
            // and those that don't
            
            set<int> incorrect;
            set<int> correct;
            
            std::set_difference(possible_choices.begin(),
                                possible_choices.end(),
                                user.watching.begin(),
                                user.watching.end(),
                                inserter(incorrect, incorrect.end()));
            incorrect.erase(correct_repo_id);
            
            if (incorrect.size() > 20) {
                
                vector<int> sample(incorrect.begin(), incorrect.end());
                std::random_shuffle(sample.begin(), sample.end());
                
                incorrect.clear();
                incorrect.insert(sample.begin(), sample.begin() + 20);
            }
                
            if (info.include_all_correct) {
                std::set_intersection(possible_choices.begin(),
                                      possible_choices.end(),
                                      user.watching.begin(),
                                      user.watching.end(),
                                      inserter(correct, correct.end()));
                
                
                    
                if (correct.size() > 20) {
                    
                    vector<int> sample(correct.begin(), correct.end());
                    std::random_shuffle(sample.begin(), sample.end());
                    
                    correct.clear();
                    correct.insert(sample.begin(), sample.begin() + 20);
                }
            }
            
            correct.insert(correct_repo_id);
            
            // Go through and dump those selected
            for (unsigned j = 0;  j < candidates.size();  ++j) {
                const Ranked_Entry & candidate = candidates[j];
                int repo_id = candidate.repo_id;
                
                // if it's one we don't dump then don't output it
                if (!correct.count(repo_id)
                    && !incorrect.count(repo_id))
                    continue;
                
                bool label = correct.count(repo_id);
                float weight = (label
                                ? 1.0f / correct.size()
                                : 1.0f / incorrect.size());
                
                int group = user_id;
                
                out << label << " " << weight << " " << group << " "
                    << (repo_id == correct_repo_id) << " ";

                // Get the common features
                distribution<float> features;
                Candidate_Source::common_features(features, user_id, repo_id,
                                                  data, candidate_data);
                
                features.insert(features.end(),
                                candidate.features.begin(),
                                candidate.features.end());

                boost::shared_ptr<Mutable_Feature_Set> encoded
                    = info.source_fs->encode(features);
                out << info.source_fs->print(*encoded);
                
                // A comment so we know where this feature vector came from
                out << " # repo " << repo_id << " "
                    << (data.repos[repo_id].author == -1 ? "????"
                        : data.authors[data.repos[repo_id].author].name.c_str())
                    << "/" << data.repos[repo_id].name << endl;
            }
            
            out << endl << endl;

            return;
        }
        
        // Not doing source training
        Ranked candidates;
        Candidate_Data candidate_data;
        info.generator->candidates(candidates, candidate_data, data, user_id);

        set<int> possible_choices;
        for (unsigned j = 0;  j < candidates.size();  ++j)
            possible_choices.insert(candidates[j].repo_id);

        if (!info.dump_merger_data || info.train_discriminative) {
            info.ranker->rank(candidates, user_id, candidate_data,
                              data);
            candidates.sort();
        }

        if (info.dump_source_data) {
            bool possible = possible_choices.count(correct_repo_id);

            out << "# user_id " << user_id << " correct " << correct_repo_id
                << " npossible " << possible_choices.size() << " possible "
                << possible
                << endl;

            if (!possible) {
                out << endl;
                return;
            }

            // Divide into two sets: those that predict a watched repo,
            // and those that don't
            
            set<int> incorrect;
            set<int> correct;
            
            if (info.train_discriminative) {
                // Find the highest 10 incorrect examples from the ranked set
                for (unsigned i = 0;  i < candidates.size() && incorrect.size() < 10;  ++i) {
                    int repo_id = candidates[i].repo_id;
                    bool correct = (user.watching.count(repo_id)
                                    || repo_id == correct_repo_id);
                    if (correct) continue;
                    incorrect.insert(repo_id);
                }
            }
            else {
                std::set_difference(possible_choices.begin(),
                                    possible_choices.end(),
                                    user.watching.begin(),
                                    user.watching.end(),
                                    inserter(incorrect, incorrect.end()));
                incorrect.erase(correct_repo_id);
                
                if (incorrect.size() > 20) {
                    
                    vector<int> sample(incorrect.begin(), incorrect.end());
                    std::random_shuffle(sample.begin(), sample.end());
                    
                    incorrect.clear();
                    incorrect.insert(sample.begin(), sample.begin() + 20);
                }
                
                if (info.include_all_correct) {
                    std::set_intersection(possible_choices.begin(),
                                          possible_choices.end(),
                                          user.watching.begin(),
                                          user.watching.end(),
                                          inserter(correct, correct.end()));
                    
                    
                    
                    if (correct.size() > 20) {
                        
                        vector<int> sample(correct.begin(), correct.end());
                        std::random_shuffle(sample.begin(), sample.end());
                        
                        correct.clear();
                        correct.insert(sample.begin(), sample.begin() + 20);
                    }
                }
                
            }
            
            correct.insert(correct_repo_id);
            
            // Generate features
            vector<distribution<float> > features;
            info.ranker->features(features, user_id, candidates, candidate_data,
                                  data);
            
            // Go through and dump those selected
            for (unsigned j = 0;  j < candidates.size();  ++j) {
                const Ranked_Entry & candidate = candidates[j];
                int repo_id = candidate.repo_id;
                
                // if it's one we don't dump then don't output it
                if (!correct.count(repo_id)
                    && !incorrect.count(repo_id))
                    continue;
                
                bool label = correct.count(repo_id);
                float weight = (label
                                ? 1.0f / correct.size()
                                : 1.0f / incorrect.size());
                
                int group = user_id;
                
                out << label << " " << weight << " " << group << " "
                    << (repo_id == correct_repo_id) << " ";
                
                boost::shared_ptr<Mutable_Feature_Set> encoded
                    = info.ranker_fs->encode(features[j]);
                out << info.ranker_fs->print(*encoded);
                
                // A comment so we know where this feature vector came from
                out << " # repo " << repo_id << " "
                    << data.authors[data.repos[repo_id].author].name << "/"
                    << data.repos[repo_id].name << endl;
            }
            
            out << endl << endl;
        }


        if (info.dump_merger_data) {
            bool possible = possible_choices.count(correct_repo_id);

            out << "# user_id " << user_id << " correct " << correct_repo_id
                << " npossible " << possible_choices.size() << " possible "
                << possible
                << endl;

            if (!possible) {
                out << endl;
                return;
            }

            // Divide into two sets: those that predict a watched repo,
            // and those that don't
            
            set<int> incorrect;
            set<int> correct;
            
            if (info.train_discriminative) {
                // Find the highest 10 incorrect examples from the ranked set
                for (unsigned i = 0;  i < candidates.size() && incorrect.size() < 10;  ++i) {
                    int repo_id = candidates[i].repo_id;
                    bool correct = (user.watching.count(repo_id)
                                    || repo_id == correct_repo_id);
                    if (correct) continue;
                    incorrect.insert(repo_id);
                }
            }
            else {
                std::set_difference(possible_choices.begin(),
                                    possible_choices.end(),
                                    user.watching.begin(),
                                    user.watching.end(),
                                    inserter(incorrect, incorrect.end()));
                incorrect.erase(correct_repo_id);
                
                if (incorrect.size() > 20) {
                    
                    vector<int> sample(incorrect.begin(), incorrect.end());
                    std::random_shuffle(sample.begin(), sample.end());
                    
                    incorrect.clear();
                    incorrect.insert(sample.begin(), sample.begin() + 20);
                }
                
                if (info.include_all_correct) {
                    std::set_intersection(possible_choices.begin(),
                                          possible_choices.end(),
                                          user.watching.begin(),
                                          user.watching.end(),
                                          inserter(correct, correct.end()));
                    
                    
                    
                    if (correct.size() > 20) {
                        
                        vector<int> sample(correct.begin(), correct.end());
                        std::random_shuffle(sample.begin(), sample.end());
                        
                        correct.clear();
                        correct.insert(sample.begin(), sample.begin() + 20);
                    }
                }
                
            }
            
            correct.insert(correct_repo_id);
            
            // Generate features
            vector<distribution<float> > features;
            info.ranker->features(features, user_id, candidates, candidate_data,
                                  data);
            
            // Go through and dump those selected
            for (unsigned j = 0;  j < candidates.size();  ++j) {
                const Ranked_Entry & candidate = candidates[j];
                int repo_id = candidate.repo_id;
                
                // if it's one we don't dump then don't output it
                if (!correct.count(repo_id)
                    && !incorrect.count(repo_id))
                    continue;
                
                bool label = correct.count(repo_id);
                float weight = (label
                                ? 1.0f / correct.size()
                                : 1.0f / incorrect.size());
                
                int group = user_id;
                
                out << label << " " << weight << " " << group << " "
                    << (repo_id == correct_repo_id) << " ";
                
                boost::shared_ptr<Mutable_Feature_Set> encoded
                    = info.ranker_fs->encode(features[j]);
                out << info.ranker_fs->print(*encoded);
                
                // A comment so we know where this feature vector came from
                out << " # repo " << repo_id << " "
                    << (data.repos[repo_id].author == -1
                        ? "?????"
                        : data.authors[data.repos[repo_id].author].name.c_str())
                    << "/"
                    << data.repos[repo_id].name << endl;
            }
            
            out << endl << endl;
        }

        if (info.dump_merger_data || info.possible_only) {
            results = set<int>();
            result_possible_choices
                = vector<int>(possible_choices.begin(),
                              possible_choices.end());
            result_non_zero = vector<int>();
            return;
        }
        
        if (info.dump_predictions) {
            // verbosity...
            bool possible = possible_choices.count(correct_repo_id);

            out << "user_id " << user_id << " correct " << correct_repo_id
                << " npossible " << possible_choices.size() << " possible "
                << possible << " authors { ";
            for (IdSet::const_iterator
                     it = user.inferred_authors.begin(),
                     end = user.inferred_authors.end();
                 it != end;  ++it) {
                out << data.authors.at(*it).name << " ";
            }
            out << "}" << endl;

            int num_done = 0;

            out << " rank    score   c  prank repoid watch name" << endl;

            for (unsigned j = 0;  j < candidates.size();  ++j) {
                int repo_id = candidates[j].repo_id;

                bool correct = (correct_repo_id == repo_id
                                || user.watching.count(repo_id));

                if (correct && correct_repo_id != repo_id
                    && !info.include_all_correct) continue;

                if (num_done > 10 && correct_repo_id != repo_id) continue;
                
                ++num_done;

                out << format("%5d %8.6f %c %d %6d %6d %5d ", j, candidates[j].score,
                              (correct_repo_id == repo_id ? '*' : ' '),
                              correct,
                              data.repos[repo_id].popularity_rank,
                              repo_id,
                              data.repos[repo_id].watchers.size())
                    << data.authors[data.repos[repo_id].author].name << "/"
                    << data.repos[repo_id].name << endl;
            }

            if (!possible)
                out << format("               * 1 %6d %6d %5d ",
                              correct_repo_id,
                              data.repos[correct_repo_id].popularity_rank,
                              data.repos[correct_repo_id].watchers.size())
                    << data.authors[data.repos[correct_repo_id].author].name
                    << "/"
                    << data.repos[correct_repo_id].name << endl;

            out << endl;
        }

        // Extract the best ones
        set<int> user_results;
        set<int> nz;

        if (info.dump_results)
            out << user_id << ":";

        int dumped = 0;

        for (unsigned j = 0;  j < candidates.size(); ++j) {
            int repo_id = candidates[j].repo_id;

            if (user.watching.count(repo_id)) continue;  // already watched
            if (user_results.size() < 10)
                user_results.insert(repo_id);
            if (candidates[j].score > 0.0)
                nz.insert(repo_id);

            if (info.dump_results && dumped < 100) {
                if (dumped != 0)
                    out << ",";
                ++dumped;
                out << "{" << repo_id << "," << format("%.4f", candidates[j].score) << "}";
            }
        }
        if (info.dump_results) out << endl;

        results = user_results;
        result_possible_choices
            = vector<int>(possible_choices.begin(),
                          possible_choices.end());
        result_non_zero
            = vector<int>(nz.begin(), nz.end());
    }
};


int main(int argc, char ** argv)
{
    // Do we perform a fake test (where we test different users than the ones
    // from the real test) but it works locally.
    bool fake_test = false;

    // Do we dump a results file?
    bool dump_results = false;

    // Do we dump the predictions data?
    bool dump_predictions = false;

    // Filename to dump output data to
    string output_file;

    // Dump the data to train a merger classifier?
    bool dump_merger_data = false;

    // Include all correct entries or only the removed one?
    bool include_all_correct = false;

    // Train a discriminative merger file
    bool train_discriminative = false;

    // Configuration file to use
    string config_file = "config.txt";

    // Candidate generator to use
    string generator_name = "@default_generator";

    // Ranker to use
    string ranker_name = "@default_ranker";

    // Extra configuration options
    vector<string> extra_config_options;

    // Number of users for fake data generation
    int num_users = 4788;

    // Random seed for fake data generation
    int rseed = 0;

    // Cluster users?
    bool cluster_users = false;

    // Cluster repos?
    bool cluster_repos = false;

    // Calculate only possible/impossible (not ranking)
    bool possible_only = false;

    // Dump source data?
    bool dump_source_data = false;

    // Which source to dump?
    string source_to_train;

    // Tranche specification
    string tranches = "1";

    {
        using namespace boost::program_options;

        options_description config_options("Configuration");

        config_options.add_options()
            ("config-file,c", value<string>(&config_file),
             "configuration file to read configuration options from")
            ("generator-name,g", value<string>(&generator_name),
             "name of object to generate candidates to be ranked")
            ("ranker-name,r", value<string>(&ranker_name),
             "name of object to rank candidates")
            ("extra-config-option", value<vector<string> >(&extra_config_options),
             "extra configuration option=value (can go directly on command line)");

        options_description control_options("Control Options");

        control_options.add_options()
            ("fake-test,f", value<bool>(&fake_test)->zero_tokens(),
             "run a fake local test instead of generating real results")
            ("num-users,n", value<int>(&num_users),
             "number of users for fake test")
            ("random-seed", value<int>(&rseed),
             "random seed for fake data")
            ("dump-merger-data", value<bool>(&dump_merger_data)->zero_tokens(),
             "dump data to train a merger classifier")
            ("dump-source-data", value<bool>(&dump_source_data)->zero_tokens(),
             "dump data to train a classifier for the named repo source")
            ("source-to-train", value<string>(&source_to_train),
             "source to dump data for")
            ("dump-results", value<bool>(&dump_results)->zero_tokens(),
             "dump ranked results in official submission format")
            ("dump-predictions", value<bool>(&dump_predictions)->zero_tokens(),
             "dump predictions for debugging")
            ("include-all-correct", value<bool>(&include_all_correct),
             "include all correct (1, default) or only excluded correct (0)?")
            ("discriminative",
             value<bool>(&train_discriminative)->zero_tokens(),
             "train second-phase merger data for discrimination")
            ("possible-only", value<bool>(&possible_only),
             "only calculate metrics for possible/impossible (no ranking)")
            ("cluster-repos", value<bool>(&cluster_repos)->zero_tokens(),
             "cluster repositories, writing a cluster map")
            ("cluster-users", value<bool>(&cluster_users)->zero_tokens(),
             "cluster users, writing a cluster map")
            ("tranches", value<string>(&tranches),
             "bitmap of which parts of the testing set to use")
            ("output-file,o",
             value<string>(&output_file),
             "dump output file to the given filename");

        positional_options_description p;
        p.add("extra-config-option", -1);

        options_description all_opt;
        all_opt
            .add(config_options)
            .add(control_options);

        all_opt.add_options()
            ("help,h", "print this message");
        
        variables_map vm;
        store(command_line_parser(argc, argv)
              .options(all_opt)
              .positional(p)
              .run(),
              vm);
        notify(vm);

        if (vm.count("help")) {
            cout << all_opt << endl;
            return 1;
        }
    }

    // Load up configuration
    Configuration config;
    if (config_file != "") config.load(config_file);

    // Allow configuration to be overridden on the command line
    config.parse_command_line(extra_config_options);

    // Load up the data
    cerr << "loading data...";
    Data data;
    data.load();
    cerr << " done." << endl;

    if (fake_test || dump_merger_data || dump_source_data)
        data.setup_fake_test(num_users, rseed);

    Decomposition decomposition;
    decomposition.decompose(data);

    cerr << "doing keywords" << endl;
    analyze_keywords(data);
    cerr << "done keywords" << endl;

    // results file
    filter_ostream out(output_file);

    if (cluster_users) {
        decomposition.kmeans_users(data);
        decomposition.save_kmeans_users(out, data);
        return 0;
    }
    else if (cluster_repos) {
        decomposition.kmeans_repos(data);
        decomposition.save_kmeans_repos(out, data);
        return 0;
    }
    else {
        decomposition.load_kmeans_users("data/kmeans_users.txt", data);
        decomposition.load_kmeans_repos("data/kmeans_repos.txt", data);
    }

    if (generator_name != "" && generator_name[0] == '@')
        config.must_find(generator_name, string(generator_name, 1));

    boost::shared_ptr<Candidate_Generator> generator
        = get_candidate_generator(config, generator_name);

    if (ranker_name != "" && ranker_name[0] == '@')
        config.must_find(ranker_name, string(ranker_name, 1));

    boost::shared_ptr<Ranker> ranker
        = get_ranker(config, ranker_name, generator);

    boost::shared_ptr<Candidate_Source> source;
    boost::shared_ptr<const ML::Dense_Feature_Space> source_fs;
    if (dump_source_data) {
        source = get_candidate_source(config, source_to_train);
        source_fs = source->feature_space();
    }

    boost::shared_ptr<const ML::Dense_Feature_Space> ranker_fs
        = ranker->feature_space();

    // Dump the feature vector for the merger file
    if (dump_merger_data || dump_source_data) {
        // Get the feature space for the merger file

        // Write out the header
        out << "LABEL:k=BOOLEAN/o=BIASED "
            << "WT:k=REAL/o=BIASED "
            << "GROUP:k=REAL/o=GROUPING "
            << "REAL_TEST:k=BOOLEAN/o=BIASED ";
        if (dump_merger_data)
            out << ranker_fs->print();
        else out << source_fs->print();
        out << endl;
    }

    Global_Info info(data, out,
                     dump_source_data, dump_merger_data,
                     dump_predictions, dump_results,
                     train_discriminative, possible_only,
                     include_all_correct,
                     source, generator, ranker, source_fs, ranker_fs);

    Timer timer;

    vector<int> users_tested;
    vector<int> answers_tested;

    static Worker_Task & worker = Worker_Task::instance(num_threads() - 1);

    // Find everything that we need to do
    for (unsigned i = 0;  i < data.users_to_test.size();  ++i) {

        if (tranches[i % tranches.size()] == '0')
            continue;

        int user_id = data.users_to_test[i];

        users_tested.push_back(user_id);
        if (data.answers.size())
            answers_tested.push_back(data.answers[i]);
        else answers_tested.push_back(-1);
    }

    cerr << "processing " << users_tested.size() << " users..."
         << endl;
    
    boost::progress_display progress(users_tested.size(), cerr);


    vector<set<int> > results;
    vector<vector<int> > result_possible_choices;
    vector<vector<int> > result_non_zero;
    results.resize(users_tested.size());
    result_possible_choices.resize(users_tested.size());
    result_non_zero.resize(users_tested.size());

    // Now, submit it as jobs to the worker task to be done multithreaded
    int group;
    int job_num = 0;
    {
        int parent = -1;  // no parent group
        group = worker.get_group(NO_JOB, "dump user results task", parent);

        // Make sure the group gets unlocked once we've populated
        // everything
        Call_Guard guard(boost::bind(&Worker_Task::unlock_group,
                                     boost::ref(worker),
                                     group));
        
        for (unsigned i = 0;  i < users_tested.size();  i += 100, ++job_num) {
            vector<int> these_users;
            vector<int> these_answers;

            vector<set<int> *> these_results;
            vector<vector<int> *> these_possible_choices;
            vector<vector<int> *> these_non_zero;

            for (unsigned j = i;  j < i + 100 && j < users_tested.size();  ++j) {
                these_users.push_back(users_tested[j]);
                these_answers.push_back(answers_tested[j]);
                these_results.push_back(&results[j]);
                these_possible_choices.push_back(&result_possible_choices[j]);
                these_non_zero.push_back(&result_non_zero[j]);
            }


            // Create the job
            Do_User_Job job(info, job_num, these_users, these_answers,
                            these_results, these_possible_choices,
                            these_non_zero,
                            progress);

            // Send it to a thread to be processed
            worker.add(job, "do users job", group);
        }

    }

    // Add this thread to the thread pool until we're ready
    worker.run_until_finished(group);

    if (info.up_to_job != job_num)
        throw Exception("didn't finish jobs");

    cerr << "elapsed: " << timer.elapsed() << endl;

    if (dump_merger_data || dump_source_data) return(0);

    if (results.size() != users_tested.size())
        throw Exception("wrong number of results");

    cerr << "done" << endl << endl;


    if (fake_test || true) {
        cerr << "calculating test result...";

        Result_Stats all, nz;

        for (unsigned i = 0;  i < results.size();  ++i) {
            if (results[i].size() > 10)
                throw Exception("invalid result");

            int answer = (answers_tested.empty() ? -1 : answers_tested.at(i));

            bool correct = results[i].count(answer);
            
            bool possible
                = std::binary_search(result_possible_choices[i].begin(),
                                     result_possible_choices[i].end(),
                                     answer);

            bool nz_possible
                = std::binary_search(result_non_zero[i].begin(),
                                     result_non_zero[i].end(),
                                     answer);

            all.add(correct, possible, result_possible_choices[i].size());
            nz.add (correct, nz_possible, result_non_zero[i].size());
        }
        cerr << " done." << endl;

        (fake_test ? out : cerr)
            << "fake test results: \n"
            << all.print()
            << "non-zero scores: \n"
            << nz.print()
            << endl;
    }


    if (dump_results && false) {

        cerr << "dumping results...";

        for (unsigned i = 0;  i < users_tested.size();  ++i) {

            int user_id = users_tested[i];

            const set<int> & user_results = results[i];

            // Write to results file
            out << user_id << ":";
            int j = 0;

            for (set<int>::const_iterator
                     it = user_results.begin(),
                     end = user_results.end();
                 it != end;  ++it, ++j) {
                out << *it;

                if (j != user_results.size() - 1)
                    out << ',';
            }
            out << endl;
        }

        cerr << "done." << endl;
    }
}

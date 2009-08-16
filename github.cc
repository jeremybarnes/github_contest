/* github.cc                                                       -*- C++ -*-
   Jeremy Barnes, 6 August 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   Code to implement a basic github recommendation engine.
*/

#include "data.h"
#include "ranker.h"

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

#include <boost/program_options/cmdline.hpp>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/positional_options.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/variables_map.hpp>
#include <boost/progress.hpp>
#include <boost/tuple/tuple.hpp>


using namespace std;
using namespace ML;

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
            ("dump-results", value<bool>(&dump_results)->zero_tokens(),
             "dump ranked results in official submission format")
            ("dump-predictions", value<bool>(&dump_predictions)->zero_tokens(),
             "dump predictions for debugging")
            ("include-all-correct", value<bool>(&include_all_correct),
             "include all correct (1, default) or only excluded correct (0)?")
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

    if (fake_test || dump_merger_data)
        data.setup_fake_test(num_users, rseed);

    // Write out results file
    filter_ostream out(output_file);

    vector<set<int> > results;
    vector<vector<int> > result_possible_choices;
    results.reserve(data.users_to_test.size());
    result_possible_choices.reserve(data.users_to_test.size());

    if (generator_name != "" && generator_name[0] == '@')
        config.must_find(generator_name, string(generator_name, 1));

    boost::shared_ptr<Candidate_Generator> generator
        = get_candidate_generator(config, generator_name);

    if (ranker_name != "" && ranker_name[0] == '@')
        config.must_find(ranker_name, string(ranker_name, 1));

    boost::shared_ptr<Ranker> ranker
        = get_ranker(config, ranker_name, generator);

    boost::shared_ptr<const ML::Dense_Feature_Space> ranker_fs;

    // Dump the feature vector for the merger file
    if (dump_merger_data) {
        // Get the feature space for the merger file
        ranker_fs = ranker->feature_space();

        // Put it in the header
        out << "LABEL:k=BOOLEAN/o=BIASED WT:k=REAL/o=BIASED GROUP:k=REAL/o=GROUPING REAL_TEST:k=BOOLEAN/o=BIASED " << ranker_fs->print() << endl;
    }

    cerr << "processing " << data.users_to_test.size() << " users..."
         << endl;
    
    boost::progress_display progress(data.users_to_test.size(),
                                     cerr);

    Timer timer;

    for (unsigned i = 0;  i < data.users_to_test.size();  ++i, ++progress) {

        int user_id = data.users_to_test[i];

        const User & user = data.users[user_id];

        vector<Candidate> candidates;
        boost::shared_ptr<Candidate_Data> candidate_data;
        boost::tie(candidates, candidate_data)
            = generator->candidates(data, user_id);

        set<int> possible_choices;
        for (unsigned j = 0;  j < candidates.size();  ++j)
            possible_choices.insert(candidates[j].repo_id);


        if (dump_merger_data) {

            // TODO: try to predict ALL repositories, not just the missing
            // one.  This should give us more general data.  Should be done
            // by leave-one-out kind of training.

            int correct_repo_id = data.answers[i];

            bool possible = possible_choices.count(correct_repo_id);

            out << "# user_id " << user_id << " correct " << correct_repo_id
                << " npossible " << possible_choices.size() << " possible "
                << possible
                << endl;

            if (possible) {

                // Divide into two sets: those that predict a watched repo,
                // and those that don't

                set<int> incorrect;
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


                set<int> correct;

                if (include_all_correct) {
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

                // Generate features
                vector<distribution<float> > features
                    = ranker->features(user_id, candidates, *candidate_data,
                                       data);

                // Go through and dump those selected
                for (unsigned j = 0;  j < candidates.size();  ++j) {
                    const Candidate & candidate = candidates[j];
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
                        = ranker_fs->encode(features[j]);
                    out << ranker_fs->print(*encoded);
                    
                    // A comment so we know where this feature vector came from
                    out << " # repo " << repo_id << " "
                        << data.authors[data.repos[repo_id].author].name << "/"
                        << data.repos[repo_id].name << endl;
                }
            }
            
            out << endl;
        }

        if (dump_merger_data) continue;

        Ranked ranked = ranker->rank(user_id, candidates, *candidate_data,
                                     data);
        ranked.sort();

        if (dump_predictions) {
            // verbosity...

            int correct_repo_id = data.answers[i];

            bool possible = possible_choices.count(correct_repo_id);

            out << "user_id " << user_id << " correct " << correct_repo_id
                << " npossible " << possible_choices.size() << " possible "
                << possible
                << endl;

            for (unsigned j = 0;  j < ranked.size();  ++j) {
                int repo_id = ranked[j].repo_id;

                if (j > 10 && correct_repo_id != repo_id) continue;
                
                out << format("%5d %8.6f %c %d %6d ", j, ranked[j].score,
                              (correct_repo_id == repo_id ? '*' : ' '),
                              (correct_repo_id == repo_id
                               || user.watching.count(repo_id)),
                              repo_id)
                    << data.authors[data.repos[repo_id].author].name << "/"
                    << data.repos[repo_id].name << endl;
            }

            out << endl;
        }

        // Extract the best ones
        set<int> user_results;
        for (unsigned j = 0;  j < ranked.size() && user_results.size() < 10;
             ++j) {
            int repo_id = ranked[j].repo_id;

            if (user.watching.count(repo_id)) continue;  // already watched
            user_results.insert(repo_id);
        }

        results.push_back(user_results);
        result_possible_choices.push_back(vector<int>(possible_choices.begin(),
                                                      possible_choices.end()));
    }

    cerr << "elapsed: " << timer.elapsed() << endl;

    if (dump_merger_data) return(0);

    if (results.size() != data.users_to_test.size())
        throw Exception("wrong number of results");

    cerr << "done" << endl << endl;


    if (fake_test) {
        cerr << "calculating test result...";

        // We now perform the evaluation
        size_t correct = 0;
        size_t in_set = 0;
        size_t not_enough = 0, is_enough = 0;
        size_t total_not_enough = 0, total_is_enough = 0;
        size_t not_enough_correct = 0, is_enough_correct = 0;
        size_t not_enough_in_set = 0, is_enough_in_set = 0;

        for (unsigned i = 0;  i < results.size();  ++i) {
            if (results[i].size() > 10)
                throw Exception("invalid result");
            correct += results[i].count(data.answers[i]);
            
            bool possible
                = std::binary_search(result_possible_choices[i].begin(),
                                     result_possible_choices[i].end(),
                                     data.answers[i]);
            in_set += possible;
            if (result_possible_choices[i].size() < 10) {
                ++not_enough;
                total_not_enough += result_possible_choices[i].size();

                not_enough_correct += results[i].count(data.answers[i]);
                not_enough_in_set += possible;
            }
            else {
                ++is_enough;
                total_is_enough += result_possible_choices[i].size();
                is_enough_correct += results[i].count(data.answers[i]);
                is_enough_in_set += possible;
            }
        }

        cerr << " done." << endl;

        out << format("fake test results: \n"
                      "     total:      real: %4zd/%4zd = %6.2f%%  "
                      "poss: %4zd/%4zd = %6.2f%%\n",
                      correct, results.size(),
                      100.0 * correct / results.size(),
                      in_set, results.size(),
                      100.0 * in_set / results.size())
            << format("     enough:     real: %4zd/%4zd = %6.2f%%  "
                      "poss: %4zd/%4zd = %6.2f%%  avg num: %5.1f\n",
                      is_enough_correct, is_enough,
                      100.0 * is_enough_correct / is_enough,
                      is_enough_in_set, is_enough,
                      100.0 * is_enough_in_set / is_enough,
                      total_is_enough * 1.0 / is_enough)
            << format("     not enough: real: %4zd/%4zd = %6.2f%%  "
                      "poss: %4zd/%4zd = %6.2f%%  avg num: %5.1f\n",
                      not_enough_correct, not_enough,
                      100.0 * not_enough_correct / not_enough,
                      not_enough_in_set, not_enough,
                      100.0 * not_enough_in_set / not_enough,
                      total_not_enough * 1.0 / not_enough)
            << endl;
        
        // don't write results
    }


    if (dump_results) {

        cerr << "dumping results...";

        for (unsigned i = 0;  i < data.users_to_test.size();  ++i) {

            int user_id = data.users_to_test[i];

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

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

#include "boost/program_options/cmdline.hpp"
#include "boost/program_options/options_description.hpp"
#include "boost/program_options/positional_options.hpp"
#include "boost/program_options/parsers.hpp"
#include "boost/program_options/variables_map.hpp"


using namespace std;
using namespace ML;

int main(int argc, char ** argv)
{
    // Do we perform a fake test (where we test different users than the ones
    // from the real test) but it works locally.
    bool fake_test = false;

    // Filename to dump output data to
    string output_file;

    // Dump the data to train a merger classifier?
    bool dump_merger_data = false;

    {
        using namespace boost::program_options;

        options_description options("Options");
        options.add_options()
            ("fake-test,f", value<bool>(&fake_test)->zero_tokens(),
             "run a fake local test instead of generating real results")
            ("dump-merger-data", value<bool>(&dump_merger_data)->zero_tokens(),
             "dump data to train a merger classifier")
            ("output-file,o",
             value<string>(&output_file)->default_value("results.txt"),
             "dump an output file to the given filename");

        //positional_options_description p;
        //p.add("dataset", -1);

        options_description all_opt;
        all_opt
            .add(options);
        all_opt.add_options()
            ("help,h", "print this message");
        
        variables_map vm;
        store(command_line_parser(argc, argv)
              .options(all_opt)
              //.positional(p)
              .run(),
              vm);
        notify(vm);

        if (vm.count("help")) {
            cout << all_opt << endl;
            return 1;
        }
    }

    // Load up the data
    Data data;
    data.load();

    if (fake_test || dump_merger_data)
        data.setup_fake_test();

    vector<set<int> > results;
    vector<set<int> > result_possible_choices;
    results.reserve(data.users_to_test.size());
    result_possible_choices.reserve(data.users_to_test.size());

    Candidate_Generator generator;
    Ranker ranker;

    for (unsigned i = 0;  i < data.users_to_test.size();  ++i) {

        int user_id = data.users_to_test[i];

        vector<Candidate> candidates = generator.candidates(data, user_id);

        set<int> possible_choices;
        for (unsigned j = 0;  j < candidates.size();  ++j)
            possible_choices.insert(candidates[j].repo_id);

        Ranked ranked = ranker.rank(data, user_id, candidates);

        // Convert to other format
        sort_on_second_descending(ranked);

        int nres = std::min<int>(10, ranked.size());

        set<int> user_results(first_extractor(ranked.begin()),
                              first_extractor(ranked.begin() + nres));

        // Now generate the results

        results.push_back(user_results);
        result_possible_choices.push_back(possible_choices);
    }

    if (results.size() != data.users_to_test.size())
        throw Exception("wrong number of results");


    if (fake_test) {
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
            in_set += result_possible_choices[i].count(data.answers[i]);
            if (result_possible_choices[i].size() < 10) {
                ++not_enough;
                total_not_enough += result_possible_choices[i].size();

                not_enough_correct += results[i].count(data.answers[i]);
                not_enough_in_set += result_possible_choices[i].count(data.answers[i]);
            }
            else {
                ++is_enough;
                total_is_enough += result_possible_choices[i].size();
                is_enough_correct += results[i].count(data.answers[i]);
                is_enough_in_set += result_possible_choices[i].count(data.answers[i]);
            }
        }

        cerr << format("fake test results: \n"
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


    if (output_file != "") {
        // Write out results file
        filter_ostream out(output_file);

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
    }
}

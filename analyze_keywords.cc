/* analyze_keywords.cc
   Jeremy Barnes, 19 August 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   Main program to work with keywords in repo names.
*/

#include "data.h"
#include "utils/vector_utils.h"
#include "utils/configuration.h"
#include "keywords.h"

#include <boost/program_options/cmdline.hpp>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/positional_options.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/variables_map.hpp>
#include <boost/progress.hpp>
#include <boost/tuple/tuple.hpp>

#include "utils/hash_map.h"

#include "svdlibc/svdlib.h"

using namespace std;
using namespace ML;

int main(int argc, char ** argv)
{
    // Configuration file to use
    string config_file = "config.txt";

    // Extra configuration options
    vector<string> extra_config_options;

    {
        using namespace boost::program_options;

        options_description config_options("Configuration");

        config_options.add_options()
            ("config-file,c", value<string>(&config_file),
             "configuration file to read configuration options from")
            ("extra-config-option",
             value<vector<string> >(&extra_config_options),
             "extra configuration option=value (can go directly on "
             "command line)");
        
        options_description control_options("Control Options");
        
        control_options.add_options();

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

    analyze_keywords(data);
}

/* keywords.cc
   Jeremy Barnes, 19 August 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   Work with keywords in repo names.
*/

#include "keywords.h"
#include "utils/vector_utils.h"
#include "utils/hash_map.h"
#include <boost/tuple/tuple.hpp>

#include "svdlibc/svdlib.h"

using namespace std;
using namespace ML;

std::vector<std::string> uncamelcase(const std::string & str)
{
    int num_lower = 0, num_upper = 0;
    for (unsigned i = 0;  i < str.size();  ++i) {
        if (islower(str[i])) ++num_lower;
        if (isupper(str[i])) ++num_upper;
    }

    if (num_upper == 0) return vector<string>(1, str);

    // TODO: do...

    string result;
    result.reserve(str.size());
    for (unsigned i = 0;  i < str.size();  ++i)
        result.push_back(tolower(str[i]));

    return vector<string>(1, result);
}

std::vector<std::string> tokenize(const std::string & str,
                                  Name_Type type)
{
    std::string token;
    std::vector<std::string> tokens;
    bool after_space = true;

    for (unsigned i = 0;  i <= str.size();  ++i) {
        if (i == str.size() || str[i] == '_' || str[i] == ':'
            || str[i] == '-' || str[i] == '.' || str[i] == ' ') {
            if (token != "") {
                vector<string> tokens2 = uncamelcase(token);

                tokens.insert(tokens.end(), tokens2.begin(), tokens2.end());
            }
            
            token = "";
            after_space = true;
            continue;
        }

        token.push_back(str[i]);
        after_space = false;
    }
    
    return tokens;
}

void analyze_keywords(Data & data)
{
    // Steps:
    // * Tokenize.  We also turn CamelCase into camel case and normalize
    //   punctuation, etc.
    // * Convert runtogethertext into run together text (hard)
    // * Filter out stopwords
    // * Substitution of known synonyms
    // * replacement of known compound terms with compound_terms
    // * Get the term frequency matrix
    // * Run a SVD to get the major variation in co-usage
    // * Write the data file

    // The goal is to get as far towards a uniform representation as possible,
    // without spending too much time on trying to get it perfect.

    vector<string> all_names;
    for (unsigned i = 0;  i < data.repos.size();  ++i) {
        if (!data.repos[i].invalid())
            all_names.push_back(data.repos[i].name);
    }

    make_vector_set(all_names);

    // Tokenization

    vector<Name> names;

    std::hash_map<string, int> vocab_map;
    vector<Vocab_Entry> vocab;


    for (unsigned i = 0;  i < all_names.size();  ++i) {
        string name = all_names[i];
        
        vector<string> tokens = tokenize(name, Repo_Name);

        //vector<string> desc_tokens = tokenize(name, Description);

        //cerr << "name " << name << " tokens " << tokens << endl;

        // Add each token to the vocabulary

        set<int> ids_done;

        for (unsigned j = 0;  j < tokens.size();  ++j) {
            string token = tokens[j];

            // Insert or find vocabulary entry
            hash_map<string, int>::iterator it;
            bool inserted;
            boost::tie(it, inserted)
                = vocab_map.insert(make_pair(token, vocab.size()));

            int id;
            if (inserted) {
                Vocab_Entry new_entry;
                new_entry.id = vocab.size();
                new_entry.token = token;

                id = vocab.size();
                vocab.push_back(new_entry);
            }
            else id = it->second;

            Vocab_Entry & entry = vocab[id];
            
            entry.seen_count += 1;

            if (!ids_done.count(id)) {
                ids_done.insert(id);
                entry.in_names += 1;
            }
        }
    }

    cerr << vocab.size() << " vocab entries" << endl;

    int num_gt_two = 0;

    for (unsigned i = 0;  i < vocab.size();  ++i) {
        if (vocab[i].in_names >= 5) {
            cerr << vocab[i].token << " " << vocab[i].in_names << endl;
            ++num_gt_two;
        }
    }

    cerr << "num_gt_two = " << num_gt_two << endl;


#if 0
    // First, count number of non-zero entries
    vector<int> repo_to_index(data.repos.size(), -1);
    vector<int> index_to_repo;
    int num_non_zero = 0;
    int num_valid_repos = 0;
    for (unsigned i = 0;  i < data.repos.size();  ++i) {
        if (data.repos[i].watchers.empty()) continue;
        repo_to_index[i] = num_valid_repos++;
        index_to_repo.push_back(i);
        num_non_zero += data.repos[i].watchers.size();
    }

    vector<int> user_to_index(data.users.size());
    vector<int> index_to_user;
    int num_valid_users = 0;
    for (unsigned i = 0;  i < data.users.size();  ++i) {
        if (data.users[i].watching.empty()) continue;
        user_to_index[i] = num_valid_users++;
        index_to_user.push_back(i);
    }

    smat matrix;
    matrix.rows = num_valid_repos;
    matrix.cols = num_valid_users;
    matrix.vals = num_non_zero;
    matrix.pointr = new long[matrix.cols + 1];
    matrix.rowind = new long[num_non_zero];
    matrix.value  = new double[num_non_zero];

    int entry_num = 0;
    for (unsigned i = 0;  i < data.users.size();  ++i) {
        const User & user = data.users[i];
        int index = user_to_index[i];
        matrix.pointr[index] = entry_num;
        for (IdSet::const_iterator
                 it = user.watching.begin(),
                 end = user.watching.end();
             it != end;  ++it) {
            matrix.rowind[entry_num] = repo_to_index[*it];
            matrix.value[entry_num] = 1.0;
            ++entry_num;
        }
    }
    matrix.pointr[num_valid_users] = entry_num;

    // Free up memory (TODO: put into guards...)
    delete[] matrix.pointr;
    delete[] matrix.rowind;
    delete[] matrix.value;
    svdFreeSVDRec(result);
#endif
}

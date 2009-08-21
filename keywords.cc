/* keywords.cc
   Jeremy Barnes, 19 August 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   Work with keywords in repo names.
*/

#include "data.h"
#include "utils/vector_utils.h"

using namespace std;
using namespace ML;

struct Vocab_Entry {
    std::string token;
    int id;
};

struct Name : std::vector<int> {
};

std::vector<std::string> uncamelcase(const std::string & str)
{
    int num_lower = 0, num_upper = 0;
    for (unsigned i = 0;  i < str.size();  ++i) {
        if (islower(str[i])) ++num_lower;
        if (isupper(str[i])) ++num_upper;
    }

    if (num_upper == 0) return str;

    // TODO: do...

    string result;
    result.reserve(str.size());
    for (unsigned i = 0;  i < str.size();  ++i)
        result.push_back(str[i]);

    return result;
}

std::vector<std::string> tokenize(const std::string & str)
{
    std::string token;
    std::vector<std::string> tokens;
    bool after_space = true;

    for (unsigned i = 0;  i < str.size();  ++i) {
        char c = str[i];
        if (c == '_' || c == ':' || c == '-' || c == '.')
            c = ' ';
        if (c != ' ') token.push_back(c);
        else if (!after_space) {
            tokens.push_back(token);
            token = "";
        }

        after_space = (c != ' ');
    }
    
    if (token != "") {
        vector<string> tokens = uncamelcase(token);
        result.insert(result.back(), tokens.begin(), tokens.end());
    }
    
    return result;
}

void analyze_keywords(const Data & data)
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
        if (data.repos[i].valid())
            all_names.push_back(data.repos[i].name);
    }

    make_vector_set(all_names.begin(), all_names.end());

    // Tokenization

    vector<Name> names;

    std::hash_map<string, int> vocab_map;
    vector<Vocab_Entry> vocab;


    for (unsigned i = 0;  i < all_names.size();  ++i) {
        string name = all_names[i];
        
        vector<string> tokens = tokenize(name);

        // Add each token to the vocabulary
        for (unsigned i = 0;  i < tokens.size();  ++i) {
            string token = tokens[i];

            // Insert or find vocabulary entry
            hash_map<string, int>::iterator it;
            bool found;
            boost::tie(it, found)
                = vocab_map.insert(make_pair(token, vocab.size()));

            int id;
            if (!found) {
                Vocab_Entry new_entry;
                new_entry.id = vocab.size();
                new_entry.token = token;

                id = vocab.size();
                vocab.push_back(new_entry);
            }
            else id = it->second;

            Vocab_Entry & entry = vocab[id];
            
        }
    }
}

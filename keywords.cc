/* keywords.cc
   Jeremy Barnes, 19 August 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   Work with keywords in repo names.
*/

#include "keywords.h"
#include "utils/vector_utils.h"
#include "utils/hash_map.h"
#include "utils/hash_set.h"
#include <boost/tuple/tuple.hpp>
#include "utils/parse_context.h"
#include "utils/string_functions.h"

#include "svdlibc/svdlib.h"
#include "arch/timers.h"

using namespace std;
using namespace ML;

const std::hash_set<std::string> & get_stopwords()
{
    static std::hash_set<std::string> results;

    if (results.empty()) {
        Parse_Context context("stop_words.txt");

        while (context) {
            string word = context.expect_text('\n');
            results.insert(word);
            context.expect_eol();
        }
    }

    return results;
}

std::vector<std::string> uncamelcase(const std::string & str)
{
    int num_lower = 0, num_upper = 0;
    for (unsigned i = 0;  i < str.size();  ++i) {
        if (islower(str[i])) ++num_lower;
        if (isupper(str[i])) ++num_upper;
    }

    if (num_upper == 0) return vector<string>(1, str);

    vector<string> result;
    result.reserve(1);

    if (num_upper >= 2 && num_lower >= 2) {
        // We split on lower-to-upper transitions
        bool last_lower = false;
        int start = 0;
        for (int i = 0;  i <= str.size();  ++i) {
            bool upper = (i == str.size() ? true : isupper(str[i]));
            bool lower = (i == str.size() ? true : islower(str[i]));

            if ((last_lower && upper) || i == str.size()) {
                result.push_back(string(str, start, i - start));
                for (unsigned j = 0;  j < result.back().size();  ++j)
                    result.back()[j] = tolower(result.back()[j]);
                start = i;
            }

            last_lower = lower;
        }

        if (result.size() > 1 && false)
            cerr << "un camel case: transformed " << str << " into "
                 << result << endl;
    }
    else {
        string result_str;
        result_str.reserve(str.size());
        for (unsigned i = 0;  i < str.size();  ++i)
            result_str.push_back(tolower(str[i]));
        result.push_back(result_str);
    }

    return result;
}

// Remove leading, trailing punctuation
std::string unpunct(const std::string & str)
{
    int start_pos = 0, end_pos = str.size();
    while (start_pos < str.size() && ispunct(str[start_pos])) ++start_pos;
    while (end_pos > 0 && ispunct(str[end_pos - 1])) --end_pos;
    if (start_pos == 0 && end_pos == str.size()) return str;
    return string(str, start_pos, end_pos - start_pos);
}

std::vector<std::string>
tokenize(const std::string & str,
         Name_Type type,
         const std::hash_map<string, int> * vocab_map,
         const vector<Vocab_Entry> * vocab)
{
    std::string token;
    std::vector<std::string> tokens;
    bool after_space = true;

    for (unsigned i = 0;  i <= str.size();  ++i) {
        if (i == str.size() || str[i] == '_' || str[i] == ':'
            || str[i] == '-' || str[i] == '.' || str[i] == ' '
            || str[i] == '/') {
            if (token != "") {
                bool keeptogether = true;
                vector<string> tokens2;

                string lctoken = lowercase(token);

                if (vocab_map) {
                    // Find the frequency
                    hash_map<string, int>::const_iterator it
                        = vocab_map->find(lctoken);

                    if (it != vocab_map->end()) {
                        int count = (*vocab)[it->second].in_names;
                        keeptogether = (count >= 50);
                        if (keeptogether && false)
                            cerr << "keeping together " << token
                                 << " as it was seen " << count
                                 << " times" << endl;
                    }
                }
                
                if (keeptogether)
                    tokens2.push_back(lctoken);
                else tokens2 = uncamelcase(token);

                for (unsigned j = 0;  j < tokens2.size();  ++j) {
                    string up = unpunct(tokens2[j]);
                    if (up.empty()) continue;
                    tokens.push_back(up);
                }
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

    // Tokenization

    vector<Name> names;

    std::hash_map<string, int> vocab_map;
    vector<Vocab_Entry> vocab;

    int num_valid_repos = 0;

    const std::hash_set<std::string> & stopwords = get_stopwords();

    for (unsigned i = 0;  i < data.repos.size();  ++i) {
        if (data.repos[i].invalid()) continue;
        ++num_valid_repos;
        Repo & repo = data.repos[i];
        vector<string> tokens = tokenize(repo.name, Repo_Name);

        vector<string> desc_tokens = tokenize(repo.description, Description);

        //cerr << "name " << name << " tokens " << tokens << endl;

        // Add each token to the vocabulary

        tokens.insert(tokens.end(),
                      desc_tokens.begin(), desc_tokens.end());

        //cerr << "name: " << repo.name << " desc: " << repo.description
        //     << endl;
        //cerr << "  processed: " << tokens << endl;
        
        set<int> ids_done;

        for (unsigned j = 0;  j < tokens.size();  ++j) {
            string token = tokens[j];

            // filter stopwords
            if (stopwords.count(token)) continue;

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

    cerr << "pass 1: " << vocab.size() << " vocab entries" << endl;

    std::hash_map<string, int> vocab_map2;
    vector<Vocab_Entry> vocab2;

    // Pass 2: we can use frequency counts to improve our tokenization
    for (unsigned i = 0;  i < data.repos.size();  ++i) {
        if (data.repos[i].invalid()) continue;
        ++num_valid_repos;
        Repo & repo = data.repos[i];
        vector<string> tokens = tokenize(repo.name, Repo_Name,
                                         &vocab_map, &vocab);

        vector<string> desc_tokens
            = tokenize(repo.description, Description,
                       &vocab_map, &vocab);

        tokens.insert(tokens.end(),
                      desc_tokens.begin(), desc_tokens.end());
        
        set<int> ids_done;

        for (unsigned j = 0;  j < tokens.size();  ++j) {
            string token = tokens[j];

            if (stopwords.count(token)) continue;

            // Insert or find vocabulary entry
            hash_map<string, int>::iterator it;
            bool inserted;
            boost::tie(it, inserted)
                = vocab_map2.insert(make_pair(token, vocab2.size()));

            int id;
            if (inserted) {
                Vocab_Entry new_entry;
                new_entry.id = vocab2.size();
                new_entry.token = token;

                id = vocab2.size();
                vocab2.push_back(new_entry);
            }
            else id = it->second;

            Vocab_Entry & entry = vocab2[id];
            
            entry.seen_count += 1;

            if (!ids_done.count(id)) {
                ids_done.insert(id);
                entry.in_names += 1;
            }

            repo.keywords.add(id, 1.0 / tokens.size());
        }

        repo.keywords.finish();
        repo.keywords_2norm = sqrt(repo.keywords.overlap(repo.keywords).first);
    }

    cerr << vocab2.size() << " vocab entries" << endl;

    static const int min_keyword_freq = 5;

    int num_gt_two = 0;

    for (unsigned i = 0;  i < vocab2.size();  ++i) {
        if (vocab2[i].in_names >= min_keyword_freq) {
            //cerr << vocab[i].token << " " << vocab[i].in_names << endl;
            ++num_gt_two;
        }
    }

    cerr << "num over threshold of " << min_keyword_freq
         << " = " << num_gt_two << endl;
    
    size_t num_entries = 0;
    size_t empty_repos = 0;
    size_t non_empty_repos = 0;

    vector<int> repo_to_index(data.repos.size(), -1);
    vector<int> index_to_repo;

    vector<int> word_to_index(vocab2.size(), -1);
    vector<int> index_to_word;

    // Scale by IDF and add new vector, then prepare data for the SVD
    for (unsigned i = 0;  i < data.repos.size();  ++i) {
        if (data.repos[i].invalid()) continue;
        Repo & repo = data.repos[i];

        // Filter out keywords that didn't appear enough times
        Cooccurrences filtered;

        double total_score = 0.0;
        for (Cooccurrences::const_iterator
                 it = repo.keywords.begin(),
                 end = repo.keywords.end();
             it != end;  ++it) {
            float freq = vocab2[it->with].in_names;
            if (freq < min_keyword_freq) continue;

            filtered.add(it->with, it->score);
            total_score += it->score;
            if (word_to_index[it->with] == -1) {
                word_to_index[it->with] = index_to_word.size();
                index_to_word.push_back(it->with);
            }
        }

        // Normalize
        double factor = 1.0 / total_score;
        for (Cooccurrences::iterator
                 it = repo.keywords.begin(),
                 end = repo.keywords.end();
             it != end;  ++it)
            it->score *= factor;
        
        filtered.finish();
        repo.keywords.swap(filtered);

        if (repo.keywords.empty()) {
            empty_repos += 1;
            continue;
        }

        num_entries += repo.keywords.size();

        repo_to_index[i] = index_to_repo.size();
        index_to_repo.push_back(i);

        non_empty_repos += 1;

        // Calculate tf-idf
        repo.keywords_idf.reserve(repo.keywords.size());
        for (Cooccurrences::const_iterator
                 it = repo.keywords.begin(),
                 end = repo.keywords.end();
             it != end;  ++it) {
            float freq = vocab2[it->with].in_names;
            float idf = log(1.0 * num_valid_repos / freq);
            repo.keywords_idf.add(it->with, it->score * idf);
        }
        repo.keywords_idf.finish();
        repo.keywords_idf_2norm
            = sqrt(repo.keywords_idf.overlap(repo.keywords_idf).first);
    }

    cerr << "num_entries = " << num_entries << endl;
    cerr << "empty_repos = " << empty_repos << endl;
    cerr << "non-empty repos = " << non_empty_repos << endl;

    // Rows: words
    // Columns: repos

    // Create the matrix
    smat matrix;
    matrix.rows = index_to_word.size();
    matrix.cols = index_to_repo.size();
    matrix.vals = num_entries;
    matrix.pointr = new long[matrix.cols + 1];
    matrix.rowind = new long[matrix.vals];
    matrix.value  = new double[matrix.vals];

    // Fill it in
    int entry_num = 0;
    int last_index = -1;
    for (unsigned i = 0;  i < data.repos.size();  ++i) {
        if (data.repos[i].invalid()) continue;
        Repo & repo = data.repos[i];
        if (repo_to_index[i] == -1) continue;

        int index = repo_to_index[i];
        matrix.pointr[index] = entry_num;

        if (index < 0 || index >= index_to_repo.size()) {
            cerr << "i = " << i << " index = " << index << endl;
            throw Exception("bad index");
        }
        if (index != last_index + 1) {
            throw Exception("index didn't increment");
        }

        last_index = index;

        for (Cooccurrences::const_iterator
                 it = repo.keywords.begin(),
                 end = repo.keywords.end();
             it != end;  ++it) {

            matrix.rowind[entry_num] = word_to_index[it->with];

            if (matrix.rowind[entry_num] == -1)
                throw Exception("invalid entry num");
            
            matrix.value[entry_num] = 1.0;//it->score;
            ++entry_num;
        }
    }
    matrix.pointr[index_to_repo.size()] = entry_num;

    if (entry_num != num_entries)
        throw Exception("wrong num_entries");

    // Now for the SVD
    cerr << "running keyword SVD" << endl;

    Timer timer;

    int nvalues = 100;

    // Run the SVD
    svdrec * result = svdLAS2A(&matrix, nvalues);

    cerr << "SVD elapsed: " << timer.elapsed() << endl;

    if (!result)
        throw Exception("error performing SVD");

    //cerr << "num_valid_repos = " << num_valid_repos << endl;

    distribution<float> values(result->S, result->S + nvalues);

    data.keyword_singular_values = values;

#if 0

    cerr << "highest values: " << values << endl;

    cerr << "result->Ut->rows = " << result->Ut->rows << endl;
    cerr << "result->Ut->cols = " << result->Ut->cols << endl;
    cerr << "result->Vt->rows = " << result->Vt->rows << endl;
    cerr << "result->Vt->cols = " << result->Vt->cols << endl;

    // Analyze the highest repos for the principal factor
    for (unsigned i = 0;  i < 20;  ++i) {
        cerr << "factor " << i << " value " << result->S[i] << endl;

        // Get the repo vector
        vector<pair<int, double> > sorted;
        for (unsigned j = 0;  j < matrix.cols;  ++j)
            sorted.push_back(make_pair(index_to_repo[j], result->Vt->value[i][j]));

        sort_on_second_descending(sorted);

        for (unsigned j = 0;  j < 20;  ++j) {
            int repo_id = sorted[j].first;
            const Repo & repo = data.repos[repo_id];
            cerr << format("     %3d %6d %6zd %8.6f %6d %s/%s\n",
                           j,
                           repo.popularity_rank,
                           repo.watchers.size(),
                           sorted[j].second,
                           repo_id,
                           data.authors[repo.author].name.c_str(),
                           repo.name.c_str());
            cerr << "          " << string(repo.description, 0, 70)
                 << endl;
        }
        cerr << endl;

        sorted.clear();

        for (unsigned j = 0;  j < matrix.rows;  ++j)
            sorted.push_back(make_pair(index_to_word[j], result->Ut->value[i][j]));

        sort_on_second_descending(sorted);

        cerr << "words: ";
        for (unsigned j = 0;  j < 20;  ++j)
            cerr << " " << vocab2[sorted[j].first].token
                 << "/" << sorted[j].second;
        cerr << endl;
    }
#endif

    for (unsigned i = 0;  i < data.repos.size();  ++i) {
        Repo & repo = data.repos[i];
        distribution<float> & repo_vec = repo.keyword_vec;
        repo_vec.resize(nvalues);

        int index = repo_to_index.at(i);
        
        if (index == -1) continue;
        if (index < 0 || index >= num_valid_repos)
            throw Exception("invalid number in index");
        
        for (unsigned j = 0;  j < nvalues;  ++j)
            repo_vec.at(j) = result->Vt->value[j][index];

        repo.keyword_vec_2norm = repo_vec.two_norm();
    }


    // Free up memory (TODO: put into guards...)
    delete[] matrix.pointr;
    delete[] matrix.rowind;
    delete[] matrix.value;
    svdFreeSVDRec(result);
}

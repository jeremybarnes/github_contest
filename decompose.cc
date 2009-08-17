/* decompose.cc
   Jeremy Barnes, 16 August 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

*/

#include "decompose.h"
#include "arch/timers.h"
#include "utils/vector_utils.h"

using namespace std;
using namespace ML;


void
Decomposition::
decompose(Data & data)
{
    // Convert data to sparse matrix form

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

    //cerr << "num_valid_repos = " << num_valid_repos << endl;

    cerr << "running SVD" << endl;

    Timer timer;

    int nvalues = 50;

    // Run the SVD
    svdrec * result = svdLAS2A(&matrix, nvalues);

    cerr << "SVD elapsed: " << timer.elapsed() << endl;

    if (!result)
        throw Exception("error performing SVD");

    //cerr << "num_valid_repos = " << num_valid_repos << endl;

    distribution<float> values(result->S, result->S + nvalues);
    data.singular_values = values;

#if 0
    cerr << "highest values: " << values << endl;

    cerr << "result->Ut->rows = " << result->Ut->rows << endl;
    cerr << "result->Ut->cols = " << result->Ut->cols << endl;
    cerr << "result->Vt->rows = " << result->Vt->rows << endl;
    cerr << "result->Vt->cols = " << result->Vt->cols << endl;

    // Analyze the highest repos for the principal factor
    for (unsigned i = 0;  i < 5;  ++i) {
        cerr << "factor " << i << " value " << result->S[i] << endl;

        // Get the repo vector
        vector<pair<int, double> > sorted;
        for (unsigned j = 0;  j < num_valid_repos;  ++j)
            sorted.push_back(make_pair(index_to_repo[j], result->Ut->value[i][j]));

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
        }
        cerr << endl;
    }
#endif

    for (unsigned i = 0;  i < data.repos.size();  ++i) {
        Repo & repo = data.repos[i];
        distribution<float> & repo_vec = repo.singular_vec;
        repo_vec.resize(nvalues);

        if (repo.watchers.empty()) continue;
        
        int index = repo_to_index.at(i);
        //cerr << "i = " << i << " index = " << index << endl;
        if (index == -1)
            throw Exception("index out of range");

        if (index >= num_valid_repos)
            throw Exception("invalid number in index");

        for (unsigned j = 0;  j < nvalues;  ++j)
            repo_vec.at(j) = result->Ut->value[j][index];

        repo.singular_2norm = sqrt((repo_vec * repo_vec).total());
    }

    cerr << "done repos" << endl;

    for (unsigned i = 0;  i < data.users.size();  ++i) {
        User & user = data.users[i];
        distribution<float> & user_vec = user.singular_vec;
        user_vec.resize(nvalues);

        if (user.watching.empty()) continue;
        
        int index = user_to_index[i];
        if (index == -1)
            throw Exception("index out of range");

        for (unsigned j = 0;  j < nvalues;  ++j)
            user_vec[j] = result->Vt->value[j][index];

        user.singular_2norm = sqrt((user_vec * user_vec).total());
    }

    // Free up memory (TODO: put into guards...)
    delete[] matrix.pointr;
    delete[] matrix.rowind;
    delete[] matrix.value;
    svdFreeSVDRec(result);
}

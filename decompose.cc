/* decompose.cc
   Jeremy Barnes, 16 August 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

*/

#include "decompose.h"
#include "arch/timers.h"


using namespace std;
using namespace ML;


void
Decomposition::
decompose(const Data & data)
{
    // Convert data to sparse matrix form

    // First, count number of non-zero entries
    vector<int> repo_to_index(data.repos.size(), -1);
    int num_non_zero = 0;
    int num_valid_repos = 0;
    for (unsigned i = 0;  i < data.repos.size();  ++i) {
        if (data.repos[i].watchers.empty()) continue;
        repo_to_index[i] = num_valid_repos++;
        num_non_zero += data.repos[i].watchers.size();
    }

    vector<int> user_to_index(data.users.size());
    int num_valid_users = 0;
    for (unsigned i = 0;  i < data.users.size();  ++i) {
        if (data.users[i].watching.empty()) continue;
        user_to_index[i] = num_valid_users++;
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

    cerr << "running SVD" << endl;

    Timer timer;

    int nvalues = 100;

    // Run the SVD
    svdrec * result = svdLAS2A(&matrix, nvalues);

    cerr << "SVD elapsed: " << timer.elapsed() << endl;

    if (!result)
        throw Exception("error performing SVD");

    distribution<double> values(result->S, result->S + nvalues);
    cerr << "highest values: " << values << endl;
}

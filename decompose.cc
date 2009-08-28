/* decompose.cc
   Jeremy Barnes, 16 August 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

*/

#include "decompose.h"
#include "svdlibc/svdlib.h"
#include "arch/timers.h"
#include "utils/vector_utils.h"
#include "arch/simd_vector.h"
#include "stats/distribution_simd.h"
#include "utils/parse_context.h"
#include "utils/pair_utils.h"


using namespace std;
using namespace ML;

enum {
    NUM_CLUSTERS_USER = 200,
    NUM_CLUSTERS_REPO = 200
};

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

        repo.singular_2norm = repo_vec.two_norm();
    }

    cerr << "done repos" << endl;

    for (unsigned i = 0;  i < data.users.size();  ++i) {
        User & user = data.users[i];
        distribution<float> & user_vec = user.singular_vec;
        user_vec.resize(nvalues);
        user.repo_centroid.resize(nvalues);

        if (user.watching.empty()) continue;
        
        int index = user_to_index[i];
        if (index == -1)
            throw Exception("index out of range");

        for (unsigned j = 0;  j < nvalues;  ++j)
            user_vec[j] = result->Vt->value[j][index];

        user.singular_2norm = user_vec.two_norm();

        distribution<double> centroid(nvalues);

        for (IdSet::const_iterator
                 it = user.watching.begin(),
                 end = user.watching.end();
             it != end;  ++it) {
            centroid += data.repos[*it].singular_vec;
        }
        centroid /= centroid.two_norm();

        user.repo_centroid = centroid;
    }

    // Free up memory (TODO: put into guards...)
    delete[] matrix.pointr;
    delete[] matrix.rowind;
    delete[] matrix.value;
    svdFreeSVDRec(result);
}

struct RepoDataAccess {
    RepoDataAccess(const Data & data)
        : data(data)
    {
    }
    
    const Data & data;

    int nobjects() const
    {
        return data.repos.size();
    }

    bool invalid(int object) const
    {
        return data.repos[object].watchers.empty();
    }

    typedef Repo Object;
    const Repo & object(int object)
    {
        return data.repos[object];
    }

    int nd() const
    {
        return data.singular_values.size();
    }

    string what() const { return "repo"; }
};

template<class DataAccess>
void calc_kmeans(vector<Cluster> & clusters,
                 vector<int> & in_cluster,
                 int nclusters,
                 DataAccess & access)
{
    int nd = access.nd();

    typedef typename DataAccess::Object Object;

    clusters.resize(nclusters);
    in_cluster.resize(access.nobjects(), -1);

    // Random initialization
    for (unsigned i = 0;  i < access.nobjects();  ++i) {
        if (access.invalid(i)) continue;
        int cluster = rand() % nclusters;
        clusters[cluster].members.push_back(i);
        in_cluster[i] = cluster;
    }

    int changes = -1;

    for (int iter = 0;  iter < 100 && changes != 0;  ++iter) {
        // Calculate means
        for (unsigned i = 0;  i < clusters.size();  ++i) {
            Cluster & cluster = clusters[i];
            cluster.centroid.resize(nd);
            std::fill(cluster.centroid.begin(), cluster.centroid.end(), 0.0);

            double k = 1.0 / cluster.members.size();
            
            for (unsigned j = 0;  j < cluster.members.size();  ++j) {
                const Object & object = access.object(cluster.members[j]);
                SIMD::vec_add(&cluster.centroid[0], k, //k / object.singular_2norm,
                              &object.singular_vec[0], &cluster.centroid[0], nd);
            }

            // Normalize
            cluster.centroid /= cluster.centroid.two_norm();

            //cerr << "cluster " << i << " had " << cluster.members.size()
            //     << " members" << endl;

            cluster.members.clear();
        }

        // How many have changed cluster?  Used to know when the cluster
        // contents are stable
        changes = 0;

        for (unsigned i = 0;  i < access.nobjects();  ++i) {
            const Object & object = access.object(i);

            // Take the dot product with each cluster
            int best_cluster = -1;
            float best_score = -INFINITY;

            for (unsigned j = 0;  j < nclusters;  ++j) {
                float score
                    = clusters[j].centroid.dotprod(object.singular_vec);

                if (score > best_score) {
                    best_score = score;
                    best_cluster = j;
                }
            }
            
            if (best_cluster != in_cluster[i]) ++changes;

            in_cluster[i] = best_cluster;
            clusters[best_cluster].members.push_back(i);
        }

        cerr << "clustering iter " << iter << " for " << access.what()
             << ": " << changes << " changes" << endl;
    }
}

// Perform a k-means clustering of repos and users
void
Decomposition::
kmeans_repos(Data & data)
{
    int nclusters = NUM_CLUSTERS_REPO;
    
    vector<Cluster> repo_clusters;
    vector<int> repo_in_cluster;

    RepoDataAccess repo_access(data);
    calc_kmeans(repo_clusters, repo_in_cluster, nclusters, repo_access);

#if 0
    int all_to_check[] = { 17, 356, 62 };

    for (unsigned x = 0;  x < sizeof(all_to_check) / sizeof(all_to_check[0]);  ++x) {
        // Check what is similar to rails
        int to_check = all_to_check[x];
        const Repo & repo1 = data.repos[to_check];
        
        vector<pair<int, float> > similarities;
        
        for (unsigned i = 0;  i < data.repos.size();  ++i) {
            const Repo & repo2 = data.repos[i];
            if (repo2.invalid()) continue;
            
            float sim
                = repo1.singular_vec.dotprod(repo2.singular_vec)
                / (repo1.singular_2norm * repo2.singular_2norm);
            
            similarities.push_back(make_pair(i, sim));
        }
        
        sort_on_second_descending(similarities);
        
        cerr << "most similar to " << 
            data.authors[repo1.author].name << "/" << repo1.name << endl;
        
        for (unsigned j = 0;  j < 20;  ++j) {
            int repo_id = similarities[j].first;
            const Repo & repo = data.repos[repo_id];
            cerr << format("     %3d %6d %6zd %8.6f %6d %s/%s\n",
                           j,
                           repo.popularity_rank,
                           repo.watchers.size(),
                           similarities[j].second,
                           repo_id,
                           data.authors[repo.author].name.c_str(),
                           repo.name.c_str());
        }
        cerr << endl;
    }
#endif

    for (unsigned i = 0;  i < repo_in_cluster.size();  ++i)
        data.repos[i].kmeans_cluster = repo_in_cluster[i];
}

struct UserDataAccess {
    UserDataAccess(const Data & data)
        : data(data)
    {
    }
    
    const Data & data;

    int nobjects() const
    {
        return data.users.size();
    }

    bool invalid(int object) const
    {
        return data.users[object].watching.empty();
    }

    typedef User Object;
    const User & object(int object)
    {
        return data.users[object];
    }

    int nd() const
    {
        return data.singular_values.size();
    }

    string what() const { return "user"; }
};

void
Decomposition::
kmeans_users(Data & data)
{
    int nclusters = NUM_CLUSTERS_USER;
    
    vector<Cluster> user_clusters;
    vector<int> user_in_cluster;

    UserDataAccess user_access(data);
    calc_kmeans(user_clusters, user_in_cluster, nclusters, user_access);

    for (unsigned i = 0;  i < user_in_cluster.size();  ++i)
        data.users[i].kmeans_cluster = user_in_cluster[i];
}

void
Decomposition::
save_kmeans_users(std::ostream & stream, const Data & data)
{
    for (unsigned i = 0;  i < data.users.size();  ++i) {
        const User & user = data.users[i];
        stream << i << ":" << user.kmeans_cluster << "\n";
    }
}

void
Decomposition::
save_kmeans_repos(std::ostream & stream, const Data & data)
{
    for (unsigned i = 0;  i < data.repos.size();  ++i) {
        const Repo & repo = data.repos[i];
        stream << i << ":" << repo.kmeans_cluster << "\n";
    }
}

void
Decomposition::
load_kmeans_users(const std::string & filename, Data & data)
{
    Parse_Context context(filename);

    data.user_clusters.clear();
    data.user_clusters.reserve(NUM_CLUSTERS_USER);

    while (context) {
        int user_id = context.expect_int();
        context.expect_literal(':');
        
        if (user_id < 0 || user_id >= data.users.size())
            context.exception("invalid user ID");
        
        int cluster = context.expect_int();
        context.expect_eol();

        data.users[user_id].kmeans_cluster = cluster;

        if (cluster == -1) continue;

        if (cluster >= data.user_clusters.size())
            data.user_clusters.resize(cluster + 1);
        data.user_clusters[cluster].members.push_back(user_id);
        data.user_clusters[cluster].centroid.resize(data.singular_values.size());
        data.user_clusters[cluster].centroid
            += data.users[user_id].singular_vec;
    }

    for (unsigned i = 0;  i < data.user_clusters.size();  ++i) {
        // Normalize the centroid vector
        Cluster & cluster = data.user_clusters[i];
        cluster.centroid /= cluster.centroid.two_norm();

        // Rank the members and store
        vector<pair<int, float> > ranked;
        ranked.reserve(cluster.members.size());
        for (unsigned j = 0;  j < cluster.members.size();  ++j)
            ranked.push_back(make_pair(cluster.members[j],
                                       data.users[cluster.members[j]].user_prob));
        sort_on_second_descending(ranked);

        cluster.top_members.insert(cluster.top_members.end(),
                                   first_extractor(ranked.begin()),
                                   first_extractor(ranked.end()));
    }
}

void
Decomposition::
load_kmeans_repos(const std::string & filename, Data & data)
{
    Parse_Context context(filename);

    data.repo_clusters.clear();
    data.repo_clusters.reserve(NUM_CLUSTERS_REPO);

    while (context) {
        int repo_id = context.expect_int();
        context.expect_literal(':');
        
        if (repo_id < 0 || repo_id >= data.repos.size())
            context.exception("invalid repo ID");
        
        int cluster = context.expect_int();
        context.expect_eol();

        data.repos[repo_id].kmeans_cluster = cluster;
        if (cluster == -1) continue;

        if (cluster >= data.repo_clusters.size())
            data.repo_clusters.resize(cluster + 1);
        data.repo_clusters[cluster].members.push_back(repo_id);
        data.repo_clusters[cluster].centroid.resize(data.singular_values.size());
        data.repo_clusters[cluster].centroid
            += data.repos[repo_id].singular_vec;
    }

    for (unsigned i = 0;  i < data.repo_clusters.size();  ++i) {
        // Normalize the centroid vector
        Cluster & cluster = data.repo_clusters[i];
        cluster.centroid /= cluster.centroid.two_norm();

        // Rank the members and store
        vector<pair<int, float> > ranked;
        ranked.reserve(cluster.members.size());
        for (unsigned j = 0;  j < cluster.members.size();  ++j)
            ranked.push_back(make_pair(cluster.members[j],
                                       data.repos[cluster.members[j]].repo_prob));
        sort_on_second_descending(ranked);

        cluster.top_members.insert(cluster.top_members.end(),
                                   first_extractor(ranked.begin()),
                                   first_extractor(ranked.end()));
    }
}

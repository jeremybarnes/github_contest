/* siamese.cc
   Jeremy Barnes, 9 August 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   A siamese neural network for user/network similarity.

   Parameters:
   - 50 dimension embedding for user and network
   - Features describing user and repo are added
   - 3 hidden layer network with 500/100/50 units for both
   - Training is done on (user, repo) pairs using hinge loss
     - user has recommended repo: try to pull embedded representation closer
     - user didn't recommend repo: try to pull them apart
   - At each iteration, one is used

*/

#include "data.h"
#include "github_features.h"
#include "boosting/perceptron.h"

using namespace ML;
using namespace std;

struct Siamese {

    Siamese()
        : user(10), repo(10)
    {
    }

    /// A siamese network is made up of two twins...
    struct Twin : public vector<ML::Perceptron::Layer> {
        Twin(int ninputs)
            : vector<ML::Perceptron::Layer>(3)
        {
        }
    };

    /// The two twins
    Twin user, repo;

    /// Entry for the data
    struct Data_Entry {
        int user_id;
        int repo_id;
        int num_repos_for_user;
    };

    void train(const Data & data, float learning_rate)
    {
        /* Randomize the training data */
        vector<Data_Entry> shuffled;

        for (unsigned i = 0;  i < data.repos.size();  ++i) {
            const Repo & repo = data.repos[i];
            if (repo.id == -1) continue;

            for (IdSet::const_iterator
                     it = repo.watchers.begin(),
                     end = repo.watchers.end();
                 it != end;  ++it) {

                Data_Entry entry;
                entry.user_id = *it;
                entry.repo_id = i;
                shuffled.push_back(entry);
            }
        }

        std::random_shuffle(shuffled.begin(), shuffled.end());

        /* Now present the examples */
        
        for (unsigned i = 0;  i < shuffled.size();  ++i) {
            // Train both a positive and a negative example

            //int user_id = shuffled[i].user_id;
            //int repo_id = shuffled[i].repo_id;
            //const User & user = data.users[user_id];
            //const Repo & repo = data.repos[repo_id];

            // Get a fake user and repo ID for the negative example
            int fake_user_id;
            do {
                fake_user_id = rand() % data.users.size();
            } while (data.users[fake_user_id].invalid());
            
            int fake_repo_id;
            do {
                fake_repo_id = rand() % data.users.size();
            } while (data.users[fake_repo_id].invalid()
                     || data.users[fake_user_id].watching.count(fake_repo_id));

            train_example(true, shuffled[i].user_id, shuffled[i].repo_id, learning_rate);
            train_example(false, fake_user_id, fake_repo_id, learning_rate);


        }
    }

    void train_example(bool correct, int user_id, int repo_id, float learning_rate)
    {
    }

};

/* decompose.h
   Jeremy Barnes, 16 August 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   Decomposition of matrix.
*/

#ifndef __github__decompose_h__
#define __github__decompose_h__

#include "data.h"

struct Decomposition {

    // Perform a SVD on the adjacency matrix
    void decompose(Data & data);

    // Perform a k-means clustering based upon embedded representation
    void kmeans_repos(Data & data);

    // Ditto for the users
    void kmeans_users(Data & data);

    void save_kmeans_users(std::ostream & stream, const Data & data);
    void save_kmeans_repos(std::ostream & stream, const Data & data);

    void load_kmeans_users(const std::string & filename, Data & data);
    void load_kmeans_repos(const std::string & filename, Data & data);
};

#endif /* __github__decompose_h__ */

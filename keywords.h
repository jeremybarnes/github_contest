/* keywords.h                                                      -*- C++ -*-
   Jeremy Barnes, 22 August 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   Keyword analysis functions.
*/

#ifndef __github__keywords_h__
#define __github__keywords_h__

#include <vector>
#include <string>
#include "data.h"

struct Vocab_Entry {
    Vocab_Entry()
        : id(-1), seen_count(0), in_names(0)
    {
    }

    std::string token;
    int id;
    int seen_count;
    int in_names;

};

struct Name : std::vector<int> {
};

enum Name_Type {
    Repo_Name,
    Description
};

std::vector<std::string> uncamelcase(const std::string & str);

std::vector<std::string> tokenize(const std::string & str,
                                  Name_Type type);

void analyze_keywords(Data & data);



#endif /* __github__keywords_h__ */

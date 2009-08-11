/* features.h                                                      -*- C++ -*-
   Jeremy Barnes, 9 August 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   Feature definition and calculation for machine learning part of
   recommendation engine.
*/

#ifndef __github__features_h__
#define __github__features_h__

#include "boosting/feature_space.h"

boost::shared_ptr<ML::Feature_Space>
get_user_feature_space();

boost::shared_ptr<ML::Feature_Space>
get_repo_feature_space();


#endif /* __githib__features_h__ */

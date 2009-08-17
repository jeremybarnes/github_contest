/* decompose.h
   Jeremy Barnes, 16 August 2009
   Copyright (c) 2009 Jeremy Barnes.  All rights reserved.

   Decomposition of matrix.
*/

#ifndef __github__decompose_h__
#define __github__decompose_h__

#include "svdlibc/svdlib.h"
#include "data.h"

struct Decomposition {
    svdrec result;

    void decompose(const Data & data);
};

#endif /* __github__decompose_h__ */

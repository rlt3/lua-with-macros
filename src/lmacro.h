#ifndef lmacro_h
#define lmacro_h

#include <stdio.h>

int lmacro_llex (LexState *ls, SemInfo *seminfo) {
    int t = llex(ls, seminfo);
    if (t == TK_MACRO) {
        next(ls);
        t = llex(ls, seminfo);
    }
    return t;
}

#define llex lmacro_llex

#endif

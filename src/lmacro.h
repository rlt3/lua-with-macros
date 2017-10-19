#ifndef lmacro_h
#define lmacro_h

#include <stdlib.h>
#include <stdio.h>

#define MACROTABLE "__macro"

static int llex (LexState *ls, SemInfo *seminfo);
static int lmacro_llex (LexState *ls, SemInfo *seminfo);
static void next (LexState *ls);

/* Sometimes you don't want meta-recursion . . . */
static inline int
next_llex (LexState *ls, SemInfo *seminfo)
{
    next(ls);
    return llex(ls, seminfo);
}

/* . . . and sometimes you do */
static inline int
next_lmacro (LexState *ls, SemInfo *seminfo)
{
    next(ls);
    return lmacro_llex(ls, seminfo);
}

/* Push the macro table onto the stack, creating it if it doesn't exist */
static inline void
lmacro_lua_getmacrotable (lua_State *L)
{
    lua_getglobal(L, MACROTABLE);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_setglobal(L, MACROTABLE);
        lua_getglobal(L, MACROTABLE);
    }
}

/* Set a macro and clean the stack */
static inline void 
lmacro_lua_setmacro (lua_State *L, const char *name, const char *definition)
{
    static char key[2] = {'\0'};
    int len = strlen(name) - 1;

    for (int i = 0; i <= len; i++) {
        key[0] = name[i];
        if (i < len)
            lua_newtable(L);
        else
            lua_pushstring(L, definition);
        lua_setfield(L, -2, key);
        lua_getfield(L, -1, key);
        /* move getfield value down and pop previous table */
        lua_insert(L, -2);
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
}

/* 
 * Expects table on top of stack. If table has key [c\0] then return 1. Else 0.
 */
static int
lmacro_ispartial (lua_State *L, char c)
{
    static char key[2] = {'\0'};
    key[0] = c;
    lua_getfield(L, -1, key);
    return lua_isnil(L, -1);
}

static int
lmacro_define (int t, LexState *ls, SemInfo *seminfo)
{
    const char *name = NULL;
    const char *def = NULL;

    if (next_llex(ls, seminfo) != TK_NAME)
        lexerror(ls, "Expected macro name in macro form", 0);
    name = getstr(seminfo->ts);

    if (next_llex(ls, seminfo) != TK_STRING)
        lexerror(ls, "Expected macro definition after name", 0);
    def = getstr(seminfo->ts);

    lmacro_lua_getmacrotable(ls->L);
    lmacro_lua_setmacro(ls->L, name, def);

    return next_lmacro(ls, seminfo);
}

/*
 * TODO:
 * 
 *  - Replace lmacro_get's name-token-as-hash-key lookup with a find-and-replace
 *  using strstr over input buffer as it is updated. Input buffer is not loaded
 *  all at once so we need some sort of mechanism in place that lets us know
 *  when it will be updated. Currently when ls->z->n - 1 == 0 is when it is 
 *  update, so we can set a flag.
 *
 *  - Think of implications of nested macros.
 *
 *  - Think of implications of using this method to replace macros with 
 *  expansions where code isn't simple function-call lookalikes.
 *
 *  - FIX bug in REPL where first character gets consumed from input buffer so
 *  macros at beginning of line won't be replaced.
 *  > macro FUN [[ function ]]
 *  > FUN e () return 10 end
 *  error
 *  >  FUN e () return 10 end
 *  OK
 */

static int
lmacro_llex (LexState *ls, SemInfo *seminfo)
{
    int t = llex(ls, seminfo);
    switch (t) {
        case TK_MACRO:
            t = lmacro_define(t, ls, seminfo);
            break;
    }
    return t;
}

#endif

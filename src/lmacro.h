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

/* 
 * Expects a table on top of the stack. If the table has the key "%c\0",
 * returns 1, else 0. This function removes the table from the top of the stack
 * and replaces it with whatever is pushed from looking up the key.
 */
static int
lmacro_ispartial (lua_State *L, char c)
{
    static char key[2] = {'\0'};
    if (c == EOZ)
        return 0;
    key[0] = c;
    lua_getfield(L, -1, key);
    lua_insert(L, -2);
    lua_pop(L, 1);
    return lua_istable(L, -1);
}

/*
 * Setup the hash tables for a particular defintion. Each character gets its
 * own table. The first character is always in the macro table itself. As an
 * example with the macro form ``macro DEFINE [[macro]]'' then the macro table
 * is setup as ``__macro["D"]["E"]["F"]["I"]["E"] = "macro"''
 */
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
 * Parse a macro form. Right now this is simply the most basic form of:
 * macro <name> <string>
 */
static int
lmacro_define (LexState *ls, SemInfo *seminfo)
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

static int
lmacro_llex (LexState *ls, SemInfo *seminfo)
{
    int t = llex(ls, seminfo);
    switch (t) {
        case TK_MACRO:
            t = lmacro_define(ls, seminfo);
            break;
    }
    return t;
}

#endif

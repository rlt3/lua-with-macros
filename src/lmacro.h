#ifndef lmacro_h
#define lmacro_h

#include <stdlib.h>
#include <stdio.h>

#define MACROTABLE "__macro"

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
    lua_pushstring(L, definition);
    lua_setfield(L, -2, name);
    lua_pop(L, 1);
}

/* 
 * If macro exists return 1 and leaves the value on top of the stack. If the
 * macro doesn't exist, cleans the stack and returns 0.
 */
static inline int
lmacro_lua_ismacro (lua_State *L, const char *name)
{
    lua_getfield(L, -1, name);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 2);
        return 0;
    }
    return 1;
}

/* Get macro's replacement. Leaves stack clean */
static inline const char * 
lmacro_lua_getmacro (lua_State *L)
{
    const char *def = lua_tostring(L, -1);
    lua_pop(L, 2);
    return def;
}

static int lmacro_llex (LexState *ls, SemInfo *seminfo);

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

static int
lmacro_get (int t, LexState *ls, SemInfo *seminfo)
{
    const char *name = getstr(seminfo->ts);
    const char *def = NULL;
    char *buff = NULL;
    int def_len = 0;
    int replace_len = 0;

    lmacro_lua_getmacrotable(ls->L);
    if (!lmacro_lua_ismacro(ls->L, name))
        goto exit;

    def = lmacro_lua_getmacro(ls->L);
    def_len = strlen(def);
    replace_len = def_len + 1 + ls->z->n;

    /* concatenate the rest of the lexical buffer onto the macro form */
    buff = calloc(replace_len, sizeof(char*));
    if (!buff)
        lexerror(ls, "not enough memory", 0); /* from MEMERRMSG */
    strncpy(buff, def, def_len);
    buff[def_len] = ls->current;
    buff[def_len + 1] = '\0';
    strncat(buff, ls->z->p, ls->z->n);

    /* let Lua itself handle its own input buffer memory */
    ls->z->p = getstr(luaX_newstring(ls, buff, replace_len));
    free(buff);

    /* consume the macro identifier (which we don't replace, but skip) */
    t = next_lmacro(ls, seminfo);

exit:
    return t;
}

static int
lmacro_llex (LexState *ls, SemInfo *seminfo)
{
    int t = llex(ls, seminfo);
    switch (t) {
        case TK_MACRO:
            t = lmacro_define(t, ls, seminfo);
            break;

        case TK_NAME:
            t = lmacro_get(t, ls, seminfo);
            break;
    }
    return t;
}

#define llex lmacro_llex

#endif

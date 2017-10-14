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

static void
lmacro_replace (LexState *ls, const char *name, const char *replace)
{
    const int name_len = strlen(name);
    const int rplc_len = strlen(replace);
    const char *p = ls->z->p;
    int num_replacements = 0;

    while (p && (p = strstr(p, name))) {
        num_replacements++;
        p += name_len;
    }

    const int len = ls->z->n + ((rplc_len - name_len) * num_replacements);
    char *buff = calloc(len, sizeof(char*));

    p = ls->z->p;
    const char *last_p = p;
    const char *s;
    int slen;
    int n;

    for (n = 0; n < num_replacements; n++) {
        p = strstr(p, name);
        for (s = last_p, slen = 0; s && s != p; s++, slen++) ;
        strncat(buff, last_p, slen);
        strncat(buff, replace, rplc_len);
        p = p + name_len;
        last_p = p;
    }
    for (s = last_p, slen = 0; s && *s != '\0'; s++, slen++) ;
    strncat(buff, last_p, slen);

    ls->z->p = getstr(luaX_newstring(ls, buff, len));
    ls->z->n = len;

    free(buff);
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

    lmacro_replace(ls, name, def);
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
    ls->z->n = replace_len;
    free(buff);

    /* consume the macro identifier (which we don't replace, but skip) */
    t = next_lmacro(ls, seminfo);

exit:
    return t;
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
 */

static int
lmacro_llex (LexState *ls, SemInfo *seminfo)
{
    int t = llex(ls, seminfo);
    switch (t) {
        case TK_MACRO:
            // printf("[[ %s ]]\n", ls->z->p);
            t = lmacro_define(t, ls, seminfo);
            break;

        case TK_NAME:
            // t = lmacro_get(t, ls, seminfo);
            break;
    }
    return t;
}

#define llex lmacro_llex

#endif

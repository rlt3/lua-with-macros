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
    size_t len = lua_rawlen(L, -1);

    lua_newtable(L);
    lua_pushstring(L, name);
    lua_rawseti(L, -2, 1);
    lua_pushstring(L, definition);
    lua_rawseti(L, -2, 2);

    lua_rawseti(L, -2, ++len);
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

/* strnstr with a twist -- set len it took to find subtstr */
static const char *
lmacro_findsubstr (const char *hay, const int haylen,
                   const char *ndl, const int ndllen,
                   int *len)
{
    *len = 0;
    if (ndllen > haylen)
        return NULL;
    while (strncmp(hay, ndl, ndllen) != 0) {
        hay++;
        (*len)++;
        if (!hay || *len > haylen)
            return NULL;
    }
    return hay;
}

static void
lmacro_replace (LexState *ls, const char *name, const char *replace)
{
    const int name_len = strlen(name);
    const int rplc_len = strlen(replace);
    const char *p = ls->z->p;
    int plen = ls->z->n;
    int num_replacements = 0;
    int dst = 0;
    char *buff = NULL;
    int bufflen = 0;

    /* lua doesn't mark the end of the input buffer as '\0' */
    ((char*)ls->z->p)[ls->z->n] = '\0';

    /* find all replacements first rather than guess about memory */
    while (p && (p = lmacro_findsubstr(p, plen, name, name_len, &dst))) {
        num_replacements++;
        p += name_len;
        plen -= dst + name_len;
    }

    /* then actually do the replacments on the buffer */
    bufflen = ls->z->n + ((rplc_len - name_len) * num_replacements);
    buff = calloc(bufflen, sizeof(char*));
    p = ls->z->p;
    plen = ls->z->n;
    dst = 0;

    for (int n = 0; n < num_replacements; n++) {
        p = lmacro_findsubstr(p, plen, name, name_len, &dst);
        strncat(buff, p - dst, dst);
        strncat(buff, replace, rplc_len);
        p += name_len;
        plen -= dst + name_len;
    }
    strncat(buff, p, plen);

    /* use Lua's mem so it can handle its own input buffer */
    ls->z->p = getstr(luaX_newstring(ls, buff, bufflen));
    ls->z->n = bufflen;
    free(buff);
}

static void
lmacro_updatebuffer (LexState *ls, SemInfo *seminfo)
{
    lua_State *L = ls->L;
    const char *name, *definition;

    lmacro_lua_getmacrotable(L);
    for (int i = 1 ;; i++) {
        lua_rawgeti(L, -1, i);
        if (lua_isnil(L, -1)) {
            lua_pop(L, 1);
            break;
        }

        lua_rawgeti(L, -1, 1);
        name = lua_tostring(L, -1);
        lua_pop(L, 1);

        lua_rawgeti(L, -1, 2);
        definition = lua_tostring(L, -1);
        lua_pop(L, 1);

        lua_pop(L, 1);
        lmacro_replace(ls, name, definition);
    }
    lua_pop(L, 1);
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
    if (ls->z->l) {
        lmacro_updatebuffer(ls, seminfo);
        ls->z->l = 0;
    }

    int t = llex(ls, seminfo);
    switch (t) {
        case TK_MACRO:
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

#ifndef lmacro_h
#define lmacro_h

#include <stdlib.h>
#include <stdio.h>

#define MACROTABLE  "__macro"
#define READERTABLE "__reader"

#define skipwhitespace(ls) \
    while (lisspace(ls->current) || currIsNewline(ls)) { \
        if (currIsNewline(ls)) \
            inclinenumber(ls); /* calls next internally */ \
        else \
            next(ls); \
    }

static int llex (LexState *ls, SemInfo *seminfo);
static int lmacro_llex (LexState *ls, SemInfo *seminfo);
static void next (LexState *ls);
static void inclinenumber (LexState *ls);
static int skip_sep (LexState *ls);
static void read_long_string (LexState *ls, SemInfo *seminfo, int sep);
static void read_string (LexState *ls, int del, SemInfo *seminfo);
extern int luaL_loadbufferx (lua_State *, const char *, size_t,
                             const char *, const char *);
extern int luaL_ref(lua_State *L, const int);

static inline void
lmacro_lua_getglobaltable (lua_State *L, const char *name)
{
    lua_getglobal(L, name);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_setglobal(L, name);
        lua_getglobal(L, name);
    }
}

/* Push the macro table onto the stack, creating it if it doesn't exist */
static inline void
lmacro_lua_getmacrotable (lua_State *L)
{
    lmacro_lua_getglobaltable(L, MACROTABLE);
}

/* Set function ref on top of stack as key in reader. Balances stack. */
static inline void
lmacro_lua_setreader (lua_State *L)
{
    lmacro_lua_getglobaltable(L, READERTABLE);
    lua_insert(L, -2); /* move function on top */
    lua_pushboolean(L, 1);
    lua_settable(L, -3);
    lua_pop(L, 1);
}

/* Returns true if func reference `r' is in reader table */
static int
lmacro_lua_isreader (lua_State *L, const int r)
{
    int reader = 0;
    lmacro_lua_getglobaltable(L, READERTABLE);
    lua_pushinteger(L, r);
    lua_gettable(L, -2);
    if (lua_isboolean(L, -1))
        reader = 1;
    lua_pop(L, 2); /* table value and table */
    return reader;
}

/* 
 * Expects a table on top of the stack. If the table has the key "%c\0",
 * returns 1, else 0. This function removes the table from the top of the stack
 * and replaces it with whatever is pushed from looking up the key.
 */
enum MacroMatch {
    MATCH_FAIL = 0,
    MATCH_PARTIAL,
    MATCH_SUCCESS_FUN,
    MATCH_SUCCESS_SMP,
    MATCH_SUCCESS_RDR,
};

static int
lmacro_match (lua_State *L, char c)
{
    static char key[2] = {'\0'};
    int ret = MATCH_FAIL;
    if (c == EOZ)
        goto exit;
    key[0] = c;
    lua_getfield(L, -1, key);
    lua_insert(L, -2);
    lua_pop(L, 1);
    if (lua_istable(L, -1))
        ret = MATCH_PARTIAL;
    else if (lua_isinteger(L, -1)) /* must be tested first */
        ret = MATCH_SUCCESS_FUN;
    else if (lua_isstring(L, -1))  /* true if val is convertable to string */
        ret = MATCH_SUCCESS_SMP;
exit:
    return ret;
}

/*
 * Takes the string on top of the stack and places it in the macro buffer.
 */
static inline void
lmacro_setmacrobuff (LexState *ls)
{
    size_t len;
    const char *str = lua_tolstring(ls->L, -1, &len);
    if (!str)
        lexerror(ls, "Macro expansion must return a string", ls->current);
    if (len >= BUFSIZ)
        lexerror(ls, "Macro expansion overflows buffer", ls->current);
    strncpy(ls->macro.buff, str, len);
    ls->macro.buff[len] = '\0';
    ls->macro.has_replace = 1;
    ls->macro.idx = 0;
}

/*
 * Get the next character from the macro buffer.
 */
static inline char
lmacro_next (LexState *ls)
{
    if (ls->macro.idx + 1 >= BUFSIZ)
        lexerror(ls, "Macro expansion overflows buffer", 0);
    char c = ls->macro.buff[ls->macro.idx++];
    if (c == '\0') {
        ls->macro.has_replace = 0;
        ls->macro.has_buff = 0;
        ls->macro.idx = 0;
    }
    return c;
}

/* 
 * Place the string on top of the stack onto the macro buffer.
 */
static char
lmacro_replacesimple (LexState *ls)
{
    lmacro_setmacrobuff(ls);
    return lmacro_next(ls);
}

/*
 * Next function skips the overridden `next` function declared in this file.
 * This lets the reader macro read the raw source before macro transformations
 * have occured.
 */
static int
lmacro_luafunction_next (lua_State *L)
{
    LexState *ls = lua_touserdata(L, lua_upvalueindex(1));
    if (!ls) {
        lua_pushstring(L, "Lua's lexical state doesn't exist");
        lua_error(L);
    }
    if (ls->current == EOZ) {
        lua_pushstring(L, "Unexpected end of file in reader macro");
        lua_error(L);
    }
    ls->current = zgetc(ls->z);
    if (ls->current == EOZ) {
        lua_pushstring(L, "Unexpected end of file in reader macro");
        lua_error(L);
    }
    lua_pushfstring(ls->L, "%c", ls->current);
    return 1;
}

static int
lmacro_luafunction_curr (lua_State *L)
{
    LexState *ls = lua_touserdata(L, lua_upvalueindex(1));
    if (!ls) {
        lua_pushstring(L, "Lua's lexical state doesn't exist");
        lua_error(L);
    }
    lua_pushfstring(ls->L, "%c", ls->current);
    return 1;
}

static int
lmacro_luafunction_word (lua_State *L)
{
#define getcharsmatching(match) \
{ \
    if (match(ls->current)) { \
        do { \
            if (i + 1 >= BUFSIZ) { \
                lua_pushstring(L, "Reader macro will overflow buffer"); \
                lua_error(L); \
            } \
            if (ls->current == EOZ) { \
                lua_pushstring(L, "Unexpected end of file in reader macro"); \
                lua_error(L); \
            } \
            buff[i++] = ls->current; \
            ls->current = zgetc(ls->z); \
        } while (ls->current != c && \
                 !lisspace(ls->current) && \
                 match(ls->current)); \
        goto exit; \
    } \
}

    int i = 0;
    char c = '\0';
    char buff[BUFSIZ] = {'\0'};
    LexState *ls = lua_touserdata(L, lua_upvalueindex(1));

    if (!ls) {
        lua_pushstring(L, "Lua's lexical state doesn't exist");
        lua_error(L);
    }

    if (lua_isstring(L, -1)) {
        const char *s = lua_tostring(L, -1);
        if (!s) {
            lua_pushstring(L, "String cannot be null to `nextw`.");
            lua_error(L);
        }
        c = s[0];
        if (ls->current == c) {
            ls->current = zgetc(ls->z);
            buff[0] = c;
            goto exit;
        }
    }

    if (ls->current == EOZ) {
        lua_pushstring(L, "Unexpected end of file in reader macro");
        lua_error(L);
    }

    skipwhitespace(ls);

    if (ls->current == EOZ) {
        lua_pushstring(L, "Unexpected end of file in reader macro");
        lua_error(L);
    }

    getcharsmatching(lislalpha);
    getcharsmatching(lisdigit);
    getcharsmatching(lispunct);

exit:
    lua_pushstring(L, buff);
    return 1;
}

/*
 * Push next function to reader macro along with the current character which
 * should be the last character of the reader macro's name.
 */
static char
lmacro_replacereader (LexState *ls, const int c)
{
    next(ls); /* remove current char which matches last char of macro name */
    ls->macro.in_reader = 1;

    lua_pushlightuserdata(ls->L, ls);
    lua_pushcclosure(ls->L, &lmacro_luafunction_next, 1);

    lua_pushlightuserdata(ls->L, ls);
    lua_pushcclosure(ls->L, &lmacro_luafunction_curr, 1);

    lua_pushlightuserdata(ls->L, ls);
    lua_pushcclosure(ls->L, &lmacro_luafunction_word, 1);

    if (lua_pcall(ls->L, 3, 1, 0)) {
        const char *err = lua_tostring(ls->L, -1);
        lexerror(ls, err, c);
    }

    if (!lua_isstring(ls->L, -1) || lua_isnumber(ls->L, -1)) {
        lexerror(ls, "Macro reader must return a string", c);
    }

    ls->macro.in_reader = 0;
    lmacro_setmacrobuff(ls);

    if (ls->z->p - 1 != NULL) {
        ls->z->p--;
        ls->z->n++;
        *((char*)ls->z->p) = ls->current;
    }

    return lmacro_next(ls);
}

/* 
 * Get the string that replaces a macro string from a function on top of the
 * stack and place that string into the macro buffer.
 */
static char
lmacro_replacefunction (LexState *ls, int c)
{
    int i = lua_tointeger(ls->L, -1);
    lua_pop(ls->L, 1);
    lua_rawgeti(ls->L, LUA_REGISTRYINDEX, i);

    if (lmacro_lua_isreader(ls->L, i))
        return lmacro_replacereader(ls, c);

    /* 
     * The current LexState's buffer can now be dismissed for this
     * macro's replacement to allow us to call `next' to find other
     * macro replacements as arguments to this macro function.
     */
    ls->macro.idx = 0;
    ls->macro.buff[0] = '\0';
    ls->macro.has_buff = 0;

    
    /* TODO: in recursive next calls, if lexerror occurs, who frees this? */
    char *argstr = calloc(BUFSIZ, sizeof(char));
    int args = 0;

    if (!argstr)
        lexerror(ls, "Not enough memory for macro form", 0);

    next(ls);
    c = ls->current;
    if (c != '(') {
        free(argstr);
        lexerror(ls, "Expected '(' to start argument list", c);
    }

    int is_newline = 0;
    for (i = 0 ;; i++) {
        /* 
         * newline is a valid character that needs to be pushed as argument.
         * inclinenumber calls next, so we need to let the newline be written
         * to argstr before incrementing the line number.
         */
        if (is_newline) {
            inclinenumber(ls);
            is_newline = 0;
        } else {
            next(ls);
            if (currIsNewline(ls))
                is_newline = 1;
        }

        c = ls->current;

        if (c == EOZ)
            break;

        if (c == ',' || c == ')') {
            if (i > 0) {
                argstr[i] = '\0';
                lua_pushstring(ls->L, argstr);
                args++;
            }
            i = -1;
            if (c == ')')
                break;
            continue;
        }

        argstr[i] = c;
    }

    if (c != ')') {
        free(argstr);
        lexerror(ls, "Missing ')' to close argument list", c);
    }

    if (lua_pcall(ls->L, args, 1, 0)) {
        free(argstr);
        const char *err = lua_tostring(ls->L, -1);
        lexerror(ls, err, c);
    }

    if (!lua_isstring(ls->L, -1) || lua_isnumber(ls->L, -1)) {
        free(argstr);
        lexerror(ls, "Macro function must return a string", c);
    }

    free(argstr);
    lmacro_setmacrobuff(ls);
    return lmacro_next(ls);
}

/*
 * Read ahead as many characters as it takes to fail or succeed a match. Saves
 * read characters into the LexState's macro buffer. If read characters form
 * a macro, the macro buffer becomes the replacement.
 */
static char
lmacro_matchpartial (LexState *ls, char c)
{
    int match = MATCH_PARTIAL;
    int i = ls->macro.idx;

    while (match == MATCH_PARTIAL) {
        /* if the buffer isn't active then append to our buffer */
        if (!ls->macro.has_buff) { /* implicitly i starts at 0 */
            ls->macro.buff[i] = c;
            /* always write EOZ to buffer because no sentinels after EOZ */
            if (c == EOZ)
                break;
            c = zgetc(ls->z);
        }
        /* the buffer is active, read from it. if buff ends, then append */
        else {
            if (ls->macro.buff[i] != '\0') {
                c = ls->macro.buff[i];
            } else {
                c = zgetc(ls->z);
                ls->macro.buff[i] = c;
                ls->macro.buff[i + 1] = '\0';
            }
            if (c == EOZ)
                break;
        }
        i++;
        match = lmacro_match(ls->L, c);
    }

    switch (match) {
        /* current c must be part of macro form, simple or function */
        case MATCH_SUCCESS_FUN:
            c = lmacro_replacefunction(ls, c);
            break;

        case MATCH_SUCCESS_SMP:
            c = lmacro_replacesimple(ls);
            break;

        case MATCH_FAIL:
        default:
        {
            /* 
             * In either of these statements, the idx is set to the first 
             * character that started the failed macro replacement sequence.
             */
            if (!ls->macro.has_buff) {
                /* current c is lookahead that failed lmacro_match */
                ls->macro.buff[i] = c;
                ls->macro.buff[i + 1] = '\0';
                ls->macro.has_buff = 1;
                ls->macro.idx = 0;
            } else {
                ls->macro.idx--;
            }
            c = lmacro_next(ls);
            break;
        }
    }
    return c;
}

/*
 * Sets ls->current to the next character from the input buffer.
 * If a char read is a partial match to a macro string then read ahead more
 * characters into a temp buffer.
 * If those characters match a macro, then the buffer is set to the replacement
 * and the tmp buffer is read until the nil char \0.
 * If the read ahead characters don't match a replacement then those characters
 * are read from the tmp buffer one at a time and are then tested again to see
 * if they are apart of a macro string.
 */
static void
next (LexState *ls)
{
    char c = EOZ;

    if (ls->macro.has_replace || ls->macro.has_buff) {
        c = lmacro_next(ls);
        if (ls->macro.has_replace && c != '\0')
            goto setchar;
    }

    if (!(ls->macro.has_replace || ls->macro.has_buff))
        c = zgetc(ls->z);

    if (ls->macro.in_comment || ls->macro.in_reader)
        goto setchar;

    lmacro_lua_getmacrotable(ls->L);

    switch (lmacro_match(ls->L, c)) {
        case MATCH_SUCCESS_FUN:
            c = lmacro_replacefunction(ls, c);
            break;

        case MATCH_SUCCESS_SMP:
            c = lmacro_replacesimple(ls);
            break;

        case MATCH_PARTIAL:
            c = lmacro_matchpartial(ls, c);
            break;

        case MATCH_FAIL:
        default:
            break;
    }

    lua_pop(ls->L, 1);

setchar:
    ls->current = c;
    return;
}

/* 
 * Expects macro table on top with value below. This function uses each letter
 * of the string `name' as the key to a table containing the next letter and so
 * on until the final letter which is set to the value on top of the stack.  A
 * simple example with "DEFINE" being the name "macro" being the definition:
 *      __macro["D"]["E"]["F"]["I"]["E"] = "macro"
 * The Lua stack is balanced after setting the macro.
 */
static inline void 
lmacro_lua_setmacro (lua_State *L, const char *name)
{
    static char key[2] = {'\0'};
    const int len = strlen(name) - 1;
    const int val = lua_gettop(L) - 1;

    for (int i = 0; i <= len; i++) {
        key[0] = name[i];

        lua_getfield(L, -1, key);

        if (lua_isnil(L, -1)) {
            lua_pop(L, 1);
            if (i < len)
                lua_newtable(L);
            else
                lua_pushvalue(L, val);
            lua_setfield(L, -2, key);
            lua_getfield(L, -1, key);
        }

        lua_insert(L, -2); /* move gotten field down and pop previous table */
        lua_pop(L, 1);
    }
    lua_pop(L, 2); /* macro table and val */
}

/*
 * Using each letter as successive keys to tables, get value in macrotable.
 */
static inline void 
lmacro_lua_getmacro (lua_State *L, const char *name)
{
    static char key[2] = {'\0'};
    const int len = strlen(name) - 1;
    lmacro_lua_getmacrotable(L);
    for (int i = 0; lua_istable(L, -1) && i <= len; i++) {
        key[0] = name[i];
        lua_getfield(L, -1, key);
        lua_insert(L, -2); /* move gotten field down and pop previous table */
        lua_pop(L, 1);
    }
}

static int
lmacro_simpleform (const char *name, LexState *ls, SemInfo *seminfo)
{
    const char *def = NULL;

    if (llex(ls, seminfo) != TK_STRING)
        lexerror(ls, "Expected macro definition after name", TK_MACRO);
    def = getstr(seminfo->ts);

    lua_pushstring(ls->L, def);
    lmacro_lua_getmacrotable(ls->L);
    lmacro_lua_setmacro(ls->L, name);

    if (!(currIsNewline(ls) || ls->current == ';'))
        lexerror(ls, "Expected end of macro definition", TK_MACRO);

    /* start lexing at the normal level again */
    return lmacro_llex(ls, seminfo);
}

static inline void
lmacro_checkbuffer (LexState *ls, const int index, const int plus)
{
    if (index + plus > BUFSIZ)
        lexerror(ls, 
                "Macro function form not allowed to overflow buffer", TK_MACRO);
}

/*
 * Have a buffer for whitespace delimited chunks. While reading into the buffer
 * check the value of ls->current for parenthesis, opened and closed.  Check
 * the chunks for 'end','then', 'do', etc to make sure the end's are balanced.
 */
static int
lmacro_functionform (const char *name, const int is_reader,
                     LexState *ls, SemInfo *seminfo)
{
    static const char *ret_form = "return function";
    char buff[BUFSIZ] = {'\0'};
    const char *str = NULL;
    int len = 15; /* ret_form length */
    int index = len;
    int start = 0;
    int parens = 0;
    int ends = 1;
    int in_scomment = 0;
    int in_lcomment = 0;

    strncpy(buff, ret_form, len);

    while (1) {
reset:
        luaZ_resetbuffer(ls->buff);
        start = index;
        str = NULL;

        /* grab all non-whitespace characters into the buffer */
        while (!lisspace(ls->current)) {
            switch (ls->current) {
                case '(': parens++; break;
                case ')': parens--; break;
                case EOZ:
                    lexerror(ls,
                            "Unexpected end of file in macro form", TK_MACRO);
                    break;
            }

            /* don't parse strings inside comments */
            if (!(in_lcomment || in_scomment)) {
                int strc = 0;
                int seps = 0;

                /* 
                 * look for string literals and it all. we don't need to look
                 * inside strings and test for 'do', 'end', etc.
                 */
                switch (ls->current) {
                    case '[':
                        seps = skip_sep(ls);
                        if (seps >= 0)
                            read_long_string(ls, seminfo, seps);
                        else if (seps != -1)
                            lexerror(ls,
                                    "invalid long string delimiter", TK_STRING);
                        else
                            break; /* just a '[' character */
                        str = getstr(seminfo->ts);
                        break;

                    case '"': case '\'':
                        strc = ls->current;
                        read_string(ls, ls->current, seminfo);
                        str = getstr(seminfo->ts);
                        break;
                }

                if (str) {
                    /* preprend string designator */
                    if (strc != 0) {
                        lmacro_checkbuffer(ls, index, 1);
                        buff[index++] = strc;
                    } else {
                        lmacro_checkbuffer(ls, index, seps + 2);
                        buff[index++] = '[';
                        for (len = 0; len < seps; len++)
                            buff[index++] = '=';
                        buff[index++] = '[';
                    }

                    len = strlen(str);
                    lmacro_checkbuffer(ls, index, len);
                    strncat(buff, str, len);
                    index += len;

                    /* append string designator */
                    if (strc != 0) {
                        lmacro_checkbuffer(ls, index, 1);
                        buff[index++] = strc;
                    } else {
                        lmacro_checkbuffer(ls, index, seps + 2);
                        buff[index++] = ']';
                        for (len = 0; len < seps; len++)
                            buff[index++] = '=';
                        buff[index++] = ']';
                    }

                    /* ls->current needs to be retested */
                    goto reset;
                }
            }

            lmacro_checkbuffer(ls, index, 1);
            buff[index++] = ls->current;
            next(ls);
        }

        /* skip line comments */
        if (strncmp(buff + start, "--", 2) == 0)
            in_scomment = 1;
        if (strncmp(buff + start, "--[[", 4) == 0)
            in_lcomment = 1;
        if (strncmp(buff + start, "--]]", 4) == 0)
            in_lcomment = 0;

        int len = index - start;
        if (len > 1 && !(in_lcomment || in_scomment)) {
            if (strncmp(buff + start, "do", len) == 0
                || strncmp(buff + start, "function", len) == 0
                || strncmp(buff + start, "then", len) == 0)
                ends++;
            else if (strncmp(buff + start, "end", len) == 0)
                ends--;
        }

        if (parens == 0 && ends == 0)
            break;

        /* grab whitespace characters into buffer */
        do {
            lmacro_checkbuffer(ls, index, 1);
            buff[index++] = ls->current;
            if (currIsNewline(ls)) {
                if (in_scomment)
                    in_scomment = 0;
                inclinenumber(ls); /* this calls `next` a bunch */
            } else {
                next(ls);
            }
            if (ls->current == EOZ)
                lexerror(ls, "Unexpected end of file in macro form", TK_MACRO);
        } while (lisspace(ls->current));
    }

    next(ls);

    if (luaL_loadbufferx(ls->L, buff, index, name, "text") != 0) {
        const char *err = lua_tostring(ls->L, -1);
        lexerror(ls, err, TK_MACRO);
    }

    if (lua_pcall(ls->L, 0, 1, 0) != 0) {
        const char *err = lua_tostring(ls->L, -1);
        lexerror(ls, err, TK_MACRO);
    }

    /* use reference as value/key in tables */
    int r = luaL_ref(ls->L, LUA_REGISTRYINDEX);
    lua_pushinteger(ls->L, r);

    lmacro_lua_getmacrotable(ls->L);
    lmacro_lua_setmacro(ls->L, name);

    if (is_reader) {
        lmacro_lua_getmacro(ls->L, name);
        lmacro_lua_setreader(ls->L);
    }
    return lmacro_llex(ls, seminfo);
}

void
lmacro_parsename (char *name, LexState *ls)
{
    int i = 0;

#define nameaddchar(c) \
    if (i + 1 >= BUFSIZ) \
        lexerror(ls, "Macro name overflows buffer", TK_MACRO); \
    name[i++] = c; \
    name[i] = '\0';

    /* We allow linebreaks after macro token */
    if (!(lisspace(ls->current) || currIsNewline(ls)))
        lexerror(ls, "Expected macro name", ls->current);

    if (currIsNewline(ls))
        inclinenumber(ls); /* calls next internally */
    else
        next(ls);

    while (!(lisspace(ls->current) || currIsNewline(ls))) {
        if (ls->current == EOZ)
            lexerror(ls, "Unexpected end of file during macro name", TK_MACRO);

        if (ls->current == '[' || ls->current == ']') {
            int t = ls->current;
            int s = skip_sep(ls); /* calls next */
            luaZ_resetbuffer(ls->buff);
            if (s >= 0)
                lexerror(ls, "Macro name has long-string brackets", TK_MACRO);
            nameaddchar(t); /* t is vetted against checks but not ls->current */
            continue;
        }

        nameaddchar(ls->current);
        next(ls);
    }

    /* We allow linebreaks after macro's name */
    if (!(lisspace(ls->current) || currIsNewline(ls)))
        lexerror(ls, "Expected macro name", ls->current);

    skipwhitespace(ls);

    if (ls->current == EOZ)
        lexerror(ls, "Unexpected end of file during macro name", TK_MACRO);
}

/*
 * Parse a macro form.
 * Simple macro: macro <name> <string>
 * Function macro: macro <name> ([args|,]+) [<expr>]+ end
 * Reader macro: readermacro <name> (next, curr) [<expr>]+ end
 *
 * <name> is any characters in any order except [=*[ and ]=*] both prepended
 * and postpended with a whitespace or newline character.
 */
static int
lmacro_parse (const int is_reader, LexState *ls, SemInfo *seminfo)
{
    char name[BUFSIZ] = {'\0'};
    lmacro_parsename(name, ls);

    if (ls->current == '(')
        return lmacro_functionform(name, is_reader, ls, seminfo);

    if (is_reader)
        lexerror(ls, "Expected '(' to start argument list", ls->current);

    return lmacro_simpleform(name, ls, seminfo);
}

static int
lmacro_llex (LexState *ls, SemInfo *seminfo)
{
    int t = llex(ls, seminfo);
    switch (t) {
        case TK_MACRO:
            t = lmacro_parse(0, ls, seminfo);
            break;
        case TK_READERMACRO:
            t = lmacro_parse(1, ls, seminfo);
            break;
    }
    return t;
}

#endif

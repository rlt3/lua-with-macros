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
    int do_collect = 0;
    LexState *ls = lua_touserdata(L, lua_upvalueindex(1));
    if (!ls) {
        lua_pushstring(L, "Lua's lexical state doesn't exist");
        lua_error(L);
    }
    if (lua_isboolean(L, -1)) {
        do_collect = lua_toboolean(L, -1);
        lua_pop(L, 1);
    }
    /* if passing 'true' into function, collect chars by whitespace */
    if (do_collect) {
        char buff[BUFSIZ] = {'\0'};
        int i = 0;
        skipwhitespace(ls);
        while (!(lisspace(ls->current) || currIsNewline(ls))) {
            if (ls->current == EOZ) {
                lua_pushstring(L, "Unexpected end of file in reader macro");
                lua_error(L);
            }
            if (i + 1 >= BUFSIZ) {
                lua_pushstring(L, "Reader macro will overflow buffer");
                lua_error(L);
            }
            buff[i++] = ls->current;
            next(ls);
        }
        lua_pushstring(ls->L, buff);
    } else {
        next(ls);
        if (ls->current == EOZ) {
            lua_pushstring(L, "Unexpected end of file in reader macro");
            lua_error(L);
        }
        lua_pushfstring(ls->L, "%c", ls->current);
    }
    return 1;
}

static char
lmacro_replacereader (LexState *ls, const int c)
{
    ls->macro.in_reader = 1;

    lua_pushlightuserdata(ls->L, ls);
    lua_pushcclosure(ls->L, &lmacro_luafunction_next, 1);
    lua_pushfstring(ls->L, "%c", c);

    if (lua_pcall(ls->L, 2, 1, 0)) {
        const char *err = lua_tostring(ls->L, -1);
        lexerror(ls, err, ls->current);
    }

    if (!lua_isstring(ls->L, -1) || lua_isnumber(ls->L, -1)) {
        lexerror(ls, "Macro reader must return a string", ls->current);
    }

    ls->macro.in_reader = 0;
    lmacro_setmacrobuff(ls);
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
lmacro_tokenhasend (int t)
{
    switch (t) {
        case TK_DO:
        case TK_THEN:
        case TK_FUNCTION:
            return 1;
        default:
            return 0;
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

/*
 * Read the function macro function as an anonymous function into Lua. Let Lua
 * compile the function as a string and output any errors. We simply uses a 
 * combination of the lexer, char tokens, and string representations of
 * reserved keywords to build the function while also parsing it.
 * Uses the name of the macro and pushes each letter as a key to a new table
 * until the final letter which points to the compiled anonymous function.
 */
static int
lmacro_functionform (const char *name, const int is_reader,
                     LexState *ls, SemInfo *seminfo)
{
#define buff_append(str) \
    if (i + 1 > BUFSIZ) \
        lexerror(ls, "Macro form not allowed to overflow buffer", TK_MACRO); \
    len = strlen(str); \
    i += len; \
    strncat(buffer, str, len); \
    buffer[i] = '\0'; \
    str = NULL;

    /* Can't be static for some reason. Search for COMPILER */
    const char *ret_form = "return function (";
    const char *str = NULL;
    char buffer[BUFSIZ] = {0};
    int parens = 1; /* we skip past '(' that brought us into this function */
    int ends = 1; /* macro function expects end */
    int len = 0;
    int i = 0;
    int t;

    buff_append(ret_form);

    while (ends > 0 || parens > 0) {
        str = NULL;

        if (currIsNewline(ls))
            inclinenumber(ls); /* this calls `next` a bunch */
        else
            next(ls);
        t = llex(ls, seminfo);

        if (t == TK_EOS)
            lexerror(ls, "Unexpected end of input in macro form", TK_MACRO);

        switch (t) {
            case ')': str = ")"; break;
            case '(': str = "("; break;
            case '+': str = "+"; break;
            case '-': str = "-"; break;
            case '/': str = "/"; break;
            case '*': str = "*"; break;
            case ',': str = ","; break;
            case '.': str = "."; break;
            case ':': str = ":"; break;
            case '[': str = "["; break;
            case ']': str = "]"; break;
            case '=': str = "="; break;
            case '<': str = "<"; break;
            case '>': str = ">"; break;
            case '~': str = "~"; break;

            /* all the reserved keywords and symbols */
            case TK_GOTO:   case TK_IF:    case TK_IN:   case TK_LOCAL:
            case TK_NIL:    case TK_NOT:   case TK_OR:   case TK_REPEAT:
            case TK_RETURN: case TK_THEN:  case TK_TRUE: case TK_UNTIL:
            case TK_WHILE:  case TK_MACRO: case TK_IDIV: case TK_CONCAT:
            case TK_DOTS:   case TK_EQ:    case TK_GE:   case TK_LE:
            case TK_NE:     case TK_SHL:   case TK_SHR:  case TK_DBCOLON:
            case TK_EOS:
                str = luaX_tokens[t - FIRST_RESERVED];
                break;

            case TK_INT:
                i += snprintf(buffer+i, BUFSIZ-i, "%d", (int)seminfo->i);
                buffer[i] = '\0';
                break;

            case TK_FLT:
                i += snprintf(buffer+i, BUFSIZ-i, "%f", (float)seminfo->r);
                buffer[i] = '\0';
                break;

            case TK_NAME:
            case TK_STRING:
            default:
                str = getstr(seminfo->ts);
                break;
        }

        /* 
         * TODO: code 'quote' form will take care of string ambiguity here so
         * that we will have a single representation for strings that should be
         * code.
         */
        if (t == TK_STRING) {
            buffer[i] = '[';
            buffer[i+1] = '[';
            buffer[i+2] = '\0';
            i += 2;
        }

        /* TODO: COMPILER
         * I think there's a weird compiler error with gcc 4.9.2. If I
         * take out the brackets after the if statement then the program will
         * segfault when parsing macro functions with numbers. Stepping through
         * gdb shows no if statement and directly doing buff_append on a NULL
         * string. Same with the following if clause:
         *  if (!(t == TK_INT || t == TK_FLT)) { }
         */
        if (str != NULL) {
            buff_append(str);
        }

        if (t == TK_STRING) {
            buffer[i] = ']';
            buffer[i+1] = ']';
            buffer[i+2] = '\0';
            i += 2;
        }

        buffer[i] = ls->current;
        buffer[i + 1] = '\0';
        i++;

        if (lmacro_tokenhasend(t))
            ends++;
        if (t == TK_END)
            ends--;
        if (t == '(' || ls->current == '(')
            parens++;
        if (t == ')' || ls->current == ')')
            parens--;
    }

    if (!(currIsNewline(ls) || ls->current == ';'))
        lexerror(ls, "Expected end of macro definition", TK_MACRO);

    if (luaL_loadbufferx(ls->L, buffer, i, name, "text") != 0) {
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

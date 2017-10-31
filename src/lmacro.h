#ifndef lmacro_h
#define lmacro_h

#include <stdlib.h>
#include <stdio.h>

#define MACROTABLE "__macro"

static int llex (LexState *ls, SemInfo *seminfo);
static int lmacro_llex (LexState *ls, SemInfo *seminfo);
static void next (LexState *ls);
static void inclinenumber (LexState *ls);
static void esccheck (LexState *ls, int c, const char *msg);
static int skip_sep (LexState *ls);
extern int luaL_loadbufferx (lua_State *, const char *, size_t,
                             const char *, const char *);

#define skipwhitespace(ls) \
    while (lisspace(ls->current)) { \
        if (currIsNewline(ls)) \
            inclinenumber(ls); \
        next(ls); \
    }

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
    if (lua_isstring(L, -1))
        ret = MATCH_SUCCESS_SMP;
    if (lua_isfunction(L, -1))
        ret = MATCH_SUCCESS_FUN;
    if (lua_istable(L, -1))
        ret = MATCH_PARTIAL;
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
 * Get the string that replaces a macro string from a function on top of the
 * stack and place that string into the macro buffer.
 */
static char
lmacro_replacefunction (LexState *ls)
{
    const char *replacement = NULL;
    /* in recursive next calls, if lexerror occurs, who frees this? */
    char *argstr = calloc(BUFSIZ, sizeof(char));
    int i, args = 0;
    char c;

    /* 
     * The current LexState's buffer can now be dismissed for this
     * macro's replacement to allow us to call `next' to find other
     * macro replacements as arguments to this macro function.
     */
    ls->macro.idx = 0;
    ls->macro.buff[0] = '\0';
    ls->macro.has_buff = 0;

    if (!argstr)
        lexerror(ls, "Not enough memory for macro form", 0);

    next(ls);
    c = ls->current;
    if (c != '(') {
        free(argstr);
        lexerror(ls, "Expected '(' to start argument list", c);
    }

    next(ls);
    for (i = 0 ;; i++, next(ls)) {
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
        replacement = lua_tostring(ls->L, -1);
        lexerror(ls, replacement, c);
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
            c = lmacro_replacefunction(ls);
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
    char c = '\0';

    if (ls->macro.has_replace || ls->macro.has_buff) {
        c = lmacro_next(ls);
        if (ls->macro.has_replace && c != '\0')
            goto setchar;
    }

    if (!(ls->macro.has_replace || ls->macro.has_buff))
        c = zgetc(ls->z);

    if (ls->in_comment)
        goto setchar;

    lmacro_lua_getmacrotable(ls->L);

    switch (lmacro_match(ls->L, c)) {
        case MATCH_SUCCESS_FUN:
            c = lmacro_replacefunction(ls);
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
    int len = strlen(name) - 1;
    int val = lua_gettop(L) - 1;

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
        lexerror(ls, "Expected macro definition after name", TK_NAME);
    def = getstr(seminfo->ts);

    lua_pushstring(ls->L, def);
    lmacro_lua_getmacrotable(ls->L);
    lmacro_lua_setmacro(ls->L, name);

    return next_lmacro(ls, seminfo);
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
lmacro_functionform (const char *name, LexState *ls, SemInfo *seminfo)
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
        t = next_llex(ls, seminfo);

        if (t == TK_EOS)
            lexerror(ls, "Unexpected end of input in macro form", TK_EOS);

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

    if (luaL_loadbufferx(ls->L, buffer, i, name, "text") != 0) {
        const char *err = lua_tostring(ls->L, -1);
        lexerror(ls, err, TK_MACRO);
    }

    if (lua_pcall(ls->L, 0, 1, 0) != 0) {
        const char *err = lua_tostring(ls->L, -1);
        lexerror(ls, err, TK_MACRO);
    }

    lmacro_lua_getmacrotable(ls->L);
    lmacro_lua_setmacro(ls->L, name);
    return next_lmacro(ls, seminfo);
}

/*
 * Parse a macro form.
 * Simple macro: macro <name> <string>
 * Function macro: macro <name> ([args|,]+) [<expr>]+ end
 */
static int
lmacro_define (LexState *ls, SemInfo *seminfo)
{
    char name[BUFSIZ] = {'\0'};
    int i = 0;

#define nameaddchar(c) \
    if (i + 1 >= BUFSIZ) \
        lexerror(ls, "Macro name overflows buffer", TK_MACRO); \
    name[i++] = c;

    if (ls->current != ' ')
        lexerror(ls, "Expected macro name", TK_MACRO);
    next(ls);
    while (ls->current != ' ') {
        if (ls->current == '[' || ls->current == ']') {
            int t = ls->current;
            int s = skip_sep(ls);
            luaZ_resetbuffer(ls->buff);
            if (s >= 0)
                lexerror(ls, "Macro name has long-string brackets", TK_MACRO);
            else
                nameaddchar(t);
        }

        if (ls->current == EOZ)
            lexerror(ls, "Unexpected end of file during macro name", TK_MACRO);

        nameaddchar(ls->current);
        next(ls);
    }

    skipwhitespace(ls);

    if (ls->current != '(')
        return lmacro_simpleform(name, ls, seminfo);
    else
        return lmacro_functionform(name, ls, seminfo);
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

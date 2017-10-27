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
        if (i < len)
            lua_newtable(L);
        else
            lua_pushvalue(L, val);
        lua_setfield(L, -2, key);
        lua_getfield(L, -1, key);
        /* move getfield value down and pop previous table */
        lua_insert(L, -2);
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
    buffer[i] = '\0';

    static const char *ret_form = "return function (";
    char buffer[BUFSIZ] = {0};
    const char *str;
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

            /* numbers, names, and strings */
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

        buff_append(str);

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
    const char *name = NULL;
    int t = next_llex(ls, seminfo);

    if (t != TK_NAME)
        lexerror(ls, "Expected macro name in macro form", TK_MACRO);
    name = getstr(seminfo->ts);

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

#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <glob.h>
#include "../src/lua.h"
#include "../src/lauxlib.h"
#include "../src/lualib.h"

static lua_State *L = NULL;

void
cleanup ()
{
    if (L)
        lua_close(L);
}

void
new_env ()
{
    cleanup();
    L = luaL_newstate();
    if (!L) {
        fprintf(stderr, "No memory for new Lua env");
        exit(1);
    }
    luaL_openlibs(L);
}

void
LOAD_OK (const char *file)
{
    new_env();
    if (luaL_loadfile(L, file) || lua_pcall(L, 0, 0, 0)) {
        const char *err = lua_tostring(L, -1);
        fprintf(stderr, "%s\n", err);
        exit(1);
    }
    printf("%s passed.\n", file);
}

void
LOAD_ERR (const char *file)
{
    new_env();
    if (!(luaL_loadfile(L, file) || lua_pcall(L, 0, 0, 0))) {
        fprintf(stderr, "%s loaded ok, expected failure!\n", file);
        exit(1);
    }
    printf("%s passed.\n", file);
}

void
test (const char *dirpath, void (*func) (const char*))
{
    glob_t dir;
    if (glob(dirpath, 0, NULL, &dir)) {
        fprintf(stderr, "Globbing failed: %s\n", dirpath);
        exit(1);
    }
    for (int i = 0; i < dir.gl_pathc; i++)
        func(dir.gl_pathv[i]);
}

int
main (int argc, char **argv)
{
    atexit(cleanup);
    test("tests/load_ok/*.lua", LOAD_OK);
    test("tests/load_err/*.lua", LOAD_ERR);
    return 0;
}

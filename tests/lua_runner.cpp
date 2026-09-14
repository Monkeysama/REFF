#include <lua.hpp>
#include <cstdio>

// 使用 REFramework 同源 Lua 运行测试脚本；这个工具不加载游戏或原生插件。
int main(int argc, char** argv) {
    if (argc < 2) return 2;
    lua_State* state = luaL_newstate();
    if (!state) return 3;
    luaL_openlibs(state); lua_newtable(state);
    for (int i = 1; i < argc; ++i) { lua_pushstring(state, argv[i]); lua_rawseti(state, -2, i - 1); }
    lua_setglobal(state, "arg");
    int result = luaL_dofile(state, argv[1]);
    if (result) std::fprintf(stderr, "%s\n", lua_tostring(state, -1));
    lua_close(state); return result ? 1 : 0;
}

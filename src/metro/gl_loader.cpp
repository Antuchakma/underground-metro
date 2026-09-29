#include "gl_loader.h"
#include <cstdio>

#define METRO_DEFINE(type, name) type name = nullptr;
METRO_GL_FUNCS(METRO_DEFINE)
#undef METRO_DEFINE

bool loadGL() {
    bool ok = true;
#define METRO_LOAD(type, name)                                          \
    name = (type)glfwGetProcAddress(#name);                             \
    if (!name) { std::fprintf(stderr, "Missing GL function: %s\n", #name); ok = false; }
    METRO_GL_FUNCS(METRO_LOAD)
#undef METRO_LOAD
    return ok;
}

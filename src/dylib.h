#pragma once

#ifdef _WIN32
    #include <windows.h>
    #define LIBRARY_TYPE HMODULE
    #define LOAD_LIBRARY(name) LoadLibrary(name)
    #define GET_FUNCTION(lib, name) (void *)GetProcAddress(lib, name)
    #define CLOSE_LIBRARY(lib) FreeLibrary(lib)
    #define LIBRARY_ERROR() "[ unknown error ]"
#else
    #include <dlfcn.h>
    #define LIBRARY_TYPE void*
    #define LOAD_LIBRARY(name) dlopen(name, RTLD_LAZY)
    #define GET_FUNCTION(lib, name) dlsym(lib, name)
    #define CLOSE_LIBRARY(lib) dlclose(lib)
    #define LIBRARY_ERROR() dlerror()
#endif

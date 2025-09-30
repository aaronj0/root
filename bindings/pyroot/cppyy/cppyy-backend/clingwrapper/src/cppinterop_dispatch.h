#ifndef CPPINTEROP_DISPATCH_H
#define CPPINTEROP_DISPATCH_H

#include <dlfcn.h>
#include <mutex>
#include <iostream>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "CppInterOp/CppInterOpDispatch.h"


static inline void* dlGetProcAddress(const char* name, const char* libPath = "/home/ajomy/ROOT/root_build/lib/libCling.so")
{
    if (!name) return nullptr;

    static std::once_flag loaded;
    static void* handle = nullptr;
    static void* (*getCppProcAddress)(const char*) = nullptr;
    static bool initialization_successful = false;
    
    // Initialize the library once
    std::call_once(loaded, [libPath]() {
        handle = dlopen(libPath, RTLD_LOCAL | RTLD_NOW);
        if (!handle) {
            std::cout << "Failed to open library: " << dlerror() << std::endl;
            return;
        }
        
        getCppProcAddress = reinterpret_cast<void*(*)(const char*)>(dlsym(handle, "CppGetProcAddress"));
        if (!getCppProcAddress) {
            std::cout << "Failed to find CppGetProcAddress: " << dlerror() << std::endl;
            dlclose(handle);
            handle = nullptr;
            return;
        }
        
        initialization_successful = true;
    });
    if (!initialization_successful || !getCppProcAddress) {
        return nullptr;
    }
    // std::cout<<"Getting address for function: "<<name<<"\n"<<getCppProcAddress(name)<<"\n";
    return getCppProcAddress(name);
}

namespace CppDispatch {
    FOR_EACH_CPP_FUNCTION(EXTERN_CPP_FUNC);

static inline int init_functions() {
    FOR_EACH_CPP_FUNCTION(LOAD_CPP_FUNCTION);
    return 0;
}
}
#endif // CPPINTEROP_DISPATCH_H

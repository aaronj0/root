#include "cppinterop_dispatch.h" 


#define DECLARE_CPP_NULL(func_name) \
    CppAPIType::func_name func_name = nullptr;

    // Worker macros for different operations
#define DECLARE_EXTERN_FUNC(func_name, ...) \
    extern CppAPIType::func_name func_name;

#define LOAD_FUNCTION(func_name, ...) \
    func_name = reinterpret_cast<CppAPIType::func_name>(dlGetProcAddress(#func_name));


namespace CppDispatch {
    FOR_EACH_CPP_FUNCTION(DECLARE_CPP_NULL);
}

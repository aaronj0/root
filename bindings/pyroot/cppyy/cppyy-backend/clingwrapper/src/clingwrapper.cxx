#ifndef _WIN32
#ifndef _CRT_SECURE_NO_WARNINGS
// silence warnings about getenv, strncpy, etc.
#define _CRT_SECURE_NO_WARNINGS
#endif
#endif

// Bindings
#include "precommondefs.h"
#include "cpp_cppyy.h"
#include "callcontext.h"

// ROOT
#include "TBaseClass.h"
#include "TClass.h"
#include "TClassRef.h"
#include "TClassTable.h"
#include "TClassEdit.h"
#include "TCollection.h"
#include "TDataMember.h"
#include "TDataType.h"
#include "TEnum.h"
#include "TEnumConstant.h"
#include "TEnv.h"
#include "TError.h"
#include "TException.h"
#include "TFunction.h"
#include "TFunctionTemplate.h"
#include "TGlobal.h"
#include "THashList.h"
#include "TInterpreter.h"
#include "TList.h"
#include "TListOfDataMembers.h"
#include "TListOfEnums.h"
#include "TMethod.h"
#include "TMethodArg.h"
#include "TROOT.h"
#include "TSystem.h"
#include "TThread.h"

#include <dlfcn.h>
// #endif

//
// Symbol exposing facility

// Standard
#include <assert.h>
#include <algorithm>     // for std::count, std::remove
#include <stdexcept>
#include <map>
#include <new>
#include <regex>
#include <set>
#include <sstream>
#include <signal.h>
#include <stdlib.h>      // for getenv
#include <string.h>
#include <typeinfo>

#if defined(__arm64__)
#include <exception>
#include <setjmp.h>
#define CLING_CATCH_UNCAUGHT_                                                \
ARMUncaughtException guard;                                                  \
if (setjmp(gExcJumBuf) == 0) {
#define _CLING_CATCH_UNCAUGHT                                                \
} else {                                                                     \
    if (!std::getenv("CPPYY_UNCAUGHT_QUIET"))                                     \
        std::cerr << "Warning: uncaught exception in JIT is rethrown; resources may leak" \
                  << " (suppress with \"CPPYY_UNCAUGHT_QUIET=1\")" << std::endl;\
    std::rethrow_exception(std::current_exception());                        \
}
#else
#define CLING_CATCH_UNCAUGHT_
#define _CLING_CATCH_UNCAUGHT
#endif

#if 0
// force std::string and allocator instantation, otherwise Clang 13+ fails to JIT
// symbols that rely on some private helpers (e.g. _M_use_local_data) when used in
// in conjunction with the PCH; hat tip:
//  https://github.com/sxs-collaboration/spectre/pull/5222/files#diff-093aadf224e5fee0d33ae1810f2f1c23304fb5ca398ba6b96c4e7918e0811729
#if defined(__GLIBCXX__) && __GLIBCXX__ >= 20220506
template class std::allocator<char>;
template class std::basic_string<char>;
template class std::basic_string<wchar_t>;
#endif

using namespace CppyyLegacy;
#endif

// temp
#include <iostream>
typedef CPyCppyy::Parameter Parameter;
// --temp

#if 0
#if defined(__arm64__)
namespace {

// Trap uncaught exceptions and longjump back to the point of JIT wrapper entry
jmp_buf gExcJumBuf;

void arm_uncaught_exception() {
    longjmp(gExcJumBuf, 1);
}

class ARMUncaughtException {
    std::terminate_handler m_Handler;
public:
    ARMUncaughtException() { m_Handler = std::set_terminate(arm_uncaught_exception); }
    ~ARMUncaughtException() { std::set_terminate(m_Handler); }
};

} // unnamed namespace
#endif // __arm64__
#endif

// small number that allows use of stack for argument passing
// const int SMALL_ARGS_N = 8;

// convention to pass flag for direct calls (similar to Python's vector calls)
// #define DIRECT_CALL ((size_t)1 << (8 * sizeof(size_t) - 1))
// static inline size_t CALL_NARGS(size_t nargs) {
//     return nargs & ~DIRECT_CALL;
// }

// data for life time management ---------------------------------------------
// typedef std::vector<TClassRef> ClassRefs_t;
// static ClassRefs_t g_classrefs(1);
// static const ClassRefs_t::size_type GLOBAL_HANDLE = 1;
// static const ClassRefs_t::size_type STD_HANDLE = GLOBAL_HANDLE + 1;

// typedef std::map<std::string, ClassRefs_t::size_type> Name2ClassRefIndex_t;
// static Name2ClassRefIndex_t g_name2classrefidx;

// namespace {

// static inline
// Cppyy::TCppType_t find_memoized(const std::string& name)
// {
//     auto icr = g_name2classrefidx.find(name);
//     if (icr != g_name2classrefidx.end())
//         return (Cppyy::TCppType_t)icr->second;
//     return (Cppyy::TCppType_t)0;
// }
//
// } // namespace
//
// static inline
// CallWrapper* new_CallWrapper(CppyyLegacy::TFunction* f)
// {
//     CallWrapper* wrap = new CallWrapper(f);
//     gWrapperHolder.push_back(wrap);
//     return wrap;
// }
//

// typedef std::vector<TGlobal*> GlobalVars_t;
// typedef std::map<TGlobal*, GlobalVars_t::size_type> GlobalVarsIndices_t;

// static GlobalVars_t g_globalvars;
// static GlobalVarsIndices_t g_globalidx;

// using CppDispatch::Dispatch;
// builtin types
static std::set<std::string> g_builtins =
    {"bool", "char", "signed char", "unsigned char", "wchar_t", "short", "unsigned short",
     "int", "unsigned int", "long", "unsigned long", "long long", "unsigned long long",
     "float", "double", "long double", "void",      "allocator", "array", "basic_string", "complex", "initializer_list", "less", "list",
     "map", "pair", "set", "vector"};


// to filter out ROOT names
static std::set<std::string> gInitialNames;
static std::set<std::string> gRootSOs;

// configuration
static bool gEnableFastPath = true;


// global initialization -----------------------------------------------------
namespace {

const int kMAXSIGNALS = 16;

// names copied from TUnixSystem
#ifdef WIN32
const int SIGBUS   = 0;      // simple placeholders for ones that don't exist
const int SIGSYS   = 0;
const int SIGPIPE  = 0;
const int SIGQUIT  = 0;
const int SIGWINCH = 0;
const int SIGALRM  = 0;
const int SIGCHLD  = 0;
const int SIGURG   = 0;
const int SIGUSR1  = 0;
const int SIGUSR2  = 0;
#endif

static struct Signalmap_t {
   int               fCode;
   const char       *fSigName;
} gSignalMap[kMAXSIGNALS] = {       // the order of the signals should be identical
   { SIGBUS,   "bus error" }, // to the one in TSysEvtHandler.h
   { SIGSEGV,  "segmentation violation" },
   { SIGSYS,    "bad argument to system call" },
   { SIGPIPE,   "write on a pipe with no one to read it" },
   { SIGILL,    "illegal instruction" },
   { SIGABRT,   "abort" },
   { SIGQUIT,   "quit" },
   { SIGINT,    "interrupt" },
   { SIGWINCH,  "window size change" },
   { SIGALRM,   "alarm clock" },
   { SIGCHLD,   "death of a child" },
   { SIGURG,    "urgent data arrived on an I/O channel" },
   { SIGFPE,    "floating point exception" },
   { SIGTERM,   "termination signal" },
   { SIGUSR1,   "user-defined signal 1" },
   { SIGUSR2,   "user-defined signal 2" }
};

// static void inline do_trace(int sig) {
//     std::cerr << " *** Break *** " << (sig < kMAXSIGNALS ? gSignalMap[sig].fSigName : "") << std::endl;
//     gSystem->StackTrace();
// }

// class TExceptionHandlerImp : public TExceptionHandler {
// public:
//     virtual void HandleException(Int_t sig) {
//         if (TROOT::Initialized()) {
//             if (gException) {
//                 gInterpreter->RewindDictionary();
//                 gInterpreter->ClearFileBusy();
//             }
//
//             if (!getenv("CPPYY_CRASH_QUIET"))
//                 do_trace(sig);
//
//         // jump back, if catch point set
//             Throw(sig);
//         }
//
//         do_trace(sig);
//         gSystem->Exit(128 + sig);
//     }
// };

static inline
void push_tokens_from_string(char *s, std::vector <const char*> &tokens) {
    char *token = strtok(s, " ");

    while (token) {
        tokens.push_back(token);
        token = strtok(NULL, " ");
    }
}

static inline
bool is_integral(std::string& s)
{
    if (s == "false") { s = "0"; return true; }
    else if (s == "true") { s = "1"; return true; }
    return !s.empty() && std::find_if(s.begin(), 
        s.end(), [](unsigned char c) { return !std::isdigit(c); }) == s.end();
}

class ApplicationStarter {
  CppDispatch::TInterp_t Interp;
public:
    ApplicationStarter() {
        // Check if somebody already loaded CppInterOp and created an
        // interpreter for us.
        (void)gROOT;


        // 

        char *libcling = gSystem->DynamicPathName("libCling");
        void *gInterpreterLib = dlopen(libcling, RTLD_LAZY|RTLD_LOCAL);

        std::cout<<"INITIALIZING CPPYY BACKEND\n";
        std::cout<<"HANDLE\n"<<gInterpreterLib<<"\n";

        static void* (*gpa)(const char*) = nullptr;
        gpa = reinterpret_cast<void*(*)(const char*)>(dlsym(gInterpreterLib, "CppGetProcAddress"));


        // if( CppDispatch::GetInterpreter()) {
        //     std::cout<<"INTERPRETER ALREADY EXISTS\n";
        //       std::string code = R"(
        //     class TestNS {
        //     public:
        //         int add(int x, int y, int z) { return x + y; }
        //         int add(double x, double y) { return x + y; }
        //     };
        //     )";
        // CppDispatch::Declare(code.c_str(), false);
        // CppDispatch::TCppScope_t globalscope = CppDispatch::GetScope("TestNS", 0);
        // std::cout<<"gs: "<<globalscope<<"\n";
        // std::vector<CppDispatch::TCppFunction_t> methods;
        // CppDispatch::GetClassMethods(globalscope, methods);
        // for (auto m : methods) {
        //     std::cout<<"METHODS NAME: "<<CppDispatch::GetCompleteName(m)<<"\n";
        // }
        // std::cout<<"METHODS SIZE: "<<methods.size()<<"\n";

        CppDispatch::init_functions();
        std::cout<< "Interpreter:"<<(void*)CppDispatch::GetInterpreter()<<"\n";
        if (auto * existingInterp = CppDispatch::GetInterpreter()) {
            Interp = existingInterp;
        }
        else {
#ifdef __arm64__
#ifdef __APPLE__
            // If on apple silicon don't use -march=native
            std::vector<const char *> InterpArgs({"-std=c++17"});
#else
            std::vector<const char *> InterpArgs(
                {"-std=c++17", "-march=native"});
#endif
#else
            std::vector <const char *> InterpArgs({"-std=c++17", "-march=native"});
#endif
            char *InterpArgString = getenv("CPPINTEROP_EXTRA_INTERPRETER_ARGS");

            if (InterpArgString)
              push_tokens_from_string(InterpArgString, InterpArgs);

#ifdef __arm64__
#ifdef __APPLE__
            // If on apple silicon don't use -march=native
            Interp = CppDispatch::CreateInterpreter({"-std=c++17"});
#else
            Interp = CppDispatch::CreateInterpreter({"-std=c++17", "-march=native"});
#endif
#else
            Interp = CppDispatch::CreateInterpreter({"-std=c++17", "-march=native"}, {});
#endif
        }

        // fill out the builtins
        std::set<std::string> bi{g_builtins};
        for (const auto& name : bi) {
            for (const char* a : {"*", "&", "*&", "[]", "*[]"})
                g_builtins.insert(name+a);
        }

    // disable fast path if requested
        if (getenv("CPPYY_DISABLE_FASTPATH")) gEnableFastPath = false;

    // set opt level (default to 2 if not given; Cling itself defaults to 0)
        int optLevel = 2;

        if (getenv("CPPYY_OPT_LEVEL")) optLevel = atoi(getenv("CPPYY_OPT_LEVEL"));

        if (optLevel != 0) {
            std::ostringstream s;
            s << "#pragma cling optimize " << optLevel;
            CppDispatch::Process(s.str().c_str());
        }

        // This would give us something like:
        // /home/vvassilev/workspace/builds/scratch/cling-build/builddir/lib/clang/13.0.0
        const char * ResourceDir = CppDispatch::GetResourceDir();
        std::string ClingSrc = std::string(ResourceDir) + "/../../../../cling-src";
        std::string ClingBuildDir = std::string(ResourceDir) + "/../../../";
        CppDispatch::AddIncludePath((ClingSrc + "/tools/cling/include").c_str());
        CppDispatch::AddIncludePath((ClingSrc + "/include").c_str());
        CppDispatch::AddIncludePath((ClingBuildDir + "/include").c_str());
        CppDispatch::AddIncludePath("/home/ajomy/cppyy-interop-dev/CppInterOp/cppyy-backend/clingwrapper/src");
        CppDispatch::AddIncludePath("/home/ajomy/ROOT/root_src/interpreter/CppInterOp/include");
        CppDispatch::LoadLibrary("libstdc++", /* lookup= */ true);

        // load frequently used headers
        const char* code =
            "#include <algorithm>\n"
            "#include <numeric>\n"
            "#include <complex>\n"
            "#include <iostream>\n"
            "#include <string.h>\n" // for strcpy
            "#include <string>\n"
            //    "#include <DllImport.h>\n"     // defines R__EXTERN
            "#include <vector>\n"
            "#include <utility>\n"
            "#include <memory>\n"
            "#include <functional>\n" // for the dispatcher code to use
                                      // std::function
            "#include <map>\n"        // FIXME: Replace with modules
            "#include <sstream>\n"    // FIXME: Replace with modules
            "#include <array>\n"      // FIXME: Replace with modules
            "#include <list>\n"       // FIXME: Replace with modules
            "#include <deque>\n"      // FIXME: Replace with modules
            "#include <tuple>\n"      // FIXME: Replace with modules
            "#include <set>\n"        // FIXME: Replace with modules
            "#include <chrono>\n"     // FIXME: Replace with modules
            "#include <cmath>\n"      // FIXME: Replace with modules
            "#if __has_include(<optional>)\n"
            "#include <optional>\n"
            "#endif\n"
            "#include <cppinterop_dispatch.h>\n";
        CppDispatch::Process(code);

    // create helpers for comparing thingies
        CppDispatch::Declare(
            "namespace __cppyy_internal { template<class C1, class C2>"
            " bool is_equal(const C1& c1, const C2& c2) { return (bool)(c1 == c2); } }", false);
        CppDispatch::Declare(
            "namespace __cppyy_internal { template<class C1, class C2>"
            " bool is_not_equal(const C1& c1, const C2& c2) { return (bool)(c1 != c2); } }", false);

        // Define gCling when we run with clang-repl.
        // FIXME: We should get rid of all the uses of gCling as this seems to
        // break encapsulation.
        std::stringstream InterpPtrSS;
        InterpPtrSS << "#ifndef __CLING__\n"
                    << "namespace cling { namespace runtime {\n"
                    << "void* gCling=(void*)" << static_cast<void*>(Interp)
                    << ";\n }}\n"
                    << "#endif \n";
        CppDispatch::Process(InterpPtrSS.str().c_str());

    // helper for multiple inheritance
        CppDispatch::Declare("namespace __cppyy_internal { struct Sep; }", false);

    // start off with a reasonable size placeholder for wrappers
        // gWrapperHolder.reserve(1024);

    // create an exception handler to process signals
        // gExceptionHandler = new TExceptionHandlerImp{};
    }

    ~ApplicationStarter() {
      //CppDispatch::DeleteInterpreter(Interp);
        // for (auto wrap : gWrapperHolder)
        //     delete wrap;
        // delete gExceptionHandler; gExceptionHandler = nullptr;
    }
} _applicationStarter;

} // unnamed namespace


static inline
char* cppstring_to_cstring(const std::string& cppstr)
{
    char* cstr = (char*)malloc(cppstr.size()+1);
    memcpy(cstr, cppstr.c_str(), cppstr.size()+1);
    return cstr;
}

bool Cppyy::Compile(const std::string& code, bool silent)
{
    // Declare returns an enum which equals 0 on success
    return !CppDispatch::Declare(code.c_str(), silent);
}

std::string Cppyy::ToString(TCppType_t klass, TCppObject_t obj)
{
    if (klass && obj && !CppDispatch::IsNamespace((TCppScope_t)klass))
        return CppDispatch::ObjToString(CppDispatch::GetQualifiedCompleteName(klass).c_str(),
                                    (void*)obj);
    return "";
}

// // name to opaque C++ scope representation -----------------------------------
std::string Cppyy::ResolveName(const std::string& name) {
  if (!name.empty()) {
    if (Cppyy::TCppType_t type =
            Cppyy::GetType(name, /*enable_slow_lookup=*/true))
      return Cppyy::GetTypeAsString(Cppyy::ResolveType(type));
    return name;
  }
  return "";
}

Cppyy::TCppType_t Cppyy::ResolveEnumReferenceType(TCppType_t type) {
    if (!CppDispatch::IsLValueReferenceType(type))
        return type;

    TCppType_t nonReferenceType = CppDispatch::GetNonReferenceType(type);
    if (CppDispatch::IsEnumType(nonReferenceType)) {
        TCppType_t underlying_type =  CppDispatch::GetIntegerTypeFromEnumType(nonReferenceType);
        return CppDispatch::GetReferencedType(underlying_type, false);
    }
    return type;
}

Cppyy::TCppType_t Cppyy::ResolveEnumPointerType(TCppType_t type) {
    if (!CppDispatch::IsPointerType(type))
        return type;

    TCppType_t PointeeType = CppDispatch::GetPointeeType(type);
    if (CppDispatch::IsEnumType(PointeeType)) {
        TCppType_t underlying_type =  CppDispatch::GetIntegerTypeFromEnumType(PointeeType);
        return CppDispatch::GetPointerType(underlying_type);
    }
    return type;
}

Cppyy::TCppType_t int_like_type(Cppyy::TCppType_t type) {
    Cppyy::TCppType_t check_int_typedefs = type;
    if (CppDispatch::IsPointerType(check_int_typedefs))
        check_int_typedefs = CppDispatch::GetPointeeType(check_int_typedefs);
    if (CppDispatch::IsReferenceType(check_int_typedefs))
        check_int_typedefs = CppDispatch::GetReferencedType(check_int_typedefs, false);

    if (CppDispatch::GetTypeAsString(check_int_typedefs) == "int8_t" || CppDispatch::GetTypeAsString(check_int_typedefs) == "uint8_t")
        return check_int_typedefs;
    return nullptr;
}

Cppyy::TCppType_t Cppyy::ResolveType(TCppType_t type) {
    if (!type) return type;

    TCppType_t check_int_typedefs = int_like_type(type);
    if (check_int_typedefs)
        return type;

    Cppyy::TCppType_t canonType = CppDispatch::GetCanonicalType(type);

    if (CppDispatch::IsEnumType(canonType)) {
        if (Cppyy::GetTypeAsString(type) != "std::byte")
            return CppDispatch::GetIntegerTypeFromEnumType(canonType);
    }
    if (CppDispatch::HasTypeQualifier(canonType, CppDispatch::QualKind::Restrict)) {
        return CppDispatch::RemoveTypeQualifier(canonType, CppDispatch::QualKind::Restrict);
    }

    return canonType;
}

Cppyy::TCppType_t Cppyy::GetRealType(TCppType_t type) {
    TCppType_t check_int_typedefs = int_like_type(type);
    if (check_int_typedefs)
        return check_int_typedefs;
    return CppDispatch::GetUnderlyingType(type);
}

Cppyy::TCppType_t Cppyy::GetPointerType(TCppType_t type) {
  return CppDispatch::GetPointerType(type);
}

Cppyy::TCppType_t Cppyy::GetReferencedType(TCppType_t type, bool rvalue) {
  return CppDispatch::GetReferencedType(type, rvalue);
}

bool Cppyy::IsClassType(TCppType_t type) {
    return CppDispatch::IsRecordType(type);
}

bool Cppyy::IsPointerType(TCppType_t type) {
    return CppDispatch::IsPointerType(type);
}

bool Cppyy::IsFunctionPointerType(TCppType_t type) {
    return CppDispatch::IsFunctionPointerType(type);
}

std::string trim(const std::string& line)
{
    if (line.empty()) return "";
    const char* WhiteSpace = " \t\v\r\n";
    std::size_t start = line.find_first_not_of(WhiteSpace);
    std::size_t end = line.find_last_not_of(WhiteSpace);
    return line.substr(start, end - start + 1);
}

// returns false of angular brackets dont match, else true
bool split_comma_saparated_types(const std::string& name,
                                 std::vector<std::string>& types) {
  std::string trimed_name = trim(name);
  size_t start_pos = 0;
  size_t end_pos = 0;
  size_t appended_count = 0;
  int matching_angular_brackets = 0;
  while (end_pos < trimed_name.size()) {
    switch (trimed_name[end_pos]) {
    case ',': {
      if (!matching_angular_brackets) {
        types.push_back(
            trim(trimed_name.substr(start_pos, end_pos - start_pos)));
        start_pos = end_pos + 1;
      }
      break;
    }
    case '<': {
      matching_angular_brackets++;
      break;
    }
    case '>': {
      if (matching_angular_brackets > 0) {
        types.push_back(
            trim(trimed_name.substr(start_pos, end_pos - start_pos + 1)));
        start_pos = end_pos + 1;
      } else if (matching_angular_brackets < 1) {
        types.clear();
        return false;
      }
      start_pos++;
      end_pos++;
      matching_angular_brackets--;
      break;
    }
    }
    end_pos++;
  }
  if (start_pos < trimed_name.size())
    types.push_back(trim(trimed_name.substr(start_pos, end_pos - start_pos)));
  return true;
}

// returns true if no new type was added.
bool Cppyy::AppendTypesSlow(const std::string& name,
                            std::vector<CppDispatch::TemplateArgInfo>& types, Cppyy::TCppScope_t parent) {

  // Add no new type if string is empty
  if (name.empty())
    return true;

  auto replace_all = [](std::string& str, const std::string& from, const std::string& to) {
      if(from.empty())
        return;
      size_t start_pos = 0;
      while((start_pos = str.find(from, start_pos)) != std::string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length();
    }
  };

  std::string resolved_name = name;
  replace_all(resolved_name, "std::initializer_list<", "std::vector<"); // replace initializer_list with vector

  // We might have an entire expression such as int, double.
  static unsigned long long struct_count = 0;
  std::string code = "template<typename ...T> struct __Cppyy_AppendTypesSlow {};\n";
  if (!struct_count)
    CppDispatch::Declare(code.c_str(), false); // initialize the trampoline

  std::string var = "__Cppyy_s" + std::to_string(struct_count++);
  // FIXME: We cannot use silent because it erases our error code from Declare!
  if (!CppDispatch::Declare(("__Cppyy_AppendTypesSlow<" + resolved_name + "> " + var +";\n").c_str(), /*silent=*/false)) {
    TCppType_t varN = CppDispatch::GetVariableType(CppDispatch::GetNamed(var.c_str(), nullptr));
    TCppScope_t instance_class = CppDispatch::GetScopeFromType(varN);
    size_t oldSize = types.size();
    CppDispatch::GetClassTemplateInstantiationArgs(instance_class, types);
    return oldSize == types.size();
  }

  // We split each individual types based on , and resolve it
  // FIXME: see discussion on should we support template instantiation with string:
  //   https://github.com/compiler-research/cppyy-backend/pull/137#discussion_r2079357491
  //   We should consider eliminating the `split_comma_saparated_types` and `is_integral`
  //   string parsing.
  std::vector<std::string> individual_types;
  if (!split_comma_saparated_types(resolved_name, individual_types))
    return true;

  for (std::string& i : individual_types) {
    // Try going via Cppyy::GetType first.
    const char* integral_value = nullptr;
    Cppyy::TCppType_t type = nullptr;

    type = GetType(i, /*enable_slow_lookup=*/true);
    if (!type && parent && (CppDispatch::IsNamespace(parent) || CppDispatch::IsClass(parent))) {
        type = Cppyy::GetTypeFromScope(Cppyy::GetNamed(resolved_name, parent));
    }

    if (!type) {
      types.clear();
      return true;
    }

    if (is_integral(i))
        integral_value = strdup(i.c_str());
    types.emplace_back(type, integral_value);
  }
  return false;
}

Cppyy::TCppType_t Cppyy::GetType(const std::string &name, bool enable_slow_lookup /* = false */) {
    static unsigned long long var_count = 0;

    if (auto type = CppDispatch::GetType(name))
        return type;

    if (!enable_slow_lookup) {
        if (name.find("::") != std::string::npos)
            throw std::runtime_error("Calling Cppyy::GetType with qualified name '"
                                + name + "'\n");
        return nullptr;
    }

    // Here we might need to deal with integral types such as 3.14.

    std::string id = "__Cppyy_GetType_" + std::to_string(var_count++);
    std::string using_clause = "using " + id + " = __typeof__(" + name + ");\n";

    if (!CppDispatch::Declare(using_clause.c_str(), /*silent=*/false)) {
      TCppScope_t lookup = CppDispatch::GetNamed(id, 0);
      TCppType_t lookup_ty = CppDispatch::GetTypeFromScope(lookup);
      return CppDispatch::GetCanonicalType(lookup_ty);
    }
    return nullptr;
}


Cppyy::TCppType_t Cppyy::GetComplexType(const std::string &name) {
    return CppDispatch::GetComplexType(CppDispatch::GetType(name));
}

std::string Cppyy::ResolveEnum(TCppScope_t handle)
{
    std::string type = CppDispatch::GetTypeAsString(
        CppDispatch::GetIntegerTypeFromEnumScope(handle));
    if (type == "signed char")
        return "char";
    return type;
}

Cppyy::TCppScope_t Cppyy::GetUnderlyingScope(TCppScope_t scope)
{
    return CppDispatch::GetUnderlyingScope(scope);
}

Cppyy::TCppScope_t Cppyy::GetScope(const std::string& name,
                                   TCppScope_t parent_scope)
{
    if (Cppyy::TCppScope_t scope = CppDispatch::GetScope(name, parent_scope))
      return scope;
    if (!parent_scope || parent_scope == CppDispatch::GetGlobalScope())
      if (Cppyy::TCppScope_t scope = CppDispatch::GetScopeFromCompleteName(name))
        return scope;

    // FIXME: avoid string parsing here
    if (name.find('<') != std::string::npos) {
      // Templated Type; May need instantiation
      size_t start = name.find('<');
      size_t end = name.rfind('>');
      std::string params = name.substr(start + 1, end - start - 1);

      std::string pure_name = name.substr(0, start);
      Cppyy::TCppScope_t scope = CppDispatch::GetScope(pure_name, parent_scope);
      if (!scope && (!parent_scope || parent_scope == CppDispatch::GetGlobalScope()))
        scope = CppDispatch::GetScopeFromCompleteName(pure_name);

      if (Cppyy::IsTemplate(scope)) {
        std::vector<CppDispatch::TemplateArgInfo> templ_params;
        if (!Cppyy::AppendTypesSlow(params, templ_params))
          return CppDispatch::InstantiateTemplate(scope, templ_params.data(),
                                          templ_params.size(), false);
      }
    }
    return nullptr;
}

Cppyy::TCppScope_t Cppyy::GetFullScope(const std::string& name)
{
  return Cppyy::GetScope(name);
}

Cppyy::TCppScope_t Cppyy::GetTypeScope(TCppScope_t var)
{
    return CppDispatch::GetScopeFromType(
        CppDispatch::GetVariableType(var));
}

Cppyy::TCppScope_t Cppyy::GetNamed(const std::string& name,
                                   TCppScope_t parent_scope)
{
    return CppDispatch::GetNamed(name, parent_scope);
}

Cppyy::TCppScope_t Cppyy::GetParentScope(TCppScope_t scope)
{
    return CppDispatch::GetParentScope(scope);
}

Cppyy::TCppScope_t Cppyy::GetScopeFromType(TCppType_t type)
{
    return CppDispatch::GetScopeFromType(type);
}

Cppyy::TCppType_t Cppyy::GetTypeFromScope(TCppScope_t klass)
{
    return CppDispatch::GetTypeFromScope(klass);
}

Cppyy::TCppScope_t Cppyy::GetGlobalScope()
{
    return CppDispatch::GetGlobalScope();
}

bool Cppyy::IsTemplate(TCppScope_t handle)
{
    return CppDispatch::IsTemplate(handle);
}

bool Cppyy::IsTemplateInstantiation(TCppScope_t handle)
{
    return CppDispatch::IsTemplateSpecialization(handle);
}

bool Cppyy::IsTypedefed(TCppScope_t handle)
{
    return CppDispatch::IsTypedefed(handle);
}

namespace {
class AutoCastRTTI {
public:
  virtual ~AutoCastRTTI() {}
};
} // namespace

Cppyy::TCppScope_t Cppyy::GetActualClass(TCppScope_t klass, TCppObject_t obj) {
    if (!CppDispatch::IsClassPolymorphic(klass))
        return klass;

    const std::type_info *typ = &typeid(*(AutoCastRTTI *)obj);

    std::string mangled_name = typ->name();
    std::string demangled_name = CppDispatch::Demangle(mangled_name);

    if (TCppScope_t scope = Cppyy::GetScope(demangled_name))
        return scope;

    return klass;
}

size_t Cppyy::SizeOf(TCppScope_t klass)
{
    return CppDispatch::SizeOf(klass);
}

size_t Cppyy::SizeOfType(TCppType_t klass)
{
    return CppDispatch::GetSizeOfType(klass);
}

bool Cppyy::IsBuiltin(const std::string& type_name)
{
    static std::set<std::string> s_builtins =
       {"bool", "char", "signed char", "unsigned char", "wchar_t", "short",
        "unsigned short", "int", "unsigned int", "long", "unsigned long",
        "long long", "unsigned long long", "float", "double", "long double",
        "void"};
     if (s_builtins.find(trim(type_name)) != s_builtins.end())
         return true;

    if (strstr(type_name.c_str(), "std::complex"))
        return true;

    return false;
}

bool Cppyy::IsBuiltin(TCppType_t type)
{
    return  CppDispatch::IsBuiltin(type);
    
}

bool Cppyy::IsComplete(TCppScope_t scope)
{
    return CppDispatch::IsComplete(scope);
}

// // memory management ---------------------------------------------------------
Cppyy::TCppObject_t Cppyy::Allocate(TCppScope_t scope)
{
    return CppDispatch::Allocate(scope, 1UL);
}

void Cppyy::Deallocate(TCppScope_t scope, TCppObject_t instance)
{
    CppDispatch::Deallocate(scope, instance, 1UL);
}

Cppyy::TCppObject_t Cppyy::Construct(TCppScope_t scope, void* arena/*=nullptr*/)
{
    return CppDispatch::Construct(scope, arena, 1UL);
}

void Cppyy::Destruct(TCppScope_t scope, TCppObject_t instance)
{
    CppDispatch::Destruct(instance, scope, true, 0UL);
}

static inline
bool copy_args(Parameter* args, size_t nargs, void** vargs)
{
    bool runRelease = false;
    for (size_t i = 0; i < nargs; ++i) {
        switch (args[i].fTypeCode) {
        case 'X':       /* (void*)type& with free */
            runRelease = true;
        case 'V':       /* (void*)type& */
            vargs[i] = args[i].fValue.fVoidp;
            break;
        case 'r':       /* const type& */
            vargs[i] = args[i].fRef;
            break;
        default:        /* all other types in union */
            vargs[i] = (void*)&args[i].fValue.fVoidp;
            break;
        }
    }
    return runRelease;
}

static inline
void release_args(Parameter* args, size_t nargs) {
    for (size_t i = 0; i < nargs; ++i) {
        if (args[i].fTypeCode == 'X')
            free(args[i].fValue.fVoidp);
    }
}

static inline
bool WrapperCall(Cppyy::TCppMethod_t method, size_t nargs, void* args_, void* self, void* result)
{
    Parameter* args = (Parameter*)args_;
    bool is_direct = nargs & DIRECT_CALL;
    nargs = CALL_NARGS(nargs);

    // if (!is_ready(wrap, is_direct))
    //     return false;        // happens with compilation error

    if (CppDispatch::JitCall JC = CppDispatch::MakeFunctionCallable(method)) {
        bool runRelease = false;
        //const auto& fgen = /* is_direct ? faceptr.fDirect : */ faceptr;
        if (nargs <= SMALL_ARGS_N) {
            void* smallbuf[SMALL_ARGS_N];
            if (nargs) runRelease = copy_args(args, nargs, smallbuf);
            // CLING_CATCH_UNCAUGHT_
            JC.Invoke(result, {smallbuf, nargs}, self);
            // _CLING_CATCH_UNCAUGHT
        } else {
            std::vector<void*> buf(nargs);
            runRelease = copy_args(args, nargs, buf.data());
            // CLING_CATCH_UNCAUGHT_
            JC.Invoke(result, {buf.data(), nargs}, self);
            // _CLING_CATCH_UNCAUGHT
        }
        if (runRelease) release_args(args, nargs);
        return true;
    }

    return false;
}

template<typename T>
static inline
T CallT(Cppyy::TCppMethod_t method, Cppyy::TCppObject_t self, size_t nargs, void* args)
{
    T t{};
    if (WrapperCall(method, nargs, args, (void*)self, &t))
        return t;
    throw std::runtime_error("failed to resolve function");
    return (T)-1;
}

#ifdef PRINT_DEBUG
    #define _IMP_CALL_PRINT_STMT(type)                                       \
        printf("IMP CALL with type: %s\n", #type);
#else
    #define _IMP_CALL_PRINT_STMT(type)
#endif

#define CPPYY_IMP_CALL(typecode, rtype)                                      \
rtype Cppyy::Call##typecode(TCppMethod_t method, TCppObject_t self, size_t nargs, void* args)\
{                                                                            \
    _IMP_CALL_PRINT_STMT(rtype)                                              \
    return CallT<rtype>(method, self, nargs, args);                          \
}

void Cppyy::CallV(TCppMethod_t method, TCppObject_t self, size_t nargs, void* args)
{
    if (!WrapperCall(method, nargs, args, (void*)self, nullptr))
        return /* TODO ... report error */;
}

CPPYY_IMP_CALL(B,  unsigned char)
CPPYY_IMP_CALL(C,  char         )
CPPYY_IMP_CALL(H,  short        )
CPPYY_IMP_CALL(I,  int          )
CPPYY_IMP_CALL(L,  long         )
CPPYY_IMP_CALL(LL, long long    )
CPPYY_IMP_CALL(F,  float        )
CPPYY_IMP_CALL(D,  double       )
CPPYY_IMP_CALL(LD, long double  )

void* Cppyy::CallR(TCppMethod_t method, TCppObject_t self, size_t nargs, void* args)
{
    void* r = nullptr;
    if (WrapperCall(method, nargs, args, (void*)self, &r))
        return r;
    return nullptr;
}

char* Cppyy::CallS(
    TCppMethod_t method, TCppObject_t self, size_t nargs, void* args, size_t* length)
{
    char* cstr = nullptr;
    // TClassRef cr("std::string"); // TODO: Why is this required?
    std::string* cppresult = (std::string*)malloc(sizeof(std::string));
    if (WrapperCall(method, nargs, args, self, (void*)cppresult)) {
        cstr = cppstring_to_cstring(*cppresult);
        *length = cppresult->size();
        cppresult->std::string::~basic_string();
    } else
        *length = 0;
    free((void*)cppresult);
    return cstr;
}

Cppyy::TCppObject_t Cppyy::CallConstructor(
    TCppMethod_t method, TCppScope_t klass, size_t nargs, void* args)
{
    void* obj = nullptr;
    WrapperCall(method, nargs, args, nullptr, &obj);
    return (TCppObject_t)obj;
}

void Cppyy::CallDestructor(TCppScope_t scope, TCppObject_t self)
{
    CppDispatch::Destruct(self, scope, /*withFree=*/false, 0UL);
}

Cppyy::TCppObject_t Cppyy::CallO(TCppMethod_t method,
    TCppObject_t self, size_t nargs, void* args, TCppType_t result_type)
{
    void* obj = ::operator new(CppDispatch::GetSizeOfType(result_type));
    if (WrapperCall(method, nargs, args, self, obj))
        return (TCppObject_t)obj;
    ::operator delete(obj);
    return (TCppObject_t)0;
}

Cppyy::TCppFuncAddr_t Cppyy::GetFunctionAddress(TCppMethod_t method, bool check_enabled)
{
    return (TCppFuncAddr_t) CppDispatch::GetFunctionAddress(method);
}


// handling of function argument buffer --------------------------------------
void* Cppyy::AllocateFunctionArgs(size_t nargs)
{
    return new Parameter[nargs];
}

void Cppyy::DeallocateFunctionArgs(void* args)
{
    delete [] (Parameter*)args;
}

size_t Cppyy::GetFunctionArgSizeof()
{
    return sizeof(Parameter);
}

size_t Cppyy::GetFunctionArgTypeoffset()
{
    return offsetof(Parameter, fTypeCode);
}


// scope reflection information ----------------------------------------------
bool Cppyy::IsNamespace(TCppScope_t scope)
{
    if (!scope)
      return false;

    // Test if this scope represents a namespace.
    return CppDispatch::IsNamespace(scope) || CppDispatch::GetGlobalScope() == scope;
}

bool Cppyy::IsClass(TCppScope_t scope)
{
    // Test if this scope represents a namespace.
    return CppDispatch::IsClass(scope);
}
//
bool Cppyy::IsAbstract(TCppScope_t scope)
{
    // Test if this type may not be instantiated.
    return CppDispatch::IsAbstract(scope);
}

bool Cppyy::IsEnumScope(TCppScope_t scope)
{
    return CppDispatch::IsEnumScope(scope);
}

bool Cppyy::IsEnumConstant(TCppScope_t scope)
{
  return CppDispatch::IsEnumConstant(CppDispatch::GetUnderlyingScope(scope));
}

bool Cppyy::IsEnumType(TCppType_t type)
{
    return CppDispatch::IsEnumType(type);
}

bool Cppyy::IsAggregate(TCppType_t type)
{
  // Test if this type is a "plain old data" type
  return CppDispatch::IsAggregate(type);
}

bool Cppyy::IsDefaultConstructable(TCppScope_t scope)
{
// Test if this type has a default constructor or is a "plain old data" type
    return CppDispatch::HasDefaultConstructor(scope);
}

bool Cppyy::IsVariable(TCppScope_t scope)
{
    return CppDispatch::IsVariable(scope);
}

void Cppyy::GetAllCppNames(TCppScope_t scope, std::set<std::string>& cppnames)
{
// Collect all known names of C++ entities under scope. This is useful for IDEs
// employing tab-completion, for example. Note that functions names need not be
// unique as they can be overloaded.
    CppDispatch::GetAllCppNames(scope, cppnames);
}
// // class reflection information ----------------------------------------------
std::vector<Cppyy::TCppScope_t> Cppyy::GetUsingNamespaces(TCppScope_t scope)
{
    return CppDispatch::GetUsingNamespaces(scope);
}
// // class reflection information ----------------------------------------------
std::string Cppyy::GetFinalName(TCppType_t klass)
{
  return CppDispatch::GetCompleteName(CppDispatch::GetUnderlyingScope(klass));
}

std::string Cppyy::GetScopedFinalName(TCppType_t klass)
{
    return CppDispatch::GetQualifiedCompleteName(klass);
}

bool Cppyy::HasVirtualDestructor(TCppScope_t scope)
{
    TCppMethod_t func = CppDispatch::GetDestructor(scope);
    return CppDispatch::IsVirtualMethod(func);
}

Cppyy::TCppIndex_t Cppyy::GetNumBases(TCppScope_t klass)
{
// Get the total number of base classes that this class has.
    return CppDispatch::GetNumBases(klass);
}

////////////////////////////////////////////////////////////////////////////////
/// \fn Cppyy::TCppIndex_t Cppyy::GetNumBasesLongestBranch(TCppScope_t klass)
/// \brief Retrieve number of base classes in the longest branch of the
///        inheritance tree of the input class.
/// \param[in] klass The class to start the retrieval process from.
///
/// This is a helper function for Cppyy::GetNumBasesLongestBranch.
/// Given an inheritance tree, the function assigns weight 1 to each class that
/// has at least one base. Starting from the input class, the function is
/// called recursively on all the bases. For each base the return value is one
/// (the weight of the base itself) plus the maximum value retrieved for their
/// bases in turn. For example, given the following inheritance tree:
///
/// ~~~{.cpp}
/// class A {}; class B: public A {};
/// class X {}; class Y: public X {}; class Z: public Y {};
/// class C: public B, Z {};
/// ~~~
///
/// calling this function on an instance of `C` will return 3, the steps
/// required to go from C to X.
Cppyy::TCppIndex_t Cppyy::GetNumBasesLongestBranch(TCppScope_t klass) {
    std::vector<size_t> num;
    for (TCppIndex_t ibase = 0; ibase < GetNumBases(klass); ++ibase)
        num.push_back(GetNumBasesLongestBranch(Cppyy::GetBaseScope(klass, ibase)));
    if (num.empty())
        return 0;
    return *std::max_element(num.begin(), num.end()) + 1;
}

std::string Cppyy::GetBaseName(TCppType_t klass, TCppIndex_t ibase)
{
    return CppDispatch::GetName(CppDispatch::GetBaseClass(klass, ibase));
}

Cppyy::TCppScope_t Cppyy::GetBaseScope(TCppScope_t klass, TCppIndex_t ibase)
{
    return CppDispatch::GetBaseClass(klass, ibase);
}

bool Cppyy::IsSubclass(TCppScope_t derived, TCppScope_t base)
{
    return CppDispatch::IsSubclass(derived, base);
}

static std::set<std::string> gSmartPtrTypes =
    {"std::auto_ptr", "std::shared_ptr", "std::unique_ptr", "std::weak_ptr"};

bool Cppyy::IsSmartPtr(TCppScope_t klass)
{
    const std::string& rn = Cppyy::GetScopedFinalName(klass);
    if (gSmartPtrTypes.find(rn.substr(0, rn.find("<"))) != gSmartPtrTypes.end())
        return true;
    return false;
}

bool Cppyy::GetSmartPtrInfo(
    const std::string& tname, TCppScope_t* raw, TCppMethod_t* deref)
{
    // TODO: We can directly accept scope instead of name
    const std::string& rn = ResolveName(tname);
    if (gSmartPtrTypes.find(rn.substr(0, rn.find("<"))) == gSmartPtrTypes.end())
        return false;

    if (!raw && !deref) return true;

    TCppScope_t scope = Cppyy::GetScope(rn);
    if (!scope)
        return false;

    std::vector<TCppMethod_t> ops;
    CppDispatch::GetOperator(scope, CppDispatch::Operator::OP_Arrow, ops, CppDispatch::OperatorArity::kBoth);
    if (ops.size() != 1)
        return false;

    if (deref) *deref = ops[0];
    if (raw) *raw = Cppyy::GetScopeFromType(CppDispatch::GetFunctionReturnType(ops[0]));
    return (!deref || *deref) && (!raw || *raw);
}

// type offsets --------------------------------------------------------------
ptrdiff_t Cppyy::GetBaseOffset(TCppScope_t derived, TCppScope_t base,
    TCppObject_t address, int direction, bool rerror)
{
    intptr_t offset = CppDispatch::GetBaseClassOffset(derived, base);
    if (offset == -1)   // Cling error, treat silently
        return rerror ? (ptrdiff_t)offset : 0;

    return (ptrdiff_t)(direction < 0 ? -offset : offset);
}


void Cppyy::GetClassMethods(TCppScope_t scope, std::vector<Cppyy::TCppMethod_t> &methods)
{
    CppDispatch::GetClassMethods(scope, methods);
}

std::vector<Cppyy::TCppScope_t> Cppyy::GetMethodsFromName(
    TCppScope_t scope, const std::string& name)
{
    return CppDispatch::GetFunctionsUsingName(scope, name);
}
std::string Cppyy::GetMethodName(TCppMethod_t method)
{
    return CppDispatch::GetName(method);
}

std::string Cppyy::GetMethodFullName(TCppMethod_t method)
{
    return CppDispatch::GetCompleteName(method);
}

Cppyy::TCppType_t Cppyy::GetMethodReturnType(TCppMethod_t method)
{
    return CppDispatch::GetFunctionReturnType(method);
}

std::string Cppyy::GetMethodReturnTypeAsString(TCppMethod_t method)
{
    return 
    CppDispatch::GetTypeAsString(
        CppDispatch::GetCanonicalType(
            CppDispatch::GetFunctionReturnType(method)));
}

Cppyy::TCppIndex_t Cppyy::GetMethodNumArgs(TCppMethod_t method)
{
    return CppDispatch::GetFunctionNumArgs(method);
}

Cppyy::TCppIndex_t Cppyy::GetMethodReqArgs(TCppMethod_t method)
{
    return CppDispatch::GetFunctionRequiredArgs(method);
}

std::string Cppyy::GetMethodArgName(TCppMethod_t method, TCppIndex_t iarg)
{
    if (!method)
        return "<unknown>";

    return CppDispatch::GetFunctionArgName(method, iarg);
}

Cppyy::TCppType_t Cppyy::GetMethodArgType(TCppMethod_t method, TCppIndex_t iarg)
{
    return CppDispatch::GetFunctionArgType(method, iarg);
}

std::string Cppyy::GetMethodArgTypeAsString(TCppMethod_t method, TCppIndex_t iarg)
{
    return CppDispatch::GetTypeAsString(
        CppDispatch::GetFunctionArgType(method, iarg));
}

std::string Cppyy::GetMethodArgCanonTypeAsString(TCppMethod_t method, TCppIndex_t iarg)
{
    return
    CppDispatch::GetTypeAsString(
        CppDispatch::GetCanonicalType(
            CppDispatch::GetFunctionArgType(method, iarg)));
}

std::string Cppyy::GetMethodArgDefault(TCppMethod_t method, TCppIndex_t iarg)
{
    if (!method)
       return "";
    return CppDispatch::GetFunctionArgDefault(method, iarg);
}

Cppyy::TCppIndex_t Cppyy::CompareMethodArgType(TCppMethod_t method, TCppIndex_t iarg, const std::string &req_type)
{
    // if (method) {
    //     TFunction* f = m2f(method);
    //     TMethodArg* arg = (TMethodArg *)f->GetListOfMethodArgs()->At((int)iarg);
    //     void *argqtp = gInterpreter->TypeInfo_QualTypePtr(arg->GetTypeInfo());

    //     TypeInfo_t *reqti = gInterpreter->TypeInfo_Factory(req_type.c_str());
    //     void *reqqtp = gInterpreter->TypeInfo_QualTypePtr(reqti);

    //     if (ArgSimilarityScore(argqtp, reqqtp) < 10) {
    //         return ArgSimilarityScore(argqtp, reqqtp);
    //     }
    //     else { // Match using underlying types
    //         if(gInterpreter->IsPointerType(argqtp))
    //             argqtp = gInterpreter->TypeInfo_QualTypePtr(gInterpreter->GetPointerType(argqtp));

    //         // Handles reference types and strips qualifiers
    //         TypeInfo_t *arg_ul = gInterpreter->GetNonReferenceType(argqtp);
    //         TypeInfo_t *req_ul = gInterpreter->GetNonReferenceType(reqqtp);
    //         argqtp = gInterpreter->TypeInfo_QualTypePtr(gInterpreter->GetUnqualifiedType(gInterpreter->TypeInfo_QualTypePtr(arg_ul)));
    //         reqqtp = gInterpreter->TypeInfo_QualTypePtr(gInterpreter->GetUnqualifiedType(gInterpreter->TypeInfo_QualTypePtr(req_ul)));

    //         return ArgSimilarityScore(argqtp, reqqtp);
    //     }
    // }
    return 0; // Method is not valid
}

std::string Cppyy::GetMethodSignature(TCppMethod_t method, bool show_formal_args, TCppIndex_t max_args)
{
    std::ostringstream sig;
    sig << "(";
    int nArgs = GetMethodNumArgs(method);
    if (max_args != (TCppIndex_t)-1) nArgs = std::min(nArgs, (int)max_args);
    for (int iarg = 0; iarg < nArgs; ++iarg) {
        sig << Cppyy::GetMethodArgTypeAsString(method, iarg);
        if (show_formal_args) {
            std::string argname = Cppyy::GetMethodArgName(method, iarg);
            if (!argname.empty()) sig << " " << argname;
            std::string defvalue = Cppyy::GetMethodArgDefault(method, iarg);
            if (!defvalue.empty()) sig << " = " << defvalue;
        }
        if (iarg != nArgs-1) sig << ", ";
    }
    sig << ")";
    return sig.str();
}

std::string Cppyy::GetMethodPrototype(TCppMethod_t method, bool show_formal_args)
{
  assert(0 && "Unused");
  return ""; // return CppDispatch::GetFunctionPrototype(method, show_formal_args);
}

bool Cppyy::IsConstMethod(TCppMethod_t method)
{
    if (!method)
        return false;
    return CppDispatch::IsConstMethod(method);
}

void Cppyy::GetTemplatedMethods(TCppScope_t scope, std::vector<Cppyy::TCppMethod_t> &methods)
{
    CppDispatch::GetFunctionTemplatedDecls(scope, methods);
}

Cppyy::TCppIndex_t Cppyy::GetNumTemplatedMethods(TCppScope_t scope, bool accept_namespace)
{
    std::vector<Cppyy::TCppMethod_t> mc;
    CppDispatch::GetFunctionTemplatedDecls(scope, mc);
    return mc.size();
}

std::string Cppyy::GetTemplatedMethodName(TCppScope_t scope, TCppIndex_t imeth)
{
    std::vector<Cppyy::TCppMethod_t> mc;
    CppDispatch::GetFunctionTemplatedDecls(scope, mc);

    if (imeth < mc.size()) return GetMethodName(mc[imeth]);

    return "";
}

bool Cppyy::ExistsMethodTemplate(TCppScope_t scope, const std::string& name)
{
    return CppDispatch::ExistsFunctionTemplate(name, scope);
}

bool Cppyy::IsTemplatedMethod(TCppMethod_t method)
{
    return CppDispatch::IsTemplatedFunction(method);
}

bool Cppyy::IsStaticTemplate(TCppScope_t scope, const std::string& name)
{
    if (CppDispatch::TCppFunction_t tf = GetMethodTemplate(scope, name, ""))
        return CppDispatch::IsStaticMethod(tf);
    return false;
}

Cppyy::TCppMethod_t Cppyy::GetMethodTemplate(
    TCppScope_t scope, const std::string& name, const std::string& proto)
{
    std::string pureName;
    std::string explicit_params;

    if ((name.find("operator<") != 0) &&
        (name.find('<') != std::string::npos)) {
        pureName = name.substr(0, name.find('<'));
        size_t start = name.find('<');
        size_t end = name.rfind('>');
        explicit_params = name.substr(start + 1, end - start - 1);
    } else {
        pureName = name;
    }

    std::vector<Cppyy::TCppMethod_t> unresolved_candidate_methods;
    CppDispatch::GetClassTemplatedMethods(pureName, scope,
                                  unresolved_candidate_methods);
    if (unresolved_candidate_methods.empty() && name.find("operator") == 0) {
        // try operators
        Cppyy::GetClassOperators(scope, pureName, unresolved_candidate_methods);
    }

    // CPyCppyy assumes that we attempt instantiation here
    std::vector<CppDispatch::TemplateArgInfo> arg_types;
    std::vector<CppDispatch::TemplateArgInfo> templ_params;
    Cppyy::AppendTypesSlow(proto, arg_types, scope);
    Cppyy::AppendTypesSlow(explicit_params, templ_params, scope);

    Cppyy::TCppMethod_t cppmeth = CppDispatch::BestOverloadFunctionMatch(
        unresolved_candidate_methods, templ_params, arg_types);

    if (!cppmeth && unresolved_candidate_methods.size() == 1 &&
        !templ_params.empty())
      cppmeth =
          CppDispatch::InstantiateTemplate(unresolved_candidate_methods[0],
                                   templ_params.data(), templ_params.size(), false);

    return cppmeth;

    // if it fails, use Sema to propogate info about why it failed (DeductionInfo)

}

static inline std::string type_remap(const std::string& n1,
                                     const std::string& n2) {
    // Operator lookups of (C++ string, Python str) should succeed for the
    // combos of string/str, wstring/str, string/unicode and wstring/unicode;
    // since C++ does not have a operator+(std::string, std::wstring), we'll
    // have to look up the same type and rely on the converters in
    // CPyCppyy/_cppyy.
    if (n1 == "str" || n1 == "unicode" || n1 == "std::basic_string<char>") {
        if (n2 == "std::basic_string<wchar_t>")
            return "std::basic_string<wchar_t>&";                      // match like for like
        return "std::basic_string<char>&"; // probably best bet
    } else if (n1 == "std::basic_string<wchar_t>") {
        return "std::basic_string<wchar_t>&";
    } else if (n1 == "float") {
        return "double"; // debatable, but probably intended
    } else if (n1 == "complex") {
        return "std::complex<double>";
    }
    return n1;
}

void Cppyy::GetClassOperators(Cppyy::TCppScope_t klass,
                              const std::string& opname,
                              std::vector<TCppScope_t>& operators) {
    std::string op = opname.substr(8);
    CppDispatch::GetOperator(klass, CppDispatch::GetOperatorFromSpelling(op), operators, CppDispatch::OperatorArity::kBoth);
}

Cppyy::TCppMethod_t Cppyy::GetGlobalOperator(
    TCppType_t scope, const std::string& lc, const std::string& rc, const std::string& opname)
{
    std::string rc_type = type_remap(rc, lc);
    std::string lc_type = type_remap(lc, rc);
    bool is_templated = false;
    if ((lc_type.find('<') != std::string::npos) ||
        (rc_type.find('<') != std::string::npos)) {
        is_templated = true;
    }

    std::vector<TCppScope_t> overloads;
    CppDispatch::GetOperator(scope, CppDispatch::GetOperatorFromSpelling(opname), overloads, CppDispatch::OperatorArity::kBoth);

    std::vector<Cppyy::TCppMethod_t> unresolved_candidate_methods;
    for (auto overload: overloads) {
        if (CppDispatch::IsTemplatedFunction(overload)) {
            unresolved_candidate_methods.push_back(overload);
            continue;
        } else {
            TCppType_t lhs_type = CppDispatch::GetFunctionArgType(overload, 0);
            if (lc_type !=
                CppDispatch::GetTypeAsString(CppDispatch::GetUnderlyingType(lhs_type)))
                continue;

            if (!rc_type.empty()) {
                if (CppDispatch::GetFunctionNumArgs(overload) != 2)
                    continue;
                TCppType_t rhs_type = CppDispatch::GetFunctionArgType(overload, 1);
                if (rc_type !=
                    CppDispatch::GetTypeAsString(CppDispatch::GetUnderlyingType(rhs_type)))
                    continue;
            }
            return overload;
        }
    }
    if (is_templated) {
        std::string lc_template = lc_type.substr(
            lc_type.find("<") + 1, lc_type.rfind(">") - lc_type.find("<") - 1);
        std::string rc_template = rc_type.substr(
            rc_type.find("<") + 1, rc_type.rfind(">") - rc_type.find("<") - 1);

        std::vector<CppDispatch::TemplateArgInfo> arg_types;
        if (auto l = Cppyy::GetType(lc_type, true))
            arg_types.emplace_back(l);
        else
            return nullptr;

        if (!rc_type.empty()) {
            if (auto r = Cppyy::GetType(rc_type, true))
                arg_types.emplace_back(r);
            else
                return nullptr;
        }
        Cppyy::TCppMethod_t cppmeth = CppDispatch::BestOverloadFunctionMatch(
            unresolved_candidate_methods, {}, arg_types);
        if (cppmeth)
            return cppmeth;
    }
    {
        // we are trying to do a madeup IntegralToFloating implicit cast emulating clang
        bool flag = false;
        if (rc_type == "int") {
            rc_type = "double";
            flag = true;
        }
        if (lc_type == "int") {
            lc_type = "double";
            flag = true;
        }
        if (flag)
            return GetGlobalOperator(scope, lc_type, rc_type, opname);
    }
    return nullptr;
}

// // method properties ---------------------------------------------------------
bool Cppyy::IsDeletedMethod(TCppMethod_t method)
{
    return CppDispatch::IsFunctionDeleted(method);
}

bool Cppyy::IsPublicMethod(TCppMethod_t method)
{
    return CppDispatch::IsPublicMethod(method);
}

bool Cppyy::IsProtectedMethod(TCppMethod_t method)
{
    return CppDispatch::IsProtectedMethod(method);
}

bool Cppyy::IsPrivateMethod(TCppMethod_t method)
{
    return CppDispatch::IsPrivateMethod(method);
}

bool Cppyy::IsConstructor(TCppMethod_t method)
{
    return CppDispatch::IsConstructor(method);
}

bool Cppyy::IsDestructor(TCppMethod_t method)
{
    return CppDispatch::IsDestructor(method);
}

bool Cppyy::IsStaticMethod(TCppMethod_t method)
{
    return CppDispatch::IsStaticMethod(method);
}

void Cppyy::GetDatamembers(TCppScope_t scope, std::vector<TCppScope_t>& datamembers)
{
    CppDispatch::GetDatamembers(scope, datamembers);
    CppDispatch::GetStaticDatamembers(scope, datamembers);
    CppDispatch::GetEnumConstantDatamembers(scope, datamembers, false);
}

bool Cppyy::CheckDatamember(TCppScope_t scope, const std::string& name) {
    return (bool) CppDispatch::LookupDatamember(name, scope);
}

bool Cppyy::IsLambdaClass(TCppType_t type) {
    return CppDispatch::IsLambdaClass(type);
}

Cppyy::TCppScope_t Cppyy::WrapLambdaFromVariable(TCppScope_t var) {
    std::ostringstream code;
    std::string name = Cppyy::GetFinalName(var);
    code << "namespace __cppyy_internal_wrap_g {\n"
      << "  " << "std::function " << name << " = ::" << CppDispatch::GetQualifiedName(var) << ";\n"
      << "}\n";
    
    std::cout<< "\nCODE: " << code.str() << "\n";
    if (Cppyy::Compile(code.str().c_str())) {
      TCppScope_t res = CppDispatch::GetNamed(name, CppDispatch::GetScope("__cppyy_internal_wrap_g", nullptr));
      if (res) return res;
    }
    return var;
}

Cppyy::TCppScope_t Cppyy::AdaptFunctionForLambdaReturn(TCppScope_t fn) {
    std::string fn_name = CppDispatch::GetQualifiedCompleteName(fn);
    std::string signature = Cppyy::GetMethodSignature(fn, true);

    std::ostringstream call;
    call << "(";
    for (size_t i = 0, n = Cppyy::GetMethodNumArgs(fn); i < n; i++) {
        call << Cppyy::GetMethodArgName(fn, i);
        if (i != n - 1)
            call << ", ";
    }
    call << ")";
    
    std::ostringstream code;
    static int i = 0;
    std::string name = "lambda_return_convert_" + std::to_string(++i);
        code << "namespace __cppyy_internal_wrap_g {\n"
         << "auto " << name << signature << "{" << "return std::function(" << fn_name << call.str() << "); }\n"
         << "}\n";
    std::cout<< "\nCODE: " << code.str() << "\n";
    if (Cppyy::Compile(code.str().c_str())) {
      TCppScope_t res = CppDispatch::GetNamed(name, CppDispatch::GetScope("__cppyy_internal_wrap_g", nullptr));
      if (res) return res;
    }
    return fn;
}

Cppyy::TCppType_t Cppyy::GetDatamemberType(TCppScope_t var)
{
  return CppDispatch::GetVariableType(CppDispatch::GetUnderlyingScope(var));
}

std::string Cppyy::GetDatamemberTypeAsString(TCppScope_t scope)
{
  return CppDispatch::GetTypeAsString(
      CppDispatch::GetVariableType(CppDispatch::GetUnderlyingScope(scope)));
}

std::string Cppyy::GetTypeAsString(TCppType_t type)
{
    return CppDispatch::GetTypeAsString(type);
}

intptr_t Cppyy::GetDatamemberOffset(TCppScope_t var, TCppScope_t klass)
{
  return CppDispatch::GetVariableOffset(CppDispatch::GetUnderlyingScope(var), klass);
}

// data member properties ----------------------------------------------------
bool Cppyy::IsPublicData(TCppScope_t datamem)
{
    return CppDispatch::IsPublicVariable(datamem);
}

bool Cppyy::IsProtectedData(TCppScope_t datamem)
{
    return CppDispatch::IsProtectedVariable(datamem);
}

bool Cppyy::IsPrivateData(TCppScope_t datamem)
{
    return CppDispatch::IsPrivateVariable(datamem);
}

bool Cppyy::IsStaticDatamember(TCppScope_t var)
{
  return CppDispatch::IsStaticVariable(CppDispatch::GetUnderlyingScope(var));
}

bool Cppyy::IsConstVar(TCppScope_t var)
{
    return CppDispatch::IsConstVariable(var);
}

Cppyy::TCppScope_t Cppyy::ReduceReturnType(TCppScope_t fn, TCppType_t reduce) {
    std::string fn_name = CppDispatch::GetQualifiedCompleteName(fn);
    std::string signature = Cppyy::GetMethodSignature(fn, true);
    std::string result_type = Cppyy::GetTypeAsString(reduce);

    std::ostringstream call;
    call << "(";
    for (size_t i = 0, n = Cppyy::GetMethodNumArgs(fn); i < n; i++) {
        call << Cppyy::GetMethodArgName(fn, i);
        if (i != n - 1)
            call << ", ";
    }
    call << ")";
    
    std::ostringstream code;
    static int i = 0;
    std::string name = "reduced_function_" + std::to_string(++i);
        code << "namespace __cppyy_internal_wrap_g {\n"
         << result_type << " " << name << signature << "{" << "return (" << result_type << ")::" << fn_name << call.str() << "; }\n"
         << "}\n";
    if (Cppyy::Compile(code.str().c_str())) {
      TCppScope_t res = CppDispatch::GetNamed(name, CppDispatch::GetScope("__cppyy_internal_wrap_g", nullptr));
      if (res) return res;
    }
    return fn;
}

std::vector<long int>  Cppyy::GetDimensions(TCppType_t type)
{
    return CppDispatch::GetDimensions(type);
}

// enum properties -----------------------------------------------------------
std::vector<Cppyy::TCppScope_t> Cppyy::GetEnumConstants(TCppScope_t scope)
{
    return CppDispatch::GetEnumConstants(scope);
}

Cppyy::TCppType_t Cppyy::GetEnumConstantType(TCppScope_t scope)
{
  return CppDispatch::GetEnumConstantType(CppDispatch::GetUnderlyingScope(scope));
}

Cppyy::TCppIndex_t Cppyy::GetEnumDataValue(TCppScope_t scope)
{
    return CppDispatch::GetEnumConstantValue(scope);
}

Cppyy::TCppScope_t Cppyy::InstantiateTemplate(
             TCppScope_t tmpl, CppDispatch::TemplateArgInfo* args, size_t args_size)
{
    return CppDispatch::InstantiateTemplate(tmpl, args, args_size, false);
}

void Cppyy::DumpScope(TCppScope_t scope)
{
    CppDispatch::DumpScope(scope);
}
#include "Utils.h"

#include "CppInterOp/CppInterOpDispatch.h"

#include "clang/AST/ASTContext.h"
#include "clang/Basic/Version.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Sema/Sema.h"

#include "gtest/gtest.h"
#include <dlfcn.h>
#include <string>

using namespace TestUtils;
using namespace llvm;
using namespace clang;


// TEST(ExternAPITest, FindGetVersion) {

// void* cppinterop_handle = dlopen("libclangCppInterOp.so", RTLD_LOCAL);
// if (!cppinterop_handle) { /* handle error */ }

// using GetVersionFunc = std::string (*)();
// GetVersionFunc GetVersion = (GetVersionFunc)dlsym(cppinterop_handle, "Cpp::GetVersion");
// if (!GetVersion) { /* handle error */ }

// }

// TEST(ExternAPITest, GetVersionSymbolLookup) {
//   const char* libPath = "/home/ajomy/cppyy-interop-dev/CppInterOp/build/lib/libclangCppInterOp.so";
//   void* handle = dlopen(libPath, RTLD_LOCAL | RTLD_NOW);
//   ASSERT_NE(handle, nullptr) << "Failed to open library: " << dlerror();

//   typedef const char* (*GetVersionFn)();
//   GetVersionFn get_version = (GetVersionFn)dlsym(handle, "cppinterop_get_version_c");
//   ASSERT_NE(get_version, nullptr) << "failed to locate symbol: " << dlerror();
//   const char* version_cstr = get_version();
//   ASSERT_NE(version_cstr, nullptr) << "string is null";
//   std::string version(version_cstr);
//   std::cout<<"\nHELLO\n"<<version<<"\n";

//   dlclose(handle);
// }

// TEST(ExternAPITest, IsClassSymbolLookup) {
//   const char* libPath = "/home/ajomy/cppyy-interop-dev/CppInterOp/build/lib/libclangCppInterOp.so";
//   void* handle = dlopen(libPath, RTLD_LOCAL | RTLD_NOW);
//   ASSERT_NE(handle, nullptr) << "Failed to open library: " << dlerror();

//   using IsClassFunc =  bool (*)(Cpp::TCppScope_t scope);
//   std::cout<< typeid(Cpp::IsClass).name() <<"\n";
//   IsClassFunc IsClassExposed = (IsClassFunc)dlsym(handle, typeid(decltype(Cpp::IsClass)).name());
//   ASSERT_NE(IsClassExposed, nullptr) << "failed to locate symbol: " << dlerror();
//   std::vector<Decl*> Decls;
//   GetAllTopLevelDecls("namespace N {} class C{}; int I;", Decls);
//   EXPECT_FALSE(IsClassExposed(Decls[0]));
//   EXPECT_TRUE(IsClassExposed(Decls[1]));
//   EXPECT_FALSE(IsClassExposed(Decls[2]));

//   dlclose(handle);
// }


// void* dlGetProcAddress (const char* name)
// {

//   const char* libPath = "/home/ajomy/cppyy-interop-dev/CppInterOp/build/lib/libclangCppInterOp.so";
//   static void* handle = dlopen(libPath, RTLD_LOCAL | RTLD_NOW);
//   if (!handle) return nullptr;
//   // ASSERT_NE(handle, nullptr) << "Failed to open library: " << dlerror();
//   static void* gpa = dlsym(handle, "CppGetProcAddress");
//   if (!gpa) return nullptr;
//   // ASSERT_NE(gpa, nullptr) << "Failed to find GetProcAddress" << dlerror();
//   dlclose(handle);
//   return ((void*(*)(const char*))gpa)(name);
// }

void* dlGetProcAddress (const char* name)
{
  // const char* libPath = "/home/ajomy/cppyy-interop-dev/CppInterOp/build/lib/libclangCppInterOp.so";
  const char* libPath = "/home/ajomy/ROOT/root_build/lib/libCling.so";
  static void* handle = nullptr;
  handle = dlopen(libPath, RTLD_LAZY|RTLD_LOCAL);
  if (!handle)
  std::cout << "Failed to open li: " << dlerror();
  static void* (*gpa)(const char*) = nullptr;
  gpa = reinterpret_cast<void*(*)(const char*)>(dlsym(handle, "CppGetProcAddress"));
  std::cout<<"\nGPA: "<<gpa<<"\n";
  if (gpa) {
    std::cout << "Found GetProcAddress" << dlerror();
  }
  else {
    std::cout << "Failed to find GetProcAddress" << dlerror();
  return nullptr;
  }

  return gpa(name);
}

TEST(ExternAPITest, IsClassSymbolLookup) {
  CppAPIType::IsClass IsClassExposed = reinterpret_cast<CppAPIType::IsClass>(dlGetProcAddress("IsClass"));
  ASSERT_NE(IsClassExposed, nullptr) << "failed to locate symbol: " << dlerror();
  std::vector<Decl*> Decls;
  GetAllTopLevelDecls("namespace N {} class C{}; int I;", Decls);
  EXPECT_FALSE(IsClassExposed(Decls[0]));
  EXPECT_TRUE(IsClassExposed(Decls[1]));
  EXPECT_FALSE(IsClassExposed(Decls[2]));
}

TEST(ExternAPITest, Demangle) {

  std::string code = R"(
    int add(int x, int y) { return x + y; }
    int add(double x, double y) { return x + y; }
  )";

  std::vector<Decl*> Decls;
  GetAllTopLevelDecls(code, Decls);
  EXPECT_EQ(Decls.size(), 2);

  auto Add_int = clang::GlobalDecl(static_cast<clang::NamedDecl*>(Decls[0]));
  auto Add_double = clang::GlobalDecl(static_cast<clang::NamedDecl*>(Decls[1]));

  std::string mangled_add_int;
  std::string mangled_add_double;
  compat::maybeMangleDeclName(Add_int, mangled_add_int);
  compat::maybeMangleDeclName(Add_double, mangled_add_double);

  // using fDemangle =  std::string (*)(const std::string scope);
  CppAPIType::Demangle DemangleDispatched = reinterpret_cast<CppAPIType::Demangle>(dlGetProcAddress("Demangle"));
  CppAPIType::GetQualifiedCompleteName GetQualifiedCompleteNameDispatched = reinterpret_cast<CppAPIType::GetQualifiedCompleteName>(dlGetProcAddress("GetQualifiedCompleteName"));

  // using namespace CppDispatch
  std::string demangled_add_int = DemangleDispatched(mangled_add_int);
  std::string demangled_add_double = DemangleDispatched(mangled_add_double);

  std::cout << "mangled_add_int: " << mangled_add_int << "\n";
  std::cout << "demangled_add_int: " << demangled_add_int << "\n";
  std::cout << "mangled_add_double: " << mangled_add_double <<  "\n";
  std::cout << "demangled_add_double: " << demangled_add_double << "\n";
  std::cout << "GetQualifiedCompleteNameDispatched(Decls[0]): " << GetQualifiedCompleteNameDispatched(Decls[0]) << "\n";
  std::cout << "GetQualifiedCompleteNameDispatched(Decls[1]): " << GetQualifiedCompleteNameDispatched(Decls[1]) << "\n";
  EXPECT_NE(demangled_add_int.find(GetQualifiedCompleteNameDispatched(Decls[0])),
            std::string::npos);
  EXPECT_NE(demangled_add_double.find(GetQualifiedCompleteNameDispatched(Decls[1])),
            std::string::npos);
}

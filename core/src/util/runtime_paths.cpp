// SPDX-License-Identifier: MPL-2.0
#include "core/util/runtime_paths.h"
#include <cstdlib>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <dlfcn.h>
#else
#include <unistd.h>
#include <dlfcn.h>
#endif
namespace bc {
std::filesystem::path executableDirectory() {
#ifdef _WIN32
    std::vector<wchar_t> buffer(32768);
    const auto length=GetModuleFileNameW(nullptr,buffer.data(),static_cast<DWORD>(buffer.size()));
    if(length && length<buffer.size()) return std::filesystem::path(std::wstring(buffer.data(),length)).parent_path();
#elif defined(__APPLE__)
    uint32_t size=0;_NSGetExecutablePath(nullptr,&size);
    std::vector<char> buffer(size);
    if(_NSGetExecutablePath(buffer.data(),&size)==0){
        std::error_code error;auto path=std::filesystem::weakly_canonical(buffer.data(),error);
        return (error?std::filesystem::path(buffer.data()):path).parent_path();
    }
#else
    std::vector<char> buffer(32768);
    const auto length=readlink("/proc/self/exe",buffer.data(),buffer.size());
    if(length>0 && static_cast<size_t>(length)<buffer.size()) return std::filesystem::path(std::string(buffer.data(),static_cast<size_t>(length))).parent_path();
#endif
    return std::filesystem::current_path();
}
std::filesystem::path environmentPath(const char* name) {
#ifdef _WIN32
    const std::wstring key(name,name+std::char_traits<char>::length(name));
    const auto length=GetEnvironmentVariableW(key.c_str(),nullptr,0);
    if(!length)return {};
    std::vector<wchar_t> value(length);
    const auto written=GetEnvironmentVariableW(key.c_str(),value.data(),length);
    return written && written<length?std::filesystem::path(std::wstring(value.data(),written)):std::filesystem::path{};
#else
    const auto* value=std::getenv(name);return value?std::filesystem::u8path(value):std::filesystem::path{};
#endif
}
std::filesystem::path runtimeDirectory() {
    auto shared=environmentPath("NL_AUDIO_RUNTIME_DIR");return shared.empty()?executableDirectory():shared;
}
std::string libraryFileName(const std::string& base) {
#ifdef _WIN32
    return base+".dll";
#else
    return (base.rfind("lib",0)==0?base:"lib"+base)+
#ifdef __APPLE__
        ".dylib";
#else
        ".so";
#endif
#endif
}
LibraryHandle openSharedLibrary(const std::filesystem::path& path) {
#ifdef _WIN32
    return reinterpret_cast<void*>(LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS));
#else
    return dlopen(path.c_str(),RTLD_NOW|RTLD_LOCAL);
#endif
}
void* librarySymbol(LibraryHandle library,const char* name) {
#ifdef _WIN32
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(library),name));
#else
    return dlsym(library,name);
#endif
}
void closeSharedLibrary(LibraryHandle library) {
    if(!library)return;
#ifdef _WIN32
    FreeLibrary(static_cast<HMODULE>(library));
#else
    dlclose(library);
#endif
}
}

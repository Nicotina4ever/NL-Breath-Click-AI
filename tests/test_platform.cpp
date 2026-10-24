// SPDX-License-Identifier: MPL-2.0
#include "core/util/runtime_paths.h"
#include "studio_engine.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
int main() {
    namespace fs=std::filesystem;
    if(!fs::is_directory(bc::executableDirectory()) || !bc::executableDirectory().is_absolute())return 1;
    const auto root=fs::temp_directory_path()/("nlbc_platform_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    const auto file=root/fs::u8path("hash-caffè.txt");
    {std::ofstream out(file,std::ios::binary);out<<"abc";}
    std::string hash,error;
    bool ok=bc::studio::sha256File(file.u8string(),hash,&error) && hash=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    {std::ofstream out(file,std::ios::binary|std::ios::trunc);}
    ok=ok && bc::studio::sha256File(file.u8string(),hash,&error) && hash=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
    ok=ok && !bc::studio::sha256File((root/"missing").u8string(),hash,&error);
    fs::remove(file);fs::remove(root);
    const auto library=bc::openSharedLibrary(bc::runtimeDirectory()/bc::libraryFileName("libmp3lame"));
    ok=ok && library && bc::librarySymbol(library,"lame_init");
    bc::closeSharedLibrary(library);
    if(!ok){std::cerr<<"Platform runtime/hash contract failed: "<<error<<'\n';return 2;}
    std::cout<<"Platform OK: executable path, Unicode SHA-256 vectors and optional codec loader\n";
    return 0;
}

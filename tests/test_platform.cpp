// SPDX-License-Identifier: MPL-2.0
#include "core/util/runtime_paths.h"
#include "studio_engine.h"
#include "core/edit/audio_document.h"
#include "core/learning/personal_profile.h"
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
    // Same PCM identity across platforms preserves the recording holdout.
    bc::AudioBuffer pcm; pcm.sampleRate=48000; pcm.channels=1; pcm.samples={0.f,.25f,-.5f,1.f};
    bc::AudioDocument document; ok=ok && document.adopt(pcm,"synthetic.wav");
    bc::EditRegion region; region.startSample=0; region.endSample=4;
    region.trainingEligible=true; region.decisionSource="user"; region.learningLabel="breath_confirmed";
    document.addRegion(region);
    const auto examples=bc::collectLearningExamples(document);
    ok=ok && examples.size()==1 && examples[0].recordingId=="07654226a170fc33d8a70093f82f6ee4c508696801cfec9832d8392c062962d1";
    if(!ok){std::cerr<<"Platform runtime/hash contract failed: "<<error<<'\n';return 2;}
    std::cout<<"Platform OK: executable path, Unicode SHA-256 vectors and optional codec loader\n";
    return 0;
}

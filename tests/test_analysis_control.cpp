// SPDX-License-Identifier: MPL-2.0
#include "standalone/studio_engine.h"
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace bc;
using namespace bc::studio;
static void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
static bool same(const Analysis& a, const Analysis& b) {
    if (a.inputSha256 != b.inputSha256 || a.frames != b.frames || a.events.size() != b.events.size() ||
        a.aiProbabilities != b.aiProbabilities || a.aiInferenceFrames != b.aiInferenceFrames) return false;
    for (size_t i = 0; i < a.events.size(); ++i) {
        const auto& x = a.events[i]; const auto& y = b.events[i];
        if (x.kind != y.kind || x.startSample != y.startSample || x.endSample != y.endSample ||
            x.score != y.score || x.classification != y.classification) return false;
    }
    return true;
}

int main() try {
    const auto root = std::filesystem::temp_directory_path() /
        ("bc_analysis_control_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    const auto path = (root / "synthetic.wav").u8string();
    AudioBuffer audio; audio.sampleRate = 16000; audio.channels = 1; audio.samples.resize(16000 * 8);
    for (size_t i = 0; i < audio.samples.size(); ++i) {
        audio.samples[i] = .05f * std::sin(static_cast<double>(i) * .071) + .02f * std::sin(static_cast<double>(i) * .913);
    }
    audio.samples[35000] = .7f;
    std::string error;
    check(writeWav(path, audio, WavFormat::Float32, &error), "write fixture");
    AnalysisControl control;
    Analysis seed; seed.inputPath = "keep previous analysis"; seed.frames = 12345;
    auto cancelledLeavesResult = [&](const std::string& input, const Settings& settings) {
        Analysis out = seed;
        check(!analyzeFile(input, settings, &out, &error, &control), "cancel returns false");
        check(control.cancelled() && error == "Analisi annullata", "cancellation distinct from engine error");
        check(out.inputPath == seed.inputPath && out.frames == seed.frames, "cancel leaves previous result intact");
    };
    control.cancelRequested = true;
    cancelledLeavesResult((root / "does-not-exist.wav").u8string(), Settings{});
    control.cancelRequested = false;
    control.progress = [&](float, const char* stage) {
        if (std::string(stage) == "Analisi dei frame") control.cancelRequested = true;
    };
    cancelledLeavesResult(path, Settings{});
    for (const std::string mode : {"dsp", "ai", "fusion"}) {
        Settings settings; settings.detector = mode; settings.detectClicks = true; settings.traceAiProbabilities = true;
        Analysis baseline, observed; control.cancelRequested = false;
        int reports = 0;
        control.progress = [&](float, const char*) { ++reports; };
        check(analyzeFile(path, settings, &baseline, &error), error.c_str());
        check(analyzeFile(path, settings, &observed, &error, &control), error.c_str());
        check(reports > 0 && same(baseline, observed), "progress/worker control changes detection or AI probabilities");
    }
    Settings ai; ai.detector = "ai";
    control.progress = [&](float fraction, const char* stage) {
        if (std::string(stage) == "Analisi della voce" && fraction > .5f) control.cancelRequested = true;
    };
    cancelledLeavesResult(path, ai);
    // A genuine in-flight ORT run, not cancellation just before calling Run.
    audio.samples.resize(16000 * 90, .001f);
    check(writeWav(path, audio, WavFormat::Float32, &error), "write longer inference fixture");
    control.cancelRequested = false;
    std::thread cancelThread;
    struct Join { std::thread& thread; ~Join() { if (thread.joinable()) thread.join(); } } join{cancelThread};
    std::chrono::steady_clock::time_point requested;
    control.progress = [&](float, const char* stage) {
        if (std::string(stage) == "Rilevamento dei respiri") cancelThread = std::thread([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(75));
            requested = std::chrono::steady_clock::now(); control.cancelRequested = true;
        });
    };
    cancelledLeavesResult(path, ai);
    cancelThread.join();
    const auto latency = std::chrono::duration<double>(std::chrono::steady_clock::now() - requested).count();
    check(latency < 3, "ORT cancellation did not stop inference promptly");
    control.cancelRequested = false; control.progress = {};
    // Termination belongs to the cancelled run only, never the next analysis.
    audio.samples.resize(16000 * 2); check(writeWav(path, audio, WavFormat::Float32, &error), "write retry");
    Analysis retry; check(analyzeFile(path, ai, &retry, &error, &control), error.c_str());
    std::filesystem::remove(std::filesystem::u8path(path));
    std::filesystem::remove(root);
    std::cout << "Analysis control PASS: transactional cancel, DSP/AI/Fusion parity, feature cancellation, ORT stop "
              << latency << " s, retry\n";
    return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }

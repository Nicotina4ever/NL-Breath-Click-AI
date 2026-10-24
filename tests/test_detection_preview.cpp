// SPDX-License-Identifier: MPL-2.0
#include "editor/detection_preview_worker.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

namespace {
int fail(const char* message) { std::fprintf(stderr, "FAIL: %s\n", message); return 1; }
bool equal(const bc::AnalysisResult& a, const bc::AnalysisResult& b) {
    if (a.error != b.error || a.detections.size() != b.detections.size() ||
        a.breathScores != b.breathScores || a.clickScores != b.clickScores) return false;
    for (size_t i = 0; i < a.detections.size(); ++i) {
        const auto& x = a.detections[i]; const auto& y = b.detections[i];
        if (x.kind != y.kind || x.startSample != y.startSample || x.endSample != y.endSample ||
            x.peakScore != y.peakScore || x.modelScore != y.modelScore) return false;
    }
    return true;
}
bool wait(bc::DetectionPreviewWorker& worker, bc::AnalysisResult& result) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < end) {
        if (worker.takeResult(result)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}
}

int main() {
    std::vector<float> mono(48000 * 15);
    uint32_t seed = 42;
    for (size_t i = 0; i < mono.size(); ++i) {
        seed = seed * 1664525u + 1013904223u;
        mono[i] = (static_cast<float>(seed >> 8) / 16777216.0f - .5f) *
            ((i % 48000) < 12000 ? .1f : .001f);
    }
    bc::OfflineAnalyzer cached, reference;
    bc::OfflineFeatureCache cache;
    cached.setFeatureCache(&cache);
    bc::DetectorSettings settings;
    settings.includeScores = true;
    auto run = [&](bc::OfflineAnalyzer& analyzer) {
        return analyzer.analyze(mono.data(), mono.size(), 48000, settings);
    };
    auto first = run(cached);
    if (first.framesReused || !equal(first, run(reference))) return fail("initial analysis");
    for (int i = 0; i < 6; ++i) {
        settings.breathThreshold = .01f + .12f * i;
        settings.clickSensitivity = .1f * i;
        settings.breathSensitivity = .2f + .1f * i;
        settings.requirePauseForBreath = i % 2 == 0;
        auto result = run(cached);
        if (result.framesReused != result.framesAnalyzed || !equal(result, run(reference)))
            return fail("cached thresholds/sensitivity differ from fresh analysis");
    }
    mono[100] += .25f;
    auto edited = run(cached);
    if (edited.framesReused || !equal(edited, run(reference))) return fail("edited audio invalidation");
    settings.hopSize *= 2;
    if (run(cached).framesReused) return fail("hop invalidation");
    int checks = 0;
    cached.setCancellationCallback([&] { return ++checks > 5; });
    auto cancelled = run(cached);
    if (!cancelled.cancelled || !cancelled.detections.empty()) return fail("cooperative cancellation");
    {
        bc::DetectionPreviewWorker worker(mono, 48000, 1234);
        worker.submit(settings);
        bc::AnalysisResult result;
        if (!wait(worker, result) || !result.error.empty()) return fail("worker completion");
        for (int i = 0; i < 100; ++i) {
            settings.breathThreshold = .001f * i;
            worker.submit(settings);
        }
        if (!wait(worker, result) || result.settings.breathThreshold != settings.breathThreshold ||
            result.framesReused == 0) return fail("latest request and feature reuse");
        auto expected = run(reference);
        for (auto& detection : expected.detections) {
            detection.startSample += 1234; detection.endSample += 1234;
        }
        if (!equal(result, expected)) return fail("worker absolute coordinates");
        worker.submit(settings);
        worker.cancel();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (worker.takeResult(result)) return fail("cancelled generation published");
        worker.submit(settings); // destruction must cancel and join safely
    }
    std::puts("PREVIEW TESTS OK: feature equivalence, invalidation, cancellation, latest result, shutdown");
    return 0;
}

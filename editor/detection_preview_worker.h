// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>
#include "core/detect/offline_analyzer.h"

namespace bc {

// Immutable local audio snapshot, one worker, one pending request. No HWND or
// document pointers cross the thread boundary. Results are polled by the UI.
class DetectionPreviewWorker {
public:
    DetectionPreviewWorker(std::vector<float> mono, int sampleRate, int64_t offset);
    ~DetectionPreviewWorker();
    DetectionPreviewWorker(const DetectionPreviewWorker&) = delete;
    DetectionPreviewWorker& operator=(const DetectionPreviewWorker&) = delete;
    void cancel();
    void submit(const DetectorSettings& settings);
    bool takeResult(AnalysisResult& result);

private:
    void run();
    const std::vector<float> mono_;
    const int sampleRate_;
    const int64_t offset_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::atomic<uint64_t> generation_{0};
    bool stopping_ = false;
    std::optional<DetectorSettings> pending_;
    std::optional<AnalysisResult> result_;
    std::thread thread_;
};
} // namespace bc

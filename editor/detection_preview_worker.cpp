// SPDX-License-Identifier: MPL-2.0
#include "detection_preview_worker.h"
#include <exception>

namespace bc {
DetectionPreviewWorker::DetectionPreviewWorker(std::vector<float> mono, int sampleRate,
                                               int64_t offset)
    : mono_(std::move(mono)), sampleRate_(sampleRate), offset_(offset),
      thread_(&DetectionPreviewWorker::run, this) {}

DetectionPreviewWorker::~DetectionPreviewWorker() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        ++generation_;
    }
    wake_.notify_one();
    thread_.join();
}

void DetectionPreviewWorker::cancel() {
    std::lock_guard<std::mutex> lock(mutex_);
    ++generation_;
    pending_.reset();
    result_.reset();
}

void DetectionPreviewWorker::submit(const DetectorSettings& settings) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++generation_;
        pending_ = settings;
        result_.reset();
    }
    wake_.notify_one();
}

bool DetectionPreviewWorker::takeResult(AnalysisResult& result) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!result_) return false;
    result = std::move(*result_);
    result_.reset();
    return true;
}

void DetectionPreviewWorker::run() {
    OfflineFeatureCache features;
    OfflineAnalyzer analyzer;
    analyzer.setFeatureCache(&features);
    for (;;) {
        DetectorSettings settings;
        uint64_t generation;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [&] { return stopping_ || pending_.has_value(); });
            if (stopping_) return;
            settings = std::move(*pending_);
            pending_.reset();
            generation = generation_.load();
        }
        analyzer.setCancellationCallback([&, generation] {
            return generation_.load() != generation;
        });
        AnalysisResult result;
        try {
            result = analyzer.analyze(mono_.data(), mono_.size(), sampleRate_, settings);
            for (auto& detection : result.detections) {
                detection.startSample += offset_;
                detection.endSample += offset_;
            }
        } catch (const std::exception& e) {
            result.error = e.what();
        } catch (...) {
            result.error = "Errore durante l'anteprima";
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!stopping_ && generation_.load() == generation && !result.cancelled)
                result_ = std::move(result);
        }
    }
}
} // namespace bc

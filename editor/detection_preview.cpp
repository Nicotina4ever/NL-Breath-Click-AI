// SPDX-License-Identifier: MPL-2.0
#include "detection_preview.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "core/detect/offline_analyzer.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace bc {
namespace {

bool overlaps(const Detection& a, const Detection& b, int64_t tolerance) {
    return a.kind == b.kind &&
           a.startSample < b.endSample + tolerance &&
           b.startSample < a.endSample + tolerance;
}

int64_t overlapAmount(const Detection& a, const Detection& b) {
    const int64_t start = std::max(a.startSample, b.startSample);
    const int64_t end = std::min(a.endSample, b.endSample);
    return std::max<int64_t>(0, end - start);
}

} // namespace

DetectionPreviewWindow choosePreviewWindow(const AudioDocument& document,
                                            int64_t focusFrame,
                                            int64_t visibleFrames,
                                            double maxSeconds) {
    DetectionPreviewWindow window;
    window.sampleRate = std::max(1, document.sampleRate());
    const int64_t total = document.frameCount();
    const int64_t maxFrames = std::max<int64_t>(
        1, static_cast<int64_t>(std::llround(maxSeconds * window.sampleRate)));
    int64_t frames = maxFrames;
    if (visibleFrames > 0 && visibleFrames < maxFrames) frames = visibleFrames;
    frames = std::min(frames, total);
    focusFrame = std::max<int64_t>(0, std::min(focusFrame, total));
    int64_t start = focusFrame - frames / 2;
    start = std::max<int64_t>(0, std::min(start, total - frames));
    window.startFrame = start;
    window.endFrame = std::min(total, start + frames);
    return window;
}

AnalysisResult analyzeDetectionPreview(const AudioDocument& document,
                                       const DetectionPreviewWindow& window,
                                       const DetectorSettings& settings) {
    AnalysisResult result;
    result.sampleRate = window.sampleRate;
    const int channels = std::max(1, document.channels());
    const int64_t start = std::max<int64_t>(0, window.startFrame);
    const int64_t end = std::min<int64_t>(document.frameCount(), window.endFrame);
    if (end <= start) {
        result.error = "finestra di anteprima vuota";
        return result;
    }
    const int64_t frames = end - start;
    std::vector<float> mono(static_cast<size_t>(frames), 0.0f);
    const std::vector<float>& samples = document.audio().samples;
    for (int64_t i = 0; i < frames; ++i) {
        float sum = 0.0f;
        for (int c = 0; c < channels; ++c) {
            const size_t index = static_cast<size_t>((start + i) * channels + c);
            if (index < samples.size()) sum += samples[index];
        }
        mono[static_cast<size_t>(i)] = sum / static_cast<float>(channels);
    }

    DetectorSettings local = settings;
    local.includeScores = false;
    OfflineAnalyzer analyzer;
    result = analyzer.analyze(mono.data(), mono.size(),
                              static_cast<double>(window.sampleRate), local);
    // Le coordinate del risultato sono relative alla finestra: qui servono
    // assolute per disegnare e per riconciliare con il documento.
    for (Detection& detection : result.detections) {
        detection.startSample += start;
        detection.endSample += start;
    }
    result.totalSamples = document.frameCount();
    return result;
}

std::vector<Detection> detectionsFromDocument(const AudioDocument& document) {
    std::vector<Detection> detections;
    for (const EditRegion& region : document.regions()) {
        if (!region.fromDetection) continue;
        Detection detection;
        detection.kind = region.kind;
        detection.startSample = region.startSample;
        detection.endSample = region.endSample;
        detection.peakScore = region.score;
        detection.meanScore = region.score;
        detections.push_back(detection);
    }
    return detections;
}

DetectionPreviewDelta compareDetectionProposals(
    const std::vector<Detection>& before,
    const std::vector<Detection>& after,
    int64_t tolerance) {
    DetectionPreviewDelta delta;
    delta.before = before;
    delta.after = after;
    std::vector<bool> beforeMatched(before.size(), false);
    std::vector<bool> afterMatched(after.size(), false);

    // Matching uno-a-uno sull'intersezione maggiore: niente fusione silenziosa.
    for (size_t i = 0; i < before.size(); ++i) {
        int best = -1;
        int64_t bestOverlap = 0;
        for (size_t j = 0; j < after.size(); ++j) {
            if (afterMatched[j] || !overlaps(before[i], after[j], tolerance)) continue;
            const int64_t amount = overlapAmount(before[i], after[j]);
            if (amount > bestOverlap) {
                bestOverlap = amount;
                best = static_cast<int>(j);
            }
        }
        if (best >= 0) {
            beforeMatched[i] = true;
            afterMatched[static_cast<size_t>(best)] = true;
            ++delta.kept;
        }
    }
    delta.beforeKept.resize(before.size());
    delta.afterKept.resize(after.size());
    for (size_t i = 0; i < before.size(); ++i) {
        delta.beforeKept[i] = beforeMatched[i] ? 1 : 0;
        if (!beforeMatched[i]) ++delta.removed;
    }
    for (size_t j = 0; j < after.size(); ++j) {
        delta.afterKept[j] = afterMatched[j] ? 1 : 0;
        if (!afterMatched[j]) ++delta.added;
    }
    return delta;
}

#ifdef _WIN32
namespace {

int xForFrame(const DetectionPreviewWindow& window, int64_t frame, const RECT& client) {
    const int64_t span = std::max<int64_t>(1, window.endFrame - window.startFrame);
    return client.left + static_cast<int>(
        static_cast<double>(frame - window.startFrame) / span *
        static_cast<double>(client.right - client.left));
}

void drawRegions(HDC dc, const RECT& area, const DetectionPreviewWindow& window,
                 const std::vector<Detection>& regions,
                 const std::vector<char>& kept,
                 COLORREF keptColor, COLORREF changedColor, bool upperHalf) {
    HBRUSH keptBrush = CreateSolidBrush(keptColor);
    HBRUSH changedBrush = CreateSolidBrush(changedColor);
    const int top = upperHalf ? area.top : area.top + (area.bottom - area.top) / 2 + 2;
    const int bottom = upperHalf ? area.top + (area.bottom - area.top) / 2 - 2
                                 : area.bottom;
    for (size_t i = 0; i < regions.size(); ++i) {
        const Detection& region = regions[i];
        const int x1 = xForFrame(window, region.startSample, area);
        const int x2 = xForFrame(window, region.endSample, area);
        RECT rect = {x1, top + 2, std::max(x1 + 2, x2), bottom - 2};
        const bool isKept = i < kept.size() && kept[i] != 0;
        FillRect(dc, &rect, isKept ? keptBrush : changedBrush);
    }
    DeleteObject(keptBrush);
    DeleteObject(changedBrush);
}

} // namespace

void drawDetectionPreview(HDC dc, const RECT& client,
                          const AudioDocument& document,
                          const DetectionPreviewWindow& window,
                          const std::vector<Detection>& before,
                          const std::vector<Detection>& after,
                          const std::wstring& title,
                          const std::wstring& counts) {
    HBRUSH background = CreateSolidBrush(RGB(26, 30, 36));
    FillRect(dc, &client, background);
    DeleteObject(background);
    RECT titleArea = {8, 4, client.right - 8, 24};
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(225, 232, 240));
    DrawTextW(dc, title.c_str(), -1, &titleArea, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

    RECT wave = {8, 26, client.right - 8, client.bottom - 22};
    const int channels = std::max(1, document.channels());
    const std::vector<float>& samples = document.audio().samples;
    const int64_t span = std::max<int64_t>(1, window.endFrame - window.startFrame);
    const int width = std::max(1, static_cast<int>(wave.right - wave.left));
    const int mid = (wave.top + wave.bottom) / 2;
    const int height = std::max(1, static_cast<int>(wave.bottom - wave.top) / 2);
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(85, 140, 170));
    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen));
    for (int x = 0; x < width; ++x) {
        const int64_t begin = window.startFrame + span * x / width;
        const int64_t end = window.startFrame + span * (x + 1) / width;
        float minimum = 1.0f;
        float maximum = -1.0f;
        for (int64_t f = begin; f < end; ++f) {
            for (int c = 0; c < channels; ++c) {
                const size_t index = static_cast<size_t>(f * channels + c);
                if (index >= samples.size()) continue;
                minimum = std::min(minimum, samples[index]);
                maximum = std::max(maximum, samples[index]);
            }
        }
        if (minimum > maximum) continue;
        const int y1 = mid - static_cast<int>(maximum * height);
        const int y2 = mid - static_cast<int>(minimum * height);
        MoveToEx(dc, wave.left + x, y1, nullptr);
        LineTo(dc, wave.left + x, y2 + 1);
    }
    SelectObject(dc, oldPen);
    DeleteObject(pen);
    const DetectionPreviewDelta delta = compareDetectionProposals(before, after);
    // Giallo = mantenuto, rosso = rimosso (sopra), verde = mantenuto,
    // azzurro = aggiunto (sotto). E' un confronto visivo immediato.
    drawRegions(dc, wave, window, before, delta.beforeKept,
                RGB(214, 176, 64), RGB(190, 72, 72), true);
    drawRegions(dc, wave, window, after, delta.afterKept,
                RGB(64, 170, 108), RGB(70, 158, 214), false);

    RECT countsArea = {8, client.bottom - 20, client.right - 8, client.bottom - 2};
    SetTextColor(dc, RGB(180, 190, 202));
    DrawTextW(dc, counts.c_str(), -1, &countsArea, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
}

#endif

} // namespace bc

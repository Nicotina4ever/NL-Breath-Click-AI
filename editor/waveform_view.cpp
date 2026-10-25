// SPDX-License-Identifier: MPL-2.0
#include "waveform_view.h"

#ifdef _WIN32

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windowsx.h>

#include "editor_commands.h"

namespace bc {

namespace {

constexpr wchar_t kViewClass[] = L"BreathClickWaveformView";

// Spaziatura del righello: si sceglie il passo piu' piccolo che lascia almeno
// `minPixelGap` pixel fra due tacche, cosi' le etichette non si accavallano a
// nessun livello di zoom.
const double kTickSteps[] = {0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.2,
                             0.5, 1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0,
                             120.0, 300.0, 600.0};

std::wstring formatTime(double seconds, bool withMilliseconds) {
    if (seconds < 0.0) seconds = 0.0;

    const int totalMinutes = static_cast<int>(seconds / 60.0);
    const double rest = seconds - totalMinutes * 60.0;

    wchar_t buffer[64] = {};
    if (withMilliseconds && seconds < 60.0) {
        swprintf(buffer, 64, L"%.3f s", rest);
    } else if (withMilliseconds) {
        swprintf(buffer, 64, L"%d:%06.3f", totalMinutes, rest);
    } else {
        swprintf(buffer, 64, L"%d:%02d", totalMinutes, static_cast<int>(rest));
    }
    return buffer;
}

int clampInt(int value, int low, int high) {
    return value < low ? low : (value > high ? high : value);
}

} // namespace

// ===========================================================================
//  PeakCache
// ===========================================================================
void PeakCache::build(const AudioBuffer& buffer, int64_t bucketFrames) {
    clear();

    const int channels = std::max(1, buffer.channels);
    const int64_t frames = static_cast<int64_t>(buffer.samples.size()) / channels;
    if (frames <= 0) return;

    bucketFrames_ = std::max<int64_t>(1, bucketFrames);
    channels_ = channels;
    buckets_ = (frames + bucketFrames_ - 1) / bucketFrames_;

    minima_.assign(static_cast<size_t>(buckets_ * channels), 0.0f);
    maxima_.assign(static_cast<size_t>(buckets_ * channels), 0.0f);

    for (int64_t bucket = 0; bucket < buckets_; ++bucket) {
        const int64_t start = bucket * bucketFrames_;
        const int64_t end = std::min(frames, start + bucketFrames_);

        for (int channel = 0; channel < channels; ++channel) {
            float minimum = 0.0f;
            float maximum = 0.0f;
            bool first = true;

            for (int64_t frame = start; frame < end; ++frame) {
                const float value = buffer.samples[static_cast<size_t>(frame * channels + channel)];
                if (first) {
                    minimum = maximum = value;
                    first = false;
                } else {
                    minimum = std::min(minimum, value);
                    maximum = std::max(maximum, value);
                }
            }

            minima_[static_cast<size_t>(bucket * channels + channel)] = minimum;
            maxima_[static_cast<size_t>(bucket * channels + channel)] = maximum;
        }
    }
}

void PeakCache::clear() {
    minima_.clear();
    maxima_.clear();
    buckets_ = 0;
    channels_ = 1;
}

float PeakCache::minimum(int64_t bucket, int channel) const {
    if (bucket < 0 || bucket >= buckets_ || channel < 0 || channel >= channels_) return 0.0f;
    return minima_[static_cast<size_t>(bucket * channels_ + channel)];
}

float PeakCache::maximum(int64_t bucket, int channel) const {
    if (bucket < 0 || bucket >= buckets_ || channel < 0 || channel >= channels_) return 0.0f;
    return maxima_[static_cast<size_t>(bucket * channels_ + channel)];
}

// ===========================================================================
//  WaveformView
// ===========================================================================
WaveformView::WaveformView() = default;

WaveformView::~WaveformView() {
    if (backBufferBitmap_) DeleteObject(backBufferBitmap_);
    if (backBufferDc_) DeleteDC(backBufferDc_);
    if (smallFont_) DeleteObject(smallFont_);
    if (tinyFont_) DeleteObject(tinyFont_);
    if (window_) DestroyWindow(window_);
}

bool WaveformView::create(HWND parent, int controlId, HINSTANCE instance) {
    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = &WaveformView::windowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.hbrBackground = nullptr;
    windowClass.lpszClassName = kViewClass;
    RegisterClassExW(&windowClass);   // puo' esistere gia' da un'altra istanza

    window_ = CreateWindowExW(0, kViewClass, L"",
                              WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPCHILDREN,
                              0, 0, 100, 100,
                              parent,
                              reinterpret_cast<HMENU>(static_cast<INT_PTR>(controlId)),
                              instance, this);
    if (!window_) {
        return false;
    }

    smallFont_ = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    tinyFont_ = CreateFontW(-11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    return true;
}

LRESULT CALLBACK WaveformView::windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    WaveformView* view = reinterpret_cast<WaveformView*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (message == WM_NCCREATE) {
        const CREATESTRUCTW* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        view = static_cast<WaveformView*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(view));
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    if (!view) {
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    return view->handleMessage(message, wParam, lParam);
}

void WaveformView::setDocument(AudioDocument* document) {
    document_ = document;
    selectedRegion_ = -1;
    hoveredRegion_ = -1;
    clearSelection();
    playheadFrame_ = 0;
    refreshDocument(true);
    zoomToFit();
}

void WaveformView::refreshDocument(bool rebuildCache) {
    if (rebuildCache) {
        if (document_ && document_->isLoaded()) {
            // La cache scende di livello quando il file e' molto lungo, cosi' la
            // memoria resta contenuta anche su brani di mezz'ora.
            const int64_t frames = document_->frameCount();
            int64_t bucket = 128;
            while (frames / bucket > 4 * 1024 * 1024 && bucket < 16384) {
                bucket *= 2;
            }
            peaks_.build(document_->audio(), bucket);
        } else {
            peaks_.clear();
        }
    }
    refreshGainCurve();
    invalidate();
}

void WaveformView::refreshGainCurve() {
    if (document_ && document_->isLoaded()) {
        EditSettings settings;
        gainCurve_ = document_->gainCurve(settings, gainCurveMode_);
    } else {
        gainCurve_.clear();
    }
}

void WaveformView::invalidate() {
    if (window_) {
        InvalidateRect(window_, nullptr, FALSE);
    }
}

int64_t WaveformView::visibleFrameCount() const {
    if (width_ <= 0) return 0;
    return static_cast<int64_t>(std::llround(framesPerPixel_ * width_));
}

int64_t WaveformView::frameAtX(int x) const {
    return firstFrame_ + static_cast<int64_t>(std::llround(x * framesPerPixel_));
}

int WaveformView::xAtFrame(int64_t frame) const {
    if (framesPerPixel_ <= 0.0) return 0;
    return static_cast<int>(std::llround((static_cast<double>(frame) - firstFrame_) / framesPerPixel_));
}

void WaveformView::clampView() {
    if (!document_ || !document_->isLoaded()) {
        firstFrame_ = 0;
        return;
    }

    const int64_t frames = document_->frameCount();
    framesPerPixel_ = std::max(0.01, framesPerPixel_);
    framesPerPixel_ = std::min<double>(framesPerPixel_, std::max<double>(1.0, frames));

    const int64_t visible = visibleFrameCount();
    const int64_t maximumFirst = std::max<int64_t>(0, frames - visible);
    firstFrame_ = std::max<int64_t>(0, std::min(firstFrame_, maximumFirst));
}

void WaveformView::zoomToFit() {
    if (!document_ || !document_->isLoaded() || width_ <= 0) {
        framesPerPixel_ = 32.0;
        firstFrame_ = 0;
        return;
    }

    const int64_t frames = std::max<int64_t>(1, document_->frameCount());
    framesPerPixel_ = static_cast<double>(frames) / static_cast<double>(width_);
    firstFrame_ = 0;
    invalidate();

    if (onViewChanged) onViewChanged();
}

void WaveformView::zoomAround(double factor, int64_t anchorFrame) {
    const double before = framesPerPixel_;
    framesPerPixel_ = std::max(0.01, std::min(before * factor, 100000.0));

    // Il frame sotto il puntatore deve restare sotto il puntatore.
    const double anchorX = (static_cast<double>(anchorFrame) - firstFrame_) / before;
    firstFrame_ = static_cast<int64_t>(std::llround(anchorFrame - anchorX * framesPerPixel_));

    clampView();
    invalidate();
    if (onViewChanged) onViewChanged();
}

void WaveformView::setVerticalZoom(double zoom) {
    verticalZoom_ = std::max(1.0, std::min(8.0, zoom));
    invalidate();
}

void WaveformView::scrollByPixels(int pixels) {
    firstFrame_ += static_cast<int64_t>(std::llround(pixels * framesPerPixel_));
    clampView();
    invalidate();
    if (onViewChanged) onViewChanged();
}

void WaveformView::ensureFrameVisible(int64_t frame) {
    if (!document_ || !document_->isLoaded()) return;
    const int64_t visible = visibleFrameCount();
    if (frame < firstFrame_ || frame >= firstFrame_ + visible) {
        firstFrame_ = frame - visible / 2;
        clampView();
        invalidate();
        if (onViewChanged) onViewChanged();
    }
}

void WaveformView::setPlayhead(int64_t frame) {
    playheadFrame_ = frame;
    invalidate();
}

void WaveformView::setSelectedRegion(int index) {
    selectedRegion_ = index;
    selectedRegions_.clear();
    if (index >= 0) selectedRegions_.push_back(index);
    invalidate();
}

void WaveformView::toggleSelectedRegion(int index) {
    if (index < 0 || !document_ || index >= static_cast<int>(document_->regions().size())) return;
    auto it = std::find(selectedRegions_.begin(), selectedRegions_.end(), index);
    if (it == selectedRegions_.end()) selectedRegions_.push_back(index);
    else selectedRegions_.erase(it);
    selectedRegion_ = selectedRegions_.empty() ? -1 : selectedRegions_.back();
    invalidate();
}

void WaveformView::selectAllRegions() {
    selectedRegions_.clear();
    if (document_) {
        for (size_t i = 0; i < document_->regions().size(); ++i) {
            if (regionVisible(static_cast<int>(i))) selectedRegions_.push_back(static_cast<int>(i));
        }
    }
    selectedRegion_ = selectedRegions_.empty() ? -1 : selectedRegions_.front();
    invalidate();
}

void WaveformView::clearRegionSelection() {
    selectedRegions_.clear();
    selectedRegion_ = -1;
    invalidate();
}

bool WaveformView::isRegionSelected(int index) const {
    return std::find(selectedRegions_.begin(), selectedRegions_.end(), index) !=
           selectedRegions_.end();
}

void WaveformView::setSelection(int64_t start, int64_t end) {
    if (end < start) std::swap(start, end);
    selectionStart_ = std::max<int64_t>(0, start);
    selectionEnd_ = std::max<int64_t>(0, end);
    if (document_ && document_->isLoaded()) {
        selectionEnd_ = std::min(selectionEnd_, document_->frameCount());
    }
    invalidate();
    if (onSelectionChanged) onSelectionChanged();
}

void WaveformView::clearSelection() {
    selectionStart_ = 0;
    selectionEnd_ = 0;
    invalidate();
    if (onSelectionChanged) onSelectionChanged();
}

// ---------------------------------------------------------------------------
//  Disegno
// ---------------------------------------------------------------------------
void WaveformView::ensureBackBuffer(int width, int height) {
    if (width <= 0 || height <= 0) return;
    if (backBufferDc_ && width == backBufferWidth_ && height == backBufferHeight_) return;

    if (backBufferBitmap_) {
        DeleteObject(backBufferBitmap_);
        backBufferBitmap_ = nullptr;
    }
    if (backBufferDc_) {
        DeleteDC(backBufferDc_);
        backBufferDc_ = nullptr;
    }

    HDC screen = GetDC(window_);
    backBufferDc_ = CreateCompatibleDC(screen);
    backBufferBitmap_ = CreateCompatibleBitmap(screen, width, height);
    ReleaseDC(window_, screen);

    if (backBufferDc_ && backBufferBitmap_) {
        SelectObject(backBufferDc_, backBufferBitmap_);
        backBufferWidth_ = width;
        backBufferHeight_ = height;
    }
}

void WaveformView::drawRuler(HDC dc, const RECT& client) {
    RECT ruler = {0, 0, client.right, kRulerHeight};
    HBRUSH background = CreateSolidBrush(RGB(20, 23, 28));
    FillRect(dc, &ruler, background);
    DeleteObject(background);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, EditorColors::kTextDim);
    SelectObject(dc, tinyFont_);

    const double sampleRate = document_ && document_->isLoaded() ? document_->sampleRate() : 48000.0;
    const double secondsPerPixel = framesPerPixel_ / sampleRate;

    // Passo piu' piccolo che lascia almeno 70 pixel fra due tacche.
    double step = kTickSteps[0];
    for (double candidate : kTickSteps) {
        step = candidate;
        if (candidate / secondsPerPixel >= 70.0) break;
    }

    const bool withMilliseconds = step < 1.0;
    const double startSeconds = static_cast<double>(firstFrame_) / sampleRate;
    const double endSeconds = static_cast<double>(firstFrame_ + visibleFrameCount()) / sampleRate;

    const double firstTick = std::floor(startSeconds / step) * step;
    for (double time = firstTick; time <= endSeconds + step; time += step) {
        if (time < 0.0) continue;
        const int x = xAtFrame(static_cast<int64_t>(std::llround(time * sampleRate)));
        if (x < 0 || x > client.right) continue;

        HPEN pen = CreatePen(PS_SOLID, 1, EditorColors::kGrid);
        HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen));
        MoveToEx(dc, x, kRulerHeight - 6, nullptr);
        LineTo(dc, x, kRulerHeight);
        SelectObject(dc, oldPen);
        DeleteObject(pen);

        const std::wstring label = formatTime(time, withMilliseconds);
        TextOutW(dc, x + 3, 3, label.c_str(), static_cast<int>(label.size()));
    }
}

void WaveformView::drawWaveform(HDC dc, const RECT& area) {
    // Sfondo delle corsie: va riempito SEMPRE, anche senza documento. Se si
    // esce prima, il buffer fuori schermo resta non inizializzato e la finestra
    // appare nera senza spiegazione.
    HBRUSH laneBrush = CreateSolidBrush(EditorColors::kLaneBackground);
    FillRect(dc, &area, laneBrush);
    DeleteObject(laneBrush);
    drawRegions(dc, area, true);
    drawSelection(dc, area);

    if (!document_ || !document_->isLoaded() || peaks_.empty()) {
        // Invece del vuoto, si dice cosa fare: una finestra completamente nera
        // non fa capire se il programma e' rotto o se manca solo il file.
        SetBkMode(dc, TRANSPARENT);
        SelectObject(dc, smallFont_);

        const wchar_t* line1 = L"Nessun file aperto";
        const wchar_t* line2 = L"Ctrl+O per aprire un file WAV, oppure trascinalo sulla finestra";

        RECT first = {area.left, area.top, area.right, area.bottom};
        SetTextColor(dc, EditorColors::kText);
        DrawTextW(dc, line1, -1, &first,
                  DT_CENTER | DT_SINGLELINE | DT_VCENTER);

        RECT second = {area.left, area.top + 26, area.right, area.bottom + 26};
        SetTextColor(dc, EditorColors::kTextDim);
        SelectObject(dc, tinyFont_);
        DrawTextW(dc, line2, -1, &second,
                  DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        return;
    }

    const AudioBuffer& audio = document_->audio();
    const int channels = std::max(1, audio.channels);
    const int lanes = std::min(channels, 2);
    // I membri di RECT sono LONG: senza conversione esplicita std::max non
    // trova un overload comune fra int e long.
    const int laneHeight = std::max(1, static_cast<int>(area.bottom - area.top) / lanes);

    // Linea dello zero per ogni corsia.
    HPEN zeroPen = CreatePen(PS_SOLID, 1, EditorColors::kGrid);
    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, zeroPen));
    for (int lane = 0; lane < lanes; ++lane) {
        const int centerY = area.top + lane * laneHeight + laneHeight / 2;
        MoveToEx(dc, area.left, centerY, nullptr);
        LineTo(dc, area.right, centerY);
    }
    SelectObject(dc, oldPen);
    DeleteObject(zeroPen);

    const int64_t bucketFrames = peaks_.bucketFrames();
    const bool drawSamples = framesPerPixel_ <= 3.0;

    HPEN wavePen = CreatePen(PS_SOLID, 1, EditorColors::kWaveform);
    oldPen = static_cast<HPEN>(SelectObject(dc, wavePen));

    for (int lane = 0; lane < lanes; ++lane) {
        const int channel = lanes == 1 ? 0 : lane;
        const int laneTop = area.top + lane * laneHeight;
        const int laneCenter = laneTop + laneHeight / 2;
        const int halfHeight = std::max(1, laneHeight / 2 - 4);

        if (drawSamples && channels > 0) {
            // Zoom ravvicinato: si disegna il segnale vero campione per campione.
            const int64_t firstSample = std::max<int64_t>(0, firstFrame_);
            const int64_t lastSample = std::min<int64_t>(document_->frameCount(),
                                                         firstFrame_ + visibleFrameCount() + 2);
            bool started = false;

            for (int64_t frame = firstSample; frame < lastSample; ++frame) {
                const int x = xAtFrame(frame);
                if (x < area.left - 1 || x > area.right + 1) continue;

                const float value = audio.samples[static_cast<size_t>(frame * channels + channel)];
                const float clamped = std::max(-1.0f, std::min(1.0f,
                    value * static_cast<float>(verticalZoom_)));
                const int y = laneCenter - static_cast<int>(std::lround(clamped * halfHeight));

                if (!started) {
                    MoveToEx(dc, x, y, nullptr);
                    started = true;
                } else {
                    LineTo(dc, x, y);
                }
            }
        } else {
            // Zoom ampio: un valore minimo/massimo per pixel dalla cache.
            for (int x = area.left; x < area.right; ++x) {
                const int64_t startFrame = frameAtX(x);
                const int64_t endFrame = startFrame + static_cast<int64_t>(framesPerPixel_);
                if (endFrame < 0 || startFrame > document_->frameCount()) continue;

                const int64_t firstBucket = startFrame / bucketFrames;
                const int64_t lastBucket = std::max(firstBucket, (endFrame - 1) / bucketFrames);

                float minimum = 0.0f;
                float maximum = 0.0f;
                bool first = true;

                for (int64_t bucket = firstBucket; bucket <= lastBucket; ++bucket) {
                    const float bucketMin = peaks_.minimum(bucket, channel);
                    const float bucketMax = peaks_.maximum(bucket, channel);
                    if (first) {
                        minimum = bucketMin;
                        maximum = bucketMax;
                        first = false;
                    } else {
                        minimum = std::min(minimum, bucketMin);
                        maximum = std::max(maximum, bucketMax);
                    }
                }

                const float lowClamped = std::max(-1.0f, std::min(1.0f,
                    minimum * static_cast<float>(verticalZoom_)));
                const float highClamped = std::max(-1.0f, std::min(1.0f,
                    maximum * static_cast<float>(verticalZoom_)));
                int y1 = laneCenter - static_cast<int>(std::lround(highClamped * halfHeight));
                int y2 = laneCenter - static_cast<int>(std::lround(lowClamped * halfHeight));
                if (y1 > y2) std::swap(y1, y2);
                if (y1 == y2) --y1;   // almeno un pixel, altrimenti il silenzio sparisce

                MoveToEx(dc, x, y1, nullptr);
                LineTo(dc, x, y2 + 1);
            }
        }
    }

    SelectObject(dc, oldPen);
    DeleteObject(wavePen);
}

void WaveformView::drawGainCurve(HDC dc, const RECT& area) {
    if (gainCurve_.empty() || !document_) return;

    const int64_t frames = document_->frameCount();
    if (frames <= 0) return;

    HPEN pen = CreatePen(PS_SOLID, 1, EditorColors::kGainCurve);
    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen));

    // La curva e' disegnata in alto: guadagno 1.0 al bordo superiore, 0.0 in
    // fondo. Cosi' un'attenuazione si vede come un avvallamento.
    const int top = area.top + 2;
    const int bottom = area.bottom - 2;
    const int span = std::max(1, bottom - top);

    bool started = false;
    for (int x = area.left; x < area.right; ++x) {
        const int64_t frame = frameAtX(x);
        if (frame < 0 || frame >= frames) {
            started = false;
            continue;
        }
        const float gain = std::max(0.0f, std::min(1.0f, gainCurve_[static_cast<size_t>(frame)]));
        const int y = bottom - static_cast<int>(std::lround(gain * span));

        if (!started) {
            MoveToEx(dc, x, y, nullptr);
            started = true;
        } else {
            LineTo(dc, x, y);
        }
    }

    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

void WaveformView::drawRegions(HDC dc, const RECT& area, bool backgroundOnly) {
    if (!document_ || !document_->isLoaded()) return;

    const std::vector<EditRegion>& regions = document_->regions();
    const int selectionCount = static_cast<int>(selectedRegions_.size());

    // Le regioni selezionate si disegnano DOPO le altre: se restassero sotto,
    // il riempimento della regione coprirebbe la selezione (il difetto visto
    // nell'uso reale). Due passate, non un colore leggermente diverso.
    auto drawOne = [&](size_t index, bool selected) {
        if (!regionVisible(static_cast<int>(index))) return;
        const EditRegion& region = regions[index];
        const int x1 = xAtFrame(region.startSample);
        const int x2 = xAtFrame(region.endSample);
        if (x2 < area.left - 2 || x1 > area.right + 2) return;

        int left = std::max(static_cast<int>(area.left), x1);
        int right = std::min(static_cast<int>(area.right), std::max(x2, x1 + 1));
        if (right <= left) {
            if (!selected) return;
            right = std::min(static_cast<int>(area.right), left + 2);
            if (right <= left) return;
        }

        const bool isBreath = region.kind == DetectionKind::Breath;
        const bool isIncluded = region.enabled && region.approved;
        const bool multi = selected && selectionCount > 1;

        COLORREF fillColor = isBreath ? EditorColors::kBreathSoft : EditorColors::kClickSoft;
        COLORREF edgeColor = isBreath ? EditorColors::kBreath : EditorColors::kClick;
        if (region.kind==DetectionKind::Click && region.classification=="mouth_click") {
            fillColor=RGB(78,35,77);edgeColor=RGB(220,115,220);
        }
        if (region.manualOperation) {
            fillColor = RGB(28, 70, 82); edgeColor = RGB(90, 210, 220);
        }
        if (region.applied && isIncluded) {
            fillColor = RGB(38, 86, 56);
            edgeColor = RGB(86, 196, 112);
        } else if (!isIncluded) {
            fillColor = RGB(58, 62, 70);
            edgeColor = RGB(150, 156, 168);
        }
        if (selected) {
            fillColor = multi ? EditorColors::kSelectionMulti : EditorColors::kSelectionSingle;
            edgeColor = multi ? EditorColors::kSelectionMultiEdge : EditorColors::kSelectionEdge;
        }

        if (backgroundOnly) {
            RECT rect = {left, area.top, right, area.bottom};
            HBRUSH brush = CreateSolidBrush(fillColor);
            FillRect(dc, &rect, brush);
            DeleteObject(brush);
            return;
        }

        // Banda superiore di stato: respiro/click e incluso/escluso restano
        // leggibili anche su waveform molto fitte.
        RECT topBand = {left, area.top, right, area.top + (selected ? 5 : 2)};
        HBRUSH brush = CreateSolidBrush(edgeColor);
        FillRect(dc, &topBand, brush);
        DeleteObject(brush);

        const int edgeWidth = selected ? 3 : 1;
        RECT leftEdge = {left, area.top, left + edgeWidth, area.bottom};
        RECT rightEdge = {std::max(left, right - edgeWidth), area.top, right, area.bottom};
        brush = CreateSolidBrush(edgeColor);
        FillRect(dc, &leftEdge, brush);
        FillRect(dc, &rightEdge, brush);
        DeleteObject(brush);

        if (selected) {
            // Cornice esterna luminosa (2 px) e maniglie: distinguibile anche
            // senza colore e su monitor a DPI alto.
            HPEN outerPen = CreatePen(PS_SOLID, 2, RGB(250, 253, 255));
            HPEN oldPen = static_cast<HPEN>(SelectObject(dc, outerPen));
            HBRUSH hollow = static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
            HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(dc, hollow));
            Rectangle(dc, left - 2, area.top, right + 2, area.bottom - 1);
            SelectObject(dc, oldBrush);
            SelectObject(dc, oldPen);
            DeleteObject(outerPen);

            const int handleW = 7;
            const int handleH = 14;
            RECT leftHandle = {left - handleW / 2, area.top + 1,
                               left + handleW / 2, area.top + 1 + handleH};
            RECT rightHandle = {right - handleW / 2, area.top + 1,
                                right + handleW / 2, area.top + 1 + handleH};
            RECT leftHandleBottom = {left - handleW / 2, area.bottom - handleH - 1,
                                     left + handleW / 2, area.bottom - 1};
            RECT rightHandleBottom = {right - handleW / 2, area.bottom - handleH - 1,
                                      right + handleW / 2, area.bottom - 1};
            HBRUSH handleBrush = CreateSolidBrush(EditorColors::kSelectionHandle);
            FillRect(dc, &leftHandle, handleBrush);
            FillRect(dc, &rightHandle, handleBrush);
            FillRect(dc, &leftHandleBottom, handleBrush);
            FillRect(dc, &rightHandleBottom, handleBrush);
            DeleteObject(handleBrush);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(10, 16, 22));
            SelectObject(dc, tinyFont_);
            const wchar_t* handleMark = multi ? L"+" : L"↔";
            TextOutW(dc, left + 3, area.top + 7, handleMark, 1);
        }

        // Stato Incluso/Escluso sempre esplicito, non affidato al solo colore.
        if (right - left >= 34) {
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, EditorColors::kText);
            SelectObject(dc, tinyFont_);
            wchar_t label[96] = {};
            const bool repaired = region.kind == DetectionKind::Click && region.repair;
            const wchar_t* state = !isIncluded ? L"ESCLUSO" : L"INCLUSO";
            if (region.manualOperation) {
                const wchar_t* modes[] = {L"ATTENUA", L"TAGLIA", L"DIMEZZA", L"SILENZIA"};
                swprintf(label, 96, L"%s %s", modes[std::clamp(region.editMode, 0, 3)], state);
            } else if (selected) {
                swprintf(label, 96, multi ? L"SEL %s" : L"SELEZIONATO %s", state);
            } else if (repaired) {
                swprintf(label, 96, L"click riparato");
            } else if (region.applied) {
                swprintf(label, 96, L"OK applicato");
            } else if (region.trainingEligible) {
                swprintf(label, 96, L"OK confermato");
            } else if (!region.fromDetection) {
                swprintf(label, 96, L"OK manuale");
            } else {
                swprintf(label, 96, L"OK incluso");
            }
            TextOutW(dc, left + 4, area.top + 7, label, static_cast<int>(wcslen(label)));
        }
    };

    for (size_t i = 0; i < regions.size(); ++i) {
        if (!isRegionSelected(static_cast<int>(i))) drawOne(i, false);
    }
    for (size_t i = 0; i < regions.size(); ++i) {
        if (isRegionSelected(static_cast<int>(i))) drawOne(i, true);
    }
}

void WaveformView::drawSelection(HDC dc, const RECT& area, bool outline) {
    if (!hasSelection()) return;

    const int x1 = xAtFrame(selectionStart_);
    const int x2 = xAtFrame(selectionEnd_);
    const int left = std::max(static_cast<int>(area.left), x1);
    const int right = std::min(static_cast<int>(area.right), x2);
    if (right <= left) return;

    RECT rect = {left, area.top, right, area.bottom};
    if (outline) {
        RECT band = {left, area.bottom - dpiPixels(8), right, area.bottom};
        HBRUSH bandBrush = CreateSolidBrush(EditorColors::kSelection);
        FillRect(dc, &band, bandBrush); DeleteObject(bandBrush);
        HPEN pen = CreatePen(PS_SOLID, dpiPixels(2), EditorColors::kSelectionEdge);
        HPEN previous = static_cast<HPEN>(SelectObject(dc, pen));
        for (int x : {x1, x2}) {
            MoveToEx(dc, x, area.top, nullptr); LineTo(dc, x, area.bottom);
            RECT handle = {x - dpiPixels(3), area.top, x + dpiPixels(4), area.top + dpiPixels(12)};
            HBRUSH brush = CreateSolidBrush(EditorColors::kSelectionHandle);
            FillRect(dc, &handle, brush); DeleteObject(brush);
        }
        SelectObject(dc, previous); DeleteObject(pen);
        return;
    }
    HBRUSH brush = CreateSolidBrush(EditorColors::kSelection);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

int WaveformView::dpiPixels(int logical) const {
    HDC dc = GetDC(window_);
    const int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc) ReleaseDC(window_, dc);
    return std::max(1, MulDiv(logical, dpi, 96));
}

bool WaveformView::regionVisible(int index) const {
    if (!document_ || index < 0 || index >= static_cast<int>(document_->regions().size())) return false;
    const auto& r = document_->regions()[index];
    switch (regionFilter_) {
        case 1: return r.kind == DetectionKind::Breath;
        case 2: return r.kind == DetectionKind::Click;
        case 3: return !r.fromDetection;
        case 4: return r.fromDetection;
        case 5: return !r.enabled || !r.approved;
        case 7: return r.kind==DetectionKind::Click && r.classification=="digital_click";
        case 8: return r.kind==DetectionKind::Click && (r.classification=="saliva_click" || r.classification=="mouth_click");
        case 9: return r.learningLabel=="false_positive" || r.learningLabel=="click_false_positive";
        case 6: return r.fromDetection && !r.trainingEligible &&
            (r.classification == "dsp" || r.score < .5f || clickReviewRequired(r.classification));
        default: return true;
    }
}

void WaveformView::setRegionFilter(int filter) {
    regionFilter_ = std::clamp(filter, 0, 9);
    clearRegionSelection(); invalidate();
    if (onRegionSelected) onRegionSelected(-1);
}

void WaveformView::selectRegionsInRange(int64_t start, int64_t end) {
    selectedRegions_.clear(); selectedRegion_ = -1;
    if (document_) for (int i = 0; i < static_cast<int>(document_->regions().size()); ++i) {
        const auto& r = document_->regions()[i];
        if (regionVisible(i) && r.startSample < end && r.endSample > start) {
            selectedRegions_.push_back(i); selectedRegion_ = i;
        }
    }
    invalidate();
}

void WaveformView::drawPlayhead(HDC dc, const RECT& area) {
    const int x = xAtFrame(playheadFrame_);
    if (x < area.left || x > area.right) return;

    HPEN pen = CreatePen(PS_SOLID, 1, EditorColors::kPlayhead);
    HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen));
    MoveToEx(dc, x, 0, nullptr);
    LineTo(dc, x, area.bottom);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

void WaveformView::paint(HDC target, const RECT& client) {
    ensureBackBuffer(client.right, client.bottom);
    if (!backBufferDc_) return;

    HDC dc = backBufferDc_;
    RECT area = {0, kRulerHeight, client.right, client.bottom};

    // Region backgrounds and time selection are drawn inside drawWaveform,
    // before samples and gain. Foreground borders/labels cannot hide the audio.
    drawWaveform(dc, area);
    drawGainCurve(dc, area);
    drawRegions(dc, area);
    drawSelection(dc, area, true);
    drawRuler(dc, client);
    drawPlayhead(dc, area);

    BitBlt(target, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
}

// ---------------------------------------------------------------------------
//  Interazione
// ---------------------------------------------------------------------------
void WaveformView::beginDrag(int x, int y, UINT modifiers) {
    if (!document_ || !document_->isLoaded()) return;

    dragMoved_ = false;

    // Nel righello si trascina la testina.
    if (y < kRulerHeight) {
        dragMode_ = DragMode::Playhead;
        updateDrag(x, y);
        return;
    }

    const int64_t frame = frameAtX(x);
    int edge = 0;
    int insideIndex = -1;
    for (int i = static_cast<int>(document_->regions().size()) - 1; i >= 0; --i) {
        const auto& r = document_->regions()[i];
        if (regionVisible(i) && frame >= r.startSample && frame < r.endSample) { insideIndex = i; break; }
    }
    if (timeSelectionTool_) {
        if (hasSelection() && (std::abs(x - xAtFrame(selectionStart_)) <= dpiPixels(6) ||
                               std::abs(x - xAtFrame(selectionEnd_)) <= dpiPixels(6))) {
            dragAnchor_ = std::abs(x - xAtFrame(selectionStart_)) < std::abs(x - xAtFrame(selectionEnd_))
                ? selectionEnd_ : selectionStart_;
            dragMode_ = DragMode::SelectionEdge;
        } else {
            clearRegionSelection();
            if (onRegionSelected) onRegionSelected(-1);
            dragMode_ = DragMode::Select; dragAnchor_ = frame; setSelection(frame, frame);
        }
        return;
    }
    if (insideIndex >= 0 && (modifiers & MK_SHIFT) && selectedRegion_ >= 0) {
        const auto& anchor = document_->regions()[static_cast<size_t>(selectedRegion_)];
        const auto& target = document_->regions()[static_cast<size_t>(insideIndex)];
        selectRegionsInRange(std::min(anchor.startSample, target.startSample), std::max(anchor.endSample, target.endSample));
        if (onRegionSelected) onRegionSelected(selectedRegion_);
        return;
    }

    // Un respiro molto corto, a zoom "adatta", occupa pochi pixel: se si
    // riservasse la tolleranza dei bordi a tutta la regione sarebbe
    // impossibile afferrarne il centro. La tolleranza dei bordi si usa solo
    // quando la regione e' abbastanza larga sullo schermo.
    int edgeIndex = -1;
    if (insideIndex >= 0) {
        const EditRegion& region = document_->regions()[static_cast<size_t>(insideIndex)];
        const double widthPixels = static_cast<double>(region.endSample - region.startSample) /
                                   std::max(0.01, framesPerPixel_);
        if (widthPixels >= 10.0) {
            const int64_t tolerance = static_cast<int64_t>(std::llround(4.0 * framesPerPixel_));
            edgeIndex = document_->regionIndexNearEdge(frame, tolerance, &edge);
            if (edgeIndex != insideIndex) edgeIndex = -1;
        }
    } else {
        const int64_t tolerance = static_cast<int64_t>(std::llround(4.0 * framesPerPixel_));
        edgeIndex = document_->regionIndexNearEdge(frame, tolerance, &edge);
    }

    if (edgeIndex >= 0) {
        if (!regionVisible(edgeIndex)) edgeIndex = -1;
    }
    if (edgeIndex >= 0) {
        dragMode_ = DragMode::MoveEdge;
        dragRegion_ = edgeIndex;
        dragEdge_ = edge;
        setSelectedRegion(edgeIndex);
        document_->beginUndoGroup(edge < 0 ? "Modifica inizio regione" : "Modifica fine regione");
        dragHistoryOpen_ = true;
        if (onRegionSelected) onRegionSelected(edgeIndex);
        invalidate();
        return;
    }

    // Clic o trascinamento sulla parte centrale di una regione: la seleziona
    // subito e prepara lo spostamento. Non serve Alt: l'utente deve poter
    // cliccare un respiro e vederlo selezionato.
    if (insideIndex >= 0) {
        if (modifiers & MK_CONTROL) {
            toggleSelectedRegion(insideIndex);
            if (onRegionSelected) onRegionSelected(selectedRegion_);
            return;
        }
        setSelectedRegion(insideIndex);
        dragMode_ = DragMode::MoveRegion;
        dragRegion_ = insideIndex;
        dragOriginalStart_ = document_->regions()[static_cast<size_t>(insideIndex)].startSample;
        dragOriginalEnd_ = document_->regions()[static_cast<size_t>(insideIndex)].endSample;
        dragGrabOffset_ = frame - dragOriginalStart_;
        document_->beginUndoGroup("Sposta regione");
        dragHistoryOpen_ = true;
        if (onRegionSelected) onRegionSelected(insideIndex);
        invalidate();
        return;
    }

    // Zona libera: si seleziona l'audio. La selezione delle regioni viene
    // azzerata per non lasciare un evento evidenziato mentre si sceglie un
    // intervallo diverso.
    dragMode_ = DragMode::Select;
    clearRegionSelection();
    if (onRegionSelected) onRegionSelected(-1);
    dragAnchor_ = frame;
    setSelection(frame, frame);
}

void WaveformView::updateDrag(int x, int /*y*/) {
    if (dragMode_ == DragMode::None) return;

    const int64_t frame = std::max<int64_t>(0, frameAtX(x));
    const int64_t clamped = document_ && document_->isLoaded()
                                ? std::min(frame, document_->frameCount())
                                : frame;

    switch (dragMode_) {
        case DragMode::Playhead:
            playheadFrame_ = clamped;
            invalidate();
            if (onCursorMoved) onCursorMoved(clamped);
            break;

        case DragMode::SelectionEdge:
        case DragMode::Select: {
            // L'ancoraggio e' il punto dove il trascinamento e' iniziato.
            //
            // Prima si usava hasSelection() ? selectionStart_ : clamped, ma
            // dopo beginDrag() la selezione e' vuota (start == end) e quindi
            // hasSelection() e' falso: si ripiegava sulla posizione corrente e
            // la selezione restava di larghezza zero. In pratica trascinare non
            // selezionava nulla.
            setSelection(dragAnchor_, clamped);
            dragMoved_ = true;
            break;
        }

        case DragMode::MoveEdge: {
            if (dragRegion_ < 0 || dragRegion_ >= static_cast<int>(document_->regions().size())) break;
            const EditRegion& region = document_->regions()[static_cast<size_t>(dragRegion_)];
            const int64_t minimumLength = std::max<int64_t>(1, static_cast<int64_t>(0.0002 *
                                              document_->sampleRate()));

            EditRegion updated = region;
            if (dragEdge_ < 0) {
                updated.startSample = std::min(clamped, updated.endSample - minimumLength);
                updated.startSample = std::max<int64_t>(0, updated.startSample);
            } else {
                updated.endSample = std::max(clamped, updated.startSample + minimumLength);
                updated.endSample = std::min(document_->frameCount(), updated.endSample);
            }
            document_->updateRegion(dragRegion_, updated);
            dragMoved_ = true;
            invalidate();
            if (onRegionChanged) onRegionChanged(dragRegion_);
            break;
        }

        case DragMode::MoveRegion: {
            if (dragRegion_ < 0 || dragRegion_ >= static_cast<int>(document_->regions().size())) break;
            const int64_t length = dragOriginalEnd_ - dragOriginalStart_;
            int64_t newStart = clamped - dragGrabOffset_;
            newStart = std::max<int64_t>(0, std::min(newStart, document_->frameCount() - length));

            EditRegion updated = document_->regions()[static_cast<size_t>(dragRegion_)];
            updated.startSample = newStart;
            updated.endSample = newStart + length;
            document_->updateRegion(dragRegion_, updated);
            dragMoved_ = true;
            invalidate();
            if (onRegionChanged) onRegionChanged(dragRegion_);
            break;
        }

        default:
            break;
    }

    // Scorrimento automatico quando si trascina oltre il bordo.
    if (x < 12) scrollByPixels(-8);
    else if (x > width_ - 12) scrollByPixels(8);
}

void WaveformView::endDrag() {
    KillTimer(window_, 71);
    const DragMode finishedMode = dragMode_;
    const int finishedRegion = dragRegion_;
    const bool moved = dragMoved_;
    if (finishedMode == DragMode::Playhead && onCursorMoved) {
        onCursorMoved(playheadFrame_);
    }
    if (dragHistoryOpen_ && document_) {
        document_->endUndoGroup();
    }
    dragHistoryOpen_ = false;
    dragMode_ = DragMode::None;
    dragRegion_ = -1;
    dragMoved_ = false;
    refreshGainCurve();
    invalidate();
    // Solo al rilascio del mouse si notifica la fine dell'editing: durante il
    // trascinamento la GUI non deve ricostruire cronologia e pannelli a ogni
    // pixel. Il documento ha gia' registrato una sola voce di Undo.
    if (moved && (finishedMode == DragMode::MoveRegion || finishedMode == DragMode::MoveEdge) &&
        onRegionChanged && finishedRegion >= 0) {
        onRegionChanged(finishedRegion);
    }
}

LRESULT WaveformView::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_SIZE: {
            width_ = LOWORD(lParam);
            height_ = HIWORD(lParam);
            if (framesPerPixel_ > 0.0 && document_ && document_->isLoaded() &&
                firstFrame_ == 0 && backBufferWidth_ == 0) {
                zoomToFit();
            }
            clampView();
            return 0;
        }

        // Messaggi di sola diagnosi usati dai test GUI per conoscere la
        // trasformazione pixel<->campione senza duplicare la formula nel test.
        case WM_APP + 101:
            return static_cast<LRESULT>(xAtFrame(static_cast<int64_t>(wParam)));
        case WM_APP + 102:
            return static_cast<LRESULT>(frameAtX(static_cast<int>(lParam)));
        case WM_APP + 103:
            // Diagnosi performance: forza un ridisegno completo e sincrono.
            InvalidateRect(window_, nullptr, FALSE);
            UpdateWindow(window_);
            return 0;
        case WM_APP + 104: {
            const auto frame = static_cast<int64_t>(wParam);
            return frame >= 0 && frame < static_cast<int64_t>(gainCurve_.size())
                ? static_cast<LRESULT>(std::llround(gainCurve_[static_cast<size_t>(frame)] * 1000000.)) : -1;
        }

        case WM_PAINT: {
            PAINTSTRUCT paintStruct = {};
            HDC dc = BeginPaint(window_, &paintStruct);
            RECT client = {};
            GetClientRect(window_, &client);
            paint(dc, client);
            EndPaint(window_, &paintStruct);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;   // il disegno e' gia' completo nel buffer fuori schermo

        case WM_LBUTTONDOWN:
            SetFocus(window_);
            SetCapture(window_);
            beginDrag(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), static_cast<UINT>(wParam));
            dragMouseX_ = GET_X_LPARAM(lParam); dragMouseY_ = GET_Y_LPARAM(lParam);
            if (dragMode_ != DragMode::None) SetTimer(window_, 71, 40, nullptr);
            return 0;

        case WM_MOUSEMOVE: {
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);
            dragMouseX_ = x; dragMouseY_ = y;

            if (dragMode_ != DragMode::None) {
                updateDrag(x, y);
                return 0;
            }

            // Il cursore cambia sui bordi delle regioni: e' l'unico modo per far
            // capire che si possono trascinare.
            if (timeSelectionTool_) { SetCursor(LoadCursor(nullptr, IDC_IBEAM)); return 0; }
            if (document_ && document_->isLoaded() && y >= kRulerHeight) {
                const int64_t frame = frameAtX(x);
                const int insideIndex = document_->regionIndexAt(frame);
                int edge = 0;
                int index = -1;
                if (insideIndex >= 0) {
                    const EditRegion& region = document_->regions()[static_cast<size_t>(insideIndex)];
                    const double widthPixels = static_cast<double>(region.endSample - region.startSample) /
                                               std::max(0.01, framesPerPixel_);
                    if (widthPixels >= 10.0) {
                        const int64_t tolerance = static_cast<int64_t>(
                            std::llround(4.0 * framesPerPixel_));
                        index = document_->regionIndexNearEdge(frame, tolerance, &edge);
                        if (index != insideIndex) index = -1;
                    }
                } else {
                    const int64_t tolerance = static_cast<int64_t>(
                        std::llround(4.0 * framesPerPixel_));
                    index = document_->regionIndexNearEdge(frame, tolerance, &edge);
                }
                if (index >= 0) {
                    SetCursor(LoadCursor(nullptr, IDC_SIZEWE));
                } else if (insideIndex >= 0) {
                    SetCursor(LoadCursor(nullptr, IDC_SIZEALL));
                } else {
                    SetCursor(LoadCursor(nullptr, IDC_IBEAM));
                }
            } else {
                SetCursor(LoadCursor(nullptr, IDC_ARROW));
            }
            return 0;
        }

        case WM_LBUTTONUP:
            endDrag();
            if (GetCapture() == window_) ReleaseCapture();
            return 0;

        case WM_TIMER:
            if (wParam == 71 && dragMode_ != DragMode::None &&
                (dragMouseX_ < dpiPixels(12) || dragMouseX_ > width_ - dpiPixels(12)))
                updateDrag(dragMouseX_, dragMouseY_);
            return 0;

        case WM_CAPTURECHANGED:
            if (dragMode_ != DragMode::None) endDrag();
            return 0;

        case WM_CANCELMODE:
            if (dragMode_ != DragMode::None) endDrag();
            if (GetCapture() == window_) ReleaseCapture();
            return 0;

        case WM_LBUTTONDBLCLK: {
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);
            if (y >= kRulerHeight && onDoubleClick) {
                onDoubleClick(std::max<int64_t>(0, frameAtX(x)));
            }
            return 0;
        }

        case WM_RBUTTONUP: {
            // Il menu contestuale lo costruisce la finestra principale, che sa
            // quali comandi sono disponibili.
            const int x = GET_X_LPARAM(lParam);
            const int y = GET_Y_LPARAM(lParam);

            if (document_ && document_->isLoaded()) {
                int index = document_->regionIndexAt(frameAtX(x));
                if (!regionVisible(index) || timeSelectionTool_) index = selectedRegion_;
                if (index >= 0 && !isRegionSelected(index)) setSelectedRegion(index);
                if (onRegionSelected) onRegionSelected(index);
                invalidate();

                POINT screen = {x, y};
                ClientToScreen(window_, &screen);

                // WM_CONTEXTMENU al genitore: la vista non conosce i comandi.
                SendMessageW(GetParent(window_), WM_CONTEXTMENU,
                             reinterpret_cast<WPARAM>(window_),
                             MAKELPARAM(screen.x, screen.y));
            }
            return 0;
        }

        case WM_MOUSEWHEEL: {
            const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            POINT cursor = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(window_, &cursor);

            const bool zoom = (GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL) != 0;
            if (zoom) {
                const int64_t anchor = std::max<int64_t>(0, frameAtX(cursor.x));
                zoomAround(delta > 0 ? 1.0 / 1.25 : 1.25, anchor);
            } else {
                // Una tacca di rotella scorre circa un decimo della vista.
                const int pixels = static_cast<int>(-delta / WHEEL_DELTA * (width_ / 10.0));
                scrollByPixels(pixels);
            }
            return 0;
        }

        case WM_KEYDOWN: {
            // I tasti diventano comandi per la finestra principale: la vista non
            // deve conoscere le operazioni dell'applicazione.
            const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

            int command = 0;
            switch (wParam) {
                case VK_SPACE:   command = EditorCommand::kTransportPlay; break;
                case VK_HOME:    command = EditorCommand::kGoToStart; break;
                case VK_END:     command = EditorCommand::kGoToEnd; break;
                case VK_DELETE:  command = EditorCommand::kEditDeleteSelection; break;
                case VK_ESCAPE:  command = EditorCommand::kEditDeselect; break;
                case 'Z':        if (control) command = shift ? EditorCommand::kEditRedo
                                                              : EditorCommand::kEditUndo; break;
                case 'Y':        if (control) command = EditorCommand::kEditRedo; break;
                case 'A':        if (control) command = EditorCommand::kEditSelectAll; break;
                case 'S':        if (control) command = EditorCommand::kFileSaveAs; break;
                case 'O':        if (control) command = EditorCommand::kFileOpen; break;
                case 'E':        if (control) command = EditorCommand::kFileExport; break;
                case 'R':        if (control) command = EditorCommand::kAnalyze; break;
                case VK_OEM_PLUS:
                case VK_ADD:     command = EditorCommand::kViewZoomIn; break;
                case VK_OEM_MINUS:
                case VK_SUBTRACT: command = EditorCommand::kViewZoomOut; break;
                default: break;
            }

            if (command != 0) {
                SendMessageW(GetParent(window_), WM_COMMAND,
                             MAKEWPARAM(command, 0), reinterpret_cast<LPARAM>(window_));
                return 0;
            }
            break;
        }

        case WM_SETFOCUS:
        case WM_KILLFOCUS:
            invalidate();
            return 0;

        default:
            break;
    }

    return DefWindowProcW(window_, message, wParam, lParam);
}

} // namespace bc

#endif // _WIN32

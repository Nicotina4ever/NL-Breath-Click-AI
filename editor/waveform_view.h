// SPDX-License-Identifier: MPL-2.0
// ============================================================================
//  waveform_view.h — Disegno e interazione della forma d'onda
//
//  Il controllo e' responsabile di: forma d'onda, regioni colorate, curva di
//  guadagno, selezione, testina, righello dei tempi e mouse.
//
//  Il disegno avviene su un buffer fuori schermo: ridisegnare direttamente sul
//  controllo produce sfarfallio a ogni movimento del mouse, e con una forma
//  d'onda piena il lampeggio e' insopportabile.
//
//  Per i file lunghi il disegno usa una cache di picchi: leggere i campioni
//  uno per uno per riempire 1500 pixel su un brano di tre minuti significherebbe
//  scandire 8 milioni di campioni a ogni ridisegno.
// ============================================================================
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "core/edit/audio_document.h"

namespace bc {

// ---------------------------------------------------------------------------
//  Cache dei picchi
// ---------------------------------------------------------------------------
class PeakCache {
public:
    // Ricostruisce la cache dai campioni. `bucketFrames` e' il numero di frame
    // rappresentati da un valore minimo/massimo.
    void build(const AudioBuffer& buffer, int64_t bucketFrames);

    void clear();
    bool empty() const { return buckets_ == 0; }

    int64_t bucketFrames() const { return bucketFrames_; }
    int64_t buckets() const { return buckets_; }
    int channels() const { return channels_; }

    // Estremi del bucket indicato, per canale.
    float minimum(int64_t bucket, int channel) const;
    float maximum(int64_t bucket, int channel) const;

private:
    std::vector<float> minima_;
    std::vector<float> maxima_;
    int64_t bucketFrames_ = 128;
    int64_t buckets_ = 0;
    int channels_ = 1;
};

// ---------------------------------------------------------------------------
//  Vista
// ---------------------------------------------------------------------------
class WaveformView {
public:
    WaveformView();
    ~WaveformView();

    bool create(HWND parent, int controlId, HINSTANCE instance);
    HWND handle() const { return window_; }

    void setDocument(AudioDocument* document);
    AudioDocument* document() const { return document_; }

    // Da richiamare quando il documento cambia: ricostruisce cache e curva.
    void refreshDocument(bool rebuildCache);
    void refreshGainCurve();
    void setGainCurveMode(GainCurveMode mode) { gainCurveMode_ = mode; refreshGainCurve(); invalidate(); }

    // --- navigazione -------------------------------------------------------
    void zoomToFit();
    void zoomAround(double factor, int64_t anchorFrame);
    void scrollByPixels(int pixels);
    void ensureFrameVisible(int64_t frame);
    void setVerticalZoom(double zoom);
    double verticalZoom() const { return verticalZoom_; }
    void setPlayhead(int64_t frame);
    int64_t playhead() const { return playheadFrame_; }

    void setSelection(int64_t start, int64_t end);
    void clearSelection();
    bool hasSelection() const { return selectionEnd_ > selectionStart_; }
    int64_t selectionStart() const { return selectionStart_; }
    int64_t selectionEnd() const { return selectionEnd_; }

    void setSelectedRegion(int index);
    int selectedRegion() const { return selectedRegion_; }
    const std::vector<int>& selectedRegions() const { return selectedRegions_; }
    void toggleSelectedRegion(int index);
    void selectAllRegions();
    void clearRegionSelection();
    bool isRegionSelected(int index) const;
    void setTimeSelectionTool(bool active) { timeSelectionTool_ = active; invalidate(); }
    bool timeSelectionTool() const { return timeSelectionTool_; }
    void selectRegionsInRange(int64_t start, int64_t end);
    void setRegionFilter(int filter);
    int regionFilter() const { return regionFilter_; }
    bool regionVisible(int index) const;
    bool isDraggingRegion() const {
        return dragMode_ == DragMode::MoveRegion || dragMode_ == DragMode::MoveEdge;
    }
    bool dragMoved() const { return dragMoved_; }

    // --- conversioni -------------------------------------------------------
    int64_t frameAtX(int x) const;
    int xAtFrame(int64_t frame) const;
    int64_t visibleStartFrame() const { return firstFrame_; }
    int64_t visibleFrameCount() const;
    double framesPerPixel() const { return framesPerPixel_; }

    void invalidate();

    // --- notifiche verso la finestra ---------------------------------------
    std::function<void()> onViewChanged;         // zoom o scorrimento
    std::function<void(int64_t)> onCursorMoved;  // clic singolo
    std::function<void()> onSelectionChanged;
    std::function<void(int)> onRegionChanged;    // regione modificata dal mouse
    std::function<void(int)> onRegionSelected;
    std::function<void(int64_t)> onDoubleClick;  // doppio clic su un punto

private:
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    void ensureBackBuffer(int width, int height);
    void paint(HDC target, const RECT& client);
    void drawRuler(HDC dc, const RECT& client);
    void drawWaveform(HDC dc, const RECT& area);
    void drawGainCurve(HDC dc, const RECT& area);
    void drawRegions(HDC dc, const RECT& area, bool backgroundOnly = false);
    void drawSelection(HDC dc, const RECT& area, bool outline = false);
    void drawPlayhead(HDC dc, const RECT& area);

    void clampView();
    void beginDrag(int x, int y, UINT modifiers);
    void updateDrag(int x, int y);
    void endDrag();

    AudioDocument* document_ = nullptr;

    HWND window_ = nullptr;
    int width_ = 0;
    int height_ = 0;

    HDC backBufferDc_ = nullptr;
    HBITMAP backBufferBitmap_ = nullptr;
    int backBufferWidth_ = 0;
    int backBufferHeight_ = 0;

    HFONT smallFont_ = nullptr;
    HFONT tinyFont_ = nullptr;

    PeakCache peaks_;
    std::vector<float> gainCurve_;
    GainCurveMode gainCurveMode_ = GainCurveMode::Legacy;

    // Vista
    int64_t firstFrame_ = 0;
    double framesPerPixel_ = 32.0;
    double verticalZoom_ = 1.0; // solo visualizzazione, non tocca l'audio

    // Selezione e testina
    int64_t selectionStart_ = 0;
    int64_t selectionEnd_ = 0;
    int64_t playheadFrame_ = 0;

    int selectedRegion_ = -1;
    std::vector<int> selectedRegions_;
    int hoveredRegion_ = -1;

    // Trascinamento in corso
    enum class DragMode { None, Select, SelectionEdge, MoveEdge, MoveRegion, Playhead };
    DragMode dragMode_ = DragMode::None;
    int dragRegion_ = -1;
    int dragEdge_ = 0;
    int64_t dragGrabOffset_ = 0;
    int64_t dragOriginalStart_ = 0;
    int64_t dragOriginalEnd_ = 0;
    bool dragMoved_ = false;
    bool dragHistoryOpen_ = false;
    bool timeSelectionTool_ = false;
    int regionFilter_ = 0;
    int dragMouseX_ = 0, dragMouseY_ = 0;
    int dpiPixels(int logical) const;

    // Punto in cui e' iniziato il trascinamento di selezione.
    //
    // Serve perche' beginDrag() azzera la selezione (start == end), e da quel
    // momento hasSelection() e' falso: senza questo ancoraggio il calcolo
    // ripiegava sulla posizione corrente e la selezione restava di larghezza
    // zero per tutto il trascinamento.
    int64_t dragAnchor_ = 0;

    static constexpr int kRulerHeight = 24;
};

// Colori condivisi con la finestra principale.
namespace EditorColors {
constexpr COLORREF kBackground = RGB(26, 30, 36);
constexpr COLORREF kLaneBackground = RGB(32, 37, 44);
constexpr COLORREF kGrid = RGB(48, 54, 63);
constexpr COLORREF kWaveform = RGB(120, 200, 235);
constexpr COLORREF kWaveformSoft = RGB(58, 100, 122);
constexpr COLORREF kBreath = RGB(240, 176, 62);
constexpr COLORREF kBreathSoft = RGB(120, 90, 40);
constexpr COLORREF kClick = RGB(238, 88, 88);
constexpr COLORREF kClickSoft = RGB(120, 52, 52);
constexpr COLORREF kSelection = RGB(52, 92, 140);
constexpr COLORREF kSelectionSingle = RGB(20, 88, 148);
constexpr COLORREF kSelectionSingleSoft = RGB(14, 54, 92);
constexpr COLORREF kSelectionEdge = RGB(110, 220, 255);
constexpr COLORREF kSelectionMulti = RGB(76, 54, 132);
constexpr COLORREF kSelectionMultiEdge = RGB(204, 168, 255);
constexpr COLORREF kSelectionHandle = RGB(245, 250, 255);
constexpr COLORREF kPlayhead = RGB(255, 240, 150);
constexpr COLORREF kGainCurve = RGB(150, 240, 170);
constexpr COLORREF kText = RGB(214, 220, 228);
constexpr COLORREF kTextDim = RGB(140, 148, 160);
} // namespace EditorColors

} // namespace bc

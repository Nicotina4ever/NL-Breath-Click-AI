// SPDX-License-Identifier: MPL-2.0
// ============================================================================
//  editor_window.h — Finestra principale dell'editor
//
//  Struttura volutamente essenziale: menu, una barra con i comandi usati
//  spesso, la forma d'onda e una barra di stato.
//
//  Il rendering dell'audio avviene in modo differito: trascinare il bordo di
//  una regione genera decine di modifiche al secondo, e rigenerare l'intero
//  buffer a ogni movimento renderebbe il trascinamento inutilizzabile. Le
//  modifiche marcano il documento come "da rendere" e un timer si occupa di
//  farlo quando l'utente si ferma.
// ============================================================================
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "audio_player.h"
#include "core/detect/detection.h"
#include "core/edit/audio_document.h"
#include "core/learning/personal_profile.h"
#include "core/model/model_profile.h"
#include "studio_engine.h"
#include "studio_project.h"
#include "studio_session.h"
#include "core/model/regions_json.h"
#include "waveform_view.h"

namespace bc {

// Formato scelto per l'esportazione. Il valore predefinito e' "stesso
// formato dell'originale": MP3 resta MP3, WAV resta WAV, FLAC viene salvato
// in WAV PCM perche' non includiamo un encoder FLAC.
enum class ExportFormat {
    SameAsSource = 0,
    Wav = 1,
    Mp3 = 2
};

struct ExportOptions {
    ExportFormat format = ExportFormat::SameAsSource;
    Mp3EncodeSettings mp3;
};

class EditorWindow {
public:
    EditorWindow();
    ~EditorWindow();

    bool create(HINSTANCE instance);
    int runMessageLoop();

    // Apre un file all'avvio (dalla riga di comando).
    void openInitialFile(const std::string& path);
    bool openSessionFile(const std::string& path);
    // Punti d'ingresso usati anche dal test di progetto headless.
    bool openProjectPath(const std::string& path);
    bool saveProjectPath(const std::string& path);
    bool analyzeAllTracks();
    void approveAllTracks();
    int exportAllTo(const std::string& outputDirectory, bool showSummary);
    void setStudioMode(bool enabled) {
        studioMode_ = enabled;
        view_.setGainCurveMode(enabled ? GainCurveMode::StudioSource : GainCurveMode::Legacy);
    }

private:
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    void createMenus();
    void createChildren();
    void layoutChildren();
    void updateTitle();
    void updateStatusBar();
    void updateCommandState();
    void renderIfNeeded(bool force);

    void onCommand(int commandId);
    void onContextMenu(int screenX, int screenY);

    // --- comandi -----------------------------------------------------------
    void commandOpen();
    void commandSaveAs();
    void commandExport();
    void commandExportOptions();
    void commandSetExportFormat(int commandId);
    void commandSaveSession();
    void commandOpenSession();
    // --- progetto multi-file -------------------------------------------------
    void commandOpenFolder();
    void commandOpenProject();
    void commandSaveProject(bool saveAs);
    void commandRecoverProject();
    void commandProjectAnalyzeAll();
    void commandProjectExportAll();
    void commandProjectMarkReviewed();
    void commandProjectNextFile(int delta);
    // Pannello contestuale della regione, legenda e zoom verticale.
    void createRegionPanel();
    void layoutRegionPanel();
    void refreshRegionPanel(bool force = false);
    void commandRegionEditBounds();
    void commandVerticalZoom(int step);
    void commandVerticalZoomReset();
    void commandGoToSelectedRegion();
    void commandToggleFollowPlayhead();
    void commandGoToPlayhead();
    void updateLegend();
    void createTooltips();

    void createFileListPanel();
    void layoutFileListPanel();
    void rebuildFileList();
    void onFileListSelectionChanged();
    bool analyzeActiveTrack(bool interactive, std::string* error);
    bool exportActiveTrackTo(const std::string& outputPath, std::string* error);
    bool saveProjectTo(const std::string& path, std::string* error);
    bool loadProjectFrom(const std::string& path, std::string* error);
    void saveRecovery();
    void refreshProjectMeta();
    void refreshTrackStatus();
    void setTrackStatus(int index);
    std::string makeRecoveryPath() const;
    std::string makeStatusLabel(const AudioDocument& doc, bool analyzed, bool exported) const;
    std::string globalEditSummary(bool allFiles) const;
    studio::SessionData makeSessionFor(const AudioDocument& doc) const;
    studio::Settings makeStudioSettings() const;
    int64_t playerPositionInDocument() const;
    bool applySessionTo(AudioDocument* doc, const studio::SessionData& session) const;
    AudioDocument* trackDocument(int index);
    const AudioDocument* trackDocument(int index) const;
    void commandLearningLabel(LearningLabel label);
    void commandTrainPersonalProfile();
    void commandActivatePersonalProfile(bool activate);
    void commandLearningStatus();
    void commandManualSelection(int mode);
    void commandTimeBounds();
    void commandClicksEnabled(bool enabled);
    void updateLearningSummary();
    void commandBatchFolder();
    void commandBatchReviewed();
    void commandAttenuationSettings();
    void commandDetectorMode(const std::string& mode);
    void commandRegionMode(int mode);
    void commandRegionGlobalMode(int mode);
    void commandRegionKeepFraction();
    void commandRegionKeepPreset(double fraction);
    void commandRegionMarginPreset(double marginMs);
    void commandAuditionRegion();
    void commandRegionStep(int delta);
    void commandRegionSelectAll();
    void commandRegionClearSelection();
    void commandRegionDecisionAll(bool approve);
    void commandPreview();
    void commandApplyPreview();
    void commandExportPlan();
    void commandAnalyze();
    // Importa le regioni da un file JSON prodotto dall'analisi AI.
    void commandImportAiRegions();
    // Scrive le correzioni come dati per addestrare il modello.
    void commandExportDataset();
    void commandDetectionSettings();
    void commandApplyRegionsToDocument(bool createRegions);

    void commandUndo();
    void commandRedo();
    void commandDeleteSelection();
    void commandCutSelection();

    // Campioni dell'ultimo taglio, tenuti in memoria.
    std::vector<float> tagliato_;
    int tagliatoCanali_ = 1;
    void commandShowShortcuts();
    void commandSilenceSelection();
    void commandCropToSelection();
    void commandSelectAll();
    void commandInsertSilence();

    void commandRegionDelete();
    void commandRegionToggleEnabled();
    void commandRegionSetKind(DetectionKind kind);
    void commandRegionGain(float deltaDb);
    void commandRegionGainReset();
    void commandRegionDecision(bool confirm);
    void commandRegionToggleRepair();
    void commandShortenRegions(float keepFraction);
    void commandRegionAddAtSelection();

    void commandZoomIn();
    void commandZoomOut();
    void commandZoomFit();
    void commandZoomSelection();

    void commandPlay();
    void commandPlaySelection();
    void commandPlayMode(bool original);
    void commandStop();
    void commandGoToStart();
    void commandGoToEnd();

    void commandFade();
    void commandClearFade();
    void commandAbout();

    // --- tracce multiple ---------------------------------------------------
    //
    // Piu' file aperti contemporaneamente, come le tracce di Audition.
    // Si lavora su UNA traccia alla volta, quella attiva. Cambiare traccia
    // non copia audio: si scambiano contenitori, quindi e' istantaneo anche
    // su file da mezz'ora.
    void addTrackAndLoad(const std::string& path);
    void switchToTrack(int index);
    void closeActiveTrack();
    void rebuildTrackMenu();
    void refreshActiveTrack();

    // --- profili di modello ------------------------------------------------
    //
    // Un profilo e' un assetto di rilevamento salvato con un nome (soglie, e
    // se ci sono i modelli addestrati). Il profilo "base" e' sempre
    // disponibile: e' il ripristino quando un profilo addestrato peggiora.
    void loadModelProfiles();
    void rebuildModelMenu();
    void applyModelProfile(const std::string& name);

    // --- comodita' ---------------------------------------------------------
    bool loadFile(const std::string& path);
    void adoptBuffer(AudioBuffer buffer, const std::string& name);
    void markDocumentChanged(bool rebuildPeaks);
    void setStatusMessage(const std::string& message);

    bool ensurePlayerOpen();
    void refreshDeviceStatus();

    std::string askOpenPath();
    std::string askSavePath(const char* defaultExtension);
    std::string askExportPath();

    // --- stato -------------------------------------------------------------
    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    HWND waveformHost_ = nullptr;

    HMENU mainMenu_ = nullptr;
    HMENU regionMenu_ = nullptr;

    HWND openButton_ = nullptr;
    HWND saveButton_ = nullptr;
    HWND exportButton_ = nullptr;
    HWND analyzeButton_ = nullptr;
    HWND settingsButton_ = nullptr;
    HWND playButton_ = nullptr;
    HWND stopButton_ = nullptr;
    HWND gainLabel_ = nullptr;
    HWND gainSlider_ = nullptr;
    HWND gainValue_ = nullptr;
    HWND statusBar_ = nullptr;

    HFONT uiFont_ = nullptr;
    HBRUSH backgroundBrush_ = nullptr;

    AudioDocument document_;
    ExportOptions exportOptions_;
    WaveformView view_;
    AudioPlayer player_;

    // ---------------------------------------------------------------------
    //  Tracce aperte.
    //
    //  La traccia ATTIVA vive in document_: tutto il resto del codice la usa
    //  con quel nome, e riscriverlo in 197 punti sarebbe stato un rischio
    //  inutile. Il posto della traccia attiva in questo vettore contiene un
    //  documento vuoto, che fa da segnaposto per tenere gli indici allineati.
    //
    //  Cambiare traccia: si sposta document_ nella sua cella e si preleva
    //  quella di destinazione. Sono spostamenti di vettori e stringhe, non
    //  copie dell'audio.
    // ---------------------------------------------------------------------
    std::vector<AudioDocument> tracks_;
    std::vector<std::string> trackNames_;
    std::vector<std::string> trackPaths_;
    std::vector<std::string> trackStatus_;
    std::vector<std::string> trackSessionRelative_;
    std::vector<std::string> trackOutputPaths_;
    std::vector<bool> trackAnalyzed_;
    std::vector<bool> trackReviewed_;
    std::vector<bool> trackExported_;
    std::vector<std::string> trackErrors_;
    int activeTrack_ = -1;
    HMENU trackMenu_ = nullptr;

    // Pannello multi-file e progetto.
    HWND fileList_ = nullptr;
    bool fileListSync_ = false;

    // Pannello contestuale dell'evento selezionato.
    HWND regionPanel_ = nullptr;
    HWND regionTitle_ = nullptr;
    HWND regionCounters_ = nullptr;
    HWND regionState_ = nullptr;
    HWND regionStartValue_ = nullptr;
    HWND regionEndValue_ = nullptr;
    HWND regionDuration_ = nullptr;
    HWND regionMode_ = nullptr;
    HWND regionFade_ = nullptr;
    HWND regionApply_ = nullptr;
    HWND regionApprove_ = nullptr;
    HWND regionReject_ = nullptr;
    HWND regionModeButtons_[3] = {};
    HWND verticalZoomSlider_ = nullptr;
    HWND verticalZoomValue_ = nullptr;
    HWND legend_ = nullptr;
    HWND toolRegions_ = nullptr, toolTime_ = nullptr, regionFilter_ = nullptr;
    HWND learningSummary_ = nullptr;
    HWND tooltip_ = nullptr;
    double verticalZoom_ = 1.0;
    bool regionPanelUpdating_ = false;
    bool followPlayhead_ = true;
    bool followAutoScroll_ = false;
    PersonalProfile personalProfile_;
    bool personalProfileActive_ = false;

    std::string projectPath_;
    bool projectDirty_ = false;

    // Profili di rilevamento disponibili.
    ModelRegistry models_;
    HMENU modelMenu_ = nullptr;
    std::string activeProfile_ = "base";

    std::shared_ptr<const std::vector<float>> renderedAudio_;
    std::shared_ptr<const std::vector<float>> originalAudio_;
    bool abOriginal_ = false;
    double editFadeMs_ = 15.0;
    // Crossfade predefinito della GUI per Taglia/Dimezza; CLI e piani
    // esistenti restano a 0 finche' non lo impostano esplicitamente.
    double editCrossfadeMs_ = 5.0;
    int defaultEditMode_ = 0; // 0 Attenua, 1 Taglia, 2 Dimezza
    bool renderPending_ = false;
    bool documentDirty_ = false;
    bool playerUsesPreview_ = false;
    bool recoveryPending_ = false;

    DetectorSettings detectorSettings_;
    EditSettings editSettings_;

    std::string statusMessage_;
    std::string deviceStatus_;
    std::string analysisSummary_;
    std::string studioDetector_ = "dsp";
    float aiThreshold_ = 0.064f;
    bool studioMode_ = false;
    studio::EditResult preview_;
    std::shared_ptr<const std::vector<float>> previewAudio_;
    bool hasPreview_ = false;
    int engineApplied_ = -1, engineRejected_ = -1;
    std::string previewSignature_;
    bool buildEditPlan(studio::EditPlan* plan, std::string* error);

    // Riempimento del silenzio: durata predefinita in secondi.
    double insertSilenceSeconds_ = 0.5;

    static constexpr int kTimerPlayhead = 1;
    static constexpr int kTimerRender = 2;
    static constexpr int kTimerAutosave = 3;
    static constexpr int kLegendHeight = 22;
    static constexpr int kToolbarHeight = 40;
    static constexpr int kStatusHeight = 22;
};

} // namespace bc

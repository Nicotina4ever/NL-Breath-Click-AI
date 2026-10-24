// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>

#include "core/edit/wav_io.h"
#include "core/detect/detection.h"
#include "core/edit/audio_document.h"

namespace bc {
namespace studio {

struct Settings {
    float reductionDb = -12.0f;
    double fadeMs = 15.0;
    double prePadMs = 5.0;
    double postPadMs = 10.0;
    float sensitivity = 0.8f;
    float threshold = 0.02f;
    bool detectClicks = false;
    std::optional<DetectorSettings> dspSettings;
    bool useRespiroEn = false;
    float aiThreshold = 0.064f;
    // In fusione un candidato solo-DSP viene tenuto solo se Respiro-en ha
    // almeno questa probabilita' sullo stesso intervallo (evita di attenuare
    // parlato che il DSP scambia per respiro).
    float fusionAiFloor = 0.02f;
    double cutMarginMs = 0.0;
    // Crossfade di sovrapposizione tra i bordi di Taglia/Dimezza. 0 = solo
    // rampa del vecchio comportamento; >0 = overlap-add.
    double crossfadeMs = 0.0;
    std::string modelPath;
    // "dsp", "ai" oppure "fusion" (unione DSP + Respiro-en).
    std::string detector = "dsp";
};

enum class EditMode { Attenuate = 0, Cut = 1, Halve = 2, Silence = 3 };

const char* editModeName(EditMode mode);
bool editModeFromName(const std::string& name, EditMode* mode);

// Un evento ha DUE decisioni separate:
//  * `state` e' la classificazione (proposed/ai/confirmed/rejected/manual);
//  * `approved` e' l'approvazione editoriale dell'intervento.
// Un evento puo' essere un respiro vero ma non approvato, o viceversa.
struct Event {
    DetectionKind kind = DetectionKind::Breath;
    std::optional<float> reductionDb;
    bool repair = false;
    int64_t startSample = 0;   // campioni, inizio audio
    int64_t endSample = 0;     // esclusivo
    float score = 0.0f;        // confidenza del rilevatore
    bool enabled = true;       // compatibilita': evento mostrato/attivo
    bool approved = false;     // l'utente ha approvato l'intervento
    std::string state = "proposed"; // classificazione del rilevatore
    // L'approvazione editoriale NON e' una classificazione e NON rende
    // automaticamente l'evento una etichetta di training.
    std::string decisionSource = "none"; // user | review_harness | automatic
    std::string classification;          // dsp | ai | shared | manual
    bool trainingEligible = false;
    EditMode mode = EditMode::Attenuate;
    double keepFraction = 0.5; // solo Halve, frazione del nucleo tagliabile
    double fadeMs = 15.0;      // fade/raccordo dell'evento
    double safetyMarginMs = 0.0; // margine non tagliato ai bordi dell'evento
    // Provenienza nella fusione: "dsp", "ai" o "shared".
    std::string source;
    std::string reason;
    float dspScore = 0.0f;
    float aiScore = 0.0f;
    float aiMaxProbability = 0.0f;
    std::string learningLabel;
    bool manualOperation = false;
};

Event eventFromRegion(const EditRegion& region);
Detection detectionFromEvent(const Event& event);

struct TimelineMarker {
    int64_t timeSample = 0;
    int64_t endSample = 0;     // per regioni; 0/uguale per marker
    std::string name;
    bool isRegion = false;
};

struct TimeMapSegment {
    int64_t originalStart = 0;
    int64_t originalEnd = 0;
    int64_t editedStart = 0;
    int64_t editedEnd = 0;
};

class TimeMap {
public:
    std::vector<TimeMapSegment> segments;   // solo intervalli conservati
    int64_t originalFrames = 0;
    int64_t editedFrames = 0;

    int64_t mapOriginal(int64_t sample, bool* removed = nullptr) const;
    int64_t mapEdited(int64_t sample, bool* gap = nullptr) const;
    bool isRemoved(int64_t start, int64_t end) const;
};

struct EditOperationResult {
    Event event;
    int64_t removedSamples = 0;
    int64_t safetyMarginSamples = 0;
    int64_t fadeFrames = 0;
    int64_t editedStart = 0;
    bool applied = false;
    std::string note;
};

struct WavChunkInfo {
    std::string id;
    uint64_t size = 0;
    bool copied = false;
    std::string note;
};

struct WavWriteReport {
    std::vector<WavChunkInfo> sourceChunks;
    std::vector<WavChunkInfo> writtenChunks;
    bool dataPadding = false;
    bool riffSizePatched = false;
};

struct EditResult {
    AudioBuffer audio;
    TimeMap map;
    std::vector<TimelineMarker> markers;
    std::vector<TimelineMarker> mappedMarkers;       // vista selezionata
    std::vector<TimelineMarker> voiceMappedMarkers;  // timeline compattata
    std::vector<TimelineMarker> projectMappedMarkers; // timeline originale
    std::vector<EditOperationResult> operations;
    std::string markerScope = "voice";
    int64_t removedSamples = 0;
    int64_t crossfadeFrames = 0;
    int appliedEvents = 0;
    int rejectedEvents = 0;
    WavWriteReport wav;
};

struct EditPlan {
    int schema = 2;
    // Compact within the voice item, or keep every later cue at its source time.
    // In preserve mode the removed portion becomes silence with internal fades.
    bool preserveDuration = false;
    std::string sourcePath;
    std::string sourceRelative;  // relativo al file .plan.json, se possibile
    std::string sourceSha256;
    int sampleRate = 0;
    int channels = 0;
    Settings settings;
    AnalysisProvenance analysis;
    float masterGainDb = 0;
    FadeSettings fade;
    std::vector<Event> events;
    std::vector<TimelineMarker> markers;
    // "voice" compatta la timeline del parlato; "project" lascia i marker
    // ancorati alla timeline originale. Il report contiene entrambe le viste.
    std::string markerScope = "voice";
};

struct EventMetric {
    int64_t startSample = 0;
    int64_t endSample = 0;
    double rmsBeforeDb = -160.0;
    double rmsAfterDb = -160.0;
    double deltaDb = 0.0;
};

struct Analysis {
    std::string inputPath;
    std::string inputSha256;
    int sampleRate = 0;
    int channels = 0;
    int bitsPerSample = 0;
    bool isFloat = false;
    int64_t frames = 0;
    double durationSeconds = 0.0;
    std::string detector;
    int64_t aiInferenceFrames = 0;
    std::vector<Event> events;
    std::vector<Event> excludedEvents;
};

struct Report {
    Analysis analysis;
    std::string outputPath;
    std::string outputSha256;
    Settings settings;
    std::vector<EventMetric> metrics;
    double outsideMaxDeltaDb = 0.0;
    double outsideMeanDeltaDb = 0.0;
    int processedEvents = 0;
    int skippedEvents = 0;
    double processingSeconds = 0.0;
    WavWriteReport wav;
    std::string error;
};

bool analyzeFile(const std::string& path,
                 const Settings& settings,
                 Analysis* analysis,
                 std::string* error = nullptr);

bool processFile(const std::string& inputPath,
                 const std::string& outputDirectory,
                 const Settings& settings,
                 Report* report,
                 std::string* error = nullptr);

bool processFolder(const std::string& inputDirectory,
                   const std::string& outputDirectory,
                   const Settings& settings,
                   const std::string& batchReportPath,
                   std::vector<Report>* reports,
                   std::string* error = nullptr);

struct BatchReviewItem {
    std::string inputPath;
    std::string outputPath;
    std::string planPath;
    std::string status;   // applicato | da_rivedere | errore
    std::string error;
};

struct BatchReviewReport {
    std::vector<BatchReviewItem> items;
    int applied = 0;
    int skipped = 0;
};

// Batch con coda di revisione: elabora solo i file per cui esiste un piano
// revisionato nella cartella plansDirectory. Non analizza ne' taglia nulla
// automaticamente.
bool processFolderReviewed(const std::string& inputDirectory,
                           const std::string& outputDirectory,
                           const std::string& plansDirectory,
                           BatchReviewReport* report,
                           std::string* error = nullptr);

bool writeAnalysisJson(const std::string& path, const Analysis& analysis,
                       std::string* error = nullptr);
bool writeReportJson(const std::string& path, const Report& report,
                     std::string* error = nullptr);
bool writeReportText(const std::string& path, const Report& report,
                     std::string* error = nullptr);

bool sha256File(const std::string& path, std::string& hex,
                std::string* error = nullptr);

// Applica un piano versionato. Le attenuazioni non cambiano la timeline;
// tagli e dimezzamenti vengono validati, pianificati e applicati da destra
// verso sinistra. Restituisce l'audio nuovo e la TimeMap.
bool applyEditPlan(const AudioBuffer& original, const EditPlan& plan,
                   EditResult* result, std::string* error = nullptr);

bool writeEditPlanJson(const std::string& path, const EditPlan& plan,
                       std::string* error = nullptr);
bool writeEditReportJson(const std::string& path, const EditPlan& plan,
                         const EditResult& result, const std::string& outputPath,
                         std::string* error = nullptr);
bool loadEditPlanJson(const std::string& path, EditPlan* plan,
                      std::string* error = nullptr);

// Scrive il WAV conservando i chunk BWF/iXML/LIST/cue ecc. del sorgente
// (tranne fmt/data, che vengono rigenerati).
bool writeWavWithMetadata(const std::string& sourcePath,
                          const std::string& outputPath,
                          const AudioBuffer& audio,
                          WavFormat format,
                          std::string* error = nullptr,
                          WavWriteReport* report = nullptr);

// Scrive il WAV su un file temporaneo e sostituisce l'uscita solo al termine.
bool writeWavWithMetadataAtomic(const std::string& sourcePath,
                                const std::string& outputPath,
                                const AudioBuffer& audio,
                                WavFormat format,
                                std::string* error = nullptr,
                                WavWriteReport* report = nullptr);

// Riduce di `reductionDb` con rampe a coseno di `fadeMs`. Non modifica
// l'originale: lavora su una copia passata dal chiamante.
void applyAttenuation(AudioBuffer& audio,
                      const std::vector<Event>& events,
                      const Settings& settings);

// Utile al selftest e alla GUI per misurare il risultato.
double rmsDb(const AudioBuffer& audio, int64_t startSample, int64_t endSample,
             int channels);

} // namespace studio
} // namespace bc

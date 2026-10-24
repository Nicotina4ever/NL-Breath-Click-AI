// SPDX-License-Identifier: MPL-2.0
// ============================================================================
//  audio_document.h — Modello del documento audio per l'editor
//
//  Principio: **l'audio sorgente non viene mai modificato**. Il documento tiene
//  un buffer di lavoro, un elenco di regioni con la propria attenuazione, un
//  fade di ingresso/uscita e un guadagno generale. Il rendering produce una
//  copia: il documento resta com'era.
//
//  Le uniche operazioni che toccano il buffer sono i tagli (elimina, silenzia,
//  ritaglia, inserisci silenzio), perche' cambiando la lunghezza cambiano la
//  linea del tempo. Sono comunque annullabili: la cronologia non conserva una
//  copia del buffer a ogni passo — sarebbe insostenibile su file lunghi — ma
//  solo i campioni effettivamente rimossi e la loro posizione.
// ============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/detect/detection.h"
#include "core/edit/region_editor.h"
#include "core/edit/audio_io.h"
#include "core/edit/wav_io.h"

namespace bc {

enum class GainCurveMode { Legacy, StudioSource };

// ---------------------------------------------------------------------------
//  Regione modificabile
// ---------------------------------------------------------------------------
struct EditRegion {
    DetectionKind kind = DetectionKind::Breath;
    int64_t startSample = 0;
    int64_t endSample = 0;

    // Attenuazione da applicare se la regione non viene riparata (dB, <= 0).
    float reductionDb = -18.0f;

    // Solo click: ripara per interpolazione invece di attenuare.
    bool repair = false;

    // Una regione disattivata resta visibile ma non viene applicata.
    bool enabled = true;

    // Confidenza del rilevamento; 0 per le regioni create a mano.
    float score = 0.0f;

    // Distingue i rilevamenti automatici dalle modifiche dell'utente.
    bool fromDetection = false;

    // Decisioni editoriali BreathClick Studio. `approved` e' separato dalla
    // classificazione: un respiro vero puo' non essere approvato.
    bool approved = false;
    int editMode = 0;              // 0 attenuate, 1 cut, 2 halve
    double keepFraction = 0.5;     // solo halve
    double fadeMs = 15.0;          // fade/raccordo dell'evento
    double safetyMarginMs = 0.0;   // margine non tagliato ai bordi
    bool applied = false;           // gia' esportato in una copia
    std::string decisionSource;      // user | review_harness | automatic
    std::string classification;      // dsp | ai | shared | manual
    bool trainingEligible = false;

    // Etichetta esplicita per il futuro dataset:
    // "" = non verificato, "breath_confirmed" = respiro confermato,
    // "false_positive" = falso positivo confermato,
    // "missed_breath" = respiro mancato aggiunto e confermato,
    // "editorial_exclusion" = escluso per scelta editoriale.
    std::string learningLabel;
    bool manualOperation = false; // intervento audio, non una label di respiro
    std::string reason;
    float dspScore = 0, aiScore = 0, aiMaxProbability = 0;

    int64_t lengthSamples() const { return endSample - startSample; }
    double lengthMs(double sampleRate) const {
        return sampleRate > 0.0 ? 1000.0 * static_cast<double>(lengthSamples()) / sampleRate : 0.0;
    }
    double startSeconds(double sampleRate) const {
        return sampleRate > 0.0 ? static_cast<double>(startSample) / sampleRate : 0.0;
    }
    double endSeconds(double sampleRate) const {
        return sampleRate > 0.0 ? static_cast<double>(endSample) / sampleRate : 0.0;
    }
};

    // --- Registro delle correzioni dell'utente -----------------------------
    //
    // E' il cuore dell'apprendimento: quando l'utente sposta, aggiunge o
    // cancella una regione sta dicendo al programma cosa e' davvero un fiato
    // NEL SUO materiale. Quelle indicazioni valgono piu' di qualunque soglia.
    //
    //  - aggiunge una regione      -> "qui c'e' un fiato che non avevi visto"
    //  - cancella un rilevamento   -> "questo NON e' un fiato"
    //  - sposta un rilevamento     -> la posizione nuova e' giusta, la vecchia no
    struct Correction {
        DetectionKind kind = DetectionKind::Breath;
        int64_t startSample = 0;
        int64_t endSample = 0;
        bool positive = true;        // true: e' un evento; false: non lo e'
        std::string source;          // "aggiunta", "cancellazione", "spostamento"
    };

// ---------------------------------------------------------------------------
//  Fade di ingresso / uscita
// ---------------------------------------------------------------------------
enum class FadeShape {
    Linear = 0,
    Exponential,
    Cosine
};

struct FadeSettings {
    bool inEnabled = false;
    double inSeconds = 0.05;
    FadeShape inShape = FadeShape::Cosine;

    bool outEnabled = false;
    double outSeconds = 0.05;
    FadeShape outShape = FadeShape::Cosine;
};

struct AnalysisProvenance {
    std::string detector; // empty: legacy session without verifiable provenance
    int64_t aiFrames = 0;
};

void applyDocumentEnvelope(AudioBuffer& audio, const FadeSettings& fade, float masterGainDb);

// ---------------------------------------------------------------------------
//  Documento
// ---------------------------------------------------------------------------
class AudioDocument {
public:
    // ---------------------------------------------------------------------
    //  Caricamento e salvataggio
    // ---------------------------------------------------------------------
    bool load(const std::string& path, std::string* error);
    bool adopt(AudioBuffer buffer, const std::string& displayName);

    // Salva il RISULTATO del rendering: il documento non cambia.
    bool save(const std::string& path,
              WavFormat format,
              const EditSettings& settings,
              std::string* error) const;

    bool isLoaded() const { return loaded_; }
    const std::string& sourcePath() const { return sourcePath_; }
    const AudioFileInfo& sourceInfo() const { return sourceInfo_; }
    const std::string& displayName() const { return displayName_; }

    // ---------------------------------------------------------------------
    //  Informazione sul contenuto
    // ---------------------------------------------------------------------
    const AudioBuffer& audio() const { return buffer_; }
    const AnalysisProvenance& analysisProvenance() const { return analysis_; }
    void setAnalysisProvenance(const AnalysisProvenance& analysis) { analysis_ = analysis; }
    int64_t frameCount() const { return frameCount_; }
    int sampleRate() const { return buffer_.sampleRate; }
    int channels() const { return buffer_.channels; }
    double durationSeconds() const;

    bool isModified() const { return modified_; }
    void markSaved() { modified_ = false; }

    // ---------------------------------------------------------------------
    //  Operazioni sul buffer (modificano la linea del tempo)
    // ---------------------------------------------------------------------

    // Elimina l'intervallo e chiude il buco.
    bool deleteRange(int64_t startFrame, int64_t endFrame);

    // Azzera l'intervallo mantenendo la durata.
    bool silenceRange(int64_t startFrame, int64_t endFrame);

    // Tiene solo l'intervallo indicato.
    bool cropToRange(int64_t startFrame, int64_t endFrame);

    bool insertSilence(int64_t atFrame, int64_t frames);

    // Guadagno applicato in modo distruttivo a un intervallo (dB).
    bool applyGainToRange(int64_t startFrame, int64_t endFrame, float db);

    // ---------------------------------------------------------------------
    //  Regioni
    // ---------------------------------------------------------------------
    const std::vector<EditRegion>& regions() const { return regions_; }

    int addRegion(EditRegion region);
    // Correzione dell'utente registrata a mano (es. cancellazione di un
    // rilevamento, che addRegion non vedrebbe).
    void recordCorrection(DetectionKind kind, int64_t start, int64_t end,
                          bool positive, const std::string& source);

    const std::vector<Correction>& corrections() const { return corrections_; }
    void clearCorrections() { corrections_.clear(); }

    // Export per apprendimento: solo regioni con label umana verificata e
    // trainingEligible. Il journal editoriale non costituisce verita' di training.
    bool exportCorrections(const std::string& path, std::string* error) const;
    bool updateRegion(int index, const EditRegion& region, bool userEdit = true);
    int applyManualRange(int64_t start, int64_t end, int mode, float reductionDb, double fadeMs);
    int restoreManualRange(int64_t start, int64_t end);
    bool removeRegion(int index);
    bool setRegionEnabled(int index, bool enabled);
    void markRegionApplied(int index, bool applied);
    void clearRegions();

    // Sostituisce le regioni con quelle derivate dai rilevamenti automatici.
    void setRegionsFromDetections(const std::vector<Detection>& detections,
                                  const EditSettings& settings);

    struct ReconcileResult {
        int kept = 0;
        int added = 0;
        int removed = 0;
        int ambiguous = 0;
    };

    // Riconcilia una nuova analisi con le correzioni gia' presenti.
    // - manuali, esclusioni, eventi applicati e label verificate restano;
    // - gli eventi automatici non piu' rilevati vengono rimossi;
    // - i nuovi eventi vengono aggiunti come proposte;
    // - sovrapposizioni multiple/ambigue vengono contate e NON sovrascritte.
    ReconcileResult reconcileRegions(const std::vector<Detection>& detections,
                                     const EditSettings& settings);

    // Indice della regione che contiene il campione, oppure -1.
    int regionIndexAt(int64_t sample) const;

    // Regione il cui bordo e' entro la tolleranza indicata: serve a trascinare
    // i confini con il mouse. `edge` vale -1 per l'inizio, +1 per la fine.
    int regionIndexNearEdge(int64_t sample, int64_t tolerance, int* edge) const;

    // ---------------------------------------------------------------------
    //  Fade e guadagno generale
    // ---------------------------------------------------------------------
    // Accorcia le regioni di un tipo, togliendone una frazione e chiudendo il
    // buco: il materiale successivo scorre indietro e la traccia si accorcia.
    //
    // E' l'operazione che velocizza uno spot senza togliere contenuto: un
    // respiro di un secondo diventa di mezzo, e tutto quello che segue si
    // sposta avanti di mezzo secondo.
    //
    // `fraction` e' quanto togliere (0.5 = meta'). `keepHead` decide quale parte
    // resta: con true si taglia la CODA, che e' la scelta giusta perche' il
    // punto di giunzione cade nella parte finale del respiro, la piu' quieta.
    // Tagliando la testa si attaccherebbe la pausa a un attacco forte, e si
    // sentirebbe come un click.
    //
    // Tutte le regioni vengono accorciate in UNA SOLA operazione annullabile:
    // un Ctrl+Z riporta indietro l'intero accorciamento, non un respiro per
    // volta.
    //
    // Restituisce il numero di regioni accorciate.
    int shortenRegions(DetectionKind kind, float fraction = 0.5f, bool keepHead = true);

    const FadeSettings& fade() const { return fade_; }
    void setFade(const FadeSettings& fade);

    float masterGainDb() const { return masterGainDb_; }
    void setMasterGainDb(float db);

    // ---------------------------------------------------------------------
    //  Rendering
    // ---------------------------------------------------------------------

    // Applica regioni, fade e guadagno generale a una copia del buffer.
    AudioBuffer render(const EditSettings& settings) const;

    // Curva di guadagno risultante (un valore per frame, 0..1). Serve al
    // disegno della sovrapposizione e non modifica nulla.
    // StudioSource shows amplitude edits on original time coordinates. Time
    // removals are represented by region labels, not by fictitious gain dips.
    std::vector<float> gainCurve(const EditSettings& settings,
                                GainCurveMode mode = GainCurveMode::Legacy) const;

    // ---------------------------------------------------------------------
    //  Cronologia
    // ---------------------------------------------------------------------
    bool canUndo() const { return !undoStack_.empty(); }
    bool canRedo() const { return !redoStack_.empty(); }
    const std::string& undoLabel() const;
    const std::string& redoLabel() const;
    bool undo();
    bool redo();
    void clearHistory();

    // Raggruppa piu' modifiche consecutive in UNA sola voce di cronologia.
    // Serve al trascinamento: ogni movimento aggiorna la regione, ma l'utente
    // deve poter annullare l'intero gesto con un solo Ctrl+Z.
    void beginUndoGroup(const std::string& label);
    void endUndoGroup();
    void cancelUndoGroup();
    bool undoGroupActive() const { return undoGroupActive_; }

    // Numero di passi memorizzati, per la diagnostica.
    size_t undoDepth() const { return undoStack_.size(); }
    size_t redoDepth() const { return redoStack_.size(); }

    // Memoria occupata dalla cronologia, in byte.
    size_t historyBytes() const;

private:
    // Un passo inverso: come disfare una modifica al buffer.
    struct InverseStep {
        enum class Mode { Replace, Insert, Remove };
        Mode mode = Mode::Replace;
        int64_t position = 0;
        int64_t frames = 0;             // usato da Remove
        std::vector<float> samples;     // usato da Replace e Insert
    };

    struct UndoRecord {
        std::string label;
        std::vector<EditRegion> regions;
        std::vector<Correction> corrections;
        FadeSettings fade;
        AnalysisProvenance analysis;
        float masterGainDb = 0.0f;
        std::vector<InverseStep> steps;
        size_t bytes = 0;
    };

    // Registra lo stato corrente PRIMA di una modifica.
    UndoRecord captureState(const std::string& label) const;
    void pushUndo(UndoRecord record);

    // Nucleo comune di annulla e ripristina: applica i passi inversi del record
    // in cima a `from`, costruisce il record speculare per `to` e ripristina lo
    // stato leggero. Le due operazioni sono simmetriche, quindi condividono lo
    // stesso codice.
    bool applyHistoryStep(std::vector<UndoRecord>& from, std::vector<UndoRecord>& to);

    // Applica un passo inverso e restituisce il passo che lo annulla (serve al
    // redo). Modifica il buffer.
    InverseStep applyInverseStep(const InverseStep& step);

    void clampRegionsToBuffer();
    void rebuildFrameCount();

    AudioBuffer buffer_;
    int64_t frameCount_ = 0;
    bool loaded_ = false;
    bool modified_ = false;

    std::string sourcePath_;
    std::string displayName_;
    AudioFileInfo sourceInfo_;

    std::vector<EditRegion> regions_;
    std::vector<Correction> corrections_;
    FadeSettings fade_;
    AnalysisProvenance analysis_;
    float masterGainDb_ = 0.0f;

    std::vector<UndoRecord> undoStack_;
    std::vector<UndoRecord> redoStack_;
    bool undoGroupActive_ = false;
    bool undoGroupChanged_ = false;
    std::string undoGroupLabel_;
    UndoRecord undoGroupBase_;

    // Limite di memoria per la cronologia: oltre, i passi piu' vecchi cadono.
    size_t historyByteLimit_ = 512u * 1024u * 1024u;
};

} // namespace bc

// SPDX-License-Identifier: MPL-2.0
#include "core/util/atomic_file.h"
#include "core/edit/audio_document.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

namespace bc {

namespace {
uint64_t nextAudioRevision() {
    static std::atomic<uint64_t> serial{0};
    return ++serial;
}

float dbToGain(float db) {
    return std::pow(10.0f, db / 20.0f);
}

// Forme del fade. Il coseno rialzato e' il valore predefinito perche' non ha
// discontinuita' nella derivata: un fade lineare agli estremi produce un
// "ginocchio" udibile su materiale con molta dinamica.
float fadeShapeValue(FadeShape shape, float t) {
    const float clamped = std::max(0.0f, std::min(1.0f, t));
    switch (shape) {
        case FadeShape::Linear:
            return clamped;
        case FadeShape::Exponential:
            return clamped * clamped;
        case FadeShape::Cosine:
        default:
            return 0.5f * (1.0f - std::cos(3.14159265358979323846f * clamped));
    }
}

size_t recordBytes(const std::vector<float>& samples) {
    return samples.size() * sizeof(float) + sizeof(void*) * 4;
}

} // namespace

// ===========================================================================
//  Caricamento
// ===========================================================================
bool AudioDocument::load(const std::string& path, std::string* error) {
    AudioBuffer loaded;
    AudioFileInfo info;
    if (!readAudio(path, loaded, &info, error)) {
        return false;
    }

    // Il nome mostrato e' il nome del file senza cartella.
    std::string name = path;
    const size_t slash = name.find_last_of("\\/");
    if (slash != std::string::npos) {
        name = name.substr(slash + 1);
    }

    if (!adopt(std::move(loaded), name)) {
        if (error) *error = "Il file non contiene audio utilizzabile.";
        return false;
    }

    sourcePath_ = path;
    sourceInfo_ = info;
    sourceInfo_.path = path;
    return true;
}

bool AudioDocument::adopt(AudioBuffer buffer, const std::string& displayName) {
    if (buffer.samples.empty() || buffer.channels <= 0 || buffer.sampleRate <= 0) {
        return false;
    }

    buffer_ = std::move(buffer);
    audioRevision_ = nextAudioRevision();
    displayName_ = displayName;
    sourcePath_.clear();
    sourceInfo_ = {};
    loaded_ = true;
    modified_ = false;

    regions_.clear();
    fade_ = FadeSettings{};
    analysis_ = {};
    masterGainDb_ = 0.0f;
    clearHistory();
    rebuildFrameCount();
    return true;
}

bool AudioDocument::save(const std::string& path,
                         WavFormat format,
                         const EditSettings& settings,
                         std::string* error) const {
    if (!loaded_) {
        if (error) *error = "Nessun documento caricato.";
        return false;
    }

    if (!sourcePath_.empty() && sameFilePath(sourcePath_, path)) {
        if (error) *error = "l'esportazione non puo' sovrascrivere l'audio originale";
        return false;
    }
    const AudioBuffer result = render(settings);
    return writeWav(path, result, format, error);
}

double AudioDocument::durationSeconds() const {
    return buffer_.sampleRate > 0
               ? static_cast<double>(frameCount_) / static_cast<double>(buffer_.sampleRate)
               : 0.0;
}

void AudioDocument::rebuildFrameCount() {
    const int channels = std::max(1, buffer_.channels);
    frameCount_ = static_cast<int64_t>(buffer_.samples.size()) / channels;
}

// ===========================================================================
//  Cronologia
// ===========================================================================
size_t AudioDocument::historyBytes() const {
    size_t total = 0;
    for (const UndoRecord& record : undoStack_) total += record.bytes;
    for (const UndoRecord& record : redoStack_) total += record.bytes;
    return total;
}

AudioDocument::UndoRecord AudioDocument::captureState(const std::string& label) const {
    UndoRecord record;
    record.label = label;
    if (undoGroupActive_) return record;
    record.regions = regions_;
    record.corrections = corrections_;
    record.fade = fade_;
    record.analysis = analysis_;
    record.masterGainDb = masterGainDb_;
    return record;
}

void AudioDocument::pushUndo(UndoRecord record) {
    if (!record.steps.empty()) audioRevision_ = nextAudioRevision();
    if (undoGroupActive_) {
        for (auto& step : record.steps) undoGroupBase_.steps.push_back(std::move(step));
        if (!undoGroupChanged_) {
            redoStack_.clear();
            undoGroupChanged_ = true;
        }
        modified_ = true;
        return;
    }
    record.bytes = 0;
    for (const InverseStep& step : record.steps) {
        record.bytes += recordBytes(step.samples);
    }

    undoStack_.push_back(std::move(record));

    // La cronologia non deve crescere senza limite: si scartano i passi piu'
    // vecchi finche' non rientra nel budget.
    while (!undoStack_.empty() && historyBytes() > historyByteLimit_ &&
           undoStack_.size() > 1) {
        undoStack_.erase(undoStack_.begin());
    }

    redoStack_.clear();
    modified_ = true;
}

AudioDocument::InverseStep AudioDocument::applyInverseStep(const InverseStep& step) {
    audioRevision_ = nextAudioRevision();
    InverseStep inverse;
    const int channels = std::max(1, buffer_.channels);
    const int64_t totalFrames = static_cast<int64_t>(buffer_.samples.size()) / channels;

    const int64_t position = std::max<int64_t>(0, std::min(step.position, totalFrames));
    const size_t positionSamples = static_cast<size_t>(position) * static_cast<size_t>(channels);

    switch (step.mode) {
        case InverseStep::Mode::Replace: {
            const int64_t frames = static_cast<int64_t>(step.samples.size()) /
                                   static_cast<size_t>(channels);
            const int64_t end = std::min(totalFrames, position + frames);

            // Cio' che sta li' adesso e' esattamente cio' che serve per rifare
            // la modifica.
            inverse.mode = InverseStep::Mode::Replace;
            inverse.position = position;
            inverse.samples.assign(
                buffer_.samples.begin() + static_cast<std::ptrdiff_t>(positionSamples),
                buffer_.samples.begin() +
                    static_cast<std::ptrdiff_t>(positionSamples + step.samples.size()));

            const size_t copySamples = static_cast<size_t>(end - position) *
                                       static_cast<size_t>(channels);
            std::copy_n(step.samples.begin(), static_cast<std::ptrdiff_t>(copySamples),
                        buffer_.samples.begin() + static_cast<std::ptrdiff_t>(positionSamples));
            break;
        }

        case InverseStep::Mode::Insert: {
            const int64_t frames = static_cast<int64_t>(step.samples.size()) /
                                   static_cast<size_t>(channels);
            inverse.mode = InverseStep::Mode::Remove;
            inverse.position = position;
            inverse.frames = frames;

            buffer_.samples.insert(
                buffer_.samples.begin() + static_cast<std::ptrdiff_t>(positionSamples),
                step.samples.begin(), step.samples.end());
            break;
        }

        case InverseStep::Mode::Remove: {
            const int64_t frames = std::max<int64_t>(0, step.frames);
            const int64_t end = std::min(totalFrames, position + frames);
            const size_t removeSamples = static_cast<size_t>(end - position) *
                                         static_cast<size_t>(channels);

            inverse.mode = InverseStep::Mode::Insert;
            inverse.position = position;
            inverse.samples.assign(
                buffer_.samples.begin() + static_cast<std::ptrdiff_t>(positionSamples),
                buffer_.samples.begin() + static_cast<std::ptrdiff_t>(positionSamples + removeSamples));

            buffer_.samples.erase(
                buffer_.samples.begin() + static_cast<std::ptrdiff_t>(positionSamples),
                buffer_.samples.begin() + static_cast<std::ptrdiff_t>(positionSamples + removeSamples));
            break;
        }
    }

    rebuildFrameCount();
    return inverse;
}

bool AudioDocument::applyHistoryStep(std::vector<UndoRecord>& from,
                                     std::vector<UndoRecord>& to) {
    if (from.empty()) {
        return false;
    }

    UndoRecord record = std::move(from.back());
    from.pop_back();

    // Il passo inverso si costruisce mentre si disfa: quello che il buffer
    // contiene prima del ripristino e' esattamente cio' che serve per rifare.
    UndoRecord reverse;
    reverse.label = record.label;
    reverse.regions = regions_;
    reverse.corrections = corrections_;
    reverse.fade = fade_;
    reverse.analysis = analysis_;
    reverse.masterGainDb = masterGainDb_;
    reverse.steps.reserve(record.steps.size());

    // L'inverso di una sequenza si applica in ordine contrario.
    for (auto it = record.steps.rbegin(); it != record.steps.rend(); ++it) {
        reverse.steps.push_back(applyInverseStep(*it));
    }
    for (const InverseStep& step : reverse.steps) {
        reverse.bytes += recordBytes(step.samples);
    }

    regions_ = record.regions;
    corrections_ = record.corrections;
    fade_ = record.fade;
    analysis_ = record.analysis;
    masterGainDb_ = record.masterGainDb;

    rebuildFrameCount();
    clampRegionsToBuffer();

    to.push_back(std::move(reverse));
    modified_ = true;
    return true;
}

void AudioDocument::beginUndoGroup(const std::string& label) {
    if (undoGroupActive_) return;
    undoGroupBase_ = captureState(label);
    undoGroupLabel_ = label;
    undoGroupActive_ = true;
    undoGroupChanged_ = false;
}

void AudioDocument::endUndoGroup() {
    if (!undoGroupActive_) return;
    const bool changed = undoGroupChanged_;
    UndoRecord record = std::move(undoGroupBase_);
    undoGroupActive_ = false;
    undoGroupChanged_ = false;
    undoGroupBase_ = {};
    undoGroupLabel_.clear();
    if (!changed) return;

    record.bytes = 0;
    for (const InverseStep& step : record.steps) record.bytes += recordBytes(step.samples);
    undoStack_.push_back(std::move(record));
    redoStack_.clear();
    modified_ = true;
}

void AudioDocument::cancelUndoGroup() {
    if (!undoGroupActive_) return;
    for (auto it = undoGroupBase_.steps.rbegin(); it != undoGroupBase_.steps.rend(); ++it)
        applyInverseStep(*it);
    regions_ = undoGroupBase_.regions;
    corrections_ = undoGroupBase_.corrections;
    fade_ = undoGroupBase_.fade;
    analysis_ = undoGroupBase_.analysis;
    masterGainDb_ = undoGroupBase_.masterGainDb;
    undoGroupActive_ = false;
    undoGroupChanged_ = false;
    undoGroupBase_ = {};
    undoGroupLabel_.clear();
    rebuildFrameCount();
    clampRegionsToBuffer();
}

bool AudioDocument::undo() { return applyHistoryStep(undoStack_, redoStack_); }
bool AudioDocument::redo() { return applyHistoryStep(redoStack_, undoStack_); }

const std::string& AudioDocument::undoLabel() const {
    static const std::string empty;
    return undoStack_.empty() ? empty : undoStack_.back().label;
}

const std::string& AudioDocument::redoLabel() const {
    static const std::string empty;
    return redoStack_.empty() ? empty : redoStack_.back().label;
}

void AudioDocument::clearHistory() {
    undoStack_.clear();
    redoStack_.clear();
}

// ===========================================================================
//  Adeguamento delle regioni dopo un cambio di lunghezza
// ===========================================================================
void AudioDocument::clampRegionsToBuffer() {
    regions_.erase(
        std::remove_if(regions_.begin(), regions_.end(),
                       [this](const EditRegion& region) {
                           return region.endSample <= 0 || region.startSample >= frameCount_ ||
                                  region.endSample <= region.startSample;
                       }),
        regions_.end());

    for (EditRegion& region : regions_) {
        region.startSample = std::max<int64_t>(0, std::min(region.startSample, frameCount_));
        region.endSample = std::max<int64_t>(region.startSample, std::min(region.endSample, frameCount_));
    }
}

namespace {

// Sposta/adatta le regioni dopo che l'intervallo [removedStart, removedEnd) e'
// stato eliminato: quello che stava dopo scorre indietro della durata tolta.
// Le regioni che cadono dentro l'intervallo spariscono, quelle che lo
// attraversano vengono accorciate.
void shiftRegionsAfterDeletion(std::vector<EditRegion>& regions,
                               int64_t removedStart,
                               int64_t removedEnd) {
    const int64_t removed = removedEnd - removedStart;
    if (removed <= 0) return;

    std::vector<EditRegion> result;
    result.reserve(regions.size());

    for (EditRegion region : regions) {
        if (region.endSample <= removedStart) {
            result.push_back(region);              // interamente prima: intatta
        } else if (region.startSample >= removedEnd) {
            region.startSample -= removed;         // interamente dopo: scorre
            region.endSample -= removed;
            result.push_back(region);
        } else {
            // Sovrapposta: si tiene la parte che sopravvive.
            const int64_t newStart = std::min(region.startSample, removedStart);
            const int64_t newEnd = std::max<int64_t>(removedStart,
                                                     region.endSample - removed);
            if (newEnd > newStart) {
                region.startSample = newStart;
                region.endSample = newEnd;
                result.push_back(region);
            }
        }
    }

    regions.swap(result);
}

void shiftRegionsAfterInsertion(std::vector<EditRegion>& regions,
                                int64_t atFrame,
                                int64_t inserted) {
    if (inserted <= 0) return;
    for (EditRegion& region : regions) {
        if (region.startSample >= atFrame) {
            region.startSample += inserted;
            region.endSample += inserted;
        } else if (region.endSample > atFrame) {
            region.endSample += inserted;
        }
    }
}

} // namespace

// ===========================================================================
//  Operazioni sul buffer
// ===========================================================================
bool AudioDocument::deleteRange(int64_t startFrame, int64_t endFrame) {
    if (!loaded_) return false;

    const int channels = std::max(1, buffer_.channels);
    int64_t start = std::max<int64_t>(0, std::min(startFrame, frameCount_));
    int64_t end = std::max<int64_t>(0, std::min(endFrame, frameCount_));
    if (end <= start) return false;

    UndoRecord record = captureState("Elimina intervallo");

    InverseStep step;
    step.mode = InverseStep::Mode::Insert;
    step.position = start;
    step.samples.assign(
        buffer_.samples.begin() + static_cast<std::ptrdiff_t>(start * channels),
        buffer_.samples.begin() + static_cast<std::ptrdiff_t>(end * channels));
    record.steps.push_back(std::move(step));

    buffer_.samples.erase(
        buffer_.samples.begin() + static_cast<std::ptrdiff_t>(start * channels),
        buffer_.samples.begin() + static_cast<std::ptrdiff_t>(end * channels));

    rebuildFrameCount();
    shiftRegionsAfterDeletion(regions_, start, end);
    clampRegionsToBuffer();
    pushUndo(std::move(record));
    return true;
}

// ---------------------------------------------------------------------------
//  Accorciamento delle regioni in una sola operazione annullabile
// ---------------------------------------------------------------------------
int AudioDocument::shortenRegions(DetectionKind kind, float fraction, bool keepHead) {
    if (!loaded_) return 0;
    if (fraction <= 0.0f) return 0;
    fraction = std::min(0.99f, fraction);

    const int channels = std::max(1, buffer_.channels);

    // Si calcolano PRIMA tutti gli intervalli da togliere, poi si applicano
    // dall'ultimo al primo.
    //
    // L'ordine e' obbligatorio: ogni taglio sposta indietro tutto cio' che
    // segue, quindi procedendo in avanti le posizioni calcolate in precedenza
    // non varrebbero piu'. Andando al contrario, un taglio non tocca mai le
    // posizioni che devono ancora essere usate.
    std::vector<std::pair<int64_t, int64_t>> cuts;
    cuts.reserve(regions_.size());

    for (const EditRegion& region : regions_) {
        if (region.kind != kind || !region.enabled) continue;

        const int64_t length = region.endSample - region.startSample;
        if (length <= 1) continue;

        int64_t remove = static_cast<int64_t>(std::llround(static_cast<double>(length) * fraction));
        if (remove < 1) remove = 1;
        if (remove >= length) remove = length - 1;   // almeno un campione resta

        // keepHead: si toglie la coda. Il punto di giunzione cade nella parte
        // finale del respiro, che e' la piu' quieta; togliendo la testa si
        // attaccherebbe la pausa a un attacco forte, e si sentirebbe.
        const int64_t cutStart = keepHead ? (region.endSample - remove) : region.startSample;
        cuts.emplace_back(cutStart, cutStart + remove);
    }

    if (cuts.empty()) return 0;

    UndoRecord record = captureState("Accorcia le regioni");

    int shortened = 0;
    for (auto it = cuts.rbegin(); it != cuts.rend(); ++it) {
        const int64_t start = std::max<int64_t>(0, std::min(it->first, frameCount_));
        const int64_t end = std::max<int64_t>(0, std::min(it->second, frameCount_));
        if (end <= start) continue;

        InverseStep step;
        step.mode = InverseStep::Mode::Insert;
        step.position = start;
        step.samples.assign(
            buffer_.samples.begin() + static_cast<std::ptrdiff_t>(start * channels),
            buffer_.samples.begin() + static_cast<std::ptrdiff_t>(end * channels));
        record.steps.push_back(std::move(step));

        buffer_.samples.erase(
            buffer_.samples.begin() + static_cast<std::ptrdiff_t>(start * channels),
            buffer_.samples.begin() + static_cast<std::ptrdiff_t>(end * channels));

        rebuildFrameCount();
        shiftRegionsAfterDeletion(regions_, start, end);
        clampRegionsToBuffer();
        ++shortened;
    }

    if (shortened == 0) return 0;

    // Un solo record per tutti i tagli: un Ctrl+Z annulla l'intero
    // accorciamento, non un respiro per volta.
    pushUndo(std::move(record));
    return shortened;
}

bool AudioDocument::silenceRange(int64_t startFrame, int64_t endFrame) {    if (!loaded_) return false;

    const int channels = std::max(1, buffer_.channels);
    int64_t start = std::max<int64_t>(0, std::min(startFrame, frameCount_));
    int64_t end = std::max<int64_t>(0, std::min(endFrame, frameCount_));
    if (end <= start) return false;

    UndoRecord record = captureState("Silenzia intervallo");

    InverseStep step;
    step.mode = InverseStep::Mode::Replace;
    step.position = start;
    step.samples.assign(
        buffer_.samples.begin() + static_cast<std::ptrdiff_t>(start * channels),
        buffer_.samples.begin() + static_cast<std::ptrdiff_t>(end * channels));
    record.steps.push_back(std::move(step));

    std::fill(buffer_.samples.begin() + static_cast<std::ptrdiff_t>(start * channels),
              buffer_.samples.begin() + static_cast<std::ptrdiff_t>(end * channels),
              0.0f);

    pushUndo(std::move(record));
    return true;
}

bool AudioDocument::cropToRange(int64_t startFrame, int64_t endFrame) {
    if (!loaded_) return false;

    const int channels = std::max(1, buffer_.channels);
    int64_t start = std::max<int64_t>(0, std::min(startFrame, frameCount_));
    int64_t end = std::max<int64_t>(0, std::min(endFrame, frameCount_));
    if (end <= start) return false;
    if (start == 0 && end == frameCount_) return false;   // nulla da ritagliare

    UndoRecord record = captureState("Ritaglia");

    // Per tornare indietro si reinserisce prima la testa e poi la coda: la coda
    // va inserita alla fine del blocco conservato, non alla sua posizione
    // originale.
    if (start > 0) {
        InverseStep head;
        head.mode = InverseStep::Mode::Insert;
        head.position = 0;
        head.samples.assign(
            buffer_.samples.begin(),
            buffer_.samples.begin() + static_cast<std::ptrdiff_t>(start * channels));
        record.steps.push_back(std::move(head));
    }

    if (end < frameCount_) {
        InverseStep tail;
        tail.mode = InverseStep::Mode::Insert;
        tail.position = end - start;
        tail.samples.assign(
            buffer_.samples.begin() + static_cast<std::ptrdiff_t>(end * channels),
            buffer_.samples.end());
        record.steps.push_back(std::move(tail));
    }

    buffer_.samples.erase(
        buffer_.samples.begin() + static_cast<std::ptrdiff_t>(end * channels),
        buffer_.samples.end());
    buffer_.samples.erase(
        buffer_.samples.begin(),
        buffer_.samples.begin() + static_cast<std::ptrdiff_t>(start * channels));

    // Le regioni seguono la stessa trasformazione della selezione.
    std::vector<EditRegion> kept;
    for (EditRegion region : regions_) {
        if (region.endSample <= start || region.startSample >= end) continue;
        region.startSample = std::max<int64_t>(0, region.startSample - start);
        region.endSample = std::min<int64_t>(end - start, region.endSample - start);
        if (region.endSample > region.startSample) {
            kept.push_back(region);
        }
    }
    regions_.swap(kept);

    rebuildFrameCount();
    clampRegionsToBuffer();
    pushUndo(std::move(record));
    return true;
}

bool AudioDocument::insertSilence(int64_t atFrame, int64_t frames) {
    if (!loaded_ || frames <= 0) return false;

    const int channels = std::max(1, buffer_.channels);
    const int64_t at = std::max<int64_t>(0, std::min(atFrame, frameCount_));

    UndoRecord record = captureState("Inserisci silenzio");

    InverseStep step;
    step.mode = InverseStep::Mode::Remove;
    step.position = at;
    step.frames = frames;
    record.steps.push_back(std::move(step));

    buffer_.samples.insert(
        buffer_.samples.begin() + static_cast<std::ptrdiff_t>(at * channels),
        static_cast<size_t>(frames) * static_cast<size_t>(channels),
        0.0f);

    rebuildFrameCount();
    shiftRegionsAfterInsertion(regions_, at, frames);
    clampRegionsToBuffer();
    pushUndo(std::move(record));
    return true;
}

bool AudioDocument::applyGainToRange(int64_t startFrame, int64_t endFrame, float db) {
    if (!loaded_) return false;

    const int channels = std::max(1, buffer_.channels);
    int64_t start = std::max<int64_t>(0, std::min(startFrame, frameCount_));
    int64_t end = std::max<int64_t>(0, std::min(endFrame, frameCount_));
    if (end <= start) return false;

    const float gain = dbToGain(db);
    if (std::abs(gain - 1.0f) < 1.0e-6f) return false;

    UndoRecord record = captureState("Guadagno sull'intervallo");

    InverseStep step;
    step.mode = InverseStep::Mode::Replace;
    step.position = start;
    step.samples.assign(
        buffer_.samples.begin() + static_cast<std::ptrdiff_t>(start * channels),
        buffer_.samples.begin() + static_cast<std::ptrdiff_t>(end * channels));
    record.steps.push_back(std::move(step));

    for (int64_t i = start * channels; i < end * channels; ++i) {
        buffer_.samples[static_cast<size_t>(i)] *= gain;
    }

    pushUndo(std::move(record));
    return true;
}

// ===========================================================================
//  Regioni
// ===========================================================================
int AudioDocument::addRegion(EditRegion region) {
    if (!loaded_) return -1;
    if (region.endSample <= region.startSample) return -1;

    region.startSample = std::max<int64_t>(0, std::min(region.startSample, frameCount_));
    region.endSample = std::max<int64_t>(region.startSample, std::min(region.endSample, frameCount_));
    if (region.endSample <= region.startSample) return -1;

    UndoRecord record = captureState("Aggiungi regione");
    if (!region.fromDetection)
        recordCorrection(region.kind, region.startSample, region.endSample, true, "aggiunta editoriale");
    regions_.push_back(region);
    pushUndo(std::move(record));
    return static_cast<int>(regions_.size()) - 1;
}

bool AudioDocument::updateRegion(int index, const EditRegion& region, bool userEdit) {
    if (index < 0 || index >= static_cast<int>(regions_.size())) return false;

    UndoRecord record = captureState("Modifica regione");

    EditRegion updated = region;
    updated.startSample = std::max<int64_t>(0, std::min(updated.startSample, frameCount_));
    updated.endSample = std::max<int64_t>(updated.startSample, std::min(updated.endSample, frameCount_));
    if (updated.endSample <= updated.startSample) return false;

    // Explicit editing must survive subsequent analysis even when it remains
    // included and originated in the automatic detector.
    if (userEdit) { updated.decisionSource = "user"; updated.applied = false; }

    regions_[static_cast<size_t>(index)] = updated;
    pushUndo(std::move(record));
    return true;
}

int AudioDocument::applyManualRange(int64_t start, int64_t end, int mode, float reduction, double fade) {
    if (!loaded_ || start < 0 || end > frameCount_ || end <= start || mode < 0 || mode > 3 ||
        !std::isfinite(reduction) || reduction > 0 || !std::isfinite(fade) || fade < 0 || fade > 60000) return -1;
    // Reapplying to the same interval changes the treatment in one Undo,
    // instead of stacking silence/attenuation or conflicting timeline cuts.
    // Other overlaps and labelled detector events remain independent.
    for (size_t i = 0; i < regions_.size(); ++i) {
        const auto& existing = regions_[i];
        if (!existing.manualOperation || existing.startSample != start || existing.endSample != end) continue;
        if (existing.editMode == mode && existing.reductionDb == reduction && existing.fadeMs == fade &&
            existing.enabled && existing.approved) return static_cast<int>(i);
        auto updated = existing;
        updated.editMode = mode; updated.reductionDb = reduction; updated.fadeMs = fade;
        updated.enabled = updated.approved = true;
        updateRegion(static_cast<int>(i), updated);
        return static_cast<int>(i);
    }
    EditRegion region;
    region.startSample = start; region.endSample = end;
    region.editMode = mode; region.reductionDb = reduction; region.fadeMs = fade;
    region.manualOperation = true; region.classification = "manual";
    region.decisionSource = "user"; region.approved = region.enabled = true;
    return addRegion(region);
}

int AudioDocument::restoreManualRange(int64_t start, int64_t end) {
    if (end <= start) return 0;
    int count = 0;
    beginUndoGroup("Ripristina interventi manuali");
    for (int i = static_cast<int>(regions_.size()) - 1; i >= 0; --i) {
        const auto& region = regions_[i];
        if (region.manualOperation && region.startSample < end && region.endSample > start) {
            removeRegion(i); ++count;
        }
    }
    endUndoGroup();
    return count;
}

bool AudioDocument::removeRegion(int index) {
    if (index < 0 || index >= static_cast<int>(regions_.size())) return false;

    UndoRecord record = captureState("Elimina regione");
    regions_.erase(regions_.begin() + index);
    pushUndo(std::move(record));
    return true;
}

bool AudioDocument::setRegionEnabled(int index, bool enabled) {
    if (index < 0 || index >= static_cast<int>(regions_.size())) return false;

    UndoRecord record = captureState(enabled ? "Attiva regione" : "Disattiva regione");
    regions_[static_cast<size_t>(index)].enabled = enabled;
    pushUndo(std::move(record));
    return true;
}

void AudioDocument::markRegionApplied(int index, bool applied) {
    if (index < 0 || index >= static_cast<int>(regions_.size())) return;
    regions_[static_cast<size_t>(index)].applied = applied;
}

void AudioDocument::clearRegions() {
    if (regions_.empty()) return;
    UndoRecord record = captureState("Cancella regioni");
    regions_.clear();
    pushUndo(std::move(record));
}

void AudioDocument::setRegionsFromDetections(const std::vector<Detection>& detections,
                                             const EditSettings& settings) {
    if (!loaded_) return;

    UndoRecord record = captureState("Rilevamento automatico");

    regions_.clear();
    regions_.reserve(detections.size());

    for (const Detection& detection : detections) {
        EditRegion region;
        region.kind = detection.kind;
        region.startSample = std::max<int64_t>(0, detection.startSample);
        region.endSample = std::min<int64_t>(frameCount_, detection.endSample);
        if (region.endSample <= region.startSample) continue;

        region.reductionDb = detection.kind == DetectionKind::Breath
                                 ? settings.breathReductionDb
                                 : settings.clickReductionDb;
        region.repair = detection.kind == DetectionKind::Click && settings.repairClicks;
        region.enabled = true;
        region.approved = !detectionReviewRequired(detection.kind,detection.classification);
        region.classification = detection.classification;
        region.reason = detection.reason;
        region.decisionSource = "automatic";
        region.score = detection.peakScore;
        region.fromDetection = true;
        regions_.push_back(region);
    }

    pushUndo(std::move(record));
}

int AudioDocument::regionIndexAt(int64_t sample) const {
    for (size_t i = 0; i < regions_.size(); ++i) {
        if (sample >= regions_[i].startSample && sample < regions_[i].endSample) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int AudioDocument::regionIndexNearEdge(int64_t sample, int64_t tolerance, int* edge) const {
    if (tolerance < 0) tolerance = 0;

    int best = -1;
    int64_t bestDistance = tolerance + 1;
    int bestEdge = 0;

    for (size_t i = 0; i < regions_.size(); ++i) {
        const int64_t distanceStart = std::abs(regions_[i].startSample - sample);
        const int64_t distanceEnd = std::abs(regions_[i].endSample - sample);

        if (distanceStart <= tolerance && distanceStart < bestDistance) {
            bestDistance = distanceStart;
            best = static_cast<int>(i);
            bestEdge = -1;
        }
        if (distanceEnd <= tolerance && distanceEnd < bestDistance) {
            bestDistance = distanceEnd;
            best = static_cast<int>(i);
            bestEdge = 1;
        }
    }

    if (edge) *edge = bestEdge;
    return best;
}

// ===========================================================================
//  Fade e guadagno generale
// ===========================================================================
AudioDocument::ReconcileResult AudioDocument::reconcileRegions(
    const std::vector<Detection>& detections,
    const EditSettings& settings) {
    ReconcileResult result;
    if (!loaded_) return result;

    beginUndoGroup("Riconcilia rilevamento");
    std::vector<EditRegion> previous = regions_;
    std::vector<char> newUsed(detections.size(), 0);
    std::vector<EditRegion> next;
    next.reserve(previous.size() + detections.size());

    auto overlapping = [&](const EditRegion& region, size_t index) {
        const Detection& detection = detections[index];
        return region.kind == detection.kind &&
               region.startSample < detection.endSample &&
               detection.startSample < region.endSample;
    };

    for (const EditRegion& region : previous) {
        std::vector<size_t> matches;
        bool sharedMatch = false;
        for (size_t i = 0; i < detections.size(); ++i) {
            if (overlapping(region, i)) {
                if (newUsed[i]) sharedMatch = true;
                else matches.push_back(i);
            }
        }
        const bool preserve = !region.repairFeedback.empty() || !region.fromDetection ||
                              region.decisionSource == "user" ||
                              !region.approved || !region.enabled ||
                              region.applied || region.trainingEligible;
        if (matches.size() > 1 || sharedMatch) {
            // Non si sceglie a caso: si conserva la correzione e si segnala.
            ++result.ambiguous;
            next.push_back(region);
            ++result.kept;
            for (size_t i : matches) newUsed[i] = 1;
            continue;
        }
        if (!matches.empty()) {
            newUsed[matches.front()] = 1;
            if (preserve) {
                next.push_back(region);
                ++result.kept;
            } else {
                // Un evento automatico gia' incluso mantiene le proprieta'
                // dell'utente (modo/margine/fade) ma aggiorna lo score.
                EditRegion updated = region;
                updated.score = detections[matches.front()].peakScore;
                updated.classification = detections[matches.front()].classification;
                updated.approved = !detectionReviewRequired(updated.kind,updated.classification);
                updated.reason = detections[matches.front()].reason;
                updated.dspScore = detections[matches.front()].dspScore;
                updated.aiScore = detections[matches.front()].aiScore;
                updated.aiMaxProbability = detections[matches.front()].aiMaxProbability;
                next.push_back(updated);
                ++result.kept;
            }
            continue;
        }
        if (preserve) {
            next.push_back(region);
            ++result.kept;
        } else {
            ++result.removed;
        }
    }

    for (size_t i = 0; i < detections.size(); ++i) {
        if (newUsed[i]) continue;
        const Detection& detection = detections[i];
        EditRegion region;
        region.kind = detection.kind;
        region.startSample = std::max<int64_t>(0, detection.startSample);
        region.endSample = std::min<int64_t>(frameCount_, detection.endSample);
        if (region.endSample <= region.startSample) continue;
        region.reductionDb = detection.kind == DetectionKind::Breath
                                 ? settings.breathReductionDb
                                 : settings.clickReductionDb;
        region.repair = detection.kind == DetectionKind::Click && settings.repairClicks;
        region.enabled = true;
        region.approved = !detectionReviewRequired(detection.kind,detection.classification);
        region.score = detection.peakScore;
        region.fromDetection = true;
        region.decisionSource = "automatic";
        region.classification = detection.classification.empty() ? "dsp" : detection.classification;
        region.reason = detection.reason;
        region.dspScore = detection.dspScore;
        region.aiScore = detection.aiScore;
        region.aiMaxProbability = detection.aiMaxProbability;
        region.trainingEligible = false;
        region.editMode = 0;
        region.fadeMs = settings.attackMs;
        next.push_back(region);
        ++result.added;
    }

    regions_ = std::move(next);
    // La modifica e' gia' avvenuta direttamente: segnala al gruppo che c'e'
    // qualcosa da annullare, senza aggiungere una seconda voce.
    pushUndo(captureState("Riconcilia rilevamento"));
    endUndoGroup();
    clampRegionsToBuffer();
    modified_ = true;
    return result;
}

void AudioDocument::setFade(const FadeSettings& fade) {
    if (!loaded_) return;
    UndoRecord record = captureState("Fade");
    fade_ = fade;
    pushUndo(std::move(record));
}

void AudioDocument::setMasterGainDb(float db) {
    if (!loaded_) return;
    UndoRecord record = captureState("Guadagno generale");
    masterGainDb_ = db;
    pushUndo(std::move(record));
}

// ===========================================================================
//  Rendering
// ===========================================================================
std::vector<float> AudioDocument::gainCurve(const EditSettings& settings, GainCurveMode mode) const {
    std::vector<float> curve;
    if (!loaded_ || frameCount_ <= 0) {
        return curve;
    }

    if (mode == GainCurveMode::StudioSource) {
        curve.assign(static_cast<size_t>(frameCount_), 1);
        for (const auto& region : regions_) {
            if (!region.enabled || !region.approved || (region.editMode != 0 && region.editMode != 3)) continue;
            const auto start = std::max<int64_t>(0, region.startSample);
            const auto end = std::min(frameCount_, region.endSample);
            if (end <= start) continue;
            if (region.editMode == 0 && region.kind == DetectionKind::Click && region.repair &&
                end - start <= buffer_.sampleRate * EditSettings{}.repairMaxMs / 1000.0) continue;
            const float target = region.editMode == 3 ? 0 : dbToGain(region.reductionDb);
            const RangeGainEnvelope envelope(start, end,
                static_cast<int64_t>(std::llround(region.fadeMs * buffer_.sampleRate / 1000.0)), target);
            for (int64_t frame = start; frame < end; ++frame)
                curve[static_cast<size_t>(frame)] = std::min(curve[static_cast<size_t>(frame)], envelope.gainAt(frame));
        }
    } else {
        std::vector<RegionOperation> operations;
        operations.reserve(regions_.size());
        for (const EditRegion& region : regions_) {
            if (!region.enabled) continue;
            RegionOperation operation;
            operation.kind = region.kind;
            operation.startSample = region.startSample;
            operation.endSample = region.endSample;
            operation.reductionDb = region.reductionDb;
            operation.repair = region.repair;
            operation.repairMethod = region.repairMethod;
            operations.push_back(operation);
        }

        curve = RegionEditor::buildGainCurve(frameCount_,
                                              static_cast<double>(buffer_.sampleRate),
                                              operations,
                                              settings);
    }

    // Fade di ingresso / uscita.
    const double sampleRate = static_cast<double>(buffer_.sampleRate);
    if (fade_.inEnabled && fade_.inSeconds > 0.0) {
        const int64_t length = std::min<int64_t>(
            frameCount_, static_cast<int64_t>(std::llround(fade_.inSeconds * sampleRate)));
        for (int64_t i = 0; i < length; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(length);
            curve[static_cast<size_t>(i)] *= fadeShapeValue(fade_.inShape, t);
        }
    }

    if (fade_.outEnabled && fade_.outSeconds > 0.0) {
        const int64_t length = std::min<int64_t>(
            frameCount_, static_cast<int64_t>(std::llround(fade_.outSeconds * sampleRate)));
        for (int64_t i = 0; i < length; ++i) {
            const int64_t index = frameCount_ - length + i;
            const float t = static_cast<float>(length - i) / static_cast<float>(length);
            curve[static_cast<size_t>(index)] *= fadeShapeValue(fade_.outShape, t);
        }
    }

    // Guadagno generale: la stessa curva che disegna l'editor e' quella che
    // viene applicata, cosi' non possono divergere.
    const float master = dbToGain(std::min(0.0f, masterGainDb_));
    if (master < 0.9999f) {
        for (float& value : curve) {
            value *= master;
        }
    }

    return curve;
}

void applyDocumentEnvelope(AudioBuffer& audio, const FadeSettings& fade, float masterGainDb) {
    const int64_t frames = audio.frameCount();
    const int64_t fadeIn = fade.inEnabled ? std::min(frames, static_cast<int64_t>(
        std::llround(fade.inSeconds * audio.sampleRate))) : 0;
    const int64_t fadeOut = fade.outEnabled ? std::min(frames, static_cast<int64_t>(
        std::llround(fade.outSeconds * audio.sampleRate))) : 0;
    const float master = dbToGain(std::min(0.0f, masterGainDb));
    if (fadeIn <= 0 && fadeOut <= 0 && master == 1) return;
    for (int64_t i = 0; i < frames; ++i) {
        float gain = 1;
        if (i < fadeIn) gain *= fadeShapeValue(fade.inShape, float(i) / float(fadeIn));
        if (fadeOut > 0 && i >= frames - fadeOut)
            gain *= fadeShapeValue(fade.outShape, float(frames - i) / float(fadeOut));
        gain *= master;
        for (int c = 0; c < audio.channels; ++c) audio.samples[size_t(i * audio.channels + c)] *= gain;
    }
}

AudioBuffer AudioDocument::render(const EditSettings& settings) const {
    AudioBuffer output = buffer_;

    const std::vector<float> curve = gainCurve(settings);
    if (curve.empty()) {
        return output;
    }

    // I click da riparare vanno interpolati PRIMA di applicare la curva: la
    // riparazione lavora sul segnale originale.
    std::vector<RegionOperation> repairOnly;
    for (const EditRegion& region : regions_) {
        if (!region.enabled || !region.repair) continue;
        if (region.kind != DetectionKind::Click) continue;

        RegionOperation operation;
        operation.kind = region.kind;
        operation.startSample = region.startSample;
        operation.endSample = region.endSample;
        operation.reductionDb = region.reductionDb;
        operation.repair = true;
        repairOnly.push_back(operation);
    }

    if (!repairOnly.empty()) {
        // applyOperations applicherebbe anche la curva: qui interessa solo la
        // riparazione, quindi si azzera l'attenuazione passando un guadagno
        // unitario tramite una copia delle impostazioni con rampe nulle.
        EditSettings repairSettings = settings;
        repairSettings.attackMs = 0.0f;
        repairSettings.releaseMs = 0.0f;
        repairSettings.prePadMs = 0.0f;
        repairSettings.postPadMs = 0.0f;

        for (RegionOperation& operation : repairOnly) {
            operation.reductionDb = 0.0f;
        }
        RegionEditor::applyOperations(output.samples,
                                      output.channels,
                                      static_cast<double>(output.sampleRate),
                                      repairOnly,
                                      repairSettings);
    }

    const int channels = std::max(1, output.channels);
    const int64_t frames = std::min<int64_t>(
        curve.size(), static_cast<int64_t>(output.samples.size()) / channels);

    for (int64_t frame = 0; frame < frames; ++frame) {
        const float gain = curve[static_cast<size_t>(frame)];
        if (gain >= 0.9999f) continue;
        for (int channel = 0; channel < channels; ++channel) {
            output.samples[static_cast<size_t>(frame * channels + channel)] *= gain;
        }
    }

    return output;
}



// ---------------------------------------------------------------------------
//  Correzione dell'utente
// ---------------------------------------------------------------------------
void AudioDocument::recordCorrection(DetectionKind kind, int64_t start, int64_t end,
                                     bool positive, const std::string& source) {
    Correction c;
    c.kind = kind;
    c.startSample = start;
    c.endSample = end;
    c.positive = positive;
    c.source = source;
    corrections_.push_back(c);
}

bool AudioDocument::exportCorrections(const std::string& path, std::string* error) const {
    if (sameFilePath(sourcePath_, path)) {
        if (error) *error = "le correzioni non possono sovrascrivere l'audio originale";
        return false;
    }
    auto escape = [](const std::string& value) {
        std::string out;
        const char* hex = "0123456789abcdef";
        for (unsigned char c : value) {
            if (c == '"' || c == '\\') { out += '\\'; out += char(c); }
            else if (c < 32) { out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
            else out += char(c);
        }
        return out;
    };
    std::ostringstream file;
    file << "{\n  \"schema\": 2,\n  \"file\": \"" << escape(displayName_)
         << "\",\n  \"sample_rate\": " << buffer_.sampleRate << ",\n  \"correzioni\": [\n";
    bool first = true;
    for (const auto& region : regions_) {
        const bool positive = region.kind == DetectionKind::Breath ?
            (region.learningLabel == "breath_confirmed" || region.learningLabel == "missed_breath") :
            (region.learningLabel == "click_confirmed" || region.learningLabel == "missed_click" || region.learningLabel == "mouth_click_confirmed");
        const bool negative = region.kind == DetectionKind::Breath ? region.learningLabel == "false_positive" :
            (region.learningLabel == "click_false_positive" || region.learningLabel == "vocal_transient");
        if (!region.trainingEligible || region.decisionSource != "user" ||
            (!positive && !negative)) continue;
        if (!first) file << ",\n";
        first = false;
        file << "    {\"tipo\": \"" << (region.kind == DetectionKind::Breath ? "respiro" : "click") << "\", \"positivo\": " << (positive ? "true" : "false")
             << ", \"training_eligible\": true, \"motivo\": \"training: " << region.learningLabel
             << "\", \"inizio_s\": " << std::setprecision(17)
             << double(region.startSample) / std::max(1, buffer_.sampleRate)
             << ", \"fine_s\": " << double(region.endSample) / std::max(1, buffer_.sampleRate) << "}";
    }
    file << "\n  ]\n}\n";
    return writeTextAtomic(path, file.str(), error);
}
} // namespace bc

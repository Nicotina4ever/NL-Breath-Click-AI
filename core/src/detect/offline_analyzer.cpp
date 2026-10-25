// SPDX-License-Identifier: MPL-2.0
#include "core/detect/offline_analyzer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

#include "core/dsp/breath_detector.h"
#include "core/dsp/click_detector.h"
#include "core/dsp/frame_features.h"
#include "core/dsp/resampler.h"
#include "core/detect/breath_model_eval.h"
#include "core/detect/candidates.h"
#include "core/detect/segment_features.h"

namespace bc {

namespace {

constexpr float kSilenceDb = -160.0f;

float clampf(float value, float lo, float hi) {
    return value < lo ? lo : (value > hi ? hi : value);
}

// ---------------------------------------------------------------------------
//  Esecuzione sopra soglia con isteresi
// ---------------------------------------------------------------------------
struct ScoreRun {
    int firstFrame = 0;
    int lastFrame = 0;
    float peakScore = 0.0f;
    float meanScore = 0.0f;
};

std::vector<ScoreRun> extractRuns(const std::vector<float>& scores,
                                  float enterThreshold,
                                  float exitThreshold) {
    std::vector<ScoreRun> runs;
    int start = -1;
    float peak = 0.0f;
    double sum = 0.0;
    size_t frames = 0;

    for (size_t i = 0; i < scores.size(); ++i) {
        const float score = scores[i];
        if (start < 0) {
            if (score >= enterThreshold) {
                start = static_cast<int>(i);
                peak = score;
                sum = score;
                frames = 1;
            }
        } else if (score >= exitThreshold) {
            peak = std::max(peak, score);
            sum += score;
            ++frames;
        } else {
            ScoreRun run;
            run.firstFrame = start;
            run.lastFrame = static_cast<int>(i) - 1;
            run.peakScore = peak;
            run.meanScore = frames > 0 ? static_cast<float>(sum / static_cast<double>(frames)) : 0.0f;
            runs.push_back(run);
            start = -1;
        }
    }

    if (start >= 0) {
        ScoreRun run;
        run.firstFrame = start;
        run.lastFrame = static_cast<int>(scores.size()) - 1;
        run.peakScore = peak;
        run.meanScore = frames > 0 ? static_cast<float>(sum / static_cast<double>(frames)) : 0.0f;
        runs.push_back(run);
    }

    return runs;
}

// ---------------------------------------------------------------------------
//  Rifinitura dei confini
// ---------------------------------------------------------------------------
// Restringe una regione ai blocchi in cui l'energia supera una frazione del
// massimo locale, poi aggiunge un margine. Serve a non tagliare il respiro a
// meta' e a non portarsi dietro 20 ms di silenzio per ogni lato.
void trimRegionToEnvelope(const float* samples,
                          int64_t totalSamples,
                          int64_t& start,
                          int64_t& end,
                          double sampleRate,
                          float relativeThreshold,
                          int64_t paddingSamples) {
    if (!samples || end <= start || totalSamples <= 0) return;

    const int64_t blockSamples = std::max<int64_t>(1, static_cast<int64_t>(sampleRate * 0.005)); // 5 ms
    const int64_t length = end - start;
    const int64_t blockCount = std::max<int64_t>(1, (length + blockSamples - 1) / blockSamples);

    float maxRms = 0.0f;
    int64_t firstAbove = -1;
    int64_t lastAbove = -1;

    // Prima passata: RMS per blocco e massimo locale.
    std::vector<float> blockRms(static_cast<size_t>(blockCount), 0.0f);
    for (int64_t b = 0; b < blockCount; ++b) {
        const int64_t blockStart = start + b * blockSamples;
        const int64_t blockEnd = std::min(end, blockStart + blockSamples);
        if (blockEnd <= blockStart) continue;

        double sumSquares = 0.0;
        for (int64_t i = blockStart; i < blockEnd; ++i) {
            const double value = samples[i];
            sumSquares += value * value;
        }
        const float rms = static_cast<float>(
            std::sqrt(sumSquares / static_cast<double>(blockEnd - blockStart)));
        blockRms[static_cast<size_t>(b)] = rms;
        maxRms = std::max(maxRms, rms);
    }

    if (maxRms <= 0.0f) return;

    const float threshold = maxRms * relativeThreshold;
    for (int64_t b = 0; b < blockCount; ++b) {
        if (blockRms[static_cast<size_t>(b)] >= threshold) {
            if (firstAbove < 0) firstAbove = b;
            lastAbove = b;
        }
    }

    if (firstAbove < 0) return;

    const int64_t trimmedStart = start + firstAbove * blockSamples - paddingSamples;
    const int64_t trimmedEnd = start + (lastAbove + 1) * blockSamples + paddingSamples;

    start = std::max<int64_t>(0, trimmedStart);
    end = std::min<int64_t>(totalSamples, std::max(trimmedEnd, trimmedStart + 1));
}

// ---------------------------------------------------------------------------
//  Traccia di attivita' vocale
// ---------------------------------------------------------------------------
//  Traccia di voce
//
//  Non c'e' piu' un modello esterno da interrogare. Si restituisce -1 per ogni
//  frame, che significa "non lo so": il BreathDetector interpreta -1 come "usa
//  la salienza armonica", che calcola da se' per ogni frame.
//
//  E' il gate vocale: la stessa domanda ("qui c'e' voce?") a cui rispondeva il
//  modello, ma senza 13 MB di runtime, senza DLL da caricare e senza un
//  dispositivo da scegliere.
// ---------------------------------------------------------------------------
void buildVoiceTrack(size_t frameCount, std::vector<float>& voicePerFrame) {
    voicePerFrame.assign(frameCount, -1.0f);
}

// Probabilita' del modello, scelta in un posto solo:
//   1. rete caricata da un profilo (rete.json)  -> ha la precedenza;
//   2. rete compilata larga (candidati larghi);
//   3. rete compilata stretta (candidati DSP);
//   4. regressione lineare (--ai / profilo con pesi.json).
float probabilitaDelModello(const DetectorSettings& settings,
                            const BreathSegmentFeatures& f) {
    if (!settings.usaReteNeurale) {
        return breathModelProbability(settings.modelWeights, f);
    }
    if (settings.usaReteDaFile) {
        return reteProbabilityConPesi(settings.retePesi, f);
    }
    return settings.proposteLarghe ? reteProbability(f) : reteProbabilityNarrow(f);
}

} // namespace

const char* detectionKindName(DetectionKind kind) {
    switch (kind) {
        case DetectionKind::Breath: return "Breath";
        case DetectionKind::Click:  return "Click";
    }
    return "Unknown";
}

size_t AnalysisResult::countOf(DetectionKind kind) const {
    size_t total = 0;
    for (const Detection& detection : detections) {
        if (detection.kind == kind) ++total;
    }
    return total;
}

OfflineAnalyzer::OfflineAnalyzer() = default;
OfflineAnalyzer::~OfflineAnalyzer() = default;

void OfflineAnalyzer::reportProgress(float fraction, const char* stage) {
    if (progress_) {
        progress_(clampf(fraction, 0.0f, 1.0f), stage);
    }
}

AnalysisResult OfflineAnalyzer::analyze(const float* samples,
                                        size_t count,
                                        double sampleRate,
                                        const DetectorSettings& settings) {
    const auto started = std::chrono::steady_clock::now();

    AnalysisResult result;
    result.sampleRate = sampleRate;
    result.totalSamples = static_cast<int64_t>(count);
    result.settings = settings;
    auto cancel = [&]() {
        if (!cancelled_ || !cancelled_()) return false;
        result.cancelled = true;
        result.error = "Analisi annullata";
        result.detections.clear();
        return true;
    };
    if (cancel()) return result;

    if (!samples || count == 0) {
        result.error = "Nessun campione audio da analizzare";
        return result;
    }
    if (sampleRate <= 0.0) {
        result.error = "Sample rate non valido";
        return result;
    }

    if (settings.detectBreath && settings.useBreathModel && !embeddedLegacyModelsAvailable()) {
        const bool loaded = settings.usaReteNeurale
            ? (settings.usaReteDaFile && settings.retePesi.caricati)
            : settings.modelWeights.caricati;
        if (!loaded) {
            result.error = "Modello legacy privato non disponibile: caricare un profilo o usare DSP/Fusion";
            return result;
        }
    }

    const int fftSize = std::max(128, settings.fftSize);
    const int hopSize = std::max(16, std::min(settings.hopSize, fftSize));

    if (count < static_cast<size_t>(fftSize)) {
        result.error = "Audio piu' corto di una finestra di analisi (" +
                       std::to_string(fftSize) + " campioni)";
        return result;
    }

    const size_t frameCount = (count - static_cast<size_t>(fftSize)) /
                                  static_cast<size_t>(hopSize) + 1;

    // -------------------------------------------------------------- gate vocale
    // Nessun modello da aprire: il gate e' la salienza armonica, calcolata dal
    // BreathDetector frame per frame.
    result.vadActive = false;
    result.vadStatus = "salienza armonica (nessun modello)";

    reportProgress(0.02f, "Preparazione");

    // ------------------------------------------------------------ impostazioni
    FrameAnalyzer::Config frameConfig;
    frameConfig.fftSize = fftSize;
    frameConfig.sampleRate = static_cast<float>(sampleRate);

    FrameAnalyzer analyzer(frameConfig);

    ClickDetectorConfig clickConfig;
    clickConfig.sampleRate = static_cast<float>(sampleRate);
    clickConfig.sensitivity = settings.clickSensitivity;
    ClickDetector clickDetector(clickConfig);
    BreathDetectorConfig breathConfig;
    breathConfig.sampleRate = static_cast<float>(sampleRate);
    breathConfig.sensitivity = settings.breathSensitivity;
    BreathDetector breathDetector(breathConfig);

    std::vector<float> voicePerFrame;
    buildVoiceTrack(frameCount, voicePerFrame);

    reportProgress(0.25f, "Analisi dei frame");

    // ------------------------------------------------------------- passata 1
    // I respiri si analizzano con finestre lunghe (decine di ms), i click con
    // sotto-finestre brevissime: sono fenomeni su scale temporali diverse.
    std::vector<float> breathScores(frameCount, 0.0f);

    // Le tracce per frame vivono in un posto solo: le usa il gate dei respiri,
    // il calcolo delle 14 grandezze e il dump dei candidati.
    FrameTracks tracks;
    tracks.hopSize = hopSize;
    tracks.sampleRate = sampleRate;
    tracks.harmonicity.assign(frameCount, 0.0f);
    tracks.flatness.assign(frameCount, 0.0f);
    tracks.levelDb.assign(frameCount, 0.0f);
    tracks.centroid.assign(frameCount, 0.0f);
    tracks.zcr.assign(frameCount, 0.0f);
    tracks.lowBand.assign(frameCount, 0.0f);
    tracks.midBand.assign(frameCount, 0.0f);
    tracks.highBand.assign(frameCount, 0.0f);

    const bool reuse = cache_ && cache_->sampleRate == sampleRate &&
        cache_->fftSize == fftSize && cache_->hopSize == hopSize &&
        cache_->frames.size() == frameCount && cache_->samples.size() == count &&
        std::equal(cache_->samples.begin(), cache_->samples.end(), samples);
    std::vector<FrameFeatures> freshFrames;
    if (cache_ && !reuse) freshFrames.reserve(frameCount);
    result.framesReused = reuse ? frameCount : 0;

    for (size_t f = 0; f < frameCount; ++f) {
        if (cancel()) return result;
        const size_t start = f * static_cast<size_t>(hopSize);
        const FrameFeatures features = reuse ? cache_->frames[f] :
            analyzer.analyze(samples + start, static_cast<size_t>(fftSize));
        if (cache_ && !reuse) freshFrames.push_back(features);

        tracks.harmonicity[f] = features.harmonicRatio;
        tracks.flatness[f] = features.spectralFlatness;
        tracks.centroid[f] = features.spectralCentroid;
        tracks.zcr[f] = features.zcr;
        tracks.lowBand[f] = features.lowBandRatio;
        tracks.midBand[f] = features.midBandRatio;
        tracks.highBand[f] = features.highBandRatio;
        tracks.levelDb[f] = features.levelDb;

        if (settings.detectBreath) {
            const float voice = voicePerFrame.empty() ? -1.0f : voicePerFrame[f];
            breathScores[f] = breathDetector.process(features, voice).score;
        }

        if ((f & 0x3FFFu) == 0 && frameCount > 0x3FFFu) {
            reportProgress(0.25f + 0.30f * (static_cast<float>(f) / static_cast<float>(frameCount)),
                           "Analisi dei frame");
        }
    }

    if (cache_ && !reuse) {
        cache_->samples.assign(samples, samples + count);
        cache_->frames = std::move(freshFrames);
        cache_->sampleRate = sampleRate;
        cache_->fftSize = fftSize;
        cache_->hopSize = hopSize;
    }

    // Passata dedicata ai click: sotto-finestre da ~1.3 ms.
    int subWindow = 0;
    std::vector<float> clickScores;
    std::vector<float> clickRatios;
    std::vector<float> clickNarrowness;

    if (settings.detectClicks) {
        subWindow = clickDetector.subWindowSamples();
        const size_t subCount = count / static_cast<size_t>(subWindow);
        clickScores.assign(subCount, 0.0f);
        clickRatios.assign(subCount, 0.0f);
        clickNarrowness.assign(subCount, 0.0f);

        for (size_t s = 0; s < subCount; ++s) {
            if (cancel()) return result;
            const ClickScore score =
                clickDetector.processBlock(samples + s * static_cast<size_t>(subWindow),
                                           static_cast<size_t>(subWindow));
            clickScores[s] = score.score;
            clickRatios[s] = score.ratio;
            clickNarrowness[s] = score.narrowness;
        }

        reportProgress(0.58f, "Analisi dei click");
    }

    result.framesAnalyzed = frameCount;
    const std::vector<float>& frameLevels = tracks.levelDb;
    tracks.noiseFloorDb = breathDetector.noiseFloorDb();
    tracks.speechLevelDb = speechLevelDb75(tracks.levelDb);
    tracks.pauses = findPauses(tracks.levelDb, hopSize, sampleRate);
    result.noiseFloorDb = tracks.noiseFloorDb;

    reportProgress(0.72f, "Costruzione delle regioni");

    // ------------------------------------------------------------- diagnosi
    // Livello di voce mediano, utile per capire se il gate sta funzionando.
    {
        double sum = 0.0;
        size_t n = 0;
        for (float value : voicePerFrame) {
            if (value >= 0.0f) {
                sum += value;
                ++n;
            }
        }
        result.voiceFloorDb = n > 0 ? static_cast<float>(sum / static_cast<double>(n)) : -160.0f;
    }

    const int64_t totalSamples = static_cast<int64_t>(count);
    const double msToSamples = sampleRate / 1000.0;

    const float breathEnter = settings.breathThreshold;
    const float breathExit = settings.breathThreshold * 0.60f;
    const float clickEnter = settings.clickThreshold;
    const float clickExit = settings.clickThreshold * 0.60f;

    std::vector<Detection> detections;

    // ------------------------------------------------------------------ click
    if (settings.detectClicks) {
        const auto clicks = detectMouthClicks(samples,count,sampleRate,settings,cancel);
        detections.insert(detections.end(),clicks.begin(),clicks.end());
    }

    // ---------------------------------------------------------------- respiri
    //
    // Due generatori possibili:
    //  - DSP (default): sequenze del punteggio sopra soglia. Veloce, ma un
    //    fiato tonale non supera le porte e non arriva mai alla rete;
    //  - larghi (--proposte-larghe + --rete): ogni regione con armonicita'
    //    < 0.90 e >= 40 ms. La decisione la prende la rete, non le porte.
    if (settings.detectBreath && settings.usaReteNeurale && settings.proposteLarghe) {
        const std::vector<WideCandidate> candidati = findWideCandidates(
            tracks.harmonicity, tracks.levelDb, hopSize, sampleRate);
        for (const WideCandidate& c : candidati) {
            if (cancel()) return result;
            const int64_t start = static_cast<int64_t>(c.firstFrame) * hopSize;
            const int64_t end = (static_cast<int64_t>(c.lastFrame) + 1) * hopSize;
            if (end <= start || start >= totalSamples) continue;

            Detection detection;
            detection.kind = DetectionKind::Breath;
            detection.startSample = std::max<int64_t>(0, start);
            detection.endSample = std::min<int64_t>(totalSamples, end);
            if (detection.endSample <= detection.startSample) continue;

            const BreathSegmentFeatures f = computeSegmentFeatures(
                tracks, c.firstFrame, c.lastFrame, start, end);
            const float probabilita = probabilitaDelModello(settings, f);
            detection.modelScore = probabilita;
            detection.peakScore = probabilita;
            detection.meanScore = probabilita;
            detections.push_back(detection);
        }
    } else if (settings.detectBreath) {
        const std::vector<ScoreRun> runs = extractRuns(breathScores, breathEnter, breathExit);
        const int64_t padSamples = std::max<int64_t>(1, static_cast<int64_t>(msToSamples * 8.0));
        const int64_t minSamples = std::max<int64_t>(1, static_cast<int64_t>(msToSamples * settings.minBreathMs));
        const int64_t maxSamples = std::max<int64_t>(minSamples, static_cast<int64_t>(msToSamples * settings.maxBreathMs));
        // ------------------------------------------------------------------
        //  Rilevamento delle pause
        //
        //  Un respiro sta in una pausa. Una sibilante sta dentro una parola.
        //  E' l'unico discriminante che li separi davvero: flatness, armonicita'
        //  e livello descrivono COME suona un evento, non DOVE sta, e una "s" e
        //  un respiro suonano quasi identici.
        //
        //  Misurato sul materiale reale: dei 28 candidati di un take, 27
        //  cadevano dentro il parlato e solo 1 in una pausa. Quelli erano
        //  sibilanti, e nessuna soglia sulle tre grandezze poteva escluderli.
        //
        //  Qui si trovano le pause una volta sola, dall'inviluppo di energia:
        //  nessun modello, solo la definizione di pausa - energia bassa e
        //  sostenuta. Poi un candidato vale solo se ci cade dentro.
        // ------------------------------------------------------------------
        // Livello del parlato nel file: il 75esimo percentile dei livelli per
        // frame. Serve come riferimento per la soglia dei respiri.
        //
        // Si usa un percentile e non il livello medio perche' il parlato ha
        // pause e silenzi che abbasserebbero la media, portando la soglia
        // troppo in basso.
        // P75 dei frame, calcolato una volta sola in speechLevelDb75: la stessa
        // definizione usata per il modello e per il dump dei candidati.
        const float loudLevelDb = tracks.speechLevelDb;
        for (const ScoreRun& run : runs) {
            if (cancel()) return result;
            int64_t start = static_cast<int64_t>(run.firstFrame) * hopSize;
            int64_t end = static_cast<int64_t>(run.lastFrame) * hopSize + fftSize;
            start = std::max<int64_t>(0, start);
            end = std::min<int64_t>(totalSamples, end);
            if (end <= start) continue;

            // Confini precisi seguendo l'inviluppo di energia.
            trimRegionToEnvelope(samples, totalSamples, start, end, sampleRate, 0.20f, padSamples);
            if (end <= start) continue;

            const int64_t length = end - start;
            if (length < minSamples || length > maxSamples) continue;
            // Un respiro sta sotto il livello del parlato.
            //
            // Misurato sui 13 take reali, 1568 candidati etichettati dal
            // contesto (in pausa = respiro, in voce = consonante):
            //
            //     respiro    livello mediano -43 dB
            //     consonante livello mediano -28 dB
            //
            // Quindici decibel di distanza. E le altre feature NON separano:
            // flatness 0.308 contro 0.332, armonicita' 0.430 contro 0.408. Sono
            // sovrapposte, quindi nessuna soglia su quelle due puo' funzionare,
            // per quanto la si giri.
            //
            // Il livello invece separa, e il punto di mezzo sta 15 dB sotto il
            // livello del parlato. Che e' anche fisicamente sensato: una
            // consonante sta dentro una parola e viene spinta dalla voce, un
            // respiro sta in un silenzio.
            if (settings.requirePauseForBreath && loudLevelDb > -140.0f) {
                float somma = 0.0f;
                size_t conta = 0;
                for (size_t f = run.firstFrame; f <= run.lastFrame && f < frameLevels.size(); ++f) {
                    if (frameLevels[f] > -150.0f) { somma += frameLevels[f]; ++conta; }
                }
                if (conta > 0) {
                    const float livelloCandidato = somma / static_cast<float>(conta);

                    // Livello dei DINTORNI, escludendo il candidato stesso.
                    //
                    // E' questo che separa, non il livello del candidato: un
                    // respiro non e' molto piu' piano di cio' che lo circonda,
                    // mentre una consonante sta in mezzo a parlato forte.
                    //
                    // Misurato sui take reali:
                    //   consonante -28,3 dB con dintorni -21,9  (6 dB PIU' FORTI)
                    //   respiro    -35,1 dB con dintorni -33,5  (quasi uguali)
                    //   respiro    -33,4 dB con dintorni -33,0  (quasi uguali)
                    //
                    // Sul livello assoluto del candidato i due gruppi si
                    // sovrappongono (-33,4 compare in entrambi), quindi nessuna
                    // soglia su quello poteva funzionare.
                    const size_t raggio = static_cast<size_t>(
                        std::max<int64_t>(1, static_cast<int64_t>(msToSamples * 250.0) / hopSize));
                    const size_t daFrame = run.firstFrame > raggio ? run.firstFrame - raggio : 0;
                    const size_t aFrame = std::min(frameLevels.size() - 1, run.lastFrame + raggio);

                    // MEDIANA del livello dei dintorni, non media.
                    //
                    // La media e' trascinata da pochi frame forti: se il respiro
                    // sta in una pausa breve, la finestra di +-250 ms esce dalla
                    // pausa e tocca la voce, e bastano pochi frame per far
                    // sembrare "parlato" tutto l'intorno.
                    //
                    // La mediana chiede che l'intorno sia quieto PER META', che
                    // e' esattamente la definizione di pausa.
                    std::vector<float> vicini;
                    vicini.reserve((aFrame - daFrame) + 1);
                    for (size_t f = daFrame; f <= aFrame; ++f) {
                        if (f >= run.firstFrame && f <= run.lastFrame) continue;
                        if (frameLevels[f] > -150.0f) vicini.push_back(frameLevels[f]);
                    }

                    float livelloVicini = -160.0f;
                    if (!vicini.empty()) {
                        std::sort(vicini.begin(), vicini.end());
                        livelloVicini = vicini[vicini.size() / 2];
                    }
                    const size_t contaVicini = vicini.size();
                    // Due condizioni, non una.
                    //
                    //  1. i dintorni non devono essere piu' forti del candidato:
                    //     una consonante sta in mezzo a parlato forte.
                    //  2. il candidato non deve essere a livello di parlato:
                    //     un respiro sta in un silenzio, quindi e' piano anche
                    //     in assoluto.
                    //
                    // Da sola la prima lascia passare consonanti che stanno in un
                    // passaggio piano; da sola la seconda non distingue un
                    // respiro da un transiente isolato.
                    // I dintorni non devono essere piu' forti del candidato.
                    //
                    // E' questo che separa, non il livello assoluto del
                    // candidato: misurato sui take reali, un respiro a -35 dB ha
                    // dintorni a -33, una consonante a -28 dB ha dintorni a -22.
                    // Sul livello del candidato i due gruppi si sovrappongono.
                    //
                    // Si e' provato ad aggiungere anche una condizione sul
                    // livello assoluto ("non a livello di parlato"): scarta i
                    // respiri del segnale di prova, quindi e' stata tolta.
                    // La tolleranza segue la sensibilita': senza questo il
                    // cursore muove le tre soglie ma il gate resta fermo a 3 dB
                    // e continua a scartare, quindi sembra che non faccia nulla.
                    //
                    //   0.0 -> 0 dB    severo: solo cio' che sta in un silenzio vero
                    //   0.5 -> 3 dB    fabbrica
                    //   1.0 -> 9 dB    permissivo: passa anche cio' che ha parlato vicino
                    const float tolleranza =
                        3.0f + (settings.breathSensitivity - 0.5f) * 12.0f;

                    if (contaVicini > 0 && livelloVicini > livelloCandidato + tolleranza) {
                        continue;   // si sta parlando attorno: e' una consonante
                    }
                }
            }

            Detection detection;
            detection.kind = DetectionKind::Breath;
            detection.startSample = start;
            detection.endSample = end;
            detection.peakScore = run.peakScore;
            detection.meanScore = run.meanScore;
            detections.push_back(detection);
        }
    }

    // ------------------------------------------------- modello addestrato
    //
    // Dopo che il DSP ha proposto i candidati, il modello compilato da'
    // un secondo parere su ciascuno. Le 14 grandezze arrivano da
    // computeSegmentFeatures: sono le stesse che il dataset di addestramento
    // ottiene dal dump dei candidati, non una seconda implementazione.
    if (settings.useBreathModel && !frameLevels.empty() && hopSize > 0) {
        for (Detection& detection : detections) {
            if (cancel()) return result;
            if (detection.kind != DetectionKind::Breath) continue;

            const int primo = static_cast<int>(
                std::max<int64_t>(0, detection.startSample / hopSize));
            const int ultimo = static_cast<int>(
                std::max<int64_t>(0, detection.endSample / hopSize));
            if (ultimo < primo) continue;

            const BreathSegmentFeatures f = computeSegmentFeatures(
                tracks, primo, ultimo, detection.startSample, detection.endSample);

            detection.modelScore = probabilitaDelModello(settings, f);
        }
    }

    // ------------------------------------------------------------------- merge
    // I click sono eventi discreti: si fondono solo se praticamente contigui.
    // Un respiro invece e' un fenomeno continuo e tollera un buco maggiore.
    std::sort(detections.begin(), detections.end(),
              [](const Detection& a, const Detection& b) {
                  if (a.kind != b.kind) return a.kind < b.kind;
                  return a.startSample < b.startSample;
              });

    const int64_t generalMergeGap =
        std::max<int64_t>(0, static_cast<int64_t>(msToSamples * settings.mergeGapMs));
    const int64_t clickMergeGap =
        std::max<int64_t>(0, static_cast<int64_t>(msToSamples * 0.0));
    const int64_t maxClickSamplesForMerge =
        std::max<int64_t>(1, static_cast<int64_t>(msToSamples * settings.maxClickMs));

    std::vector<Detection> merged;
    merged.reserve(detections.size());

    for (const Detection& detection : detections) {
        const int64_t mergeGap =
            detection.kind == DetectionKind::Click ? clickMergeGap : generalMergeGap;

        if (!merged.empty() &&
            merged.back().kind == detection.kind &&
            detection.startSample <= merged.back().endSample + mergeGap) {
            Detection& last = merged.back();
            last.endSample = std::max(last.endSample, detection.endSample);
            last.peakScore = std::max(last.peakScore, detection.peakScore);
            // Media pesata sulla durata.
            const double totalLength =
                static_cast<double>(last.lengthSamples() + detection.lengthSamples());
            if (totalLength > 0.0) {
                last.meanScore = static_cast<float>(
                    (last.meanScore * static_cast<double>(last.lengthSamples()) +
                     detection.meanScore * static_cast<double>(detection.lengthSamples())) /
                    totalLength);
            }
        } else {
            merged.push_back(detection);
        }
    }

    // Rete di sicurezza: dopo la fusione un click non puo' comunque superare la
    // durata massima, altrimenti il "click" e' in realta' un transiente lungo
    // (attacco di nota, plosiva) o l'attacco di un respiro.
    merged.erase(std::remove_if(merged.begin(), merged.end(),
                                [maxClickSamplesForMerge](const Detection& detection) {
                                    return detection.kind == DetectionKind::Click &&
                                           detection.lengthSamples() > maxClickSamplesForMerge;
                                }),
                 merged.end());

    // ------------------------------------------- filtro del modello addestrato
    //
    // Si scarta QUI, dopo il merge e dopo la verifica incrociata: due mezzi
    // fiati fusi in uno solo e' meglio che scartarne uno e tenere l'altro.
    if (settings.useBreathModel) {
        const size_t prima = merged.size();
        merged.erase(
            std::remove_if(merged.begin(), merged.end(),
                           [&settings](const Detection& d) {
                               if (d.kind != DetectionKind::Breath) return false;
                               if (d.modelScore < 0.0f) return false;   // non valutato
                               return d.modelScore < settings.breathModelThreshold;
                           }),
            merged.end());

        result.filteredByModel = static_cast<int>(prima - merged.size());
    }

    std::sort(merged.begin(), merged.end(), [](const Detection& a, const Detection& b) {
        return a.startSample < b.startSample;
    });

    if (cancel()) return result;
    result.detections = std::move(merged);

    // Copia dei punteggi per la taratura, solo se richiesto.
    if (settings.includeScores) {
        result.breathScores = breathScores;
        result.clickScores = clickScores;
        result.clickRatios = clickRatios;
        result.clickNarrowness = clickNarrowness;
        result.voiceProbabilities = voicePerFrame;
        result.frameHarmonicity = tracks.harmonicity;
        result.frameFlatness = tracks.flatness;
        result.frameLevelDb = tracks.levelDb;
        result.frameCentroid = tracks.centroid;
        result.frameZcr = tracks.zcr;
        result.frameLowBand = tracks.lowBand;
        result.frameMidBand = tracks.midBand;
        result.frameHighBand = tracks.highBand;
        result.hopSize = hopSize;
        result.clickSubWindow = subWindow;
    }

    const auto finished = std::chrono::steady_clock::now();
    result.analysisSeconds =
        std::chrono::duration<double>(finished - started).count();

    reportProgress(1.0f, "Completato");
    return result;
}

} // namespace bc

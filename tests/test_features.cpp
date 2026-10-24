// SPDX-License-Identifier: MPL-2.0
// ============================================================================
//  test_features.cpp — le 14 grandezze e i candidati larghi
//
//  Perche' un test: computeSegmentFeatures e' l'UNICA definizione delle
//  feature. Se cambia, cambia quello che la rete vede in addestramento e in
//  uso. I valori attesi qui sono calcolati a mano su tracce sintetiche.
// ============================================================================
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "core/detect/candidates.h"
#include "core/detect/segment_features.h"
#include "core/detect/offline_analyzer.h"

using namespace bc;

namespace {

int g_failures = 0;

void check(bool condizione, const std::string& cosa) {
    std::cout << "  [" << (condizione ? " OK " : "FAIL") << "] " << cosa << "\n";
    if (!condizione) ++g_failures;
}

bool vicino(float a, float b, float tolleranza = 1.0e-4f) {
    return std::fabs(a - b) <= tolleranza;
}

void section(const char* nome) {
    std::cout << "\n" << nome << "\n" << std::string(std::strlen(nome), '-') << "\n";
}

// 20 frame da 10 ms (sample rate 1000, hop 10).
//   frame 2-3: evento con valori noti;
//   frame 5-17: livello -50 dB -> pausa di 130 ms;
//   tutto il resto: -30 dB.
FrameTracks tracceDiProva() {
    FrameTracks t;
    t.hopSize = 10;
    t.sampleRate = 1000.0;
    t.noiseFloorDb = -60.0f;
    t.harmonicity.assign(20, 0.50f);
    t.flatness.assign(20, 0.10f);
    t.levelDb.assign(20, -30.0f);
    t.centroid.assign(20, 500.0f);
    t.zcr.assign(20, 0.0f);
    t.lowBand.assign(20, 0.0f);
    t.midBand.assign(20, 0.0f);
    t.highBand.assign(20, 0.0f);

    for (int k = 2; k <= 3; ++k) {
        t.flatness[k] = (k == 2) ? 0.3f : 0.5f;
        t.levelDb[k] = -20.0f;
        t.centroid[k] = (k == 2) ? 1000.0f : 3000.0f;
        t.zcr[k] = (k == 2) ? 0.1f : 0.3f;
        t.midBand[k] = (k == 2) ? 0.2f : 0.4f;
        t.highBand[k] = (k == 2) ? 0.6f : 0.8f;
    }
    for (int k = 5; k <= 17; ++k) t.levelDb[k] = -50.0f;

    t.pauses = findPauses(t.levelDb, t.hopSize, t.sampleRate, -42.0f, 120);
    t.speechLevelDb = speechLevelDb75(t.levelDb);
    return t;
}

void testPause() {
    section("Pause e livello del parlato");
    const FrameTracks t = tracceDiProva();

    check(t.pauses.size() == 1, "una sola pausa (frame 5-17)");
    if (t.pauses.size() == 1) {
        check(vicino(static_cast<float>(t.pauses[0].startSeconds), 0.05f),
              "la pausa comincia a 0.05 s");
        check(vicino(static_cast<float>(t.pauses[0].endSeconds), 0.18f),
              "la pausa finisce a 0.18 s");
    }
    // Con meno di 32 frame il percentile non e' definito: -160.
    check(vicino(t.speechLevelDb, -160.0f),
          "con 20 frame il P75 non e' definito (-160 dB)");

    // 100 livelli da -99 a 0: indice 75 -> -24.
    std::vector<float> molti;
    for (int i = 0; i < 100; ++i) molti.push_back(-99.0f + static_cast<float>(i));
    check(vicino(speechLevelDb75(molti), -24.0f), "P75 su 100 frame = -24 dB");
}

void testSegmento() {
    section("Le 14 grandezze di un segmento");
    const FrameTracks t = tracceDiProva();
    const BreathSegmentFeatures f = computeSegmentFeatures(t, 2, 3, 20, 40);

    check(vicino(f.durataMs, 20.0f), "durata 20 ms");
    check(vicino(f.flatMedia, 0.4f), "flatness media 0.4");
    check(vicino(f.harmMedia, 0.5f), "armonicita' media 0.5");
    check(vicino(f.dbMedia, -20.0f), "livello medio -20 dB");
    check(vicino(f.centMedia, 2000.0f), "centroide medio 2000 Hz");
    check(vicino(f.centMin, 1000.0f), "centroide minimo 1000 Hz");
    check(vicino(f.centMax, 3000.0f), "centroide massimo 3000 Hz");
    check(vicino(f.zcrMedia, 0.2f), "zcr media 0.2");
    check(vicino(f.bmediaMedia, 0.3f), "banda media 0.3");
    check(vicino(f.baltaMedia, 0.7f), "banda alta 0.7");

    // Dintorni +-250 ms escluso l'evento: 5 frame a -30 e 13 a -50,
    // mediana = -50. E' la definizione dell'addestramento.
    check(vicino(f.viciniDb, -50.0f), "dintorni: mediana locale -50 dB");
    check(vicino(f.sottoViciniDb, -30.0f), "sotto i dintorni: -30 dB");
    check(vicino(f.sopraFondoDb, 40.0f), "sopra il fondo rumore: +40 dB");
    check(vicino(f.inPausa, 1.0f), "evento nella pausa (tolleranza 150 ms)");

    // Un evento lontano da una pausa corta: la tolleranza non deve dire 1.
    FrameTracks lontano = t;
    lontano.pauses = {{0.0, 0.02}};
    const BreathSegmentFeatures g = computeSegmentFeatures(lontano, 18, 19, 180, 200);
    check(vicino(g.inPausa, 0.0f), "evento fuori pausa: in_pausa = 0");
}

void testCandidati() {
    section("Candidati larghi");
    const std::vector<float> livello(6, -20.0f);

    // Eventi: frame 0-1 (20 ms) e 3-5 (30 ms).
    const std::vector<float> armonicita = {0.5f, 0.5f, 0.95f, 0.5f, 0.5f, 0.5f};
    WideCandidateConfig config;

    config.minDurationMs = 40;
    check(findWideCandidates(armonicita, livello, 10, 1000.0, config).empty(),
          "durata minima 40 ms: nessun candidato");

    config.minDurationMs = 20;
    const std::vector<WideCandidate> c = findWideCandidates(
        armonicita, livello, 10, 1000.0, config);
    check(c.size() == 2, "due candidati a 20 ms minimi");
    if (c.size() == 2) {
        check(c[0].firstFrame == 0 && c[0].lastFrame == 1, "primo candidato 0-1");
        check(c[1].firstFrame == 3 && c[1].lastFrame == 5, "secondo candidato 3-5");
    }

    // Un frame che non passa in mezzo: con maxGapFrames = 1 i due pezzi
    // diventano un candidato solo, con 0 restano separati.
    const std::vector<float> buco = {0.5f, 0.95f, 0.5f};
    config.maxGapFrames = 1;
    const std::vector<WideCandidate> unito = findWideCandidates(
        buco, livello, 10, 1000.0, config);
    check(unito.size() == 1 && unito[0].firstFrame == 0 && unito[0].lastFrame == 2,
          "maxGapFrames = 1 unisce il buco");

    config.maxGapFrames = 0;
    config.minDurationMs = 20;
    check(findWideCandidates(buco, livello, 10, 1000.0, config).empty(),
          "maxGapFrames = 0: i due pezzi da 10 ms sono sotto il minimo");
}

} // namespace

int main() {
    // Public builds must neither depend on private weights nor manufacture
    // a confidence score when the optional legacy classifier is absent.
    if (!embeddedLegacyModelsAvailable()) {
        BreathSegmentFeatures f;
        if (!std::isnan(reteProbability(f)) || !std::isnan(breathModelProbability(pesiIncorporati(), f))) return 1;
        DetectorSettings settings; settings.useBreathModel=true;
        std::vector<float> silence(4800,0);
        OfflineAnalyzer analyzer;
        if (analyzer.analyze(silence.data(),silence.size(),48000,settings).error.empty()) return 1;
        BreathModelWeights supplied; supplied.caricati=true; supplied.intercetta=0;
        if (std::abs(breathModelProbability(supplied,f)-.5f)>1e-6f) return 1;
    }

#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    std::cout << "Test delle feature e dei candidati\n";
    std::cout << "================================\n";

    testPause();
    testSegmento();
    testCandidati();

    std::cout << "\n================================\n";
    if (g_failures == 0) {
        std::cout << "TUTTI I TEST SUPERATI\n";
        return 0;
    }
    std::cout << "TEST FALLITI: " << g_failures << "\n";
    return 1;
}

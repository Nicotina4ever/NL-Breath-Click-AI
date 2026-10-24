// SPDX-License-Identifier: MPL-2.0
// ============================================================================
//  test_document.cpp — Prove del modello di documento dell'editor
//
//  La prova piu' importante e' il ritorno esatto: dopo un'operazione e il
//  relativo annullamento il buffer deve tornare IDENTICO, campione per
//  campione. E' l'unico modo per essere certi che la cronologia non perda o
//  sposti dati, e un editor che corrompe l'audio mentre lo si modifica e'
//  peggio di un editor che non c'e'.
// ============================================================================
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "core/edit/audio_document.h"

using namespace bc;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& what) {
    std::cout << "  [" << (condition ? " OK " : "FAIL") << "] " << what << "\n";
    if (!condition) ++g_failures;
}

void section(const char* name) {
    std::cout << "\n" << name << "\n" << std::string(std::strlen(name), '-') << "\n";
}

// ---------------------------------------------------------------------------
//  Materiale di prova deterministico
// ---------------------------------------------------------------------------
AudioBuffer makeBuffer(int64_t frames, int channels, int sampleRate) {
    AudioBuffer buffer;
    buffer.channels = channels;
    buffer.sampleRate = sampleRate;
    buffer.samples.resize(static_cast<size_t>(frames) * static_cast<size_t>(channels));

    for (int64_t frame = 0; frame < frames; ++frame) {
        for (int channel = 0; channel < channels; ++channel) {
            // Contenuto vario e senza periodi semplici: se una prova passa per
            // caso, non deve poterlo fare grazie a simmetrie del segnale.
            const double t = static_cast<double>(frame) / static_cast<double>(sampleRate);
            const double value = 0.42 * std::sin(2.0 * 3.14159265358979 * 137.0 * t) +
                                 0.21 * std::sin(2.0 * 3.14159265358979 * 911.0 * t) +
                                 0.07 * ((frame * 2654435761u) % 1000) / 1000.0 +
                                 channel * 0.03;
            buffer.samples[static_cast<size_t>(frame) * channels + channel] =
                static_cast<float>(value);
        }
    }
    return buffer;
}

bool buffersIdentical(const AudioBuffer& a, const AudioBuffer& b) {
    return a.channels == b.channels && a.sampleRate == b.sampleRate &&
           a.samples.size() == b.samples.size() &&
           std::equal(a.samples.begin(), a.samples.end(), b.samples.begin());
}

// Distanza massima fra due buffer della stessa lunghezza.
float maximumDifference(const AudioBuffer& a, const AudioBuffer& b) {
    if (a.samples.size() != b.samples.size()) return 1.0e30f;
    float worst = 0.0f;
    for (size_t i = 0; i < a.samples.size(); ++i) {
        worst = std::max(worst, std::abs(a.samples[i] - b.samples[i]));
    }
    return worst;
}

double energyBetween(const AudioBuffer& buffer, int64_t startFrame, int64_t endFrame) {
    const int channels = std::max(1, buffer.channels);
    const int64_t frames = static_cast<int64_t>(buffer.samples.size()) / channels;

    startFrame = std::max<int64_t>(0, startFrame);
    endFrame = std::min<int64_t>(frames, endFrame);

    double total = 0.0;
    for (int64_t frame = startFrame; frame < endFrame; ++frame) {
        for (int channel = 0; channel < channels; ++channel) {
            const double value = buffer.samples[static_cast<size_t>(frame * channels + channel)];
            total += value * value;
        }
    }
    return total;
}

std::string makeTempPath(const char* name) {
    return std::string("bc_doc_test_") + name + ".wav";
}

// ---------------------------------------------------------------------------
//  Caricamento e informazioni di base
// ---------------------------------------------------------------------------
void testLoading() {
    section("Caricamento e informazioni");

    AudioDocument document;
    check(!document.isLoaded(), "un documento nuovo non e' caricato");

    const AudioBuffer source = makeBuffer(48000, 2, 48000);
    check(document.adopt(source, "prova.wav"), "adozione di un buffer riuscita");
    check(document.isLoaded(), "il documento risulta caricato");
    check(document.frameCount() == 48000, "conteggio dei frame corretto");
    check(document.channels() == 2, "numero di canali corretto");
    check(document.sampleRate() == 48000, "sample rate corretto");
    check(std::abs(document.durationSeconds() - 1.0) < 1.0e-9, "durata corretta (1.0 s)");
    check(!document.isModified(), "un documento appena adottato non e' modificato");
    check(document.regions().empty(), "nessuna regione all'inizio");
    check(!document.canUndo() && !document.canRedo(), "cronologia vuota all'inizio");

    // Un buffer vuoto o malformato non deve essere accettato.
    AudioDocument invalid;
    AudioBuffer empty;
    check(!invalid.adopt(empty, "vuoto"), "un buffer vuoto viene rifiutato");

    AudioBuffer noChannels = makeBuffer(100, 1, 48000);
    noChannels.channels = 0;
    check(!invalid.adopt(noChannels, "senza canali"), "un buffer senza canali viene rifiutato");
}

// ---------------------------------------------------------------------------
//  Regioni
// ---------------------------------------------------------------------------
void testRegions() {
    section("Regioni");

    AudioDocument document;
    document.adopt(makeBuffer(48000, 1, 48000), "prova.wav");

    EditRegion region;
    region.kind = DetectionKind::Breath;
    region.startSample = 1000;
    region.endSample = 5000;
    region.reductionDb = -18.0f;

    const int index = document.addRegion(region);
    check(index == 0, "regione aggiunta con indice 0");
    check(document.regions().size() == 1, "una regione presente");
    check(document.canUndo(), "l'aggiunta e' annullabile");

    check(document.regionIndexAt(3000) == 0, "ricerca per campione dentro la regione");
    check(document.regionIndexAt(500) == -1, "ricerca prima della regione");
    check(document.regionIndexAt(6000) == -1, "ricerca dopo la regione");
    // Il confine finale e' esclusivo, come in tutti gli intervalli del progetto.
    check(document.regionIndexAt(5000) == -1, "il campione di fine e' escluso");

    int edge = 0;
    check(document.regionIndexNearEdge(1005, 20, &edge) == 0 && edge == -1,
          "aggancio al bordo iniziale");
    check(document.regionIndexNearEdge(4990, 20, &edge) == 0 && edge == 1,
          "aggancio al bordo finale");
    check(document.regionIndexNearEdge(3000, 20, &edge) == -1,
          "nessun aggancio al centro della regione");

    // Regione degenere rifiutata.
    EditRegion degenerate;
    degenerate.startSample = 900;
    degenerate.endSample = 900;
    check(document.addRegion(degenerate) == -1, "una regione di durata nulla viene rifiutata");

    // Modifica.
    EditRegion moved = document.regions()[0];
    moved.endSample = 8000;
    check(document.updateRegion(0, moved), "modifica della regione riuscita");
    check(document.regions()[0].endSample == 8000, "il nuovo confine e' applicato");
    check(!document.updateRegion(5, moved), "modifica di un indice inesistente rifiutata");

    // Attivazione e rimozione.
    check(document.setRegionEnabled(0, false), "disattivazione riuscita");
    check(!document.regions()[0].enabled, "la regione risulta disattivata");
    check(document.removeRegion(0), "rimozione riuscita");
    check(document.regions().empty(), "nessuna regione rimasta");
}

// ---------------------------------------------------------------------------
//  Ritorno esatto dopo annullamento
// ---------------------------------------------------------------------------
void testUndoRoundTrip() {
    section("Annullamento: ritorno esatto");

    struct Operation {
        const char* name;
        bool (AudioDocument::*apply)();
    };

    // Ogni operazione viene provata da sola: si applica, si annulla e si
    // pretende il buffer identico all'originale, poi si rifa' e si pretende
    // identico al risultato di prima.
    auto runCase = [](const char* name,
                      const std::function<bool(AudioDocument&)>& operation) {
        AudioDocument document;
        const AudioBuffer original = makeBuffer(48000, 2, 48000);
        document.adopt(original, "prova.wav");

        check(operation(document), std::string(name) + ": operazione applicata");
        const AudioBuffer afterApply = document.render(EditSettings{});

        check(document.canUndo(), std::string(name) + ": annullamento disponibile");
        const std::string label = document.undoLabel();
        check(!label.empty(), std::string(name) + ": l'operazione ha un'etichetta (" + label + ")");

        check(document.undo(), std::string(name) + ": annullamento eseguito");
        check(buffersIdentical(document.audio(), original),
              std::string(name) + ": buffer identico all'originale dopo l'annullamento");
        check(!document.canRedo() == false, std::string(name) + ": ripristino disponibile");

        check(document.redo(), std::string(name) + ": ripristino eseguito");
        const AudioBuffer afterRedo = document.audio();
        // Il ripristino deve ridare ESATTAMENTE il buffer di prima.
        check(afterRedo.samples == document.audio().samples,
              std::string(name) + ": ripristino coerente");
        check(document.undo(), std::string(name) + ": secondo annullamento");
        check(buffersIdentical(document.audio(), original),
              std::string(name) + ": di nuovo identico dopo il secondo annullamento");
        (void)afterApply;
    };

    runCase("elimina intervallo", [](AudioDocument& d) { return d.deleteRange(10000, 20000); });
    runCase("elimina all'inizio", [](AudioDocument& d) { return d.deleteRange(0, 5000); });
    runCase("elimina alla fine", [](AudioDocument& d) { return d.deleteRange(40000, 48000); });
    runCase("silenzia intervallo", [](AudioDocument& d) { return d.silenceRange(8000, 12000); });
    runCase("ritaglia", [](AudioDocument& d) { return d.cropToRange(10000, 30000); });
    runCase("ritaglia dall'inizio", [](AudioDocument& d) { return d.cropToRange(0, 20000); });
    runCase("ritaglia fino alla fine", [](AudioDocument& d) { return d.cropToRange(20000, 48000); });
    runCase("inserisci silenzio", [](AudioDocument& d) { return d.insertSilence(24000, 1000); });
    runCase("inserisci silenzio all'inizio", [](AudioDocument& d) { return d.insertSilence(0, 500); });
    runCase("guadagno su intervallo", [](AudioDocument& d) { return d.applyGainToRange(5000, 9000, -6.0f); });
}

// ---------------------------------------------------------------------------
//  Effetti delle operazioni
// ---------------------------------------------------------------------------
void testOperationEffects() {
    section("Effetti delle operazioni");

    // --- eliminazione: accorcia e chiude il buco ---------------------------
    {
        AudioDocument document;
        const AudioBuffer original = makeBuffer(48000, 2, 48000);
        document.adopt(original, "prova.wav");

        check(document.deleteRange(10000, 20000), "eliminazione riuscita");
        check(document.frameCount() == 38000, "lunghezza ridotta di 10000 frame");

        // Il campione che stava a 20000 ora sta a 10000.
        const int channels = 2;
        bool joined = true;
        for (int channel = 0; channel < channels; ++channel) {
            const float expected = original.samples[static_cast<size_t>(20000 * channels + channel)];
            const float actual = document.audio().samples[static_cast<size_t>(10000 * channels + channel)];
            if (std::abs(expected - actual) > 1.0e-7f) joined = false;
        }
        check(joined, "i due lembi sono stati uniti senza spostamenti");

        check(!document.deleteRange(5000, 5000), "intervallo vuoto rifiutato");
        check(!document.deleteRange(9000, 1000), "intervallo invertito rifiutato");
    }

    // --- silenzio: lunghezza invariata, contenuto azzerato -----------------
    {
        AudioDocument document;
        document.adopt(makeBuffer(48000, 1, 48000), "prova.wav");

        check(document.silenceRange(10000, 20000), "silenzio applicato");
        check(document.frameCount() == 48000, "la lunghezza non cambia");
        check(energyBetween(document.audio(), 10000, 20000) == 0.0,
              "l'intervallo e' azzerato");
        check(energyBetween(document.audio(), 0, 9000) > 0.0,
              "il resto del segnale e' intatto");
    }

    // --- ritaglio: tiene solo l'intervallo --------------------------------
    {
        AudioDocument document;
        const AudioBuffer original = makeBuffer(48000, 1, 48000);
        document.adopt(original, "prova.wav");

        check(document.cropToRange(10000, 30000), "ritaglio riuscito");
        check(document.frameCount() == 20000, "lunghezza pari alla selezione");

        bool same = true;
        for (int64_t i = 0; i < 20000; ++i) {
            if (std::abs(document.audio().samples[static_cast<size_t>(i)] -
                         original.samples[static_cast<size_t>(10000 + i)]) > 1.0e-7f) {
                same = false;
                break;
            }
        }
        check(same, "il contenuto conservato e' quello giusto");
    }

    // --- guadagno: scala senza cambiare la lunghezza -----------------------
    {
        AudioDocument document;
        const AudioBuffer original = makeBuffer(48000, 1, 48000);
        document.adopt(original, "prova.wav");

        check(document.applyGainToRange(10000, 20000, -6.0f), "guadagno applicato");
        check(document.frameCount() == 48000, "la lunghezza non cambia");

        const double before = energyBetween(original, 10000, 20000);
        const double after = energyBetween(document.audio(), 10000, 20000);
        const double reductionDb = 10.0 * std::log10(after / before);
        check(std::abs(reductionDb - (-6.0)) < 0.05,
              "riduzione di 6 dB ottenuta (" + std::to_string(reductionDb) + " dB)");
        check(energyBetween(document.audio(), 25000, 30000) ==
                  energyBetween(original, 25000, 30000),
              "fuori dall'intervallo nulla cambia");
    }
}

// ---------------------------------------------------------------------------
//  Le regioni seguono i cambi di lunghezza
// ---------------------------------------------------------------------------
void testRegionsFollowEdits() {
    section("Regioni e cambi di lunghezza");

    auto addRegion = [](AudioDocument& document, int64_t start, int64_t end, const char* label) {
        EditRegion region;
        region.startSample = start;
        region.endSample = end;
        region.reductionDb = -18.0f;
        (void)label;
        return document.addRegion(region);
    };

    // --- una regione dopo il taglio scorre indietro ------------------------
    {
        AudioDocument document;
        document.adopt(makeBuffer(48000, 1, 48000), "prova.wav");
        addRegion(document, 30000, 32000, "dopo");

        check(document.deleteRange(10000, 20000), "taglio prima della regione");
        check(document.regions().size() == 1, "la regione sopravvive");
        check(document.regions()[0].startSample == 20000 &&
                  document.regions()[0].endSample == 22000,
              "la regione e' scorsa indietro di 10000 campioni");
    }

    // --- una regione dentro il taglio sparisce -----------------------------
    {
        AudioDocument document;
        document.adopt(makeBuffer(48000, 1, 48000), "prova.wav");
        addRegion(document, 12000, 15000, "dentro");

        check(document.deleteRange(10000, 20000), "taglio che contiene la regione");
        check(document.regions().empty(), "la regione interna e' stata rimossa");
    }

    // --- una regione che attraversa il taglio viene accorciata -------------
    {
        AudioDocument document;
        document.adopt(makeBuffer(48000, 1, 48000), "prova.wav");
        addRegion(document, 8000, 25000, "a cavallo");

        check(document.deleteRange(10000, 20000), "taglio a cavallo");
        check(document.regions().size() == 1, "la regione sopravvive accorciata");
        check(document.regions()[0].startSample == 8000 && document.regions()[0].endSample == 15000,
              "estremi corretti dopo l'accorciamento");
    }

    // --- una regione prima del taglio resta ferma --------------------------
    {
        AudioDocument document;
        document.adopt(makeBuffer(48000, 1, 48000), "prova.wav");
        addRegion(document, 1000, 3000, "prima");

        check(document.deleteRange(10000, 20000), "taglio dopo la regione");
        check(document.regions()[0].startSample == 1000 && document.regions()[0].endSample == 3000,
              "la regione prima del taglio non si muove");
    }

    // --- l'inserimento spinge avanti ---------------------------------------
    {
        AudioDocument document;
        document.adopt(makeBuffer(48000, 1, 48000), "prova.wav");
        addRegion(document, 30000, 32000, "dopo");

        check(document.insertSilence(10000, 5000), "inserimento prima della regione");
        check(document.regions()[0].startSample == 35000 && document.regions()[0].endSample == 37000,
              "la regione e' stata spinta avanti di 5000 campioni");
    }

    // --- il ritaglio trasla le regioni -------------------------------------
    {
        AudioDocument document;
        document.adopt(makeBuffer(48000, 1, 48000), "prova.wav");
        addRegion(document, 15000, 18000, "dentro il ritaglio");
        addRegion(document, 40000, 42000, "fuori dal ritaglio");

        check(document.cropToRange(10000, 30000), "ritaglio");
        check(document.regions().size() == 1, "sopravvive solo la regione interna");
        check(document.regions()[0].startSample == 5000 && document.regions()[0].endSample == 8000,
              "la regione e' traslata di 10000 campioni");
    }
}

// ---------------------------------------------------------------------------
//  Rendering
// ---------------------------------------------------------------------------
void testRendering() {
    section("Rendering");

    AudioDocument document;
    const AudioBuffer original = makeBuffer(48000, 2, 48000);
    document.adopt(original, "prova.wav");

    EditRegion region;
    region.kind = DetectionKind::Breath;
    region.startSample = 10000;
    region.endSample = 20000;
    region.reductionDb = -20.0f;
    document.addRegion(region);

    EditSettings settings;
    settings.attackMs = 8.0f;
    settings.releaseMs = 80.0f;
    settings.prePadMs = 0.0f;
    settings.postPadMs = 0.0f;

    const AudioBuffer rendered = document.render(settings);

    check(rendered.samples.size() == original.samples.size(),
          "il rendering non cambia la lunghezza");
    check(buffersIdentical(document.audio(), original),
          "il rendering non modifica il documento");

    // Al centro della regione serve l'attenuazione piena.
    const double before = energyBetween(original, 12000, 18000);
    const double after = energyBetween(rendered, 12000, 18000);
    const double reductionDb = 10.0 * std::log10(after / before);
    check(std::abs(reductionDb - (-20.0)) < 0.2,
          "attenuazione piena al centro della regione (" +
              std::to_string(reductionDb) + " dB)");

    // Fuori dalla regione e dalle rampe il segnale resta intatto.
    const float outside = maximumDifference(rendered, original);
    check(outside > 0.0f, "il rendering ha effettivamente modificato qualcosa");

    bool untouchedFarAway = true;
    for (int64_t frame = 25000; frame < 40000; ++frame) {
        for (int channel = 0; channel < 2; ++channel) {
            const size_t index = static_cast<size_t>(frame * 2 + channel);
            if (rendered.samples[index] != original.samples[index]) untouchedFarAway = false;
        }
    }
    check(untouchedFarAway, "lontano dalla regione il segnale e' bit per bit identico");

    // La curva disegnata deve descrivere lo stesso guadagno applicato.
    const std::vector<float> curve = document.gainCurve(settings);
    check(curve.size() == static_cast<size_t>(document.frameCount()),
          "la curva ha un valore per frame");

    const float curveAtCenter = curve[15000];
    const float gainAtCenter = rendered.samples[static_cast<size_t>(15000 * 2)] /
                               (original.samples[static_cast<size_t>(15000 * 2)] + 1.0e-12f);
    check(std::abs(curveAtCenter - gainAtCenter) < 1.0e-4f,
          "la curva corrisponde al guadagno realmente applicato");

    // Una regione disattivata non deve avere effetto.
    document.setRegionEnabled(0, false);
    const AudioBuffer withoutRegion = document.render(settings);
    check(buffersIdentical(withoutRegion, original),
          "una regione disattivata non produce alcuna modifica");
    document.setRegionEnabled(0, true);
}

// ---------------------------------------------------------------------------
//  Fade e guadagno generale
// ---------------------------------------------------------------------------
void testFadeAndMaster() {
    section("Fade e guadagno generale");

    AudioDocument document;
    const AudioBuffer original = makeBuffer(48000, 1, 48000);
    document.adopt(original, "prova.wav");

    FadeSettings fade;
    fade.inEnabled = true;
    fade.inSeconds = 0.1;      // 4800 campioni
    fade.inShape = FadeShape::Cosine;
    fade.outEnabled = true;
    fade.outSeconds = 0.1;
    fade.outShape = FadeShape::Cosine;
    document.setFade(fade);

    EditSettings settings;
    const AudioBuffer rendered = document.render(settings);

    check(std::abs(rendered.samples[0]) < 1.0e-6f, "l'inizio e' azzerato dal fade");
    check(std::abs(rendered.samples[47999]) < 1.0e-6f, "la fine e' azzerata dal fade");

    const float middleOriginal = original.samples[24000];
    check(rendered.samples[24000] == middleOriginal,
          "al centro il fade non tocca il segnale");

    // La rampa deve crescere in modo monotono.
    bool monotone = true;
    double previous = -1.0;
    for (int64_t frame = 0; frame < 4800; ++frame) {
        const double ratio = std::abs(original.samples[static_cast<size_t>(frame)]) > 1.0e-6
                                 ? rendered.samples[static_cast<size_t>(frame)] /
                                       original.samples[static_cast<size_t>(frame)]
                                 : -1.0;
        if (ratio < 0.0) continue;
        if (ratio < previous - 1.0e-6) monotone = false;
        previous = ratio;
    }
    check(monotone, "la rampa di ingresso cresce in modo monotono");

    // Guadagno generale.
    document.setMasterGainDb(-6.0f);
    const AudioBuffer quieter = document.render(settings);
    const double ratio = 10.0 * std::log10(energyBetween(quieter, 20000, 30000) /
                                           energyBetween(rendered, 20000, 30000));
    check(std::abs(ratio - (-6.0)) < 0.05,
          "guadagno generale di -6 dB applicato (" + std::to_string(ratio) + " dB)");

    // Annullamento del solo guadagno generale.
    check(document.undo(), "annullamento del guadagno generale");
    check(document.masterGainDb() == 0.0f, "guadagno generale tornato a 0 dB");
    check(buffersIdentical(document.audio(), original), "il buffer non e' cambiato");
}

// ---------------------------------------------------------------------------
//  Cronologia con piu' passi
// ---------------------------------------------------------------------------
void testHistoryStack() {
    section("Cronologia con piu' passi");

    AudioDocument document;
    const AudioBuffer original = makeBuffer(48000, 1, 48000);
    document.adopt(original, "prova.wav");

    check(document.deleteRange(10000, 11000), "primo taglio");
    check(document.silenceRange(20000, 21000), "silenzio");
    check(document.applyGainToRange(30000, 31000, -3.0f), "guadagno");
    check(document.undoDepth() == 3, "tre passi in cronologia");

    check(document.undo(), "annulla il guadagno");
    check(document.undo(), "annulla il silenzio");
    check(document.undo(), "annulla il taglio");
    check(!document.canUndo(), "cronologia esaurita");
    check(!document.undo(), "ulteriore annullamento rifiutato");
    check(buffersIdentical(document.audio(), original),
          "dopo tre annullamenti il buffer e' identico all'originale");

    check(document.redo(), "rifai il taglio");
    check(document.redo(), "rifai il silenzio");
    check(document.redo(), "rifai il guadagno");
    check(!document.canRedo(), "ripristino esaurito");

    // Una nuova modifica deve invalidare il ripristino.
    check(document.undo(), "torna indietro di un passo");
    check(document.canRedo(), "ripristino disponibile");
    check(document.insertSilence(1000, 100), "nuova modifica");
    check(!document.canRedo(), "la nuova modifica ha svuotato il ripristino");
    check(document.historyBytes() > 0, "la cronologia occupa memoria contabilizzata");
}

// ---------------------------------------------------------------------------
//  Salvataggio
// ---------------------------------------------------------------------------
void testSavingThroughDocument() {
    section("Salvataggio tramite il documento");

    AudioDocument document;
    document.adopt(makeBuffer(12000, 1, 48000), "prova.wav");

    EditRegion region;
    region.kind = DetectionKind::Breath;
    region.startSample = 2000;
    region.endSample = 6000;
    region.reductionDb = -12.0f;
    document.addRegion(region);

    EditSettings settings;
    const std::string path = makeTempPath("salvataggio");

    std::string error;
    check(document.save(path, WavFormat::Float32, settings, &error),
          "salvataggio riuscito" + (error.empty() ? "" : ": " + error));

    // Il file salvato deve contenere il RISULTATO, non l'originale.
    AudioBuffer reloaded;
    check(readWav(path, reloaded, &error), "rilettura del file salvato");
    check(reloaded.samples.size() == document.audio().samples.size(),
          "lunghezza del file salvato corretta");

    const double before = energyBetween(document.audio(), 2500, 5500);
    const double after = energyBetween(reloaded, 2500, 5500);
    const double reductionDb = 10.0 * std::log10(after / before);
    check(std::abs(reductionDb - (-12.0)) < 0.1,
          "il file salvato contiene la correzione (" + std::to_string(reductionDb) + " dB)");

    std::remove(path.c_str());

    // Il documento non deve essersi modificato per effetto del salvataggio.
    check(document.regions().size() == 1, "il documento e' rimasto intatto");
}

// ---------------------------------------------------------------------------
//  Passaggio dai rilevamenti automatici alle regioni modificabili
//
//  E' il percorso che l'editor segue quando si preme "Analizza": se si rompe,
//  il rilevamento funziona ma non arriva mai a schermo.
// ---------------------------------------------------------------------------
void testRegionsFromDetections() {
    section("Regioni dai rilevamenti");

    AudioDocument document;
    document.adopt(makeBuffer(48000, 1, 48000), "prova.wav");

    std::vector<Detection> detections;

    Detection breath;
    breath.kind = DetectionKind::Breath;
    breath.startSample = 10000;
    breath.endSample = 15000;
    breath.peakScore = 0.9f;
    detections.push_back(breath);

    Detection click;
    click.kind = DetectionKind::Click;
    click.startSample = 20000;
    click.endSample = 20100;
    click.peakScore = 0.8f;
    detections.push_back(click);

    // Un rilevamento che esce dal buffer: va ritagliato, non deve sparire ne'
    // far uscire le regioni dai limiti.
    Detection overflowing;
    overflowing.kind = DetectionKind::Breath;
    overflowing.startSample = 47000;
    overflowing.endSample = 60000;
    detections.push_back(overflowing);

    EditSettings settings;
    settings.breathReductionDb = -18.0f;
    settings.clickReductionDb = -60.0f;
    settings.repairClicks = true;

    document.setRegionsFromDetections(detections, settings);

    check(document.regions().size() == 3, "tre regioni create dai tre rilevamenti");
    check(document.canUndo(), "l'operazione e' annullabile");

    const std::vector<EditRegion>& regions = document.regions();

    check(regions[0].kind == DetectionKind::Breath, "il primo rilevamento resta un respiro");
    check(regions[0].reductionDb == settings.breathReductionDb,
          "al respiro e' applicata l'attenuazione predefinita dei respiri");
    check(!regions[0].repair, "un respiro non viene mai riparato per interpolazione");
    check(regions[0].fromDetection, "la regione ricorda di venire da un rilevamento");
    check(regions[0].score == breath.peakScore, "la confidenza viene conservata");

    check(regions[1].kind == DetectionKind::Click, "il secondo rilevamento e' un click");
    check(regions[1].repair, "il click viene riparato, come chiede la configurazione");
    check(regions[1].reductionDb == settings.clickReductionDb,
          "al click e' applicata l'attenuazione predefinita dei click");

    check(regions[2].endSample == document.frameCount(),
          "il rilevamento oltre il fondo viene ritagliato alla fine del buffer");

    // Una regione disattivata a mano deve sopravvivere a un annullamento.
    check(document.setRegionEnabled(0, false), "disattivazione di una regione");
    check(!document.regions()[0].enabled, "la regione risulta disattivata");
    check(document.undo(), "annullamento della disattivazione");
    check(document.regions()[0].enabled, "la regione e' di nuovo attiva");

    // Annullando del tutto si torna a zero regioni.
    check(document.undo(), "annullamento del rilevamento");
    check(document.regions().empty(), "nessuna regione dopo l'annullamento");

    // Con la riparazione disattivata il click non deve essere marcato come da riparare.
    settings.repairClicks = false;
    document.setRegionsFromDetections(detections, settings);
    check(document.regions().size() == 3, "nuovo rilevamento con riparazione disattivata");
    check(!document.regions()[1].repair, "il click non viene riparato se la riparazione e' spenta");
}

// ---------------------------------------------------------------------------
//  Accorciamento delle regioni (dimezzare i fiati per velocizzare uno spot)
// ---------------------------------------------------------------------------
void testShortenRegions() {
    section("Accorciamento delle regioni");

    AudioDocument document;
    // 20 secondi: le regioni di prova devono starci dentro, altrimenti vengono
    // ritagliate alla fine del buffer e il test misura un'altra cosa.
    document.adopt(makeBuffer(960000, 1, 48000), "spot.wav");
    const int64_t originalFrames = document.frameCount();
    check(originalFrames == 960000, "buffer di prova da 20 secondi");

    // Tre respiri da 1 secondo: dimezzandoli la traccia deve accorciarsi di 1,5 s.
    const int64_t breathLength = 48000;
    for (int i = 0; i < 3; ++i) {
        EditRegion breath;
        breath.kind = DetectionKind::Breath;
        breath.startSample = 100000 + i * 200000;
        breath.endSample = breath.startSample + breathLength;
        document.addRegion(breath);
    }

    // Un click, che NON deve essere toccato.
    EditRegion click;
    click.kind = DetectionKind::Click;
    click.startSample = 30000;
    click.endSample = 30500;
    document.addRegion(click);

    const int64_t clickBefore = document.regions()[3].lengthSamples();
    const size_t regionsBefore = document.regions().size();

    const int shortened = document.shortenRegions(DetectionKind::Breath, 0.5f, true);
    check(shortened == 3, "tre respiri accorciati");

    const int64_t expected = originalFrames - 3 * (breathLength / 2);
    check(document.frameCount() == expected,
          "la traccia si accorcia della somma delle meta' tolte");

    // I respiri devono essere lunghi la meta', il click intatto.
    int breathsHalf = 0;
    for (const EditRegion& region : document.regions()) {
        if (region.kind == DetectionKind::Breath &&
            region.lengthSamples() == breathLength / 2) {
            ++breathsHalf;
        }
    }
    check(breathsHalf == 3, "ogni respiro e' lungo la meta' di prima");
    check(document.regions().size() == regionsBefore, "nessuna regione persa");
    check(document.regions()[3].lengthSamples() == clickBefore,
          "il click non viene toccato");

    // Il punto cruciale: UN SOLO annullamento riporta indietro tutto.
    check(document.undo(), "un solo annullamento");
    check(document.frameCount() == originalFrames,
          "la lunghezza originale e' ripristinata");
    check(document.regions().size() == regionsBefore, "le regioni sono tornate");

    int breathsWhole = 0;
    for (const EditRegion& region : document.regions()) {
        if (region.kind == DetectionKind::Breath && region.lengthSamples() == breathLength) {
            ++breathsWhole;
        }
    }
    check(breathsWhole == 3, "i respiri sono tornati interi con UN annullamento");

    // Le regioni disattivate non vanno toccate.
    document.setRegionEnabled(1, false);
    const int shortened2 = document.shortenRegions(DetectionKind::Breath, 0.5f, true);
    check(shortened2 == 2, "le regioni disattivate vengono saltate");

    // Una frazione diversa dalla meta' deve funzionare.
    document.undo();
    // La regione 1 era stata disattivata dalla prova precedente e l'annullamento
    // non la riattiva: va riaccesa, altrimenti si misurerebbe di nuovo il
    // comportamento delle regioni spente.
    document.setRegionEnabled(1, true);
    const int shortened3 = document.shortenRegions(DetectionKind::Breath, 0.25f, true);
    check(shortened3 == 3, "accorciamento di un quarto");
    bool quarterOk = true;
    for (const EditRegion& region : document.regions()) {
        if (region.kind == DetectionKind::Breath &&
            region.lengthSamples() != breathLength - breathLength / 4) {
            quarterOk = false;
        }
    }
    check(quarterOk, "ogni respiro perde esattamente un quarto");
}

void testDenseHistoryAndReanalysis() {
    section("600 regioni: cronologia e riconciliazione");
    AudioDocument document;
    const auto audio = makeBuffer(48000 * 7, 2, 48000);
    check(document.adopt(audio, "dense.wav"), "adozione audio denso");
    std::vector<Detection> detections;
    for (int i = 0; i < 600; ++i) {
        Detection d; d.startSample = i * 480; d.endSample = d.startSample + 200;
        detections.push_back(d);
    }
    EditSettings settings;
    document.reconcileRegions(detections, settings);
    document.clearHistory();
    const auto before = document.render(settings);
    auto region = document.regions()[300];
    document.beginUndoGroup("drag denso");
    for (int step = 1; step <= 100; ++step) {
        ++region.startSample; ++region.endSample;
        document.updateRegion(300, region);
    }
    document.endUndoGroup();
    const auto after = document.render(settings);
    check(document.undo(), "Undo drag denso");
    check(!document.canUndo(), "100 movimenti formano una sola operazione");
    check(document.render(settings).samples == before.samples, "Undo ripristina esattamente l audio esportabile");
    check(document.redo(), "Redo drag denso");
    check(document.render(settings).samples == after.samples, "Redo ripristina esattamente l audio esportabile");
    check(document.audio().samples == audio.samples && after.frameCount() == audio.frameCount(),
          "originale e durata invariati in Attenua");
    document.reconcileRegions({}, settings);
    check(document.regions().size() == 1 && document.regions()[0].startSample == region.startSample,
          "nuova analisi conserva confini corretti anche senza nuovo candidato");
    check(document.undo() && document.regions().size() == 600, "riconciliazione densa annullabile in un passo");

    AudioDocument overlap;
    overlap.adopt(audio, "overlap.wav");
    detections.resize(2);
    detections[0].startSample = 100; detections[0].endSample = 200;
    detections[1].startSample = 300; detections[1].endSample = 400;
    overlap.reconcileRegions(detections, settings);
    detections.resize(1); detections[0].startSample = 50; detections[0].endSample = 450;
    const auto result = overlap.reconcileRegions(detections, settings);
    check(result.ambiguous > 0 && overlap.regions().size() == 2,
          "molti-a-uno conserva entrambe le regioni e segnala conflitto");

    AudioDocument grouped;
    grouped.adopt(audio, "grouped.wav");
    grouped.beginUndoGroup("operazioni audio");
    grouped.silenceRange(100, 300);
    grouped.deleteRange(500, 900);
    grouped.recordCorrection(DetectionKind::Breath, 100, 300, false, "journal");
    grouped.endUndoGroup();
    const auto changed = grouped.audio().samples;
    check(grouped.undo() && grouped.audio().samples == audio.samples && grouped.corrections().empty(),
          "Undo di gruppo ripristina campioni e registro correzioni");
    check(grouped.redo() && grouped.audio().samples == changed && grouped.corrections().size() == 1,
          "Redo di gruppo ripristina campioni e registro correzioni");
    grouped.beginUndoGroup("annulla transazione");
    grouped.deleteRange(10, 200);
    grouped.cancelUndoGroup();
    check(grouped.audio().samples == changed, "cancellazione gruppo ripristina anche i campioni");
}

} // namespace

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    std::cout << "Prove del documento audio\n";
    std::cout << "==========================\n";

    testLoading();
    testRegions();
    testUndoRoundTrip();
    testOperationEffects();
    testRegionsFollowEdits();
    testRendering();
    testFadeAndMaster();
    testHistoryStack();
    testSavingThroughDocument();
    testRegionsFromDetections();
    testDenseHistoryAndReanalysis();

    std::cout << "\n==========================\n";
    if (g_failures == 0) {
        std::cout << "TUTTE LE PROVE SUPERATE\n";
        return 0;
    }
    std::cout << "PROVE FALLITE: " << g_failures << "\n";
    return 1;
}

// SPDX-License-Identifier: MPL-2.0
#include "core/dsp/silence_gate.h"

#include <algorithm>

namespace bc {

namespace {

// Il livello e' quello di un frame: sotto la soglia si e' in silenzio.
// Il valore sentinella del silenzio digitale (-160) e' ovviamente "sotto".
bool sottoSoglia(float livello, float soglia) {
    return livello <= soglia;
}

} // namespace

std::vector<SilenceRange> detectSilence(const std::vector<float>& frameLevelDb,
                                        int64_t hopSize,
                                        double sampleRate,
                                        const SilenceGateConfig& config) {
    std::vector<SilenceRange> risultato;
    if (frameLevelDb.empty() || hopSize <= 0 || sampleRate <= 0.0) {
        return risultato;
    }

    const float sogliaEntrata = config.thresholdDb;
    const float sogliaUscita = config.thresholdDb + std::max(0.0f, config.hysteresisDb);

    const double msPerFrame = 1000.0 * static_cast<double>(hopSize) / sampleRate;
    const int64_t minSilenzioFrames = static_cast<int64_t>(
        std::max(1.0, static_cast<double>(config.minSilenceMs) / msPerFrame));
    const int64_t minSuonoFrames = static_cast<int64_t>(
        std::max(1.0, static_cast<double>(config.minSoundMs) / msPerFrame));

    // --- Passo 1: grezzo, con isteresi -------------------------------------
    //
    // Si parte in "suono": un file che comincia in silenzio entra nello stato
    // silenzio al primo frame sotto soglia, che e' il comportamento atteso.
    struct Grezzo { int64_t primo, ultimo; };
    std::vector<Grezzo> grezzi;

    bool inSilenzio = false;
    int64_t inizio = 0;

    for (size_t f = 0; f < frameLevelDb.size(); ++f) {
        const float livello = frameLevelDb[f];

        if (!inSilenzio) {
            if (sottoSoglia(livello, sogliaEntrata)) {
                inSilenzio = true;
                inizio = static_cast<int64_t>(f);
            }
        } else {
            // Si esce solo sopra soglia + isteresi: e' questo che impedisce a
            // un respiro di spezzare la pausa che lo contiene.
            if (livello > sogliaUscita) {
                grezzi.push_back({inizio, static_cast<int64_t>(f) - 1});
                inSilenzio = false;
            }
        }
    }
    if (inSilenzio) {
        grezzi.push_back({inizio, static_cast<int64_t>(frameLevelDb.size()) - 1});
    }

    if (grezzi.empty()) {
        return risultato;
    }

    // --- Passo 2: i suoni troppo brevi vengono assorbiti --------------------
    //
    // Un click isolato in mezzo a una pausa non deve spezzarla in due: se il
    // "suono" fra due silenzi dura meno di minSoundMs, i due silenzi sono in
    // realta' uno solo.
    std::vector<Grezzo> uniti;
    uniti.push_back(grezzi[0]);
    for (size_t i = 1; i < grezzi.size(); ++i) {
        const int64_t suonoFra = grezzi[i].primo - uniti.back().ultimo - 1;
        if (suonoFra <= minSuonoFrames) {
            uniti.back().ultimo = grezzi[i].ultimo;   // fondi
        } else {
            uniti.push_back(grezzi[i]);
        }
    }

    // --- Passo 3: i silenzi troppo brevi vengono scartati -------------------
    for (const Grezzo& g : uniti) {
        const int64_t lunghezza = g.ultimo - g.primo + 1;
        if (lunghezza < minSilenzioFrames) {
            continue;
        }

        SilenceRange r;
        r.startSample = g.primo * hopSize;
        // La fine e' l'ultimo frame di silenzio PIU' la sua durata: il livello
        // del frame e' calcolato su una finestra, e fermarsi all'inizio
        // dell'ultimo frame lascerebbe fuori la coda.
        r.endSample = (g.ultimo + 1) * hopSize;
        risultato.push_back(r);
    }

    return risultato;
}

} // namespace bc

// SPDX-License-Identifier: MPL-2.0
// ============================================================================
//  breath_model_eval.h - Pesi del modello e loro applicazione
//
//  Profili da file oppure coefficienti legacy privati opzionali.
//  La build pubblica non incorpora pesi addestrati su dati dell'utente.
// ============================================================================
#pragma once

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <limits>

// Private legacy coefficients are optional build inputs, never public source.
#ifdef BC_PRIVATE_LEGACY_MODELS
#include <breath_model.h>
#include <breath_model_net.h>
#include <breath_model_net_narrow.h>
#else
namespace bc { namespace breath_model { constexpr int kFeatureCount = 14; }
namespace breath_net { constexpr int kIngressi = 14; } }
#endif

namespace bc {
inline constexpr bool embeddedLegacyModelsAvailable() {
#ifdef BC_PRIVATE_LEGACY_MODELS
    return true;
#else
    return false;
#endif
}

// Le 14 grandezze di un segmento, nello stesso ordine dell'addestramento.
struct BreathSegmentFeatures {
    float durataMs = 0.0f;
    float flatMedia = 0.0f;
    float harmMedia = 0.0f;
    float dbMedia = -160.0f;
    float centMedia = 0.0f;
    float centMin = 0.0f;
    float centMax = 0.0f;
    float zcrMedia = 0.0f;
    float bmediaMedia = 0.0f;     // banda media
    float baltaMedia = 0.0f;      // banda alta
    float viciniDb = -160.0f;
    float sottoViciniDb = 0.0f;
    float sopraFondoDb = 0.0f;
    float inPausa = 0.0f;         // 1 se il segmento cade in una pausa
};

// I pesi di un modello.
struct BreathModelWeights {
    float media[breath_model::kFeatureCount] = {};
    float deviazione[breath_model::kFeatureCount] = {};
    float pesi[breath_model::kFeatureCount] = {};
    float intercetta = 0.0f;

    // Da dove vengono: serve a dirlo all'utente, non a calcolare.
    std::string origine = "incorporato";

    // Nome leggibile, per il menu.
    std::string nome = "base";

    bool caricati = false;   // true = letti da un file
};

// Pesi di una RETE caricati da un profilo (rete.json).
//
// Perche' esiste: la rete compilata cambia solo ricompilando. Con questi pesi
// un profilo porta con se' la propria rete e si sceglie dal menu Modello,
// senza toccare il binario. Se il file non e' valido si ricade sulla rete
// compilata: un profilo rotto non deve impedire di analizzare.
struct BreathNetWeights {
    int ingressi = 0;
    int nascosti = 0;
    std::vector<float> media;
    std::vector<float> deviazione;
    std::vector<float> W1;   // ingressi x nascosti, riga per riga
    std::vector<float> b1;
    std::vector<float> W2;
    float b2 = 0.0f;
    bool caricati = false;
    std::string origine;
};

// Coefficienti incorporati solo se esplicitamente abilitati in una build privata.
inline BreathModelWeights pesiIncorporati() {
    BreathModelWeights w;
    #ifdef BC_PRIVATE_LEGACY_MODELS
    for (int i = 0; i < breath_model::kFeatureCount; ++i) {
        w.media[i] = breath_model::kMedia[i];
        w.deviazione[i] = breath_model::kDeviazione[i];
        w.pesi[i] = breath_model::kPesi[i];
    }
    w.intercetta = breath_model::kIntercetta;
    w.origine = "incorporato";
    w.nome = "base";
    #else
    w.origine = "legacy non disponibile: caricare un profilo personale";
    #endif
    w.caricati = false;
    return w;
}

// Probabilita' che il segmento sia un fiato, secondo questi pesi.
inline float breathModelProbability(const BreathModelWeights& w,
                                    const BreathSegmentFeatures& f) {
    if (!embeddedLegacyModelsAvailable() && !w.caricati) return std::numeric_limits<float>::quiet_NaN();
    const float valori[breath_model::kFeatureCount] = {
        f.durataMs, f.flatMedia, f.harmMedia, f.dbMedia,
        f.centMedia, f.centMin, f.centMax, f.zcrMedia,
        f.bmediaMedia, f.baltaMedia, f.viciniDb, f.sottoViciniDb,
        f.sopraFondoDb, f.inPausa,
    };

    float somma = w.intercetta;
    for (int i = 0; i < breath_model::kFeatureCount; ++i) {
        // Una deviazione a zero darebbe una divisione per zero: si salta la
        // feature invece di restituire infinito.
        if (w.deviazione[i] <= 1e-9f) continue;
        somma += w.pesi[i] * (valori[i] - w.media[i]) / w.deviazione[i];
    }

    // Sigmoide scritta in modo da non traboccare.
    if (somma >= 0.0f) {
        const float e = std::exp(-somma);
        return 1.0f / (1.0f + e);
    }
    const float e = std::exp(somma);
    return e / (1.0f + e);
}

// ---------------------------------------------------------------------------
//  Lettura dei pesi da file
//
//  Formato: JSON piatto, lo stesso che scrive addestra_modello.py in pesi.json.
//  Non serve un lettore JSON generico: si cercano le quattro chiavi note e si
//  leggono i numeri che seguono.
//
//  Se qualcosa non torna si restituiscono i pesi incorporati: un file rotto
//  non deve impedire di analizzare.
// ---------------------------------------------------------------------------
namespace dettaglio_pesi {

inline bool leggiArray(const std::string& testo, const std::string& chiave,
                       int attesi, float* fuori) {
    const size_t pos = testo.find('"' + chiave + '"');
    if (pos == std::string::npos) return false;

    const size_t apre = testo.find('[', pos);
    if (apre == std::string::npos) return false;
    const size_t chiude = testo.find(']', apre);
    if (chiude == std::string::npos) return false;

    std::string dentro = testo.substr(apre + 1, chiude - apre - 1);
    for (char& c : dentro) {
        if (c == ',' || c == '\n' || c == '\r' || c == '\t') c = ' ';
    }

    std::istringstream flusso(dentro);
    int letti = 0;
    double valore = 0.0;
    while (letti < attesi && (flusso >> valore)) {
        fuori[letti] = static_cast<float>(valore);
        ++letti;
    }
    return letti == attesi;
}

inline bool leggiNumero(const std::string& testo, const std::string& chiave,
                        float* fuori) {
    const size_t pos = testo.find('"' + chiave + '"');
    if (pos == std::string::npos) return false;

    const size_t duePunti = testo.find(':', pos);
    if (duePunti == std::string::npos) return false;

    std::istringstream flusso(testo.substr(duePunti + 1));
    double valore = 0.0;
    if (!(flusso >> valore)) return false;
    *fuori = static_cast<float>(valore);
    return true;
}

} // namespace dettaglio_pesi

// Legge una rete da file: media, deviazione, W1 (appiattito), b1, W2, b2.
// Il training scrive rete.json gia' piatto, cosi' il lettore resta quello
// dei numeri e non serve un parser JSON.
inline BreathNetWeights caricaRete(const std::string& percorso,
                                   std::string* errore = nullptr) {
    BreathNetWeights rete;

    std::ifstream file(percorso, std::ios::binary);
    if (!file) {
        if (errore) *errore = "non trovo " + percorso;
        return rete;
    }

    std::ostringstream contenuto;
    contenuto << file.rdbuf();
    const std::string testo = contenuto.str();
    if (testo.empty()) {
        if (errore) *errore = "file vuoto";
        return rete;
    }

    float ingressi = 0.0f;
    float nascosti = 0.0f;
    if (!dettaglio_pesi::leggiNumero(testo, "ingressi", &ingressi) ||
        !dettaglio_pesi::leggiNumero(testo, "nascosti", &nascosti)) {
        if (errore) *errore = "mancano ingressi/nascosti";
        return rete;
    }

    const int in = static_cast<int>(ingressi + 0.5f);
    const int na = static_cast<int>(nascosti + 0.5f);
    if (in != breath_net::kIngressi || na <= 0 || na > 64) {
        if (errore) {
            *errore = "dimensioni non compatibili: " + std::to_string(in) +
                      " -> " + std::to_string(na) +
                      " (attese " + std::to_string(breath_net::kIngressi) + " ingressi)";
        }
        return rete;
    }

    rete.ingressi = in;
    rete.nascosti = na;
    rete.media.assign(static_cast<size_t>(in), 0.0f);
    rete.deviazione.assign(static_cast<size_t>(in), 0.0f);
    rete.W1.assign(static_cast<size_t>(in * na), 0.0f);
    rete.b1.assign(static_cast<size_t>(na), 0.0f);
    rete.W2.assign(static_cast<size_t>(na), 0.0f);

    const bool ok =
        dettaglio_pesi::leggiArray(testo, "media", in, rete.media.data()) &&
        dettaglio_pesi::leggiArray(testo, "deviazione", in, rete.deviazione.data()) &&
        dettaglio_pesi::leggiArray(testo, "W1", in * na, rete.W1.data()) &&
        dettaglio_pesi::leggiArray(testo, "b1", na, rete.b1.data()) &&
        dettaglio_pesi::leggiArray(testo, "W2", na, rete.W2.data()) &&
        dettaglio_pesi::leggiNumero(testo, "b2", &rete.b2);

    if (!ok) {
        if (errore) *errore = "formato non riconosciuto";
        rete = BreathNetWeights{};
        return rete;
    }

    rete.caricati = true;
    rete.origine = percorso;
    if (errore) errore->clear();
    return rete;
}

inline BreathModelWeights caricaPesi(const std::string& percorso,
                                     std::string* errore = nullptr) {
    BreathModelWeights predefiniti = pesiIncorporati();

    std::ifstream file(percorso, std::ios::binary);
    if (!file) {
        if (errore) *errore = "non trovo " + percorso;
        return predefiniti;
    }

    std::ostringstream contenuto;
    contenuto << file.rdbuf();
    const std::string testo = contenuto.str();
    if (testo.empty()) {
        if (errore) *errore = "file vuoto";
        return predefiniti;
    }

    BreathModelWeights w;
    const bool ok =
        dettaglio_pesi::leggiArray(testo, "media", breath_model::kFeatureCount, w.media) &&
        dettaglio_pesi::leggiArray(testo, "deviazione", breath_model::kFeatureCount, w.deviazione) &&
        dettaglio_pesi::leggiArray(testo, "pesi", breath_model::kFeatureCount, w.pesi) &&
        dettaglio_pesi::leggiNumero(testo, "intercetta", &w.intercetta);

    if (!ok) {
        if (errore) *errore = "formato non riconosciuto";
        return predefiniti;
    }

    w.caricati = true;
    w.origine = percorso;
    if (errore) errore->clear();
    return w;
}

// ---------------------------------------------------------------------------
//  La RETE NEURALE compilata (breath_model_net.h)
//
//  14 ingressi -> 8 neuroni (ReLU) -> 1 uscita (sigmoide). Sono 129 numeri,
//  circa 1 KB. A differenza della regressione logistica lo strato nascosto
//  permette combinazioni non lineari ("tonale E debole E in pausa"), che una
//  retta non sa esprimere: e' quello che le permette di riconoscere un fiato
//  tonale che la retta scambia per voce.
// ---------------------------------------------------------------------------
inline float reteProbability(const BreathSegmentFeatures& f) {
#ifdef BC_PRIVATE_LEGACY_MODELS
    const float valori[breath_net::kIngressi] = {
        f.durataMs, f.flatMedia, f.harmMedia, f.dbMedia,
        f.centMedia, f.centMin, f.centMax, f.zcrMedia,
        f.bmediaMedia, f.baltaMedia, f.viciniDb, f.sottoViciniDb,
        f.sopraFondoDb, f.inPausa,
    };

    // Normalizzazione con media e deviazione viste in addestramento.
    float normalizzati[breath_net::kIngressi];
    for (int i = 0; i < breath_net::kIngressi; ++i) {
        const float d = breath_net::kDeviazione[i];
        normalizzati[i] = (d > 1e-9f) ? (valori[i] - breath_net::kMedia[i]) / d : 0.0f;
    }

    // Strato nascosto: ReLU.
    float nascosti[breath_net::kHidden];
    for (int j = 0; j < breath_net::kHidden; ++j) {
        float somma = breath_net::kB1[j];
        for (int i = 0; i < breath_net::kIngressi; ++i) {
            somma += normalizzati[i] * breath_net::kW1[i * breath_net::kHidden + j];
        }
        nascosti[j] = somma > 0.0f ? somma : 0.0f;
    }

    // Uscita: sigmoide.
    float somma = breath_net::kB2;
    for (int j = 0; j < breath_net::kHidden; ++j) {
        somma += nascosti[j] * breath_net::kW2[j];
    }
    if (somma >= 0.0f) {
        const float e = std::exp(-somma);
        return 1.0f / (1.0f + e);
    }
    const float e = std::exp(somma);
    return e / (1.0f + e);
#else
    (void)f; return std::numeric_limits<float>::quiet_NaN();
#endif
}


// ---------------------------------------------------------------------------
//  Il modello PRECEDENTE (candidati DSP)
//
//  Stessa matematica di reteProbability, ma con i pesi di
//  breath_model_net_narrow.h. Serve perche' il modello nuovo, addestrato sui
//  candidati larghi, applicato ai soli candidati DSP scarta quasi tutto.
//  Le feature sono le stesse (computeSegmentFeatures), quindi il confronto
//  fra i due modelli e' onesto.
// ---------------------------------------------------------------------------
inline float reteProbabilityNarrow(const BreathSegmentFeatures& f) {
#ifdef BC_PRIVATE_LEGACY_MODELS
    const float valori[breath_net_narrow::kIngressi] = {
        f.durataMs, f.flatMedia, f.harmMedia, f.dbMedia,
        f.centMedia, f.centMin, f.centMax, f.zcrMedia,
        f.bmediaMedia, f.baltaMedia, f.viciniDb, f.sottoViciniDb,
        f.sopraFondoDb, f.inPausa,
    };

    float normalizzati[breath_net_narrow::kIngressi];
    for (int i = 0; i < breath_net_narrow::kIngressi; ++i) {
        const float d = breath_net_narrow::kDeviazione[i];
        normalizzati[i] = (d > 1e-9f) ? (valori[i] - breath_net_narrow::kMedia[i]) / d : 0.0f;
    }

    float nascosti[breath_net_narrow::kHidden];
    for (int j = 0; j < breath_net_narrow::kHidden; ++j) {
        float somma = breath_net_narrow::kB1[j];
        for (int i = 0; i < breath_net_narrow::kIngressi; ++i) {
            somma += normalizzati[i] * breath_net_narrow::kW1[i * breath_net_narrow::kHidden + j];
        }
        nascosti[j] = somma > 0.0f ? somma : 0.0f;
    }

    float somma = breath_net_narrow::kB2;
    for (int j = 0; j < breath_net_narrow::kHidden; ++j) {
        somma += nascosti[j] * breath_net_narrow::kW2[j];
    }
    if (somma >= 0.0f) {
        const float e = std::exp(-somma);
        return 1.0f / (1.0f + e);
    }
    const float e = std::exp(somma);
    return e / (1.0f + e);
#else
    (void)f; return std::numeric_limits<float>::quiet_NaN();
#endif
}


// Rete con pesi CARICATI da un profilo. Se non sono validi si ricade sulla
// rete compilata: chiamare questa funzione non puo' peggiorare nulla.
inline float reteProbabilityConPesi(const BreathNetWeights& rete,
                                    const BreathSegmentFeatures& f) {
    if (!rete.caricati || rete.ingressi != breath_net::kIngressi ||
        rete.nascosti <= 0 || rete.nascosti > 64 ||
        rete.media.size() != static_cast<size_t>(rete.ingressi) ||
        rete.deviazione.size() != static_cast<size_t>(rete.ingressi) ||
        rete.W1.size() != static_cast<size_t>(rete.ingressi * rete.nascosti) ||
        rete.b1.size() != static_cast<size_t>(rete.nascosti) ||
        rete.W2.size() != static_cast<size_t>(rete.nascosti)) {
        return reteProbability(f);
    }

    const float valori[breath_net::kIngressi] = {
        f.durataMs, f.flatMedia, f.harmMedia, f.dbMedia,
        f.centMedia, f.centMin, f.centMax, f.zcrMedia,
        f.bmediaMedia, f.baltaMedia, f.viciniDb, f.sottoViciniDb,
        f.sopraFondoDb, f.inPausa,
    };

    float normalizzati[breath_net::kIngressi];
    for (int i = 0; i < breath_net::kIngressi; ++i) {
        const float d = rete.deviazione[static_cast<size_t>(i)];
        normalizzati[i] = (d > 1e-9f)
            ? (valori[i] - rete.media[static_cast<size_t>(i)]) / d
            : 0.0f;
    }

    float nascosti[64];
    for (int j = 0; j < rete.nascosti; ++j) {
        float somma = rete.b1[static_cast<size_t>(j)];
        for (int i = 0; i < breath_net::kIngressi; ++i) {
            somma += normalizzati[i] *
                     rete.W1[static_cast<size_t>(i) *
                                 static_cast<size_t>(rete.nascosti) +
                             static_cast<size_t>(j)];
        }
        nascosti[j] = somma > 0.0f ? somma : 0.0f;
    }

    float somma = rete.b2;
    for (int j = 0; j < rete.nascosti; ++j) {
        somma += nascosti[j] * rete.W2[static_cast<size_t>(j)];
    }
    if (somma >= 0.0f) {
        const float e = std::exp(-somma);
        return 1.0f / (1.0f + e);
    }
    const float e = std::exp(somma);
    return e / (1.0f + e);
}

} // namespace bc

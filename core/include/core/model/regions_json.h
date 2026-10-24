// SPDX-License-Identifier: MPL-2.0
// ============================================================================
//  regions_json.h - Legge le regioni prodotte dall'analisi AI
//
//  Il rilevamento con la rete neurale gira in Python (Respiro-en), perche' li'
//  ci sono PyTorch e le librerie audio. Il risultato pero' deve arrivare ai
//  consumatori C++ (editor, CLI, estensione REAPER), che non possono
//  dipendere da Python.
//
//  Il ponte e' un file JSON. Questo modulo lo legge.
//
//  FORMATO (versione 1)
//  --------------------
//    {
//      "versione": 1,
//      "file": "C:\\...\\take.wav",
//      "durata_s": 20.5625,
//      "sample_rate": 48000,
//      "campioni": 986999,
//      "soglia": 0.064,
//      "sorgente": "respiro-en",
//      "fiati": [ {"inizio_s": 2.69, "fine_s": 3.59}, ... ],
//      "click": [ {"inizio_s": 19.31, "fine_s": 19.41}, ... ]
//    }
//
//  Le chiavi sconosciute si ignorano: un file scritto da una versione piu'
//  recente non deve rompere quella vecchia.
// ============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace bc {

// Un evento trovato dall'analisi, in secondi.
struct AiRegion {
    enum class Kind { Breath, Click, Unknown };

    Kind kind = Kind::Unknown;
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    float confidence = 0.0f;   // 0 se il file non la riporta
};

struct AiRegions {
    int version = 1;
    std::string sourceFile;
    double durationSeconds = 0.0;
    int sampleRate = 0;
    int64_t samples = 0;
    std::string source;              // "respiro-en", "dsp", ...
    std::vector<AiRegion> regions;   // fiati e click, nell'ordine del file

    bool empty() const { return regions.empty(); }

    int countOf(AiRegion::Kind kind) const {
        int n = 0;
        for (const AiRegion& r : regions) {
            if (r.kind == kind) ++n;
        }
        return n;
    }
};

// Legge il file. In caso di errore restituisce false e scrive il motivo.
//
// Un file con campi mancanti NON e' un errore: si legge quello che c'e'. E'
// un errore solo un file che non si apre o che non contiene alcuna regione
// riconoscibile (probabilmente non e' il formato giusto).
bool readAiRegions(const std::string& path, AiRegions* out, std::string* error = nullptr);

// Scrive il file, per simmetria e per i test.
bool writeAiRegions(const std::string& path, const AiRegions& regions,
                    std::string* error = nullptr);

} // namespace bc

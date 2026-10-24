// SPDX-License-Identifier: MPL-2.0
#include "core/model/regions_json.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace bc {

namespace {

// ---------------------------------------------------------------------------
//  Un lettore JSON minimo, limitato a quello che serve qui.
//
//  Non e' un parser generico: legge oggetti, array, stringhe e numeri, e
//  ignora il resto. Basta per il formato delle regioni, che scriviamo noi.
//  Un parser completo sarebbe stato molto piu' codice di quanto il problema
//  richieda, e ogni riga in piu' e' una riga che puo' sbagliare.
// ---------------------------------------------------------------------------
class Lettore {
public:
    explicit Lettore(const std::string& testo) : testo_(testo) {}

    void saltaSpazi() {
        while (pos_ < testo_.size()) {
            const char c = testo_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    bool fine() {
        saltaSpazi();
        return pos_ >= testo_.size();
    }

    char guarda() {
        saltaSpazi();
        return pos_ < testo_.size() ? testo_[pos_] : '\0';
    }

    bool consuma(char atteso) {
        saltaSpazi();
        if (pos_ < testo_.size() && testo_[pos_] == atteso) {
            ++pos_;
            return true;
        }
        return false;
    }

    // Legge "...". Gestisce le sequenze di escape che possono comparire nei
    // percorsi Windows ("C:\\cartella\\file.wav").
    bool stringa(std::string* out) {
        saltaSpazi();
        if (pos_ >= testo_.size() || testo_[pos_] != '"') return false;
        ++pos_;

        std::string risultato;
        while (pos_ < testo_.size()) {
            const char c = testo_[pos_++];
            if (c == '"') {
                *out = risultato;
                return true;
            }
            if (c == '\\' && pos_ < testo_.size()) {
                const char e = testo_[pos_++];
                switch (e) {
                    case 'n': risultato += '\n'; break;
                    case 't': risultato += '\t'; break;
                    case 'r': risultato += '\r'; break;
                    case 'b': risultato += '\b'; break;
                    case 'f': risultato += '\f'; break;
                    case 'u': {
                        // \uXXXX: si salta la sequenza. I nomi di file non
                        // contengono caratteri accentati in questo flusso.
                        for (int i = 0; i < 4 && pos_ < testo_.size(); ++i) ++pos_;
                        break;
                    }
                    default: risultato += e; break;
                }
            } else {
                risultato += c;
            }
        }
        return false;
    }

    bool numero(double* out) {
        saltaSpazi();
        const size_t inizio = pos_;
        if (pos_ < testo_.size() && (testo_[pos_] == '-' || testo_[pos_] == '+')) ++pos_;
        while (pos_ < testo_.size() &&
               (std::isdigit(static_cast<unsigned char>(testo_[pos_])) ||
                testo_[pos_] == '.' || testo_[pos_] == 'e' || testo_[pos_] == 'E' ||
                testo_[pos_] == '-' || testo_[pos_] == '+')) {
            ++pos_;
        }
        if (pos_ == inizio) return false;
        try {
            *out = std::stod(testo_.substr(inizio, pos_ - inizio));
        } catch (...) {
            return false;
        }
        return true;
    }

    // Salta un valore qualsiasi (per le chiavi che non conosciamo).
    bool saltaValore() {
        saltaSpazi();
        if (pos_ >= testo_.size()) return false;
        const char c = testo_[pos_];
        if (c == '"') {
            std::string scarto;
            return stringa(&scarto);
        }
        if (c == '{') return saltaOggetto();
        if (c == '[') return saltaArray();
        // numero, true, false, null
        while (pos_ < testo_.size() && testo_[pos_] != ',' && testo_[pos_] != '}' &&
               testo_[pos_] != ']') {
            ++pos_;
        }
        return true;
    }

    bool saltaArray() {
        if (!consuma('[')) return false;
        while (true) {
            saltaSpazi();
            if (consuma(']')) return true;
            if (!saltaValore()) return false;
            saltaSpazi();
            if (consuma(',')) continue;
            saltaSpazi();
            return consuma(']');
        }
    }

    bool saltaOggetto() {
        if (!consuma('{')) return false;
        while (true) {
            saltaSpazi();
            if (consuma('}')) return true;
            std::string chiave;
            if (!stringa(&chiave)) return false;
            if (!consuma(':')) return false;
            if (!saltaValore()) return false;
            saltaSpazi();
            if (consuma(',')) continue;
            saltaSpazi();
            return consuma('}');
        }
    }

    size_t posizione() const { return pos_; }
    void vaiA(size_t p) { pos_ = p; }

private:
    const std::string& testo_;
    size_t pos_ = 0;
};

// Legge un array di regioni: [ {"inizio_s": 1.0, "fine_s": 2.0}, ... ]
bool leggiRegioni(Lettore& lettore, AiRegion::Kind tipo,
                  std::vector<AiRegion>* fuori) {
    if (!lettore.consuma('[')) return false;

    while (true) {
        lettore.saltaSpazi();
        if (lettore.consuma(']')) return true;
        if (lettore.guarda() != '{') return false;
        if (!lettore.consuma('{')) return false;

        AiRegion regione;
        regione.kind = tipo;

        while (true) {
            lettore.saltaSpazi();
            if (lettore.consuma('}')) break;

            std::string chiave;
            if (!lettore.stringa(&chiave)) return false;
            if (!lettore.consuma(':')) return false;

            if (chiave == "inizio_s") {
                double v = 0.0;
                if (!lettore.numero(&v)) return false;
                regione.startSeconds = v;
            } else if (chiave == "fine_s") {
                double v = 0.0;
                if (!lettore.numero(&v)) return false;
                regione.endSeconds = v;
            } else if (chiave == "confidenza" || chiave == "confidenza_media" ||
                       chiave == "score") {
                double v = 0.0;
                if (!lettore.numero(&v)) return false;
                regione.confidence = static_cast<float>(v);
            } else if (!lettore.saltaValore()) {
                return false;
            }

            lettore.saltaSpazi();
            if (lettore.consuma(',')) continue;
            lettore.saltaSpazi();
            if (lettore.consuma('}')) break;
            return false;
        }

        if (regione.endSeconds > regione.startSeconds) {
            fuori->push_back(regione);
        }

        lettore.saltaSpazi();
        if (lettore.consuma(',')) continue;
        lettore.saltaSpazi();
        return lettore.consuma(']');
    }
}

} // namespace

bool readAiRegions(const std::string& path, AiRegions* out, std::string* error) {
    if (!out) {
        if (error) *error = "destinazione nulla";
        return false;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        if (error) *error = "impossibile aprire " + path;
        return false;
    }

    std::ostringstream contenuto;
    contenuto << file.rdbuf();
    const std::string testo = contenuto.str();

    if (testo.empty()) {
        if (error) *error = "file vuoto";
        return false;
    }

    AiRegions risultato;
    Lettore lettore(testo);

    if (!lettore.consuma('{')) {
        if (error) *error = "non e' un oggetto JSON";
        return false;
    }

    while (true) {
        lettore.saltaSpazi();
        if (lettore.consuma('}')) break;
        if (lettore.fine()) break;

        std::string chiave;
        if (!lettore.stringa(&chiave)) {
            if (error) *error = "chiave non leggibile";
            return false;
        }
        if (!lettore.consuma(':')) {
            if (error) *error = "manca ':' dopo " + chiave;
            return false;
        }

        if (chiave == "fiati" || chiave == "respiri") {
            if (!leggiRegioni(lettore, AiRegion::Kind::Breath, &risultato.regions)) {
                if (error) *error = "array 'fiati' illeggibile";
                return false;
            }
        } else if (chiave == "click") {
            if (!leggiRegioni(lettore, AiRegion::Kind::Click, &risultato.regions)) {
                if (error) *error = "array 'click' illeggibile";
                return false;
            }
        } else if (chiave == "versione") {
            double v = 0.0;
            if (!lettore.numero(&v)) {
                if (error) *error = "'versione' non numerica";
                return false;
            }
            risultato.version = static_cast<int>(v);
        } else if (chiave == "durata_s") {
            double v = 0.0;
            if (!lettore.numero(&v)) return false;
            risultato.durationSeconds = v;
        } else if (chiave == "sample_rate") {
            double v = 0.0;
            if (!lettore.numero(&v)) return false;
            risultato.sampleRate = static_cast<int>(v);
        } else if (chiave == "campioni") {
            double v = 0.0;
            if (!lettore.numero(&v)) return false;
            risultato.samples = static_cast<int64_t>(v);
        } else if (chiave == "file") {
            if (!lettore.stringa(&risultato.sourceFile)) return false;
        } else if (chiave == "sorgente") {
            if (!lettore.stringa(&risultato.source)) return false;
        } else if (!lettore.saltaValore()) {
            if (error) *error = "valore illeggibile per " + chiave;
            return false;
        }

        lettore.saltaSpazi();
        if (lettore.consuma(',')) continue;
        lettore.saltaSpazi();
        if (lettore.consuma('}')) break;
        if (error) *error = "struttura inattesa dopo " + chiave;
        return false;
    }

    // Le regioni vanno in ordine di tempo: il file puo' avere i fiati e i
    // click in due array separati, ma chi le applica vuole una sequenza sola.
    std::sort(risultato.regions.begin(), risultato.regions.end(),
              [](const AiRegion& a, const AiRegion& b) {
                  return a.startSeconds < b.startSeconds;
              });

    if (risultato.regions.empty()) {
        if (error) *error = "nessuna regione nel file";
        return false;
    }

    *out = risultato;
    if (error) error->clear();
    return true;
}

bool writeAiRegions(const std::string& path, const AiRegions& regions,
                    std::string* error) {
    std::ofstream file(path, std::ios::trunc);
    if (!file) {
        if (error) *error = "impossibile scrivere " + path;
        return false;
    }

    // Il percorso va con gli apici rovesciati raddoppiati, altrimenti il JSON
    // non e' valido e nessun lettore lo apre.
    auto escapa = [](const std::string& s) {
        std::string r;
        for (char c : s) {
            if (c == '\\') r += "\\\\";
            else if (c == '"') r += "\\\"";
            else r += c;
        }
        return r;
    };

    file << "{\n";
    file << " \"versione\": " << regions.version << ",\n";
    file << " \"file\": \"" << escapa(regions.sourceFile) << "\",\n";
    file << " \"durata_s\": " << regions.durationSeconds << ",\n";
    file << " \"sample_rate\": " << regions.sampleRate << ",\n";
    file << " \"campioni\": " << regions.samples << ",\n";
    file << " \"sorgente\": \"" << escapa(regions.source) << "\",\n";

    auto scrivi = [&file](const char* nome, const std::vector<AiRegion>& tutte,
                          AiRegion::Kind tipo, bool ultimo) {
        file << " \"" << nome << "\": [";
        bool primo = true;
        for (const AiRegion& r : tutte) {
            if (r.kind != tipo) continue;
            if (!primo) file << ",";
            file << "\n  {\"inizio_s\": " << r.startSeconds
                 << ", \"fine_s\": " << r.endSeconds;
            if (r.confidence > 0.0f) file << ", \"confidenza\": " << r.confidence;
            file << "}";
            primo = false;
        }
        file << (primo ? "]" : "\n ]") << (ultimo ? "\n" : ",\n");
    };

    scrivi("fiati", regions.regions, AiRegion::Kind::Breath, false);
    scrivi("click", regions.regions, AiRegion::Kind::Click, true);
    file << "}\n";

    if (error) error->clear();
    return true;
}

} // namespace bc

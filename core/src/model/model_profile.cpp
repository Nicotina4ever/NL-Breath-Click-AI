// SPDX-License-Identifier: MPL-2.0
#include "core/model/model_profile.h"

#include "core/detect/breath_model_eval.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace bc {

namespace {

std::string trim(const std::string& testo) {
    size_t inizio = 0;
    while (inizio < testo.size() &&
           std::isspace(static_cast<unsigned char>(testo[inizio]))) {
        ++inizio;
    }
    size_t fine = testo.size();
    while (fine > inizio &&
           std::isspace(static_cast<unsigned char>(testo[fine - 1]))) {
        --fine;
    }
    return testo.substr(inizio, fine - inizio);
}

// I numeri arrivano scritti da una persona o da uno script: se c'e' qualcosa
// di storto si tiene il valore predefinito invece di far fallire tutto il
// profilo per una virgola.
float toFloat(const std::string& testo, float predefinito) {
    try {
        return std::stof(testo);
    } catch (...) {
        return predefinito;
    }
}

int toInt(const std::string& testo, int predefinito) {
    try {
        return std::stoi(testo);
    } catch (...) {
        return predefinito;
    }
}

bool toBool(const std::string& testo, bool predefinito) {
    const std::string minuscolo = [&testo] {
        std::string r = testo;
        std::transform(r.begin(), r.end(), r.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return r;
    }();
    if (minuscolo == "si" || minuscolo == "vero" || minuscolo == "1" ||
        minuscolo == "true") {
        return true;
    }
    if (minuscolo == "no" || minuscolo == "falso" || minuscolo == "0" ||
        minuscolo == "false") {
        return false;
    }
    return predefinito;
}

} // namespace

bool ModelRegistry::readManifest(const std::string& directory, ModelProfile* out,
                                 std::string* error) {
    if (!out) {
        if (error) *error = "profilo nullo";
        return false;
    }

    const std::filesystem::path manifest =
        std::filesystem::path(directory) / "manifest.txt";

    std::ifstream file(manifest);
    if (!file) {
        if (error) *error = "manifest non trovato in " + directory;
        return false;
    }

    ModelProfile profilo;
    profilo.directory = directory;

    std::string riga;
    while (std::getline(file, riga)) {
        riga = trim(riga);
        if (riga.empty() || riga[0] == '#') continue;

        const size_t uguale = riga.find('=');
        if (uguale == std::string::npos) continue;

        const std::string chiave = trim(riga.substr(0, uguale));
        const std::string valore = trim(riga.substr(uguale + 1));

        if (chiave == "nome") {
            profilo.name = valore;
        } else if (chiave == "descrizione") {
            profilo.description = valore;
        } else if (chiave == "materiale") {
            profilo.materialType = valore.empty() ? "grezzo" : valore;
        } else if (chiave == "fiati_modello") {
            profilo.breathModel = valore;
        } else if (chiave == "fiati_soglia") {
            profilo.breathThreshold = toFloat(valore, profilo.breathThreshold);
        } else if (chiave == "fiati_min_ms") {
            profilo.breathMinMs = toInt(valore, profilo.breathMinMs);
        } else if (chiave == "fiati_pesi") {
            profilo.weightsFile = valore;
        } else if (chiave == "fiati_rete") {
            profilo.netFile = valore;
        } else if (chiave == "fiati_larghi") {
            profilo.wideCandidates = toBool(valore, profilo.wideCandidates);
        } else if (chiave == "click_modello") {
            profilo.clickModel = valore;
        } else if (chiave == "click_soglia") {
            profilo.clickThreshold = toFloat(valore, profilo.clickThreshold);
        } else if (chiave == "click_ripara") {
            profilo.repairClicks = toBool(valore, profilo.repairClicks);
        }
        // Le chiavi sconosciute si ignorano: un manifest scritto da una
        // versione piu' nuova non deve rompere questa.
    }

    // Il nome della cartella fa da nome se il manifest non lo dichiara.
    if (profilo.name.empty()) {
        profilo.name = std::filesystem::path(directory).filename().string();
    }
    profilo.fromBase = (profilo.name == "base");

    *out = profilo;
    if (error) error->clear();
    return true;
}

bool ModelRegistry::writeManifest(const ModelProfile& profile, std::string* error) {
    if (profile.directory.empty()) {
        if (error) *error = "profilo senza cartella";
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(profile.directory, ec);
    if (ec) {
        if (error) *error = "non riesco a creare " + profile.directory;
        return false;
    }

    const std::filesystem::path manifest =
        std::filesystem::path(profile.directory) / "manifest.txt";

    std::ofstream file(manifest, std::ios::trunc);
    if (!file) {
        if (error) *error = "non riesco a scrivere " + manifest.string();
        return false;
    }

    file << "# Profilo di rilevamento BreathClick\n";
    file << "# Generato dal programma. Si puo' modificare a mano.\n";
    file << "\n";
    file << "nome = " << profile.name << "\n";
    file << "descrizione = " << profile.description << "\n";
    file << "materiale = " << profile.materialType << "\n";
    file << "\n";
    file << "# Fiati\n";
    file << "fiati_modello = " << profile.breathModel << "\n";
    file << "fiati_soglia = " << profile.breathThreshold << "\n";
    file << "fiati_min_ms = " << profile.breathMinMs << "\n";
    file << "fiati_pesi = " << profile.weightsFile << "\n";
    file << "fiati_rete = " << profile.netFile << "\n";
    file << "fiati_larghi = " << (profile.wideCandidates ? "si" : "no") << "\n";
    file << "\n";
    file << "# Click\n";
    file << "click_modello = " << profile.clickModel << "\n";
    file << "click_soglia = " << profile.clickThreshold << "\n";
    file << "click_ripara = " << (profile.repairClicks ? "si" : "no") << "\n";

    if (error) error->clear();
    return true;
}

bool ModelRegistry::createProfile(const std::string& modelsRoot, const std::string& name,
                                  ModelProfile* out, std::string* error) {
    if (name.empty()) {
        if (error) *error = "nome del profilo vuoto";
        return false;
    }

    const std::filesystem::path cartella = std::filesystem::path(modelsRoot) / name;
    if (std::filesystem::exists(cartella)) {
        if (error) *error = "il profilo '" + name + "' esiste gia'";
        return false;
    }

    ModelProfile profilo;
    profilo.name = name;
    profilo.description = "";
    profilo.directory = cartella.string();

    if (!writeManifest(profilo, error)) {
        return false;
    }

    if (out) *out = profilo;
    return true;
}

bool ModelRegistry::scan(const std::string& modelsRoot, std::string* error) {
    profiles_.clear();
    root_ = modelsRoot;

    std::error_code ec;
    if (!std::filesystem::exists(modelsRoot, ec)) {
        // Cartella assente non e' un errore: il programma funziona con i
        // valori predefiniti. Si segnala solo per informazione.
        if (error) *error = "cartella dei modelli non trovata: " + modelsRoot;
        return false;
    }

    std::vector<std::string> nomi;
    for (const auto& voce : std::filesystem::directory_iterator(modelsRoot, ec)) {
        if (ec) break;
        if (!voce.is_directory()) continue;
        nomi.push_back(voce.path().filename().string());
    }
    std::sort(nomi.begin(), nomi.end());

    for (const std::string& nome : nomi) {
        const std::string cartella = (std::filesystem::path(modelsRoot) / nome).string();
        ModelProfile profilo;
        std::string avviso;
        if (!readManifest(cartella, &profilo, &avviso)) {
            continue;   // profilo rotto: si salta, non si blocca tutto
        }
        profiles_.push_back(profilo);
    }

    // Il profilo base va PRIMO nell'elenco.
    //
    // L'ordine alfabetico metteva "addestrato" davanti a "base", e nel menu
    // il profilo di partenza finiva in mezzo agli altri: chi apre il menu si
    // aspetta di trovarlo in cima, e il comando "ripristina il base" deve
    // corrispondere alla prima voce.
    {
        const auto base = std::find_if(
            profiles_.begin(), profiles_.end(),
            [](const ModelProfile& p) { return p.name == "base"; });
        if (base != profiles_.end()) {
            std::rotate(profiles_.begin(), base, base + 1);
        }
    }

    // Il profilo base deve esserci sempre. Se manca lo si crea in memoria,
    // cosi' "ripristina al base" funziona anche su un'installazione nuova.
    if (!find("base")) {
        ModelProfile base;
        base.name = "base";
        base.description = "Modello di partenza (incorporato)";
        base.directory = (std::filesystem::path(modelsRoot) / "base").string();
        base.fromBase = true;
        profiles_.insert(profiles_.begin(), base);
    }

    if (error) error->clear();
    return true;
}

const ModelProfile* ModelRegistry::find(const std::string& name) const {
    for (const ModelProfile& profilo : profiles_) {
        if (profilo.name == name) return &profilo;
    }
    return nullptr;
}

const ModelProfile& ModelRegistry::base() const {
    if (const ModelProfile* trovato = find("base")) {
        return *trovato;
    }
    fallback_ = ModelProfile{};
    fallback_.name = "base";
    fallback_.description = "Modello di partenza (incorporato)";
    fallback_.fromBase = true;
    return fallback_;
}


// ---------------------------------------------------------------------------
//  Profilo -> impostazioni
// ---------------------------------------------------------------------------
void applyProfile(const ModelProfile& profile, DetectorSettings& settings) {
    // Solo le soglie: il dispositivo di calcolo e le opzioni di riparazione
    // restano dell'utente. Un profilo descrive COME si riconosce un evento,
    // non come lo si corregge.
    // I pesi: se il profilo ne dichiara uno, si legge; altrimenti restano
    // quelli compilati nel binario.
    if (!profile.weightsFile.empty()) {
        const std::filesystem::path percorso =
            std::filesystem::path(profile.directory) / profile.weightsFile;
        std::string errore;
        settings.modelWeights = caricaPesi(percorso.string(), &errore);
        settings.modelWeights.nome = profile.name;
    } else {
        settings.modelWeights = pesiIncorporati();
        settings.modelWeights.nome = profile.name;
    }

    // La rete del profilo ha la precedenza su quella compilata. Se il file
    // non c'e' o e' rotto si ricade sulla compilata (usaReteDaFile = false).
    if (!profile.netFile.empty()) {
        const std::filesystem::path percorso =
            std::filesystem::path(profile.directory) / profile.netFile;
        std::string errore;
        settings.retePesi = caricaRete(percorso.string(), &errore);
        settings.usaReteDaFile = settings.retePesi.caricati;
    } else {
        settings.retePesi = BreathNetWeights{};
        settings.usaReteDaFile = false;
    }

    // Il profilo DEFINISCE il modello: "base" lo spegne, un profilo con pesi
    // o rete lo accende. Cosi' il menu Modello cambia davvero il rilevamento.
    settings.useBreathModel = profile.hasBreathModel() || profile.hasNet();
    settings.usaReteNeurale = profile.hasNet();
    settings.proposteLarghe = profile.wideCandidates;

    settings.breathThreshold = profile.breathThreshold;
    settings.clickThreshold = profile.clickThreshold;
    settings.minBreathMs = profile.breathMinMs;
}

ModelProfile profileFromSettings(const DetectorSettings& settings,
                                 const std::string& name) {
    ModelProfile profilo;
    profilo.name = name;
    profilo.breathThreshold = settings.breathThreshold;
    profilo.clickThreshold = settings.clickThreshold;
    profilo.breathMinMs = settings.minBreathMs;
    return profilo;
}

} // namespace bc

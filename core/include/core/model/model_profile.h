// SPDX-License-Identifier: MPL-2.0
// ============================================================================
//  model_profile.h - Profili di rilevamento
//
//  Un profilo e' un assetto di rilevamento salvato con un nome: le soglie, e
//  se ci sono i modelli addestrati su un materiale preciso.
//
//  Serve a usare l'assetto giusto per ogni progetto senza ritarare niente.
//  Una voce di doppiaggio, un audiolibro gia' trattato e una registrazione
//  rumorosa hanno caratteristiche diverse: un profilo per ciascuno.
//
//  Il profilo di partenza si chiama sempre "base" e non si puo' cancellare:
//  e' il "ripristina al base" che serve quando un profilo addestrato peggiora
//  le cose.
//
//  FORMATO DEL MANIFEST
//  --------------------
//  Un file di testo `manifest.txt` nella cartella del profilo, una coppia
//  chiave = valore per riga. Non si usa JSON perche' qui non serve: sono
//  sette valori piatti, e un lettore JSON in C++ sarebbe stato cento righe di
//  codice e una superficie di errori in piu' per leggere sette numeri.
//
//  Le correzioni dell'utente restano in JSON, perche' quelle le legge Python.
// ============================================================================
#pragma once

#include <string>
#include <vector>

#include "core/detect/detection.h"

namespace bc {

struct ModelProfile {
    // Identificatore del profilo: e' il nome della cartella.
    std::string name;

    std::string description;

    // "grezzo", "trattato", "musica", "podcast", "altro".
    //
    // Serve al caso dei file gia' trattati: un modello addestrato su materiale
    // grezzo non riconosce bene un file gia' equalizzato o compresso, e il
    // manifest lo dichiara invece di far indovinare.
    std::string materialType = "grezzo";

    // --- fiati -------------------------------------------------------------
    // Il modello addestrato e' opzionale: senza, si usa il rilevamento
    // incorporato con le soglie qui sotto.
    std::string breathModel;
    // File dei pesi addestrati (pesi.json nella cartella del profilo).
    // Vuoto = si usano i pesi compilati nel binario.
    std::string weightsFile;
    // File della RETE (rete.json appiattito). Se c'e', il profilo usa la
    // propria rete invece di quella compilata: si sceglie dal menu senza
    // ricompilare, ed e' il modo per installare un modello riaddestrato.
    std::string netFile;
    // La rete del profilo e' addestrata sui CANDIDATI LARGHI: va usata con
    // quelli, altrimenti le feature sono di un'altra famiglia e scarta tutto.
    bool wideCandidates = false;
    float breathThreshold = 0.02f;
    int breathMinMs = 90;

    // --- click -------------------------------------------------------------
    std::string clickModel;
    float clickThreshold = 0.55f;
    bool repairClicks = true;

    // --- stato -------------------------------------------------------------
    std::string directory;      // cartella da cui viene
    bool fromBase = false;      // e' il profilo di partenza?

    bool hasBreathModel() const { return !breathModel.empty(); }
    bool hasNet() const { return !netFile.empty(); }
    bool hasClickModel() const { return !clickModel.empty(); }
};

// Legge e scrive il manifest, e tiene l'elenco dei profili disponibili.
class ModelRegistry {
public:
    // Scansiona la cartella dei profili.
    //
    // Non fallisce se la cartella non esiste o se un profilo e' rotto: un
    // profilo illeggibile viene saltato con un avviso, perche' un manifest
    // sbagliato non deve impedire di aprire il programma.
    bool scan(const std::string& modelsRoot, std::string* error = nullptr);

    const std::vector<ModelProfile>& profiles() const { return profiles_; }

    // nullptr se non esiste.
    const ModelProfile* find(const std::string& name) const;

    // Il profilo di partenza. Se non c'e' nemmeno quello, restituisce un
    // profilo predefinito costruito al volo: il programma deve funzionare
    // anche con la cartella dei modelli vuota.
    const ModelProfile& base() const;

    bool empty() const { return profiles_.empty(); }

    const std::string& root() const { return root_; }

    // Scrive (o riscrive) il manifest di un profilo.
    static bool writeManifest(const ModelProfile& profile, std::string* error = nullptr);

    // Legge un manifest senza passare dalla scansione. Utile ai test.
    static bool readManifest(const std::string& directory, ModelProfile* out,
                             std::string* error = nullptr);

    // Crea la struttura di un profilo nuovo. Non sovrascrive se esiste gia'.
    static bool createProfile(const std::string& modelsRoot, const std::string& name,
                              ModelProfile* out, std::string* error = nullptr);

private:
    std::vector<ModelProfile> profiles_;
    std::string root_;
    mutable ModelProfile fallback_;   // usato da base() quando non c'e' nulla
};

// Applica le soglie di un profilo alle impostazioni di rilevamento.
//
// Non tocca il dispositivo di calcolo ne' le opzioni di riparazione: quelle
// sono scelte dell'utente, non del profilo.
void applyProfile(const ModelProfile& profile, DetectorSettings& settings);

// Costruisce un profilo a partire dalle impostazioni correnti. E' il percorso
// inverso, serve a "salva il profilo con queste soglie".
ModelProfile profileFromSettings(const DetectorSettings& settings,
                                 const std::string& name);

} // namespace bc

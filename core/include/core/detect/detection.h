// SPDX-License-Identifier: MPL-2.0
// ============================================================================
//  detection.h — Tipi condivisi da analisi offline, CLI, plugin e extension
// ============================================================================
#pragma once

#include "core/detect/breath_model_eval.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/inference/device_policy.h"

namespace bc {

enum class DetectionKind {
    Breath,
    Click
};

const char* detectionKindName(DetectionKind kind);

// Una regione rilevata, in campioni assoluti dall'inizio del materiale.
struct Detection {
    DetectionKind kind = DetectionKind::Breath;
    int64_t startSample = 0;
    int64_t endSample = 0;       // esclusivo
    float peakScore = 0.0f;

    // Parere del modello addestrato su questo evento, 0..1.
    // -1 = modello non applicato (analisi senza --ai).
    float modelScore = -1.0f;
    float meanScore = 0.0f;
    std::string classification;
    std::string reason;
    float dspScore = 0, aiScore = 0, aiMaxProbability = 0;

    int64_t lengthSamples() const { return endSample - startSample; }
    double startSeconds(double sampleRate) const {
        return sampleRate > 0.0 ? static_cast<double>(startSample) / sampleRate : 0.0;
    }
    double endSeconds(double sampleRate) const {
        return sampleRate > 0.0 ? static_cast<double>(endSample) / sampleRate : 0.0;
    }
};

// ---------------------------------------------------------------------------
//  Impostazioni del rilevatore (condivise da tutte le interfacce)
// ---------------------------------------------------------------------------
struct DetectorSettings {
    // Soglie di punteggio.
    // Soglia del punteggio dei respiri.
    //
    // Il punteggio e' il margine della condizione PIU' DEBOLE, quindi vale
    // quanto la condizione peggiore. Con 0,55 serviva un margine di 0,55 su
    // TUTTE e quattro: in pratica flatness >= 0,11 + 0,55*0,15 = 0,193, mentre
    // i respiri reali stanno a 0,15-0,21. La soglia li tagliava fuori quasi
    // tutti, e sul segnale di prova non ne trovava nessuno.
    //
    // Ora la DECISIONE la fanno le porte, non la soglia. Il centroide spettrale
    // ha separazione zero fra respiri e sibilanti (4955 Hz contro 7334 Hz) ed e'
    // una porta dura: esclude le consonanti con certezza, non con un margine.
    //
    // Con la precisione garantita dal centroide, la soglia del punteggio puo'
    // stare bassissima: serve solo a dire "almeno un margine positivo", cioe'
    // "tutte e tre le porte passano". Alzarla riduce il richiamo senza
    // migliorare la precisione.
    //
    // Misurato: 0,25 -> 21 respiri in 22,4 minuti; 0,02 -> circa 115, che e' il
    // range reale per voce letta (6-12 al minuto).
    float breathThreshold = 0.02f;

    // Applica il modello addestrato (breath_model.h) ai fiati trovati.
    //
    // Non e' un rilevatore alternativo: e' un SECONDO PARERE sui candidati
    // del DSP. Il modello e' compilato nel binario, quindi non costa niente
    // in memoria ne' in dipendenze.
    //
    // Oggi i pesi vengono da etichette euristiche e il peso dominante e'
    // in_pausa: attivarlo non migliora i risultati. Serve a tenere in piedi
    // la conduttura, cosi' quando il modello verra' riaddestrato sulle
    // annotazioni vere bastera' ricompilare.
    bool useBreathModel = false;

    // Sotto questa probabilita' il candidato viene scartato.
    float breathModelThreshold = 0.50f;

    // Profilo da file, oppure coefficienti privati se abilitati in compilazione.
    // Se il filtro viene richiesto senza un modello, analyze ritorna un errore.
    BreathModelWeights modelWeights = pesiIncorporati();

    // Usa la RETE NEURALE compilata invece della regressione logistica.
    //
    // La rete ha uno strato nascosto e puo' separare casi che una retta non
    // distingue, come un fiato tonale. Costa 1 KB di costanti compilate.
    bool usaReteNeurale = false;

    // Rete caricata da un profilo (rete.json). Se caricati, questi pesi hanno
    // la precedenza sulla rete compilata: e' cosi' che si sceglie un modello
    // dal menu senza ricompilare. usaReteDaFile dice se usarli davvero.
    BreathNetWeights retePesi;
    bool usaReteDaFile = false;

    // Se true (solo con la rete), i fiati vengono dai CANDIDATI LARGHI
    // (armonicita' < 0.90, >= 40 ms) valutati dalla rete, non dai soli
    // candidati del DSP. E' l'unico modo per far vedere alla rete un fiato
    // tonale come quello a 17 s, che le porte del DSP azzerano.
    // Default false: si attiva con --proposte-larghe e si misura.
    bool proposteLarghe = false;
    float clickThreshold = 0.55f;

    // Sensibilita' DSP predefinita. Non e' una misura di accuratezza.
    float breathSensitivity = 0.8f;
    float clickSensitivity = 0.5f;

    // Durate accettate (ms).
    // Il minimo dei respiri e' 90 ms: sotto questa soglia si trovano gli
    // attacchi di nota e le plosive, che sono spettralmente simili a un
    // respiro ma troppo brevi per esserlo. Un respiro reale dura 120-600 ms.
    int minBreathMs = 90;
    int maxBreathMs = 900;
    int minClickMs = 1;
    int maxClickMs = 45;
    int mergeGapMs = 60;

    // I respiri devono stare in una pausa invece che dentro una parola.
    //
    // E' il discriminante che separa un respiro da una sibilante, e servirebbe:
    // sul materiale reale, dei 28 candidati di un take, 27 cadevano dentro il
    // parlato - erano "s" ed "f", non respiri.
    //
    // Il filtro confronta il livello del candidato con quello dei suoi DINTORNI:
    // un respiro non e' molto piu' piano di cio' che lo circonda, mentre una
    // consonante sta in mezzo a parlato forte.
    //
    // Misurato sui take reali:
    //     consonante -28,3 dB con dintorni -21,9   (6 dB piu' forti)
    //     respiro    -35,1 dB con dintorni -33,5   (quasi uguali)
    //
    // Il livello ASSOLUTO del candidato non serve: i due gruppi si sovrappongono
    // (-33,4 compare in entrambi).
    //
    // La tolleranza segue la sensibilita' dell'utente, vedi
    // breathContextToleranceDb(). Con il valore di fabbrica sono 3 dB; alzando
    // la sensibilita' si allarga e passano piu' candidati.
    bool requirePauseForBreath = true;
    int pauseRadiusMs = 200;

    // Conservato per compatibilita' delle interfacce. Non si sceglie piu'
    // nulla: il rilevamento e' tutto DSP e gira su CPU.
    DeviceRequest device = DeviceRequest::Cpu;

    // Analisi.
    int fftSize = 1024;
    int hopSize = 256;
    bool detectBreath = true;
    bool detectClicks = true;

    // Se true, AnalysisResult contiene anche i punteggi per frame e per
    // sotto-finestra: serve a tarare le soglie e a produrre grafici.
    bool includeScores = false;
};

// ---------------------------------------------------------------------------
//  Esito di un'analisi
// ---------------------------------------------------------------------------
struct AnalysisResult {
    std::vector<Detection> detections;

    // Quanti fiati ha scartato il modello addestrato. 0 = modello non usato.
    int filteredByModel = 0;

    double sampleRate = 48000.0;
    int64_t totalSamples = 0;

    DetectorSettings settings;

    // Stato del gate vocale. Ora e' sempre la salienza armonica: non c'e' piu'
    // un modello esterno da caricare.
    bool vadActive = false;
    DeviceStatus vadDevice;
    std::string vadStatus;

    std::string error;

    // Diagnostica.
    size_t framesAnalyzed = 0;
    size_t framesReused = 0;
    bool cancelled = false;
    double analysisSeconds = 0.0;
    float noiseFloorDb = -160.0f;
    float voiceFloorDb = -160.0f;

    // Punteggi per la taratura (popolati solo con includeScores).
    std::vector<float> breathScores;      // uno per frame
    std::vector<float> clickScores;       // uno per sotto-finestra
    std::vector<float> clickRatios;       // picco |d| / mediana |d|
    std::vector<float> clickNarrowness;   // 1 = stretto
    std::vector<float> voiceProbabilities;// uno per frame, -1 se assente
    // Feature per frame: servono a capire perche' un punteggio e' alto o basso
    // e a ritoccare le soglie con dati alla mano.
    std::vector<float> frameHarmonicity;  // salienza armonica
    std::vector<float> frameFlatness;     // flatness spettrale
    std::vector<float> frameLevelDb;      // livello del frame

    // Feature spettrali esportate per la diagnosi.
    //
    // Servono a distinguere un respiro da una sibilante. Sono lo stesso suono -
    // rumore a banda larga, non armonico - ma la "s" ha l'energia concentrata
    // in alto (4-8 kHz) mentre il respiro ce l'ha piu' in basso (0,5-4 kHz).
    // Misurabile con --dump-scores invece che a intuito.
    std::vector<float> frameCentroid;      // centroide spettrale, Hz
    std::vector<float> frameZcr;           // attraversamenti dello zero
    std::vector<float> frameLowBand;       // 80-500 Hz
    std::vector<float> frameMidBand;       // 500-2000 Hz
    std::vector<float> frameHighBand;      // 2000-10000 Hz
    int hopSize = 0;
    int clickSubWindow = 0;

    size_t countOf(DetectionKind kind) const;
};

} // namespace bc

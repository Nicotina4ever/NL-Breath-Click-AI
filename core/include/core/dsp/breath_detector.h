// SPDX-License-Identifier: MPL-2.0
// ============================================================================
//  breath_detector.h — Rilevamento respiri (puramente DSP)
//
//  Un respiro in una registrazione vocale e' rumore a banda larga:
//    * spettro piatto (nessuna struttura armonica), quindi alta
//      "spectral flatness" e bassa salienza armonica;
//    * energia distribuita sopra i 500 Hz, non solo sulle basse;
//    * inviluppo sostenuto (80 - 600 ms), quindi fattore di cresta basso:
//      NON e' un transiente come un click;
//    * livello sopra il rumore di fondo ma tipicamente sotto la voce piena.
//
//  Il discriminante piu' importante rispetto alle consonanti sibilanti
//  ("s", "sh", "f"), che hanno anch'esse spettro piatto, e' il CONTESTO: un
//  respiro sta in una pausa, una sibilante sta dentro il parlato. Per questo
//  la probabilita' di voce (Silero VAD) e' un ingresso esplicito del
//  rilevatore; se non e' disponibile si ripiega sulla salienza armonica.
//
//  Nessuna allocazione in process().
// ============================================================================
#pragma once

#include <cstddef>
#include <vector>

#include "core/dsp/frame_features.h"

namespace bc {

struct BreathDetectorConfig {
    float sampleRate = 48000.0f;
    float sensitivity = 0.5f;              // 0..1

    // Il livello deve superare il fondo di almeno questo margine.
    float minLevelAboveFloorDb = 5.0f;
    // Sotto questo livello assoluto si ignora tutto (silencio digitale).
    float absoluteSilenceDb = -74.0f;

    // Soglie di forma.
    // Flatness spettrale minima perche' il suono sia considerato rumore a banda
    // larga. La rampa va da questo valore a 3.5x. Misure di riferimento: un
    // respiro ha flatness 0.28-0.34, la voce 0.02-0.03.
    float minFlatness = 0.10f;
    float maxCrest = 9.0f;

    // ------------------------------------------------------------------
    //  LE TRE SOGLIE CHE DECIDONO
    //
    //  Sostituiscono il prodotto di curve che c'era prima. Quel prodotto
    //  conteneva una quindicina di costanti scelte a mano, e ne bastava una
    //  sbagliata per schiacciare il risultato: la rampa della flatness, tarata
    //  su respiri sintetici a 0.28-0.34, riduceva a 0.35 un respiro vero a
    //  0.19-0.21, e nessun'altra soglia poteva piu' recuperarlo.
    //
    //  Ognuna di queste e' una grandezza MISURABILE:
    //      bc_cli analyze take.wav --dump-scores punteggi.csv
    //  stampa flatness, armonicita e livello per ogni frame. Quindi si
    //  correggono guardando i numeri, non indovinando.
    //
    //  Valori misurati sul materiale reale dell'utente:
    //      respiro  : flatness 0.19-0.21, armonicita 0.29-0.45, -33/-42 dB
    //      voce     : flatness 0.02-0.03, armonicita 0.90-1.00, -16 dB
    //      silenzio : flatness 0.00,      armonicita 0.00,      -160 dB
    // ------------------------------------------------------------------

    // Rumore a banda larga: sopra questa flatness.
    //
    // 0.11 e non 0.15: sul materiale reale la flatness di un respiro sta a
    // 0.15-0.21, quindi una porta a 0.15 cadeva esattamente sul bordo e il
    // punteggio si spegneva e riaccendeva di frame in frame. Le sequenze
    // risultavano lunghe 11-16 ms contro un minimo di 90, e il respiro veniva
    // scartato per durata. Con la porta piu' in basso le sequenze diventano
    // continue.
    //
    // La voce sta a 0.02-0.03 e resta fuori da entrambi i valori, quindi
    // abbassare la porta non fa entrare la voce.
    float flatnessGate = 0.11f;
    // Assenza di voce: sotto questa armonicita (o probabilita' di voce).
    float harmonicityGate = 0.55f;

    // Centroide spettrale massimo, in Hz: e' il discriminante fra un respiro e
    // una sibilante.
    //
    // Sono lo stesso suono - rumore a banda larga, non armonico - ma la "s" ha
    // l'energia concentrata in alto, il respiro ce l'ha in mezzo. Misurato su
    // 340 respiri e 2148 consonanti sui take reali:
    //
    //     respiro    centroide mediano 4955 Hz
    //     consonante centroide mediano 7334 Hz
    //
    // Le due distribuzioni NON si sovrappongono. E' un discriminante che
    // funziona sul candidato da solo, senza guardare il contesto: quindi vale
    // anche dentro il parlato continuo, dove il confronto con i dintorni
    // falliva.
    //
    // Le altre feature che separano altrettanto bene: ZCR (0,13 contro 0,25) e
    // banda media 500-2000 Hz (0,28 contro 0,09). Il centroide e' il piu'
    // diretto da spiegare.
    float centroidGate = 6000.0f;
    // Udibile: sopra questo livello assoluto.
    float levelGateDb = -50.0f;

    // Finestra di inseguimento del fondo (frame a 256 campioni di hop).
    int floorWindowFrames = 256;
};

struct BreathScore {
    float score = 0.0f;               // 0..1, margine della condizione piu' debole
    float noiseLike = 0.0f;           // componente: spettro piatto
    float unvoiced = 0.0f;            // componente: assenza di voce
    float bandOk = 0.0f;              // componente: energia sopra 500 Hz
    float levelAboveFloorDb = 0.0f;
    bool detected = false;

    // Valori misurati, riportati tali e quali: servono a chi deve correggere
    // le tre soglie, e senza di essi si torna a indovinare.
    float measuredFlatness = 0.0f;
    float measuredCentroid = 0.0f;   // centroide spettrale, Hz
    float centroidMargin = 0.0f;     // margine della condizione sul centroide
    float measuredHarmonicity = 0.0f;
    float measuredLevelDb = 0.0f;
};

class BreathDetector {
public:
    explicit BreathDetector(const BreathDetectorConfig& config);
    ~BreathDetector();

    BreathDetector(const BreathDetector&) = delete;
    BreathDetector& operator=(const BreathDetector&) = delete;

    const BreathDetectorConfig& config() const { return config_; }
    void setSensitivity(float sensitivity);
    void setThresholdOffset(float offset);   // -1..+1, aggiunto allo score
    void setVoiceProbabilityOverride(float probability); // < 0 => usa armonicita'

    // voiceProbability: probabilita' di voce 0..1 dal VAD, oppure valore
    // negativo se non disponibile.
    BreathScore process(const FrameFeatures& features, float voiceProbability);

    void reset();

    float noiseFloorDb() const { return noiseFloorDb_; }

    // Soglia effettiva applicata allo score (per la GUI / report).
    float threshold() const;

private:
    BreathDetectorConfig config_;
    std::vector<float> floorHistory_;
    size_t floorCount_ = 0;
    size_t floorWrite_ = 0;
    float noiseFloorDb_ = -90.0f;
    float thresholdOffset_ = 0.0f;
};

} // namespace bc

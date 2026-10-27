// SPDX-License-Identifier: MPL-2.0
// ============================================================================
//  region_editor.h — Correzione audio sulle regioni
//
//  Due strategie distinte, perche' respiro e click sono fenomeni diversi:
//
//   * RESPIRO -> attenuazione con rampe di attacco/rilascio a coseno
//     rialzato. Una moltiplicazione netta produrrebbe un gradino udibile.
//
//   * CLICK -> ricostruzione locale lineare/Burg, con rinuncia automatica
//     quando il contesto non giustifica la previsione. Nessun cambio durata.
//
//  L'API principale lavora su RegionOperation, un tipo neutro: cosi' le stesse
//  funzioni servono sia i rilevamenti automatici sia le regioni modificate a
//  mano nell'editor, dove ogni regione ha la propria attenuazione.
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/detect/detection.h"
#include "core/dsp/click_repair.h"

namespace bc {

struct EditSettings {
    // Attenuazione predefinita dei respiri (dB, negativo).
    float breathReductionDb = -18.0f;

    // Attenuazione predefinita dei click quando non si ripara (dB, negativo).
    float clickReductionDb = -60.0f;

    // Riparazione per interpolazione dei click brevi.
    bool repairClicks = true;
    float repairMaxMs = 45.0f;

    // Rampe.
    float attackMs = 8.0f;
    float releaseMs = 80.0f;
    float prePadMs = 5.0f;
    float postPadMs = 15.0f;
};

// Una regione da correggere, con la propria attenuazione. E' il tipo su cui
// lavorano l'editor e il documento; i rilevamenti si convertono con
// regionOperationsFromDetections().
struct RegionOperation {
    DetectionKind kind = DetectionKind::Breath;
    int64_t startSample = 0;
    int64_t endSample = 0;

    // Attenuazione da applicare se la regione non viene riparata (dB, <= 0).
    float reductionDb = -18.0f;

    // Solo per i click: ripara per interpolazione invece di attenuare.
    // Ha effetto solo se la regione non supera EditSettings::repairMaxMs.
    bool repair = false;
    std::string repairMethod = "auto";

    int64_t lengthSamples() const { return endSample - startSample; }
};

struct ClickRepairRecord {
    RegionOperation operation;
    ClickRepairDiagnostics diagnostics;
    ClickRepairStatus status = ClickRepairStatus::Invalid;
};
struct EditReport {
    size_t breathRegions = 0;
    size_t clickRegionsRepaired = 0;
    size_t clickRegionsAttenuated = 0;
    size_t clickRegionsSkipped = 0;
    std::vector<RegionOperation> declinedClicks;
    std::vector<ClickRepairRecord> clickDecisions;
    size_t regionsDisabled = 0;
    float maxGainReductionDb = 0.0f;
    int64_t samplesModified = 0;
};

// Sample-exact amplitude envelope inside a half-open interval. Shared by
// Studio rendering and the source-timeline overlay; no padding outside it.
class RangeGainEnvelope {
public:
    RangeGainEnvelope(int64_t start, int64_t end, int64_t fadeFrames, float targetGain);
    float gainAt(int64_t frame) const;
private:
    int64_t start_, end_, fade_;
    float target_;
};

class RegionEditor {
public:
    // ---------------------------------------------------------------------
    //  API principale
    // ---------------------------------------------------------------------

    // Modifica il buffer interlacciato in place.
    static EditReport applyOperations(std::vector<float>& interleaved,
                                      int channels,
                                      double sampleRate,
                                      const std::vector<RegionOperation>& operations,
                                      const EditSettings& settings);

    // Costruisce la sola curva di guadagno (un valore per frame), senza
    // modificare l'audio. Utile per grafici e per il percorso realtime.
    static std::vector<float> buildGainCurve(int64_t frameCount,
                                             double sampleRate,
                                             const std::vector<RegionOperation>& operations,
                                             const EditSettings& settings);

    // ---------------------------------------------------------------------
    //  Compatibilita' con i rilevamenti automatici
    // ---------------------------------------------------------------------

    // Converte i rilevamenti in operazioni, applicando le impostazioni
    // predefinite (compreso EditSettings::repairClicks).
    static std::vector<RegionOperation> regionOperationsFromDetections(
        const std::vector<Detection>& detections,
        const EditSettings& settings);

    static EditReport apply(std::vector<float>& interleaved,
                            int channels,
                            double sampleRate,
                            const std::vector<Detection>& detections,
                            const EditSettings& settings);

    static std::vector<float> buildGainCurve(int64_t frameCount,
                                             double sampleRate,
                                             const std::vector<Detection>& detections,
                                             const EditSettings& settings);
};

} // namespace bc

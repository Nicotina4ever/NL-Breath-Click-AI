// SPDX-License-Identifier: MPL-2.0
// ============================================================================
//  vad_detector.h - Gate vocale
//
//  Il modello Silero VAD e' stato rimosso insieme a ONNX Runtime. Questo file
//  resta perche' i consumatori (CLI, plugin, estensione REAPER) lo includono e
//  lo interrogano, ma ora descrive un gate che non esiste piu'.
//
//  isOpen() restituisce sempre false, e i consumatori hanno gia' il percorso di
//  ripiego corretto: quando il gate non c'e', il BreathDetector usa la salienza
//  armonica, che calcola da se' per ogni frame. E' esattamente cio' che
//  succedeva prima quando il modello non era configurato, solo che adesso e'
//  l'unico comportamento possibile e non costa 13 MB di runtime.
// ============================================================================
#pragma once

#include <cstddef>
#include <string>

#include "core/inference/device_policy.h"

namespace bc {

struct VadResult {
    float probability = 0.0f;
    bool valid = false;
};

class VadDetector {
public:
    VadDetector() = default;
    ~VadDetector() = default;

    VadDetector(const VadDetector&) = delete;
    VadDetector& operator=(const VadDetector&) = delete;

    // Non c'e' alcun modello da aprire: si dichiara subito che il gate non e'
    // disponibile, cosi' i consumatori prendono il percorso di ripiego.
    bool open(const std::wstring&, DeviceRequest, bool, std::string* error) {
        if (error) {
            *error = "gate vocale non disponibile: si usa la salienza armonica";
        }
        return false;
    }

    void close() {}
    void reset() {}

    bool isOpen() const { return false; }

    DeviceStatus deviceStatus() const { return DeviceStatus{}; }

    VadResult process(const float*, size_t, int) { return VadResult{}; }

    // Dimensione di finestra che il modello avrebbe richiesto. Resta perche' il
    // plugin la usa per dimensionare un buffer; qualunque valore va bene ora.
    static size_t windowSizeFor(int) { return 512; }
};

} // namespace bc
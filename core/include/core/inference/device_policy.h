// SPDX-License-Identifier: MPL-2.0
// ============================================================================
//  device_policy.h - Tipi del dispositivo di calcolo
//
//  ONNX Runtime e' stato rimosso dal progetto: il rilevamento e' tutto DSP e
//  gira su CPU. Questo file resta per non toccare i consumatori, ma descrive
//  una realta' con un solo valore possibile.
//
//  Non c'e' piu' niente da scegliere e niente che possa non caricarsi.
// ============================================================================
#pragma once

#include <string>

namespace bc {

enum class DeviceKind {
    Cpu,
    Cuda        // non piu' raggiungibile: resta per non rompere i confronti
};

enum class DeviceRequest {
    Auto,
    Gpu,
    Cpu
};

// Qualunque valore si chieda, si ottiene CPU: e' l'unico dispositivo.
inline bool parseDeviceRequest(const std::string&, DeviceRequest& request) {
    request = DeviceRequest::Cpu;
    return true;
}

struct DeviceStatus {
    DeviceKind active = DeviceKind::Cpu;
    DeviceRequest requested = DeviceRequest::Cpu;
    bool fellBackToCpu = false;
    std::string detail = "Elaborazione DSP su CPU (nessun modello esterno).";

    bool usingGpu() const { return false; }
    std::string shortLabel() const { return "CPU"; }
};

} // namespace bc
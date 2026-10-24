// SPDX-License-Identifier: MPL-2.0
// ============================================================================
//  wav_io.h — Lettura e scrittura di file WAV
//
//  Supporta PCM 16/24/32 bit, float 32 bit e WAVE_FORMAT_EXTENSIBLE, da 1 a 8
//  canali. Nessuna dipendenza esterna (libsndfile e' stato rimosso dal
//  progetto: non era utilizzato e pesava 6.7 MB).
// ============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace bc {

struct AudioBuffer {
    std::vector<float> samples;   // interlacciato
    int channels = 0;
    int sampleRate = 0;

    int64_t frameCount() const {
        return channels > 0 ? static_cast<int64_t>(samples.size()) / channels : 0;
    }
    bool empty() const { return samples.empty() || channels <= 0; }
    double durationSeconds() const {
        return sampleRate > 0 ? static_cast<double>(frameCount()) / sampleRate : 0.0;
    }

    // Media dei canali, normalizzata sul numero di canali.
    std::vector<float> toMono() const;

    void clear() {
        samples.clear();
        channels = 0;
        sampleRate = 0;
    }
};

struct WavInfo {
    int channels = 0;
    int sampleRate = 0;
    int bitsPerSample = 0;
    bool isFloat = false;
    int64_t frames = 0;
};

// Formati supportati in scrittura.
enum class WavFormat {
    Pcm16,
    Pcm24,
    Float32
};

// Il percorso e' UTF-8 (su Windows viene convertito internamente).
bool readWav(const std::string& path, AudioBuffer& out, std::string* error = nullptr);
bool writeWav(const std::string& path,
              const AudioBuffer& buffer,
              WavFormat format,
              std::string* error = nullptr);

// Legge solo l'intestazione: utile per report e controlli.
bool probeWav(const std::string& path, WavInfo& info, std::string* error = nullptr);

} // namespace bc

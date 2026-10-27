// SPDX-License-Identifier: MPL-2.0
// ============================================================================
//  audio_player.h — Riproduzione audio per l'editor
//
//  Usa waveOut (winmm). E' la scelta giusta qui: l'editor deve far sentire
//  quello che si sta modificando, non fare monitoraggio a bassa latenza. waveOut
//  e' disponibile ovunque, non richiede COM ne' thread apartment, ed e' molto
//  piu' semplice di WASAPI. Con blocchi da 1024 campioni la latenza e' di circa
//  40 ms, impercettibile per questo uso.
//
//  Il buffer riprodotto e' quello RESO dal documento: si ascolta esattamente
//  cio' che verra' esportato.
// ============================================================================
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#endif

namespace bc {

class AudioPlayer {
public:
    AudioPlayer();
    ~AudioPlayer();

    AudioPlayer(const AudioPlayer&) = delete;
    AudioPlayer& operator=(const AudioPlayer&) = delete;

    // Apre l'uscita audio. Va richiamata quando cambia il sample rate.
    bool open(int sampleRate, int channels, std::string* error);
    void close();
    bool isOpen() const { return opened_; }

    // Sostituisce l'audio riproducibile. Ferma la riproduzione se in corso.
    // Il vettore resta di proprieta' di chi chiama e deve restare valido.
    void setAudio(std::shared_ptr<const std::vector<float>> samples, int64_t frames);

    void play(int64_t fromFrame, int64_t untilFrame = -1);
    void stop();

    bool isPlaying() const { return playing_.load(std::memory_order_relaxed); }
    int takePlaybackError() { return playbackError_.exchange(0); }

    // Frame attualmente udibile, stimato sottraendo i blocchi ancora in coda.
    int64_t position() const;

    // Volume di ascolto, non incide sull'esportazione.
    void setMonitorGain(float gain) { monitorGain_.store(gain, std::memory_order_relaxed); }
    float monitorGain() const { return monitorGain_.load(std::memory_order_relaxed); }
    float sampleAt(int64_t frame, int channel = 0) const {
        std::lock_guard<std::mutex> guard(audioMutex_);
        const int64_t index = frame * channels_ + channel;
        return samples_ && index >= 0 && index < static_cast<int64_t>(samples_->size()) ? (*samples_)[static_cast<size_t>(index)] : 0.f;
    }

    const std::string& lastError() const { return lastError_; }

private:
#ifdef _WIN32
    struct Buffer {
        WAVEHDR header = {};
        std::vector<int16_t> data;
        bool prepared = false;
    };

    void threadMain();
    void fillAndQueue(Buffer& buffer, uint64_t generation);
    void stopThread();
    void releaseBuffers();
#endif

    bool opened_ = false;
    int sampleRate_ = 48000;
    int channels_ = 2;

    // L'audio da riprodurre e' in float; waveOut vuole PCM 16 bit, quindi si
    // converte al volo riempiendo i blocchi.
    //
    // Il puntatore condiviso e' protetto da un mutex: il thread di riempimento
    // e quello dell'interfaccia lo leggono e lo sostituiscono, e una copia non
    // sincronizzata di shared_ptr e' una corsa critica, non un dettaglio.
    mutable std::mutex audioMutex_;
    std::shared_ptr<const std::vector<float>> samples_;
    int64_t frames_ = 0;

    std::atomic<bool> playing_{false};
    std::atomic<int64_t> nextFrame_{0};      // prossimo frame da scrivere
    std::atomic<int64_t> startFrame_{0};
    std::atomic<int64_t> endFrame_{0};
    std::atomic<uint64_t> transportGeneration_{0};
    std::atomic<int> playbackError_{0};
    std::mutex transportMutex_;
    std::atomic<float> monitorGain_{1.0f};

    std::string lastError_;

#ifdef _WIN32
    HWAVEOUT device_ = nullptr;
    HANDLE finishedEvent_ = nullptr;
    std::thread thread_;
    std::atomic<bool> running_{false};

    // I blocchi in volo sono letti e modificati sia dal thread di riempimento
    // sia da quello dell'interfaccia (play/stop). Non e' un percorso realtime —
    // l'evento di waveOut arriva da un thread del driver e non c'e' callback —
    // quindi un mutex e' la soluzione corretta e costa nulla.
    mutable std::mutex inFlightMutex_;
    std::vector<Buffer*> inFlight_;

    static constexpr int kBufferCount = 4;
    int bufferFrames_ = 1024;
    std::vector<std::unique_ptr<Buffer>> buffers_;
#endif
};

} // namespace bc

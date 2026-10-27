// SPDX-License-Identifier: MPL-2.0
#include "audio_player.h"

#include <algorithm>
#include <cmath>

namespace bc {

AudioPlayer::AudioPlayer() = default;

AudioPlayer::~AudioPlayer() {
    close();
}

bool AudioPlayer::open(int sampleRate, int channels, std::string* error) {
    if (sampleRate <= 0 || channels <= 0) {
        if (error) *error = "Parametri audio non validi.";
        return false;
    }

    if (opened_ && sampleRate == sampleRate_ && channels == channels_) {
        return true;   // gia' aperto con lo stesso formato
    }

    close();

#ifdef _WIN32
    sampleRate_ = sampleRate;
    channels_ = channels;

    WAVEFORMATEX format = {};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = static_cast<WORD>(channels);
    format.nSamplesPerSec = static_cast<DWORD>(sampleRate);
    format.wBitsPerSample = 16;
    format.nBlockAlign = static_cast<WORD>(channels * 2);
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
    format.cbSize = 0;

    // L'evento viene segnalato quando un blocco termina: il thread di
    // riempimento si sveglia solo quando serve, senza attesa attiva.
    finishedEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!finishedEvent_) {
        lastError_ = "Impossibile creare l'evento di sincronizzazione.";
        if (error) *error = lastError_;
        return false;
    }

    const MMRESULT result = waveOutOpen(&device_,
                                        WAVE_MAPPER,
                                        &format,
                                        reinterpret_cast<DWORD_PTR>(finishedEvent_),
                                        0,
                                        CALLBACK_EVENT);
    if (result != MMSYSERR_NOERROR || !device_) {
        device_ = nullptr;
        CloseHandle(finishedEvent_);
        finishedEvent_ = nullptr;

        lastError_ = "Nessuna uscita audio disponibile (codice " +
                     std::to_string(static_cast<int>(result)) + ").";
        if (error) *error = lastError_;
        return false;
    }

    bufferFrames_ = std::max(256, sampleRate / 48);   // ~21 ms per blocco

    buffers_.clear();
    for (int i = 0; i < kBufferCount; ++i) {
        auto buffer = std::make_unique<Buffer>();
        buffer->data.assign(static_cast<size_t>(bufferFrames_) * channels, 0);

        buffer->header.lpData = reinterpret_cast<LPSTR>(buffer->data.data());
        buffer->header.dwBufferLength = static_cast<DWORD>(buffer->data.size() * sizeof(int16_t));

        if (waveOutPrepareHeader(device_, &buffer->header, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
            lastError_ = "Preparazione dei blocchi audio fallita.";
            if (error) *error = lastError_;
            releaseBuffers();
            waveOutClose(device_);
            device_ = nullptr;
            CloseHandle(finishedEvent_);
            finishedEvent_ = nullptr;
            return false;
        }
        buffer->prepared = true;
        buffers_.push_back(std::move(buffer));
    }

    inFlight_.clear();
    running_.store(true, std::memory_order_release);
    thread_ = std::thread(&AudioPlayer::threadMain, this);
    opened_ = true;
    lastError_.clear();
    return true;
#else
    (void)sampleRate;
    (void)channels;
    if (error) *error = "Riproduzione non disponibile su questa piattaforma.";
    return false;
#endif
}

void AudioPlayer::close() {
#ifdef _WIN32
    stop();
    stopThread();

    if (device_) {
        releaseBuffers();
        waveOutClose(device_);
        device_ = nullptr;
    }
    if (finishedEvent_) {
        CloseHandle(finishedEvent_);
        finishedEvent_ = nullptr;
    }
    buffers_.clear();
    {
        // Il thread e' gia' stato chiuso, ma si mantiene la stessa disciplina
        // di accesso per non lasciare percorsi non protetti.
        std::lock_guard<std::mutex> lock(inFlightMutex_);
        inFlight_.clear();
    }
#endif
    opened_ = false;
}

#ifdef _WIN32
void AudioPlayer::releaseBuffers() {
    if (!device_) return;
    waveOutReset(device_);
    for (auto& buffer : buffers_) {
        if (buffer->prepared) {
            waveOutUnprepareHeader(device_, &buffer->header, sizeof(WAVEHDR));
            buffer->prepared = false;
        }
    }
    std::lock_guard<std::mutex> lock(inFlightMutex_);
    inFlight_.clear();
}

void AudioPlayer::stopThread() {
    if (running_.exchange(false, std::memory_order_acq_rel)) {
        if (finishedEvent_) {
            SetEvent(finishedEvent_);   // sveglia subito il thread
        }
        if (thread_.joinable()) {
            thread_.join();
        }
    }
    if (thread_.joinable()) {
        thread_.join();
    }
}

void AudioPlayer::fillAndQueue(Buffer& buffer, uint64_t generation) {
    std::lock_guard<std::mutex> transport(transportMutex_);
    if (!playing_.load(std::memory_order_relaxed) || generation != transportGeneration_.load()) return;
    // La copia del puntatore condiviso avviene sotto lock: cosi' il buffer
    // resta vivo per tutta la durata della copia anche se l'interfaccia lo
    // sostituisce nel frattempo.
    std::shared_ptr<const std::vector<float>> samples;
    int64_t frames = 0;
    {
        std::lock_guard<std::mutex> lock(audioMutex_);
        samples = samples_;
        frames = frames_;
    }

    const int64_t start = nextFrame_.load(std::memory_order_relaxed);
    const int64_t available = std::max<int64_t>(0, std::min(frames, endFrame_.load()) - start);
    const int64_t toCopy = std::min<int64_t>(bufferFrames_, available);
    if (toCopy <= 0 || !samples || samples->empty()) return;

    int16_t* output = buffer.data.data();
    const size_t total = buffer.data.size();

    if (toCopy <= 0 || !samples || samples->empty()) {
        std::fill(buffer.data.begin(), buffer.data.end(), static_cast<int16_t>(0));
    } else {
        const int channels = std::max(1, channels_);
        const std::vector<float>& source = *samples;
        const float gain = monitorGain_.load(std::memory_order_relaxed);

        for (int64_t frame = 0; frame < toCopy; ++frame) {
            for (int channel = 0; channel < channels; ++channel) {
                const size_t index = static_cast<size_t>((start + frame) * channels + channel);
                float value = index < source.size() ? source[index] * gain : 0.0f;
                value = std::max(-1.0f, std::min(1.0f, value));
                output[static_cast<size_t>(frame) * channels + channel] =
                    static_cast<int16_t>(std::lround(value * 32767.0f));
            }
        }

        // L'ultimo blocco parziale va azzerato nel resto, altrimenti resta in
        // coda l'audio del giro precedente.
        const size_t written = static_cast<size_t>(toCopy) * channels;
        if (written < total) {
            std::fill(buffer.data.begin() + static_cast<std::ptrdiff_t>(written),
                      buffer.data.end(), static_cast<int16_t>(0));
        }
    }

    nextFrame_.store(start + toCopy, std::memory_order_relaxed);

    buffer.header.dwFlags &= ~WHDR_DONE;
    {
        std::lock_guard<std::mutex> lock(inFlightMutex_);
        inFlight_.push_back(&buffer);
    }
    const auto written = waveOutWrite(device_, &buffer.header, sizeof(WAVEHDR));
    if (written != MMSYSERR_NOERROR) {
        playbackError_.store(static_cast<int>(written));
        playing_.store(false);
        waveOutReset(device_);
        std::lock_guard<std::mutex> lock(inFlightMutex_);
        inFlight_.clear();
    }
}

void AudioPlayer::threadMain() {
    while (running_.load(std::memory_order_acquire)) {
        WaitForSingleObject(finishedEvent_, 20);

        std::vector<Buffer*> toRefill;
        uint64_t generation;
        {
            std::lock_guard<std::mutex> transport(transportMutex_);
            generation = transportGeneration_.load();
            std::lock_guard<std::mutex> lock(inFlightMutex_);

            // Un blocco alla volta: l'evento e' a conteggio singolo e segnala il
            // completamento del piu' vecchio.
            while (!inFlight_.empty() &&
                   (inFlight_.front()->header.dwFlags & WHDR_DONE) != 0) {
                Buffer* buffer = inFlight_.front();
                inFlight_.erase(inFlight_.begin());

                if (playing_.load(std::memory_order_relaxed) &&
                    nextFrame_.load(std::memory_order_relaxed) < endFrame_.load()) {
                    toRefill.push_back(buffer);
                }
            }

            // Fine naturale: tutti i blocchi sono stati consumati.
            if (playing_.load(std::memory_order_relaxed) && inFlight_.empty() &&
                nextFrame_.load(std::memory_order_relaxed) >= endFrame_.load()) {
                startFrame_.store(endFrame_.load());
                playing_.store(false, std::memory_order_relaxed);
            }
        }

        // Requeue only AFTER filling, with the same transport generation.
        // Re-adding a DONE header in the loop above can spin forever when all
        // buffers complete together, before nextFrame has advanced.
        for (Buffer* buffer : toRefill) {
            fillAndQueue(*buffer, generation);
        }
    }
}
#endif

void AudioPlayer::setAudio(std::shared_ptr<const std::vector<float>> samples, int64_t frames) {
    stop();
    {
        std::lock_guard<std::mutex> lock(audioMutex_);
        samples_ = std::move(samples);
        frames_ = samples_ ? frames : 0;
    }
}

void AudioPlayer::play(int64_t fromFrame, int64_t untilFrame) {
#ifdef _WIN32
    if (!opened_) return;

    int64_t frames = 0;
    {
        std::lock_guard<std::mutex> lock(audioMutex_);
        frames = frames_;
        if (!samples_ || frames <= 0) return;
    }

    stop();

    const int64_t start = std::max<int64_t>(0, std::min(fromFrame, frames));
    playbackError_.store(0);
    const int64_t end = untilFrame < 0 ? frames : std::clamp(untilFrame, int64_t(0), frames);
    if (end <= start) return;
    const auto generation = transportGeneration_.load();
    startFrame_.store(start, std::memory_order_relaxed);
    nextFrame_.store(start, std::memory_order_relaxed);
    endFrame_.store(end, std::memory_order_relaxed);

    playing_.store(true, std::memory_order_relaxed);

    // Riempimento iniziale: si accodano tutti i blocchi disponibili. Il
    // riempimento avviene fuori dal lock, l'elenco si aggiorna dentro.
    std::vector<Buffer*> primed;
    {
        std::lock_guard<std::mutex> lock(inFlightMutex_);
        for (auto& buffer : buffers_) {
            if (nextFrame_.load(std::memory_order_relaxed) >= end) break;
            primed.push_back(buffer.get());
        }
    }

    for (Buffer* buffer : primed) {
        fillAndQueue(*buffer, generation);
    }

    if (primed.empty()) {
        playing_.store(false, std::memory_order_relaxed);
    }
#else
    (void)fromFrame;
    (void)untilFrame;
#endif
}

void AudioPlayer::stop() {
#ifdef _WIN32
    if (!opened_) return;

    std::lock_guard<std::mutex> transport(transportMutex_);
    transportGeneration_.fetch_add(1);
    playing_.store(false, std::memory_order_relaxed);

    if (device_) {
        // waveOutReset segnala l'evento e marca tutti i blocchi come conclusi.
        waveOutReset(device_);
    }

    std::lock_guard<std::mutex> lock(inFlightMutex_);
    inFlight_.clear();
    nextFrame_.store(startFrame_.load(std::memory_order_relaxed), std::memory_order_relaxed);
#endif
}

int64_t AudioPlayer::position() const {
#ifdef _WIN32
    if (!playing_.load(std::memory_order_relaxed)) {
        return startFrame_.load(std::memory_order_relaxed);
    }

    // La posizione scritta e' avanti rispetto a quella udibile di tutti i
    // blocchi ancora in coda: si sottraggono per far combaciare la testina con
    // quello che si sente.
    const int64_t written = nextFrame_.load(std::memory_order_relaxed);
    int64_t queued = 0;
    {
        std::lock_guard<std::mutex> lock(inFlightMutex_);
        queued = static_cast<int64_t>(inFlight_.size()) * bufferFrames_;
    }
    const int64_t audible = written - queued;
    return std::max(startFrame_.load(std::memory_order_relaxed), audible);
#else
    return 0;
#endif
}

} // namespace bc

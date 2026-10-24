// SPDX-License-Identifier: MPL-2.0
#include "core/edit/wav_io.h"
#include "core/util/atomic_file.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace bc {

namespace {

constexpr uint16_t kFormatPcm = 1;
constexpr uint16_t kFormatFloat = 3;
constexpr uint16_t kFormatExtensible = 0xFFFE;

std::FILE* openFileUtf8(const std::string& path, const char* mode) {
#ifdef _WIN32
    if (path.empty()) return nullptr;
    const int needed = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (needed <= 1) return nullptr;
    std::wstring wide(static_cast<size_t>(needed - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wide.data(), needed);
    std::wstring wideMode(mode, mode + std::strlen(mode));
    return _wfopen(nativeFilePath(std::filesystem::path(wide)).c_str(), wideMode.c_str());
#else
    return std::fopen(path.c_str(), mode);
#endif
}

uint32_t readU32(const unsigned char* data) {
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[3]) << 24);
}

uint16_t readU16(const unsigned char* data) {
    return static_cast<uint16_t>(static_cast<uint16_t>(data[0]) |
                                 (static_cast<uint16_t>(data[1]) << 8));
}

void writeU32(std::FILE* file, uint32_t value) {
    unsigned char buffer[4] = {
        static_cast<unsigned char>(value & 0xFF),
        static_cast<unsigned char>((value >> 8) & 0xFF),
        static_cast<unsigned char>((value >> 16) & 0xFF),
        static_cast<unsigned char>((value >> 24) & 0xFF)};
    std::fwrite(buffer, 1, 4, file);
}

void writeU16(std::FILE* file, uint16_t value) {
    unsigned char buffer[2] = {
        static_cast<unsigned char>(value & 0xFF),
        static_cast<unsigned char>((value >> 8) & 0xFF)};
    std::fwrite(buffer, 1, 2, file);
}

float decodeSample(const unsigned char* data, int bitsPerSample, bool isFloat) {
    if (isFloat) {
        float value = 0.0f;
        std::memcpy(&value, data, sizeof(float));
        return value;
    }
    switch (bitsPerSample) {
        case 16: {
            const int16_t raw = static_cast<int16_t>(readU16(data));
            return static_cast<float>(raw) / 32768.0f;
        }
        case 24: {
            int32_t raw = static_cast<int32_t>(data[0]) |
                          (static_cast<int32_t>(data[1]) << 8) |
                          (static_cast<int32_t>(data[2]) << 16);
            if (raw & 0x00800000) raw |= static_cast<int32_t>(0xFF000000);
            return static_cast<float>(raw) / 8388608.0f;
        }
        case 32: {
            const int32_t raw = static_cast<int32_t>(readU32(data));
            return static_cast<float>(static_cast<double>(raw) / 2147483648.0);
        }
        default:
            return 0.0f;
    }
}

void encodeSample(unsigned char* out, float value, int bitsPerSample) {
    value = std::max(-1.0f, std::min(1.0f, value));
    switch (bitsPerSample) {
        case 16: {
            const int32_t scaled = static_cast<int32_t>(std::lround(value * 32767.0f));
            const uint16_t raw = static_cast<uint16_t>(std::max(-32768, std::min(32767, scaled)));
            out[0] = static_cast<unsigned char>(raw & 0xFF);
            out[1] = static_cast<unsigned char>((raw >> 8) & 0xFF);
            break;
        }
        case 24: {
            const int32_t scaled = static_cast<int32_t>(std::lround(value * 8388607.0f));
            const int32_t clamped = std::max(-8388608, std::min(8388607, scaled));
            out[0] = static_cast<unsigned char>(clamped & 0xFF);
            out[1] = static_cast<unsigned char>((clamped >> 8) & 0xFF);
            out[2] = static_cast<unsigned char>((clamped >> 16) & 0xFF);
            break;
        }
        case 32: {
            float copy = value;
            std::memcpy(out, &copy, sizeof(float));
            break;
        }
        default:
            break;
    }
}

bool skipChunk(std::FILE* file, uint32_t size) {
    const long aligned = static_cast<long>(size + (size & 1u));
    return std::fseek(file, aligned, SEEK_CUR) == 0;
}

} // namespace

std::vector<float> AudioBuffer::toMono() const {
    if (channels <= 1) {
        return samples;
    }
    const int64_t frames = frameCount();
    std::vector<float> mono(static_cast<size_t>(std::max<int64_t>(0, frames)), 0.0f);
    const float scale = 1.0f / static_cast<float>(channels);
    for (int64_t f = 0; f < frames; ++f) {
        float sum = 0.0f;
        for (int c = 0; c < channels; ++c) {
            sum += samples[static_cast<size_t>(f * channels + c)];
        }
        mono[static_cast<size_t>(f)] = sum * scale;
    }
    return mono;
}

bool probeWav(const std::string& path, WavInfo& info, std::string* error) {
    std::FILE* file = openFileUtf8(path, "rb");
    if (!file) {
        if (error) *error = "Impossibile aprire il file: " + path;
        return false;
    }

    bool ok = false;
    char riff[4] = {};
    char wave[4] = {};
    unsigned char riffSize[4] = {};
    // Layout RIFF: "RIFF" | dimensione (4 byte) | "WAVE".
    if (std::fread(riff, 1, 4, file) == 4 &&
        std::fread(riffSize, 1, 4, file) == 4 &&
        std::fread(wave, 1, 4, file) == 4 &&
        std::memcmp(riff, "RIFF", 4) == 0 && std::memcmp(wave, "WAVE", 4) == 0) {
        unsigned char header[8];
        while (std::fread(header, 1, 8, file) == 8) {
            const uint32_t chunkSize = readU32(header + 4);
            if (std::memcmp(header, "fmt ", 4) == 0) {
                std::vector<unsigned char> fmt(std::max<uint32_t>(16, std::min<uint32_t>(chunkSize, 64)));
                const size_t read = std::fread(fmt.data(), 1, fmt.size(), file);
                if (read >= 16) {
                    uint16_t formatTag = readU16(fmt.data());
                    info.channels = readU16(fmt.data() + 2);
                    info.sampleRate = static_cast<int>(readU32(fmt.data() + 4));
                    info.bitsPerSample = readU16(fmt.data() + 14);
                    if (formatTag == kFormatExtensible && read >= 26) {
                        formatTag = readU16(fmt.data() + 24);
                    }
                    info.isFloat = (formatTag == kFormatFloat);
                    ok = true;
                }
                if (chunkSize > fmt.size()) {
                    if (!skipChunk(file, chunkSize - static_cast<uint32_t>(fmt.size()))) break;
                } else if ((chunkSize & 1u) != 0u) {
                    std::fseek(file, 1, SEEK_CUR);
                }
            } else if (std::memcmp(header, "data", 4) == 0) {
                const int bytesPerSample = info.bitsPerSample / 8;
                const int frameBytes = bytesPerSample * std::max(1, info.channels);
                info.frames = frameBytes > 0 ? static_cast<int64_t>(chunkSize) / frameBytes : 0;
                std::fclose(file);
                if (error) error->clear();
                return ok;
            } else if (!skipChunk(file, chunkSize)) {
                break;
            }
        }
    }

    std::fclose(file);
    if (error) *error = ok ? "Chunk 'data' assente" : "File WAV non valido";
    return false;
}

bool readWav(const std::string& path, AudioBuffer& out, std::string* error) {
    out.clear();

    std::FILE* file = openFileUtf8(path, "rb");
    if (!file) {
        if (error) *error = "Impossibile aprire il file: " + path;
        return false;
    }

    char riff[4] = {};
    char wave[4] = {};
    unsigned char riffSize[4] = {};
    // Layout RIFF: "RIFF" | dimensione (4 byte) | "WAVE".
    if (std::fread(riff, 1, 4, file) != 4 ||
        std::fread(riffSize, 1, 4, file) != 4 ||
        std::fread(wave, 1, 4, file) != 4 ||
        std::memcmp(riff, "RIFF", 4) != 0 || std::memcmp(wave, "WAVE", 4) != 0) {
        std::fclose(file);
        if (error) *error = "Non e' un file RIFF/WAVE";
        return false;
    }

    int channels = 0;
    int sampleRate = 0;
    int bitsPerSample = 0;
    bool isFloat = false;
    bool haveFormat = false;

    unsigned char header[8];
    while (std::fread(header, 1, 8, file) == 8) {
        const uint32_t chunkSize = readU32(header + 4);

        if (std::memcmp(header, "fmt ", 4) == 0) {
            std::vector<unsigned char> fmt(std::max<uint32_t>(16, std::min<uint32_t>(chunkSize, 64)));
            const size_t read = std::fread(fmt.data(), 1, fmt.size(), file);
            if (read < 16) break;
            uint16_t formatTag = readU16(fmt.data());
            channels = readU16(fmt.data() + 2);
            sampleRate = static_cast<int>(readU32(fmt.data() + 4));
            bitsPerSample = readU16(fmt.data() + 14);
            if (formatTag == kFormatExtensible && read >= 26) {
                formatTag = readU16(fmt.data() + 24);
            }
            isFloat = (formatTag == kFormatFloat);
            haveFormat = true;
            if (chunkSize > fmt.size()) {
                if (!skipChunk(file, chunkSize - static_cast<uint32_t>(fmt.size()))) break;
            } else if ((chunkSize & 1u) != 0u) {
                std::fseek(file, 1, SEEK_CUR);
            }
            continue;
        }

        if (std::memcmp(header, "data", 4) == 0) {
            if (!haveFormat || channels <= 0 || bitsPerSample <= 0) {
                std::fclose(file);
                if (error) *error = "Chunk 'data' prima di 'fmt '";
                return false;
            }

            const int bytesPerSample = bitsPerSample / 8;
            if (bytesPerSample <= 0 || bytesPerSample > 4) {
                std::fclose(file);
                if (error) *error =
                    "Profondita' non supportata: " + std::to_string(bitsPerSample) + " bit";
                return false;
            }
            if (!isFloat && bitsPerSample != 16 && bitsPerSample != 24 && bitsPerSample != 32) {
                std::fclose(file);
                if (error) *error =
                    "PCM non supportato: " + std::to_string(bitsPerSample) + " bit";
                return false;
            }

            const int64_t frameBytes = static_cast<int64_t>(bytesPerSample) * channels;
            const int64_t frames = chunkSize / frameBytes;
            if (frames <= 0) {
                std::fclose(file);
                if (error) *error = "Nessun campione nel file";
                return false;
            }

            out.channels = channels;
            out.sampleRate = sampleRate;
            out.samples.resize(static_cast<size_t>(frames * channels));

            std::vector<unsigned char> block(static_cast<size_t>(frameBytes * 4096));
            int64_t written = 0;

            while (written < frames) {
                const int64_t remainingFrames = frames - written;
                const int64_t thisFrames = std::min<int64_t>(4096, remainingFrames);
                const size_t bytes =
                    std::fread(block.data(), 1, static_cast<size_t>(thisFrames * frameBytes), file);
                if (bytes < static_cast<size_t>(thisFrames * frameBytes)) {
                    // File troncato: si tengono i campioni letti.
                    const int64_t decoded = static_cast<int64_t>(bytes) / frameBytes;
                    for (int64_t f = 0; f < decoded; ++f) {
                        for (int c = 0; c < channels; ++c) {
                            const unsigned char* source =
                                block.data() + (f * channels + c) * bytesPerSample;
                            out.samples[static_cast<size_t>((written + f) * channels + c)] =
                                decodeSample(source, bitsPerSample, isFloat);
                        }
                    }
                    out.samples.resize(static_cast<size_t>((written + decoded) * channels));
                    std::fclose(file);
                    if (error) *error = "File WAV troncato: letti " +
                                        std::to_string(written + decoded) + " frame";
                    return !out.samples.empty();
                }

                for (int64_t f = 0; f < thisFrames; ++f) {
                    for (int c = 0; c < channels; ++c) {
                        const unsigned char* source =
                            block.data() + (f * channels + c) * bytesPerSample;
                        out.samples[static_cast<size_t>((written + f) * channels + c)] =
                            decodeSample(source, bitsPerSample, isFloat);
                    }
                }
                written += thisFrames;
            }

            std::fclose(file);
            if (error) error->clear();
            return true;
        }

        if (!skipChunk(file, chunkSize)) break;
    }

    std::fclose(file);
    if (error) *error = "Chunk 'data' non trovato";
    return false;
}

bool writeWav(const std::string& path,
              const AudioBuffer& buffer,
              WavFormat format,
              std::string* error) {
    if (buffer.empty()) {
        if (error) *error = "Buffer audio vuoto";
        return false;
    }
    if (buffer.sampleRate <= 0) {
        if (error) *error = "Sample rate non valido";
        return false;
    }

    std::FILE* file = openFileUtf8(path, "wb");
    if (!file) {
        if (error) *error = "Impossibile creare il file: " + path;
        return false;
    }

    const int bitsPerSample = format == WavFormat::Pcm16 ? 16 : (format == WavFormat::Pcm24 ? 24 : 32);
    const uint16_t formatTag = format == WavFormat::Float32 ? kFormatFloat : kFormatPcm;
    const int bytesPerSample = bitsPerSample / 8;
    const int channels = buffer.channels;
    const int64_t dataBytes = static_cast<int64_t>(buffer.samples.size()) * bytesPerSample;
    const uint16_t blockAlign = static_cast<uint16_t>(bytesPerSample * channels);
    const uint32_t byteRate = static_cast<uint32_t>(buffer.sampleRate) * blockAlign;

    std::fwrite("RIFF", 1, 4, file);
    writeU32(file, static_cast<uint32_t>(36 + dataBytes));
    std::fwrite("WAVE", 1, 4, file);

    std::fwrite("fmt ", 1, 4, file);
    writeU32(file, 16);
    writeU16(file, formatTag);
    writeU16(file, static_cast<uint16_t>(channels));
    writeU32(file, static_cast<uint32_t>(buffer.sampleRate));
    writeU32(file, byteRate);
    writeU16(file, blockAlign);
    writeU16(file, static_cast<uint16_t>(bitsPerSample));

    std::fwrite("data", 1, 4, file);
    writeU32(file, static_cast<uint32_t>(dataBytes));

    std::vector<unsigned char> block(static_cast<size_t>(bytesPerSample) * 8192);
    size_t offset = 0;
    while (offset < buffer.samples.size()) {
        const size_t thisCount = std::min<size_t>(8192, buffer.samples.size() - offset);
        for (size_t i = 0; i < thisCount; ++i) {
            encodeSample(block.data() + i * bytesPerSample, buffer.samples[offset + i], bitsPerSample);
        }
        if (std::fwrite(block.data(), bytesPerSample, thisCount, file) != thisCount) {
            std::fclose(file);
            if (error) *error = "Scrittura fallita (disco pieno?)";
            return false;
        }
        offset += thisCount;
    }

    std::fclose(file);
    if (error) error->clear();
    return true;
}

} // namespace bc

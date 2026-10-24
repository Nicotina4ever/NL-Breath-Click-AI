// SPDX-License-Identifier: MPL-2.0
#include "core/edit/audio_io.h"
#include "core/util/atomic_file.h"
#include "core/util/runtime_paths.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#pragma warning(push, 0)
#define DR_MP3_IMPLEMENTATION
#define DR_FLAC_IMPLEMENTATION
#include "dr_mp3.h"
#include "dr_flac.h"
#pragma warning(pop)

#include "lame.h"

namespace bc {
namespace {

struct LameApi {
    LibraryHandle module = nullptr;
    decltype(&lame_init) init = nullptr;
    decltype(&lame_close) close = nullptr;
    decltype(&lame_set_num_samples) setNumSamples = nullptr;
    decltype(&lame_set_in_samplerate) setInSampleRate = nullptr;
    decltype(&lame_set_out_samplerate) setOutSampleRate = nullptr;
    decltype(&lame_set_num_channels) setNumChannels = nullptr;
    decltype(&lame_set_mode) setMode = nullptr;
    decltype(&lame_set_quality) setQuality = nullptr;
    decltype(&lame_set_bWriteVbrTag) setWriteVbrTag = nullptr;
    decltype(&lame_set_write_id3tag_automatic) setWriteId3TagAutomatic = nullptr;
    decltype(&lame_set_VBR) setVbr = nullptr;
    decltype(&lame_set_VBR_q) setVbrQuality = nullptr;
    decltype(&lame_set_brate) setBitrate = nullptr;
    decltype(&lame_init_params) initParams = nullptr;
    decltype(&lame_encode_buffer_ieee_float) encodeFloat = nullptr;
    decltype(&lame_encode_buffer_interleaved_ieee_float) encodeInterleavedFloat = nullptr;
    decltype(&lame_encode_flush) encodeFlush = nullptr;
    decltype(&lame_get_lametag_frame) getLameTagFrame = nullptr;
    bool attempted = false;
    std::string loadError;

    template <typename T>
    bool bind(T& target, const char* name) {
        target = reinterpret_cast<T>(librarySymbol(module, name));
        if (!target) loadError = std::string("simbolo LAME mancante: ") + name;
        return target != nullptr;
    }

    bool load() {
        if (attempted) return module != nullptr;
        attempted = true;
        const auto local=runtimeDirectory()/libraryFileName("libmp3lame");
        module=openSharedLibrary(local);

        if (!module) {
            loadError = "encoder LAME non trovato nella directory runtime selezionata";
            return false;
        }
        bool ok = bind(init, "lame_init") &&
                  bind(close, "lame_close") &&
                  bind(setNumSamples, "lame_set_num_samples") &&
                  bind(setInSampleRate, "lame_set_in_samplerate") &&
                  bind(setOutSampleRate, "lame_set_out_samplerate") &&
                  bind(setNumChannels, "lame_set_num_channels") &&
                  bind(setMode, "lame_set_mode") &&
                  bind(setQuality, "lame_set_quality") &&
                  bind(setWriteVbrTag, "lame_set_bWriteVbrTag") &&
                  bind(setWriteId3TagAutomatic, "lame_set_write_id3tag_automatic") &&
                  bind(setVbr, "lame_set_VBR") &&
                  bind(setVbrQuality, "lame_set_VBR_q") &&
                  bind(setBitrate, "lame_set_brate") &&
                  bind(initParams, "lame_init_params") &&
                  bind(encodeFloat, "lame_encode_buffer_ieee_float") &&
                  bind(encodeInterleavedFloat, "lame_encode_buffer_interleaved_ieee_float") &&
                  bind(encodeFlush, "lame_encode_flush") &&
                  bind(getLameTagFrame, "lame_get_lametag_frame");
        if (!ok) {
            closeSharedLibrary(module);
            module = nullptr;
        }
        return ok;
    }
};

LameApi& lameApi() {
    static LameApi api;
    static const bool loaded=api.load();
    (void)loaded;
    return api;
}

std::string lameLoadError() {
    return lameApi().loadError.empty()
        ? "encoder MP3 non disponibile"
        : lameApi().loadError;
}

constexpr uint32_t kMaxId3Read = 4u * 1024u * 1024u;

std::wstring toWidePath(const std::string& path) {
#ifdef _WIN32
    if (path.empty()) return {};
    const int needed = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (needed <= 1) return {};
    std::wstring out(static_cast<size_t>(needed - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, out.data(), needed);
    return nativeFilePath(std::filesystem::path(out)).wstring();
#else
    return std::wstring(path.begin(), path.end());
#endif
}

std::FILE* openUtf8(const std::string& path, const char* mode) {
#ifdef _WIN32
    const std::wstring wide = toWidePath(path);
    if (wide.empty()) return nullptr;
    std::wstring wideMode(mode, mode + std::strlen(mode));
    return _wfopen(wide.c_str(), wideMode.c_str());
#else
    return std::fopen(path.c_str(), mode);
#endif
}

std::string lowerExtension(const std::string& path) {
    const std::string::size_type dot = path.find_last_of('.');
    if (dot == std::string::npos) return {};
    std::string ext = path.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

bool readPrefix(const std::string& path, std::vector<unsigned char>& bytes,
                size_t maxBytes) {
    std::FILE* file = openUtf8(path, "rb");
    if (!file) return false;
    bytes.clear();
    unsigned char chunk[8192];
    size_t total = 0;
    while (total < maxBytes) {
        const size_t want = std::min<size_t>(sizeof(chunk), maxBytes - total);
        const size_t got = std::fread(chunk, 1, want, file);
        if (got == 0) break;
        bytes.insert(bytes.end(), chunk, chunk + got);
        total += got;
    }
    std::fclose(file);
    return true;
}

bool readTail(const std::string& path, std::vector<unsigned char>& bytes,
              size_t count) {
    std::FILE* file = openUtf8(path, "rb");
    if (!file) return false;
    if (std::fseek(file, 0, SEEK_END) != 0) {
        std::fclose(file);
        return false;
    }
    const long size = std::ftell(file);
    if (size < 0) {
        std::fclose(file);
        return false;
    }
    const long start = std::max<long>(0, size - static_cast<long>(count));
    if (std::fseek(file, start, SEEK_SET) != 0) {
        std::fclose(file);
        return false;
    }
    bytes.resize(static_cast<size_t>(size - start));
    const size_t got = std::fread(bytes.data(), 1, bytes.size(), file);
    bytes.resize(got);
    std::fclose(file);
    return true;
}

uint32_t readU32(const unsigned char* data) {
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[3]) << 24);
}

uint32_t readBE32(const unsigned char* data) {
    return (static_cast<uint32_t>(data[0]) << 24) |
           (static_cast<uint32_t>(data[1]) << 16) |
           (static_cast<uint32_t>(data[2]) << 8) |
           static_cast<uint32_t>(data[3]);
}

uint32_t syncSafeToU32(const unsigned char* data) {
    return (static_cast<uint32_t>(data[0] & 0x7F) << 21) |
           (static_cast<uint32_t>(data[1] & 0x7F) << 14) |
           (static_cast<uint32_t>(data[2] & 0x7F) << 7) |
           static_cast<uint32_t>(data[3] & 0x7F);
}

void appendU32SyncSafe(std::vector<unsigned char>& out, uint32_t value) {
    out.push_back(static_cast<unsigned char>((value >> 21) & 0x7F));
    out.push_back(static_cast<unsigned char>((value >> 14) & 0x7F));
    out.push_back(static_cast<unsigned char>((value >> 7) & 0x7F));
    out.push_back(static_cast<unsigned char>(value & 0x7F));
}

std::string decodeUtf16(const unsigned char* data, size_t length) {
    if (length < 2) return {};
    unsigned int offset = 0;
    bool bigEndian = false;
    if (data[0] == 0xFF && data[1] == 0xFE) {
        offset = 2;
    } else if (data[0] == 0xFE && data[1] == 0xFF) {
        offset = 2;
        bigEndian = true;
    }
    std::wstring wide;
    wide.reserve(length / 2);
    for (size_t i = offset; i + 1 < length; i += 2) {
        wchar_t value = bigEndian
            ? static_cast<wchar_t>((data[i] << 8) | data[i + 1])
            : static_cast<wchar_t>(data[i] | (data[i + 1] << 8));
        if (value == 0) break;
        wide.push_back(value);
    }
    if (wide.empty()) return {};
#ifdef _WIN32
    const int needed = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1,
                                           nullptr, 0, nullptr, nullptr);
    if (needed <= 1) return {};
    std::string out(static_cast<size_t>(needed - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, out.data(), needed,
                        nullptr, nullptr);
    return out;
#else
    return std::string(wide.begin(), wide.end());
#endif
}

std::string decodeTextPayload(const unsigned char* payload, size_t length,
                              int encoding) {
    if (length == 0) return {};
    const unsigned char* data = payload + 1;
    const size_t size = length - 1;
    if (size == 0) return {};
    std::string text;
    if (encoding == 1 || encoding == 2) {
        text = decodeUtf16(data, size);
    } else if (encoding == 3) {
        text.assign(reinterpret_cast<const char*>(data), size);
    } else {
        text.assign(reinterpret_cast<const char*>(data), size);
    }
    while (!text.empty() && (text.back() == '\0' || text.back() == ' ')) text.pop_back();
    return text;
}

void setTag(AudioTags& tags, const std::string& key, std::string value) {
    if (value.empty()) return;
    tags[key] = std::move(value);
}

void parseId3v22(const unsigned char* data, size_t size, AudioTags& tags) {
    size_t offset = 10;
    while (offset + 6 <= size) {
        const std::string id(reinterpret_cast<const char*>(data + offset), 3);
        const size_t length = (static_cast<size_t>(data[offset + 3]) << 16) |
                              (static_cast<size_t>(data[offset + 4]) << 8) |
                              static_cast<size_t>(data[offset + 5]);
        offset += 6;
        if (length == 0 || offset + length > size) break;
        const std::string value = decodeTextPayload(data + offset, length, data[offset]);
        if (id == "TT2") setTag(tags, "title", value);
        else if (id == "TP1") setTag(tags, "artist", value);
        else if (id == "TAL") setTag(tags, "album", value);
        else if (id == "TYE") setTag(tags, "date", value);
        else if (id == "TRK") setTag(tags, "track", value);
        else if (id == "TCO") setTag(tags, "genre", value);
        else if (id == "TCM") setTag(tags, "comment", value);
        offset += length;
    }
}

void parseId3v23v24(const unsigned char* data, size_t size, bool v24, AudioTags& tags) {
    size_t offset = 10;
    if (size >= 14) {
        const unsigned int flags = (static_cast<unsigned int>(data[5]) << 8) | data[6];
        if (((static_cast<unsigned int>(data[5]) << 8) | data[6]) & 0x40) {
            const uint32_t extended = v24 ? syncSafeToU32(data + offset)
                                          : readBE32(data + offset);
            if (extended >= 4 && offset + extended <= size) offset += extended;
        }
        (void)flags;
    }
    while (offset + 10 <= size) {
        if (data[offset] == 0) break;
        const std::string id(reinterpret_cast<const char*>(data + offset), 4);
        const uint32_t frameSize = v24 ? syncSafeToU32(data + offset + 4)
                                       : readBE32(data + offset + 4);
        if (frameSize == 0) break;
        offset += 10;
        if (offset + frameSize > size) break;
        const unsigned char* payload = data + offset;
        if (id == "TIT2") setTag(tags, "title", decodeTextPayload(payload, frameSize, payload[0]));
        else if (id == "TPE1") setTag(tags, "artist", decodeTextPayload(payload, frameSize, payload[0]));
        else if (id == "TPE2") setTag(tags, "album_artist", decodeTextPayload(payload, frameSize, payload[0]));
        else if (id == "TALB") setTag(tags, "album", decodeTextPayload(payload, frameSize, payload[0]));
        else if (id == "TDRC" || id == "TYER") setTag(tags, "date", decodeTextPayload(payload, frameSize, payload[0]));
        else if (id == "TRCK") setTag(tags, "track", decodeTextPayload(payload, frameSize, payload[0]));
        else if (id == "TCON") setTag(tags, "genre", decodeTextPayload(payload, frameSize, payload[0]));
        else if (id == "TCOP") setTag(tags, "copyright", decodeTextPayload(payload, frameSize, payload[0]));
        else if (id == "TSSE") setTag(tags, "encoder", decodeTextPayload(payload, frameSize, payload[0]));
        else if (id == "COMM" && frameSize >= 4) {
            int encoding = payload[0];
            size_t pos = 4;
            while (pos < frameSize && payload[pos] != 0) ++pos;
            if (pos < frameSize) ++pos;
            setTag(tags, "comment", decodeTextPayload(payload, frameSize, encoding));
        }
        offset += frameSize;
    }
}

void parseId3v1(const std::vector<unsigned char>& data, size_t offset, AudioTags& tags) {
    if (offset + 128 > data.size()) return;
    const unsigned char* tag = data.data() + offset;
    if (std::memcmp(tag, "TAG", 3) != 0) return;
    auto text = [](const unsigned char* begin, size_t length) {
        size_t end = length;
        while (end > 0 && (begin[end - 1] == 0 || begin[end - 1] == ' ')) --end;
        return std::string(reinterpret_cast<const char*>(begin), end);
    };
    setTag(tags, "title", text(tag + 3, 30));
    setTag(tags, "artist", text(tag + 33, 30));
    setTag(tags, "album", text(tag + 63, 30));
    setTag(tags, "date", text(tag + 93, 4));
    setTag(tags, "comment", text(tag + 97, 30));
}

AudioTags parseMp3Tags(const std::string& path) {
    AudioTags tags;
    std::vector<unsigned char> prefix;
    if (readPrefix(path, prefix, kMaxId3Read) && prefix.size() >= 10 &&
        std::memcmp(prefix.data(), "ID3", 3) == 0) {
        const unsigned int major = prefix[3];
        const uint32_t size = syncSafeToU32(prefix.data() + 6);
        const size_t total = std::min<size_t>(prefix.size(), 10u + size);
        if (major == 2) parseId3v22(prefix.data(), total, tags);
        else if (major == 3) parseId3v23v24(prefix.data(), total, false, tags);
        else if (major == 4) parseId3v23v24(prefix.data(), total, true, tags);
    }
    std::vector<unsigned char> tail;
    if (readTail(path, tail, 128) && tail.size() >= 128) {
        parseId3v1(tail, tail.size() - 128, tags);
    }
    return tags;
}

void parseWavTags(const std::string& path, AudioTags& tags, std::string& note) {
    std::vector<unsigned char> bytes;
    if (!readPrefix(path, bytes, 4u * 1024u * 1024u)) return;
    if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) != 0 ||
        std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) return;

    size_t offset = 12;
    while (offset + 8 <= bytes.size()) {
        const std::string id(reinterpret_cast<const char*>(bytes.data() + offset), 4);
        const uint32_t size = readU32(bytes.data() + offset + 4);
        const size_t body = offset + 8;
        if (body + size > bytes.size()) break;
        if (id == "bext" && size >= 256 + 32 + 32 + 10) {
            auto fixedText = [&](size_t start, size_t length) {
                size_t end = start + length;
                while (end > start && (bytes[body + end - 1] == 0 ||
                                       bytes[body + end - 1] == ' ')) --end;
                return std::string(reinterpret_cast<const char*>(bytes.data() + body + start),
                                   end - start);
            };
            setTag(tags, "title", fixedText(0, 256));
            setTag(tags, "artist", fixedText(256, 32));
            setTag(tags, "date", fixedText(320, 10));
            note = "Metadati BWF bext letti; i chunk non rappresentabili in MP3 sono elencati nel report.";
        } else if (id == "LIST" && size >= 12 &&
                   std::memcmp(bytes.data() + body, "INFO", 4) == 0) {
            size_t pos = body + 4;
            while (pos + 8 <= body + size) {
                const std::string key(reinterpret_cast<const char*>(bytes.data() + pos), 4);
                const uint32_t len = readU32(bytes.data() + pos + 4);
                const size_t textPos = pos + 8;
                if (textPos + len > bytes.size()) break;
                std::string value(reinterpret_cast<const char*>(bytes.data() + textPos), len);
                while (!value.empty() && (value.back() == 0 || value.back() == ' ')) value.pop_back();
                if (key == "INAM") setTag(tags, "title", value);
                else if (key == "IART") setTag(tags, "artist", value);
                else if (key == "IPRD") setTag(tags, "album", value);
                else if (key == "ICRD") setTag(tags, "date", value);
                else if (key == "IGNR") setTag(tags, "genre", value);
                else if (key == "ITRK") setTag(tags, "track", value);
                else if (key == "ICMT") setTag(tags, "comment", value);
                else if (key == "ICOP") setTag(tags, "copyright", value);
                pos = textPos + len + (len & 1u);
            }
        }
        offset = body + size + (size & 1u);
    }
}

AudioFileFormat sniffFormat(const std::string& path) {
    std::vector<unsigned char> head;
    if (readPrefix(path, head, 16) && head.size() >= 4) {
        if (head.size() >= 12 && std::memcmp(head.data(), "RIFF", 4) == 0 &&
            std::memcmp(head.data() + 8, "WAVE", 4) == 0) {
            return AudioFileFormat::Wav;
        }
        if (std::memcmp(head.data(), "fLaC", 4) == 0) return AudioFileFormat::Flac;
        if (std::memcmp(head.data(), "OggS", 4) == 0) return AudioFileFormat::Flac;
        if (std::memcmp(head.data(), "ID3", 3) == 0) return AudioFileFormat::Mp3;
        if (head[0] == 0xFF && (head[1] & 0xE0) == 0xE0) return AudioFileFormat::Mp3;
    }
    return audioFormatFromPath(path);
}

bool detectMp3Vbr(const std::string& path) {
    std::vector<unsigned char> bytes;
    if (!readPrefix(path, bytes, 256 * 1024)) return false;
    const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (text.find("Xing") != std::string::npos) return true;
    if (text.find("VBRI") != std::string::npos) return true;
    return false;
}

int mp3BitrateFromHeader(const std::string& path) {
    std::vector<unsigned char> bytes;
    if (!readPrefix(path, bytes, 64 * 1024)) return 0;
    static constexpr int bitrates[2][3][16] = {
        {{0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, 0},
         {0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, 0},
         {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0}},
        {{0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256, 0},
         {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0},
         {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0}}};
    for (size_t i = 0; i + 4 < bytes.size(); ++i) {
        if (bytes[i] != 0xFF || (bytes[i + 1] & 0xE0) != 0xE0) continue;
        const int version = (bytes[i + 1] >> 3) & 3; // 3=MPEG1, 2=MPEG2, 0=MPEG2.5
        const int layer = (bytes[i + 1] >> 1) & 3;   // 1=Layer III
        const int index = (bytes[i + 2] >> 4) & 0x0F;
        if (layer != 1 || index == 0 || index == 15 || version == 1) continue;
        const int table = version == 3 ? 0 : 1;
        return bitrates[table][2][index];
    }
    return 0;
}

bool decodeMp3(const std::string& path, AudioBuffer& out, AudioFileInfo& info,
               std::string& error) {
    drmp3 decoder = {};
#ifdef _WIN32
    const std::wstring wide = toWidePath(path);
    if (!drmp3_init_file_w(&decoder, wide.c_str(), nullptr)) {
        error = "MP3 non valido o non decodificabile";
        return false;
    }
#else
    if (!drmp3_init_file(&decoder, path.c_str(), nullptr)) {
        error = "MP3 non valido o non decodificabile";
        return false;
    }
#endif
    info.format = AudioFileFormat::Mp3;
    out.channels = static_cast<int>(decoder.channels);
    out.sampleRate = static_cast<int>(decoder.sampleRate);
    if (out.channels <= 0 || out.sampleRate <= 0) {
        drmp3_uninit(&decoder);
        error = "MP3 senza formato audio valido";
        return false;
    }

    std::vector<float> samples;
    std::vector<float> chunk(4096u * static_cast<size_t>(out.channels));
    drmp3_uint64 got = 0;
    while ((got = drmp3_read_pcm_frames_f32(&decoder, 4096, chunk.data())) > 0) {
        samples.insert(samples.end(), chunk.begin(),
                       chunk.begin() + static_cast<size_t>(got) * out.channels);
    }
    info.mp3HasGapless = decoder.delayInPCMFrames != 0 || decoder.paddingInPCMFrames != 0;
    drmp3_uninit(&decoder);

    if (samples.empty()) {
        error = "MP3 senza campioni decodificabili";
        return false;
    }
    out.samples = std::move(samples);
    info.channels = out.channels;
    info.sampleRate = out.sampleRate;
    info.frames = out.frameCount();
    info.durationSeconds = out.durationSeconds();
    info.bitsPerSample = 16;
    info.isFloat = false;
    info.mp3BitrateKbps = mp3BitrateFromHeader(path);
    info.mp3Vbr = detectMp3Vbr(path);
    info.tags = parseMp3Tags(path);
    info.metadataNote =
        "MP3 decodificato in PCM float; il file esportato in MP3 subisce una nuova codifica lossy.";
    return true;
}

bool decodeFlac(const std::string& path, AudioBuffer& out, AudioFileInfo& info,
                std::string& error) {
#ifdef _WIN32
    const std::wstring wide = toWidePath(path);
    drflac* decoder = drflac_open_file_w(wide.c_str(), nullptr);
#else
    drflac* decoder = drflac_open_file(path.c_str(), nullptr);
#endif
    if (!decoder) {
        error = "FLAC non valido o non decodificabile";
        return false;
    }
    info.format = AudioFileFormat::Flac;
    out.channels = static_cast<int>(decoder->channels);
    out.sampleRate = static_cast<int>(decoder->sampleRate);
    info.bitsPerSample = static_cast<int>(decoder->bitsPerSample);
    if (out.channels <= 0 || out.sampleRate <= 0) {
        drflac_close(decoder);
        error = "FLAC senza formato audio valido";
        return false;
    }

    std::vector<float> samples;
    std::vector<float> chunk(4096u * static_cast<size_t>(out.channels));
    drflac_uint64 got = 0;
    while ((got = drflac_read_pcm_frames_f32(decoder, 4096, chunk.data())) > 0) {
        samples.insert(samples.end(), chunk.begin(),
                       chunk.begin() + static_cast<size_t>(got) * out.channels);
    }
    drflac_close(decoder);
    if (samples.empty()) {
        error = "FLAC senza campioni decodificabili";
        return false;
    }
    out.samples = std::move(samples);
    info.channels = out.channels;
    info.sampleRate = out.sampleRate;
    info.frames = out.frameCount();
    info.durationSeconds = out.durationSeconds();
    info.isFloat = false;
    info.metadataNote =
        "FLAC importato in PCM float; i commenti Vorbis non sono trasferiti nel WAV.";
    return true;
}

bool probeMp3(const std::string& path, AudioFileInfo& info, std::string& error) {
    drmp3 decoder = {};
#ifdef _WIN32
    const std::wstring wide = toWidePath(path);
    if (!drmp3_init_file_w(&decoder, wide.c_str(), nullptr)) {
#else
    if (!drmp3_init_file(&decoder, path.c_str(), nullptr)) {
#endif
        error = "MP3 non valido o non decodificabile";
        return false;
    }
    info.format = AudioFileFormat::Mp3;
    info.channels = static_cast<int>(decoder.channels);
    info.sampleRate = static_cast<int>(decoder.sampleRate);
    info.bitsPerSample = 16;
    info.isFloat = false;
    drmp3_uint64 mp3Frames = 0;
    drmp3_uint64 pcmFrames = 0;
    if (drmp3_get_mp3_and_pcm_frame_count(&decoder, &mp3Frames, &pcmFrames)) {
        const drmp3_uint64 delay = decoder.delayInPCMFrames;
        const drmp3_uint64 padding = decoder.paddingInPCMFrames;
        info.frames = pcmFrames > delay + padding
            ? static_cast<int64_t>(pcmFrames - delay - padding)
            : static_cast<int64_t>(pcmFrames);
        info.mp3HasGapless = delay != 0 || padding != 0;
    }
    info.durationSeconds = info.sampleRate > 0
        ? static_cast<double>(info.frames) / info.sampleRate : 0.0;
    info.mp3BitrateKbps = mp3BitrateFromHeader(path);
    info.mp3Vbr = detectMp3Vbr(path);
    info.tags = parseMp3Tags(path);
    drmp3_uninit(&decoder);
    info.metadataNote =
        "MP3: durata effettiva calcolata da delay/padding quando il tag LAME e' presente.";
    return info.channels > 0 && info.sampleRate > 0;
}

bool probeFlac(const std::string& path, AudioFileInfo& info, std::string& error) {
#ifdef _WIN32
    const std::wstring wide = toWidePath(path);
    drflac* decoder = drflac_open_file_w(wide.c_str(), nullptr);
#else
    drflac* decoder = drflac_open_file(path.c_str(), nullptr);
#endif
    if (!decoder) {
        error = "FLAC non valido o non decodificabile";
        return false;
    }
    info.format = AudioFileFormat::Flac;
    info.channels = static_cast<int>(decoder->channels);
    info.sampleRate = static_cast<int>(decoder->sampleRate);
    info.bitsPerSample = static_cast<int>(decoder->bitsPerSample);
    info.frames = static_cast<int64_t>(decoder->totalPCMFrameCount);
    info.durationSeconds = info.sampleRate > 0
        ? static_cast<double>(info.frames) / info.sampleRate : 0.0;
    drflac_close(decoder);
    info.metadataNote =
        "FLAC: import supportato; l'esportazione FLAC non e' disponibile.";
    return info.channels > 0 && info.sampleRate > 0;
}

std::string trimTag(const std::string& text) {
    size_t begin = 0;
    while (begin < text.size() && (text[begin] == ' ' || text[begin] == '\0')) ++begin;
    size_t end = text.size();
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\0')) --end;
    return text.substr(begin, end - begin);
}

void fillTagsFromInfo(const AudioFileInfo& info, AudioTags& tags) {
    for (const auto& pair : info.tags) {
        if (!pair.second.empty()) tags[pair.first] = pair.second;
    }
}

std::vector<unsigned char> buildFrame(const char* id, const std::string& text) {
    if (text.empty()) return {};
    std::vector<unsigned char> payload;
    payload.reserve(text.size() + 2);
    payload.push_back(3);  // UTF-8
    payload.insert(payload.end(), text.begin(), text.end());
    std::vector<unsigned char> frame;
    frame.insert(frame.end(), id, id + 4);
    appendU32SyncSafe(frame, static_cast<uint32_t>(payload.size()));
    frame.push_back(0);
    frame.push_back(0);
    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}

std::vector<unsigned char> buildCommentFrame(const std::string& text) {
    if (text.empty()) return {};
    std::vector<unsigned char> payload;
    payload.push_back(3);
    payload.insert(payload.end(), {'e', 'n', 'g'});
    payload.push_back(0);
    payload.insert(payload.end(), text.begin(), text.end());
    std::vector<unsigned char> frame;
    frame.insert(frame.end(), {'C', 'O', 'M', 'M'});
    appendU32SyncSafe(frame, static_cast<uint32_t>(payload.size()));
    frame.push_back(0);
    frame.push_back(0);
    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}

std::vector<unsigned char> buildId3v2(const AudioTags& tags, double durationSeconds,
                                      int sampleRate, int channels,
                                      const Mp3EncodeSettings& settings) {
    std::vector<unsigned char> frames;
    const auto add = [&](const char* id, const char* key) {
        const auto it = tags.find(key);
        if (it == tags.end()) return;
        const std::vector<unsigned char> frame = buildFrame(id, trimTag(it->second));
        frames.insert(frames.end(), frame.begin(), frame.end());
    };
    add("TIT2", "title");
    add("TPE1", "artist");
    add("TPE2", "album_artist");
    add("TALB", "album");
    add("TDRC", "date");
    add("TRCK", "track");
    add("TCON", "genre");
    add("TCOP", "copyright");
    add("TSSE", "encoder");
    if (durationSeconds > 0.0) {
        char length[64] = {};
        snprintf(length, sizeof(length), "%.0f", durationSeconds * 1000.0);
        const std::vector<unsigned char> frame = buildFrame("TLEN", length);
        frames.insert(frames.end(), frame.begin(), frame.end());
    }
    {
        char description[128] = {};
        snprintf(description, sizeof(description),
                 "NL Breath Click Studio / LAME 3.100 / %s / %d Hz / %d ch",
                 settings.vbr ? "VBR" : "CBR", sampleRate, channels);
        const std::vector<unsigned char> frame = buildFrame("TSSE", description);
        frames.insert(frames.end(), frame.begin(), frame.end());
    }
    if (tags.count("comment")) {
        const std::vector<unsigned char> frame = buildCommentFrame(trimTag(tags.at("comment")));
        frames.insert(frames.end(), frame.begin(), frame.end());
    }

    std::vector<unsigned char> id3;
    id3.insert(id3.end(), {'I', 'D', '3'});
    id3.push_back(4);
    id3.push_back(0);
    id3.push_back(0);
    appendU32SyncSafe(id3, static_cast<uint32_t>(frames.size() + 1024));
    id3.insert(id3.end(), frames.begin(), frames.end());
    id3.resize(10 + frames.size() + 1024, 0);
    return id3;
}

std::string lameError(int code) {
    switch (code) {
        case -1: return "memoria insufficiente";
        case -2: return "parametro non valido";
        case -3: return "inizializzazione fallita";
        case -4: return "file di output non scrivibile";
        case -5: return "errore di codifica";
        default: return "errore LAME " + std::to_string(code);
    }
}

} // namespace

const char* audioFormatName(AudioFileFormat format) {
    switch (format) {
        case AudioFileFormat::Wav: return "WAV";
        case AudioFileFormat::Mp3: return "MP3";
        case AudioFileFormat::Flac: return "FLAC";
        default: return "sconosciuto";
    }
}

AudioFileFormat audioFormatFromPath(const std::string& path) {
    const std::string ext = lowerExtension(path);
    if (ext == ".wav" || ext == ".wave") return AudioFileFormat::Wav;
    if (ext == ".mp3" || ext == ".mp2") return AudioFileFormat::Mp3;
    if (ext == ".flac" || ext == ".ogg") return AudioFileFormat::Flac;
    return AudioFileFormat::Unknown;
}

bool isSupportedAudioPath(const std::string& path) {
    return audioFormatFromPath(path) != AudioFileFormat::Unknown;
}

bool probeAudio(const std::string& path, AudioFileInfo& info, std::string* error) {
    info = {};
    info.path = path;
    info.extension = lowerExtension(path);
    const AudioFileFormat format = sniffFormat(path);
    std::string localError;
    bool ok = false;
    if (format == AudioFileFormat::Wav) {
        WavInfo wav;
        ok = probeWav(path, wav, &localError);
        if (ok) {
            info.format = AudioFileFormat::Wav;
            info.channels = wav.channels;
            info.sampleRate = wav.sampleRate;
            info.bitsPerSample = wav.bitsPerSample;
            info.isFloat = wav.isFloat;
            info.frames = wav.frames;
            info.durationSeconds = wav.sampleRate > 0
                ? static_cast<double>(wav.frames) / wav.sampleRate : 0.0;
            parseWavTags(path, info.tags, info.metadataNote);
        }
    } else if (format == AudioFileFormat::Mp3) {
        ok = probeMp3(path, info, localError);
    } else if (format == AudioFileFormat::Flac) {
        ok = probeFlac(path, info, localError);
    } else {
        localError = "Formato audio non riconosciuto: usare WAV, MP3 o FLAC";
    }
    if (error) *error = ok ? localError : (localError.empty() ? "Formato non supportato" : localError);
    return ok;
}

bool readAudio(const std::string& path, AudioBuffer& out, AudioFileInfo* info,
               std::string* error) {
    out.clear();
    AudioFileInfo localInfo;
    const AudioFileFormat format = sniffFormat(path);
    std::string localError;
    bool ok = false;
    if (format == AudioFileFormat::Wav) {
        ok = readWav(path, out, &localError);
        if (ok) {
            localInfo.format = AudioFileFormat::Wav;
            localInfo.channels = out.channels;
            localInfo.sampleRate = out.sampleRate;
            localInfo.frames = out.frameCount();
            localInfo.durationSeconds = out.durationSeconds();
            WavInfo wav;
            if (probeWav(path, wav, nullptr)) {
                localInfo.bitsPerSample = wav.bitsPerSample;
                localInfo.isFloat = wav.isFloat;
            }
            parseWavTags(path, localInfo.tags, localInfo.metadataNote);
        }
    } else if (format == AudioFileFormat::Mp3) {
        ok = decodeMp3(path, out, localInfo, localError);
    } else if (format == AudioFileFormat::Flac) {
        ok = decodeFlac(path, out, localInfo, localError);
    } else {
        localError = "Formato audio non riconosciuto: usare WAV, MP3 o FLAC";
    }
    if (!ok) {
        if (error) *error = localError.empty() ? "Lettura audio fallita" : localError;
        return false;
    }
    localInfo.path = path;
    localInfo.extension = lowerExtension(path);
    localInfo.frames = out.frameCount();
    localInfo.durationSeconds = out.durationSeconds();
    if (!localInfo.warning.empty() && error) *error = localInfo.warning;
    else if (error) error->clear();
    if (info) *info = localInfo;
    return true;
}

bool writeMp3(const std::string& path, const AudioBuffer& buffer,
              const Mp3EncodeSettings& settings, const AudioTags& tags,
              AudioFileInfo* writtenInfo, std::string* error) {
    if (buffer.empty() || buffer.sampleRate <= 0) {
        if (error) *error = "Buffer audio vuoto o sample rate non valido";
        return false;
    }
    if (buffer.channels != 1 && buffer.channels != 2) {
        if (error) *error = "MP3 supporta solo mono o stereo (file a " +
                            std::to_string(buffer.channels) + " canali)";
        return false;
    }
    const int bitrate = settings.bitrateKbps;
    if (!settings.vbr && bitrate != 128 && bitrate != 192 &&
        bitrate != 256 && bitrate != 320) {
        if (error) *error = "Bitrate MP3 non valido: usare 128, 192, 256 o 320 kbps";
        return false;
    }
    if (settings.vbrQuality < 0 || settings.vbrQuality > 9) {
        if (error) *error = "Qualita' VBR non valida: usare 0..9";
        return false;
    }

    LameApi& api = lameApi();
    if (!api.load()) {
        if (error) *error = "esportazione MP3 non disponibile: " + lameLoadError() +
                            " (l'esportazione WAV resta disponibile)";
        return false;
    }
    lame_t lame = api.init();
    if (!lame) {
        if (error) *error = "encoder MP3 non inizializzabile";
        return false;
    }
    api.setNumSamples(lame, static_cast<unsigned long>(std::min<int64_t>(
        buffer.frameCount(), std::numeric_limits<unsigned long>::max())));
    api.setInSampleRate(lame, buffer.sampleRate);
    api.setOutSampleRate(lame, buffer.sampleRate);
    api.setNumChannels(lame, buffer.channels);
    api.setMode(lame, buffer.channels == 1 ? MONO : JOINT_STEREO);
    api.setQuality(lame, std::max(0, std::min(9, settings.quality)));
    api.setWriteVbrTag(lame, 1);
    api.setWriteId3TagAutomatic(lame, 0);
    if (settings.vbr) {
        api.setVbr(lame, vbr_mtrh);
        api.setVbrQuality(lame, settings.vbrQuality);
    } else {
        api.setVbr(lame, vbr_off);
        api.setBitrate(lame, bitrate);
    }
    const int init = api.initParams(lame);
    if (init < 0) {
        const std::string message = "inizializzazione LAME: " + lameError(init);
        api.close(lame);
        if (error) *error = message;
        return false;
    }

    const int64_t totalFrames = buffer.frameCount();
    if (totalFrames > static_cast<int64_t>(std::numeric_limits<int>::max())) {
        api.close(lame);
        if (error) *error = "file troppo lungo per l'encoder MP3";
        return false;
    }
    const int frames = static_cast<int>(totalFrames);
    const size_t estimated = static_cast<size_t>(static_cast<double>(frames) * 1.25) + 7200u;
    std::vector<unsigned char> mp3Buffer(estimated);
    std::vector<unsigned char> encoded;
    int result = 0;
    if (buffer.channels == 1) {
        result = api.encodeFloat(lame, buffer.samples.data(), buffer.samples.data(),
                                 frames, mp3Buffer.data(),
                                 static_cast<int>(mp3Buffer.size()));
    } else {
        std::vector<float> left(static_cast<size_t>(frames));
        std::vector<float> right(static_cast<size_t>(frames));
        for (int i = 0; i < frames; ++i) {
            left[static_cast<size_t>(i)] = buffer.samples[static_cast<size_t>(i) * 2];
            right[static_cast<size_t>(i)] = buffer.samples[static_cast<size_t>(i) * 2 + 1];
        }
        result = api.encodeFloat(lame, left.data(), right.data(), frames,
                                 mp3Buffer.data(), static_cast<int>(mp3Buffer.size()));
    }
    if (result < 0) {
        const std::string message = "codifica LAME: " + lameError(result);
        api.close(lame);
        if (error) *error = message;
        return false;
    }
    encoded.insert(encoded.end(), mp3Buffer.begin(), mp3Buffer.begin() + result);
    {
        const int flushed = api.encodeFlush(lame, mp3Buffer.data(),
                                           static_cast<int>(mp3Buffer.size()));
        if (flushed < 0) {
            const std::string message = "flush LAME: " + lameError(flushed);
            api.close(lame);
            if (error) *error = message;
            return false;
        }
        encoded.insert(encoded.end(), mp3Buffer.begin(), mp3Buffer.begin() + flushed);
    }
    {
        std::vector<unsigned char> tagBuffer(8192);
        const size_t tagSize = api.getLameTagFrame(lame, tagBuffer.data(), tagBuffer.size());
        if (tagSize > 0 && tagSize <= tagBuffer.size()) {
            tagBuffer.resize(tagSize);
            encoded.insert(encoded.begin(), tagBuffer.begin(), tagBuffer.end());
        } else {
        }
    }
    api.close(lame);

    if (encoded.empty()) {
        if (error) *error = "LAME non ha prodotto audio";
        return false;
    }
    const double duration = buffer.durationSeconds();
    const std::vector<unsigned char> id3 = buildId3v2(tags, duration, buffer.sampleRate,
                                                      buffer.channels, settings);
    std::FILE* output = openUtf8(path, "wb");
    if (!output) {
        if (error) *error = "Impossibile creare il file MP3: " + path;
        return false;
    }
    bool ok = std::fwrite(id3.data(), 1, id3.size(), output) == id3.size() &&
              std::fwrite(encoded.data(), 1, encoded.size(), output) == encoded.size();
    if (std::fclose(output) != 0) ok = false;
    if (!ok) {
        if (error) *error = "Scrittura MP3 fallita (disco pieno?)";
        return false;
    }

    if (writtenInfo) {
        *writtenInfo = {};
        writtenInfo->format = AudioFileFormat::Mp3;
        writtenInfo->path = path;
        writtenInfo->extension = ".mp3";
        writtenInfo->channels = buffer.channels;
        writtenInfo->sampleRate = buffer.sampleRate;
        writtenInfo->frames = totalFrames;
        writtenInfo->durationSeconds = duration;
        writtenInfo->mp3BitrateKbps = settings.vbr ? 0 : bitrate;
        writtenInfo->mp3Vbr = settings.vbr;
        writtenInfo->mp3VbrQuality = settings.vbr ? settings.vbrQuality : -1;
        writtenInfo->tags = tags;
        writtenInfo->metadataNote = "MP3 riscritto: nuova codifica lossy, non lossless.";
    }
    if (error) error->clear();
    return true;
}

} // namespace bc

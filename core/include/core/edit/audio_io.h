// SPDX-License-Identifier: MPL-2.0
// ============================================================================
//  audio_io.h — lettura/scrittura audio con formati reali da spot radio
//
//  WAV resta il formato di lavoro interno (PCM float); MP3 e FLAC vengono
//  decodificati in PCM float all'ingresso. In uscita BreathClick scrive WAV
//  PCM oppure MP3 con LAME (CBR/VBR). Il documento originale non viene mai
//  modificato.
// ============================================================================
#pragma once

#include <cstdint>
#include <map>
#include <string>

#include "core/edit/wav_io.h"

namespace bc {

enum class AudioFileFormat {
    Unknown = 0,
    Wav,
    Mp3,
    Flac
};

// Tag comuni (ID3 per MP3, LIST/INFO o bext per WAV quando disponibili).
// Le chiavi sono minuscole: title, artist, album, album_artist, date,
// genre, track, comment, copyright, encoder.
using AudioTags = std::map<std::string, std::string>;

struct AudioFileInfo {
    AudioFileFormat format = AudioFileFormat::Unknown;
    std::string path;
    std::string extension;
    int channels = 0;
    int sampleRate = 0;
    int bitsPerSample = 0;
    bool isFloat = false;
    int64_t frames = 0;
    double durationSeconds = 0.0;

    // Solo MP3: bitrate nominale (CBR) e tipo di flusso.
    int mp3BitrateKbps = 0;
    bool mp3Vbr = false;
    int mp3VbrQuality = -1;
    bool mp3HasGapless = false;

    AudioTags tags;
    // Metadati non trasferibili o avvisi leggibili (es. new codifica lossy).
    std::string metadataNote;
    std::string warning;
};

struct Mp3EncodeSettings {
    // bitrate usato quando vbr == false (128/192/256/320).
    int bitrateKbps = 192;
    bool vbr = false;
    int vbrQuality = 2;   // 0 = migliore, 9 = peggiore
    int quality = 2;      // algoritmo LAME 0..9
};

// Riconosce il contenuto, non solo l'estensione. Un .mp3 rinominato .wav
// viene letto come MP3 e viceversa.
const char* audioFormatName(AudioFileFormat format);
AudioFileFormat audioFormatFromPath(const std::string& path);

bool readAudio(const std::string& path, AudioBuffer& out,
               AudioFileInfo* info = nullptr, std::string* error = nullptr);
bool probeAudio(const std::string& path, AudioFileInfo& info,
                std::string* error = nullptr);

// Scrive un MP3 con ID3v2.4; restituisce i metadati effettivamente scritti.
bool writeMp3(const std::string& path, const AudioBuffer& buffer,
              const Mp3EncodeSettings& settings, const AudioTags& tags,
              AudioFileInfo* writtenInfo = nullptr, std::string* error = nullptr);

bool isSupportedAudioPath(const std::string& path);

} // namespace bc

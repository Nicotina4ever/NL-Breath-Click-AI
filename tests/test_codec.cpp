// SPDX-License-Identifier: MPL-2.0
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "core/edit/audio_io.h"

namespace {

bool almostEqual(double a, double b, double tolerance) {
    return std::abs(a - b) <= tolerance;
}

int fail(const char* message) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

} // namespace

int main() {
    const auto directory = std::filesystem::temp_directory_path() / "bc_codec_tests";
    std::error_code ec;
    std::filesystem::remove_all(directory, ec);
    std::filesystem::create_directories(directory, ec);

    bc::AudioBuffer source;
    source.channels = 1;
    source.sampleRate = 48000;
    const int frames = 48000;
    source.samples.resize(static_cast<size_t>(frames));
    for (int i = 0; i < frames; ++i) {
        source.samples[static_cast<size_t>(i)] =
            0.22f * std::sin(2.0 * 3.14159265358979323846 * 440.0 * i / source.sampleRate);
    }

    const std::string wavPath = (directory / "source.wav").u8string();
    if (!bc::writeWav(wavPath, source, bc::WavFormat::Pcm16)) return fail("writeWav");

    bc::AudioFileInfo wavInfo;
    bc::AudioBuffer decoded;
    std::string error;
    if (!bc::readAudio(wavPath, decoded, &wavInfo, &error)) return fail(error.c_str());
    if (wavInfo.format != bc::AudioFileFormat::Wav) return fail("WAV format");
    if (decoded.channels != 1 || decoded.sampleRate != 48000) return fail("WAV channels/rate");
    if (!almostEqual(decoded.durationSeconds(), 1.0, 0.001)) return fail("WAV duration");

    const std::string mp3Path = (directory / "source.mp3").u8string();
    bc::Mp3EncodeSettings settings;
    settings.vbr = false;
    settings.bitrateKbps = 192;
    bc::AudioTags tags;
    tags["title"] = "Prova codec";
    tags["artist"] = "BreathClick Test";
    if (!bc::writeMp3(mp3Path, source, settings, tags)) return fail("writeMp3");

    bc::AudioFileInfo mp3Info;
    bc::AudioBuffer mp3Decoded;
    if (!bc::readAudio(mp3Path, mp3Decoded, &mp3Info, &error)) return fail(error.c_str());
    if (mp3Info.format != bc::AudioFileFormat::Mp3) return fail("MP3 format");
    if (mp3Info.channels != 1 || mp3Info.sampleRate != 48000) return fail("MP3 channels/rate");
    if (!almostEqual(mp3Info.durationSeconds, 1.0, 0.01)) {
        std::fprintf(stderr, "MP3 duration %.6f\n", mp3Info.durationSeconds);
        return fail("MP3 duration");
    }
    if (mp3Info.tags.count("title") == 0 || mp3Info.tags.at("title") != "Prova codec") {
        return fail("MP3 title tag");
    }
    if (mp3Info.tags.count("artist") == 0 || mp3Info.tags.at("artist") != "BreathClick Test") {
        return fail("MP3 artist tag");
    }

    // Stereo e tutti i bitrate previsti dalla GUI.
    bc::AudioBuffer stereo;
    stereo.channels = 2;
    stereo.sampleRate = 44100;
    const int stereoFrames = 22050;
    stereo.samples.resize(static_cast<size_t>(stereoFrames) * 2);
    for (int i = 0; i < stereoFrames; ++i) {
        const float value = 0.18f * std::sin(2.0 * 3.14159265358979323846 * 330.0 * i / 44100.0);
        stereo.samples[static_cast<size_t>(i) * 2] = value;
        stereo.samples[static_cast<size_t>(i) * 2 + 1] = -value;
    }
    for (int bitrate : {128, 192, 256, 320}) {
        const std::string path = (directory / ("stereo_" + std::to_string(bitrate) + ".mp3")).u8string();
        settings.vbr = false;
        settings.bitrateKbps = bitrate;
        if (!bc::writeMp3(path, stereo, settings, tags)) return fail("writeMp3 stereo");
        bc::AudioFileInfo info;
        bc::AudioBuffer decodedStereo;
        if (!bc::readAudio(path, decodedStereo, &info, &error)) return fail(error.c_str());
        if (info.channels != 2 || info.sampleRate != 44100) return fail("stereo channels/rate");
        if (!almostEqual(info.durationSeconds, 0.5, 0.01)) return fail("stereo duration");
        if (info.mp3BitrateKbps != bitrate) {
            std::fprintf(stderr, "bitrate %d -> %d\n", bitrate, info.mp3BitrateKbps);
            return fail("stereo bitrate");
        }
    }

    const std::string vbrPath = (directory / "vbr.mp3").u8string();
    settings.vbr = true;
    settings.vbrQuality = 2;
    if (!bc::writeMp3(vbrPath, stereo, settings, tags)) return fail("writeMp3 VBR");
    bc::AudioFileInfo vbrInfo;
    bc::AudioBuffer vbrAudio;
    if (!bc::readAudio(vbrPath, vbrAudio, &vbrInfo, &error)) return fail(error.c_str());
    if (!vbrInfo.mp3Vbr) return fail("VBR not detected");
    if (!almostEqual(vbrInfo.durationSeconds, 0.5, 0.01)) return fail("VBR duration");

    // Un WAV con estensione .mp3 deve essere riconosciuto dal contenuto.
    const std::string disguised = (directory / "disguised.mp3").u8string();
    std::filesystem::copy_file(wavPath, disguised, std::filesystem::copy_options::overwrite_existing, ec);
    bc::AudioFileInfo sniffed;
    if (!bc::probeAudio(disguised, sniffed, &error)) return fail(error.c_str());
    if (sniffed.format != bc::AudioFileFormat::Wav) return fail("content sniffing");

    // Un file non audio deve fallire con un errore, non con un crash.
    const std::string broken = (directory / "broken.mp3").u8string();
    {
        std::FILE* file = std::fopen(broken.c_str(), "wb");
        if (!file) return fail("broken fixture");
        const unsigned char bytes[] = {0x00, 0x01, 0x02, 0x03, 0x04};
        std::fwrite(bytes, 1, sizeof(bytes), file);
        std::fclose(file);
    }
    bc::AudioBuffer invalid;
    if (bc::readAudio(broken, invalid, nullptr, &error)) return fail("broken MP3 accepted");
    if (error.empty()) return fail("broken MP3 error message");

    std::filesystem::remove_all(directory, ec);
    std::printf("CODEC TESTS OK: WAV, MP3, tag, sniffing, errore controllato\n");
    return 0;
}

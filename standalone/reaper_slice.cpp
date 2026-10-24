// SPDX-License-Identifier: MPL-2.0
#include "reaper_slice.h"
#include "studio_engine.h"
#include "core/edit/audio_io.h"
#include "core/util/atomic_file.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>

int commandVerifySource(int argc, char** argv) {
    if (argc != 5 || std::string(argv[3]) != "--sha256") return 2;
    std::string hash, error;
    if (!bc::studio::sha256File(argv[2], hash, &error) || hash != argv[4]) {
        std::cerr << "Sorgente cambiata o non leggibile: " << error << "\n";
        return 1;
    }
    return 0;
}

int commandSlice(int argc, char** argv) {
    std::string output;
    double start = -1, duration = -1;
    try {
        for (int i = 3; i < argc; ++i) {
            const std::string arg = argv[i];
            if (++i >= argc) return 2;
            if (arg == "--out") output = argv[i];
            else if (arg == "--start" || arg == "--duration") {
                size_t used = 0;
                const double value = std::stod(argv[i], &used);
                if (used != std::string(argv[i]).size()) return 2;
                (arg == "--start" ? start : duration) = value;
            } else return 2;
        }
    } catch (...) { return 2; }
    if (argc < 3 || output.empty() || !std::isfinite(start) || !std::isfinite(duration) ||
        start < 0 || duration <= 0) return 2;
    const auto destination = bc::nativeFilePath(std::filesystem::u8path(output));
    if (std::filesystem::exists(destination) || std::filesystem::exists(
            bc::nativeFilePath(std::filesystem::u8path(output + ".source.json")))) {
        std::cerr << "La destinazione deve essere nuova\n";
        return 1;
    }
    bc::AudioBuffer source, segment;
    std::string before, after, error;
    if (!bc::studio::sha256File(argv[2], before, &error) ||
        !bc::readAudio(argv[2], source, nullptr, &error) ||
        !bc::studio::sha256File(argv[2], after, &error) || before != after) {
        std::cerr << "Sorgente instabile o non leggibile: " << error << "\n";
        return 1;
    }
    // Check in floating point before converting, including overflow/NaN.
    const double begin = start * source.sampleRate;
    const double count = duration * source.sampleRate;
    if (!std::isfinite(begin) || !std::isfinite(count) || count < 0.5 ||
        begin > source.frameCount() || count > source.frameCount() + 0.5 ||
        begin + count > source.frameCount() + 0.5) {
        std::cerr << "Intervallo fuori dalla sorgente (loop non renderizzato)\n";
        return 1;
    }
    const auto first = static_cast<int64_t>(std::llround(begin));
    const auto frames = static_cast<int64_t>(std::llround(count));
    if (first < 0 || frames < 1 || first + frames > source.frameCount()) return 1;
    segment.sampleRate = source.sampleRate;
    segment.channels = source.channels;
    segment.samples.assign(source.samples.begin() + first * source.channels,
                           source.samples.begin() + (first + frames) * source.channels);
    if (!bc::writeWav(output, segment, bc::WavFormat::Float32, &error)) {
        std::cerr << error << "\n"; return 1;
    }
    std::ostringstream metadata;
    metadata << "{\"source_sha256\":\"" << before << "\",\"sample_rate\":"
             << segment.sampleRate << ",\"channels\":" << segment.channels
             << ",\"source_start_frame\":" << first << ",\"frames\":" << frames << "}";
    if (!bc::writeTextAtomic(output + ".source.json", metadata.str(), &error)) {
        std::cerr << error << "\n"; return 1;
    }
    std::cout << "SLICE OK: " << frames << " frames\n";
    return 0;
}

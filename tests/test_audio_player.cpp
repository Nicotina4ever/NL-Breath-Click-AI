// SPDX-License-Identifier: MPL-2.0
#include "editor/audio_player.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

static void check(bool ok, const char* text) { if (!ok) throw std::runtime_error(text); }
int main() try {
    bc::AudioPlayer player;
    std::string error;
    check(player.open(48000, 1, &error), "No playback device available for the real WaveOut range test");
    auto audio = std::make_shared<std::vector<float>>(48000 * 2, 0.f); // silent host test
    player.setAudio(audio, audio->size());
    auto finished = [&](int64_t end) {
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (player.isPlaying() && std::chrono::steady_clock::now() < until)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        check(!player.isPlaying(), "playback did not stop at the selected endpoint");
        check(player.position() == end, "natural completion lost the sample endpoint");
    };
    for (int width : {1, 999, 3001, 4500, 12000}) {
        player.play(13000, 13000 + width);
        finished(13000 + width);
    }
    // Rapid source/transport changes must not requeue DONE buffers from an
    // earlier run. No sound is emitted and no system configuration is changed.
    for (int i = 0; i < 50; ++i) {
        player.play(i * 100, i * 100 + 20000);
        std::this_thread::sleep_for(std::chrono::milliseconds(i % 4));
        player.stop();
        player.setAudio(audio, audio->size());
    }
    player.play(24000, 26017); finished(26017);
    player.play(4000, 3000);
    check(!player.isPlaying(), "inverted range must not start playback");
    player.play(95000); finished(96000);
    check(player.takePlaybackError() == 0, "unexpected WaveOut write error");
    player.close();
    std::cout << "WaveOut PASS: bounded audition, exact completion, rapid restart/source change, normal full playback\n";
    return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }

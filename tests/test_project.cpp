// SPDX-License-Identifier: MPL-2.0
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "studio_project.h"
#include "reaper_adapter.h"

int main() {
    const auto root = std::filesystem::temp_directory_path() / "bc_project_test";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "audio", ec);
    const auto sourceA = root / "audio" / "spot_a.wav";
    const auto sourceB = root / "audio" / "spot_b.wav";
    std::ofstream(sourceA, std::ios::binary) << "not-a-real-wav";
    std::ofstream(sourceB, std::ios::binary) << "not-a-real-wav";

    bc::studio::ProjectData project;
    project.name = "test";
    project.settings.reductionDb = -12.0f;
    project.settings.detector = "dsp";
    bc::studio::ProjectFile a;
    a.sourcePath = sourceA.u8string();
    a.relativeSource = "audio/spot_a.wav";
    a.status = "da_revisionare";
    a.events = 3;
    a.approved = 1;
    a.analyzed = true;
    a.sessionRelative = ".breathclick_sessions/1_spot_a.bcnstudio.json";
    project.files.push_back(a);
    bc::studio::ProjectFile b;
    b.sourcePath = sourceB.u8string();
    b.relativeSource = "audio/spot_b.wav";
    b.status = "approvato";
    b.events = 2;
    b.approved = 2;
    b.analyzed = true;
    project.files.push_back(b);

    const auto projectPath = root / "session.bcnproj.json";
    std::string error;
    if (!bc::studio::saveProjectFile(projectPath.u8string(), project, &error)) {
        std::cerr << "save failed: " << error << "\n";
        return 1;
    }
    bc::studio::ProjectData loaded;
    if (!bc::studio::loadProjectFile(projectPath.u8string(), &loaded, &error)) {
        std::cerr << "load failed: " << error << "\n";
        return 1;
    }
    assert(loaded.schema == 1);
    assert(loaded.files.size() == 2);
    assert(loaded.files[0].status == "da_revisionare");
    assert(loaded.files[0].events == 3);
    assert(loaded.files[0].approved == 1);
    assert(loaded.files[1].status == "approvato");
    assert(loaded.files[0].sourcePath == sourceA.u8string());
    assert(loaded.settings.detector == "dsp");

    // Riscrittura atomica: il file deve restare leggibile.
    loaded.files[0].status = "elaborato";
    loaded.files[0].exported = true;
    if (!bc::studio::saveProjectFile(projectPath.u8string(), loaded, &error)) {
        std::cerr << "rewrite failed: " << error << "\n";
        return 1;
    }
    bc::studio::ProjectData reloaded;
    if (!bc::studio::loadProjectFile(projectPath.u8string(), &reloaded, &error)) {
        std::cerr << "reload failed: " << error << "\n";
        return 1;
    }
    assert(reloaded.files[0].status == "elaborato");
    assert(reloaded.files[0].exported);

    // Adattatore REAPER: pura preparazione, nessuna DAW avviata.
    {
        bc::studio::EditPlan plan;
        plan.sampleRate = 48000;
        plan.markers = {{48000, 0, "intro", false},
                        {96000, 120000, "battuta", true}};
        bc::studio::EditResult result;
        result.voiceMappedMarkers = {{24000, 0, "intro", false},
                                     {72000, 96000, "battuta", true}};
        result.projectMappedMarkers = plan.markers;
        const auto voice = bc::studio::buildReaperTimelineMap(
            plan, result, bc::studio::ReaperTimelineScope::Voice);
        assert(voice.valid);
        assert(voice.markers.size() == 1);
        assert(std::abs(voice.markers[0].positionSeconds - 0.5) < 1e-9);
        const auto projectMap = bc::studio::buildReaperTimelineMap(
            plan, result, bc::studio::ReaperTimelineScope::Project);
        assert(projectMap.valid);
        assert(projectMap.regions.size() == 1);
        assert(std::abs(projectMap.regions[0].endSeconds - 2.5) < 1e-9);
    }

    std::filesystem::remove_all(root, ec);
    std::cout << "PROJECT TEST OK\n";
    return 0;
}

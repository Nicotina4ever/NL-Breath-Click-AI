// SPDX-License-Identifier: MPL-2.0
#pragma once

// ============================================================================
//  reaper_adapter.h — adattatore puro per marker e regioni REAPER
//
//  Non apre REAPER, non usa Lua e non modifica la DAW. Prende il piano e il
//  risultato prodotti dal motore condiviso e prepara le posizioni in secondi
//  per un futuro adapter. La differenza fra "voce" e "progetto" e' esplicita:
//
//    Voice   : output = voce compattata; i marker seguono la TimeMap.
//    Project : output = traccia voce; i marker restano ancorati alla timeline
//              originale del progetto, cosi' le altre tracce non si spostano.
//
//  La logica e' testabile con EditPlan/EditResult sintetici.
// ============================================================================
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "studio_engine.h"

namespace bc {
namespace studio {

enum class ReaperTimelineScope { Voice, Project };

struct ReaperTimelineItem {
    double positionSeconds = 0.0;
    double endSeconds = 0.0;
    std::string name;
    bool isRegion = false;
    bool clippedByEdit = false;
};

struct ReaperTimelineMap {
    ReaperTimelineScope scope = ReaperTimelineScope::Voice;
    std::vector<ReaperTimelineItem> markers;
    std::vector<ReaperTimelineItem> regions;
    bool valid = false;
    std::string note;
};

inline ReaperTimelineMap buildReaperTimelineMap(const EditPlan& plan,
                                                const EditResult& result,
                                                ReaperTimelineScope scope) {
    ReaperTimelineMap map;
    map.scope = scope;
    const int sampleRate = plan.sampleRate > 0 ? plan.sampleRate : result.audio.sampleRate;
    if (sampleRate <= 0) {
        map.note = "sample rate assente";
        return map;
    }
    const std::vector<TimelineMarker>& source =
        scope == ReaperTimelineScope::Voice ? result.voiceMappedMarkers
                                            : result.projectMappedMarkers;
    for (const TimelineMarker& marker : source) {
        ReaperTimelineItem item;
        item.positionSeconds = static_cast<double>(marker.timeSample) / sampleRate;
        item.endSeconds = static_cast<double>(marker.endSample) / sampleRate;
        item.name = marker.name;
        item.isRegion = marker.isRegion;
        item.clippedByEdit = item.name.find("[dentro taglio]") != std::string::npos;
        if (item.endSeconds < item.positionSeconds) item.endSeconds = item.positionSeconds;
        (item.isRegion ? map.regions : map.markers).push_back(item);
    }
    auto byPosition = [](const ReaperTimelineItem& a, const ReaperTimelineItem& b) {
        if (a.positionSeconds != b.positionSeconds) return a.positionSeconds < b.positionSeconds;
        return a.name < b.name;
    };
    std::stable_sort(map.markers.begin(), map.markers.end(), byPosition);
    std::stable_sort(map.regions.begin(), map.regions.end(), byPosition);
    map.valid = true;
    map.note = scope == ReaperTimelineScope::Voice
                   ? "posizioni sulla voce compattata; i marker dentro un taglio sono marcati"
                   : "posizioni sulla timeline originale; nessuno spostamento delle altre tracce";
    return map;
}

} // namespace studio
} // namespace bc

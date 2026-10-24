// SPDX-License-Identifier: MPL-2.0
#pragma once

#include <string>
#include <vector>

#include "studio_engine.h"

namespace bc {
namespace studio {

// Un file dentro un progetto multi-file. sourcePath e' il percorso assoluto
// risolto; relativeSource e' quello salvato nel progetto, se possibile.
struct ProjectFile {
    std::string sourcePath;
    std::string relativeSource;
    std::string sessionRelative;
    std::string outputRelative;
    std::string status = "da_analizzare";
    int events = 0;
    int approved = 0;
    bool analyzed = false;
    bool reviewed = false;
    bool exported = false;
    std::string error;
};

struct ProjectData {
    int schema = 1;
    std::string name;
    Settings settings;
    std::vector<ProjectFile> files;
};

// Scrive/legge il progetto in modo atomico (file temporaneo + rename).
bool saveProjectFile(const std::string& path, const ProjectData& project,
                     std::string* error = nullptr);
bool loadProjectFile(const std::string& path, ProjectData* project,
                     std::string* error = nullptr);

// Percorso della sessione interna per un file del progetto.
std::string projectSessionPath(const std::string& projectPath, size_t index,
                               const std::string& sourcePath);

// Crea una sessione .bcnstudio.json accanto al progetto, se possibile.
std::string makeProjectSessionRelative(const std::string& projectPath,
                                       size_t index, const std::string& sourcePath);

} // namespace studio
} // namespace bc

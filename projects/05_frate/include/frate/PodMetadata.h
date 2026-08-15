#pragma once

#include <string>
#include <vector>
#include <optional>

namespace frate {

struct PodDependency {
    std::string name;
    std::string version;
};

struct PodMetadata {
    std::string name;
    std::string version;
    std::string type; // "bin" or "lib"
    std::string description;
    std::vector<std::string> exports;
    std::vector<PodDependency> dependencies;
    
    // Workspace support
    bool isWorkspace = false;
    std::vector<std::string> workspaceMembers;
};

} // namespace frate

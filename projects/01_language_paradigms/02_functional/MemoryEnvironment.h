#pragma once

// An in-memory HostEnvironment: files in a map, log lines in a vector. What the
// tests use, and a reference for what an environment has to do.

#include "HostEnvironment.h"

#include <map>
#include <string>
#include <vector>

namespace frust {

class MemoryFileSystem final : public FileSystem {
public:
    std::map<std::string, std::string> files; // normalized path -> bytes

    bool exists(const std::string& path) override {
        std::string p;
        return NormalizePath(path, p) && files.count(p) == 1;
    }

    bool read(const std::string& path, std::string& bytes) override {
        std::string p;
        if (!NormalizePath(path, p)) return false;
        const auto found = files.find(p);
        if (found == files.end()) return false;
        bytes = found->second;
        return true;
    }

    bool write(const std::string& path, const std::string& bytes) override {
        std::string p;
        if (!NormalizePath(path, p) || p.empty()) return false;
        files[p] = bytes;
        return true;
    }

    bool remove(const std::string& path) override {
        std::string p;
        return NormalizePath(path, p) && files.erase(p) == 1;
    }

    bool listFiles(const std::string& directory, std::vector<std::string>& relativePaths) override {
        std::string dir;
        if (!NormalizePath(directory, dir)) return false;
        const std::string prefix = dir.empty() ? std::string() : dir + "/";
        relativePaths.clear();
        for (const auto& [path, bytes] : files)
            if (path.compare(0, prefix.size(), prefix) == 0) relativePaths.push_back(path.substr(prefix.size()));
        return true;
    }
};

struct LogLine {
    LogLevel level;
    std::string source;
    std::string message;
};

class VectorLogSink final : public LogSink {
public:
    std::vector<LogLine> lines;

    void log(LogLevel level, const std::string& source, const std::string& message) override {
        lines.push_back({ level, source, message });
    }

    bool contains(const std::string& text) const {
        for (const auto& line : lines)
            if (line.message.find(text) != std::string::npos) return true;
        return false;
    }
};

// Both together, for tests: `MemoryEnvironment env; frust::compileFiles(env.environment(), ...)`.
struct MemoryEnvironment {
    MemoryFileSystem fileSystem;
    VectorLogSink logSink;

    HostEnvironment environment() { return HostEnvironment{ fileSystem, logSink }; }
};

} // namespace frust

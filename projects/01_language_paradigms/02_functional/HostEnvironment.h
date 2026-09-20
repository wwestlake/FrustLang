#pragma once

// The one door between the FRust libraries and the outside world.
//
// The compiler (frust_lang) and Frate (frate_core) do ALL of their reading,
// writing and logging through a HostEnvironment. They never open a file, list a
// directory, print to the console or start a program on their own. The host
// decides what a "file" is:
//
//   * the command-line tools give them the real disk and the console;
//   * the Djehuti Suite gives them the VFS and VFS log entries;
//   * tests give them memory.
//
// Paths are virtual: '/'-separated, relative to the environment's own root, no
// drive letters and no "..". What a path means is the environment's business.

#include <string>
#include <vector>

namespace frust {

class FileSystem {
public:
    virtual ~FileSystem() = default;

    virtual bool exists(const std::string& path) = 0;

    // Reads the whole file. False if there is no such file.
    virtual bool read(const std::string& path, std::string& bytes) = 0;

    // Writes the whole file, creating it (and any directory it needs) or replacing it.
    virtual bool write(const std::string& path, const std::string& bytes) = 0;

    virtual bool remove(const std::string& path) = 0;

    // Every file at or below `directory`, as paths relative to `directory`
    // ('/' separators). An empty result is not an error: the directory may simply be empty
    // or absent.
    virtual bool listFiles(const std::string& directory, std::vector<std::string>& relativePaths) = 0;
};

enum class LogLevel { Info, Warning, Error };

class LogSink {
public:
    virtual ~LogSink() = default;

    // `source` names the part of FRust speaking ("frust", "frate"). One call per line.
    virtual void log(LogLevel level, const std::string& source, const std::string& message) = 0;
};

struct HostEnvironment {
    FileSystem& files;
    LogSink& log;
};

// ---- Virtual path helpers (pure string work; no environment involved) ----

// "a/b" + "c" -> "a/b/c". Empty parts are skipped; a leading "/" on `tail` is dropped.
inline std::string JoinPath(const std::string& head, const std::string& tail) {
    if (head.empty()) return tail;
    if (tail.empty()) return head;
    std::string result = head;
    if (result.back() != '/') result += '/';
    result += (tail.front() == '/') ? tail.substr(1) : tail;
    return result;
}

// "a/b/c.fr" -> "a/b"; "c.fr" -> "".
inline std::string DirectoryOf(const std::string& path) {
    const auto slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

// "a/b/c.fr" -> "c.fr".
inline std::string FileNameOf(const std::string& path) {
    const auto slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// Backslashes become '/', "." segments and doubled slashes are dropped. A path that
// climbs out with ".." (or is absolute or has a drive letter) is refused: returns false.
inline bool NormalizePath(const std::string& in, std::string& out) {
    std::string result, segment;
    bool first = true;
    for (size_t i = 0; i <= in.size(); ++i) {
        const char c = i < in.size() ? in[i] : '/';
        if (c == '/' || c == '\\') {
            if (segment == "..") return false;
            if (first && segment.empty() && i < in.size()) return false;           // absolute
            if (first && segment.size() == 2 && segment[1] == ':') return false;    // drive letter
            if (!segment.empty() && segment != ".") {
                if (!result.empty()) result += '/';
                result += segment;
            }
            segment.clear();
            first = false;
        } else {
            segment += c;
        }
    }
    out = result;
    return true;
}

} // namespace frust

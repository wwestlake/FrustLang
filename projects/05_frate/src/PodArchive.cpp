#include <frate/PodArchive.h>

#include <frate/PodMetadata.h>
#include "PodMetadataJson.h"

#include <juce_core/juce_core.h>

namespace frate {
namespace {

bool hasSourceExtension(const std::string& path) {
    const auto endsWith = [&path](const char* suffix) {
        const std::string s(suffix);
        return path.size() >= s.size() && path.compare(path.size() - s.size(), s.size(), s) == 0;
    };
    return endsWith(".fr") || endsWith(".fri");
}

bool safeRelativePath(const std::string& path) {
    if (path.empty() || path.front() == '/' || path.front() == '\\') return false;
    if (path.size() > 1 && path[1] == ':') return false; // drive letter
    std::string segment;
    for (const char c : path + "/") {
        if (c == '/' || c == '\\') {
            if (segment == "..") return false;
            segment.clear();
        } else {
            segment += c;
        }
    }
    return true;
}

} // namespace

bool packPod(const PodFiles& files, std::string& frpodBytes, std::string& error) {
    const auto manifest = files.find("frate.json");
    if (manifest == files.end()) {
        error = "the pod has no frate.json";
        return false;
    }

    juce::ZipFile::Builder builder;
    // A fixed time keeps the package bytes the same for the same content.
    const juce::Time fixed(2020, 0, 1, 0, 0, 0, 0, false);
    const auto add = [&builder, &fixed](const std::string& path, const std::string& contents) {
        builder.addEntry(new juce::MemoryInputStream(contents.data(), contents.size(), true), 9,
                         juce::String(path), fixed);
    };

    add("frate.json", manifest->second);
    for (const auto& [path, contents] : files) // std::map: sorted, so deterministic
        if (path != "frate.json" && hasSourceExtension(path)) add(path, contents);

    juce::MemoryOutputStream out;
    if (!builder.writeToStream(out, nullptr)) {
        error = "could not write the package";
        return false;
    }
    frpodBytes.assign(static_cast<const char*>(out.getData()), out.getDataSize());
    return true;
}

bool unpackPod(const void* data, std::size_t size, PodFiles& files, std::string& error) {
    auto input = std::make_unique<juce::MemoryInputStream>(data, size, false);
    juce::ZipFile zip(input.release(), true);
    if (zip.getNumEntries() <= 0) {
        error = "the package is empty or is not a zip";
        return false;
    }

    PodFiles result;
    for (int i = 0; i < zip.getNumEntries(); ++i) {
        const auto* entry = zip.getEntry(i);
        if (entry == nullptr) continue;
        const std::string path = entry->filename.replaceCharacter('\\', '/').toStdString();
        if (path.empty() || path.back() == '/') continue; // a directory entry
        if (!safeRelativePath(path)) {
            error = "the package holds an unsafe path: " + path;
            return false;
        }
        std::unique_ptr<juce::InputStream> stream(zip.createStreamForEntry(i));
        if (stream == nullptr) {
            error = "could not read " + path + " from the package";
            return false;
        }
        juce::MemoryBlock block;
        stream->readIntoMemoryBlock(block);
        result[path].assign(static_cast<const char*>(block.getData()), block.getSize());
    }

    if (result.find("frate.json") == result.end()) {
        error = "the package has no frate.json";
        return false;
    }
    files = std::move(result);
    return true;
}

PodFiles scaffoldPodFiles(const PodMetadata& metadata) {
    PodFiles files;
    files["frate.json"] = PodMetadataJson::toJson(metadata).isVoid()
        ? std::string()
        : juce::JSON::toString(PodMetadataJson::toJson(metadata)).toStdString();

    std::string entry = "// Frust pod: " + metadata.name + "\n";
    if (metadata.type != "lib") entry += "\nfn main() -> i64 = {\n    0\n}\n";
    files[metadata.type == "lib" ? "src/lib.fr" : "src/main.fr"] = entry;
    return files;
}

} // namespace frate

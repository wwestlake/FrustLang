#include <frate/FratePodBuilder.h>
#include "../src/PodMetadataJson.h"

namespace frate {

bool FratePodBuilder::scaffoldPod(const juce::File& targetDir, const PodMetadata& metadata) {
    if (!targetDir.exists()) {
        if (!targetDir.createDirectory()) return false;
    }
    
    juce::File podJson = targetDir.getChildFile("frate.json");
    juce::var jsonVar = PodMetadataJson::toJson(metadata);
    juce::String jsonStr = juce::JSON::toString(jsonVar);
    
    if (!podJson.replaceWithText(jsonStr)) return false;
    
    juce::File srcDir = targetDir.getChildFile("src");
    if (!srcDir.exists()) {
        if (!srcDir.createDirectory()) return false;
    }
    
    juce::String entryFilename = (metadata.type == "lib") ? "lib.fr" : "main.fr";
    juce::File entryFile = srcDir.getChildFile(entryFilename);
    if (!entryFile.existsAsFile()) {
        juce::String initialContent = "// Frust pod: " + metadata.name + "\n";
        if (metadata.type != "lib") {
            initialContent += "\nfn main() -> i64 = {\n    0\n}\n";
        }
        if (!entryFile.replaceWithText(initialContent)) return false;
    }
    
    return true;
}

juce::File FratePodBuilder::packagePod(const juce::File& podDir) {
    juce::File podJson = podDir.getChildFile("frate.json");
    if (!podJson.existsAsFile()) return juce::File();
    
    auto jsonVar = juce::JSON::parse(podJson);
    PodMetadata meta = PodMetadataJson::fromJson(jsonVar);
    if (meta.name.empty() || meta.version.empty()) return juce::File();
    
    juce::String frpodName = meta.name + "-" + meta.version + ".frpod";
    juce::File frpodFile = podDir.getParentDirectory().getChildFile(frpodName);
    
    frpodFile.deleteFile();
    
    juce::ZipFile::Builder builder;
    
    // Add frate.json
    builder.addFile(podJson, 9, "frate.json");
    
    // Add all .fr and .fri files recursively
    juce::Array<juce::File> sourceFiles;
    podDir.findChildFiles(sourceFiles, juce::File::findFiles, true, "*.fr;*.fri");
    
    for (const auto& file : sourceFiles) {
        juce::String relativePath = file.getRelativePathFrom(podDir);
        builder.addFile(file, 9, relativePath);
    }
    
    std::unique_ptr<juce::FileOutputStream> outStream = frpodFile.createOutputStream();
    if (outStream) {
        if (builder.writeToStream(*outStream, nullptr)) {
            return frpodFile;
        }
    }
    
    return juce::File();
}

} // namespace frate

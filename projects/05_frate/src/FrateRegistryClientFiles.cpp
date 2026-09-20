#include <frate/FrateRegistryClient.h>
#include "../src/PodMetadataJson.h"

namespace frate {

// The file halves of the registry client: uploading a package from, and downloading one to, a real
// file. Their own object file so an application that keeps pods in memory (downloadToMemory) does
// not link them.

bool FrateRegistryClient::uploadToS3(const juce::String& presignedUrl, const juce::File& frpodFile) {
    juce::URL url(presignedUrl);

    juce::MemoryBlock block;
    frpodFile.loadFileAsData(block);
    url = url.withPOSTData(block);

    int statusCode = 0;
    std::unique_ptr<juce::InputStream> stream(url.createInputStream(
        juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
            .withHttpRequestCmd("PUT")
            .withStatusCode(&statusCode)
    ));

    return statusCode == 200;
}

bool FrateRegistryClient::downloadFromS3(const juce::String& presignedUrl, const juce::File& targetFile) {
    juce::URL url(presignedUrl);
    
    int statusCode = 0;
    std::unique_ptr<juce::InputStream> stream(url.createInputStream(
        juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
            .withStatusCode(&statusCode)
    ));
    
    if (stream && statusCode == 200) {
        targetFile.deleteFile();
        std::unique_ptr<juce::FileOutputStream> outStream = targetFile.createOutputStream();
        if (outStream) {
            outStream->writeFromInputStream(*stream, -1);
            return true;
        }
    }
    
    return false;
}

} // namespace frate

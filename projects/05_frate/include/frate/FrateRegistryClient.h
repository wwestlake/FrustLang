#pragma once

#include <juce_core/juce_core.h>
#include <string>
#include <vector>
#include <frate/PodMetadata.h>

namespace frate {

struct RegistrySearchResult {
    std::string name;
    std::string description;
    std::string latestVersion;
    std::string license;
    std::vector<std::string> exports;
};

class FrateRegistryClient {
public:
    FrateRegistryClient(const juce::String& baseUrl = "https://lagdaemon.com/djehuti/api/frate");

    void setAuthToken(const juce::String& token);

    // GET {baseUrl}/pods?q={substring}
    std::vector<RegistrySearchResult> searchPods(const juce::String& query);

    // GET {baseUrl}/pods/{name}/{version}
    // Returns the download URL (presigned S3 GET URL) or empty string on failure.
    juce::String getDownloadUrl(const std::string& name, const std::string& version);

    // POST {baseUrl}/pods/{name}/{version}/upload-url
    // Returns { presignedUrl, s3Key } or empty strings on failure.
    std::pair<juce::String, juce::String> getUploadUrl(const std::string& name, const std::string& version);

    // POST {baseUrl}/pods/{name}/{version}
    bool publishPod(const PodMetadata& metadata, const juce::String& s3Key, int64_t sizeBytes, const juce::String& license);

    // Upload raw bytes to the presigned S3 URL
    bool uploadToS3(const juce::String& presignedUrl, const juce::File& frpodFile);

    // Download raw bytes from the presigned S3 URL
    bool downloadFromS3(const juce::String& presignedUrl, const juce::File& targetFile);

private:
    juce::String baseUrl;
    juce::String authToken;
};

} // namespace frate

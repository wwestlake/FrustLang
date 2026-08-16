#include <frate/FrateRegistryClient.h>
#include "../src/PodMetadataJson.h"

namespace frate {

FrateRegistryClient::FrateRegistryClient(const juce::String& url) : baseUrl(url) {}

void FrateRegistryClient::setAuthToken(const juce::String& token) {
    authToken = token;
}

std::vector<RegistrySearchResult> FrateRegistryClient::searchPods(const juce::String& query) {
    std::vector<RegistrySearchResult> results;
    
    juce::URL url(baseUrl + "/pods");
    if (query.isNotEmpty()) {
        url = url.withParameter("q", query);
    }
    
    juce::String responseStr = url.readEntireTextStream(false);
    auto jsonVar = juce::JSON::parse(responseStr);
    
    if (jsonVar.isArray()) {
        auto* arr = jsonVar.getArray();
        for (auto& item : *arr) {
            if (item.isObject()) {
                auto* obj = item.getDynamicObject();
                RegistrySearchResult r;
                r.name = obj->getProperty("name").toString().toStdString();
                r.description = obj->getProperty("description").toString().toStdString();
                r.latestVersion = obj->getProperty("latestVersion").toString().toStdString();
                r.license = obj->getProperty("license").toString().toStdString();
                
                if (obj->hasProperty("exports") && obj->getProperty("exports").isArray()) {
                    auto* expArr = obj->getProperty("exports").getArray();
                    for (auto& e : *expArr) {
                        r.exports.push_back(e.toString().toStdString());
                    }
                }
                
                results.push_back(r);
            }
        }
    }
    
    return results;
}

juce::String FrateRegistryClient::getDownloadUrl(const std::string& name, const std::string& version) {
    juce::URL url(baseUrl + "/pods/" + name + "/" + version);
    
    int statusCode = 0;
    std::unique_ptr<juce::InputStream> stream(url.createInputStream(
        juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
            .withConnectionTimeoutMs(10000)
            .withNumRedirectsToFollow(0)
            .withStatusCode(&statusCode)
    ));
    
    if (statusCode == 302 || statusCode == 301 || statusCode == 303 || statusCode == 307) {
        if (auto* webStream = dynamic_cast<juce::WebInputStream*>(stream.get())) {
            return webStream->getResponseHeaders().getValue("Location", "");
        }
    }
    
    return "";
}

std::pair<juce::String, juce::String> FrateRegistryClient::getUploadUrl(const std::string& name, const std::string& version) {
    juce::URL url(baseUrl + "/pods/" + name + "/" + version + "/upload-url");
    
    int statusCode = 0;
    std::unique_ptr<juce::InputStream> stream(url.createInputStream(
        juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
            .withHttpRequestCmd("POST")
            .withExtraHeaders("Authorization: Bearer " + authToken)
            .withStatusCode(&statusCode)
    ));
    
    if (stream && statusCode == 200) {
        juce::String responseStr = stream->readEntireStreamAsString();
        auto jsonVar = juce::JSON::parse(responseStr);
        if (jsonVar.isObject()) {
            auto* obj = jsonVar.getDynamicObject();
            return {
                obj->getProperty("presignedUrl").toString(),
                obj->getProperty("s3Key").toString()
            };
        }
    }
    
    return {"", ""};
}

bool FrateRegistryClient::publishPod(const PodMetadata& metadata, const juce::String& s3Key, int64_t sizeBytes, const juce::String& license) {
    juce::URL url(baseUrl + "/pods/" + metadata.name + "/" + metadata.version);
    
    auto* obj = new juce::DynamicObject();
    obj->setProperty("description", juce::String(metadata.description));
    obj->setProperty("s3Key", s3Key);
    // int64_t is `long` on Linux/AArch64 but `long long` on Windows - only
    // the latter matches juce::var's juce::int64 (always `long long`)
    // overload unambiguously, so explicitly cast rather than relying on
    // int64_t to always mean the same thing as juce::int64.
    obj->setProperty("sizeBytes", static_cast<juce::int64>(sizeBytes));
    obj->setProperty("license", license);
    
    juce::Array<juce::var> exportsArray;
    for (const auto& exp : metadata.exports) {
        exportsArray.add(juce::var(juce::String(exp)));
    }
    obj->setProperty("exports", exportsArray);
    
    juce::Array<juce::var> depsArray;
    for (const auto& dep : metadata.dependencies) {
        depsArray.add(PodMetadataJson::toJson(dep));
    }
    obj->setProperty("dependencies", depsArray);
    
    juce::String jsonBody = juce::JSON::toString(juce::var(obj));
    url = url.withPOSTData(jsonBody);
    
    int statusCode = 0;
    std::unique_ptr<juce::InputStream> stream(url.createInputStream(
        juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
            .withHttpRequestCmd("POST")
            .withExtraHeaders("Authorization: Bearer " + authToken + "\nContent-Type: application/json")
            .withStatusCode(&statusCode)
    ));
    
    return statusCode == 200;
}

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

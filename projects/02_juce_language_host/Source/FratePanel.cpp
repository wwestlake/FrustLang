#include "FratePanel.h"
#include <frate/FrateCache.h>
#include <frate/FrateResolver.h>
#include <frate/FratePodBuilder.h>
#include "../../05_frate/src/PodMetadataJson.h"

//==============================================================================
// ConsumerView
//==============================================================================

ConsumerView::ConsumerView()
{
    addAndMakeVisible(dependencyList);
    addAndMakeVisible(addLabel);
    
    nameInput.setTextToShowWhenEmpty("Pod Name", juce::Colours::grey);
    addAndMakeVisible(nameInput);
    
    versionInput.setTextToShowWhenEmpty("Version (e.g. 1.0.0)", juce::Colours::grey);
    addAndMakeVisible(versionInput);
    
    addButton.onClick = [this] { addDependency(); };
    addAndMakeVisible(addButton);
    
    resolveButton.onClick = [this] { resolveAll(); };
    addAndMakeVisible(resolveButton);
}

ConsumerView::~ConsumerView() {}

void ConsumerView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff252526));
}

void ConsumerView::resized()
{
    auto bounds = getLocalBounds().reduced(8);
    
    auto topRow = bounds.removeFromTop(24);
    addLabel.setBounds(topRow.removeFromLeft(100));
    nameInput.setBounds(topRow.removeFromLeft(120).reduced(2));
    versionInput.setBounds(topRow.removeFromLeft(100).reduced(2));
    addButton.setBounds(topRow.removeFromLeft(60).reduced(2));
    
    bounds.removeFromTop(8); // spacing
    
    auto bottomRow = bounds.removeFromBottom(30);
    resolveButton.setBounds(bottomRow.withSizeKeepingCentre(120, 24));
    
    dependencyList.setBounds(bounds);
}

void ConsumerView::setProjectRoot(const juce::File& root)
{
    projectRoot = root;
    reloadConfig();
}

void ConsumerView::reloadConfig()
{
    if (projectRoot.exists() && projectRoot.isDirectory()) {
        juce::File configPath = projectRoot.getChildFile("frate.json");
        currentConfig.load(configPath);
    }
    
    dependencyList.updateContent();
}

int ConsumerView::getNumRows()
{
    return (int)currentConfig.getDependencies().size();
}

void ConsumerView::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    if (rowNumber < 0 || rowNumber >= getNumRows()) return;
    
    if (rowIsSelected)
        g.fillAll(juce::Colours::lightblue.withAlpha(0.2f));
        
    auto& deps = currentConfig.getDependencies();
    auto& dep = deps[(size_t)rowNumber];
    
    juce::String text = juce::String(dep.name) + "  v" + juce::String(dep.version);
    
    auto it = resolveStatuses.find(dep.name);
    if (it != resolveStatuses.end()) {
        text += " [" + it->second + "]";
    } else {
        frate::FrateCache cache;
        bool cached = cache.isCached(dep.name, dep.version);
        text += cached ? " [Cached]" : " [Unresolved]";
    }
    
    g.setColour(juce::Colours::white);
    g.setFont(14.0f);
    g.drawText(text, 4, 0, width - 8, height, juce::Justification::centredLeft, true);
}

void ConsumerView::addDependency()
{
    auto name = nameInput.getText().trim();
    auto version = versionInput.getText().trim();
    
    if (name.isNotEmpty() && version.isNotEmpty() && projectRoot.exists()) {
        frate::PodDependency dep;
        dep.name = name.toStdString();
        dep.version = version.toStdString();
        
        currentConfig.addDependency(dep);
        currentConfig.save(projectRoot.getChildFile("frate.json"));
        
        nameInput.clear();
        versionInput.clear();
        reloadConfig();
    }
}

void ConsumerView::resolveAll()
{
    frate::FrateCache cache;
    frate::FrateResolver resolver(cache, registryClient);
    
    auto results = resolver.resolveAll(currentConfig);
    
    resolveStatuses.clear();
    for (const auto& pair : results) {
        juce::String statusStr;
        switch (pair.second) {
            case frate::ResolveStatus::ResolvedFromCache: statusStr = "Cached"; break;
            case frate::ResolveStatus::ResolvedFromRegistry: statusStr = "Fetched"; break;
            case frate::ResolveStatus::UnresolvedNotFound: statusStr = "Not Found 404"; break;
            case frate::ResolveStatus::UnresolvedNetworkError: statusStr = "Network Error"; break;
            case frate::ResolveStatus::UnresolvedExtractError: statusStr = "Extract Error"; break;
        }
        resolveStatuses[pair.first] = statusStr;
    }
    
    dependencyList.updateContent();
}

//==============================================================================
// ProducerView
//==============================================================================

ProducerView::ProducerView(DesktopAuthSession* session) 
    : authSession(session)
{
    addAndMakeVisible(createLabel);
    
    nameInput.setTextToShowWhenEmpty("Pod Name", juce::Colours::grey);
    addAndMakeVisible(nameInput);
    
    versionInput.setTextToShowWhenEmpty("Version", juce::Colours::grey);
    addAndMakeVisible(versionInput);
    
    descInput.setTextToShowWhenEmpty("Description", juce::Colours::grey);
    addAndMakeVisible(descInput);
    
    createButton.onClick = [this] { createPod(); };
    addAndMakeVisible(createButton);
    
    addAndMakeVisible(actionsLabel);
    
    packageButton.onClick = [this] { packagePod(); };
    addAndMakeVisible(packageButton);
    
    installButton.onClick = [this] { installToCache(); };
    addAndMakeVisible(installButton);
    
    publishButton.onClick = [this] { publishToRegistry(); };
    publishButton.setColour(juce::TextButton::buttonColourId, juce::Colours::maroon);
    addAndMakeVisible(publishButton);
}

ProducerView::~ProducerView() {}

void ProducerView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff252526));
    
    g.setColour(juce::Colours::darkgrey);
    g.drawHorizontalLine(140, 10, getWidth() - 10);
}

void ProducerView::resized()
{
    auto bounds = getLocalBounds().reduced(16);
    
    auto createArea = bounds.removeFromTop(100);
    createLabel.setBounds(createArea.removeFromTop(24));
    
    auto row1 = createArea.removeFromTop(28);
    nameInput.setBounds(row1.removeFromLeft(150).reduced(2));
    versionInput.setBounds(row1.removeFromLeft(100).reduced(2));
    
    auto row2 = createArea.removeFromTop(28);
    descInput.setBounds(row2.removeFromLeft(250).reduced(2));
    
    createButton.setBounds(createArea.removeFromTop(28).withWidth(120).reduced(2));
    
    bounds.removeFromTop(40); // spacing past line
    
    actionsLabel.setBounds(bounds.removeFromTop(24));
    packageButton.setBounds(bounds.removeFromTop(28).withWidth(200).reduced(2));
    installButton.setBounds(bounds.removeFromTop(28).withWidth(200).reduced(2));
    bounds.removeFromTop(10);
    publishButton.setBounds(bounds.removeFromTop(28).withWidth(200).reduced(2));
}

void ProducerView::setProjectRoot(const juce::File& root)
{
    projectRoot = root;
}

void ProducerView::createPod()
{
    if (!projectRoot.exists()) return;
    
    frate::PodMetadata meta;
    meta.name = nameInput.getText().trim().toStdString();
    meta.version = versionInput.getText().trim().toStdString();
    meta.description = descInput.getText().trim().toStdString();
    
    if (meta.name.empty() || meta.version.empty()) return;
    
    auto targetDir = projectRoot.getChildFile(juce::String(meta.name));
    if (frate::FratePodBuilder::scaffoldPod(targetDir, meta)) {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "Success", 
            "Created new pod scaffold in " + meta.name + "/");
            
        if (onFileSystemChanged) onFileSystemChanged();
    } else {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Error", "Failed to create pod directory.");
    }

    nameInput.clear();
    versionInput.clear();
    descInput.clear();
}

void ProducerView::packagePod()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Select Pod Folder to Package...",
        projectRoot,
        "*");

    fileChooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [this](const juce::FileChooser& fc) {
            auto podDir = fc.getResult();
            if (podDir.isDirectory()) {
                auto podJson = podDir.getChildFile("frate.json");
                if (!podJson.existsAsFile()) {
                    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Error", "Selected directory is not a valid pod (missing frate.json).");
                    return;
                }
                
                auto json = juce::JSON::parse(podJson);
                frate::PodMetadata meta = frate::PodMetadataJson::fromJson(json);
                if (meta.name.empty() || meta.version.empty()) {
                    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Error", "Invalid pod.json.");
                    return;
                }
                
                juce::File frpod = frate::FratePodBuilder::packagePod(podDir);
                if (frpod.existsAsFile()) {
                    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "Success",
                        "Packaged pod to " + frpod.getFileName());
                    if (onFileSystemChanged) onFileSystemChanged();
                } else {
                    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Error", "Packaging failed.");
                }
            }
            fileChooser = nullptr;
        });
}

void ProducerView::installToCache()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Select .frpod to Install...",
        projectRoot,
        "*.frpod");

    fileChooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc) {
            auto outFile = fc.getResult();
            if (outFile.existsAsFile()) {
                // `FrateCache::installFromPackage` requires name and version; parse them
                // from the filename (format: name-version.frpod) rather than opening the
                // zip to read frate.json.
                juce::String filename = outFile.getFileNameWithoutExtension();
                int lastDash = filename.lastIndexOfChar('-');
                if (lastDash > 0) {
                    juce::String name = filename.substring(0, lastDash);
                    juce::String version = filename.substring(lastDash + 1);
                    
                    frate::FrateCache cache;
                    if (cache.installFromPackage(outFile, name.toStdString(), version.toStdString())) {
                        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "Success", "Installed to machine-wide cache.");
                    } else {
                        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Error", "Install failed.");
                    }
                } else {
                    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Error", "Could not parse name and version from filename. Ensure format is name-version.frpod");
                }
            }
            fileChooser = nullptr;
        });
}

void ProducerView::publishToRegistry()
{
    if (!authSession || !authSession->hasValidSession()) {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Auth Required", "Please sign in from the Account menu first.");
        return;
    }
    
    fileChooser = std::make_unique<juce::FileChooser>(
        "Select .frpod to Publish...",
        projectRoot,
        "*.frpod");

    fileChooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc) {
            auto outFile = fc.getResult();
            if (outFile.existsAsFile()) {
                juce::String filename = outFile.getFileNameWithoutExtension();
                int lastDash = filename.lastIndexOfChar('-');
                if (lastDash > 0) {
                    juce::String name = filename.substring(0, lastDash);
                    juce::String version = filename.substring(lastDash + 1);
                    
                    frate::PodMetadata meta;
                    meta.name = name.toStdString();
                    meta.version = version.toStdString();
                    
                    std::string token = authSession->getSession().token.toStdString();
                    registryClient.setAuthToken(token);
                    
                    auto [presignedUrl, s3Key] = registryClient.getUploadUrl(meta.name, meta.version);
                    if (presignedUrl.isEmpty()) {
                        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Error", "Failed to get upload URL. Do you have publisher role?");
                        fileChooser = nullptr;
                        return;
                    }
                    
                    if (!registryClient.uploadToS3(presignedUrl, outFile)) {
                        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Error", "Failed to upload to S3.");
                        fileChooser = nullptr;
                        return;
                    }
                    
                    juce::String defaultLicense = "MIT";
                    
                    if (registryClient.publishPod(meta, s3Key, outFile.getSize(), defaultLicense)) {
                        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "Success", "Pod published successfully to the registry!");
                    } else {
                        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Error", "Failed to publish metadata. Check if version already exists.");
                    }
                } else {
                    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Error", "Could not parse name and version from filename. Ensure format is name-version.frpod");
                }
            }
            fileChooser = nullptr;
        });
}

//==============================================================================
// FratePanel
//==============================================================================

FratePanel::FratePanel(DesktopAuthSession* session)
{
    consumerView = std::make_unique<ConsumerView>();
    producerView = std::make_unique<ProducerView>(session);
    
    producerView->onFileSystemChanged = [this] {
        if (onFileSystemChanged) onFileSystemChanged();
    };

    tabs.addTab("Consumer (Use Pods)", juce::Colours::transparentBlack, consumerView.get(), false);
    tabs.addTab("Producer (Make Pods)", juce::Colours::transparentBlack, producerView.get(), false);
    
    addAndMakeVisible(tabs);
}

FratePanel::~FratePanel() {}

void FratePanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1e1e1e));
}

void FratePanel::resized()
{
    tabs.setBounds(getLocalBounds());
}

void FratePanel::setProjectRoot(const juce::File& root)
{
    if (consumerView) consumerView->setProjectRoot(root);
    if (producerView) producerView->setProjectRoot(root);
}

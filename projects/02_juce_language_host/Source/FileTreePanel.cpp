#include "FileTreePanel.h"

FileTreePanel::FileTreePanel()
{
    headerLabel.setFont(juce::Font(14.0f, juce::Font::bold));
    headerLabel.setColour(juce::Label::textColourId, juce::Colours::orange);
    addAndMakeVisible(headerLabel);

    openFolderButton.onClick = [this] {
        auto chooser = std::make_shared<juce::FileChooser>(
            "Select Root Directory for Project Explorer...",
            juce::File::getCurrentWorkingDirectory(),
            "*"
        );
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [this, chooser](const juce::FileChooser& fc) {
                auto file = fc.getResult();
                if (file.isDirectory()) {
                    setRootDirectory(file);
                }
            }
        );
    };
    addAndMakeVisible(openFolderButton);

    thread = std::make_unique<juce::TimeSliceThread>("FileTreeScanner");
    thread->startThread();

    // Default to workspace root
    setRootDirectory(juce::File::getCurrentWorkingDirectory());
}

FileTreePanel::~FileTreePanel()
{
    if (fileTree) fileTree->removeListener(this);
    fileTree = nullptr;
    if (thread) thread->stopThread(1000);
    directoryContents = nullptr;
}

void FileTreePanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1e1e1e));
    g.setColour(juce::Colour(0xff333333));
    g.drawRect(getLocalBounds(), 1);
}

void FileTreePanel::resized()
{
    auto bounds = getLocalBounds().reduced(6);

    auto topBar = bounds.removeFromTop(24);
    openFolderButton.setBounds(topBar.removeFromRight(100));
    headerLabel.setBounds(topBar);

    bounds.removeFromTop(6);

    if (fileTree) {
        fileTree->setBounds(bounds);
    }
}

void FileTreePanel::setRootDirectory(const juce::File& dir)
{
    if (fileTree) {
        fileTree->removeListener(this);
        fileTree = nullptr;
    }

    currentRoot = dir;
    headerLabel.setText("Root: " + dir.getFileName(), juce::dontSendNotification);

    directoryContents = std::make_unique<juce::DirectoryContentsList>(nullptr, *thread);
    directoryContents->setDirectory(dir, true, true);

    fileTree = std::make_unique<juce::FileTreeComponent>(*directoryContents);
    fileTree->setColour(juce::FileTreeComponent::backgroundColourId, juce::Colour(0xff181818));
    fileTree->addListener(this);

    addAndMakeVisible(*fileTree);
    resized();
    
    if (onRootDirectoryChanged) {
        onRootDirectoryChanged(currentRoot);
    }
}

void FileTreePanel::refresh()
{
    if (directoryContents) {
        directoryContents->refresh();
    }
}

void FileTreePanel::fileDoubleClicked(const juce::File& file)
{
    if (file.existsAsFile() && onFileDoubleClicked) {
        onFileDoubleClicked(file);
    }
}

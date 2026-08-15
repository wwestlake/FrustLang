#pragma once

#include <JuceHeader.h>
#include <functional>

class FileTreePanel : public juce::Component,
                      public juce::FileBrowserListener
{
public:
    FileTreePanel();
    ~FileTreePanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void setRootDirectory(const juce::File& dir);
    juce::File getRootDirectory() const { return currentRoot; }
    void refresh();

    // juce::FileBrowserListener Overrides
    void selectionChanged() override {}
    void fileClicked(const juce::File& file, const juce::MouseEvent& e) override {}
    void fileDoubleClicked(const juce::File& file) override;
    void browserRootChanged(const juce::File& newRoot) override {}

    std::function<void(const juce::File&)> onFileDoubleClicked;
    std::function<void(const juce::File&)> onRootDirectoryChanged;

private:
    juce::Label headerLabel { "Header", "Project Explorer" };
    juce::TextButton openFolderButton { "Open Folder..." };
    juce::File currentRoot;

    std::unique_ptr<juce::TimeSliceThread> thread;
    std::unique_ptr<juce::DirectoryContentsList> directoryContents;
    std::unique_ptr<juce::FileTreeComponent> fileTree;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FileTreePanel)
};

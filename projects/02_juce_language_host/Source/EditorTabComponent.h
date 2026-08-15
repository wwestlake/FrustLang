#pragma once

#include <JuceHeader.h>
#include "FrustTokeniser.h"

class CodeEditorTab : public juce::Component,
                      private juce::CodeDocument::Listener
{
public:
    explicit CodeEditorTab(const juce::File& file);
    ~CodeEditorTab() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    juce::File getFile() const { return targetFile; }
    juce::String getContent() const { return document.getAllContent(); }
    juce::String displayName() const;
    void saveFile();
    void saveAs(const juce::File& newFile);

private:
    // juce::CodeDocument::Listener
    void codeDocumentTextInserted(const juce::String&, int) override;
    void codeDocumentTextDeleted(int, int) override;

    juce::File targetFile;
    juce::CodeDocument document;
    FrustTokeniser tokeniser;
    juce::CodeEditorComponent editor { document, &tokeniser };
    juce::Label statusBar;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CodeEditorTab)
};

class EditorTabComponent : public juce::Component
{
public:
    EditorTabComponent();
    ~EditorTabComponent() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void openFile(const juce::File& file);
    void newUntitledTab();
    void saveActiveFile();
    void saveActiveFileAs(const juce::File& newFile);
    void closeActiveTab();

    // Empty juce::String/File if there's no active tab.
    juce::String getActiveFileContent() const;
    juce::File getActiveFile() const;

    std::function<void(const juce::File&)> onActiveFileChanged;

private:
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EditorTabComponent)
};

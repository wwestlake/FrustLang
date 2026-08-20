#pragma once

#include <JuceHeader.h>
#include <CreationDock/DockManager.h>
#include "FileTreePanel.h"
#include "EditorTabComponent.h"
#include "MetadataPanel.h"
#include "ConsolePanel.h"
#include "ContextPanel.h"
#include "AiChatPanel.h"
#include "FratePanel.h"
#include "TerminalPanel.h"
#include "Auth/DesktopAuthSession.h"

class WorkbenchComponent  : public juce::Component,
                           public juce::MenuBarModel
{
public:
    WorkbenchComponent();
    ~WorkbenchComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // MenuBarModel Overrides
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex(int topLevelMenuIndex, const juce::String& menuName) override;
    void menuItemSelected(int menuItemID, int topLevelMenuIndex) override;

private:
    void runActiveFileInRepl();

    std::unique_ptr<juce::MenuBarComponent> menuBar;
    juce::TextButton runButton { "Run" };
    std::unique_ptr<juce::ApplicationProperties> appProperties;
    std::unique_ptr<DesktopAuthSession> authSession;
    std::unique_ptr<CreationDock::DockManager> dockManager;
    FileTreePanel* fileTreePanel = nullptr;
    EditorTabComponent* editorTabComponent = nullptr;
    ConsolePanel* consolePanel = nullptr;
    TerminalPanel* terminalPanel = nullptr;
    ContextPanel* contextPanel = nullptr;
    FratePanel* fratePanel = nullptr;
    std::unique_ptr<juce::FileChooser> activeFileChooser;

    static juce::File getLayoutFile();

    enum MenuCommands {
        FileNew = 1,
        FileOpenFolder,
        FileSave,
        FileCloseTab,
        FileExit,
        EditUndo,
        EditRedo,
        EditCut,
        EditCopy,
        EditPaste,
        ViewSaveLayout,
        ViewResetLayout,
        ProjectBuildJIT,
        ProjectBuildAOT,
        ProjectRunREPL,
        AccountSignIn,
        AccountSignOut,
        HelpAbout
    };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WorkbenchComponent)
};

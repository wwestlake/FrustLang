#include "WorkbenchComponent.h"

WorkbenchComponent::WorkbenchComponent()
{
    menuBar = std::make_unique<juce::MenuBarComponent>(this);
    addAndMakeVisible(menuBar.get());

    runButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2d6a2d));
    runButton.setTooltip("Run the active editor tab's code in the console (Project > Run in REPL)");
    runButton.onClick = [this] { runActiveFileInRepl(); };
    addAndMakeVisible(runButton);

    authSession = std::make_unique<DesktopAuthSession>("ide");

    dockManager = std::make_unique<CreationDock::DockManager>(*this);
    addAndMakeVisible(*dockManager);
    
    // Application properties for saving state
    juce::PropertiesFile::Options propOptions;
    propOptions.applicationName = "LagDaemonResearchIDE";
    propOptions.folderName = "LagDaemon";
    appProperties = std::make_unique<juce::ApplicationProperties>();
    appProperties->setStorageParameters(propOptions);

    // Register panels into the DockManager layout
    auto fileTree = std::make_unique<FileTreePanel>();
    fileTreePanel = fileTree.get();
    auto editor = std::make_unique<EditorTabComponent>();
    editorTabComponent = editor.get();
    fileTreePanel->onFileDoubleClicked = [this](const juce::File& file) { editorTabComponent->openFile(file); };
    fileTreePanel->onRootDirectoryChanged = [this](const juce::File& folder) {
        if (fratePanel != nullptr) fratePanel->setProjectRoot(folder);
        if (terminalPanel != nullptr) terminalPanel->setProjectRoot(folder);
        if (appProperties) {
            appProperties->getUserSettings()->setValue("lastOpenedFolder", folder.getFullPathName());
            appProperties->getUserSettings()->saveIfNeeded();
        }
    };
    auto metadata = std::make_unique<MetadataPanel>();
    auto console = std::make_unique<ConsolePanel>();
    consolePanel = console.get();
    consolePanel->getProjectRoot = [this] { return fileTreePanel->getRootDirectory(); };
    
    auto terminal = std::make_unique<TerminalPanel>(appProperties.get());
    terminalPanel = terminal.get();
    terminalPanel->getProjectRoot = [this] { return fileTreePanel->getRootDirectory(); };

    auto context = std::make_unique<ContextPanel>();
    contextPanel = context.get();
    contextPanel->getBindings = [this] { return consolePanel->getReplSession()->listBindings(); };
    consolePanel->onSessionChanged = [this] { contextPanel->refresh(); };

    auto frate = std::make_unique<FratePanel>(authSession.get());
    fratePanel = frate.get();
    fratePanel->onFileSystemChanged = [this] {
        if (fileTreePanel) fileTreePanel->refresh();
    };

    auto aiChat = std::make_unique<AiChatPanel>();

    dockManager->registerPanel("explorer", "Project Explorer", std::move(fileTree), CreationDock::DockTargetZone::Left);
    dockManager->registerPanel("editor", "Code Editor", std::move(editor), CreationDock::DockTargetZone::CenterTab);
    dockManager->registerPanel("metadata", "AST & Metadata Inspector", std::move(metadata), CreationDock::DockTargetZone::Right);
    dockManager->registerPanel("context", "Frust Context", std::move(context), CreationDock::DockTargetZone::Right);
    dockManager->registerPanel("frate", "Frate Package Manager", std::move(frate), CreationDock::DockTargetZone::Right);
    dockManager->registerPanel("ai", "AI Assistant", std::move(aiChat), CreationDock::DockTargetZone::Right);
    dockManager->registerPanel("console", "Console & Output REPL", std::move(console), CreationDock::DockTargetZone::Bottom);
    dockManager->registerPanel("terminal", "OS Terminal", std::move(terminal), CreationDock::DockTargetZone::Bottom);

    dockManager->loadLayoutFromFile(getLayoutFile());
    
    // Restore last opened folder
    if (appProperties) {
        auto* props = appProperties->getUserSettings();
        juce::String lastFolder = props->getValue("lastOpenedFolder");
        if (lastFolder.isNotEmpty()) {
            juce::File folder(lastFolder);
            if (folder.isDirectory()) {
                if (fileTreePanel != nullptr) fileTreePanel->setRootDirectory(folder);
                if (fratePanel != nullptr) fratePanel->setProjectRoot(folder);
                if (terminalPanel != nullptr) terminalPanel->setProjectRoot(folder);
            }
        }
    }
}

WorkbenchComponent::~WorkbenchComponent()
{
    if (dockManager) {
        dockManager->saveLayoutToFile(getLayoutFile());
    }

    dockManager = nullptr;
    menuBar = nullptr;
}

juce::File WorkbenchComponent::getLayoutFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("LagDaemonResearchIDE")
        .getChildFile("layout.json");
}

void WorkbenchComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1e1e1e));
}

void WorkbenchComponent::resized()
{
    auto bounds = getLocalBounds();
    menuBar->setBounds(bounds.removeFromTop(28));

    auto toolbar = bounds.removeFromTop(32).reduced(4);
    runButton.setBounds(toolbar.removeFromLeft(80));

    if (dockManager) {
        dockManager->setBounds(bounds);
    }
}

juce::StringArray WorkbenchComponent::getMenuBarNames()
{
    return { "File", "Edit", "View", "Project", "Account", "Help" };
}

juce::PopupMenu WorkbenchComponent::getMenuForIndex(int topLevelMenuIndex, const juce::String& menuName)
{
    juce::PopupMenu menu;
    if (menuName == "File") {
        menu.addItem(FileNew, "New File");
        menu.addItem(FileOpenFolder, "Open Folder...");
        menu.addSeparator();
        menu.addItem(FileSave, "Save File");
        menu.addItem(FileCloseTab, "Close Tab");
        menu.addSeparator();
        menu.addItem(FileExit, "Exit");
    } else if (menuName == "Edit") {
        menu.addItem(EditUndo, "Undo");
        menu.addItem(EditRedo, "Redo");
        menu.addSeparator();
        menu.addItem(EditCut, "Cut");
        menu.addItem(EditCopy, "Copy");
        menu.addItem(EditPaste, "Paste");
    } else if (menuName == "View") {
        menu.addItem(ViewSaveLayout, "Save Layout");
        menu.addItem(ViewResetLayout, "Reset Layout to Default");
    } else if (menuName == "Project") {
        menu.addItem(ProjectBuildJIT, "Compile & Build (JIT)");
        menu.addItem(ProjectBuildAOT, "Compile & Build (AOT)");
        menu.addItem(ProjectRunREPL, "Run in REPL");
    } else if (menuName == "Account") {
        if (authSession && authSession->hasValidSession()) {
            menu.addItem(AccountSignOut, "Sign Out (" + authSession->getSession().user.displayName + ")");
        } else {
            menu.addItem(AccountSignIn, "Sign In...");
        }
    } else if (menuName == "Help") {
        menu.addItem(HelpAbout, "About LagDaemon IDE");
    }
    return menu;
}

void WorkbenchComponent::menuItemSelected(int menuItemID, int topLevelMenuIndex)
{
    if (menuItemID == FileExit) {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    } else if (menuItemID == FileOpenFolder) {
        activeFileChooser = std::make_unique<juce::FileChooser>(
            "Open Project Folder...",
            juce::File::getCurrentWorkingDirectory(),
            "*");

        activeFileChooser->launchAsync(
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [this](const juce::FileChooser& fc) {
                auto folder = fc.getResult();
                if (folder.isDirectory()) {
                    if (fileTreePanel != nullptr) fileTreePanel->setRootDirectory(folder);
                    // The onRootDirectoryChanged callback will handle updating the FratePanel and saving properties
                }
                activeFileChooser = nullptr;
            });
    } else if (menuItemID == FileNew) {
        if (editorTabComponent) editorTabComponent->newUntitledTab();
    } else if (menuItemID == FileSave) {
        if (editorTabComponent == nullptr) return;

        if (editorTabComponent->getActiveFile().getFullPathName().isNotEmpty()) {
            editorTabComponent->saveActiveFile();
            return;
        }

        // Untitled tab - Save behaves as Save As.
        auto startDir = fileTreePanel != nullptr && fileTreePanel->getRootDirectory().isDirectory()
            ? fileTreePanel->getRootDirectory()
            : juce::File::getCurrentWorkingDirectory();

        activeFileChooser = std::make_unique<juce::FileChooser>(
            "Save File As...",
            startDir.getChildFile("untitled.fr"),
            "*.fr;*.fri");

        activeFileChooser->launchAsync(
            juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
            [this](const juce::FileChooser& fc) {
                auto file = fc.getResult();
                if (file != juce::File() && editorTabComponent != nullptr) {
                    editorTabComponent->saveActiveFileAs(file);
                }
                activeFileChooser = nullptr;
            });
    } else if (menuItemID == FileCloseTab) {
        if (editorTabComponent) editorTabComponent->closeActiveTab();
    } else if (menuItemID == ViewSaveLayout) {
        if (dockManager) dockManager->saveLayoutToFile(getLayoutFile());
    } else if (menuItemID == ViewResetLayout) {
        if (dockManager) dockManager->resetLayout();
    } else if (menuItemID == ProjectRunREPL) {
        runActiveFileInRepl();
    } else if (menuItemID == AccountSignIn) {
        if (authSession) authSession->beginLogin();
    } else if (menuItemID == AccountSignOut) {
        if (authSession) authSession->clearSession();
    }
}

void WorkbenchComponent::runActiveFileInRepl()
{
    if (editorTabComponent == nullptr || consolePanel == nullptr) return;

    auto source = editorTabComponent->getActiveFileContent();
    if (source.trim().isEmpty()) return;

    auto label = editorTabComponent->getActiveFile().exists()
        ? editorTabComponent->getActiveFile().getFileName()
        : juce::String("untitled");

    consolePanel->runScript(source, label);
}

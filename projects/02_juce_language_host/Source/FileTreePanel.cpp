#include "FileTreePanel.h"

namespace
{
const juce::Colour panelBackground(0xff1e1e1e);
const juce::Colour treeBackground(0xff181818);
const juce::Colour selectedBackground(0xff094771);
const juce::Colour textColour(0xffcccccc);
const juce::Colour activeTextColour(0xffffffff);
const juce::Colour folderColour(0xffdcb67a);
constexpr const char* dragPrefix = "frustide-files:";

// The colour a file's icon gets, by its kind.
juce::Colour colourFor(const juce::File& file)
{
    const auto ext = file.getFileExtension().toLowerCase();
    const auto name = file.getFileName().toLowerCase();
    if (ext == ".fr" || ext == ".frust") return juce::Colour(0xffff8c42);
    if (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".c") return juce::Colour(0xff519aba);
    if (ext == ".h" || ext == ".hpp" || ext == ".hh" || ext == ".hxx") return juce::Colour(0xffa074c4);
    if (ext == ".cs" || ext == ".csproj") return juce::Colour(0xff68217a).brighter(0.6f);
    if (ext == ".fs" || ext == ".fsi" || ext == ".fsx" || ext == ".fsproj") return juce::Colour(0xff378bba);
    if (ext == ".json" || ext == ".toml" || ext == ".yaml" || ext == ".yml") return juce::Colour(0xffcbcb41);
    if (ext == ".md" || ext == ".txt") return juce::Colour(0xff9dc3e6);
    if (ext == ".cmake" || name == "cmakelists.txt") return juce::Colour(0xff6d8086).brighter(0.4f);
    if (ext == ".sln" || ext == ".vcxproj") return juce::Colour(0xff854cc7);
    if (ext == ".py") return juce::Colour(0xff3572a5).brighter(0.3f);
    if (ext == ".rs") return juce::Colour(0xffdea584);
    return juce::Colour(0xff8a8a8a);
}

// "name copy.ext", "name copy 2.ext"... the first that does not exist.
juce::File copyNameFor(const juce::File& wanted)
{
    if (! wanted.exists())
        return wanted;
    const auto folder = wanted.getParentDirectory();
    const auto base = wanted.isDirectory() ? wanted.getFileName() : wanted.getFileNameWithoutExtension();
    const auto ext = wanted.isDirectory() ? juce::String() : wanted.getFileExtension();
    for (int n = 1; n < 1000; ++n)
    {
        const auto candidate = folder.getChildFile(base + (n == 1 ? " copy" : " copy " + juce::String(n)) + ext);
        if (! candidate.exists())
            return candidate;
    }
    return folder.getNonexistentChildFile(base, ext, false);
}

// The entries of a folder the tree shows: folders first, then files, each by name.
juce::Array<juce::File> listFolder(const juce::File& folder)
{
    juce::Array<juce::File> entries;
    for (const auto& entry : folder.findChildFiles(juce::File::findFilesAndDirectories, false, "*"))
        if (! FileTreePanel::isHidden(entry))
            entries.add(entry);
    std::sort(entries.begin(), entries.end(), [](const juce::File& a, const juce::File& b) {
        const bool da = a.isDirectory(), db = b.isDirectory();
        if (da != db) return da;
        return a.getFileName().compareNatural(b.getFileName()) < 0;
    });
    return entries;
}

juce::String signatureOf(const juce::Array<juce::File>& entries)
{
    juce::String text;
    for (const auto& e : entries)
        text << e.getFileName() << (e.isDirectory() ? "/" : "") << "\n";
    return text;
}

void showProblem(const juce::String& title, const juce::String& message)
{
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, title, message);
}
}

// =============================================================================================================================

class FileTreePanel::Item final : public juce::TreeViewItem
{
public:
    Item(FileTreePanel& ownerPanel, juce::File itemFile)
        : panel(ownerPanel), file(std::move(itemFile)), folder(file.isDirectory()) {}

    const juce::File& getFile() const { return file; }
    bool isFolder() const { return folder; }
    const juce::String& recordedListing() const { return listing; }

    bool mightContainSubItems() override { return folder; }
    juce::String getUniqueName() const override { return file.getFileName(); }
    int getItemHeight() const override { return 22; }
    juce::String getTooltip() override { return file.getFullPathName(); }

    void itemOpennessChanged(bool isNowOpen) override
    {
        if (isNowOpen)
            populate();
        else
            clearSubItems();   // read again when opened: never shows a stale folder
    }

    void populate()
    {
        clearSubItems();
        const auto entries = listFolder(file);
        listing = signatureOf(entries);
        for (const auto& entry : entries)
            addSubItem(new Item(panel, entry));
    }

    void paintItem(juce::Graphics& g, int width, int height) override
    {
        const bool active = ! folder && panel.isActiveFile(file);
        auto area = juce::Rectangle<int>(0, 0, width, height);

        // The icon: a folder, or a page coloured by the file's kind.
        auto icon = area.removeFromLeft(18).toFloat().reduced(2.0f, 4.0f);
        if (folder)
        {
            g.setColour(folderColour);
            juce::Path shape;
            shape.addRoundedRectangle(icon.getX(), icon.getY() + 2.0f, icon.getWidth(), icon.getHeight() - 2.0f, 1.5f);
            shape.addRoundedRectangle(icon.getX(), icon.getY(), icon.getWidth() * 0.45f, 4.0f, 1.0f);
            g.fillPath(shape);
        }
        else
        {
            auto page = icon.withSizeKeepingCentre(icon.getHeight() * 0.8f, icon.getHeight());
            g.setColour(colourFor(file));
            g.fillRoundedRectangle(page, 1.5f);
            g.setColour(treeBackground.withAlpha(0.6f));
            g.fillRect(page.reduced(2.5f, 3.0f).withHeight(1.2f));
            g.fillRect(page.reduced(2.5f, 3.0f).withHeight(1.2f).translated(0.0f, 3.0f));
        }

        area.removeFromLeft(4);
        g.setColour(active ? activeTextColour : textColour);
        g.setFont(active ? juce::Font(13.5f, juce::Font::bold) : juce::Font(13.5f));
        g.drawText(file.getFileName(), area, juce::Justification::centredLeft, true);
    }

    void itemClicked(const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            if (! isSelected())
                setSelected(true, true);
            panel.showMenuFor(this);
        }
    }

    void itemDoubleClicked(const juce::MouseEvent&) override { panel.openItem(this); }

    // ---- Dragging to move, and dropping files from Windows Explorer to copy them in ----
    juce::var getDragSourceDescription() override
    {
        juce::StringArray paths;
        if (isSelected())
            for (const auto& f : panel.selectedFiles())
                paths.add(f.getFullPathName());
        else
            paths.add(file.getFullPathName());
        return juce::String(dragPrefix) + paths.joinIntoString("\n");
    }

    bool isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& details) override
    {
        return details.description.toString().startsWith(dragPrefix);
    }

    void itemDropped(const juce::DragAndDropTarget::SourceDetails& details, int) override
    {
        juce::Array<juce::File> files;
        for (const auto& path : juce::StringArray::fromLines(details.description.toString().fromFirstOccurrenceOf(dragPrefix, false, false)))
            if (path.isNotEmpty())
                files.add(juce::File(path));
        panel.moveInto(files, folder ? file : file.getParentDirectory());
    }

    bool isInterestedInFileDrag(const juce::StringArray&) override { return true; }

    void filesDropped(const juce::StringArray& paths, int) override
    {
        panel.copyInto(paths, folder ? file : file.getParentDirectory());
    }

private:
    FileTreePanel& panel;
    juce::File file;
    bool folder = false;
    juce::String listing;   // what was on disk when this folder was read
};

// =============================================================================================================================

class FileTreePanel::ExplorerTree final : public juce::TreeView
{
public:
    explicit ExplorerTree(FileTreePanel& ownerPanel) : panel(ownerPanel) {}

    bool keyPressed(const juce::KeyPress& key) override
    {
        return panel.handleKey(key) || juce::TreeView::keyPressed(key);
    }

private:
    FileTreePanel& panel;
};

// =============================================================================================================================

FileTreePanel::FileTreePanel()
{
    headerLabel.setFont(juce::Font(14.0f, juce::Font::bold));
    headerLabel.setColour(juce::Label::textColourId, juce::Colours::orange);
    addAndMakeVisible(headerLabel);

    openFolderButton.onClick = [this] {
        auto chooser = std::make_shared<juce::FileChooser>("Choose the project folder", currentRoot, "*");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [this, chooser](const juce::FileChooser& fc) {
                const auto folder = fc.getResult();
                if (folder.isDirectory())
                    setRootDirectory(folder);
            });
    };
    addAndMakeVisible(openFolderButton);

    newFileButton.setTooltip("New file in the selected folder");
    newFileButton.onClick = [this] { newFile(targetFolder()); };
    newFolderButton.setTooltip("New folder in the selected folder");
    newFolderButton.onClick = [this] { newFolder(targetFolder()); };
    refreshButton.setTooltip("Read the folders again");
    refreshButton.onClick = [this] { refresh(); };
    collapseButton.setTooltip("Close every folder");
    collapseButton.onClick = [this] { collapseAll(); };
    for (auto* b : { &newFileButton, &newFolderButton, &refreshButton, &collapseButton })
    {
        b->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2d2d2d));
        addAndMakeVisible(*b);
    }

    tree = std::make_unique<ExplorerTree>(*this);
    tree->setColour(juce::TreeView::backgroundColourId, treeBackground);
    tree->setColour(juce::TreeView::selectedItemBackgroundColourId, selectedBackground);
    tree->setColour(juce::TreeView::linesColourId, juce::Colours::transparentBlack);
    tree->setRootItemVisible(false);
    tree->setMultiSelectEnabled(true);
    tree->setIndentSize(14);
    tree->setWantsKeyboardFocus(true);
    tree->addMouseListener(this, true);
    addAndMakeVisible(*tree);

    setRootDirectory(juce::File::getCurrentWorkingDirectory());
    startTimer(1200);
}

FileTreePanel::~FileTreePanel()
{
    stopTimer();
    tree->removeMouseListener(this);
    tree->setRootItem(nullptr);
}

void FileTreePanel::paint(juce::Graphics& g)
{
    g.fillAll(panelBackground);
    g.setColour(juce::Colour(0xff333333));
    g.drawRect(getLocalBounds(), 1);
}

void FileTreePanel::resized()
{
    auto bounds = getLocalBounds().reduced(6);
    auto topBar = bounds.removeFromTop(24);
    openFolderButton.setBounds(topBar.removeFromRight(100));
    headerLabel.setBounds(topBar);
    bounds.removeFromTop(4);

    auto buttons = bounds.removeFromTop(22);
    const int width = (buttons.getWidth() - 3 * 4) / 4;
    for (auto* b : { &newFileButton, &newFolderButton, &refreshButton, &collapseButton })
    {
        b->setBounds(buttons.removeFromLeft(width));
        buttons.removeFromLeft(4);
    }
    bounds.removeFromTop(4);
    tree->setBounds(bounds);
}

bool FileTreePanel::isHidden(const juce::File& file)
{
    static const char* const hidden[] = { ".git", ".vs", ".idea", "node_modules", "__pycache__", ".agent-state", ".frustide",
                                          "Thumbs.db", "desktop.ini", ".DS_Store" };
    const auto name = file.getFileName();
    for (auto* h : hidden)
        if (name.equalsIgnoreCase(h))
            return true;
    return false;
}

juce::String FileTreePanel::relativePath(const juce::File& file) const
{
    return file.getRelativePathFrom(currentRoot).replaceCharacter('\\', '/');
}

void FileTreePanel::setRootDirectory(const juce::File& dir)
{
    currentRoot = dir;
    headerLabel.setText("Root: " + dir.getFileName(), juce::dontSendNotification);
    tree->setRootItem(nullptr);
    rootItem = std::make_unique<Item>(*this, dir);
    tree->setRootItem(rootItem.get());
    rootItem->setOpen(true);
    lastSignature = diskSignature();
    if (onRootDirectoryChanged)
        onRootDirectoryChanged(currentRoot);
}

void FileTreePanel::refresh()
{
    rebuild();
}

// Reads the tree again from disk, keeping which folders are open, the selection and the scroll position.
void FileTreePanel::rebuild()
{
    if (rootItem == nullptr)
        return;
    auto state = tree->getOpennessState(true);
    tree->setRootItem(nullptr);
    rootItem = std::make_unique<Item>(*this, currentRoot);
    tree->setRootItem(rootItem.get());
    rootItem->setOpen(true);
    if (state != nullptr)
        tree->restoreOpennessState(*state, true);
    lastSignature = diskSignature();
}

// What is on disk in every folder the tree has open, compared with what was read, so a change from anyone shows up.
juce::String FileTreePanel::diskSignature() const
{
    juce::String text;
    std::function<void(Item*)> walk = [&text, &walk](Item* item) {
        if (item == nullptr || ! item->isFolder() || ! item->isOpen())
            return;
        text << item->getFile().getFullPathName() << "|" << signatureOf(listFolder(item->getFile()));
        for (int i = 0; i < item->getNumSubItems(); ++i)
            walk(dynamic_cast<Item*>(item->getSubItem(i)));
    };
    walk(rootItem.get());
    return text;
}

void FileTreePanel::timerCallback()
{
    // Changes on disk.
    if (currentRoot.isDirectory())
    {
        const auto now = diskSignature();
        if (now != lastSignature)
            rebuild();
    }

    // The file the editor shows: highlighted, with its folders opened so it can be seen.
    const auto editorFile = getActiveEditorFile ? getActiveEditorFile() : juce::File();
    if (editorFile != activeFile)
    {
        activeFile = editorFile;
        if (activeFile.isAChildOf(currentRoot))
        {
            Item* item = rootItem.get();
            juce::StringArray parts;
            parts.addTokens(activeFile.getRelativePathFrom(currentRoot), "\\/", "");
            for (int p = 0; p < parts.size() && item != nullptr; ++p)
            {
                item->setOpen(true);
                Item* next = nullptr;
                for (int i = 0; i < item->getNumSubItems(); ++i)
                    if (auto* sub = dynamic_cast<Item*>(item->getSubItem(i)); sub != nullptr && sub->getFile().getFileName() == parts[p])
                        next = sub;
                item = next;
            }
            if (item != nullptr)
                tree->scrollToKeepItemVisible(item);
            lastSignature = diskSignature();
        }
        tree->repaint();
    }
}

juce::Array<juce::File> FileTreePanel::selectedFiles() const
{
    juce::Array<juce::File> files;
    for (int i = 0; i < tree->getNumSelectedItems(); ++i)
        if (auto* item = dynamic_cast<Item*>(tree->getSelectedItem(i)))
            files.add(item->getFile());
    return files;
}

juce::File FileTreePanel::targetFolder() const
{
    const auto selected = selectedFiles();
    if (selected.size() == 1)
        return selected[0].isDirectory() ? selected[0] : selected[0].getParentDirectory();
    return currentRoot;
}

void FileTreePanel::selectFile(const juce::File& file)
{
    std::function<Item*(Item*)> find = [&file, &find](Item* item) -> Item* {
        if (item == nullptr)
            return nullptr;
        if (item->getFile() == file)
            return item;
        if (file.isAChildOf(item->getFile()))
        {
            item->setOpen(true);
            for (int i = 0; i < item->getNumSubItems(); ++i)
                if (auto* found = find(dynamic_cast<Item*>(item->getSubItem(i))))
                    return found;
        }
        return nullptr;
    };
    if (auto* item = find(rootItem.get()); item != nullptr && item != rootItem.get())
    {
        item->setSelected(true, true);
        tree->scrollToKeepItemVisible(item);
    }
    lastSignature = diskSignature();
}

void FileTreePanel::openItem(Item* item)
{
    if (item == nullptr)
        return;
    if (item->isFolder())
        item->setOpen(! item->isOpen());
    else if (onFileDoubleClicked)
        onFileDoubleClicked(item->getFile());
}

// ---- The right-click menu ----

void FileTreePanel::showMenuFor(Item* item)
{
    if (item == nullptr)
        tree->clearSelectedItems();
    const auto selected = selectedFiles();
    const bool one = selected.size() == 1;
    const bool any = ! selected.isEmpty();
    const bool oneFile = one && selected[0].existsAsFile();
    const auto folder = targetFolder();

    juce::PopupMenu menu;
    menu.addItem(1, "Open", oneFile);
    menu.addSeparator();
    menu.addItem(2, "New File...");
    menu.addItem(3, "New Folder...");
    menu.addSeparator();
    menu.addItem(4, "Cut", any);
    menu.addItem(5, "Copy", any);
    menu.addItem(6, "Paste", ! clipboardFiles.isEmpty());
    menu.addItem(7, "Duplicate", one);
    menu.addSeparator();
    menu.addItem(8, "Rename...", one);
    menu.addItem(9, any && selected.size() > 1 ? "Delete " + juce::String(selected.size()) + " Items..." : juce::String("Delete..."), any);
    menu.addSeparator();
    menu.addItem(10, "Copy Path", one);
    menu.addItem(11, "Copy Relative Path", one);
    menu.addItem(12, "Reveal in File Explorer");
    menu.addSeparator();
    menu.addItem(13, "Refresh");
    menu.addItem(14, "Collapse All");

    menu.showMenuAsync(juce::PopupMenu::Options().withMousePosition(),
        [this, selected, folder](int choice) {
            const auto first = selected.isEmpty() ? juce::File() : selected[0];
            switch (choice)
            {
                case 1: if (onFileDoubleClicked) onFileDoubleClicked(first); break;
                case 2: newFile(folder); break;
                case 3: newFolder(folder); break;
                case 4: copyToClipboard(selected, true); break;
                case 5: copyToClipboard(selected, false); break;
                case 6: paste(folder); break;
                case 7: duplicateFile(first); break;
                case 8: renameFile(first); break;
                case 9: deleteFiles(selected); break;
                case 10: juce::SystemClipboard::copyTextToClipboard(first.getFullPathName()); break;
                case 11: juce::SystemClipboard::copyTextToClipboard(relativePath(first)); break;
                case 12: (first != juce::File() ? first : currentRoot).revealToUser(); break;
                case 13: refresh(); break;
                case 14: collapseAll(); break;
                default: break;
            }
        });
}

// Right-click on empty space (below the last item) gives the menu for the project root; a click there clears the selection.
void FileTreePanel::mouseDown(const juce::MouseEvent& e)
{
    if (e.eventComponent == this)
        return;
    const auto inTree = e.getEventRelativeTo(tree.get()).getPosition();
    if (tree->getItemAt(inTree.y) != nullptr || ! tree->getLocalBounds().contains(inTree))
        return;
    tree->clearSelectedItems();
    if (e.mods.isPopupMenu())
        showMenuFor(nullptr);
}

bool FileTreePanel::handleKey(const juce::KeyPress& key)
{
    const auto selected = selectedFiles();
    const bool command = key.getModifiers().isCommandDown();
    if (key == juce::KeyPress::returnKey && selected.size() == 1)
    {
        if (auto* item = dynamic_cast<Item*>(tree->getSelectedItem(0)))
            openItem(item);
        return true;
    }
    if (key == juce::KeyPress::F2Key && selected.size() == 1) { renameFile(selected[0]); return true; }
    if (key == juce::KeyPress::deleteKey && ! selected.isEmpty()) { deleteFiles(selected); return true; }
    if (command && key.getKeyCode() == 'C' && ! selected.isEmpty()) { copyToClipboard(selected, false); return true; }
    if (command && key.getKeyCode() == 'X' && ! selected.isEmpty()) { copyToClipboard(selected, true); return true; }
    if (command && key.getKeyCode() == 'V' && ! clipboardFiles.isEmpty()) { paste(targetFolder()); return true; }
    return false;
}

// ---- The operations ----

juce::String FileTreePanel::nameProblem(const juce::String& name)
{
    if (name.isEmpty())
        return "Give a name.";
    if (name.containsAnyOf("<>:\"|?*"))
        return "A name cannot contain < > : \" | ? or *.";
    juce::StringArray parts;
    parts.addTokens(name, "\\/", "");
    for (const auto& part : parts)
        if (part == ".." || part == ".")
            return "A name cannot be . or ..";
    if (name.endsWithChar('.') || name.endsWithChar(' '))
        return "A name cannot end with a dot or a space.";
    return {};
}

void FileTreePanel::askForName(const juce::String& title, const juce::String& initial, int selectLength,
                               std::function<void(const juce::String&)> done)
{
    nameWindow = std::make_unique<juce::AlertWindow>(title, juce::String(), juce::MessageBoxIconType::NoIcon, this);
    nameWindow->addTextEditor("name", initial, "Name:");
    nameWindow->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
    nameWindow->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    if (auto* editor = nameWindow->getTextEditor("name"))
    {
        editor->setHighlightedRegion({ 0, selectLength > 0 ? selectLength : initial.length() });
        editor->grabKeyboardFocus();
    }
    auto safeThis = juce::Component::SafePointer<FileTreePanel>(this);
    nameWindow->enterModalState(true, juce::ModalCallbackFunction::create([safeThis, done](int result) {
        if (safeThis == nullptr || safeThis->nameWindow == nullptr)
            return;
        const auto text = safeThis->nameWindow->getTextEditorContents("name").trim();
        juce::MessageManager::callAsync([safeThis] { if (safeThis != nullptr) safeThis->nameWindow.reset(); });
        if (result != 1)
            return;
        const auto problem = nameProblem(text);
        if (problem.isNotEmpty())
        {
            showProblem("Not a usable name", problem);
            return;
        }
        done(text);
    }), false);
}

void FileTreePanel::newFile(const juce::File& folder)
{
    askForName("New File", {}, 0, [this, folder](const juce::String& name) {
        const auto file = folder.getChildFile(name);
        if (file.exists())
            return showProblem("Not created", "'" + relativePath(file) + "' already exists.");
        if (! file.create().wasOk())
            return showProblem("Not created", "'" + relativePath(file) + "' could not be created.");
        rebuild();
        selectFile(file);
        if (onFileDoubleClicked)
            onFileDoubleClicked(file);
    });
}

void FileTreePanel::newFolder(const juce::File& folder)
{
    askForName("New Folder", {}, 0, [this, folder](const juce::String& name) {
        const auto dir = folder.getChildFile(name);
        if (dir.exists())
            return showProblem("Not created", "'" + relativePath(dir) + "' already exists.");
        if (! dir.createDirectory().wasOk())
            return showProblem("Not created", "'" + relativePath(dir) + "' could not be created.");
        rebuild();
        selectFile(dir);
    });
}

void FileTreePanel::renameFile(const juce::File& file)
{
    if (file == juce::File() || file == currentRoot)
        return;
    const auto name = file.getFileName();
    const int stem = file.isDirectory() ? name.length() : file.getFileNameWithoutExtension().length();
    askForName("Rename", name, stem, [this, file](const juce::String& newName) {
        if (newName.containsAnyOf("\\/"))
            return showProblem("Not renamed", "A new name cannot contain \\ or /. To move it, drag it to another folder.");
        const auto target = file.getParentDirectory().getChildFile(newName);
        if (target == file)
            return;
        if (target.exists() && ! target.getFileName().equalsIgnoreCase(file.getFileName()))
            return showProblem("Not renamed", "'" + newName + "' already exists here.");
        if (! file.moveFileTo(target))
            return showProblem("Not renamed", "'" + file.getFileName() + "' could not be renamed (is it open in another program?).");
        if (onFileMoved)
            onFileMoved(file, target);
        rebuild();
        selectFile(target);
    });
}

void FileTreePanel::deleteFiles(const juce::Array<juce::File>& files)
{
    juce::Array<juce::File> targets;
    for (const auto& f : files)
        if (f != currentRoot && f.exists())
            targets.add(f);
    if (targets.isEmpty())
        return;

    const auto what = targets.size() == 1 ? "'" + relativePath(targets[0]) + "'" : juce::String(targets.size()) + " items";
    auto options = juce::MessageBoxOptions()
        .withIconType(juce::MessageBoxIconType::WarningIcon)
        .withTitle("Delete")
        .withMessage("Move " + what + " to the Recycle Bin?")
        .withButton("Delete")
        .withButton("Cancel")
        .withAssociatedComponent(this);
    auto safeThis = juce::Component::SafePointer<FileTreePanel>(this);
    juce::AlertWindow::showAsync(options, [safeThis, targets](int result) {
        if (safeThis == nullptr || result != 1)
            return;
        juce::StringArray failed;
        for (const auto& f : targets)
        {
            if (f.moveToTrash())
            {
                if (safeThis->onFileDeleted)
                    safeThis->onFileDeleted(f);
            }
            else
                failed.add(safeThis->relativePath(f));
        }
        safeThis->rebuild();
        if (! failed.isEmpty())
            showProblem("Not deleted", "These could not be moved to the Recycle Bin (open in another program?):\n" + failed.joinIntoString("\n"));
    });
}

void FileTreePanel::duplicateFile(const juce::File& file)
{
    if (file == juce::File() || file == currentRoot)
        return;
    const auto copy = copyNameFor(file);
    const bool ok = file.isDirectory() ? file.copyDirectoryTo(copy) : file.copyFileTo(copy);
    if (! ok)
        return showProblem("Not duplicated", "'" + relativePath(file) + "' could not be copied.");
    rebuild();
    selectFile(copy);
}

void FileTreePanel::copyToClipboard(const juce::Array<juce::File>& files, bool cut)
{
    clipboardFiles.clear();
    for (const auto& f : files)
        if (f != currentRoot)
            clipboardFiles.add(f);
    clipboardIsCut = cut;
}

void FileTreePanel::paste(const juce::File& folder)
{
    if (clipboardFiles.isEmpty())
        return;
    if (clipboardIsCut)
    {
        const auto files = clipboardFiles;
        clipboardFiles.clear();
        moveInto(files, folder);
        return;
    }
    juce::StringArray paths;
    for (const auto& f : clipboardFiles)
        paths.add(f.getFullPathName());
    copyInto(paths, folder);
}

void FileTreePanel::copyInto(const juce::StringArray& paths, const juce::File& folder)
{
    juce::StringArray failed;
    juce::File last;
    for (const auto& path : paths)
    {
        const juce::File source(path);
        if (! source.exists())
            continue;
        if (source.isDirectory() && (folder == source || folder.isAChildOf(source)))
        {
            failed.add(source.getFileName() + " (a folder cannot be copied into itself)");
            continue;
        }
        const auto target = copyNameFor(folder.getChildFile(source.getFileName()));
        const bool ok = source.isDirectory() ? source.copyDirectoryTo(target) : source.copyFileTo(target);
        if (ok)
            last = target;
        else
            failed.add(source.getFileName());
    }
    rebuild();
    if (last != juce::File())
        selectFile(last);
    if (! failed.isEmpty())
        showProblem("Not copied", failed.joinIntoString("\n"));
}

void FileTreePanel::moveInto(const juce::Array<juce::File>& files, const juce::File& folder)
{
    juce::Array<juce::File> moving;
    juce::StringArray refused;
    for (const auto& f : files)
    {
        if (! f.exists() || f == currentRoot || f.getParentDirectory() == folder)
            continue;
        if (folder == f || folder.isAChildOf(f))
            refused.add(f.getFileName() + " (a folder cannot go into itself)");
        else
            moving.add(f);
    }
    if (! refused.isEmpty())
        showProblem("Not moved", refused.joinIntoString("\n"));
    if (moving.isEmpty())
        return;

    const auto what = moving.size() == 1 ? "'" + moving[0].getFileName() + "'" : juce::String(moving.size()) + " items";
    const auto where = folder == currentRoot ? juce::String("the project root") : "'" + relativePath(folder) + "'";
    auto options = juce::MessageBoxOptions()
        .withIconType(juce::MessageBoxIconType::QuestionIcon)
        .withTitle("Move")
        .withMessage("Move " + what + " into " + where + "?")
        .withButton("Move")
        .withButton("Cancel")
        .withAssociatedComponent(this);
    auto safeThis = juce::Component::SafePointer<FileTreePanel>(this);
    juce::AlertWindow::showAsync(options, [safeThis, moving, folder](int result) {
        if (safeThis == nullptr || result != 1)
            return;
        juce::StringArray failed;
        juce::File last;
        for (const auto& f : moving)
        {
            const auto target = folder.getChildFile(f.getFileName());
            if (target.exists())
            {
                failed.add(f.getFileName() + " (already exists there)");
                continue;
            }
            if (f.moveFileTo(target))
            {
                last = target;
                if (safeThis->onFileMoved)
                    safeThis->onFileMoved(f, target);
            }
            else
                failed.add(f.getFileName());
        }
        safeThis->rebuild();
        if (last != juce::File())
            safeThis->selectFile(last);
        if (! failed.isEmpty())
            showProblem("Not moved", failed.joinIntoString("\n"));
    });
}

void FileTreePanel::collapseAll()
{
    if (rootItem == nullptr)
        return;
    for (int i = 0; i < rootItem->getNumSubItems(); ++i)
        rootItem->getSubItem(i)->setOpen(false);
    lastSignature = diskSignature();
}

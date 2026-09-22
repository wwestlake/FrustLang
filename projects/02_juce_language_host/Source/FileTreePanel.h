#pragma once

#include <JuceHeader.h>
#include <functional>

// The Project Explorer: the project's folders and files as a tree, with what every IDE's explorer does.
//
//  - Click selects (highlighted), Ctrl/Shift+click selects several, arrows move, Enter opens, double-click opens a file or
//    opens/closes a folder. The file shown in the editor is highlighted and its folders are opened.
//  - Right-click: New File, New Folder, Open, Rename (F2), Delete (Del, to the Recycle Bin, confirmed), Duplicate,
//    Cut / Copy / Paste (Ctrl+X/C/V), Copy Path, Copy Relative Path, Reveal in File Explorer, Refresh, Collapse All.
//  - Drag to move files and folders (confirmed); drop files from Windows Explorer to copy them in.
//  - Changes on disk, from anyone (the Engineer, another program), appear by themselves; open folders stay open.
//  - Folders first, then files, by name; icons by file type; .git and similar clutter hidden.
class FileTreePanel : public juce::Component,
                      public juce::DragAndDropContainer,
                      private juce::Timer
{
public:
    FileTreePanel();
    ~FileTreePanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

    void setRootDirectory(const juce::File& dir);
    juce::File getRootDirectory() const { return currentRoot; }
    void refresh();

    std::function<void(const juce::File&)> onFileDoubleClicked;                        // open this file in the editor
    std::function<void(const juce::File&)> onRootDirectoryChanged;
    std::function<void(const juce::File& from, const juce::File& to)> onFileMoved;     // renamed or moved (file or folder)
    std::function<void(const juce::File&)> onFileDeleted;                              // file or folder, now in the Recycle Bin
    std::function<juce::File()> getActiveEditorFile;                                   // highlighted in the tree

    // ---- Used by the tree's items ----
    class Item;
    void showMenuFor(Item* item);
    void openItem(Item* item);
    bool isActiveFile(const juce::File& file) const { return file == activeFile; }
    juce::String relativePath(const juce::File& file) const;
    void moveInto(const juce::Array<juce::File>& files, const juce::File& folder);
    void copyInto(const juce::StringArray& paths, const juce::File& folder);
    static bool isHidden(const juce::File& file);

private:
    class ExplorerTree;

    void timerCallback() override;
    void rebuild();
    juce::String diskSignature() const;

    juce::Array<juce::File> selectedFiles() const;
    juce::File targetFolder() const;   // the selected folder, the selected file's folder, or the root
    void selectFile(const juce::File& file);

    void newFile(const juce::File& folder);
    void newFolder(const juce::File& folder);
    void renameFile(const juce::File& file);
    void deleteFiles(const juce::Array<juce::File>& files);
    void duplicateFile(const juce::File& file);
    void copyToClipboard(const juce::Array<juce::File>& files, bool cut);
    void paste(const juce::File& folder);
    void collapseAll();
    bool handleKey(const juce::KeyPress& key);

    // Asks for a name (for New File, New Folder, Rename) and calls `done` with it, checked, if the user presses OK.
    void askForName(const juce::String& title, const juce::String& initial, int selectLength,
                    std::function<void(const juce::String&)> done);
    static juce::String nameProblem(const juce::String& name);

    juce::Label headerLabel { "Header", "Project Explorer" };
    juce::TextButton openFolderButton { "Open Folder..." };
    juce::TextButton newFileButton { "+ File" };
    juce::TextButton newFolderButton { "+ Folder" };
    juce::TextButton refreshButton { "Refresh" };
    juce::TextButton collapseButton { "Collapse" };
    std::unique_ptr<ExplorerTree> tree;
    std::unique_ptr<Item> rootItem;
    juce::File currentRoot;
    juce::File activeFile;
    juce::String lastSignature;
    juce::Array<juce::File> clipboardFiles;
    bool clipboardIsCut = false;
    std::unique_ptr<juce::AlertWindow> nameWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FileTreePanel)
};

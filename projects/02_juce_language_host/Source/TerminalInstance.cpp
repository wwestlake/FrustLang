#include "TerminalInstance.h"

TerminalInstance::TerminalInstance(const juce::String& shellType, std::function<juce::File()> getRoot)
    : getProjectRoot(std::move(getRoot)), currentShell(shellType)
{
    if (getProjectRoot) {
        currentWorkingDirectory = getProjectRoot();
    }
    if (!currentWorkingDirectory.isDirectory()) {
        currentWorkingDirectory = juce::File::getCurrentWorkingDirectory();
    }

    consoleText.setMultiLine(true, true);
    consoleText.setReturnKeyStartsNewLine(false);
    consoleText.setReadOnly(false);
    consoleText.setCaretVisible(true);
    consoleText.setFont(juce::Font("Consolas", 13.0f, juce::Font::plain));
    consoleText.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff0d0d0d));
    consoleText.setColour(juce::TextEditor::textColourId, juce::Colour(0xffd4d4d4));
    consoleText.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    consoleText.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    
    updatePromptText();
    consoleText.setText(promptText);
    
    consoleText.onReturnKey = [this] { executeCommand(); };
    consoleText.addKeyListener(this);
    addAndMakeVisible(consoleText);
}

TerminalInstance::~TerminalInstance()
{
    stopTimer();
    consoleText.removeKeyListener(this);
    if (activeProcess) activeProcess->kill();
}

void TerminalInstance::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff0d0d0d));
}

void TerminalInstance::resized()
{
    consoleText.setBounds(getLocalBounds());
}

void TerminalInstance::setProjectRoot(const juce::File& root)
{
    if (root.isDirectory()) {
        currentWorkingDirectory = root;
        updatePromptText();
        consoleText.setText(promptText);
        consoleText.moveCaretToEnd();
    }
}

void TerminalInstance::updatePromptText()
{
    juce::String dirName = currentWorkingDirectory.getFileName();
    if (dirName.isEmpty()) dirName = currentWorkingDirectory.getFullPathName();
    
    if (currentShell == "powershell") {
        promptText = "PS [" + dirName + "]> ";
    } else {
        promptText = "wsl [" + dirName + "]$ ";
    }
}

void TerminalInstance::executeCommand()
{
    if (activeProcess && activeProcess->isRunning()) {
        logMessage("A command is already running.");
        return;
    }

    auto fullText = consoleText.getText();
    auto lastPromptIndex = fullText.lastIndexOfIgnoreCase(promptText);
    if (lastPromptIndex < 0) return;

    auto input = fullText.substring(lastPromptIndex + promptText.length()).trim();
    if (input.isEmpty()) {
        consoleText.setText(fullText + "\n" + promptText);
        consoleText.moveCaretToEnd();
        return;
    }

    consoleText.setText(fullText + "\n");
    consoleText.moveCaretToEnd();

    if (input.startsWithIgnoreCase("cd ")) {
        auto target = input.substring(3).trim();
        if (target.startsWith("\"") && target.endsWith("\"")) {
            target = target.substring(1, target.length() - 1);
        }
        
        juce::File newDir;
        if (juce::File::isAbsolutePath(target)) {
            newDir = juce::File(target);
        } else {
            newDir = currentWorkingDirectory.getChildFile(target);
        }
        
        if (newDir.isDirectory()) {
            currentWorkingDirectory = newDir;
            updatePromptText();
        } else {
            logMessage("cd: cannot change directory to " + target + ": No such file or directory");
        }
        
        consoleText.setText(consoleText.getText() + promptText);
        consoleText.moveCaretToEnd();
        return;
    } else if (input.trim() == "cd") {
        if (getProjectRoot) {
            juce::File root = getProjectRoot();
            if (root.isDirectory()) {
                currentWorkingDirectory = root;
            }
        }
        updatePromptText();
        consoleText.setText(consoleText.getText() + promptText);
        consoleText.moveCaretToEnd();
        return;
    }

    juce::StringArray args;
    if (currentShell == "powershell") {
        args.add("powershell");
        args.add("-NoProfile");
        args.add("-Command");
        args.add("Push-Location -LiteralPath '" + currentWorkingDirectory.getFullPathName() + "'; " + input);
    } else {
        juce::String wslPath = currentWorkingDirectory.getFullPathName().replace("\\", "/");
        if (wslPath.length() >= 2 && wslPath[1] == ':') {
            juce::String drive = wslPath.substring(0, 1).toLowerCase();
            wslPath = "/mnt/" + drive + wslPath.substring(2);
        }

        args.add("wsl");
        args.add("--");
        args.add("bash");
        args.add("-c");
        args.add("cd '" + wslPath + "'; " + input);
    }

    activeProcess = std::make_unique<juce::ChildProcess>();
    
    juce::uint32 flags = juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr;
    if (activeProcess->start(args, flags)) {
        startTimer(50);
    } else {
        logMessage("Failed to start shell process.");
        consoleText.setText(consoleText.getText() + promptText);
        consoleText.moveCaretToEnd();
        activeProcess.reset();
    }
}

void TerminalInstance::timerCallback()
{
    if (activeProcess) {
        juce::String output = activeProcess->readAllProcessOutput();
        if (output.isNotEmpty()) {
            consoleText.setText(consoleText.getText() + output);
            consoleText.moveCaretToEnd();
        }
        
        if (!activeProcess->isRunning()) {
            stopTimer();
            consoleText.setText(consoleText.getText() + "\n" + promptText);
            consoleText.moveCaretToEnd();
            activeProcess.reset();
        }
    }
}

void TerminalInstance::logMessage(const juce::String& message)
{
    consoleText.setText(consoleText.getText() + message + "\n");
    consoleText.moveCaretToEnd();
}

bool TerminalInstance::keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent)
{
    if (originatingComponent != &consoleText) return false;

    auto fullText = consoleText.getText();
    auto promptIndex = fullText.lastIndexOfIgnoreCase(promptText);
    if (promptIndex < 0) return false;

    int inputStartIndex = promptIndex + promptText.length();
    int caretPos = consoleText.getCaretPosition();
    auto selection = consoleText.getHighlightedRegion();

    // Allow copying
    if ((key.getKeyCode() == 'c' || key.getKeyCode() == 'C') && key.getModifiers().isCommandDown()) {
        return false;
    }

    bool isModifyingKey = key.getTextCharacter() != 0 || 
                          key == juce::KeyPress::backspaceKey || 
                          key == juce::KeyPress::deleteKey || 
                          ((key.getKeyCode() == 'v' || key.getKeyCode() == 'V') && key.getModifiers().isCommandDown()) ||
                          ((key.getKeyCode() == 'x' || key.getKeyCode() == 'X') && key.getModifiers().isCommandDown());

    if (isModifyingKey) {
        // Prevent backspace from deleting the prompt
        if (key == juce::KeyPress::backspaceKey && caretPos <= inputStartIndex && selection.isEmpty()) {
            return true; 
        }

        // If trying to modify read-only text, jump to the end
        if (caretPos < inputStartIndex || selection.getStart() < inputStartIndex) {
            consoleText.setCaretPosition(fullText.length());
            
            // Consume destructive keys entirely to prevent accidents
            if (key == juce::KeyPress::backspaceKey || key == juce::KeyPress::deleteKey || 
               ((key.getKeyCode() == 'x' || key.getKeyCode() == 'X') && key.getModifiers().isCommandDown())) {
                return true; 
            }
        }
    }

    return false;
}

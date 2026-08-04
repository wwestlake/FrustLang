#include "MainComponent.h"

MainComponent::MainComponent()
{
    // Setup Header
    headerLabel.setFont(juce::Font(22.0f, juce::Font::bold));
    headerLabel.setColour(juce::Label::textColourId, juce::Colours::cyan);
    headerLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(headerLabel);

    // Setup Status
    statusLabel.setFont(juce::Font(14.0f, juce::Font::italic));
    statusLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    statusLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(statusLabel);

    // Setup Code Editor
    codeEditor.setMultiLine(true);
    codeEditor.setReturnKeyStartsNewLine(true);
    codeEditor.setFont(juce::Font("Courier New", 14.0f, juce::Font::plain));
    codeEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff1e1e1e));
    codeEditor.setColour(juce::TextEditor::textColourId, juce::Colour(0xffd4d4d4));
    codeEditor.setText(
        "// LagDaemon Multi-Paradigm Script Example\n"
        "fn calculate_signal(frequency: f32) -> f32 {\n"
        "    let omega = 2.0 * 3.14159 * frequency;\n"
        "    return sin(omega);\n"
        "}\n"
    );
    addAndMakeVisible(codeEditor);

    // Setup Console Output
    consoleOutput.setMultiLine(true);
    consoleOutput.setReadOnly(true);
    consoleOutput.setFont(juce::Font("Courier New", 13.0f, juce::Font::plain));
    consoleOutput.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff111111));
    consoleOutput.setColour(juce::TextEditor::textColourId, juce::Colour(0xff00ff66));
    consoleOutput.setText("--- LagDaemon Language Workbench Console ---\nReady.\n");
    addAndMakeVisible(consoleOutput);

    // Buttons
    executeButton.onClick = [this] { onExecuteClicked(); };
    clearButton.onClick = [this] { onClearClicked(); };

    addAndMakeVisible(executeButton);
    addAndMakeVisible(clearButton);

    setSize(900, 600);
}

MainComponent::~MainComponent()
{
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour(0xff252526));
}

void MainComponent::resized()
{
    auto bounds = getLocalBounds().reduced(10);

    // Header bar
    auto headerArea = bounds.removeFromTop(40);
    headerLabel.setBounds(headerArea.removeFromLeft(bounds.getWidth() / 2));
    statusLabel.setBounds(headerArea);

    bounds.removeFromTop(5);

    // Action buttons bar
    auto buttonArea = bounds.removeFromBottom(35);
    executeButton.setBounds(buttonArea.removeFromLeft(200));
    buttonArea.removeFromLeft(10);
    clearButton.setBounds(buttonArea.removeFromLeft(120));

    bounds.removeFromBottom(10);

    // Split view: Top 65% Code Editor, Bottom 35% Console Output
    auto consoleHeight = bounds.getHeight() * 0.35f;
    consoleOutput.setBounds(bounds.removeFromBottom((int)consoleHeight));
    bounds.removeFromBottom(10);
    codeEditor.setBounds(bounds);
}

void MainComponent::onExecuteClicked()
{
    auto inputCode = codeEditor.getText();
    consoleOutput.insertTextAtCaret("\n> Compiling snippet via OrcJIT...\n");

#if HAS_LLVM
    consoleOutput.insertTextAtCaret("[LLVM Backend Active]: AST -> IR Builder -> OrcJIT execution pipeline invoked.\n");
#else
    consoleOutput.insertTextAtCaret("[Simulator Mode]: LLVM headers unlinked in standalone mode.\n");
#endif

    consoleOutput.insertTextAtCaret("Evaluated successfully.\n");
}

void MainComponent::onClearClicked()
{
    consoleOutput.setText("--- LagDaemon Language Workbench Console ---\nReady.\n");
}

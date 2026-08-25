#include <JuceHeader.h>
#include "WorkbenchComponent.h"

#include <cstdint>
#include <cstdio>
#include <iostream>

// Runtime helpers (frust_print_str, frust_format_*, frust_buf_*) now
// live in Runtime.cpp (01_language_paradigms/02_functional), compiled
// into the frust_runtime static library - the IDE links that instead of
// defining its own copy (see CMakeLists.txt). Also gives frust_jit_eval_f32
// (quote/unquote/build_time) for free, which the IDE never had before.
//
// Same force-link concern as frust_compiler's Main.cpp - nothing here
// calls these by name in C++, only the JIT finds them by symbol name, so
// without this the linker could drop Runtime.cpp's .obj as unreferenced.
#if defined(_WIN32)
#pragma comment(linker, "/include:frust_print_str")
#endif

class LagDaemonIDEApplication  : public juce::JUCEApplication
{
public:
    LagDaemonIDEApplication() {}

    const juce::String getApplicationName() override      { return "LagDaemon Language Research IDE"; }
    const juce::String getApplicationVersion() override   { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override             { return true; }

    void initialise (const juce::String& commandLine) override
    {
        mainWindow.reset (new MainWindow (getApplicationName()));
    }

    void shutdown() override
    {
        mainWindow = nullptr;
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    void anotherInstanceStarted (const juce::String& commandLine) override {}

    class MainWindow    : public juce::DocumentWindow
    {
    public:
        MainWindow (juce::String name)
            : DocumentWindow (name,
                              juce::Desktop::getInstance().getDefaultLookAndFeel()
                                                          .findColour (juce::ResizableWindow::backgroundColourId),
                              DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new WorkbenchComponent(), true);

           #if JUCE_IOS || JUCE_ANDROID
            setFullScreen (true);
           #else
            setResizable (true, true);
            setResizeLimits (800, 600, 3840, 2160);
            centreWithSize (1280, 800);
           #endif

            setVisible (true);
        }

        void closeButtonPressed() override
        {
            JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (LagDaemonIDEApplication)

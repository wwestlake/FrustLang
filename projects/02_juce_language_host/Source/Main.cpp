#include <JuceHeader.h>
#include "WorkbenchComponent.h"

#include <cstdint>
#include <cstdio>
#include <iostream>

// Same runtime helpers as frust_compiler's Main.cpp (see that file's
// comment) - the IDE's embedded REPL (ConsolePanel -> frust::ReplSession)
// JIT-executes Frust code inside THIS process, so it needs its own copy of
// these exported symbols; nothing in the CLI executable's image is visible
// here. Keep both copies in sync if the set changes.
//
// __declspec(dllexport) is MSVC-only - see frust_compiler's Main.cpp for
// the full explanation of FRUST_RUNTIME_EXPORT.
#if defined(_WIN32)
#define FRUST_RUNTIME_EXPORT extern "C" __declspec(dllexport)
#else
#define FRUST_RUNTIME_EXPORT extern "C" __attribute__((visibility("default")))
#endif

FRUST_RUNTIME_EXPORT void frust_print_f64(double val) {
    std::cout << val << "\n";
}
FRUST_RUNTIME_EXPORT void frust_print_str(const char* val) {
    std::cout << val << "\n";
}

namespace {
constexpr int kFormatBufferCount = 16;
constexpr size_t kFormatBufferSize = 64;
thread_local char formatBufferPool[kFormatBufferCount][kFormatBufferSize];
thread_local int formatBufferIndex = 0;

char* nextFormatBuffer() {
    char* buf = formatBufferPool[formatBufferIndex];
    formatBufferIndex = (formatBufferIndex + 1) % kFormatBufferCount;
    return buf;
}
} // namespace

FRUST_RUNTIME_EXPORT const char* frust_format_i64(int64_t val) {
    char* buf = nextFormatBuffer();
    std::snprintf(buf, kFormatBufferSize, "%lld", static_cast<long long>(val));
    return buf;
}
FRUST_RUNTIME_EXPORT const char* frust_format_f64(double val) {
    char* buf = nextFormatBuffer();
    std::snprintf(buf, kFormatBufferSize, "%g", val);
    return buf;
}
FRUST_RUNTIME_EXPORT const char* frust_format_bool(bool val) {
    return val ? "true" : "false";
}
FRUST_RUNTIME_EXPORT const char* frust_str_concat(const char* a, const char* b) {
    char* buf = nextFormatBuffer();
    std::snprintf(buf, kFormatBufferSize, "%s%s", a, b);
    return buf;
}

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

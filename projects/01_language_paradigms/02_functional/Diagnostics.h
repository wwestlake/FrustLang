#pragma once

// Where the compiler's own diagnostics ("frust: codegen error: ...") go.
//
// By default they go to std::cerr, exactly as before, so the command-line
// compiler is unchanged. A host that embeds the compiler installs a stream
// for the duration of a compile (see DiagnosticCapture) and reads the text
// back, so nothing needs a console, a temp file or a redirected std::cerr.
// The stream pointer is per thread: two threads can compile at once and each
// sees only its own diagnostics.

#include <iostream>
#include <ostream>

namespace frust {

inline std::ostream*& DiagnosticStreamSlot() {
    static thread_local std::ostream* slot = nullptr;
    return slot;
}

// The stream compiler diagnostics are written to on this thread.
inline std::ostream& Diag() {
    auto* stream = DiagnosticStreamSlot();
    return stream ? *stream : std::cerr;
}

// Sends this thread's diagnostics to `stream` until destroyed.
class DiagnosticCapture {
public:
    explicit DiagnosticCapture(std::ostream& stream) : previous(DiagnosticStreamSlot()) {
        DiagnosticStreamSlot() = &stream;
    }
    ~DiagnosticCapture() { DiagnosticStreamSlot() = previous; }
    DiagnosticCapture(const DiagnosticCapture&) = delete;
    DiagnosticCapture& operator=(const DiagnosticCapture&) = delete;

private:
    std::ostream* previous;
};

} // namespace frust

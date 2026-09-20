// Checks the embeddable compiler (CompilerApi.h): FRust source text in,
// structured diagnostics and object bytes out, in memory, touching no file.
// Exit code 0 = pass.

#include "CompilerApi.h"

#include <filesystem>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <thread>

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl;
    if (!ok) ++failures;
}

std::set<std::string> listFiles(const std::filesystem::path& dir) {
    std::set<std::string> names;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        // Only what a compile could plausibly leave behind; other programs use the temp folder too.
        const auto ext = entry.path().extension().string();
        if (ext == ".o" || ext == ".obj" || ext == ".ll" || ext == ".fr" || ext == ".frust" || ext == ".bc")
            names.insert(entry.path().filename().string());
    }
    return names;
}

frust::CompileRequest one(const std::string& name, const std::string& text) {
    frust::CompileRequest request;
    request.sources.push_back({ name, text });
    return request;
}

const char* kGood = "pub fn gain(input: f64, amount: f64) -> f64 = {\n    input * amount\n}\n";

bool anyMessageContains(const frust::CompileResult& r, const std::string& needle) {
    for (const auto& d : r.diagnostics)
        if (d.message.find(needle) != std::string::npos) return true;
    return false;
}

} // namespace

int main() {
    // Watch the working directory and the temp directory: a compile must add nothing to either.
    const auto cwd = std::filesystem::current_path();
    const auto tmp = std::filesystem::temp_directory_path();
    const auto cwdBefore = listFiles(cwd);
    const auto tmpBefore = listFiles(tmp);

    // Nothing may be written to the console either: diagnostics belong in the result.
    std::ostringstream captured;
    auto* oldCerr = std::cerr.rdbuf(captured.rdbuf());

    // 1. A good program compiles to an object, in memory.
    {
        const auto r = frust::Compile(one("gain.fr", kGood));
        check(r.ok, "good source compiles");
        check(r.diagnostics.empty(), "good source has no diagnostics");
        check(r.object.size() > 100, "an object file comes back as bytes");
        check(r.object.size() > 2 && r.object[0] == 0x64 && r.object[1] == 0x86, "the object is an x64 COFF file");
    }

    // 2. A syntax error is reported with file, line and column.
    {
        const auto r = frust::Compile(one("bad.fr", "pub fn f() -> i64 = {\n    let x = ;\n}\n"));
        check(!r.ok, "syntax error fails");
        check(r.object.empty(), "no object for a failed compile");
        check(!r.diagnostics.empty() && r.diagnostics[0].severity == frust::Diagnostic::Severity::Error, "the syntax error is an error diagnostic");
        check(!r.diagnostics.empty() && r.diagnostics[0].file == "bad.fr", "the diagnostic names the file");
        check(!r.diagnostics.empty() && r.diagnostics[0].line == 2 && r.diagnostics[0].column > 0, "the diagnostic has line 2 and a column");
        check(!r.diagnostics.empty() && r.diagnostics[0].phase == frust::Diagnostic::Phase::Parser, "it is a parser diagnostic");
        check(!frust::FormatDiagnostic(r.diagnostics[0]).empty() && frust::FormatDiagnostic(r.diagnostics[0]).rfind("bad.fr:2:", 0) == 0,
              "a diagnostic formats as file:line:column");
    }

    // 3. A lexer error carries its line.
    {
        const auto r = frust::Compile(one("lex.fr", "pub fn f() -> i64 = {\n    1 @ 2\n}\n"));
        check(!r.ok, "invalid character fails");
        bool sawLexer = false;
        for (const auto& d : r.diagnostics)
            if (d.phase == frust::Diagnostic::Phase::Lexer && d.line == 2) sawLexer = true;
        check(sawLexer, "the lexer diagnostic carries line 2");
    }

    // 4. Errors in several files are all reported in one pass.
    {
        frust::CompileRequest request;
        request.sources.push_back({ "a.fr", "pub fn a() -> i64 = ;\n" });
        request.sources.push_back({ "b.fr", "pub fn b() -> i64 = ;\n" });
        const auto r = frust::Compile(request);
        bool a = false, b = false;
        for (const auto& d : r.diagnostics) { a = a || d.file == "a.fr"; b = b || d.file == "b.fr"; }
        check(!r.ok && a && b, "errors in two files are both reported");
    }

    // 5. A code generation error comes back as a diagnostic, not on the console.
    {
        const auto r = frust::Compile(one("cg.fr", "pub fn f() -> i64 = missing_function(1)\n"));
        check(!r.ok, "a call to an unknown function fails");
        check(!r.diagnostics.empty() && r.diagnostics.back().phase == frust::Diagnostic::Phase::Codegen, "it is a codegen diagnostic");
        check(!r.diagnostics.empty() && !r.diagnostics.back().message.empty(), "the codegen diagnostic has a message");
        check(captured.str().empty(), "nothing was written to std::cerr");
    }

    // 6. Sibling files come from the host, not from disk.
    {
        frust::CompileRequest request = one("main.fr",
            "use self::helper;\npub fn quad(x: i64) -> i64 = twice(twice(x))\n");
        int asked = 0;
        request.siblingFiles = [&asked](const std::string& name, std::string& text) {
            ++asked;
            if (name == "helper.fr") { text = "pub fn twice(x: i64) -> i64 = x * 2\n"; return true; }
            return false;
        };
        const auto r = frust::Compile(request);
        check(r.ok, "use self:: resolves through the host's provider");
        check(asked >= 1, "the provider was asked for the file");

        frust::CompileRequest missing = one("main.fr", "use self::nothing;\npub fn f() -> i64 = 1\n");
        missing.siblingFiles = [](const std::string&, std::string&) { return false; };
        const auto m = frust::Compile(missing);
        check(!m.ok && anyMessageContains(m, "nothing"), "a missing sibling is an error that names it");

        const auto none = frust::Compile(one("main.fr", "use self::helper;\npub fn f() -> i64 = 1\n"));
        check(!none.ok, "use self:: with no provider is an error, not a disk lookup");
    }

    // 7. Pods come from the host too.
    {
        frust::CompileRequest request = one("main.fr", "import mathpod, \"1.0.0\";\npub fn f() -> i64 = 1\n");
        request.pods = [](const std::string& name, const std::string& version, frust::PodSource& pod) {
            if (name != "mathpod" || version != "1.0.0") return false;
            pod.ns = "mathpod";
            pod.sources.push_back({ "lib.fr", "use self::ops;\n" });
            pod.sources.push_back({ "ops.fr", "pub fn three() -> i64 = 3\n" });
            return true;
        };
        const auto r = frust::Compile(request);
        check(r.ok, "an imported pod resolves through the host's provider");

        request.pods = [](const std::string&, const std::string&, frust::PodSource&) { return false; };
        const auto m = frust::Compile(request);
        check(!m.ok && anyMessageContains(m, "mathpod"), "an unavailable pod is an error that names it");
    }

    // 8. Check-only and IR capture.
    {
        auto request = one("gain.fr", kGood);
        request.emitObject = false;
        request.captureIr = true;
        const auto r = frust::Compile(request);
        check(r.ok && r.object.empty(), "check-only succeeds without producing an object");
        check(r.irBeforeOptimization.find("define") != std::string::npos, "IR before optimization is returned");
        check(r.irAfterOptimization.find("define") != std::string::npos, "IR after optimization is returned");
    }

    // 9. Two compiles at once do not mix their diagnostics.
    {
        frust::CompileResult bad, good;
        std::thread t1([&] { bad = frust::Compile(one("cg.fr", "pub fn f() -> i64 = missing_function(1)\n")); });
        std::thread t2([&] { good = frust::Compile(one("gain.fr", kGood)); });
        t1.join();
        t2.join();
        check(!bad.ok && !bad.diagnostics.empty(), "the failing compile reports its error");
        check(good.ok && good.diagnostics.empty(), "the concurrent good compile has none of it");
    }

    // 10. The same input gives the same object.
    {
        const auto a = frust::Compile(one("gain.fr", kGood));
        const auto b = frust::Compile(one("gain.fr", kGood));
        check(a.ok && b.ok && a.object == b.object, "compiling twice gives identical bytes");
    }

    std::cerr.rdbuf(oldCerr);

    // Nothing was written anywhere.
    check(listFiles(cwd) == cwdBefore, "the working directory gained no file");
    check(listFiles(tmp) == tmpBefore, "the temp directory gained no file");

    std::cout << (failures == 0 ? "ALL PASSED" : "FAILURES: " + std::to_string(failures)) << std::endl;
    return failures == 0 ? 0 : 1;
}

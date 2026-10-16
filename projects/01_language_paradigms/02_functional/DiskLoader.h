#pragma once

// The disk half of FRust's module loading (see DiskLoader.cpp). Used by the command-line tools and by
// hosts whose sources live in real folders. Not part of frust_lang: an application that keeps its
// sources in the VFS does not link or call any of this.

#include "AST.h"
#include "CompilerApi.h"

#include <string>
#include <vector>

namespace frust {

// `use self::x;` resolved against files next to `baseDir` ("x.frust" then "x.fr").
bool ResolveSelfUsesFromDisk(Program* prog, AstArena& arena, const std::string& baseDir, std::vector<std::string>& errors);

// `use pod;` / `import pod, "v";` resolved from frate.json in the working directory and the Frate cache.
bool ResolveImportsFromDisk(Program* prog, AstArena& arena, std::vector<std::string>& errors);

SourceProvider DiskSourceProvider();  // a name is a path; returns its contents
PodProvider FratePodProvider();       // frate.json in the working directory and the Frate cache

// The command line's compile: reads `paths` from disk, compiles them as one unit (`use self::x;` next to
// the file that says it, pods through frate.json and the Frate cache), prints diagnostics on stderr and
// writes the object file to `outputPath`. Installs the disk resolvers. This is what `frust_compiler
// --emit-obj` and `frate build` both call: one compiler, in process, no program launched.
// dumpIr also writes output_pre_opt.ll / output_post_opt.ll.
bool CompileFilesToObjectFile(const std::vector<std::string>& paths, const std::string& outputPath,
                              const std::string& podNamespace = "", bool dumpIr = false);

// Makes the real disk the answer to the loader's two hooks (SetImportResolver, SetFileReader), so
// ResolveImports and the plugin host's path-based loads work as they always did. Safe to call more than once.
void InstallDiskResolvers();

} // namespace frust

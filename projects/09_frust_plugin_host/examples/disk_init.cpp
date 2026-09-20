// The plugin host's examples load plugins by file path, so they need the real disk installed as the loader's
// file access. The plugin host itself opens no file (see frust::SetFileReader); this object, linked into
// each example, installs the disk before main() runs.

#include "DiskLoader.h"

namespace {
const bool installed = (frust::InstallDiskResolvers(), true);
}

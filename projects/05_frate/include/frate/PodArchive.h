#pragma once

// A pod held in memory, and the .frpod package format read and written in
// memory. Nothing here opens a file: a host that keeps pods somewhere other
// than the disk (Djehuti Station keeps them in its VFS container) uses these
// instead of FrateCache / FratePodBuilder, which work on real folders.

#include <cstddef>
#include <map>
#include <string>

namespace frate {

// A pod's files: relative path (always '/' separators, e.g. "frate.json",
// "src/lib.fr") -> contents.
using PodFiles = std::map<std::string, std::string>;

// Packs a pod into .frpod bytes (a zip): frate.json plus every .fr and .fri
// file, at any depth. Same content rules as FratePodBuilder::packagePod.
// Deterministic: the same files give the same bytes.
bool packPod(const PodFiles& files, std::string& frpodBytes, std::string& error);

// Reads .frpod bytes into a pod's files. Refuses absolute paths and paths
// that climb out of the pod ("..").
bool unpackPod(const void* data, std::size_t size, PodFiles& files, std::string& error);

// A pod scaffold (frate.json plus the entry file), as `frate new` creates.
struct PodMetadata;
PodFiles scaffoldPodFiles(const PodMetadata& metadata);

} // namespace frate

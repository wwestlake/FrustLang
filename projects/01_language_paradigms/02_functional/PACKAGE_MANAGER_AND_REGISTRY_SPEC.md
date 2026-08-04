# Frust Package Manager, Server Storage & Registry Specification (`frustpack`)

## 1. Overview & Vision

**`frustpack`** is the official package management, build automation, dependency reconciliation, and registry ecosystem for **Frust** (**F**unctional **Rust**).

It enables developers to:
1. Build executable binaries (`bin`), pre-compiled/source libraries (`lib`), and resource bundles (`bundle`).
2. Manage dependencies with **Cargo-style Semantic Versioning (SemVer)** and reproducible **`frustpack.lock`** locking.
3. Package source code, config, binary artifacts, and resources into compressed `.frpack` archives.
4. Publish packages to the **LagDaemon Package Registry** (`https://lagdaemon.com/api/frustpack/`), automatically storing tarballs on **Amazon S3** and indexing metadata in **PostgreSQL**.

---

## 2. Package Artifact Output Types

| Target Type | Build Output | Description |
| :--- | :--- | :--- |
| **`bin`** | `.exe` (Win) / ELF (Linux) | Standalone AOT "Hard-Iron" executable binary. |
| **`lib`** | `.frust` source / `.lib` / `.a` / `.dll` | Reusable library module for JIT and AOT linking. |
| **`bundle`** | `.frpack` Archive | Complete distribution package containing source, config, binaries, and assets. |

---

## 3. Package Manifest (`frustpack.toml`) & Lockfile (`frustpack.lock`)

### Manifest Example (`frustpack.toml`)
```toml
[package]
name = "dsp_audio_suite"
version = "1.2.0"
authors = ["W. Westlake <admin@lagdaemon.com>"]
description = "Verifiable DSP Audio Components and SIMD Filter Engine"
license = "MIT"
edition = "2026"
type = "lib" # Options: "bin", "lib", "bundle"

[dependencies]
std = "1.0.0"
math_intrinsics = "^0.4.1"
juce_bridge = { version = ">=2.0.0, <3.0.0", registry = "lagdaemon" }

[resources]
assets = ["assets/presets/", "config/default.toml"]
```

### Reproducible Lockfile (`frustpack.lock`)
Guarantees byte-for-byte build reproducibility across machines using cryptographic BLAKE3 checksums:

```toml
[[package]]
name = "math_intrinsics"
version = "0.4.1"
checksum = "blake3:8f9a2b7c4d..."
source = "https://lagdaemon.com/api/frustpack/crates/math_intrinsics-0.4.1.frpack"
```

---

## 4. LagDaemon Package Registry & Server Architecture

```
[ Developer Terminal ] ───(frustpack publish)───► [ LagDaemon Registry API ]
                                                        │
                                   ┌────────────────────┴────────────────────┐
                                   ▼                                         ▼
                     [ PostgreSQL Database ]                      [ Amazon S3 Bucket ]
                     - Index Package Metadata                     - Store .frpack Archives
                     - SemVer Dependency Solver                   - Source + Config + Assets
                     - Author Authentication Tokens               - Fast CDN Distribution
```

### Server API Endpoints (`https://lagdaemon.com/api/frustpack/`)
* **`POST /api/v1/packages/publish`**: Authenticates user token, validates `.frpack` archive, stores tarball in S3, and indexes package metadata in PostgreSQL.
* **`GET /api/v1/packages/:name/:version/download`**: Fetches package archive from S3 storage.
* **`GET /api/v1/packages/search?q=:query`**: Queries PostgreSQL index for matching packages.

---

## 5. CLI Command Reference

```bash
# Initialize a new Frust project
frustpack new my_audio_app --bin

# Build debug (OrcJIT) or release (Hard-Iron AOT) binaries
frustpack build
frustpack build --release

# Install dependencies and update lockfile
frustpack update

# Package source, config, binaries, and assets into .frpack bundle
frustpack pack

# Publish package bundle to LagDaemon Registry (S3 + PostgreSQL index)
frustpack publish --token <REGISTRY_TOKEN>

# Search packages on the LagDaemon Registry
frustpack search dsp
```

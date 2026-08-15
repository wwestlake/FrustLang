# LagDaemon Language Research IDE Workbench Specification

## 1. Vision & Purpose

A dedicated **JUCE-based Desktop IDE & Research Platform** built specifically to house, edit, inspect, and execute our custom programming languages (Frust and future experimental paradigms).

The platform uses JUCE for its rich cross-platform UI system (with optional audio capability disabled by default) and features a fixed-layout workbench layout tailored for programming language research.

---

## 2. Workbench Layout & Component Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│ Top Menu Bar: [File]  [Edit]  [View]  [Project]  [Help]                     │
├──────────────┬──────────────────────────────────────────────┬───────────────┤
│ Left Panel:  │ Center Panel: Multi-Tab Code Editor          │ Right Panel:  │
│              ├──────────────────────────────────────────────┤               │
│ File Tree    │ [ main.frust ] [ math.frust ] [ + ]          │ Metadata &    │
│ Directory    │──────────────────────────────────────────────│ Inspection    │
│ Explorer     │ 1  mod math {                                │               │
│              │ 2      pub fn gain(s: f32) -> f32 =          │ - AST View    │
│ (Rooted at   │ 3          s * 1.5                           │ - Type Bounds │
│ project dir) │ 4  }                                         │ - SI Units    │
│              │                                              │ - Ports       │
├──────────────┴──────────────────────────────────────────────┴───────────────┤
│ Bottom Panel: Interactive Output & REPL Console Log                          │
│ > Compiling via OrcJIT... [Success in 0.4ms]                                │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 3. UI Component Details

### A. Top Menu Bar (`juce::MenuBarComponent`)
* **File**: `New File`, `Open Folder...` (Sets Root Directory), `Save`, `Save As`, `Close Tab`, `Exit`.
* **Edit**: `Undo`, `Redo`, `Cut`, `Copy`, `Paste`, `Find / Replace`.
* **View**: `Toggle Metadata Panel`, `Toggle Output Panel`, `Clear Console`.
* **Project**: `Build (JIT)`, `Build Hard-Iron (AOT)`, `Run REPL Session`, `Frate Package Settings`.
* **Help**: `Frust Language Wiki`, `About LagDaemon Research IDE`.

### B. Left Panel: File Tree Explorer (`juce::FileTreeComponent`)
* Displays directory hierarchy starting from a user-selected **Root Folder**.
* Double-clicking a `.frust`, `.toml`, `.cpp`, or text file opens it as a new tab in the central multi-tab editor.
* Right-click context menu (`New File`, `New Subfolder`, `Delete`, `Reveal in Explorer`).

### C. Center Panel: Multi-Tab Code Editor (`juce::TabbedComponent`)
* Tabbed interface supporting multiple open files simultaneously.
* Line numbering, syntax highlighting, bracket matching, and auto-indentation.
* Status bar at the bottom showing current line, column, character count, and file path.

### D. Right Panel: Metadata & Inspector (`juce::GroupComponent` / Custom Panel)
* **AST Inspector**: Displays the parsed AST hierarchy of the active editor file.
* **Type & Refinement Bounds**: Shows type predictions, refinement predicates (`f32[-1.0..1.0]`), and physical SI unit dimensions (`Meter/Second`).
* **Component Port Status**: For component files (`component` / `interface`), displays input/output ports and contract invariants (`where`).

### E. Bottom Panel: Output Window & Console (`juce::TextEditor`)
* Real-time compiler output, error tracebacks with line-number jumps, stdout/stderr streams, and LLVM OrcJIT execution logs.
* Integrated CLI input prompt for interactive REPL commands (`:type`, `:ir`, `:load`).

---

## 4. Technical Foundations (JUCE + CMake)

* **UI Framework**: JUCE 8 (using `D:/JUCE`).
* **Audio**: JUCE Audio Device Manager present but disabled/muted by default (ready for future DSP integration).
* **Build System**: CMake with Visual Studio 2022 generator on Windows.
* **LLVM Connection**: Hooks into LLVM OrcJIT for live in-memory code compilation directly from the editor tabs.

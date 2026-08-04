# LagDaemon First-Class Component Paradigm

## 1. Core Vision

In traditional programming, functions or objects are the primary abstraction. In the **LagDaemon Component Architecture**, the fundamental unit of computation is the **First-Class Component**.

A **Component** is an autonomous, encapsulated functional unit that:
1. Exposes **Verifiable Interfaces** with contractual input and output ports.
2. Is a **First-Class Value** (can be created dynamically, passed to higher-order components, transformed, and returned).
3. Connects to other components via **Type-Checked & Contract-Verified Wiring**, where connections are verified at compile-time or dynamic JIT link-time.

---

## 2. Verifiable Interfaces

An **Interface** specifies the precise contract of communication:

```ldfn
interface AudioProcessor {
    // Input Ports
    in signal: Stream<f32>[range: -1.0 .. 1.0]
    in control: ControlSignal[freq: <= 1000.0]

    // Output Ports
    out output: Stream<f32>[range: -1.0 .. 1.0]

    // Verifiable Invariant Contracts
    where {
        latency <= 64,          // Latency guarantee in samples
        thread_safe == true     // Real-time audio thread safety proof
    }
}
```

---

## 3. First-Class Component Definition

Components implement one or more interfaces and declare their internal behavior:

```ldfn
component GainFilter(factor: f32) : AudioProcessor {
    // Port implementation
    in signal
    in control
    out output

    // Internal reactive transformation
    output = signal * factor * (control / 1000.0)
}
```

---

## 4. First-Class Composition & Wiring

Because Components are first-class values, they can be created, stored in variables, composed, and connected dynamically:

```ldfn
// Higher-Order Component Assembler
fn build_processing_chain(gain: f32, lowpass_cutoff: f32) -> Component<AudioProcessor> = {
    let stage1 = GainFilter(gain)
    let stage2 = LowPassFilter(lowpass_cutoff)

    // Connect Stage 1 Output to Stage 2 Input
    // Compiler verifies that stage1.output matches stage2.signal contracts!
    let chain = stage1.output ~> stage2.signal

    return chain
}
```

---

## 5. Dynamic JIT Verification at Runtime

When running inside a host application (such as JUCE), new components can be compiled into memory via LLVM OrcJIT and plugged into existing running chains:

```
[ Running Host Audio Engine ]
            │
            ▼
[ Interface Verifier (OrcJIT) ]
    ├── Checks: Port Data Types (Stream<f32> == Stream<f32>)
    ├── Checks: Range Bounds & Constraints (-1.0..1.0)
    └── Result: VERIFIED -> Hot-swap Component into Graph
```

If a user tries to wire an incompatible component (e.g. mismatched sample rates or un-bounded range), the JIT compiler rejects the connection with a contract violation error before any execution takes place.

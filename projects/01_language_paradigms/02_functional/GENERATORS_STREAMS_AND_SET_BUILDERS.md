# Generated Streams, Lazy Sequences & Set-Builder Notation in Frust

## 1. Overview & Vision

In **Frust**, mathematical sequence generation and collection construction are first-class language constructs.

Rather than writing imperative `for` loops with accumulator vectors, Frust provides two complementary mechanisms:
1. **First-Class Mathematical Set-Builder Notation**: `{ expr | pattern <- sequence, predicate }` for sets, streams, arrays, and maps.
2. **Lazy Stream Generators (`yield` / `Stream<T>`)**: Infinite or finite demand-driven value streams compiled directly into zero-cost LLVM IR loops.

---

## 2. Mathematical Set-Builder Notation

Frust supports clean, readable set-builder comprehension syntax derived directly from mathematical set theory:

$$\{ x^2 \mid x \in [0..10], x \text{ is even} \}$$

### Frust Set-Builder Syntax

```frust
// 1. Computed Set Creation
let even_squares: Set<i32> = { x * x | x <- 0..10, x % 2 == 0 }
// Result Set: {0, 4, 16, 36, 64}

// 2. Computed Array Comprehension
let scaled_audio: Array<f32, 512> = [ sample * 1.5 | sample <- raw_buffer, sample > 0.01 ]

// 3. Computed Map Builder
let user_lookup: Map<u64, String> = { user.id: user.name | user <- user_list, user.is_active }
```

---

## 3. Lazy Stream Generators (`Stream<T>` & `yield`)

Generators produce streams of numbers or values on demand. Because evaluation is lazy, generators can represent **infinite mathematical sequences** (e.g. prime numbers, Fibonacci streams, audio LFO signals) with zero memory overhead.

### Infinite Fibonacci Stream Example
```frust
// Infinite Fibonacci Generator
pub fn fibonacci_stream() -> Stream<u64> = generator {
    let mut a: u64 = 0
    let mut b: u64 = 1
    loop {
        yield a
        let temp = a + b
        a = b
        b = temp
    }
}

// Consuming first 10 items lazily:
let first_10: Array<u64, 10> = fibonacci_stream().take(10)
```

### Audio DSP Sine Wave Stream
```frust
// Continuous Real-Time Audio Signal Stream
pub fn sine_generator(frequency: f32, sample_rate: f32) -> Stream<f32> = generator {
    let phase_step = (2.0 * 3.14159265 * frequency) / sample_rate
    let mut phase: f32 = 0.0
    loop {
        yield fast_sin(phase)
        phase = (phase + phase_step) % (2.0 * 3.14159265)
    }
}
```

---

## 4. Combining Set-Builders with Streams

Set-builder notation works seamlessly over infinite streams with lazy evaluation:

```frust
// Create a lazy filtered stream of audio peaks above threshold
let audio_peaks = stream { sample * 2.0 | sample <- sine_generator(440.0, 44100.0), abs(sample) > 0.8 }

// Pull 64 samples on demand into real-time JUCE audio buffer
let buffer: Array<f32, 64> = audio_peaks.take(64)
```

---

## 5. EBNF Grammar Addition

```ebnf
SetBuilderExpr    ::= "{" Expr "|" ComprehensionClause { "," ComprehensionClause } "}" ;
ArrayBuilderExpr  ::= "[" Expr "|" ComprehensionClause { "," ComprehensionClause } "]" ;
MapBuilderExpr    ::= "{" Expr ":" Expr "|" ComprehensionClause { "," ComprehensionClause } "}" ;

ComprehensionClause ::= Pattern "<-" Expr
                      | GuardExpr ;
```

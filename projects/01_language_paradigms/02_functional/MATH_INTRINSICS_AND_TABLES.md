# Math Architecture, Hardware SIMD & Compile-Time Lookup Tables in Frust

## 1. Overview & Vision

To achieve bare-metal performance ("aiming for the iron") while supporting advanced domain applications (JUCE audio DSP, 3D graphics, physics engines, and OS kernels), **Frust** divides math support into three distinct layers:

1. **Hardware Compiler Intrinsics**: LLVM-mapped scalar and SIMD vector primitives (`v128`, `v256`, `fma`, `sqrt`).
2. **Compile-Time Table Generation (`build_time`)**: Fast pre-computed lookup tables (LUTs) for trigonometric and transcendental functions generated at compile-time without runtime allocation or `libc` dependency.
3. **High-Level Math Libraries (`std::math`)**: User-land and standard library implementations of Vectors, Matrices, Complex Numbers, and Quaternions built on top of SIMD intrinsics.

---

## 2. Hardware SIMD & Intrinsic Primitives

Frust exposes fixed-width SIMD hardware register types mapped directly to x86 AVX2/AVX-512 and ARM NEON instructions:

```frust
// Hardware 128-bit and 256-bit SIMD primitives
type v128f = Simd<f32, 4>    // 4 x f32 (SSE/NEON)
type v256f = Simd<f32, 8>    // 8 x f32 (AVX2)

// Intrinsic operations execute in single CPU instruction cycles:
pub fn dot_product_4d(a: v128f, b: v128f) -> f32 = {
    let mul = intrinsic::simd_mul(a, b)
    return intrinsic::simd_reduce_add(mul)
}
```

---

## 3. "Going to the Metal": Compile-Time Lookup Tables (LUTs)

In bare-metal OS kernels or real-time JUCE audio threads, standard `libc` functions like `sin()` or `exp()` can be too slow, non-deterministic, or unavailable.

Frust uses `build_time` metaprogramming to **generate exact sine/cosine/exp lookup tables during compilation** and embed them as read-only static memory:

```frust
// Metaprogram: Generates a 2048-entry Sine Lookup Table at compile time!
fn SineTable(entries: usize) -> Array<f32, entries> = build_time {
    let mut table = Array::uninitialized(entries)
    for i in 0 .. entries {
        let phase = (i as f64 / entries as f64) * 2.0 * 3.141592653589793
        table[i] = compute_precise_sin(phase) as f32
    }
    return table
}

// Embedded read-only table in binary zero runtime initialization cost!
pub static SIN_LUT: Array<f32, 2048> = SineTable(2048)

// Ultra-fast interpolation lookup
pub fn fast_sin(phase_0_to_1: f32) -> f32 = {
    let index_f = phase_0_to_1 * 2048.0
    let index = (index_f as usize) % 2048
    return SIN_LUT[index]
}
```

---

## 4. High-Level Math Objects (`std::math`)

Using SIMD intrinsics and LUTs, `std::math` provides clean, composable data structures:

### Complex Numbers (`Complex<T>`)
```frust
record Complex<T> {
    real: T,
    imag: T
}

pub fn mul_complex(a: Complex<f32>, b: Complex<f32>) -> Complex<f32> = {
    Complex {
        real: a.real * b.real - a.imag * b.imag,
        imag: a.real * b.imag + a.imag * b.real
    }
}
```

### Vectors & Matrices (`Vec4`, `Mat4x4`)
```frust
record Vec4 {
    data: v128f // Backed by 128-bit hardware SIMD register!
}

record Mat4x4 {
    rows: Array<v128f, 4>
}

pub fn mul_mat_vec(m: Mat4x4, v: Vec4) -> Vec4 = {
    // Single-pass SIMD matrix multiplication
    Vec4 {
        data: intrinsic::simd_matrix_mul_vec(m.rows, v.data)
    }
}
```

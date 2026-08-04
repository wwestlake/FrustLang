# Monads & Composable Monadic Architecture in Frust

## 1. Overview & Vision

In **Frust**, **Monads** are first-class functional abstractions used to handle side-effects, error propagation, optionality, state transitions, and async operations—without sacrificing bare-metal performance or readable code.

Instead of mathematical category-theory jargon, Frust presents Monads with two complementary, zero-cost mechanisms:
1. **Clean `do` Expression Syntax** (For sequencing monadic steps cleanly).
2. **The `?` Propagation Operator** (For early exit error/optionality propagation).

---

## 2. Core Built-in Monadic Types

### 1. `Option<T>` (Optionality Monad)
Replaces `null` / `nullptr` crashes with explicit compile-time optionality:

```frust
type Option<T> =
    | Some(T)
    | None
```

### 2. `Result<T, E>` (Error Handling Monad)
Replaces exceptions with explicit, recoverable error results:

```frust
type Result<T, E> =
    | Ok(T)
    | Err(E)
```

### 3. `State<S, A>` (Pure State Monad)
Encapsulates stateful computations in pure functional code:

```frust
record State<S, A> {
    run: fn(S) -> (A, S)
}
```

---

## 3. Monad Syntax in Frust

### Option A: The `do` Expression Block
The `do` block unwraps monadic values sequentially:

```frust
pub fn calculate_total_gain(device_id: u32) -> Option<f32> = do {
    let dev  <- find_audio_device(device_id)    // Unwraps Option<Device>
    let chan <- dev.get_channel(0)             // Unwraps Option<Channel>
    let gain <- chan.read_gain_setting()        // Unwraps Option<f32>

    return Some(gain * 2.0)
}
```

### Option B: The `?` Try Propagation Operator
Identical to Rust's `?` operator, automatically unwrapping `Ok(val)` or returning `Err(err)` early:

```frust
pub fn load_config(path: &str) -> Result<Config, IoError> = {
    let file = File::open(path)?
    let text = file.read_to_string()?
    let cfg  = Config::parse(text)?
    Ok(cfg)
}
```

---

## 4. Generic `Monad` Interface Contract

Frust defines the formal `Monad` contract using verifiable interfaces:

```frust
interface Monad<M, A> {
    // Wrap value into monadic context
    fn pure(value: A) -> M<A>

    // Monadic Bind (flatMap)
    fn bind<B>(self: M<A>, f: fn(A) -> M<B>) -> M<B>

    where {
        thread_safe == true
    }
}
```

### Method Chaining (`.map` and `.and_then`)
All monadic types support zero-cost method chaining:

```frust
let final_value = find_user(42)
    .map(fn(user) => user.email)
    .and_then(fn(email) => send_verification(email))
    .unwrap_or("Fallback Email")
```

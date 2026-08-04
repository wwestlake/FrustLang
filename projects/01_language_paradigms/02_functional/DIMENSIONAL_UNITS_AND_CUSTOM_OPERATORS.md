# Compile-Time Dimensional Units & Custom Operator Definition in Frust

## 1. Overview & Vision

In **Frust**, physical dimensions and unit safety are checked at **compile-time with zero runtime overhead**.

In physics, engineering, graphics, and audio DSP, dimensional unit bugs (e.g. adding meters to seconds, or confusing milliseconds with samples) cause catastrophic crashes. Frust enforces dimensional safety at compile time, compiling down to raw primitive floats (`f32`/`f64`) in the machine code.

Additionally, Frust supports **Custom Operator Definition**, allowing developers to introduce clean domain-specific syntax with customizable precedence and associativity.

---

## 2. Compile-Time Physical Units & Dimensions

### Base SI Dimensions & Scientific Units
Frust includes built-in SI base dimensions and standard scientific units:

```frust
// SI Base Dimensions
unit Meter     : Length
unit Kilogram  : Mass
unit Second    : Time
unit Ampere    : ElectricCurrent
unit Kelvin    : Temperature
unit Mole      : AmountOfSubstance
unit Candela   : LuminousIntensity

// Derived Physical Units
unit Newton    : Force               = Kilogram * Meter / (Second ^ 2)
unit Joule     : Energy              = Newton * Meter
unit Watt      : Power               = Joule / Second
unit Hertz     : Frequency           = 1 / Second
unit Volt      : Voltage             = Watt / Ampere
unit Ohm       : Resistance          = Volt / Ampere
unit Pascal    : Pressure            = Newton / (Meter ^ 2)
```

### Common & Custom Unit Definitions with Conversion Rules
Developers can define custom or imperial units with conversion equations:

```frust
// Imperial & Common Unit Definitions
unit Foot : Length = 0.3048 * Meter
unit Inch : Length = Foot / 12.0
unit Mile : Length = 5280.0 * Foot

unit Hour : Time   = 3600.0 * Second
unit MPH  : Speed  = Mile / Hour
unit KPH  : Speed  = (1000.0 * Meter) / Hour

// Audio DSP Units
unit Sample : Time = 1.0 / 44100.0 * Second
unit Decibels       = log10_scale
```

### Dimensional Unit Compile-Time Enforcement

```frust
let dist: f32[Meter] = 100.0 * Meter
let time: f32[Second] = 9.58 * Second

// Compiler infers speed has type f32[Meter / Second]
let speed = dist / time

// CONVERSION IS AUTOMATIC AND SAFE:
let speed_mph: f32[MPH] = speed.to<MPH>()

// COMPILE-TIME ERROR:
// let invalid = dist + time // ERROR: Cannot add Length (Meter) to Time (Second)!
```

---

## 3. Custom Operator Definition

Frust allows defining custom infix, prefix, and postfix operators with explicit precedence and associativity:

### Infix Custom Operator (`infix operator`)
```frust
// Define custom pipe operator `|>` (Left associative, precedence level 5)
infix operator 5 left (|>)

pub fn (|>) <A, B>(val: A, func: fn(A) -> B) -> B = {
    return func(val)
}

// Usage:
let result = 42.0 |> sqrt |> fn(x) => x * 2.0
```

### Signal Wiring Operator (`~>`)
```frust
// Define custom component connection operator `~>`
infix operator 8 right (~>)

pub fn (~>) <A: Interface, B: Interface>(a: Component<A>, b: Component<B>) -> Component<Composite> = {
    return connect_ports(a.out_port, b.in_port)
}

// Usage:
let chain = Oscillator(440.0) ~> LowPassFilter(800.0) ~> AudioOut()
```

### Custom Prefix / Postfix Operators
```frust
// Postfix degree symbol operator
postfix operator (deg)
pub fn (deg)(val: f32) -> f32[Radians] = {
    return (val * 3.14159265 / 180.0) * Radians
}

// Usage:
let angle = 90.0 deg // Automatically converted to Radians at compile time!
```

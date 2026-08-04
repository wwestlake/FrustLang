# Built-in Smart Pointers & Memory Architecture in Frust

## 1. Vision: "Aiming for the Iron" Without Lifetime Friction

Rust's lifetime annotations (`'a`, `'b`, `&'a mut T`) are safe, but notoriously difficult to grok and write.

In **Frust**, we aim for the iron (capable of writing OS kernels, device drivers, and bare-metal audio engines) by building **Smart Pointers directly into the language as first-class primitives**—giving you Boost/C++-style smart pointer power with clean, intuitive syntax and zero template bloat.

---

## 2. The 5 Built-in Pointer Primitives

| Pointer Type | Syntax | Description | Use Case |
| :--- | :--- | :--- | :--- |
| **Unique (Owned)** | `Unique<T>` or `own T` | Exclusive ownership. Automatically freed on scope exit. Zero overhead. | Single-owner heap buffers, stack-to-heap allocation. |
| **Shared (Ref-Counted)**| `Shared<T>` or `shared T` | Shared ownership with atomic reference counting. Auto-managed. | Shared data graphs, UI components, multi-thread state. |
| **Weak (Observer)** | `Weak<T>` or `weak T` | Non-owning reference to `Shared<T>`. Prevents cyclic leaks. | Parent pointers, observer patterns, event listeners. |
| **Raw (Hardware)** | `Raw<T>` or `raw* T` | Direct physical memory address. Allows pointer arithmetic. | OS kernels, MMIO drivers, bare-metal hardware. |
| **Managed Ref** | `Ref<T>` or `&T` | Clean auto-borrowed reference. Scope inferred by compiler. | Function arguments, local temporary access. |

---

## 3. Code Examples in Frust

### Unique (Exclusive Owned) Pointer
```frust
fn create_buffer(size: usize) -> own AudioBuffer = {
    // Allocates unique buffer on the heap
    let buf = own AudioBuffer::new(size)
    return buf // Transferred out via move semantics
} // Automatically freed when dropped if not returned
```

### Shared & Weak Pointers
```frust
struct Node {
    value: i32,
    parent: weak Node,        // Prevents reference cycles!
    children: Vector<shared Node>
}

fn add_child(parent: shared Node, val: i32) = {
    let child = shared Node {
        value: val,
        parent: shared::downgrade(parent),
        children: Vector::new()
    }
    parent.children.push(child)
}
```

### Raw Hardware Pointers (Bare-Metal OS & Kernel MMIO)
For writing OS kernels, page tables, and hardware register access:

```frust
pub unsafe fn write_hardware_register(address: usize, value: u32) = {
    let ptr = address as raw* u32
    *ptr = value // Direct hardware MMIO write!
}
```

---

## 4. Comparison

| Feature | Rust | C++ (Boost/std) | Frust |
| :--- | :--- | :--- | :--- |
| **Lifetimes Syntax** | Mandatory `'a`, `'b` annotations | N/A | **Compiler Inferred** |
| **Smart Pointers** | External `Rc<T>`, `Arc<T>`, `Box<T>` | Complex templates `std::shared_ptr<T>` | **Language Built-in Primitives** |
| **OS Kernel & MMIO** | `*const T`, `*mut T` in `unsafe` | Raw pointers `T*` | **Built-in `raw* T`** |
| **Cycle Prevention** | `Weak<T>` | `std::weak_ptr<T>` | **Built-in `weak T`** |

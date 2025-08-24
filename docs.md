# CatBeater Language and Tooling Reference 📚

## 📖 Overview

CatBeater is a small language that compiles to a custom bytecode and executes via a native runtime.

- **Source files**: `.cb` (CatBeater source)
- **Compiled bytecode**: `.cat` (CatBeater bytecode)
- **Execution mode**: Native runtime (bytecode executor) with optional experimental numeric JIT
- **Triple syntax**: English, concise C-like, and modern concise forms (mix-and-match)

## 🚀 Quick Start

### Compile and Run
```bash
# Compile to bytecode (.cat) [default]
CatBeater.exe path\to\program.cb            # writes path\to\program.cat next to the source
CatBeater.exe --emit out.cat path\to\program.cb

# Run bytecode
CatBeater.exe program.cat
CatBeater.exe --run program.cat

# Bundle to a self-contained EXE (optional)
CatBeater.exe --bundle-exe program.exe path\to\program.cb
```

## 🤖 AI-Friendly Diagnostics and Auto-Fix

- **Errors include**: File, line, and often column; parser messages include brief hints for common issues
- **Suggestion/auto-fix patterns**:
  - `band/bor/bxor` → ensure `and` between args
  - `shl/shr` → ensure `by` before shift amount
  - `call NAME with a and b` expected; commas converted to `and`
  - `set NAME to VALUE`, `replace S X with Y`
- **Auto-fix defaults to ON**. Disable via environment `CB_AUTOFIX=0`

## 🎯 Values

| Type | Description | Example |
|------|-------------|---------|
| **number** | 64-bit floating (prints as integer if whole) | `42`, `3.14` |
| **string** | Text literals | `"hello"` |
| **bool** | Boolean values | `true`, `false` |
| **nil** | Absence of value (does not print) | `nil` |
| **array** | Mutable list | `[1, 2, 3]` |
| **function** | User-defined functions | `define function ... end` |

## 🔤 Expressions

- **Arithmetic**: `+ - * / %`
- **Comparison**: `> >= < <= == !=`
- **Logical**: `and`, `or` (truthy semantics). Concise aliases: `&&`, `||`
- **Grouping**: `(expr)`
- **Array literal**: `[expr, ...]`
- **Indexing**: `a[0]`
- **Function calls**: `f(1, 2)`

## 🍬 English Sugar (Aliases)

| English Form | Concise Equivalent |
|--------------|-------------------|
| `say EXPR` | `print EXPR` |
| `let NAME be VALUE`, `make NAME equal to VALUE` | `let NAME = VALUE` |
| `set NAME to VALUE`, `set NAME equal to VALUE` | `NAME = VALUE` |
| `append VAL to a`, `push VAL onto a`, `pop from a` | Array operations |
| `length of a` | `len a` |
| `if ... then ... else/otherwise ... end` | `if (...) { ... } else { ... }` |
| `while EXPR do STATEMENT \| while EXPR do ... end` | `while (...) { ... }` |
| `call add with 2 and 3` | `add(2, 3)` |
| `repeat N times do ... end` | Desugars to for-each over range |
| `range from A to B` (inclusive) | Array `[A..B]` |

## 📝 Statements

- **print EXPR [EXPR ...]**
- **let NAME be EXPR**
- **set NAME to EXPR**
- **set a[IDX] to EXPR**
- **if COND then (STATEMENT \| do ... end) [else/otherwise (STATEMENT \| do ... end)] end**
- **while COND do (STATEMENT \| do ... end) end**
- **define function NAME with parameters a, b returning number: (STATEMENT \| do ... end)**
  - The last expression in a function body is returned if no explicit return is used
- **return EXPR**

## 📋 Examples

### Math and Print
```bash
say "Calculator ready:"
3+1
add 2 and 5
subtract 2 from 9
(2+3)*4
10 % 3
```

### Variables and Arrays
```bash
let x be 10
x+2
set x to 20
x

let a be [1, 2, 3]
print a[0] a[1] a[2]
set a[1] to 42
append 4 to a
print a[0] a[1] a[2] a[3]
len a
```

### Control Flow
```bash
if 1 == 1 then do
    say "inside then block"
end else do
    say "inside else block"
end end

while x > 0 do do
    say x
    set x to x - 1
end end
```

### Functions
```bash
define function add with parameters a, b returning number: do
    let s be a + b
    s
end
print add(2, 3)
call add with 2 and 3
```

### Concise and Modern Forms
```c
// Classic concise
fn add(a, b) {
    return a + b;
}

// Modern concise (alias)
function add2(a, b) {
    return a + b;
}

// Modern concise arrow forms
function add3(a, b) -> a + b;

// Arrow if
if (x > 0) -> print("pos"); else -> print("neg");
```

### Rich strings and streams
```bash
print upper "hello"
print lower "HELLO"
print contains "cat" in "concatenate"
print format("Hi, {}!", "world")

let h be fopen("out.txt", "w")
fwrite(h, "hello\n")
fclose(h)
```

### Memory
```bash
let p be alloc 8
write8 65 to p at 0
write8 66 to p at 1
print read8 p at 0 read8 p at 1
let q be ptradd p by 4
write32 0x11223344 to p at 4
print read32 p at 4
free p
```

## 🛠️ Builtins (Language)

### Arrays
- `[x, y, z]` - Array literal
- `a[i]` - Index access
- `append v to a` / `push v onto a` - Add element
- `pop from a` - Remove last element
- `len a` / `length of a` - Array length

### Strings/Maps (VM Support)
- `char at I in S` - Character at index
- `substring of S from A to B` - Extract substring
- `find "needle" in S` - Find substring (index or -1)
- `ord of S` / `chr N` - Character code conversion
- `split S by SEP` - Split string by delimiter
- `new map` / `set key K of M to V` / `get K from M` / `has K in M` / `does M have K` - Map operations

### Packing & Helpers (VM Support)
- `pack16 N` → 2 bytes (LE)
- `pack32 N` → 4 bytes (LE)
- `pack64 N` → 8 bytes (f64, LE)
- `concat A and B` → string
- `join ARRAY by SEP` → string
- `trim S` → string
- `replace S X with Y` → string
- `upper S` / `lower S` → string
- `contains NEEDLE in HAYSTACK` → bool
- `format(fmt, ...args)` → string
- `assert COND` → fails if false; `panic MSG` → aborts with message
- `parse int S` / `parse float S` - String parsing
- `starts with P in S` / `ends with P in S` - String prefix/suffix
- `delete key K from M` / `keys of M` - Map operations
- `exists file PATH` - File existence check

### Numeric/Math (VM Support)
- `floor X`, `ceil X`, `round X`, `sqrt X`, `abs X`
- `pow A by B` - Exponentiation

### Bitwise (VM Support; numbers treated as 64-bit integers)
- `band A and B`, `bor A and B`, `bxor A and B`
- `shl A by B`, `shr A by B`

### Strings
- `tostring X` → string

### Arrays
- `reserve A by N`, `clear A`

### Maps
- `size of M`, `clear map M`

### Process
- `exit N`
 - `stdin()` / `stdout()` / `stderr()` → handles
 - `fopen PATH MODE` / `fclose H` / `fread H N` / `freadline H` / `fwrite H DATA`

### Functions
- `define function add with parameters a, b returning number: do ... end`
- **Call forms**: `add(2,3)` or `call add with 2 and 3`

## 💾 Memory (Native Runtime)

| Operation | Description | Example |
|-----------|-------------|---------|
| `alloc N` | Allocate N bytes | `let p = alloc(64)` |
| `free P` | Free pointer | `free(p)` |
| `ptradd P by K` | Pointer at P+K | `let q = ptradd(p, 4)` |
| `read8 P at K` | Read byte (0..255) | `let val = read8(p, 0)` |
| `write8 V to P at K` | Write byte | `write8(65, p, 0)` |
| `read16/32/64 P at K` | Read 16/32/64-bit value | `let val = read32(p, 4)` |
| `write16/32/64 V to P at K` | Write 16/32/64-bit value | `write32(0x12345678, p, 4)` |
| `readf32/writef32 V to P at K` | Read/write 32-bit float | `writef32(3.14, p, 8)` |
| `memcpy DST SRC N` | Copy memory | `memcpy(dst, src, 32)` |
| `memset DST VAL N` | Fill memory | `memset(p, 0, 64)` |
| `ptrdiff A B` | Signed offset | `let diff = ptrdiff(p, q)` |
| `realloc P SIZE` | Resize allocation | `let q = realloc(p, 128)` |
| `blocksize P` | Block size in bytes | `let size = blocksize(p)` |
| `ptroffset P` | Offset within block | `let offset = ptroffset(p)` |
| `ptrblock P` | Block ID | `let id = ptrblock(p)` |


## 🖥️ CLI Reference

| Command | Description |
|---------|-------------|
| `CatBeater.exe program.cb` | Compile source to bytecode (.cat) |
| `CatBeater.exe --emit program.cat program.cb` | Emit bytecode into provided path |
| `CatBeater.exe program.cat` | Run bytecode |
| `CatBeater.exe --run program.cat` | Run bytecode explicitly |
| `CatBeater.exe --bundle-exe program.cb program.exe` | Create self-contained EXE (bundled) |
| `CatBeater.exe` | Start REPL |

## 📁 File Types
## 📦 Modules / Import

- Use textual includes before parsing via either form (relative to the current file):
  - `use "path/to/file.cb"`
  - `import "path/to/file.cb"`
- Includes are expanded recursively and guarded against cycles.
- Recommended for sharing function definitions and constants between scripts.

## 🧠 Closures and Scopes (planned)

Closures and nested lexical scopes will be supported in a future native lowering.

- **`.cb`**: CatBeater source (English-like)
- **`.cat`**: CatBeater bytecode (binary)

## 🔬 Self-Host Pipeline (Work-in-Progress)

- The host writes an opcode manifest (`selfhost/opcodes.txt`) each run
- The minimal selfhost compiler (`selfhost/compiler.cb`) emits a bytecode file (`selfhost/demo.cat`)
- If present, the bytecode is executed by the native runtime
- This scaffold will evolve to a full compiler written in CatBeater

## ✅ Runtime Status (This Build)

### ✅ Supported Features
- **Literals**: Numbers, strings, booleans, nil, arrays
- **Arithmetic**: All basic operations with precedence
- **Comparisons**: All comparison operators
- **Logical**: `and`, `or` with truthy semantics
- **Print/Say**: Variadic printing
- **Arrays**: Literal, index get/set, len, append, pop, reserve, clear
- **Strings**: Index, substring, find, ord/chr, split, tostring
- **Maps**: New/get/set/has/del/keys, size, clear
- **Control Flow**: if/else, while with multi-statement blocks
- **Functions**: Global scope, parameters, local let
- **Memory Operations**: All basic and extended operations
- **File I/O**: Read/write operations

### ✅ Packing Helpers and Controls
- `pack16/pack32/pack64`, `concat/join/trim/replace`, `assert/panic`, `exit`

### ✅ Math/Bitwise
- `floor/ceil/round/sqrt/abs/pow`, `band/bor/bxor/shl/shr`

### 🚧 Not Yet Implemented
- Closures
- Nested lexical scopes with shadowing across blocks

### 📦 Standard Library

CatBeater now ships with a growing standard library written entirely in CatBeater itself. These modules provide common functionalities and serve as examples of modern concise CatBeater code.

Refer to [libraries.md](libraries.md) for detailed documentation on all standard library functions.

### 🔌 Foreign Function Interface (FFI)

The FFI allows CatBeater to call functions from external C/C++ DLLs. This enables interoperability with system APIs, graphics libraries, and more.
FFI calls require specifying the DLL name, function name (or ordinal), and a signature string for type marshaling.
FFI is enabled by default. To disable, set environment variable `CB_ENABLE_FFI=0`.

#### FFI Functions

*   **`__ffi_call(dllName, funcName, ...args)`**
    Calls a C function from `dllName` by `funcName`. All arguments are treated as 64-bit unsigned integers (`u64`), and the return value is also a `u64`.
    ```cat
    print(__ffi_call("kernel32.dll", "GetTickCount"))
    ```

*   **`__ffi_call_sig(dllName, funcName, signature, ...args)`**
    Calls a C function with explicit type marshaling. The `signature` string defines return and argument types using a mini-DSL (e.g., "`u32(f64, i32)`").
    
    **Supported Types in Signature:** `i32`, `u32`, `i64`, `u64`, `f32`, `f64`, `ptr`, `cstr` (C-style string), `wstr` (Wide string/UTF-16).
    
    ```cat
    ; Call a function returning u32 with no arguments
    print(__ffi_call_sig("kernel32.dll", "GetCurrentProcessId", "u32()"))
    
    ; Call a function with float arguments and float return
    print(__ffi_call_sig("msvcrt.dll", "pow", "f64(f64,f64)", 2, 3)) ; calculates 2^3
    
    ; Call with C-style string
    print(__ffi_call_sig("msvcrt.dll", "strncmp", "i32(cstr,cstr,i32)", "foo", "foobar", 3))
    
    ; Call with Wide (UTF-16) string
    print(__ffi_call_sig("kernel32.dll", "lstrlenW", "i32(wstr)", "hello"))
    ```

*   **`__ffi_proc(dllName, funcNameOrOrdinal)`**
    Retrieves a function pointer (as a `u64` number) for a function in `dllName`. `funcNameOrOrdinal` can be a string (function name) or a number (ordinal).
    This is useful for obtaining a function pointer once and calling it multiple times without repeated lookups.
    ```cat
    ; Get pointer to pow function by name
    let pPow be __ffi_proc("msvcrt.dll", "pow")
    
    ; Get pointer to lstrlenA by ordinal (example ordinal, may vary by OS)
    ; This will likely fail on your system as ordinals are not stable!
    ; let pLenA be __ffi_proc("kernel32.dll", 0x0014) ; Example ordinal for lstrlenA
    ```

*   **`__ffi_call_ptr(signature, ptr, ...args)`**
    Calls a function using a previously obtained function `ptr` and a `signature` string for type marshaling.
    ```cat
    ; Call pow using the pointer obtained above
    print(__ffi_call_ptr("f64(f64,f64)", pPow, 2, 4)) ; calculates 2^4
    
    ; Call lstrlenA using its pointer
    ; print(__ffi_call_ptr("i32(cstr)", pLenA, "abc"))
    ```

#### C++ Compatibility

CatBeater's FFI aims for robust C-style interoperability. For C++ libraries, direct compatibility is limited:

*   **Supported**: Calling `extern "C"` functions or C++ functions if their names are not mangled (or you provide the exact mangled name) and their signatures exclusively use primitive types (numbers, raw pointers, C-style strings).
*   **Not Supported (Directly)**: C++ classes, member functions (without manual `this` pointer passing and specific export), templates, standard library types (e.g., `std::string`, `std::vector`), exceptions, and passing complex structs/objects by value. These typically require a C wrapper layer.

## 💡 Design Notes

- **Numbers**: Printed as integers when whole; otherwise as floating
- **Truthiness**: `nil`/`0.0`/`false` are falsey; others truthy
- **Nil behavior**: Does not print; expressions should return concrete values
- **Runtime errors**: Include file and line when available
- **Memory debugging**: Set env `CB_MEMDBG=1` to print arrays/maps created/destroyed on halt
- **Interpreter vs VM**: The interpreter may support more features than the VM; use during development when needed

---

**For detailed documentation on the standard library, see [libraries.md](libraries.md).**

---

**🎯 CatBeater**: Bridging the gap between readable English and powerful systems programming.



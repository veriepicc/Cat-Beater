# CatBeater 🐱

> An English-like programming language, console, and future VM — built from scratch, the Terry Davis way.

## 🎯 Vision

1. **Language**: Build a readable, English-style language that scales from calculator to systems programming
2. **Bootstrap**: Re-implement the CatBeater compiler in CatBeater itself (self-hosting)
3. **Machine**: Target a native bytecode executor suitable for OS-level work

## 🚀 Status (v0.4: Native executor, console, variables, functions, arrays, memory, dual syntax)

### ✨ Current Features

- **Expressions**: `+ - * / %` with precedence and parentheses: `3+1`, `(2+3)*4`, `10 % 3`
- **Comparisons**: `> >= < <= == !=` and English: `is`, `is not`
- **Booleans and nil**: `true`, `false`, `nil`; hex numbers: `0x11223344`
- **Printing (variadic)**: `print "hello" 1 true` or `say 1 2 3`
- **Variables**: `let x be 10`, `make x equal to 10`, `set x to 20`
- **Arrays and indexing**: `let a be [1, 2, 3]`, `a[0]`, `append 4 to a`, `push 4 onto a`, `pop from a`, `length of a`
- **Maps/dicts**: `let m be new map`, `set key "x" of m to 42`, `get "x" from m`, `has "x" in m`, `does m have "x"`
- **Control flow**: Multi-statement blocks with `do ... end` for `if/else/otherwise` and `while`
- **Loops and helpers**: `for each x in a do ... end end`, `repeat 3 times do ... end`, `range from 1 to 5`
- **Functions**:
  - Single-line: `define function add with parameters a, b returning number: return a + b end`
  - Block body: `define function f with parameters a returning number: do let s be a+1 s end`
  - Call forms: `add(2, 3)`, `add 2 and 3`, or `call add with 2 and 3`
  - Last expression in a body returns implicitly if no `return`
- **Memory (experimental)**: `alloc N`, `free P`, `ptradd P by K`, `read8 P at K`, `write8 V to P at K`, `read32`/`write32`
  - Extended memory/pointers: `read16`/`write16`, `read64`/`write64`, `readf32`/`writef32`, `memcpy DST SRC N`, `memset DST VAL N`, `ptrdiff A B`, `realloc P SIZE`, `blocksize P`, `ptroffset P`, `ptrblock P`
- **Strings (basic)**: `char at I in S`, `substring of S from A to B`, `find "needle" in S` (index or -1), `ord of "A"`, `chr 65`, `split S by ","`, `tostring X`
  - Rich strings: `upper("hi")`, `lower("HI")`, `contains("hay", "haystack")`, `format("Hello, {}!", name)`
- **File I/O**: `read file PATH`, `write DATA to file PATH` (returns true/false)
  - Streams: `stdin()`, `stdout()`, `stderr()`, `fopen(PATH, MODE)`, `fread(H, N)`, `freadline(H)`, `fwrite(H, DATA)`, `fclose(H)`
- **Packing and helpers (for self-host)**: `pack16 N`, `pack32 N`, `pack64 N` (f64 LE), `concat A and B`, `trim S`, `replace S X with Y`, `join [..] by SEP`, `assert COND`, `panic MSG`, `exit N`
- **Math/bitwise**: `floor X`, `ceil X`, `round X`, `sqrt X`, `abs X`, `pow A by B`, `band A and B`, `bor A and B`, `bxor A and B`, `shl A by B`, `shr A by B`
- **Comments**: Lines starting with `;` or `#` (English), plus `//` and `/* ... */` (concise)
- **Console**: Runs after the script; type `exit` or `quit` to leave

### 🔥 Triple Syntax Support

CatBeater now supports **English**, **concise C-like ("rust syntax")**, and **modern concise** that can be mixed freely:

- **Concise (classic / "rust syntax")**: `fn f(a,b){ return a+b; }`, `if (x>0) { print(x); } else { print(0); }`, `while (n>0) { n = n - 1; }`
- **Concise (modern)**: `function f(a, b) { return a + b; }`, arrow form: `function f(a, b) -> a + b;`, terse decl: `x := 10;`, arrow-if: `if (x>0) -> print("pos"); else -> print("neg");`
- **Blocks**: `{ ... }` with optional `;` separators
- **Comments**: `// line`, `/* block */`
- **Logical**: `&&`, `||` in addition to English `and`, `or`

### 📦 Standard Library

CatBeater includes a growing standard library written in pure CatBeater. For detailed documentation on all available modules and functions, see the [Standard Library Reference](libraries.md).

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
    
    ; Get pointer to lstrlenA by name
    let pLenA be __ffi_proc("kernel32.dll", "lstrlenA")
    ```

*   **`__ffi_call_ptr(signature, ptr, ...args)`**
    Calls a function using a previously obtained function `ptr` and a `signature` string for type marshaling.
    ```cat
    ; Call pow using the pointer obtained above
    print(__ffi_call_ptr("f64(f64,f64)", pPow, 2, 4)) ; calculates 2^4
    
    ; Call lstrlenA using its pointer
    print(__ffi_call_ptr("i32(cstr)", pLenA, "abc"))
    ```

#### C++ Compatibility

CatBeater's FFI aims for robust C-style interoperability. For C++ libraries, direct compatibility is limited:

*   **Supported**: Calling `extern "C"` functions or C++ functions if their names are not mangled (or you provide the exact mangled name) and their signatures exclusively use primitive types (numbers, raw pointers, C-style strings).
*   **Not Supported (Directly)**: C++ classes, member functions (without manual `this` pointer passing and specific export), templates, standard library types (e.g., `std::string`, `std::vector`), exceptions, and passing complex structs/objects by value. These typically require a C wrapper layer.

### 🚧 Not Yet Implemented

- Sized integers (`i32/u32`)
- Concurrency

## 🚀 Getting Started

### Build Requirements
- **Visual Studio 2022** (C++20 modules support)
- Open `CatBeater.sln`, build, and run

### Quick Start
On launch, CatBeater will automatically run `test.cat` and then open the console:

```bash
CatBeater Console. Type expressions. 'exit' to quit.
> 3+1
4
> print "hello"
hello
> let x be 10
> x+2
12
> set x to 20
> x
20
> let a be [1, 2, 3]
> print a[0] a[1] a[2]
1 2 3
> exit
```

## 📚 Language Syntax

### 🔢 Math
```bash
3+1                    # Basic arithmetic
add 2 and 5            # English function calls
subtract 2 from 9      # Interpreted as 9 - 2
(2+3)*4                # Parentheses and precedence
10 % 3                 # Modulo
```

### 🖨️ Print
```bash
print "hello"          # Basic printing
say "hello"            # English alias
print "multiple words ok"
print 1+2              # Print expressions
```

### 📝 Variables
```bash
let x be 10            # Declaration
make x equal to 10     # Alternative declaration
x+2                    # Use in expressions
set x to 20            # Assignment
x                      # Display value
let a be [1, 2, 3]    # Array declaration
a[0]                   # Array indexing
append 4 to a          # Array operations
push 4 onto a
pop from a
length of a            # Array length
```

### 🔍 Comparisons
```bash
7 > 3                  # Numeric comparisons
3 <= 3
1 == 1                 # Equality
"a" != "b"             # String comparisons
true == true           # Boolean comparisons
nil == nil             # Nil comparisons
```

### 💬 Comments
```bash
; comment              # English-style comment
# comment              # Alternative comment
// comment             # C-style line comment
/* block comment */    # C-style block comment
```

### 🔥 Concise (C-like) Equivalents

#### Variables and Assignment
```c
let x = 10;            // Declaration
x = x + 1;             // Assignment
// Modern terse decl
x := 10;
```

#### Calls and Printing
```c
print("hello", 123);   // Function calls with parentheses
```

#### Conditionals
```c
if (x > 0) {           // If statements with braces
    print(x);
} else {
    print(0);
}
// Modern arrow-if
if (x > 0) -> print("pos"); else -> print("neg");
```

#### Loops
```c
while (n > 0) {        // While loops with braces
    n = n - 1;
}
// Modern arrow-while
while (n > 0) -> (n = n - 1);
```

#### Functions
```c
fn add(a, b) {         // Function definitions (classic concise)
    return a + b;
}
fn mul(a, b) {         // Last expression returns
    a * b;
}

// Modern concise alias
function add2(a, b) {
    return a + b;
}
// Modern arrow function
function add3(a, b) -> a + b;
```

### 🧠 Closures (planned)

Closures and nested lexical scopes will be supported in a future native lowering.

## 🔄 How Scripts Run

- The entire `test.cat` file is parsed into an AST (accumulating `do ... end` blocks)
- Compiled to a bytecode Chunk
- Executed by the native runtime
- Function bodies remain alive for later calls
- Expression results are echoed; statements don't echo values
- Interactive console starts with persistent variables

## 🗺️ Roadmap to Self-Hosting

| Milestone | Description | Status |
|-----------|-------------|---------|
| **1** | Control flow and types | ✅ Complete |
| **2** | Functions and scope | ✅ Complete |
| **3** | Modules and stdlib | 🔄 In Progress |
| **4** | Memory and systems | ✅ Complete |
| **5** | IR + Native runtime | ✅ Complete |
| **6** | Self-host | 🔄 Scaffold Ready |

### Milestone Details

- **Milestone 1**: Types (`number`, `bool`, `string`), control flow (`if/else`, `while`)
- **Milestone 2**: English functions, closures, lexical scope
- **Milestone 3**: `import math`, `use "file.cat"`, strings, arrays, maps, math
- **Milestone 4**: Pointers, buffers/slices, file IO, FFI hooks
- **Milestone 5**: Typed IR, bytecode runtime, compile to bytecode
- **Milestone 6**: Re-implement compiler in CatBeater, bootstrapped builds

> **Note**: You don't need to make a hardware CPU or real filesystem to self-host. We target our own bytecode VM and use the host OS filesystem via runtime.

## 📚 Standard Library

CatBeater includes a growing standard library written in pure CatBeater. For detailed documentation on all available modules and functions, see the [Standard Library Reference](libraries.md).

## 📁 Repository Layout

```
CatBeater/
├── src/
│   ├── Lexer.ixx          # Tokenization
│   ├── Parser.ixx         # Expressions and commands
│   ├── Expr.ixx           # AST expressions
│   ├── Stmt.ixx           # AST statements
│   ├── Codegen.ixx        # AST → bytecode
│   ├── NativeExec.ixx     # Native bytecode executor
│   └── JIT.ixx            # Experimental numeric JIT
├── test.cat               # Sample script
├── selfhost/              # CatBeater-in-CatBeater scaffolding
├── docs.md                # Language reference
├── terse-docs.md          # Concise syntax reference
└── README.md              # This file
```

## 🤝 Contributing

We're iterating rapidly! File issues/PRs with concise examples.

**Style Guide**: Clear names, early returns, minimal global state.

## 🤖 AI Developer Guide

### Goal
Make CatBeater easy and robust enough to write the CatBeater compiler in CatBeater.

### Principles
- **Total builtins**: No surprises. Nil doesn't print; runtime errors include file and line
- **English-first**: With concise sugar and predictable desugaring to VM calls
- **Incremental**: Small changes with tests in `test.cat` and clear docs

### How to Add a Builtin (Checklist)
1. **IR**: Add opcode in `src/IR.ixx` enum
2. **Runtime**: Implement opcode in `src/NativeExec.ixx`
3. **Codegen**: Route `__name` call in `src/Codegen.ixx` to opcode
4. **Parser**: Add syntax in `src/Parser.ixx` (command or primary expression)
5. **Docs**: Update `docs.md` and this README
6. **Tests**: Extend `test.cat` with example

### Error Reporting
- **Parse errors**: Show statement start line
- **VM runtime errors**: Show file and exact line (column when present)
- **Memory instrumentation**: Set `CB_MEMDBG=1`

### AI-Friendly Diagnostics and Auto-Fix
- Parse/lex errors include line and column with hints
- Targeted suggestions for common English forms
- Auto-fix enabled by default (disable with `CB_AUTOFIX=0`)
- Logs: `Auto-fix applied at line L: 'bad' -> 'good'`

### Common English Forms (Examples)

| Category | Examples |
|----------|----------|
| **Numeric** | `floor X`, `ceil X`, `round X`, `sqrt X`, `abs X`, `pow A by B` |
| **Bitwise** | `band A and B`, `bor A and B`, `bxor A and B`, `shl A by B`, `shr A by B` |
| **Strings** | `tostring X`, `concat A and B`, `trim S`, `replace S X with Y`, `split S by SEP` |
| **Arrays** | `reserve A by N`, `clear A`, `append V to A`, `pop from A`, `len A` |
| **Maps** | `let m be new map`, `set key "k" of m to v`, `get "k" from m`, `has "k" in m` |
| **Process** | `exit N` |

## 🏗️ Build and Run

### Build
```bash
# Visual Studio 2022 (x64 Release/Debug)
# Open CatBeater.sln and build
```

### Run
```bash
# Compile to bytecode (.cat) [default]
CatBeater.exe path\to\program.cb            # writes path\to\program.cat next to the source
CatBeater.exe --emit out.cat path\to\program.cb

# Run bytecode
CatBeater.exe program.cat

# Bundle a self-contained EXE (optional)
CatBeater.exe --bundle-exe path\to\program.cb program.exe
```

## 🔬 Self-Host Scaffold (Experimental)

- On startup, after running `test.cat`, the app can optionally run the selfhost compiler
- Any produced bytecode can be executed by the native runtime
- Current selfhost is a minimal scaffold intended to grow into the full compiler

---

**🎉 Welcome to CatBeater!** A language that bridges the gap between readable English and powerful systems programming.


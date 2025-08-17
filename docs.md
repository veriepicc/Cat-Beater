# CatBeater Language and Tooling Reference 📚

## 📖 Overview

CatBeater is a small language that compiles to a custom bytecode and executes via a native runtime.

- **Source files**: `.cb` (CatBeater source)
- **Compiled bytecode**: `.cat` (CatBeater bytecode)
- **Execution mode**: Native runtime (bytecode executor) with optional experimental numeric JIT
- **Dual syntax**: English and concise C-like forms (mix-and-match)

## 🚀 Quick Start

### Run Source (Native)
```bash
CatBeater.exe path\to\program.cb
```

### Emit Bytecode and Run
```bash
CatBeater.exe --emit program.cat path\to\program.cb
CatBeater.exe program.cat
```

### Run Bytecode
```bash
CatBeater.exe program.cat
CatBeater.exe --run program.cat
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

## 🖥️ CLI Reference

| Command | Description |
|---------|-------------|
| `CatBeater.exe program.cb` | Run source (native) |
| `CatBeater.exe --emit program.cat program.cb` | Emit bytecode while running |
| `CatBeater.exe program.cat` | Run bytecode |
| `CatBeater.exe --run program.cat` | Run bytecode explicitly |
| `CatBeater.exe` | Start REPL |

## 📁 File Types

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
- Rich strings
- Modules/import
- I/O streams

## 💡 Design Notes

- **Numbers**: Printed as integers when whole; otherwise as floating
- **Truthiness**: `nil`/`0.0`/`false` are falsey; others truthy
- **Nil behavior**: Does not print; expressions should return concrete values
- **Runtime errors**: Include file and line when available
- **Memory debugging**: Set env `CB_MEMDBG=1` to print arrays/maps created/destroyed on halt
- **Interpreter vs VM**: The interpreter may support more features than the VM; use during development when needed

---

**🎯 CatBeater**: Bridging the gap between readable English and powerful systems programming.



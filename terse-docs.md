# CatBeater Concise Syntax Reference

CatBeater supports both English and C-like concise syntax that can be mixed freely in the same file.

## Comments

```c
// Line comment
/* Block comment */
```

## Variables

```c
let x = 10;          // Declaration
x = 20;              // Assignment
let arr = [1, 2, 3]; // Array
arr[0] = 42;         // Index assignment
```

## Functions

```c
fn add(a, b) {
    return a + b;
}

fn multiply(a, b) {
    a * b;  // Last expression returns implicitly
}

// Modern concise alias
function add2(a, b) {
    return a + b;
}

// Call forms
add(2, 3);
print(add(5, 7));
```

## Control Flow

### Conditionals
```c
if (x > 0) {
    print("positive");
} else {
    print("zero or negative");
}

// Single statements (braces optional)
if (x > 0) print("positive");
```

### Loops
```c
while (n > 0) {
    print(n);
    n = n - 1;
}
```

## Operators

### Arithmetic
```c
x + y    // Addition
x - y    // Subtraction  
x * y    // Multiplication
x / y    // Division
x % y    // Modulo
```

### Comparison
```c
x > y    // Greater than
x >= y   // Greater than or equal
x < y    // Less than
x <= y   // Less than or equal
x == y   // Equal
x != y   // Not equal
```

### Logical
```c
a && b   // Logical AND
a || b   // Logical OR
```

## I/O

```c
print("hello", "world", 123);
print(x, y, z);
```

## Arrays

```c
let arr = [1, 2, 3, 4];
print(arr[0]);              // Access
arr[1] = 99;                // Assignment
print(arr.length);          // Length (or len(arr))
```

## Memory Operations

```c
let p = alloc(64);          // Allocate 64 bytes
write8(65, p, 0);           // Write byte 'A' at offset 0
let val = read8(p, 0);      // Read byte at offset 0
write32(0x12345678, p, 4);  // Write 32-bit value
let val32 = read32(p, 4);   // Read 32-bit value
free(p);                    // Free memory

// Extended memory operations
let q = realloc(p, 128);    // Resize allocation
let size = blocksize(p);    // Get block size
let diff = ptrdiff(p, q);   // Pointer difference
memcpy(dst, src, 32);       // Copy memory
memset(p, 0, 64);           // Fill memory
```

## Data Types

```c
42          // Number (64-bit float, prints as int if whole)
3.14        // Float
"hello"     // String
true        // Boolean
false       // Boolean
nil         // Null value
[1, 2, 3]   // Array
```

## Blocks and Statements

```c
{
    let x = 10;
    let y = 20;
    print(x + y);
    // Semicolons optional but recommended
}

// Empty statements (just semicolons) are ignored
{;;; let x = 5; ;;;;}
```

## String Operations

```c
let s = "hello world";
print(s[0]);                    // Character at index
print(s.substring(0, 5));       // Substring
print(s.find("world"));         // Find substring (index or -1)
print(s.split(" "));            // Split by delimiter
print(s.length);                // String length
```

## Math Functions

```c
floor(3.7);     // 3
ceil(3.2);      // 4
round(3.6);     // 4
sqrt(16);       // 4
abs(-5);        // 5
pow(2, 3);      // 8
```

## Bitwise Operations

```c
band(a, b);     // Bitwise AND
bor(a, b);      // Bitwise OR
bxor(a, b);     // Bitwise XOR
shl(a, 2);      // Shift left by 2
shr(a, 1);      // Shift right by 1
```

## File I/O

```c
let content = readfile("data.txt");
let success = writefile("out.txt", "hello");
```

## Error Handling

```c
assert(x > 0);          // Assert condition
panic("error message"); // Abort with message
exit(1);                // Exit with code
```

## Maps/Dictionaries

```c
let m = {};                    // New map
m["key"] = "value";            // Set key
let val = m["key"];            // Get value
print(m.has("key"));           // Check if key exists
delete m["key"];               // Delete key
print(m.keys());               // Get all keys
print(m.size());               // Map size
```

## Mixed Syntax Example

```c
// You can mix English and concise syntax freely
fn calculate(x, y) {
    let result = x * y;
    
    if (result > 100) {
        say "Large result!";    // English syntax
        return result;
    } else {
        print("Small result:", result);  // Concise syntax
        let doubled be result * 2;      // English syntax
        doubled                          // Return
    }
}

let answer = calculate(7, 8);
print("Final answer:", answer);
```

## Complete Program Example

```c
// Factorial function
fn factorial(n) {
    if (n <= 1) {
        return 1;
    } else {
        return n * factorial(n - 1);
    }
}

// Main program
print("Factorial calculator");
let numbers = [1, 5, 7, 10];

for (let i = 0; i < numbers.length; i++) {
    let n = numbers[i];
    let result = factorial(n);
    print("factorial(", n, ") =", result);
}

// Memory allocation example
let buffer = alloc(1024);
memset(buffer, 0, 1024);        // Zero out buffer
write32(0xDEADBEEF, buffer, 0); // Write magic number
let magic = read32(buffer, 0);   // Read it back
print("Magic number:", magic);
free(buffer);
```

## Comparison with English Syntax

| Concise | English |
|---------|---------|
| `fn add(a, b) { return a + b; }` | `define function add with parameters a, b returning number: return a + b end` |
| `if (x > 0) { print(x); }` | `if x > 0 then print x end` |
| `while (n > 0) { n = n - 1; }` | `while n > 0 do set n to n - 1 end` |
| `let x = 10;` | `let x be 10` |
| `x = 20;` | `set x to 20` |
| `print("hello", x);` | `print "hello" x` |
| `// comment` | `; comment` |
| `a && b` | `a and b` |
| `a || b` | `a or b` |

## 📦 Standard Library

CatBeater includes a growing standard library written in pure CatBeater. For detailed documentation on all available modules and functions, see the [Standard Library Reference](libraries.md).

## 🔌 Foreign Function Interface (FFI)

The FFI allows CatBeater to call functions from external C/C++ DLLs. This enables interoperability with system APIs, graphics libraries, and more.
FFI calls require specifying the DLL name, function name (or ordinal), and a signature string for type marshaling.
FFI is enabled by default. To disable, set environment variable `CB_ENABLE_FFI=0`.

### FFI Functions

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

### C++ Compatibility

CatBeater's FFI aims for robust C-style interoperability. For C++ libraries, direct compatibility is limited:

*   **Supported**: Calling `extern "C"` functions or C++ functions if their names are not mangled (or you provide the exact mangled name) and their signatures exclusively use primitive types (numbers, raw pointers, C-style strings).
*   **Not Supported (Directly)**: C++ classes, member functions (without manual `this` pointer passing and specific export), templates, standard library types (e.g., `std::string`, `std::vector`), exceptions, and passing complex structs/objects by value. These typically require a C wrapper layer.

## Notes

- Semicolons are optional but recommended for clarity
- Braces `{}` define code blocks
- Functions return the last expression if no explicit `return`
- All concise forms compile to the same bytecode as English equivalents
- You can mix both syntaxes in the same file or even the same function
- Comments use `//` for line comments and `/* */` for block comments
- Logical operators `&&` and `||` are aliases for `and` and `or`

---

**Build & CLI**

```bash
# Compile to bytecode (.cat) [default]
CatBeater.exe path\to\program.cb            # writes path\to\program.cat
CatBeater.exe --emit out.cat path\to\program.cb

# Run bytecode
CatBeater.exe program.cat

# Bundle to a self-contained EXE (optional)
CatBeater.exe CatBeater.exe --bundle-exe program.exe path\to\program.cb
```

**For detailed documentation on the standard library, see [libraries.md](libraries.md).**

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
| `a \|\| b` | `a or b` |

## Notes

- Semicolons are optional but recommended for clarity
- Braces `{}` define code blocks
- Functions return the last expression if no explicit `return`
- All concise forms compile to the same bytecode as English equivalents
- You can mix both syntaxes in the same file or even the same function
- Comments use `//` for line comments and `/* */` for block comments
- Logical operators `&&` and `||` are aliases for `and` and `or`

# CatBeater Standard Library

The CatBeater standard library provides a core set of utility functions for common tasks, written entirely in CatBeater. To use these modules, simply include them in your source files using the `include` keyword:

```
include "libs/string.cb"
include "libs/math.cb"

print(Trim("  hello  "));
print(Clamp(100, 0, 50));
```

---

## `libs/string.cb`

A collection of functions for string manipulation.

**`Trim(s)`**
Removes leading and trailing whitespace from a string.
```
Trim("  hello world  "); // "hello world"
```

**`Contains(needle, hay)`**
Checks if the string `hay` contains the substring `needle`. Returns `true` or `false`.
```
Contains("cat", "concatenate"); // true
```

**`Find(needle, hay)`**
Finds the first index of substring `needle` in string `hay`. Returns `-1` if not found.
```
Find("world", "hello world"); // 6
```

**`PadLeft(s, width, ch)`**
Pads the start of string `s` with the character `ch` until it reaches `width`.
```
PadLeft("7", 3, "0"); // "007"
```

**`PadRight(s, width, ch)`**
Pads the end of string `s` with the character `ch` until it reaches `width`.
```
PadRight("7", 3, "0"); // "700"
```

**`Repeat(s, count)`**
Repeats a string `s` for `count` times.
```
Repeat("ab", 3); // "ababab"
```

**`Join(arr, sep)`**
Joins the elements of an array `arr` into a single string, separated by `sep`.
```
Join([1, 2, 3], ", "); // "1, 2, 3"
```

**`Split(s, sep)`**
Splits a string `s` into an array of substrings using `sep` as the delimiter.
```
Split("a-b-c", "-"); // ["a", "b", "c"]
```

**`Replace(s, needle, repl)`**
Replaces all occurrences of `needle` in string `s` with the string `repl`.
```
Replace("a_b_a", "_", ":"); // "a:b:a"
```

**`StartsWith(s, prefix)`**
Checks if string `s` starts with `prefix`. Returns `true` or `false`.
```
StartsWith("hello", "he"); // true
```

**`EndsWith(s, suffix)`**
Checks if string `s` ends with `suffix`. Returns `true` or `false`.
```
EndsWith("hello", "lo"); // true
```

---

## `libs/fs.cb`

Functions for interacting with the file system.

**`Exists(path)`**
Checks if a file exists at the given `path`. Returns `true` or `false`.
```
Write("test.txt", "data");
Exists("test.txt"); // true
```

**`Read(path)`**
Reads the entire content of a file at `path` and returns it as a string.
```
Write("test.txt", "hello");
Read("test.txt"); // "hello"
```

**`Write(path, data)`**
Writes the string `data` to a file at `path`, overwriting it if it exists.
```
Write("test.txt", "new data");
```

**`Append(path, data)`**
Appends the string `data` to the end of a file at `path`.
```
Write("log.txt", "start\n");
Append("log.txt", "end\n");
```

**`Readline(path)`**
Reads just the first line from a file at `path`.
```
Write("multi.txt", "line1\nline2");
Readline("multi.txt"); // "line1"
```

---

## `libs/os.cb`

Functions for operating system interactions.

**`Stdout(msg)`**
Writes a message string to the standard output stream.
```
Stdout("Hello directly to stdout\n");
```

**`Stderr(msg)`**
Writes a message string to the standard error stream.
```
Stderr("This is an error message\n");
```

**`Exit(code)`**
Terminates the program with the given integer `code`.
```
Exit(1); // Exits with error code 1
```

---

## `libs/math.cb`

A comprehensive collection of mathematical utility functions.

### Core Functions

**`Clamp(x, lo, hi)`**
Clamps a number `x` to be within the inclusive range of `lo` and `hi`.
```
Clamp(100, 0, 10); // 10
```

**`Lerp(a, b, t)`**
Performs linear interpolation between `a` and `b` using interpolant `t`. For unclamped interpolation, see `LerpUnclamped`.
```
Lerp(0, 100, 0.5); // 50.0
```

**`Abs(x)`**, **`Floor(x)`**, **`Ceil(x)`**, **`Round(x)`**, **`Sqrt(x)`**
Standard math operations.
```
Abs(-10);   // 10
Floor(3.8); // 3
```

**`Pow(a, b)`**
Calculates `a` raised to the power of `b`.
```
Pow(2, 8); // 256
```

### Interpolation & Clamping

**`Clamp01(x)`**
Clamps a number `x` to be within the inclusive range of 0 and 1.

**`LerpUnclamped(a, b, t)`**
Performs linear interpolation between `a` and `b` without clamping `t`.

**`InverseLerp(a, b, v)`**
Calculates the `t` value that produces `v` when interpolating between `a` and `b`.

**`LerpAngle(a, b, t)`**
Linearly interpolates between two angles in degrees, handling wraparound correctly.

**`SmoothStep(a, b, x)`**
Performs a smooth Hermite interpolation between 0 and 1, as `x` ranges from `a` to `b`.

**`Step(edge, x)`**
Returns 0 if `x` is less than `edge`, and 1 otherwise.

### Repetition & Remapping

**`Mod(a, b)`**
Calculates the remainder of `a` divided by `b`.

**`Repeat(t, length)`**
Loops the value `t`, so that it is never larger than `length` and never smaller than 0.

**`PingPong(t, length)`**
Loops the value `t`, so that it goes back and forth between 0 and `length`.

**`Remap(value, from1, to1, from2, to2)`**
Remaps a `value` from one range (`from1`, `to1`) to another (`from2`, `to2`).

### Rounding & Signs

**`Trunc(x)`**
Returns the integer part of `x`, removing any fractional digits.

**`Sign(x)`**
Returns the sign of `x`: 1 for positive, -1 for negative, 0 for zero.

### Powers, Roots, and Logs

**`Exp(x)`**
Returns `e` raised to the power of `x`.

**`Log(x)`**
Returns the natural logarithm of `x`.

**`Log10(x)`**
Returns the base-10 logarithm of `x`.

**`Cbrt(x)`**
Returns the cube root of `x`.

**`InverseSqrt(x)`**
Returns `1 / Sqrt(x)`.

### Combinatorics

**`Factorial(n)`**
Computes the factorial of a non-negative integer `n`.

**`Combinations(n, k)`**
Computes the number of ways to choose `k` items from a set of `n` items.

### Trigonometry

**`Sin(x)`**, **`Cos(x)`**, **`Tan(x)`**
Standard trigonometric functions (input in radians).

**`ASin(x)`**, **`ACos(x)`**, **`ATan(x)`**
Inverse trigonometric functions.

**`ATan2(y, x)`**
Returns the angle in radians whose tangent is `y / x`.

**`DegToRad(d)`**
Converts an angle from degrees to radians.

**`RadToDeg(r)`**
Converts an angle from radians to degrees.

**`WrapAngle(a)`**
Wraps an angle in degrees to be between -180 and 180.

### Min/Max

**`Min(a, b)`**
Returns the smaller of two numbers.

**`Max(a, b)`**
Returns the larger of two numbers.

### Random Numbers

**`Random()`**
Returns a random float between 0.0 (inclusive) and 1.0 (exclusive).

**`RandomRange(min, max)`**
Returns a random float between `min` (inclusive) and `max` (exclusive).

**`RandomInt(min, max)`**
Returns a random integer between `min` (inclusive) and `max` (inclusive).

### Vector Operations (2D, 3D, 4D)

Vectors are represented as maps. Available functions per dimension:

— 2D (`Vec2`):
  - `Vec2(x, y)`
  - `Vec2Add(a, b)`, `Vec2Sub(a, b)`, `Vec2Mul(a, s)`, `Vec2Div(a, s)`
  - `Vec2Dot(a, b)`
  - `Vec2Length(a)`, `Vec2Normalize(a)`, `Vec2Distance(a, b)`
  - `Vec2Angle(a, b)`

— 3D (`Vec3`):
  - `Vec3(x, y, z)`
  - `Vec3Add(a, b)`, `Vec3Sub(a, b)`, `Vec3Mul(a, s)`, `Vec3Div(a, s)`
  - `Vec3Dot(a, b)`, `Vec3Cross(a, b)`
  - `Vec3Length(a)`, `Vec3Normalize(a)`, `Vec3Distance(a, b)`

— 4D (`Vec4`):
  - `Vec4(x, y, z, w)`
  - `Vec4Add(a, b)`, `Vec4Sub(a, b)`, `Vec4Mul(a, s)`
  - `Vec4Dot(a, b)`

### Matrix Operations

Functions for working with matrices, represented as arrays of arrays.

**`MatIdentity(n)`**
Creates an `n x n` identity matrix.

**`MatTranspose(m)`**
Transposes a matrix.

**`MatMul(a, b)`**
Multiplies two matrices.

**`MatDeterminant(m)`**
Computes the determinant of a square matrix.

**`MatInverse(m)`**
Computes the inverse of a square matrix. Returns `nil` if not invertible.

### Quaternion Operations

Functions for working with quaternions for 3D rotations. Quaternions are represented as maps.

**`Quat(w, x, y, z)`**
Creates a new quaternion.

**`QuatIdentity()`**
Returns the identity quaternion.

**`QuatMul(a, b)`**
Multiplies two quaternions.

**`QuatConjugate(q)`**
Computes the conjugate of a quaternion.

**`QuatLength(q)`**
Computes the magnitude of a quaternion.

**`QuatNormalize(q)`**
Returns a quaternion with a magnitude of 1.

### Color Helpers

**`RGB(r, g, b)`**
Creates a color map with red, green, and blue components.

**`HSV(h, s, v)`**
Creates a color map with hue, saturation, and value components.

**`HSVtoRGB(h, s, v)`**
Converts a color from HSV to RGB representation.

### Calculus and Numerical Methods

**`Derivative(f, x, h)`**
Computes the numerical derivative of a function `f` at point `x`.

**`Partial(f, vars, index, h)`**
Computes the partial derivative of a multivariate function.

**`Gradient(f, vars, h)`**
Computes the gradient of a multivariate function.

**`Integral(f, a, b, n)`**
Computes the numerical definite integral of a function `f` from `a` to `b`.

**`Limit(f, x, dx)`**
Computes the limit of a function.

**`Series(f, n)`**
Computes the sum of a series.

**`Sigmoid(x)`**
Computes the sigmoid function.

**`Softmax(arr)`**
Computes the softmax function for an array of numbers.

**`Erf(x)`**
Computes the error function.

**`Gamma(z)`**
Computes the gamma function.

**`Divergence(F, vars, h)`**
Computes the divergence of a vector field.

**`Curl(F, vars, h)`**
Computes the curl of a 3D vector field.

**`NewtonRaphson(f, df, x0, tol, maxIter)`**
Finds a root of a function using the Newton-Raphson method.

---

## `libs/map.cb`

Functions for creating and manipulating map (dictionary) objects.

**`New()`**
Creates and returns a new, empty map.
```
m := New();
```

**`Set(m, key, val)`**
Sets a `key` to a `val` in the map `m`.
```
m := New();
Set(m, "name", "CatBeater");
```

**`Get(m, key)`**
Retrieves the value for a `key` from map `m`.
```
Get(m, "name"); // "CatBeater"
```

**`Has(m, key)`**
Checks if a `key` exists in map `m`. Returns `true` or `false`.
```
Has(m, "name"); // true
Has(m, "version"); // false
```

**`Del(m, key)`**
Deletes a key-value pair from map `m`.
```
Del(m, "name");
```

**`Keys(m)`**
Returns an array containing all the keys in map `m`.
```
m := New();
Set(m, "a", 1);
Set(m, "b", 2);
Keys(m); // ["a", "b"] or ["b", "a"]
```

**`Size(m)`**
Returns the number of key-value pairs in map `m`.
```
Size(m); // 2
```

**`Clear(m)`**
Removes all key-value pairs from map `m`.
```
Clear(m);
Size(m); // 0
```

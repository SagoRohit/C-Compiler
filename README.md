# CSE 310 Assignment 4 — Intermediate Code Generation (Phase 2)

Student ID: 2005071

## What this is

An ANTLR4 (C++ target) visitor-based compiler that reads a program written in
the C-subset grammar from Assignment 3 and emits 32-bit x86 Linux assembly
for FASM (https://flatassembler.net/), following the conventions demonstrated
in `fasm/tutorial.pdf` and `fasm/codes/*.asm` (stack-frame based locals/params,
`INT 0x80` syscalls, the `OUTDEC` printing routine, jumping/short-circuit code
for boolean expressions).

Two files are produced from an input `.c` file:

- `code.asm` — generated straight from the parse tree, annotated with the
  originating source line as a comment above each statement's instructions.
- `optimized_code.asm` — `code.asm` after a peephole optimization pass
  (`PeepholeOptimizer.cpp`), implementing the four redundancy patterns from
  the spec: redundant `MOV a,b`/`MOV b,a`, redundant `PUSH x`/`POP x`,
  `ADD/SUB reg,0` and `MOV reg,1`+`MUL reg`, and collapsing runs of
  consecutive labels (rewriting jump/call targets accordingly).

Both files assemble and run under FASM (a Linux build of `fasm` is bundled at
`tools/fasm` in case it isn't installed on the grading machine).

## Phase 2 scope

This submission builds on the Phase 1 deliverable and implements the full
`P1_Grammar.pdf` grammar. On top of everything from Phase 1 (non-array
declarations, zero-argument functions, arithmetic/relational expressions,
assignment, postfix `++`/`--`, `println`, `return`), Phase 2 adds:

- **Arrays** — fixed-size global and local `int` arrays (`int a[N];`),
  indexing (`a[expr]`), including as an assignment target and with
  `++`/`--`. Global arrays are addressed as `[label + EBX]`; local arrays as
  `[EBP + ESI]` with `ESI` computed from the element's declared base offset,
  exactly matching `fasm/codes/s09_arrays.asm`.
- **Function parameters** — any number of parameters, passed via the stack
  at `[EBP+8], [EBP+12], ...` in declaration order.
- **Function calls** — arguments are pushed **right-to-left**, the callee
  cleans up its own arguments (`RET N`), and the result comes back in `EAX`,
  matching `fasm/codes/s15_function_calls.asm` / `s20_factorial.asm`
  (including recursion).
- **`if` / `if-else` / `while` / `for`** — jumping code built on the
  already-working `TEST EAX,EAX` / `Jcc` pattern, matching
  `fasm/codes/s11_*.asm`. `else if` chains fall out for free from the
  grammar's natural recursion (an `ELSE`'s statement can itself be another
  `IF`).
- **Logical `&&` / `||`** — short-circuit evaluation matching
  `fasm/codes/s12_logical.asm`'s exact semantics: on `&&`, a false left
  operand short-circuits to `0` without evaluating the right operand,
  otherwise the result is the right operand's raw value (not renormalized to
  `0`/`1`); symmetrically for `||` a true left operand short-circuits to `1`.
  This (not a naive "normalize both sides to booleans" approach) is what the
  official `input/test6_i.c` / `test7_i.c` expected outputs actually require.

Floating point is intentionally left as Phase 1 left it (`CONST_FLOAT`
literals truncate to `int`) since the spec explicitly does not require it.

## Files

- `CSubset.g4`, `Lexer.g4` — the grammar (same productions as Assignment 3;
  file renamed to match the grammar name, which newer ANTLR versions require).
- `CGSymbol.h` — scope/symbol bookkeeping for code generation: global label
  vs. `[EBP±N]` stack operand, plus array metadata (element count, and either
  the data-segment label for a global array or the base stack offset for a
  local one).
- `CodeGenVisitor.h/.cpp` — the visitor that walks the parse tree and emits
  assembly (no embedded actions in the grammar file, as required).
- `PeepholeOptimizer.h/.cpp` — the optimization pass, operating purely on the
  text of `code.asm`.
- `main.cpp` — driver: parses the input file, runs the visitor, writes
  `code.asm`, runs the optimizer, writes `optimized_code.asm`.
- `build.sh` — generates the parser from the grammar (via ANTLR) and compiles
  everything into `./icg.out`.
- `run.sh <input.c>` — builds if needed, runs `icg.out`, assembles both
  `.asm` files with FASM, and runs both resulting executables.
- `clean.sh` — removes everything `build.sh`/`run.sh` generate, restoring a
  fresh checkout.
- `tests/*.c` + `tests/expected_output.txt` — the Phase 1 sample programs
  plus four new Phase 2 ones (`t7_control_flow.c`, `t8_arrays.c`,
  `t9_functions_params.c`, `t10_logical.c`) covering each new construct in
  isolation. (`tests/online_*.c` / `t_online_a1.c` are extra ad hoc scalar
  scratch programs kept from Phase 1 development; they're not part of the
  graded regression set.)
- `tools/fasm` — a bundled Linux FASM 1.73.32 binary (used automatically by
  `run.sh` if `fasm` isn't already on `PATH`).

The primary regression suite for Phase 2, though, is `../input/*.c` (the
official sample programs) checked against `../input/output.txt` — those
exercise every new construct together (arrays, recursion, nested control
flow, short-circuit logic) and all pass byte-for-byte, both unoptimized and
optimized.

## How to run

```
./run.sh tests/t8_arrays.c
```

or, against one of the official samples:

```
./run.sh ../input/test6_i.c
```

This regenerates `code.asm` / `optimized_code.asm` in the current directory,
assembles them into `code.out` / `optimized_code.out`, and runs both.

## Design notes

- Every expression-generating visit leaves its result in `EAX` on exit. A
  binary operator evaluates the right operand first, `PUSH`es it, evaluates
  the left operand into `EAX`, `POP`s the right operand into `EBX`, then
  operates `EAX`/`EBX` — the exact pattern used throughout the FASM tutorial.
- All locals live on the stack (`[EBP-N]`), allocated on function entry via
  `SUB ESP, N`. Since the total frame size is only known after the whole
  function body has been visited, each function's body is generated into an
  in-memory buffer first (`CodeGenVisitor::funcBodyBuffer` — the "at most one
  temporary file" the spec allows is implemented as an in-memory buffer, so
  zero on-disk temp files are actually needed) and only written out, wrapped
  in the now-known prologue/epilogue, once the function is fully visited.
  Parameters are pre-declared into their own scope (positive `[EBP+N]`
  offsets) before the body is visited, one level outer to the body's own
  `compound_statement` scope.
- `main()` is emitted as the literal `main:` label (matching `entry main`)
  and always exits via `INT 0x80`; every other function is emitted as
  `func_<name>` (to avoid colliding with FASM instruction mnemonics, e.g. a
  C function named `add`) and returns via `RET N`, where `N` is
  `paramCount * 4` (callee-cleans-up convention).
- Relational operators and `!` use jumping code (`CMP`/`Jcc` into a 0/1
  result), as required by the spec for boolean expressions.
- Resolving a `variable` (`CodeGenVisitor::addressOf`) returns the asm
  operand text to use directly in `MOV`/`PUSH`/etc. For a scalar this is a
  fixed string with no side effects; for an array element it *emits* the
  index-expression evaluation and offset computation (index × 4, plus the
  array's base offset for locals) as a side effect and returns an operand
  that references the just-computed `EBX`/`ESI`. Call sites that already
  hold a live value in `EAX` (e.g. the right-hand side of `x[i] = expr`)
  `PUSH`/`POP` it around the `addressOf` call so the index computation can't
  clobber it.

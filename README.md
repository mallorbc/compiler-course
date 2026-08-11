# Compiler-Course
Repository for EECS 6083.

In this project I was tasked with writing a complete compiler for a custom language spec without using any compiler tools. 
The compiler will compile the custom language to C or LLVM.  I was allowed to use any language of my choice and chose C++. 

## Assignment Documents
The assignment PDFs were never committed while I was taking the class (~2019). They were recovered from the web in July 2026: the professor's course page is still live, and the Wayback Machine holds pre-2024 snapshots. See [docs/assignment/](docs/assignment/) for the language specification (`projectLanguage.pdf`), the project description (`project.pdf`), and [PROVENANCE.md](docs/assignment/PROVENANCE.md) for where each copy came from and how close it is to the 2019 original.

## Compiling the Compiler
Compiling this compiler uses g++.

```cli
g++ <all source files> -o <output binary> -g(used for debugging)
```

Alternatively, use the make command along with the Makefile to ease this process. 
```cli
make
```

This command will compile all the files together and create an excutable called "compiler".  After running this command, run the following to clean the workspace and prepare for another compilation:

```cli
make clean
```
## Usage of Compiler
After the compiler is compiled, it can be used but running the executable in the cli, along with passing along a text file to be compiled.

```cli
./<name of binary> <name of file to compile>
```

The one-argument form remains a check-only frontend invocation. The current
restricted-C backend can be requested explicitly for Integer, Bool, Float, and
String Programs and procedures, including scalar control flow/I/O, `sqrt`, and
typed procedure fallthrough (deterministic Integer `0`, Float `+0.0`, Bool
`false`, or empty String), plus Stage 6D1 array storage, checked indexing, whole
copies/conversions, and exact by-value user-procedure parameters, plus Stage 6D2
elementwise unary/binary operators and scalar broadcast. The backend can still
be requested as C source without invoking a host compiler.
Float is carried as an exact
IEEE binary32 word in the fixed `int32_t` memory/register model.  String values
are word-index handles to one-byte-per-word, zero-terminated immutable data in
that same memory; an array occupies one MM word per element, including String
handle elements.  Consumers of
the emitter API must map `RestrictedCLink::Math` to their platform math-library
link option when present:

```cli
./compiler --emit-c <output.c> <source file>
```

On standard Linux, Stage 7 can instead compile and atomically publish a native
executable (the three option spellings are equivalent):

```cli
./compiler -o <output> <source file>
./compiler --output <output> <source file>
./compiler --native <output> <source file>
```

Native mode uses `cc` by default. `CC` may select one executable path or name;
its value is never split into shell words, and `CFLAGS`/`LDFLAGS` are not read.
The driver passes `-std=c11`, private absolute temporary input/output paths, and
one trailing `-lm` only when the emitter reports the typed Math dependency. It
uses `posix_spawnp` rather than a shell and replaces an existing regular output
only after the compiler succeeds and its product is validated. Direct or
dangling output symlinks, directories, missing parents, and source/output or
host-compiler/output aliases are rejected. See the
[Stage 7 note](docs/notes/2026-08-11-stage7-native-driver.md) for the transaction
and POSIX portability boundary.

## Testing the Compiler
The shell script *test_all.sh* builds the compiler using the make file, then proceeds to test the compiler using the script and test files. These test files are located in testPgms/correct, which are test files provided by the professor, and in testPgms/custom for a correct test file that I made, and testPgms/fail which are files that I made that do not work.

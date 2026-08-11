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
restricted-C backend can be requested explicitly for the supported scalar
straight-line subset, including the `putInteger` runtime sub-slice; it writes
C source but deliberately does not invoke a C compiler or produce an
executable yet:

```cli
./compiler --emit-c <output.c> <source file>
```

## Testing the Compiler
The shell script *test_all.sh* builds the compiler using the make file, then proceeds to test the compiler using the script and test files. These test files are located in testPgms/correct, which are test files provided by the professor, and in testPgms/custom for a correct test file that I made, and testPgms/fail which are files that I made that do not work.

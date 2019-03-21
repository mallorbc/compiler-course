# Compiler-Course
Repository for EECS 6083.

In this project I was tasked with writing a complete compiler for a custom language spec without using any compiler tools. 
The compiler will compile the custom language to C or LLVM.  I was allowed to use any language of my choice and chose C++. 

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

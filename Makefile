# compiler: main.o scanner.o parser.o SymbolTable.o CustomFunctions.o TypeChecker.o
# 	g++ main.o scanner.o parser.o SymbolTable.o CustomFunctions.o TypeChecker.o -o compiler -g

# tester: UnitTests.o scanner.o parser.o SymbolTable.o CustomFunctions.o TypeChecker.o
# 	g++ UnitTests.o scanner.o parser.o SymbolTable.o CustomFunctions.o TypeChecker.o -o UnitTests -g
compiler: main.o scanner.o parser.o SymbolTable.o CustomFunctions.o
	g++ main.o scanner.o parser.o SymbolTable.o CustomFunctions.o -o compiler -g

tester: UnitTests.o scanner.o parser.o SymbolTable.o CustomFunctions.o
	g++ UnitTests.o scanner.o parser.o SymbolTable.o CustomFunctions.o  -o UnitTests -g

UnitTests.o: UnitTests.cpp token.h
	g++ -c UnitTests.cpp -g

main.o: main.cpp token.h
	g++ -c main.cpp -g

scanner.o: scanner.cpp scanner.h token.h
	g++ -c scanner.cpp -g

parser.o: parser.cpp parser.h token.h
	g++ -c parser.cpp -g

SymbolTable.o: SymbolTable.cpp SymbolTable.h token.h
	g++ -c SymbolTable.cpp -g


CustomFunctions.o: CustomFunctions.cpp CustomFunctions.h token.h
	g++ -c CustomFunctions.cpp -g

# TypeChecker.o: TypeChecker.cpp SymbolTable.cpp token.h
# 	g++ -c TypeChecker.cpp -g

clean:
	rm *.o compiler UnitTests

compiler: main.o scanner.o parser.o SymbolTable.o CustomFunctions.o ScopeTable.o
	g++ main.o scanner.o parser.o SymbolTable.o CustomFunctions.o ScopeTable.o -o compiler -g

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

ScopeTable.o: ScopeTable.h ScopeTable.cpp token.h
	g++ -c ScopeTable.cpp -g

clean:
	rm *.o compiler

compiler: main.o scanner.o parser.o SymbolTable.o CustomFunctions.o
	g++ main.o scanner.o parser.o SymbolTable.o CustomFunctions.o -o compiler -g

main.o: main.cpp token.h
	g++ -c main.cpp

scanner.o: scanner.cpp scanner.h token.h
	g++ -c scanner.cpp

parser.o: parser.cpp parser.h token.h
	g++ -c parser.cpp

SymbolTable.o: SymbolTable.cpp SymbolTable.h token.h
	g++ -c SymbolTable.cpp


CustomFunctions.o: CustomFunctions.cpp CustomFunctions.h token.h
	g++ -c CustomFunctions.cpp

clean:
	rm *.o compiler

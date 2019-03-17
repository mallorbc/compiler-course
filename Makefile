compiler: main.o scanner.o parser.o SymbolTable.o CustomFunctions.o
	g++ main.o scanner.o parser.o SymbolTable.o CustomFunctions.o -o compiler -g

main.o:
	g++ -c main.cpp

scanner.o:
	g++ -c scanner.cpp

parser.o:
	g++ -c parser.cpp

SymbolTable.o:
	g++ -c SymbolTable.cpp


CustomFunctions.o:
	g++ -c CustomFunctions.cpp

clean:
	rm *.o compiler

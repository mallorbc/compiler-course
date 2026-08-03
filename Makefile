CXX = g++
#-MMD -MP writes a .d file per object with its real header dependencies, so
#editing any header rebuilds every translation unit that includes it (the
#hand-listed prerequisites below are incomplete, e.g. parser.h pulls in
#scanner.h and Typechecker.h)
CXXFLAGS = -g -Wall -Wextra -Werror=return-type -MMD -MP

#everything except main.o, so the unit test binary can supply its own main
CORE_OBJS = scanner.o parser.o SymbolTable.o CustomFunctions.o ScopeTable.o Typechecker.o
UNIT_SRCS = $(wildcard tests/unit/*.cpp)
UNIT_BIN = tests/unit_tests

compiler: main.o $(CORE_OBJS)
	$(CXX) main.o $(CORE_OBJS) -o compiler $(CXXFLAGS)

main.o: main.cpp token.h
	$(CXX) -c main.cpp $(CXXFLAGS)

scanner.o: scanner.cpp scanner.h token.h
	$(CXX) -c scanner.cpp $(CXXFLAGS)

parser.o: parser.cpp parser.h token.h
	$(CXX) -c parser.cpp $(CXXFLAGS)

SymbolTable.o: SymbolTable.cpp SymbolTable.h token.h
	$(CXX) -c SymbolTable.cpp $(CXXFLAGS)


CustomFunctions.o: CustomFunctions.cpp CustomFunctions.h token.h
	$(CXX) -c CustomFunctions.cpp $(CXXFLAGS)

ScopeTable.o: ScopeTable.h ScopeTable.cpp token.h
	$(CXX) -c ScopeTable.cpp $(CXXFLAGS)

Typechecker.o: Typechecker.h Typechecker.cpp token.h
	$(CXX) -c Typechecker.cpp $(CXXFLAGS)

$(UNIT_BIN): $(UNIT_SRCS) $(CORE_OBJS) tests/vendor/doctest.h
	$(CXX) $(UNIT_SRCS) $(CORE_OBJS) -o $(UNIT_BIN) $(CXXFLAGS)

#builds and runs the doctest unit tests
unit: $(UNIT_BIN)
	./$(UNIT_BIN)

#runs the golden output tests over the programs in testPgms/
check: compiler
	python3 tests/run_golden.py

test: unit check

clean:
	rm -f *.o *.d compiler $(UNIT_BIN) $(UNIT_BIN).d

.PHONY: clean unit check test

-include $(wildcard *.d)

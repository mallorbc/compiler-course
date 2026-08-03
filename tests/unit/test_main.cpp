//doctest supplies main() for the unit test binary; the compiler's own main.cpp
//is deliberately left out of the link so that the test binary can call into the
//scanner and the typechecker directly.  The test cases live in the sibling
//translation units in this directory.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "../vendor/doctest.h"

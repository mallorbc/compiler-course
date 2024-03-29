#!/bin/bash
#cleans build then rebuilds
echo "Will compile then test"
sleep 1
make clean
make
sleep 1
clear


#moves compiler binary to testPgms/correct and then starts testing
yes | cp compiler testPgms/correct
yes | cp compiler testPgms/custom
yes | cp compiler testPgms/fail
cd testPgms/correct

echo "Testing provided programs"
sleep 2
clear

echo "Testing Math.src"
sleep 1
echo $(./compiler math.src)
sleep 1
clear

echo "Testing test_heap.src"
sleep 1
echo $(./compiler test_heap.src)
sleep 1
clear

echo "Testing test_program_minimal.src"
sleep 1
echo $(./compiler test_program_minimal.src)
sleep 1
clear

echo "Testing test2.src"
sleep 1
echo $(./compiler test2.src)
sleep 1
clear

echo "Testing logicals.src"
sleep 1
echo $(./compiler logicals.src)
sleep 1
clear

echo "Testing multipleProcs.src"
sleep 1
echo $(./compiler multipleProcs.src)
sleep 1
clear

echo "Testing source.src"
sleep 1
echo $(./compiler source.src)
sleep 1
clear

echo "Testing test1.src"
sleep 1
echo $(./compiler test1.src)
sleep 1
clear

echo "Testing iterativeFib.src"
sleep 1
echo $(./compiler iterativeFib.src)
sleep 1
clear

echo "Testing recursiveFib.src"
sleep 1
echo $(./compiler recursiveFib.src)
sleep 1
clear

echo "Testing test1b.src; Should fail with period error"
sleep 2
echo $(./compiler test1b.src)
sleep 1
clear

echo "All 11 given tests have been tested"
sleep 2
clear

echo "Running custom test"
sleep 2
cd ../
cd ./custom
clear

echo "Testing custom_math.src"
sleep 1
echo $(./compiler custom_math.src)
sleep 1 
clear

echo "Running failure files"
echo "Testing math_fail.src"
sleep 2
cd ../
cd ./fail
echo $(./compiler math_fail.src)
sleep 3
clear

echo "Testing test1_fail.src"
sleep 1
echo $(./compiler test1_fail.src)
sleep 3
clear


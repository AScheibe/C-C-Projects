#!/usr/bin/env bash

make
rm -f runtests.log
touch runtests.log

RED="\x1b[31;1m"
GREEN="\x1b[32;1m"
ENDCOLOR="\x1b[0m"

function run_test {
    echo "Running Test $1"
    echo "TEST OUTPUT FOR $1:" >> runtests.log
    echo "==========" >> runtests.log
    rm -rf output
    runscan test_disk_images/test_$1/0$1.img output &>> runtests.log
    python3 rcheck.py output test_disk_images/test_$1/output | sed "s/^/    /; s/FAILED!/${RED}FAILED!${ENDCOLOR}/; s/PASSED!/${GREEN}PASSED!${ENDCOLOR}/"
    echo ""
}

if [ -z "$1" ]
then
    for i in {0..7}
    do
        run_test $i
    done

    echo ""
    echo "All tests run"
else
    run_test $1
    echo "Test $1 run"
fi



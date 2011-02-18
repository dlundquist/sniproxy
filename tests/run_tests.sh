#!/bin/sh

find . -name \*.test -type f -executable | \
while read TEST
do
    ${TEST} || (echo "Failure in test ${TEST}"; exit 1)
done
echo "All tests completed successfully"

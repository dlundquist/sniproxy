#!/bin/sh

find . -name \*.test -perm -0700 -type f | \
while read TEST
do
    ${TEST} || (echo "Failure in test ${TEST}"; exit 1)
done

echo "All tests completed successfully"

#!/bin/sh

find . -name \*.test -perm /u+x -type f | \
while read TEST
do
    ${TEST} || (echo "Failure in test ${TEST}"; exit 1)
done

echo "All tests completed successfully"

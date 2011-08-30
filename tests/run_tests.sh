#!/bin/sh

find . -name \*.test -type f -executable | \
while read TEST
do
    ${TEST} || (echo "Failure in test ${TEST}"; exit 1)
done

./test_proxy || (echo "Failure in functional test"; exit 1);

echo "All tests completed successfully"

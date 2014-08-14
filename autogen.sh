#!/bin/sh

./setver.sh

autoreconf --install
automake --add-missing --copy > /dev/null 2>&1

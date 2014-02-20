#!/bin/sh
touch config.rpath
autoreconf --install
automake --add-missing --copy > /dev/null 2>&1

#!/bin/sh

VERSION=0.3.4
DEBIAN_VERSION=${VERSION}

SOURCE_DIR=$(dirname $0)
GIT_DIR=${SOURCE_DIR}/.git

cd ${SOURCE_DIR}

if [ -d ${GIT_DIR} ]; then
    GIT_VERSION=$(git describe --tags)
    if [ "x" != "x${GIT_VERSION}" ]; then
        if echo ${GIT_VERSION} | grep -q '-'; then
            VER=$(echo ${GIT_VERSION} | cut -d- -f1)
            REV=$(echo ${GIT_VERSION} | cut -d- -f2)
            REF=$(echo ${GIT_VERSION} | cut -d- -f3)

            VERSION=${GIT_VERSION}
            # Debian versions for native packages can not contain hyphens
            DEBIAN_VERSION=${VER}+git${REV}.${REF}
        else
            # Release version (e.g. 0.3.5)
            VERSION=${GIT_VERSION}
            DEBIAN_VERSION=${VERSION}
        fi
    fi
fi

# Update Autoconf with new version
sed -i "s/^\(AC_INIT(\[sniproxy\], \[\)[^]]*\(.\+\)$/\1${VERSION}\2/" ${SOURCE_DIR}/configure.ac

# Update redhat/sniproxy.spec with new version
sed -i "s/^Version:\s\+[^ ]\+/Version: ${VERSION}/" ${SOURCE_DIR}/redhat/sniproxy.spec

# Update debian/changelog with new version
debchange --newversion ${DEBIAN_VERSION} "New git revision"

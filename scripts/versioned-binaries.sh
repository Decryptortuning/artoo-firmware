#!/bin/sh

set -eu
cd -- "$(dirname -- "$0")/.."
PROG=artoo

VERSION=$(./git_describe_tag)
DEST=bins

rm -rf ${DEST}
mkdir ${DEST}

cp ${PROG}.elf ${DEST}/${PROG}_${VERSION}.elf
cp ${PROG}.bin ${DEST}/${PROG}_${VERSION}.bin
cp ${PROG}.hex ${DEST}/${PROG}_${VERSION}.hex

echo copied ${VERSION} binaries to ${DEST}

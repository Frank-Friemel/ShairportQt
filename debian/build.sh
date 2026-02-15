#!/bin/bash

# NOTE: Call from the root of the project, not from this directory!

fail() { echo "[!] $1" ; exit 1 ; }

[ -z "${ARCH}" ] && ARCH=amd64
echo "Getting version"

VERSION=$(grep -Po 'project\(ShairportQt VERSION \K[^\s]+' CMakeLists.txt)

if [ -z "$VERSION" ]; then
  fail "Version not found, exiting"
fi

if [ ! -f build/ShairportQt ]; then
  fail "ShairportQt build not found, exiting"
fi

PACKAGE="shairport-qt_${VERSION}_${ARCH}"

echo "About to (re)create ${PACKAGE}"

rm -rvf ${PACKAGE}

mkdir -vp ${PACKAGE}/DEBIAN
cp -vf debian/control ${PACKAGE}/DEBIAN/control

sed -i "s/^Version:.*/Version: ${VERSION}/" ${PACKAGE}/DEBIAN/control
sed -i "s/^Architecture:.*/Architecture: ${ARCH}/" ${PACKAGE}/DEBIAN/control

install -vD build/ShairportQt ${PACKAGE}/usr/bin/ShairportQt
install -vD debian/ShairportQt.desktop ${PACKAGE}/usr/share/applications/org.shairport.ShairportQt.desktop
install -vDm644 res/ShairportQt.png ${PACKAGE}/usr/share/icons/hicolor/256x256/apps/org.shairport.ShairportQt.png

SIZE=$(du -sk ${PACKAGE} | cut -f1)
sed -i "s/^Installed-Size:.*\Installed-Size: ${SIZE}" ${PACKAGE}/DEBIAN/control

dpkg-deb --build --root-owner-group ${PACKAGE}

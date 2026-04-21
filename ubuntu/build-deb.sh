#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PACKAGE_NAME="cute-alden"
UPSTREAM_VERSION="0.2"
PACKAGE_RELEASE="1"
PACKAGE_VERSION="${UPSTREAM_VERSION}-${PACKAGE_RELEASE}"
SOURCE_DIR_NAME="${PACKAGE_NAME}-${UPSTREAM_VERSION}"
SOURCE_TREE="${PROJECT_ROOT}/${SOURCE_DIR_NAME}"
SOURCE_TARBALL="${PROJECT_ROOT}/${SOURCE_DIR_NAME}.tar.gz"
DIST_DIR="${PROJECT_ROOT}/dist"

require_cmd() {
    local cmd="$1"

    if command -v "${cmd}" >/dev/null 2>&1; then
        return 0
    fi

    case "${cmd}" in
        gcc|make)
            echo "Missing required tool: ${cmd} (Ubuntu package: build-essential)" >&2
            ;;
        dpkg|dpkg-deb)
            echo "Missing required tool: ${cmd} (Ubuntu package: dpkg)" >&2
            ;;
        tar)
            echo "Missing required tool: ${cmd} (Ubuntu package: tar)" >&2
            ;;
        gzip)
            echo "Missing required tool: ${cmd} (Ubuntu package: gzip)" >&2
            ;;
        *)
            echo "Missing required tool: ${cmd}" >&2
            ;;
    esac

    exit 1
}

for cmd in gcc make dpkg dpkg-deb tar gzip install cp rm mkdir mktemp getconf; do
    require_cmd "${cmd}"
done

mkdir -p "${DIST_DIR}"
BUILD_ROOT="$(mktemp -d "${PROJECT_ROOT}/.${PACKAGE_NAME}-deb.XXXXXX")"
trap 'rm -rf "${BUILD_ROOT}"' EXIT

SOURCE_COPY="${BUILD_ROOT}/${SOURCE_DIR_NAME}"
PACKAGE_ROOT="${BUILD_ROOT}/pkgroot"
DEBIAN_DIR="${PACKAGE_ROOT}/DEBIAN"
DOC_DIR="${PACKAGE_ROOT}/usr/share/doc/${PACKAGE_NAME}"
MANPAGE="${PACKAGE_ROOT}/usr/share/man/man1/${PACKAGE_NAME}.1"

if [ -d "${SOURCE_TREE}" ]; then
    cp -a "${SOURCE_TREE}" "${SOURCE_COPY}"
elif [ -f "${SOURCE_TARBALL}" ]; then
    tar -xzf "${SOURCE_TARBALL}" -C "${BUILD_ROOT}"
else
    echo "Could not find ${SOURCE_TREE} or ${SOURCE_TARBALL}" >&2
    exit 1
fi

ARCH="$(dpkg --print-architecture)"
JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)"
OUTPUT_DEB="${DIST_DIR}/${PACKAGE_NAME}_${PACKAGE_VERSION}_${ARCH}.deb"

(
    cd "${SOURCE_COPY}"
    ./configure --prefix=/usr
    make -j"${JOBS}"
    make DESTDIR="${PACKAGE_ROOT}" install
)

mkdir -p "${DEBIAN_DIR}" "${DOC_DIR}"

if [ -f "${PROJECT_ROOT}/LICENSE" ]; then
    install -m 0644 "${PROJECT_ROOT}/LICENSE" "${DOC_DIR}/LICENSE"
fi

if [ -f "${PROJECT_ROOT}/CHANGELOG" ]; then
    install -m 0644 "${PROJECT_ROOT}/CHANGELOG" "${DOC_DIR}/changelog"
fi

if [ -f "${SOURCE_COPY}/COPYING" ]; then
    install -m 0644 "${SOURCE_COPY}/COPYING" "${DOC_DIR}/copyright"
fi

if [ -f "${MANPAGE}" ]; then
    gzip -n -9 "${MANPAGE}"
fi

INSTALLED_SIZE="$(du -sk "${PACKAGE_ROOT}" | cut -f1)"

cat > "${DEBIAN_DIR}/control" <<EOF
Package: ${PACKAGE_NAME}
Version: ${PACKAGE_VERSION}
Section: utils
Priority: optional
Architecture: ${ARCH}
Maintainer: Local Builder <builder@localhost>
Depends: libc6
Installed-Size: ${INSTALLED_SIZE}
Description: detachable terminal sessions without breaking scrollback
 cute-alden keeps terminal-based shells alive across disconnects.
 Unlike terminal multiplexers, it forwards an existing terminal connection
 and preserves the terminal emulator's own scrollback buffer.
EOF

rm -f "${OUTPUT_DEB}"
dpkg-deb --build --root-owner-group "${PACKAGE_ROOT}" "${OUTPUT_DEB}" >/dev/null

echo "Built ${OUTPUT_DEB}"

#!/bin/sh
#
# build-release.sh -- build xf86-input-penmount and penmount-calibrate and
# package the result into a release tarball.
#
# Usage:
#   ./release/build-release.sh
#
# (0) Version info: override any of these via environment variables
#     before running, e.g.:
#       DRIVER_VERSION=1.1.0 CALIBRATE_VERSION=1.0.2 ./release/build-release.sh
#     or just edit the defaults below directly.
# (1) make -C xf86-input-penmount and -C penmount-calibrate.
#     Any build-machine-specific variables the driver Makefile reads
#     (TARGET_XORGPATH, ARCH, OEM, ...) should already be exported in
#     your shell before running this script -- they pass straight
#     through to `make` as environment variables.
# (2) Copies penmount_drv.so and pm_calibrate into a fresh release
#     staging directory.
# (3) Copies the Simplified Chinese penmount-setup.sh into that
#     directory.
# (4) Generates a Simplified Chinese README.txt from the template in
#     this directory, with the version/date placeholders filled in.
# (5) Packages the staging directory as a .tar.bz2 whose filename carries
#     the package version and the build date.

set -eu

# --- (0) version info --------------------------------------------------
DRIVER_VERSION="${DRIVER_VERSION:-1.2.0}"
CALIBRATE_VERSION="${CALIBRATE_VERSION:-1.0.2}"
PACKAGE_VERSION="${PACKAGE_VERSION:-$DRIVER_VERSION}"
BUILD_DATE="$(date +%Y%m%d)"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
DRIVER_DIR="$REPO_ROOT/xf86-input-penmount"
CALIB_DIR="$REPO_ROOT/penmount-calibrate"

PKG_NAME="penmount-release-${PACKAGE_VERSION}-${BUILD_DATE}"
STAGE_DIR="$SCRIPT_DIR/$PKG_NAME"
TARBALL="$SCRIPT_DIR/${PKG_NAME}.tar.bz2"

log() { echo "==> $*"; }
die() { echo "build-release: $*" >&2; exit 1; }

[ -d "$DRIVER_DIR" ] || die "找不到 $DRIVER_DIR"
[ -d "$CALIB_DIR" ]  || die "找不到 $CALIB_DIR"
[ -f "$DRIVER_DIR/penmount-setup.zh-CN.sh" ] || die "找不到 $DRIVER_DIR/penmount-setup.zh-CN.sh"
[ -f "$SCRIPT_DIR/README.zh-CN.template.txt" ] || die "找不到 $SCRIPT_DIR/README.zh-CN.template.txt"

log "版本信息: package=$PACKAGE_VERSION driver=$DRIVER_VERSION calibrate=$CALIBRATE_VERSION date=$BUILD_DATE"

# --- (1) build both components -----------------------------------------
log "编译 xf86-input-penmount ..."
make -C "$DRIVER_DIR" clean
make -C "$DRIVER_DIR"
[ -f "$DRIVER_DIR/penmount_drv.so" ] || die "$DRIVER_DIR/penmount_drv.so 编译失败或不存在"

log "编译 penmount-calibrate ..."
make -C "$CALIB_DIR" clean
make -C "$CALIB_DIR"
[ -f "$CALIB_DIR/pm_calibrate" ] || die "$CALIB_DIR/pm_calibrate 编译失败或不存在"

# --- (2) stage release directory ----------------------------------------
log "整理打包目录 $STAGE_DIR ..."
rm -rf "$STAGE_DIR"
mkdir -p "$STAGE_DIR"

cp -p "$DRIVER_DIR/penmount_drv.so" "$STAGE_DIR/"
cp -p "$CALIB_DIR/pm_calibrate" "$STAGE_DIR/"

# --- (3) Simplified Chinese install/uninstall script ---------------------
cp -p "$DRIVER_DIR/penmount-setup.zh-CN.sh" "$STAGE_DIR/penmount-setup.sh"
chmod +x "$STAGE_DIR/penmount-setup.sh"

# --- (4) Simplified Chinese README, placeholders filled in ---------------
sed \
    -e "s/@PACKAGE_VERSION@/$PACKAGE_VERSION/g" \
    -e "s/@DRIVER_VERSION@/$DRIVER_VERSION/g" \
    -e "s/@CALIBRATE_VERSION@/$CALIBRATE_VERSION/g" \
    -e "s/@BUILD_DATE@/$BUILD_DATE/g" \
    "$SCRIPT_DIR/README.zh-CN.template.txt" > "$STAGE_DIR/README.txt"

# --- (5) package as tar.bz2, filename carries version + date -------------
log "打包 $TARBALL ..."
tar -cjf "$TARBALL" -C "$SCRIPT_DIR" "$PKG_NAME"

log "完成"
echo "  打包目录 : $STAGE_DIR"
echo "  压缩包   : $TARBALL"
ls -la "$TARBALL"

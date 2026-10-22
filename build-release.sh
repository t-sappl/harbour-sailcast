#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds the Sailcast release packages for all architectures with sfdk,
# copies the RPMs to one folder and runs the Harbour check on each.
#
# Usage:
#   ./build-release.sh                  all architectures
#   ./build-release.sh aarch64          only the given ones
#   ./build-release.sh --no-check       build only, skip the Harbour check
#
# Every architecture is built from scratch in its own build folder (no
# leftovers from Qt Creator debug builds). Package name, version and release
# are read from the .spec in rpm/. Result and check logs end up in $OUT_DIR;
# the script ends with a summary and exits with 1 if anything failed.

set -uo pipefail

# --- Settings -------------------------------------------------------------
SFDK="/home/thomass/SailfishOS/bin/sfdk"
PROJECT_DIR="/home/thomass/Programming/harbour-sailcast"
OUT_DIR="/home/thomass/Programming/RPMs"
BUILD_ROOT="/home/thomass/Programming/build-release"   # must be inside $HOME (shared with the build engine)
TARGET_PREFIX="SailfishOS-5.1.0.11"
ALL_ARCHES=(aarch64 armv7hl i486)
# --------------------------------------------------------------------------

RUN_CHECK=1
ARCHES=()
for arg in "$@"; do
    case "$arg" in
        --no-check) RUN_CHECK=0 ;;
        -h|--help)  sed -n '2,16p' "$0"; exit 0 ;;
        *)          ARCHES+=("$arg") ;;
    esac
done
[ ${#ARCHES[@]} -eq 0 ] && ARCHES=("${ALL_ARCHES[@]}")

die() { echo "ERROR: $*" >&2; exit 1; }

[ -x "$SFDK" ]        || die "sfdk not found: $SFDK"
[ -d "$PROJECT_DIR" ] || die "project not found: $PROJECT_DIR"
SPECS=("$PROJECT_DIR"/rpm/*.spec)
[ -f "${SPECS[0]}" ]  || die "no .spec found in $PROJECT_DIR/rpm"
[ ${#SPECS[@]} -eq 1 ] || die "more than one .spec in $PROJECT_DIR/rpm - remove the old one"
SPEC="${SPECS[0]}"

# An in-source build (Makefile/.qmake.stash in the project folder) makes
# qmake refuse or mix up the shadow build
if [ -e "$PROJECT_DIR/Makefile" ] || [ -e "$PROJECT_DIR/.qmake.stash" ]; then
    die "the project folder contains build files (Makefile/.qmake.stash) - remove them first"
fi

PKG=$(awk '/^Name:/ {print $2; exit}' "$SPEC")
VERSION=$(awk '/^Version:/ {print $2; exit}' "$SPEC")
RELEASE=$(awk '/^Release:/ {print $2; exit}' "$SPEC")
NAME="$PKG-$VERSION-$RELEASE"

mkdir -p "$OUT_DIR" "$BUILD_ROOT"

echo "$NAME – architectures: ${ARCHES[*]}"
echo "Starting the build engine (if not running yet) ..."
"$SFDK" engine start >/dev/null 2>&1 || true

declare -A BUILD_RESULT CHECK_RESULT

for arch in "${ARCHES[@]}"; do
    target="$TARGET_PREFIX-$arch"
    build_dir="$BUILD_ROOT/$arch"
    rpm="$NAME.$arch.rpm"
    echo
    echo "=== $arch ($target) ==="

    rm -rf "$build_dir"
    mkdir -p "$build_dir"
    rm -f "$OUT_DIR/$rpm" "$OUT_DIR/check-$arch.log"

    # Build (shadow build: run in the build folder, project path as argument)
    if ( cd "$build_dir" && "$SFDK" -c target="$target" build "$PROJECT_DIR" ) \
            > "$OUT_DIR/build-$arch.log" 2>&1; then
        found=$(find "$build_dir" -path '*/RPMS/*' -name "$rpm" | head -n 1)
        if [ -n "$found" ]; then
            cp "$found" "$OUT_DIR/"
            BUILD_RESULT[$arch]="OK"
            echo "Build OK  -> $OUT_DIR/$rpm"
        else
            BUILD_RESULT[$arch]="no RPM"
            echo "Build ran, but $rpm was not found - see $OUT_DIR/build-$arch.log"
            continue
        fi
    else
        BUILD_RESULT[$arch]="FAILED"
        echo "Build FAILED - last lines of $OUT_DIR/build-$arch.log:"
        tail -n 15 "$OUT_DIR/build-$arch.log"
        continue
    fi

    # Harbour check
    if [ $RUN_CHECK -eq 1 ]; then
        if "$SFDK" -c target="$target" check -s harbour "$OUT_DIR/$rpm" \
                > "$OUT_DIR/check-$arch.log" 2>&1; then
            CHECK_RESULT[$arch]="OK"
            echo "Harbour check OK"
        else
            CHECK_RESULT[$arch]="FAILED"
            echo "Harbour check FAILED:"
            grep -E "ERROR|WARNING|FAILED" "$OUT_DIR/check-$arch.log" || cat "$OUT_DIR/check-$arch.log"
        fi
    else
        CHECK_RESULT[$arch]="skipped"
    fi
done

# --- Summary --------------------------------------------------------------
echo
echo "=== Summary ($NAME) ==="
printf "%-9s %-8s %s\n" "Arch" "Build" "Harbour check"
failed=0
for arch in "${ARCHES[@]}"; do
    b="${BUILD_RESULT[$arch]:-?}"
    c="${CHECK_RESULT[$arch]:--}"
    printf "%-9s %-8s %s\n" "$arch" "$b" "$c"
    [ "$b" != "OK" ] && failed=1
    [ "$c" = "FAILED" ] && failed=1
done
echo
echo "RPMs and logs: $OUT_DIR"
ls -l "$OUT_DIR"/"$NAME".*.rpm 2>/dev/null
exit $failed

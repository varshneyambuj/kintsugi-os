#!/bin/bash
# Force a clean rebuild of the Kintsugi image and iso.
#
# Removes compiled objects, packages, image artifacts and jam caches, but keeps
# the cross-toolchain and downloaded build packages — those are slow to rebuild
# and rarely need refreshing. After running this, `jam @kintsugi-anyboot`
# rebuilds everything from source object files upward.
#
# Use --full to also drop the cross-toolchain and downloads (very slow rebuild).

set -e

GEN=generated.x86_64
FULL=0

for arg in "$@"; do
    case "$arg" in
        --full) FULL=1 ;;
        -y|--yes) ASSUME_YES=1 ;;
        -h|--help)
            echo "Usage: $0 [--full] [--yes]"
            echo "  (default) Removes objects/, packages, *.image, *.iso, jam caches."
            echo "  --full    Also removes cross-tools-x86_64, build_packages, download."
            echo "  --yes     Don't prompt for confirmation."
            exit 0
            ;;
    esac
done

if [ ! -d "$GEN" ]; then
    echo "No $GEN/ directory — nothing to wipe. Run ./configure first."
    exit 1
fi

echo "About to wipe build artifacts in $GEN/:"
echo "  - $GEN/objects/"
echo "  - $GEN/attributes/"
echo "  - $GEN/*.image, *.iso, *.mmc, *.vmdk"
echo "  - $GEN/haiku.image-* helper scripts"
echo "  - $GEN/build/jamfile_cache, header_cache"
if [ "$FULL" = "1" ]; then
    echo "  - $GEN/cross-tools-x86_64/  (FULL: cross-toolchain rebuild required)"
    echo "  - $GEN/build_packages/      (FULL: re-download required)"
    echo "  - $GEN/download/            (FULL: re-download required)"
    echo "  - $GEN/build/BuildConfig    (FULL: re-run ./configure required)"
fi

if [ "$ASSUME_YES" != "1" ]; then
    read -r -p "Proceed? [y/N] " reply
    case "$reply" in
        y|Y|yes|YES) ;;
        *) echo "Aborted."; exit 1 ;;
    esac
fi

rm -rf "$GEN"/objects "$GEN"/attributes
rm -f "$GEN"/*.image "$GEN"/*.iso "$GEN"/*.mmc "$GEN"/*.vmdk
rm -f "$GEN"/haiku.image-* "$GEN"/haiku-floppyboot-* "$GEN"/haiku-bootstrap-*
rm -f "$GEN"/build/jamfile_cache "$GEN"/build/header_cache
rm -f "$GEN"/build/haiku-revision "$GEN"/build/last-built-revision

if [ "$FULL" = "1" ]; then
    rm -rf "$GEN"/cross-tools-x86_64 "$GEN"/build_packages "$GEN"/download
    rm -f "$GEN"/build/BuildConfig
    echo "Wipe complete. Run ./configure ... && jam -q -j12 @kintsugi-anyboot"
else
    echo "Wipe complete. Run: jam -q -j12 @kintsugi-anyboot"
fi

#!/usr/bin/env bash
# setup-vcpkg-overlay.sh
#
# Clone the stdware/vcpkg-overlay repository (latest main) into scripts/vcpkg
# for local development. The vcpkg overlay provides custom ports and triplets
# referenced by scripts/vcpkg-manifest/vcpkg.json (overlay-ports / overlay-triplets).
#
# This script is idempotent: if scripts/vcpkg is already a non-empty directory,
# it prints a hint and exits 0 without re-cloning.
#
# CI uses an equivalent inline step in .github/workflows/build.yml.
#
# Usage:
#   bash scripts/setup-vcpkg-overlay.sh
#   bash scripts/setup-vcpkg-overlay.sh --force   # discard existing and re-clone

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
target_dir="${repo_root}/scripts/vcpkg"
overlay_url="https://github.com/stdware/vcpkg-overlay.git"

force=0
if [[ "${1-}" == "--force" || "${1-}" == "-f" ]]; then
    force=1
fi

if [[ -d "${target_dir}" ]] && [[ -n "$(ls -A "${target_dir}" 2>/dev/null)" ]]; then
    if [[ ${force} -eq 1 ]]; then
        echo "setup-vcpkg-overlay: --force specified, removing existing ${target_dir}"
        rm -rf "${target_dir}"
    else
        echo "setup-vcpkg-overlay: ${target_dir} already exists and is non-empty."
        echo "  To refresh, delete it and re-run, or run with --force."
        exit 0
    fi
fi

echo "setup-vcpkg-overlay: cloning ${overlay_url} into ${target_dir}"
git clone --depth 1 "${overlay_url}" "${target_dir}"

echo "setup-vcpkg-overlay: clone complete."
echo "  Overlay ports:    ${target_dir}/ports"
echo "  Overlay triplets: ${target_dir}/triplets"

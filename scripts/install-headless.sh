#!/bin/sh
# Builds only what a remote machine needs (ct and the session daemon, no GUI)
# and installs them to ~/.local/libexec/cowterm-remote, linked from
# ~/.local/bin as ct and cowtermd. A full CowTerm install's ct is left alone:
# it runs `ct bridge` too, with the daemon bundled beside it.
#
# A running daemon keeps running on its old binary, so installing never kills
# a session; `ct bridge` restarts it through the pane-restore path only when
# the daemon protocol changed.
set -e

cd "$(dirname "$0")/.."
build_directory="build-headless"
install_directory="$HOME/.local/libexec/cowterm-remote"
bin_directory="$HOME/.local/bin"

configure() {
    cmake -S . -B "$build_directory" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCOWTERM_HEADLESS=ON "$@" >/dev/null
}

# eacp's network library needs libcurl on Linux. When CMake can't find one
# (no libcurl4-openssl-dev), borrow it from nixpkgs if nix is there: no sudo.
configure_with_libcurl_from_nix() {
    nix_build="nix --extra-experimental-features nix-command build --no-link --print-out-paths"
    curl_headers="$($nix_build nixpkgs#curl.dev)"
    curl_library="$($nix_build nixpkgs#curl.out)"
    rm -f "$build_directory/CMakeCache.txt"
    configure -DCURL_INCLUDE_DIR="$curl_headers/include" -DCURL_LIBRARY="$curl_library/lib/libcurl.so"
}

if ! configure 2>/dev/null; then
    if [ "$(uname -s)" = "Linux" ] && command -v nix >/dev/null 2>&1; then
        echo "[cowterm] no libcurl found; using nixpkgs#curl"
        configure_with_libcurl_from_nix
    else
        configure
    fi
fi

cmake --build "$build_directory" --target CowTermDaemon CowTermCli

mkdir -p "$install_directory" "$bin_directory"
# Copy to a new name and rename over the old one: a daemon still running the
# old binary keeps its inode, and the copy never writes into a busy file.
cp "$build_directory/Daemon/CowTermDaemon" "$install_directory/cowtermd.new"
mv -f "$install_directory/cowtermd.new" "$install_directory/cowtermd"
cp "$build_directory/Remote/ct" "$install_directory/ct.new"
mv -f "$install_directory/ct.new" "$install_directory/ct"
ln -sf "$install_directory/cowtermd" "$bin_directory/cowtermd"

existing_ct="$(readlink "$bin_directory/ct" 2>/dev/null || true)"
case "$existing_ct" in
    *CowTerm.app*|*libexec/cowterm/*)
        echo "[cowterm] kept $bin_directory/ct -> $existing_ct (full CowTerm install)" ;;
    *)
        ln -sf "$install_directory/ct" "$bin_directory/ct" ;;
esac

echo "[cowterm] installed $("$bin_directory/ct" version)"

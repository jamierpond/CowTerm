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

# Windows (git-bash over ssh) builds with MSVC, whose environment
# scripts/with-msvc.ps1 brings in; executables there end in .exe.
compiler_environment=""
executable_suffix=""
case "$(uname -s)" in
    MINGW*|MSYS*)
        compiler_environment="powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/with-msvc.ps1"
        executable_suffix=".exe" ;;
esac

configure() {
    $compiler_environment cmake -S . -B "$build_directory" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCOWTERM_HEADLESS=ON "$@" >/dev/null
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

$compiler_environment cmake --build "$build_directory" --target CowTermDaemon CowTermCli

mkdir -p "$install_directory" "$bin_directory"

# Copy to a new name and rename over the old one: a daemon still running the
# old binary keeps its inode, and the copy never writes into a busy file.
install_executable() {
    cp "$1" "$2.new"
    mv -f "$2.new" "$2"
}

daemon="cowtermd$executable_suffix"
ct="ct$executable_suffix"

if [ -n "$executable_suffix" ]; then
    # Windows can't overwrite a running exe, but can rename it: a running
    # daemon moves aside and keeps its sessions. ~/.local/bin is left alone
    # (a full install's ct.cmd lives there); CowTerm runs this ct by its path.
    for name in "$daemon" "$ct"; do
        [ -f "$install_directory/$name" ] && mv -f "$install_directory/$name" "$install_directory/$name.old" 2>/dev/null || true
    done
    cp "$build_directory/Daemon/CowTermDaemon.exe" "$install_directory/$daemon"
    cp "$build_directory/Remote/ct.exe" "$install_directory/$ct"
    echo "[cowterm] installed $("$install_directory/$ct" version)"
    exit 0
fi

install_executable "$build_directory/Daemon/CowTermDaemon" "$install_directory/cowtermd"
install_executable "$build_directory/Remote/ct" "$install_directory/ct"
ln -sf "$install_directory/cowtermd" "$bin_directory/cowtermd"

existing_ct="$(readlink "$bin_directory/ct" 2>/dev/null || true)"
case "$existing_ct" in
    *CowTerm.app*|*libexec/cowterm/*)
        echo "[cowterm] kept $bin_directory/ct -> $existing_ct (full CowTerm install)" ;;
    *)
        ln -sf "$install_directory/ct" "$bin_directory/ct" ;;
esac

echo "[cowterm] installed $("$bin_directory/ct" version)"

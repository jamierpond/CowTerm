# The Linux build dependencies, so `just build` has them without touching the
# system package manager.
#
# eacp finds Wayland, FreeType/HarfBuzz/fontconfig and libcurl through
# pkg-config (see its CMake/FindWayland.cmake and CMake/FindLinuxText.cmake).
# On Debian/Ubuntu those can equally come from apt, as the READMEs describe —
# this shell is the no-sudo, pinned alternative, and the justfile prefers
# whatever pkg-config can already see before falling back to it.
#
#   nix develop            enter the shell by hand
#   direnv allow           enter it automatically (.envrc)
#
# macOS and Windows need none of this; the shell is Linux-only on purpose.
{
  description = "CowTerm Linux build dependencies";

  # Pinned to the nixpkgs revision the build tree was first configured against,
  # so entering the shell neither downloads nor rebuilds anything already here.
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/34ab99075ac4f7e40cf037eef32cb1c360bb85e9";

  outputs = { self, nixpkgs }:
    let
      systems = [ "aarch64-linux" "x86_64-linux" ];
      forEachSystem = fn:
        nixpkgs.lib.genAttrs systems (system: fn nixpkgs.legacyPackages.${system});
    in
    {
      devShells = forEachSystem (pkgs: {
        default = pkgs.mkShell {
          name = "cowterm";

          # The toolchain. cmake fetches the rest of the dependency graph with
          # CPM at configure time, so nothing else belongs here.
          nativeBuildInputs = with pkgs; [ cmake ninja pkg-config gcc ];

          # Libraries, in buildInputs so the pkg-config wrapper puts their .pc
          # files on PKG_CONFIG_PATH — which is the only way CMake finds them.
          # wayland-scanner is a tool, but it is listed here too because
          # FindWayland.cmake reads its path out of wayland-scanner.pc rather
          # than looking for the binary.
          buildInputs = with pkgs; [
            wayland
            wayland-scanner
            wayland-protocols
            libxkbcommon
            libdecor
            freetype
            harfbuzz
            fontconfig
            curl
          ];

          # Vulkan is a *runtime* dependency, not a build one: eacp compiles
          # against the CPM-fetched Vulkan-Headers and reaches the loader
          # through volk, which dlopens libvulkan.so.1 when the first GPUView
          # is created. Nothing links against it, so it deliberately stays out
          # of buildInputs -- adding it there would only put its .pc on
          # PKG_CONFIG_PATH for a CMake probe that never runs.
          #
          # It has to be here all the same. A binary built by this shell runs
          # against the shell's glibc, whose loader searches the store and this
          # RUNPATH only -- never /usr/lib -- so a distro-installed
          # libvulkan.so.1 is invisible to it and the dlopen fails with "cannot
          # open shared object file". volk then reports no Vulkan at all,
          # VulkanShared never creates an instance, presentationSupported stays
          # false, and every GPUView quietly falls back to its off-screen path:
          # the window opens, and nothing is ever drawn in it.
          #
          # mesa comes along for the driver. The same glibc split rules out the
          # system ICDs -- the loader dlopens those too -- so the shell points
          # VK_DRIVER_FILES at mesa's own, which on a machine with no
          # hardware Vulkan (a VM whose virtio-gpu exposes no Venus capset,
          # say) means lavapipe and software rasterisation.
          shellHook = ''
            export LD_LIBRARY_PATH="''${LD_LIBRARY_PATH:+$LD_LIBRARY_PATH:}${pkgs.lib.makeLibraryPath [ pkgs.vulkan-loader ]}"

            # Left alone if already set, so a machine that knows better -- real
            # hardware drivers, a different mesa -- keeps its own choice.
            if [ -z "''${VK_DRIVER_FILES:-}" ] && [ -z "''${VK_ICD_FILENAMES:-}" ]; then
              VK_DRIVER_FILES="$(echo ${pkgs.mesa}/share/vulkan/icd.d/*.json | tr ' ' ':')"
              export VK_DRIVER_FILES
            fi
          '';
        };
      });
    };
}

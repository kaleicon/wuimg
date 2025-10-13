# SPDX-License-Identifier: 0BSD
# SPDX-FileCopyrightText: 2025 leguteape
{
  description = ''
    A minimalistic image viewer with maximalist aims for
    comfort, speed, accurate color rendering, and format
    documentation and preservation.
  '';

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = {
    self,
    flake-parts,
    ...
  } @ inputs:
    flake-parts.lib.mkFlake {inherit inputs;} {
      systems = ["x86_64-linux"];
      imports = [flake-parts.flakeModules.easyOverlay];

      perSystem = {
        config,
        pkgs,
        ...
      }: let
        inherit (pkgs) callPackage mkShell;
      in rec {
        packages = rec {
          flif = callPackage ./nix/flif.nix {};

          wuimg = callPackage ./nix/wuimg.nix {
            inherit flif;
            version = self.shortRev or self.dirtyShortRev or "trunk";
          };

          default = wuimg;
        };

        devShells.default = mkShell {
          packages =
            (with pkgs; [cmake glfw])
            ++ (with packages.wuimg;
                nativeBuildInputs ++ buildInputs);

          shellHook = ''
            export AR="gcc-ar"
          '';
        };

        overlayAttrs = {
          inherit (config.packages) flif wuimg;
        };
      };
    };
}

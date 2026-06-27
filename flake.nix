{
  description = "Experimental Samsung SSC-style encoder";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = nixpkgs.lib.genAttrs systems;
    in
    {
      packages = forAllSystems (system:
        let pkgs = import nixpkgs { inherit system; };
        in {
          default = pkgs.callPackage ./nix/package.nix { };
          sscenc = self.packages.${system}.default;
          pipewire = pkgs.callPackage ./nix/pipewire-ssc.nix { };
        });

      apps = forAllSystems (system: {
        default = {
          type = "app";
          program = "${self.packages.${system}.default}/bin/sscenc";
          meta.description = "Encode 16-bit PCM WAV to experimental SSC-style frames";
        };
        sscenc = self.apps.${system}.default;
      });

      checks = forAllSystems (system: {
        package = self.packages.${system}.default;
      });

      devShells = forAllSystems (system:
        let pkgs = import nixpkgs { inherit system; };
        in {
          default = pkgs.mkShell {
            packages = [ pkgs.gcc pkgs.gnumake pkgs.pkg-config pkgs.pipewire pkgs.wireplumber ];
          };
        });
    };
}

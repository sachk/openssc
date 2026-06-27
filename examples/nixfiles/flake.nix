{
  description = "Example nixfiles using the experimental SSC PipeWire build";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    sscenc.url = "github:your-user/sscenc";
  };

  outputs = { nixpkgs, sscenc, ... }: {
    nixosConfigurations.host = nixpkgs.lib.nixosSystem {
      system = "x86_64-linux";
      modules = [
        ({ pkgs, ... }: {
          services.pipewire = {
            enable = true;
            audio.enable = true;
            package = sscenc.packages.${pkgs.stdenv.hostPlatform.system}.pipewire;

            wireplumber = {
              enable = true;
              package = pkgs.wireplumber.override {
                pipewire = sscenc.packages.${pkgs.stdenv.hostPlatform.system}.pipewire;
              };
              extraConfig."80-bluez-ssc" = {
                "monitor.bluez.properties" = {
                  "bluez5.roles" = [
                    "a2dp_sink"
                    "a2dp_source"
                    "bap_sink"
                    "bap_source"
                    "hfp_hf"
                    "hfp_ag"
                  ];
                  "bluez5.codecs" = [
                    "ssc"
                    "sbc"
                    "sbc_xq"
                    "aac"
                    "ldac"
                    "aptx"
                    "aptx_hd"
                  ];
                  "bluez5.enable-sbc-xq" = true;
                  "bluez5.hfphsp-backend" = "native";
                  "bluez5.default.rate" = 48000;
                  "bluez5.default.channels" = 2;
                };
              };
            };
          };

          # Runtime-only path to your phone-extracted Samsung blob.
          systemd.user.services.pipewire.environment.SSCENC_BLOB_SO = "/home/me/src/ssc/.re/latest_src/libScalable_Encoder.so";
          systemd.user.services.pipewire-pulse.environment.SSCENC_BLOB_SO = "/home/me/src/ssc/.re/latest_src/libScalable_Encoder.so";
          systemd.user.services.wireplumber.environment.SSCENC_BLOB_SO = "/home/me/src/ssc/.re/latest_src/libScalable_Encoder.so";

          environment.systemPackages = [ sscenc.packages.${pkgs.stdenv.hostPlatform.system}.default ];
        })
      ];
    };
  };
}

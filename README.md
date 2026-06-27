# SSC Encoder Workbench

Clean C implementation work lives at the repo root. Reverse-engineering artifacts and Samsung blobs are local-only under `.re/` and ignored.

## Build

```sh
make
make test
```

## CLI

```sh
build/sscenc [-b 88000|96000|128000|192000|229000] input.wav output.ssc
```

Input: 16-bit PCM WAV, mono/stereo, 44.1 or 48 kHz. Output is a raw stream of SSC-style frames with the recovered Samsung frame header and a simple experimental payload. The payload is not bit-exact Samsung SSC yet.

## Nix

```sh
nix build
nix run . -- -b 192000 input.wav output.ssc
```

The flake also exposes `packages.${system}.pipewire`: a patched PipeWire build that installs an experimental BlueZ5 A2DP codec plugin named `ssc`. There is no `sscenc` service or daemon. PipeWire loads the SPA codec plugin when BlueZ/WirePlumber negotiates an A2DP transport.

Copy-paste NixOS shape:

```nix
{ pkgs, inputs, ... }:
{
  services.pipewire = {
    enable = true;
    audio.enable = true;
    package = inputs.sscenc.packages.${pkgs.stdenv.hostPlatform.system}.pipewire;

    wireplumber = {
      enable = true;
      extraConfig."80-bluez-ssc" = {
        "monitor.bluez.properties" = {
          "bluez5.roles" = [ "a2dp_sink" "a2dp_source" "bap_sink" "bap_source" "hfp_hf" "hfp_ag" ];
          "bluez5.codecs" = [ "ssc" "sbc" "sbc_xq" "aac" "ldac" "aptx" "aptx_hd" ];
          "bluez5.enable-sbc-xq" = true;
          "bluez5.hfphsp-backend" = "native";
          "bluez5.default.rate" = 48000;
          "bluez5.default.channels" = 2;
        };
      };
    };
  };

  environment.systemPackages = [ inputs.sscenc.packages.${pkgs.stdenv.hostPlatform.system}.default ];
}
```

See `examples/nixfiles/flake.nix` for a full flake example.

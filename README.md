# SSC Encoder Workbench

Clean C implementation work lives at the repo root. Reverse-engineering artifacts and Samsung blobs are local-only under `.re/` and ignored.

## Local Samsung blob bundle

Reverse-engineering source artifacts are under `.re/` and intentionally ignored by Git.

- Encoder blob: `.re/latest_src/libScalable_Encoder.so`
- Bluetooth codec bundle: `.re/latest_src/lib_bt_bundle.so`
- Decoder status: `.re/latest_src/MISSING_libScalable_Decoder.txt`
- Full metadata: `.re/latest_src/metadata.json`
- Preserved extraction tree for future library lookup: `.re/firmware_extract/`

`libScalable_Decoder.so` was searched across the retained S948BXXS3AZF4 `system`, `vendor`, `product`, `system_ext`, `odm`, and extracted `com.android.bt` APEX library locations. It was not present as a standalone file. Samsung's vendor audio config maps `CODEC_TYPE_SSC` encode/decode to `/vendor/lib64/lib_bt_bundle.so`, so decoder-side code may be bundled there.

Kept extraction locations are library-bearing paths only (`lib`, `lib64`, `apex`, selected build props/configs); original firmware zip/AP/super/partition images were removed.

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

The flake also exposes `packages.${system}.pipewire`: a patched PipeWire build that installs a BlueZ5 A2DP codec plugin named `ssc`. The plugin encodes through the Samsung `libScalable_Encoder.so` under qemu-aarch64 when `SSCENC_BLOB_SO` points at your local phone-extracted blob. There is no always-running `sscenc` service; the helper is spawned only while PipeWire/WirePlumber uses the codec.

Runtime A2DP currently advertises Samsung's basic-SSC capability byte `0x0c` (bitrate-limit flag + 24-bit/HiFi flag) and rejects selected configs that set `0x02` (UHQ2/96 kHz-ish). This mirrors the S948BXXS3AZF4 phone capture against Buds: Samsung selected `0x0c`, then sent one 864-sample / 484-byte `ff ee ... f3` frame directly after each RTP header. `ssc_local-4.btsnoop` showed that adding BlueZ's generic framed-codec RTP payload byte made the Buds produce no audio, so SSC does not use that extra byte on this path.

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
      package = pkgs.wireplumber.override {
        pipewire = inputs.sscenc.packages.${pkgs.stdenv.hostPlatform.system}.pipewire;
      };
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

  # Runtime-only; the blob stays outside the Nix store/source tree.
  # Point this at your local `.re/latest_src/libScalable_Encoder.so`.
  systemd.user.services.pipewire.environment.SSCENC_BLOB_SO = "/home/me/src/ssc/.re/latest_src/libScalable_Encoder.so";
  systemd.user.services.pipewire-pulse.environment.SSCENC_BLOB_SO = "/home/me/src/ssc/.re/latest_src/libScalable_Encoder.so";
  systemd.user.services.wireplumber.environment.SSCENC_BLOB_SO = "/home/me/src/ssc/.re/latest_src/libScalable_Encoder.so";

  environment.systemPackages = [ inputs.sscenc.packages.${pkgs.stdenv.hostPlatform.system}.default ];
}
```

See `examples/nixfiles/flake.nix` for a full flake example.

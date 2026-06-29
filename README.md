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

Runtime A2DP defaults to Samsung's basic-SSC capability byte `0x0c` (bitrate-limit flag + 24-bit/HiFi flag) and `192000` bps. Set `SSCENC_PROFILE` on PipeWire/WirePlumber to test other forced profiles without rebuilding:

```text
default, samsung, samsung-basic, samsung-default -> 192000 bps, caps 0x0c, 864 frames
basic-88                         ->  88000 bps, caps 0x0c, 864 frames
basic-96                         ->  96000 bps, caps 0x0c, 864 frames
basic-128                        -> 128000 bps, caps 0x0c, 864 frames
basic-192                        -> 192000 bps, caps 0x0c, 864 frames
basic-229                        -> 229000 bps, caps 0x0c, 864 frames
basic-256                        -> 256000 bps, caps 0x0c, 864 frames
basic-328, force-high, basic-max, top-basic -> 328000 bps, caps 0x0c, 864 frames
open-basic-229                   -> 229000 bps, caps 0x04, 864 frames
open-basic-256                   -> 256000 bps, caps 0x04, 864 frames
open-basic-328                   -> 328000 bps, caps 0x04, 864 frames
uhq-152                          -> 152000 bps, caps 0x0e, 864 frames
uhq-250                          -> 250000 bps, caps 0x0e, 864 frames
uhq-291                          -> 291000 bps, caps 0x0e, 864 frames
uhq-308                          -> 308000 bps, caps 0x0e, 864 frames
uhq-442                          -> 442000 bps, caps 0x0e, 864 frames
uhq-584                          -> 584000 bps, caps 0x0e, 864 frames
uhq-886, uhq-max                 -> 886000 bps, caps 0x0e, 864 frames
open-uhq-250                     -> 250000 bps, caps 0x06, 864 frames
open-uhq-291                     -> 291000 bps, caps 0x06, 864 frames
open-uhq-308                     -> 308000 bps, caps 0x06, 864 frames
open-uhq-442                     -> 442000 bps, caps 0x06, 864 frames
short-basic-192                  -> 192000 bps, caps 0x0c, 512 frames
short-basic-229                  -> 229000 bps, caps 0x0c, 512 frames
short-uhq-291                    -> 291000 bps, caps 0x0e, 512 frames
short-uhq-308                    -> 308000 bps, caps 0x0e, 512 frames
short-uhq-442                    -> 442000 bps, caps 0x0e, 512 frames
short-uhq-584                    -> 584000 bps, caps 0x0e, 512 frames
short-open-uhq-291               -> 291000 bps, caps 0x06, 512 frames
short-open-uhq-308               -> 308000 bps, caps 0x06, 512 frames
short-open-uhq-442               -> 442000 bps, caps 0x06, 512 frames
short-open-uhq-584               -> 584000 bps, caps 0x06, 512 frames
stress-512                       -> 512000 bps, caps 0x0e, 864 frames
stress-768                       -> 768000 bps, caps 0x0e, 864 frames
stress-990                       -> 990000 bps, caps 0x0e, 864 frames
stress-1200                      -> 1200000 bps, caps 0x0e, 864 frames
stress-1411                      -> 1411000 bps, caps 0x0e, 864 frames
stress-2304, probably-broken-2304 -> 2304000 bps, caps 0x0e, 864 frames
stress-3200                      -> 3200000 bps, caps 0x0e, 864 frames
```

The `0x0e` profiles deliberately set the recovered UHQ2/96-kHz-ish bit while still feeding 48 kHz PCM into the current path. The `0x04`/`0x06` `open-*` probes clear the suspected bitrate-limit bit. The `short-*` probes use 512 PCM frames per SSC packet so 442/584 kbps blob frames fit the observed phone media MTU. They are probe modes, not confirmed Samsung-default behavior. Unknown profile names fall back to `default`.

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

  # Optional test profile. Omit for Samsung-basic 192 kbps.
  # Useful next probes: "open-uhq-291", "open-uhq-308", "short-open-uhq-442", "short-open-uhq-584".
  systemd.user.services.pipewire.environment.SSCENC_PROFILE = "open-uhq-291";
  systemd.user.services.pipewire-pulse.environment.SSCENC_PROFILE = "open-uhq-291";
  systemd.user.services.wireplumber.environment.SSCENC_PROFILE = "open-uhq-291";

  environment.systemPackages = [ inputs.sscenc.packages.${pkgs.stdenv.hostPlatform.system}.default ];
}
```

See `examples/nixfiles/flake.nix` for a full flake example.

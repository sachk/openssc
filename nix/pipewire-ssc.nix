{
  lib,
  pipewire,
  pkgsCross,
  qemu,
}:

let
  aarch64Cc = pkgsCross.aarch64-multiplatform.stdenv.cc;
  aarch64Prefix = aarch64Cc.targetPrefix;
in
pipewire.overrideAttrs (old: {
  pname = "pipewire-ssc";

  nativeBuildInputs = (old.nativeBuildInputs or [ ]) ++ [ aarch64Cc ];

  postPatch = (old.postPatch or "") + ''
    mkdir -p spa/plugins/bluez5/sscenc/include/ssc spa/plugins/bluez5/sscenc/blob
    cp ${../pipewire/a2dp-codec-ssc.c} spa/plugins/bluez5/a2dp-codec-ssc.c
    cp ${../src/encoder.c} spa/plugins/bluez5/sscenc/encoder.c
    cp ${../include/ssc/encoder.h} spa/plugins/bluez5/sscenc/include/ssc/encoder.h
    cp ${../tools/blob/ssc_blob_helper.c} spa/plugins/bluez5/sscenc/blob/ssc_blob_helper.c
    cp ${../tools/blob/android_libc_shim.c} spa/plugins/bluez5/sscenc/blob/android_libc_shim.c
    cp ${../tools/blob/android_log_shim.c} spa/plugins/bluez5/sscenc/blob/android_log_shim.c
    cp ${../tools/blob/empty_shim.c} spa/plugins/bluez5/sscenc/blob/empty_shim.c
    cp ${../tools/blob/android_libc.version} spa/plugins/bluez5/sscenc/blob/android_libc.version

    BLOB_CC=${aarch64Cc}/bin/${aarch64Prefix}gcc
    SHIM_CFLAGS="-fPIC -fno-builtin -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables"
    SHIM_LDFLAGS="-shared -nostdlib"
    "$BLOB_CC" $SHIM_CFLAGS $SHIM_LDFLAGS -Wl,-soname,libc.so \
      -Wl,--version-script,spa/plugins/bluez5/sscenc/blob/android_libc.version \
      -o spa/plugins/bluez5/sscenc/blob/libc.so \
      spa/plugins/bluez5/sscenc/blob/android_libc_shim.c
    "$BLOB_CC" $SHIM_CFLAGS $SHIM_LDFLAGS -Wl,-soname,liblog.so \
      -o spa/plugins/bluez5/sscenc/blob/liblog.so \
      spa/plugins/bluez5/sscenc/blob/android_log_shim.c
    "$BLOB_CC" $SHIM_CFLAGS $SHIM_LDFLAGS -Wl,-soname,libm.so \
      -o spa/plugins/bluez5/sscenc/blob/libm.so \
      spa/plugins/bluez5/sscenc/blob/empty_shim.c
    "$BLOB_CC" $SHIM_CFLAGS $SHIM_LDFLAGS -Wl,-soname,libdl.so \
      -o spa/plugins/bluez5/sscenc/blob/libdl.so \
      spa/plugins/bluez5/sscenc/blob/empty_shim.c
    "$BLOB_CC" -O2 -Wall -Wextra \
      -o spa/plugins/bluez5/sscenc/blob/ssc_blob_helper \
      spa/plugins/bluez5/sscenc/blob/ssc_blob_helper.c -ldl

    substituteInPlace spa/include/spa/param/bluetooth/audio.h \
      --replace-fail $'\tSPA_BLUETOOTH_AUDIO_CODEC_OPUS_G,\n\n\t/* HFP */' \
                     $'\tSPA_BLUETOOTH_AUDIO_CODEC_OPUS_G,\n\tSPA_BLUETOOTH_AUDIO_CODEC_SSC,\n\n\t/* HFP */'

    substituteInPlace spa/include/spa/param/bluetooth/type-info.h \
      --replace-fail $'\t{ SPA_BLUETOOTH_AUDIO_CODEC_OPUS_G, SPA_TYPE_Int, SPA_TYPE_INFO_BLUETOOTH_AUDIO_CODEC_BASE "opus_g", NULL },\n' \
                     $'\t{ SPA_BLUETOOTH_AUDIO_CODEC_OPUS_G, SPA_TYPE_Int, SPA_TYPE_INFO_BLUETOOTH_AUDIO_CODEC_BASE "opus_g", NULL },\n\t{ SPA_BLUETOOTH_AUDIO_CODEC_SSC, SPA_TYPE_Int, SPA_TYPE_INFO_BLUETOOTH_AUDIO_CODEC_BASE "ssc", NULL },\n'

    substituteInPlace spa/plugins/bluez5/codec-loader.c \
      --replace-fail $'\t\tSPA_BLUETOOTH_AUDIO_CODEC_LDAC,\n' \
                     $'\t\tSPA_BLUETOOTH_AUDIO_CODEC_LDAC,\n\t\tSPA_BLUETOOTH_AUDIO_CODEC_SSC,\n' \
      --replace-fail $'\t\tMEDIA_CODEC_FACTORY_LIB("ldac"),\n' \
                     $'\t\tMEDIA_CODEC_FACTORY_LIB("ldac"),\n\t\tMEDIA_CODEC_FACTORY_LIB("ssc"),\n'

    substituteInPlace spa/plugins/bluez5/bluez5-dbus.c \
      --replace-fail $'\tcase SPA_BLUETOOTH_AUDIO_CODEC_LDAC:\n\t\treturn 125 * SPA_NSEC_PER_MSEC;' \
                     $'\tcase SPA_BLUETOOTH_AUDIO_CODEC_LDAC:\n\tcase SPA_BLUETOOTH_AUDIO_CODEC_SSC:\n\t\treturn 125 * SPA_NSEC_PER_MSEC;'

    substituteInPlace spa/plugins/bluez5/meson.build \
      --replace-fail "codec_args = [ '-DCODEC_PLUGIN' ]" "codec_args = [ '-DCODEC_PLUGIN' ]
sscenc_inc = include_directories('sscenc/include')
ssc_codec_args = codec_args + [ '-DSSCENC_BLOB_HELPER=\"$out/libexec/sscenc/sscenc-blob-run\"' ]" \
      --replace-fail "bluez_codec_faststream = shared_library('spa-codec-bluez5-faststream'," "bluez_codec_ssc = shared_library('spa-codec-bluez5-ssc',
  [ 'a2dp-codec-ssc.c', 'sscenc/encoder.c', 'media-codecs.c' ],
  include_directories : [ configinc, sscenc_inc ],
  c_args : ssc_codec_args,
  dependencies : [ spa_dep ],
  install : true,
  install_dir : spa_plugindir / 'bluez5')

bluez_codec_faststream = shared_library('spa-codec-bluez5-faststream',"
  '';

  postInstall = (old.postInstall or "") + ''
    blob_dir=$NIX_BUILD_TOP/source/spa/plugins/bluez5/sscenc/blob
    mkdir -p $out/libexec/sscenc/aarch64 $out/libexec/sscenc/shims
    install -Dm755 $blob_dir/ssc_blob_helper \
      $out/libexec/sscenc/aarch64/ssc_blob_helper
    install -Dm755 $blob_dir/libc.so $out/libexec/sscenc/shims/libc.so
    install -Dm755 $blob_dir/liblog.so $out/libexec/sscenc/shims/liblog.so
    install -Dm755 $blob_dir/libm.so $out/libexec/sscenc/shims/libm.so
    install -Dm755 $blob_dir/libdl.so $out/libexec/sscenc/shims/libdl.so

    cat > $out/libexec/sscenc/sscenc-blob-run <<EOF
#!/bin/sh
if [ -z "\''${SSCENC_BLOB_SO:-}" ]; then
  echo "SSCENC_BLOB_SO is not set" >&2
  exit 127
fi
exec ${qemu}/bin/qemu-aarch64 -E LD_LIBRARY_PATH=$out/libexec/sscenc/shims \
  $out/libexec/sscenc/aarch64/ssc_blob_helper "\''${SSCENC_BLOB_SO}" "\$@"
EOF
    chmod 0755 $out/libexec/sscenc/sscenc-blob-run
  '';

  meta = (old.meta or { }) // {
    description = "PipeWire with Samsung SSC A2DP codec bridge";
    platforms = lib.platforms.linux;
  };
})

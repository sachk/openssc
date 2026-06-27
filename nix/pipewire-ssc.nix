{
  pipewire,
}:

pipewire.overrideAttrs (old: {
  pname = "pipewire-ssc";

  postPatch = (old.postPatch or "") + ''
    mkdir -p spa/plugins/bluez5/sscenc/include/ssc
    cp ${../pipewire/a2dp-codec-ssc.c} spa/plugins/bluez5/a2dp-codec-ssc.c
    cp ${../src/encoder.c} spa/plugins/bluez5/sscenc/encoder.c
    cp ${../include/ssc/encoder.h} spa/plugins/bluez5/sscenc/include/ssc/encoder.h

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
      --replace-fail $'codec_args = [ \'-DCODEC_PLUGIN\' ]\n' \
                     $'codec_args = [ \'-DCODEC_PLUGIN\' ]\nsscenc_inc = include_directories(\'sscenc/include\')\n' \
      --replace-fail $'bluez_codec_faststream = shared_library(\'spa-codec-bluez5-faststream\',' \
                     $'bluez_codec_ssc = shared_library(\'spa-codec-bluez5-ssc\',\n  [ \'a2dp-codec-ssc.c\', \'sscenc/encoder.c\', \'media-codecs.c\' ],\n  include_directories : [ configinc, sscenc_inc ],\n  c_args : codec_args,\n  dependencies : [ spa_dep ],\n  install : true,\n  install_dir : spa_plugindir / \'bluez5\')\n\nbluez_codec_faststream = shared_library(\'spa-codec-bluez5-faststream\','
  '';
})

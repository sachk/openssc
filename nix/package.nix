{
  lib,
  stdenv,
}:

stdenv.mkDerivation {
  pname = "sscenc";
  version = "0.1.0";

  src = lib.cleanSourceWith {
    src = ../.;
    filter = path: type:
      let
        base = baseNameOf path;
        rel = lib.removePrefix (toString ../. + "/") (toString path);
      in
        !(lib.hasPrefix ".re/" rel)
        && !(lib.hasPrefix "build/" rel)
        && base != "result"
        && !(lib.hasPrefix "result-" base)
        && base != ".git";
  };

  makeFlags = [ "PREFIX=$(out)" ];

  doCheck = true;
  checkPhase = ''
    runHook preCheck
    make test
    runHook postCheck
  '';

  installPhase = ''
    runHook preInstall
    make install PREFIX=$out
    mkdir -p $out/lib/pkgconfig
    cat > $out/lib/pkgconfig/sscenc.pc <<EOF
prefix=$out
exec_prefix=$out
libdir=$out/lib
includedir=$out/include

Name: sscenc
Description: Experimental Samsung SSC-style encoder core
Version: 0.1.0
Libs: -L$out/lib -lsscenc
Cflags: -I$out/include
EOF
    runHook postInstall
  '';

  meta = {
    description = "Experimental Samsung SSC-style encoder core and CLI";
    license = lib.licenses.mit;
    platforms = lib.platforms.linux;
    mainProgram = "sscenc";
  };
}

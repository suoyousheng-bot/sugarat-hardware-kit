#!/bin/sh
# Downloads the OFL fonts that tools/gen_assets.py rasterizes into main/zodiac/zd_assets.c.
# Needed only to change on-screen text; the build uses the generated glyphs.
set -eu
cd "$(dirname "$0")/../assets/fonts"
base=https://github.com/google/fonts/raw/main/ofl
curl -fL -o NotoSansSC.ttf "$base/notosanssc/NotoSansSC%5Bwght%5D.ttf"
curl -fL -o Nunito-Variable.ttf "$base/nunito/Nunito%5Bwght%5D.ttf"
curl -fL -o NotoSansSymbols.ttf "$base/notosanssymbols/NotoSansSymbols%5Bwght%5D.ttf"

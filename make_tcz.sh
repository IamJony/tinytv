#!/bin/sh
# make_tcz.sh - builds tcz/tinytv.tcz (+ .dep .md5.txt .list .info) from build/tinytv.
# Run after "make". Needs squashfs-tools (loaded with tce-load -i if missing).
set -e

D=$(cd "$(dirname "$0")" && pwd)
APP=tinytv
VER=2.0.0
OUT="$D/tcz"
SHARE=usr/local/share/tinytv

[ -x "$D/build/$APP" ] || { echo "ERROR: run make first"; exit 1; }
command -v mksquashfs >/dev/null || tce-load -i squashfs-tools >/dev/null

P=$(mktemp -d)
chmod 755 "$P"
install -d "$P/usr/local/bin" "$P/usr/local/share/pixmaps" "$P/usr/local/share/applications" "$P/$SHARE/js"
install -m 755 "$D/build/$APP" "$P/usr/local/bin/$APP"
strip "$P/usr/local/bin/$APP"
install -m 644 "$D/js/sanson_host.js" "$P/$SHARE/js/sanson_host.js"

# Copia comun.js y todos los plugins locales incluidos en plugins/ (ej. latinotv)
install -d "$P/$SHARE/plugins"
[ -f "$D/plugins/comun.js" ] && install -m 644 "$D/plugins/comun.js" "$P/$SHARE/plugins/comun.js"
for pdir in "$D/plugins"/*; do
    if [ -d "$pdir" ] && [ -f "$pdir/config.json" ] && [ -f "$pdir/script.js" ]; then
        pname=$(basename "$pdir")
        install -d "$P/$SHARE/plugins/$pname"
        install -m 644 "$pdir/config.json" "$pdir/script.js" "$P/$SHARE/plugins/$pname/"
    fi
done

install -m 644 "$D/LICENSE" "$P/$SHARE/LICENSE"
install -m 644 "$D/third_party/quickjs/LICENSE" "$P/$SHARE/LICENSE.quickjs"
install -m 644 "$D/third_party/cjson/LICENSE" "$P/$SHARE/LICENSE.cjson"

# Ícono y lanzador .desktop (soporta tanto tinytv.* como sanson.*)
if [ -f "$D/data/tinytv.png" ]; then
    install -m 644 "$D/data/tinytv.png" "$P/usr/local/share/pixmaps/$APP.png"
elif [ -f "$D/data/sanson.png" ]; then
    install -m 644 "$D/data/sanson.png" "$P/usr/local/share/pixmaps/sanson.png"
fi

if [ -f "$D/data/tinytv.desktop" ]; then
    install -m 644 "$D/data/tinytv.desktop" "$P/usr/local/share/applications/$APP.desktop"
elif [ -f "$D/data/sanson.desktop" ]; then
    install -m 644 "$D/data/sanson.desktop" "$P/usr/local/share/applications/$APP.desktop"
fi

find "$P" -type d -exec chmod 755 {} +
mkdir -p "$OUT"
mksquashfs "$P" "$OUT/$APP.tcz" -all-root -noappend -no-progress -b 4096 -no-xattrs >/dev/null
(cd "$P" && find usr -type f | sed 's|^|/|' | sort) > "$OUT/$APP.tcz.list"
rm -rf "$P"

printf '%s\n' fltk-1.3.tcz curl.tcz mplayer-cli.tcz libwebp1.tcz gcc_libs.tcz > "$OUT/$APP.tcz.dep"
(cd "$OUT" && md5sum "$APP.tcz" > "$APP.tcz.md5.txt")
SIZE=$(du -k "$OUT/$APP.tcz" | cut -f1)

cat > "$OUT/$APP.tcz.info" <<INFO
Title:          $APP.tcz
Description:    TinyTV: modular video, live streams & music player (fork of Sansón)
Version:        $VER
Author:         AnddyCort (original Sansón), IamJony (TinyTV fork)
Original-site:  https://github.com/IamJony/tinytv
Copying-policy: GPL-3.0-or-later; QuickJS and cJSON MIT
Size:           ${SIZE}K
Extension_by:   IamJony
Tags:           youtube twitch kick music video live chat player mplayer plugins
Comments:       Fork of Sansón (by AnddyCort), maintained by IamJony.
                Introduces a dynamic external plugin architecture and an
                integrated Plugin Store to install, update and manage
                platforms without recompiling.
                Watch videos and live streams with mplayer, listen to
                music with synced lyrics and queue, download media in
                parallel, and log in using HTTP headers or Netscape
                cookie files.
                Runs platform plugins in a built-in QuickJS engine.
Change-log:     2026/10/16 1.0.0 first version (Sansón by AnddyCort)
                2026/11/13 1.7.0 opens links from the command line
                2026/11/19 2.0.0 TinyTV fork by IamJony: dynamic external
                plugins, built-in Plugin Store, Netscape cookie support
Current:        2026/11/19 2.0.0 TinyTV fork with external plugins and store
INFO
echo "OK: $OUT/$APP.tcz (${SIZE}K)"

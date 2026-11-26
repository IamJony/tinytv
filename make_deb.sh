#!/bin/sh
set -e

# Definición de colores
VERDE='\033[0;32m'
AZUL='\033[0;34m'
AMARILLO='\033[0;33m'
SIN_COLOR='\033[0m'

WORKDIR="/tmp"
REPO_URL="https://github.com/IamJony/tinytv"
DIR_NAME="tinytv"

echo "${AZUL}Descargando código desde $REPO_URL${SIN_COLOR}"
cd "$WORKDIR"

if [ ! -d "$DIR_NAME" ]; then
    git clone "$REPO_URL" "$DIR_NAME"
else
    echo "${AMARILLO}El directorio ya existe. Actualizando código...${SIN_COLOR}"
    cd "$DIR_NAME" && git pull && cd "$WORKDIR"
fi

cd "$DIR_NAME"

echo "${AZUL}Leyendo versión dinámica del archivo info${SIN_COLOR}"
INFO_FILE="tcz/tinytv.tcz.info"
if [ ! -f "$INFO_FILE" ]; then
    INFO_FILE=$(ls tcz/*.tcz.info 2>/dev/null | head -n 1)
fi

if [ -n "$INFO_FILE" ] && [ -f "$INFO_FILE" ]; then
    VERSION=$(grep -i '^Version:' "$INFO_FILE" | awk '{print $2}' | tr -d '\r')
else
    VERSION="1.0.1"
fi

if [ -z "$VERSION" ]; then
    echo "Error: No se pudo determinar la versión"
    exit 1
fi

echo "${VERDE}Versión detectada: $VERSION${SIN_COLOR}"
DEB_DIR_NAME="tinytv_${VERSION}_amd64"
DEB_DIR="$WORKDIR/$DEB_DIR_NAME"

echo "${AZUL}Modificando el Makefile para 64 bits${SIN_COLOR}"
sed -i 's/-march=i686 -mtune=generic //g' Makefile

echo "${AZUL}Compilando el proyecto${SIN_COLOR}"
make clean
make

echo "${AZUL}Creando la estructura de carpetas del paquete${SIN_COLOR}"
rm -rf "$DEB_DIR"
mkdir -p "$DEB_DIR/DEBIAN"
mkdir -p "$DEB_DIR/usr/bin"
mkdir -p "$DEB_DIR/usr/share/applications"
mkdir -p "$DEB_DIR/usr/share/pixmaps"

APP_SHARE_DIR="$DEB_DIR/usr/share/tinytv"
mkdir -p "$APP_SHARE_DIR/build"

echo "${AZUL}Copiando binarios y preservando directorios estructurales${SIN_COLOR}"
for bin in build/*; do
    if [ -f "$bin" ] && [ -x "$bin" ]; then
        bin_name=$(basename "$bin")
        cp -p "$bin" "$APP_SHARE_DIR/build/"
        ln -s "/usr/share/tinytv/build/$bin_name" "$DEB_DIR/usr/bin/$bin_name"
    fi
done

if [ -f "$APP_SHARE_DIR/build/sanson" ] && [ ! -f "$DEB_DIR/usr/bin/tinytv" ]; then
    ln -s "/usr/share/tinytv/build/sanson" "$DEB_DIR/usr/bin/tinytv"
fi

for folder in plugins js scripts assets; do
    if [ -d "$folder" ]; then
        cp -r "$folder" "$APP_SHARE_DIR/"
    fi
done

if [ -d "data" ]; then
    [ -f "data/tinytv.desktop" ] && cp -p data/tinytv.desktop "$DEB_DIR/usr/share/applications/"
    [ -f "data/sanson.desktop" ] && cp -p data/sanson.desktop "$DEB_DIR/usr/share/applications/"
    [ -f "data/tinytv.png" ] && cp -p data/tinytv.png "$DEB_DIR/usr/share/pixmaps/"
    [ -f "data/sanson.png" ] && cp -p data/sanson.png "$DEB_DIR/usr/share/pixmaps/"
fi

echo "${AZUL}Generando archivo de control de Debian${SIN_COLOR}"
cat << EOF > "$DEB_DIR/DEBIAN/control"
Package: tinytv
Version: $VERSION
Architecture: amd64
Maintainer: IamJony <https://github.com/IamJony/tinytv>
Homepage: https://github.com/IamJony/tinytv
Depends: libcurl4, libfltk1.3, mplayer, libwebp7 | libwebp6
Section: utils
Priority: optional
Description: TinyTV: modular video, live streams & music player (fork of Sanson)
 Modular video, live streams and music player with a dynamic external
 plugin architecture and an integrated Plugin Store to install, update
 and manage platforms without recompiling.
 .
 Watch videos and live streams with mplayer, listen to music with synced
 lyrics and queue, download media in parallel, and log in using HTTP
 headers or Netscape cookie files.
EOF

cat << 'EOF' > "$DEB_DIR/DEBIAN/postinst"
#!/bin/sh
set -e
if [ -f /usr/share/tinytv/plugins/comun.js ]; then
    mkdir -p /home/j/.config/tinytv/plugins
    cp -f /usr/share/tinytv/plugins/comun.js /home/j/.config/tinytv/plugins/comun.js
    chown -R j:j /home/j/.config/tinytv 2>/dev/null || true
fi
exit 0
EOF
chmod 755 "$DEB_DIR/DEBIAN/postinst"

echo "${AZUL}Construyendo el archivo .deb final${SIN_COLOR}"
cd "$WORKDIR"
dpkg-deb --root-owner-group --build "$DEB_DIR_NAME"

rm -rf "$DEB_DIR"

echo "${VERDE}Paquete creado con éxito en $WORKDIR/$DEB_DIR_NAME.deb${SIN_COLOR}"

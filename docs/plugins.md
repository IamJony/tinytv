# Plugins de TinyTV

TinyTV **escanea** los plugins instalados en `~/.config/tinytv/plugins`, por lo que los plugins del repositorio [plugins-tinytv](https://github.com/IamJony/plugins-tinytv) (RamenTV, Internet Archive, Odysee, Hackstream, YouTube, Twitch, Kick, YouTube Music) se integran mediante la **Tienda de plugins** automáticamente, o puedes instalarlos manualmente en esa carpeta.

---

## Índice general

### Parte I: uso y creación de plugins
1. [Lista de plugins disponibles](#1-lista-de-plugins-disponibles)
   - [Enlaces directos](#enlaces-directos)
   - [Índice completo (JSON)](#índice-completo-json)
2. [Cómo crear nuestro propio plugin](#2-cómo-crear-nuestro-propio-plugin)
   - [Plugin de ejemplo: Osos Polares (paso a paso)](#plugin-de-ejemplo-osos-polares-paso-a-paso)
   - [Paso 1: crear la carpeta](#paso-1-crear-la-carpeta)
   - [Paso 2: verificar `comun.js`](#paso-2-verificar-comunjs)
   - [Paso 3: crear `config.json`](#paso-3-crear-configjson)
   - [Paso 4: crear `script.js`](#paso-4-crear-scriptjs)
   - [Paso 5: probar](#paso-5-probar)
   - [Qué hace cada parte](#qué-hace-cada-parte)
   - [Opciones avanzadas y empaquetado](#opciones-avanzadas-y-empaquetado)
   - [Resumen de creación](#resumen-de-creación)

### Parte II: métodos de reproducción y funcionamiento interno
3. [Visión general](#3-visión-general)
4. [El contenedor: `VideoSourceDescriptor`](#4-el-contenedor-videosourcedescriptor)
5. [Fuentes de vídeo directo: `VideoUrlSource`](#5-fuentes-de-vídeo-directo-videourlsource)
6. [Fuentes de audio: `AudioUrlSource`](#6-fuentes-de-audio-audiourlsource)
7. [HLS en vivo: `HLSSource`](#7-hls-en-vivo-hlssource)
8. [DASH: `DashSource`](#8-dash-dashsource)
9. [Vídeo + audio separados: `UnMuxVideoSourceDescriptor`](#9-vídeo--audio-separados-unmuxvideosourcedescriptor)
10. [Modificadores de petición: `requestModifier`](#10-modificadores-de-petición-requestmodifier)
11. [El proxy interno (`stream.c`)](#11-el-proxy-interno-streamc)
12. [Tabla comparativa](#12-tabla-comparativa)
13. [Patrones comunes](#13-patrones-comunes)
14. [Errores frecuentes](#14-errores-frecuentes)
15. [Resumen de reproducción](#15-resumen-de-reproducción)

---

## Parte I: uso y creación de plugins

## 1. Lista de plugins disponibles

| Plugin               | Autor     | Versión | Descripción                                                  |
| -------------------- | --------- | ------- | ------------------------------------------------------------ |
| **RamenTV**          | IamJony   | 1.0.1   | Anime sub español                                            |
| **Internet Archive** | IamJony   | 1.0.1   | Mira videos, películas, series y anime en español            |
| **Odysee**           | IamJony   | 1.0.1   | Cliente Odysee / LBRY                                        |
| **Hackstream**       | IamJony   | 1.0.1   | Hacking ético y ciberseguridad en español                    |
| **YouTube**          | AnddyCort | 1.0     | Cliente de YouTube                                           |
| **Twitch**           | AnddyCort | 1.0     | Cliente de Twitch                                            |
| **Kick**             | AnddyCort | 1.0     | Cliente de Kick                                              |
| **YouTube Music**    | AnddyCort | 1.0     | Cliente de YouTube Music. Depende del plugin YouTube para funcionar |

### Enlaces directos

- **RamenTV**: https://raw.githubusercontent.com/IamJony/plugins-tinytv/main/zips/ramentv.zip
- **Internet Archive**: https://raw.githubusercontent.com/IamJony/plugins-tinytv/main/zips/archive.zip
- **Odysee**: https://raw.githubusercontent.com/IamJony/plugins-tinytv/main/zips/odysee.zip
- **Hackstream**: https://raw.githubusercontent.com/IamJony/plugins-tinytv/main/zips/hackstream.zip
- **YouTube**: https://raw.githubusercontent.com/IamJony/plugins-tinytv/main/zips/youtube.zip
- **Twitch**: https://raw.githubusercontent.com/IamJony/plugins-tinytv/main/zips/twitch.zip
- **Kick**: https://raw.githubusercontent.com/IamJony/plugins-tinytv/main/zips/kick.zip
- **YouTube Music**: https://raw.githubusercontent.com/IamJony/plugins-tinytv/main/zips/music.zip

### Índice completo (JSON)

```
https://raw.githubusercontent.com/IamJony/plugins-tinytv/refs/heads/main/index.json
```

> **Nota:** El plugin **YouTube Music** requiere que el plugin **YouTube** esté instalado para funcionar correctamente.

---

## 2. Cómo crear nuestro propio plugin

### Plugin de ejemplo: Osos Polares (paso a paso)

Vamos a crear un plugin real usando el vídeo de osos polares de Wikimedia. Este plugin tendrá un solo vídeo y servirá para probar que todo funciona.

### Paso 1: crear la carpeta

```bash
mkdir -p ~/.config/tinytv/plugins/osos
cd ~/.config/tinytv/plugins/osos
```

### Paso 2: verificar `comun.js`

```bash
ls ~/.config/tinytv/plugins/comun.js
```

Si no existe:

```bash
curl -fsSL "https://raw.githubusercontent.com/IamJony/tinytv/refs/heads/main/plugins/comun.js" -o ~/.config/tinytv/plugins/comun.js
```

### Paso 3: crear `config.json`

```json
{
  "name": "Osos Polares",
  "description": "Vídeo de demostración: osos polares jugando (Wikimedia Commons)",
  "author": "TuNombre",
  "platformUrl": "https://commons.wikimedia.org",
  "version": "1.0.0",
  "id": "osos",
  "packages": ["Http"],
  "sansonIncludes": ["../comun.js"],
  "allowUrls": [
    "upload.wikimedia.org",
    ".upload.wikimedia.org",
    "commons.wikimedia.org",
    ".commons.wikimedia.org",
    "wikimedia.org",
    ".wikimedia.org"
  ],
  "comments": false,
  "chat": false
}
```

> `allowUrls` incluye `upload.wikimedia.org` porque ahí está alojado el vídeo.

### Paso 4: crear `script.js`

```js
"use strict";

const PLATFORM = "Osos Polares";

// URL del vídeo (240p VP9 WebM)
const VIDEO_URL = "https://upload.wikimedia.org/wikipedia/commons/transcoded/0/04/Play_fight_of_polar_bears_edit_1.ogv/Play_fight_of_polar_bears_edit_1.ogv.240p.vp9.webm";

// Miniatura del vídeo (primer fotograma)
const THUMB_URL = "https://upload.wikimedia.org/wikipedia/commons/thumb/0/04/Play_fight_of_polar_bears_edit_1.ogv/480px--Play_fight_of_polar_bears_edit_1.ogv.jpg";

// Página del vídeo en Wikimedia
const PAGE_URL = "https://commons.wikimedia.org/wiki/File:Play_fight_of_polar_bears_edit_1.ogv";

const VIDEO_TITLE = "Osos polares jugando";
const VIDEO_DESC = "Dos osos polares jugando en la nieve. Vídeo de demostración de Wikimedia Commons.";
const VIDEO_DURATION = 30; // segundos aproximados

let config = {};

// 1. Inicialización
source.enable = function (cfg, settings) {
    config = cfg || {};
};

// 2. Página de inicio: un solo vídeo
source.getHome = function () {
    const video = new PlatformVideo({
        id: new PlatformID(PLATFORM, "osos-polares", config.id),
        name: VIDEO_TITLE,
        thumbnails: new Thumbnails([
            new Thumbnail(THUMB_URL, 0)
        ]),
        author: new PlatformAuthorLink(
            new PlatformID(PLATFORM, "wikimedia", config.id),
            "Wikimedia Commons",
            "https://commons.wikimedia.org",
            null,
            null
        ),
        url: PAGE_URL,
        isLive: false,
        duration: VIDEO_DURATION,
        viewCount: -1,
        datetime: 0
    });

    return new VideoPager([video], false);
};

// 3. Detalle del vídeo: fuente de reproducción
source.getContentDetails = function (url) {
    const videoSource = new VideoUrlSource({
        name: "240p",
        url: VIDEO_URL,
        container: "webm",
        codec: "vp9",
        width: 426,
        height: 240,
        bitrate: 0,
        duration: VIDEO_DURATION
    });

    return new PlatformVideoDetails({
        id: new PlatformID(PLATFORM, "osos-polares", config.id),
        name: VIDEO_TITLE,
        thumbnails: new Thumbnails([
            new Thumbnail(THUMB_URL, 0)
        ]),
        author: new PlatformAuthorLink(
            new PlatformID(PLATFORM, "wikimedia", config.id),
            "Wikimedia Commons",
            "https://commons.wikimedia.org",
            null,
            null
        ),
        url: url,
        isLive: false,
        duration: VIDEO_DURATION,
        viewCount: -1,
        description: VIDEO_DESC,
        datetime: 0,
        video: new VideoSourceDescriptor([videoSource])
    });
};
```

### Paso 5: probar

1. Abre TinyTV.
2. Ve a la **Tienda de plugins** o reinicia la app.
3. Debería aparecer **Osos Polares**.
4. Ábrelo: verás la miniatura y el título "Osos polares jugando".
5. Haz clic: debería reproducir el vídeo de 240p.

### Qué hace cada parte

| Parte                      | Función                                                   |
| -------------------------- | --------------------------------------------------------- |
| `config.json`              | Metadatos + `allowUrls` con `upload.wikimedia.org`        |
| `source.enable`            | Guarda la config                                          |
| `source.getHome`           | Devuelve **1 vídeo** (`VideoPager`)                       |
| `source.getContentDetails` | Devuelve la **fuente de reproducción** (`VideoUrlSource`) |
| `VIDEO_URL`                | El `.webm` de Wikimedia                                   |
| `THUMB_URL`                | Miniatura del fotograma                                   |

### Opciones avanzadas y empaquetado

#### Probar la URL primero (opcional)

Antes de meterte en el plugin, verifica que el vídeo se descarga:

```bash
curl -I "https://upload.wikimedia.org/wikipedia/commons/transcoded/0/04/Play_fight_of_polar_bears_edit_1.ogv/Play_fight_of_polar_bears_edit_1.ogv.240p.vp9.webm"
```

Debe devolver `HTTP/2 200` y `content-type: video/webm`.

#### Añadir más calidades (opcional)

Wikimedia ofrece varias resoluciones del mismo vídeo. Puedes añadir más `VideoUrlSource`:

```js
const sources = [
    new VideoUrlSource({
        name: "240p", url: VIDEO_URL_240,
        container: "webm", codec: "vp9", width: 426, height: 240
    }),
    new VideoUrlSource({
        name: "480p", url: VIDEO_URL_480,
        container: "webm", codec: "vp9", width: 854, height: 480
    }),
    new VideoUrlSource({
        name: "720p", url: VIDEO_URL_720,
        container: "webm", codec: "vp9", width: 1280, height: 720
    })
];

// ...
video: new VideoSourceDescriptor(sources)
```

Las URLs siguen el patrón:
```
.../Play_fight_of_polar_bears_edit_1.ogv.240p.vp9.webm
.../Play_fight_of_polar_bears_edit_1.ogv.480p.vp9.webm
.../Play_fight_of_polar_bears_edit_1.ogv.720p.vp9.webm
```

#### Empaquetar

```bash
cd ~/.config/tinytv/plugins/osos
zip -r ../osos.zip config.json script.js
```

### Resumen de creación

```
Paso 1  →  mkdir ~/.config/tinytv/plugins/osos
Paso 2  →  verificar comun.js
Paso 3  →  config.json con allowUrls de wikimedia
Paso 4  →  script.js con getHome + getContentDetails
Paso 5  →  abrir TinyTV y reproducir el vídeo de osos
```

---

## Parte II: métodos de reproducción y funcionamiento interno

Documentación de los tipos de fuente de vídeo/audio que un plugin puede devolver en `source.getContentDetails()`, **cómo se construyen** y **qué ocurre internamente** en el host (C) cuando el reproductor los consume.

---

## 3. Visión general

Cuando el usuario pulsa un vídeo, TinyTV llama a:

```js
source.getContentDetails(url)
```

Esta función **debe** devolver un `PlatformVideoDetails` que contenga un objeto `video` del tipo `VideoSourceDescriptor`. Dentro de ese descriptor van una o varias **fuentes** (`videoSources`) que el reproductor intentará usar.

```
PlatformVideoDetails
└── video: VideoSourceDescriptor
    └── videoSources: [ VideoUrlSource | HLSSource | DashSource | ... ]
```

### Qué pasa internamente

1. El motor JS del host ejecuta el plugin y serializa el resultado (`__sansonSerialize`).
2. Cada fuente se convierte en una **entrada del proxy local** gestionada por `stream.c`.
3. El host arranca un servidor HTTP en `127.0.0.1` (`sp_start()`), que escucha en un puerto efímero.
4. El reproductor (mplayer) recibe URLs como `http://127.0.0.1:<puerto>/s/<id>` en lugar de la URL original.
5. Cuando mplayer pide bytes, `stream.c` hace la petición real al CDN con las cabeceras correctas y **reenvía los bytes**.

Esto permite:
- Inyectar `Referer`/`User-Agent`/cookies que el CDN exige.
- Manejar **rangos** (`Range: bytes=a-b`) sin que el CDN los soporte directamente.
- **Saltar anuncios** en HLS.
- Convertir audio AAC crudo en MPEG-TS con PTS correctos.
- Evitar CORS/Geo-bloqueos que el reproductor no puede gestionar.

Los tres tipos internos del proxy son:

```c
enum { K_RANGE, K_HLS, K_DIRECT };
```

| Tipo interno | Origen en el plugin                                        |
| ------------ | ---------------------------------------------------------- |
| `K_RANGE`    | `VideoUrlSource` / `AudioUrlSource` con `length` conocido  |
| `K_HLS`      | `HLSSource`                                                |
| `K_DIRECT`   | `VideoUrlSource` sin longitud conocida (streaming directo) |

---

## 4. El contenedor: `VideoSourceDescriptor`

Agrupa las fuentes **muxed** (vídeo + audio en el mismo stream).

```js
new VideoSourceDescriptor([source1, source2, source3])
```

| Campo          | Tipo  | Descripción                        |
| -------------- | ----- | ---------------------------------- |
| `videoSources` | array | Lista de fuentes de reproducción   |
| `isUnMuxed`    | bool  | Siempre `false` en este descriptor |

**Cuándo usarlo:** cuando cada fuente ya contiene vídeo y audio juntos (MP4, WebM, HLS, DASH muxed).

**Internamente:** el host recorre `videoSources` y registra cada una en el proxy con `sp_add_range`, `sp_add_direct` o `sp_add_hls` según corresponda. El reproductor recibe la lista de URLs locales y elige la primera compatible.

---

## 5. Fuentes de vídeo directo: `VideoUrlSource`

La más común. Apunta a un archivo de vídeo directo (MP4, WebM, MKV…).

```js
new VideoUrlSource({
    name: "720p",
    url: "https://ejemplo.com/video_720.mp4",
    referer: "https://ejemplo.com/",
    container: "mp4",
    codec: "h264",
    width: 1280,
    height: 720,
    bitrate: 0,
    duration: 3600
})
```

| Campo             | Tipo   | Obligatorio | Descripción                          |
| ----------------- | ------ | ----------- | ------------------------------------ |
| `name`            | string | Sí          | Etiqueta visible (ej. "720p")        |
| `url`             | string | Sí          | URL del archivo de vídeo             |
| `referer`         | string | No          | Referer HTTP para la petición        |
| `container`       | string | No          | `mp4`, `webm`, `mkv`, `m4a`…         |
| `codec`           | string | No          | `h264`, `vp9`, `av1`, `none` (audio) |
| `width`           | int    | No          | Ancho en píxeles                     |
| `height`          | int    | No          | Alto en píxeles                      |
| `bitrate`         | int    | No          | Bitrate en bps (0 = desconocido)     |
| `duration`        | int    | No          | Duración en segundos                 |
| `requestModifier` | objeto | No          | Cabeceras extra (ver sección 10)     |

### Funcionamiento interno

**Caso A — longitud conocida (`K_RANGE`).** Si el host puede inferir el tamaño (por ejemplo, con un `HEAD` previo o porque la URL incluye `clen=`), se registra con `sp_add_range(url, length, referer)`:

1. `stream.c` guarda la entrada y devuelve `http://127.0.0.1:<puerto>/s/<id>`.
2. Cuando mplayer pide `/s/<id>` con `Range: bytes=a-b`, se ejecuta `serve_range()`.
3. Se responde con `Content-Range: bytes a-b/total` y `Content-Length`.
4. Internamente, `serve_range` **trocea la descarga en bloques de 1 MB** (`SP_CHUNK`) y hace peticiones sucesivas al CDN añadiendo `?range=a-b&rn=N`. Esto permite:
   - Reintentar bloque a bloque si falla la red (hasta 4 fallos seguidos).
   - Reanudar si mplayer hace `seek`.
   - Evitar timeouts largos en CDNs lentos.
5. Si `referer` está definido, se añaden `Origin: <referer>` y `Referer: <referer>/` a cada petición.

**Caso B — longitud desconocida (`K_DIRECT`).** Se registra con `sp_add_direct(url)` y se ejecuta `serve_direct()`:

1. Se hace un `GET` con `CURLOPT_HEADERFUNCTION` capturando `Content-Length`, `Content-Range`, `Content-Type`, `Accept-Ranges`.
2. Se reenvían esas cabeceras al reproductor y se hace streaming del cuerpo con `CURLOPT_WRITEFUNCTION`.
3. Se soporta `Range` si el cliente lo pide (`CURLOPT_RANGE`).
4. Ideal para archivos sin longitud conocida o streams directos.

**Ejemplo real (plugin osos):**

```js
new VideoUrlSource({
    name: "240p",
    url: "https://upload.wikimedia.org/.../Play_fight_of_polar_bears_edit_1.ogv.240p.vp9.webm",
    container: "webm",
    codec: "vp9",
    width: 426,
    height: 240,
    duration: 30
})
```

Wikimedia soporta `Range`, así que el host lo tratará como `K_RANGE` si detecta la longitud, o como `K_DIRECT` si no.

---

## 6. Fuentes de audio: `AudioUrlSource`

Para audio separado del vídeo (pistas de audio independientes).

```js
new AudioUrlSource({
    name: "Audio ES",
    url: "https://ejemplo.com/audio_es.m4a",
    container: "m4a",
    codec: "aac",
    bitrate: 128000,
    duration: 3600,
    language: "es"
})
```

| Campo       | Tipo   | Descripción                    |
| ----------- | ------ | ------------------------------ |
| `name`      | string | Etiqueta visible               |
| `url`       | string | URL del audio                  |
| `container` | string | `m4a`, `mp3`, `opus`, `webm`   |
| `codec`     | string | `aac`, `mp3`, `opus`, `vorbis` |
| `bitrate`   | int    | Bitrate en bps                 |
| `duration`  | int    | Duración en segundos           |
| `language`  | string | Código ISO (`es`, `en`, `ja`…) |

**Cuándo usarlo:** solo cuando el vídeo y el audio están **separados** (ver sección 9). No se usa dentro de `VideoSourceDescriptor`.

### Funcionamiento interno

Se registra igual que un `VideoUrlSource` (`K_RANGE` o `K_DIRECT`). La diferencia es que **solo se usa dentro de `UnMuxVideoSourceDescriptor`**, y el reproductor mezcla ambas pistas en tiempo real.

Caso especial: si el audio es **ADTS AAC crudo** (típico en YouTube) y la fuente está marcada con `SP_AAC_TS`, `stream.c` ejecuta `aac_to_ts()` para convertir cada frame AAC en paquetes MPEG-TS con PTS correctos. Esto evita que mplayer reproduzca el audio desde 0 mientras el vídeo empieza en otro timestamp.

---

## 7. HLS en vivo: `HLSSource`

Para streams **HLS** (`.m3u8`). Es el estándar para directos y para muchos VOD.

```js
new HLSSource({
    name: "En Vivo",
    url: "https://ejemplo.com/live/stream.m3u8",
    duration: 0,
    isLive: true
})
```

| Campo      | Tipo   | Descripción          |
| ---------- | ------ | -------------------- |
| `name`     | string | Etiqueta visible     |
| `url`      | string | URL del `.m3u8`      |
| `duration` | int    | `0` si es en vivo    |
| `isLive`   | bool   | `true` para directos |

**Cuándo usarlo:**
- Directos en vivo (Twitch, Kick, YouTube Live).
- VODs que solo ofrecen `.m3u8`.
- Plataformas que segmentan el vídeo en trozos.

### Funcionamiento interno (el más complejo)

El host registra la fuente con `sp_add_hls(url, flags, start)` y ejecuta `serve_hls()`. Este bucle:

1. **Descarga el `.m3u8` maestro** (`#EXT-X-STREAM-INF`) con `sp_hls_variants()` para detectar variantes. Cada variante tiene `bandwidth`, `width`, `height`, `fps` y opcionalmente una pista de audio separada (`#EXT-X-MEDIA:TYPE=AUDIO`).
2. **Selecciona la variante** según calidad o la primera disponible.
3. **Bucle principal** mientras el cliente no se desconecte:
   - Descarga el `.m3u8` de media.
   - `parse_media()` extrae segmentos (`#EXTINF` + URI), secuencia (`#EXT-X-MEDIA-SEQUENCE`), `#EXT-X-ENDLIST`, `#EXT-X-MAP` (init segment para fMP4) y marca **anuncios** (segmentos sin `,live` en el título).
   - Salta los segmentos ya enviados (`seq < next`).
   - **Salta anuncios** si `SP_SKIP_ADS` está activo: los marca con `ad_seen = time(NULL)` (para que `sp_in_ad()` devuelva 1 durante 8 s) y **no los envía**.
   - Descarga cada segmento con `send_url()` (o `send_aac_seg()` si `SP_AAC_TS`) y lo reenvía tal cual.
   - Si es un directo (`SP_LIVE`) y no hay `start`, empieza por los **3 últimos segmentos** para reducir latencia.
   - Si es VOD, avanza hasta `start` segundos.
4. **Manejo de `#EXT-X-MAP`**: envía el init segment fMP4 antes del primer media segment. Twitch ya sirve algunos canales en fMP4.
5. **Detección de fin**: `sp_hls_duration()` calcula la duración total solo si el `.m3u8` tiene `#EXT-X-ENDLIST`.
6. **Relleno anti-timeout**: si pasan ~2 s sin datos mientras se salta un anuncio, envía paquetes nulos MPEG-TS (o una caja `free` si es fMP4) para que mplayer no interprete EOF (mplayer da fin de archivo tras ~14 s sin datos).
7. **Reintentos**: hasta 3 intentos por segmento; hasta 6 fallos seguidos del `.m3u8` antes de abortar.

**Nota:** TinyTV reproduce HLS internamente; el plugin solo entrega la URL del manifest.

---

## 8. DASH: `DashSource`

Para streams **DASH** (`.mpd`). Similar a HLS pero con otro formato de manifest.

```js
new DashSource({
    name: "1080p",
    url: "https://ejemplo.com/manifest.mpd",
    duration: 3600
})
```

| Campo      | Tipo   | Descripción          |
| ---------- | ------ | -------------------- |
| `name`     | string | Etiqueta visible     |
| `url`      | string | URL del `.mpd`       |
| `duration` | int    | Duración en segundos |

**Cuándo usarlo:** plataformas que ofrecen `.mpd` (algunas cuentas de Odysee, servicios con DRM ligero, etc.).

> Si la plataforma ofrece tanto HLS como DASH, prioriza **HLS**: TinyTV lo soporta de forma más estable.

**Internamente:** el host lo trata como una fuente directa (`K_DIRECT`) porque el reproductor soporta DASH nativamente vía su propio demuxer. No pasa por el bucle de `serve_hls`.

---

## 9. Vídeo + audio separados: `UnMuxVideoSourceDescriptor`

Cuando el vídeo y el audio vienen en **streams distintos** (común en YouTube, algunos CDNs).

```js
new UnMuxVideoSourceDescriptor(
    [ videoSource1, videoSource2 ],   // pistas de vídeo
    [ audioSource1, audioSource2 ]    // pistas de audio
)
```

| Campo          | Tipo  | Descripción                                   |
| -------------- | ----- | --------------------------------------------- |
| `videoSources` | array | `VideoUrlSource` / `HLSSource` / `DashSource` |
| `audioSources` | array | `AudioUrlSource`                              |
| `isUnMuxed`    | bool  | Siempre `true`                                |

**Cuándo usarlo:** la API devuelve URLs separadas para vídeo y audio (típico en YouTube, algunos CDNs de Odysee).

**Ejemplo:**

```js
const videoSources = [
    new VideoUrlSource({
        name: "720p",
        url: "https://cdn/video_720.mp4",
        container: "mp4",
        codec: "h264",
        width: 1280,
        height: 720
    })
];

const audioSources = [
    new AudioUrlSource({
        name: "Audio",
        url: "https://cdn/audio.m4a",
        container: "m4a",
        codec: "aac",
        language: "es"
    })
];

// ...
video: new UnMuxVideoSourceDescriptor(videoSources, audioSources)
```

### Funcionamiento interno

El host registra **cada pista por separado** en el proxy (vídeo como `K_RANGE`/`K_DIRECT`, audio como `K_RANGE`/`K_DIRECT` con posible `SP_AAC_TS`). El reproductor recibe dos URLs locales y las sincroniza por PTS.

Si el audio es AAC crudo (YouTube), `stream.c` convierte cada segmento a MPEG-TS con `aac_to_ts()`, leyendo el timestamp del tag ID3 `com.apple.streaming.transportStreamTimestamp`. Sin esta conversión, mplayer reproduciría el audio desde 0 mientras el vídeo empieza en el reloj de emisión.

---

## 10. Modificadores de petición: `requestModifier`

Muchos CDNs requieren cabeceras específicas (`Referer`, `User-Agent`, `Origin`, cookies). Se añaden a la fuente:

```js
new VideoUrlSource({
    name: "720p",
    url: "https://cdn/video.mp4",
    container: "mp4",
    codec: "h264",
    width: 1280,
    height: 720,
    requestModifier: {
        headers: {
            "Referer": "https://ejemplo.com/",
            "User-Agent": "Mozilla/5.0 ...",
            "Origin": "https://ejemplo.com"
        }
    }
})
```

**Cuándo usarlo:**
- El CDN devuelve `403` sin `Referer`.
- La plataforma exige un `User-Agent` concreto.
- Hay que enviar cookies de sesión.

### Funcionamiento interno

El host traduce `requestModifier.headers` a un `struct curl_slist` que se pasa a `new_curl()` en `stream.c`. Además, si `referer` está definido, se añaden automáticamente `Origin: <referer>` y `Referer: <referer>/` a las peticiones de `serve_range`.

**Ejemplo real (plugin hackstream):**

```js
const ODYSEE_STREAM_HEADERS = {
    "User-Agent": DEFAULT_UA,
    "Origin": "https://odysee.com",
    "Referer": "https://odysee.com/",
    "X-Odysee-User-Id": SESSION_USER_ID,
    "Sec-Fetch-Dest": "empty",
    "Sec-Fetch-Mode": "cors",
    "Sec-Fetch-Site": "cross-site"
};

new VideoUrlSource({
    name: "HackStream / Odysee",
    url: proxyUrl,
    referer: "https://odysee.com/",
    container: "mp4",
    codec: "h264",
    requestModifier: {
        headers: Object.assign({}, ODYSEE_STREAM_HEADERS, {
            "Referer": "https://odysee.com/"
        })
    }
})
```

Además, HackStream usa `sp_add_hackstream(url)` para lanzar un **proxy externo** (`hackstream_proxy`) que resuelve la URL de Odysee/LBRY y devuelve una URL local. `stream.c` lee la primera línea de su salida (la URL) y la registra como fuente.

---

## 11. El proxy interno (`stream.c`)

Es el componente C que hace de **intermediario** entre el reproductor y el CDN. Se compila dentro de TinyTV y arranca con `sp_start()`.

### Arquitectura

```
mplayer  ──HTTP──►  stream.c (127.0.0.1:puerto)
                      │
                      ├─ K_RANGE  → serve_range()   (bloques de 1 MB con ?range=)
                      ├─ K_DIRECT → serve_direct()  (streaming con Range opcional)
                      └─ K_HLS    → serve_hls()     (bucle de segmentos + anti-timeout)
                      │
                      ▼
                    CDN real (con Referer/UA/cookies)
```

### Registro de fuentes

| Función del plugin            | Función interna                                       | Tipo                 |
| ----------------------------- | ----------------------------------------------------- | -------------------- |
| `VideoUrlSource` con longitud | `sp_add_range(url, length, referer)`                  | `K_RANGE`            |
| `VideoUrlSource` sin longitud | `sp_add_direct(url)`                                  | `K_DIRECT`           |
| `HLSSource`                   | `sp_add_hls(url, flags, start)`                       | `K_HLS`              |
| `sp_add_hackstream(url)`      | lanza `hackstream_proxy` y registra la URL resultante | `K_DIRECT`/`K_RANGE` |

Cada entrada se guarda en `entries[SP_MAX]` (máx. 64) con `id`, `kind`, `url`, `referer`, `length`, `flags`, `start`. El host devuelve `http://127.0.0.1:<puerto>/s/<id>`.

### Hilos

- **`accept_thread`**: acepta conexiones y crea un hilo por conexión (`conn_thread`).
- **`conn_thread`**: parsea la petición HTTP, extrae `id` y `Range`, busca la entrada y llama al `serve_*` correspondiente. Prioridad baja (`setpriority` 5) para no competir con la UI.
- **Un hilo por conexión**: mplayer puede abrir varias conexiones en paralelo (seek, precarga).

### Concurrencia

- `lock` (mutex) protege `entries` y `next_id`.
- `sp_clear()` vacía todas las entradas (se llama al cambiar de vídeo).
- `sp_stop_hackstream()` mata el proceso hijo `hackstream_proxy` y espera 500 ms para que el SO cierre los sockets del CDN.

### Depuración

Con `SP_DEBUG=1` en el entorno, `DBG()` imprime trazas en `stderr`: peticiones, códigos HTTP, segmentos enviados, anuncios saltados, etc.

---

## 12. Tabla comparativa

| Tipo                         | Cuándo usarlo                | Vídeo | Audio | En vivo | Tipo interno           |
| ---------------------------- | ---------------------------- | ----- | ----- | ------- | ---------------------- |
| `VideoUrlSource`             | Archivo directo MP4/WebM/MKV | SI     | SI     | NO       | `K_RANGE` / `K_DIRECT` |
| `AudioUrlSource`             | Pista de audio separada      | NO     | SI     | NO       | `K_RANGE` / `K_DIRECT` |
| `HLSSource`                  | `.m3u8` (VOD o live)         | SI     | SI     | SI       | `K_HLS`                |
| `DashSource`                 | `.mpd`                       | SI     | SI     | SI       | `K_DIRECT`             |
| `UnMuxVideoSourceDescriptor` | Vídeo y audio separados      | SI     | SI     | NO       | Mixto                  |

---

## 13. Patrones comunes

### 13.1. Un solo archivo MP4

```js
source.getContentDetails = function (url) {
    return new PlatformVideoDetails({
        // ...metadata...
        video: new VideoSourceDescriptor([
            new VideoUrlSource({
                name: "Original",
                url: "https://cdn/video.mp4",
                container: "mp4",
                codec: "h264"
            })
        ])
    });
};
```

### 13.2. Varias calidades del mismo archivo

```js
const sources = [
    new VideoUrlSource({ name: "1080p", url: url1080, container: "mp4", codec: "h264", width: 1920, height: 1080 }),
    new VideoUrlSource({ name: "720p",  url: url720,  container: "mp4", codec: "h264", width: 1280, height: 720  }),
    new VideoUrlSource({ name: "480p",  url: url480,  container: "mp4", codec: "h264", width: 854,  height: 480  })
];

video: new VideoSourceDescriptor(sources)
```

### 13.3. Directo en vivo (HLS)

```js
video: new VideoSourceDescriptor([
    new HLSSource({
        name: "En Vivo",
        url: "https://cdn/live/stream.m3u8",
        duration: 0,
        isLive: true
    })
])
```

### 13.4. Vídeo + audio separados (tipo YouTube)

```js
video: new UnMuxVideoSourceDescriptor(
    [ new VideoUrlSource({ name: "720p", url: vUrl, container: "mp4", codec: "h264", width: 1280, height: 720 }) ],
    [ new AudioUrlSource({ name: "Audio", url: aUrl, container: "m4a", codec: "aac" }) ]
)
```

### 13.5. CDN con `Referer` obligatorio

```js
new VideoUrlSource({
    name: "720p",
    url: "https://cdn/video.mp4",
    container: "mp4",
    codec: "h264",
    requestModifier: {
        headers: {
            "Referer": "https://plataforma.com/",
            "User-Agent": "Mozilla/5.0 ..."
        }
    }
})
```

### 13.6. Proxy externo (HackStream)

```js
const proxyUrl = sp_add_hackstream("https://odysee.com/@canal:abc/video:def");
if (proxyUrl) {
    sources.push(new VideoUrlSource({
        name: "HackStream",
        url: proxyUrl,
        referer: "https://odysee.com/",
        container: "mp4",
        codec: "h264",
        requestModifier: { headers: ODYSEE_STREAM_HEADERS }
    }));
}
```

---

## 14. Errores frecuentes

| Síntoma                         | Causa probable                                          | Solución                                                     |
| ------------------------------- | ------------------------------------------------------- | ------------------------------------------------------------ |
| El vídeo no reproduce           | `video` no es `VideoSourceDescriptor`                   | Envuelve las fuentes en `new VideoSourceDescriptor([...])`   |
| Error `403`                     | Falta `Referer` o `User-Agent`                          | Añade `requestModifier.headers`                              |
| El directo no arranca           | Usaste `VideoUrlSource` para un `.m3u8`                 | Usa `HLSSource`                                              |
| Solo se oye, no se ve           | Mezclaste vídeo y audio en `UnMuxVideoSourceDescriptor` | Usa `VideoSourceDescriptor` si están muxed                   |
| Solo se ve, no se oye           | El vídeo no tiene audio y no añadiste `AudioUrlSource`  | Usa `UnMuxVideoSourceDescriptor` con pista de audio          |
| Calidad no cambia               | Las fuentes no tienen `width`/`height`                  | Rellena `width` y `height` para que el reproductor las distinga |
| `UnavailableException`          | `sources` está vacío                                    | Verifica que `getContentDetails` devuelve al menos una fuente |
| El directo se corta solo        | mplayer interpreta EOF tras ~14 s sin datos             | Ya lo gestiona `serve_hls` con relleno; si persiste, revisa `SP_SKIP_ADS` |
| El audio empieza desincronizado | AAC crudo sin PTS                                       | Marca la fuente con `SP_AAC_TS` para que `aac_to_ts()` lo convierta |
| `hackstream_proxy fallo`        | No se encuentra el binario                              | Revisa las rutas en `run_hackstream_proxy_url()`             |

---

## 15. Resumen de reproducción

```
getContentDetails(url)
  └─ PlatformVideoDetails
       └─ video: VideoSourceDescriptor([...])        ← muxed (vídeo+audio juntos)
          o
          video: UnMuxVideoSourceDescriptor([...], [...])  ← separados

Fuentes posibles:
  VideoUrlSource   → MP4/WebM/MKV directo     → K_RANGE / K_DIRECT
  HLSSource        → .m3u8 (VOD o live)       → K_HLS (bucle de segmentos)
  DashSource       → .mpd                     → K_DIRECT
  AudioUrlSource   → solo audio (para UnMux)  → K_RANGE / K_DIRECT (+ AAC→TS)

Opcional en cualquier fuente:
  requestModifier.headers → Referer, User-Agent, cookies...

Internamente, stream.c actúa de proxy local:
  mplayer ──► 127.0.0.1:puerto/s/<id> ──► CDN real
  (inyecta cabeceras, maneja rangos, salta anuncios, convierte AAC→TS)
```

> **Regla práctica:** si la URL termina en `.mp4`/`.webm` → `VideoUrlSource`. Si termina en `.m3u8` → `HLSSource`. Si termina en `.mpd` → `DashSource`. Si la API te da vídeo y audio por separado → `UnMuxVideoSourceDescriptor`. Si el CDN exige cabeceras → `requestModifier.headers`. Si la URL necesita resolución especial (LBRY/Odysee) → `sp_add_hackstream`.

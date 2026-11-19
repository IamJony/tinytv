const PLATFORM = "LatinoTV";
const BASE = "https://raw.githubusercontent.com/IamJony/IPTVListaLibre/refs/heads/main";
const IPTV_API = "https://iptv-org.github.io/api/streams.json";
const UA = "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";

const COUNTRIES = [
    { iso: "ar", file: "ar_canales.m3u", name: "Argentina" },
    { iso: "bo", file: "bo_canales.m3u", name: "Bolivia" },
    { iso: "cl", file: "cl_canales.m3u", name: "Chile" },
    { iso: "co", file: "co_canales.m3u", name: "Colombia" },
    { iso: "cr", file: "cr_canales.m3u", name: "Costa Rica" },
    { iso: "do", file: "do_canales.m3u", name: "Rep. Dominicana" },
    { iso: "ec", file: "ec_canales.m3u", name: "Ecuador" },
    { iso: "sv", file: "sv_canales.m3u", name: "El Salvador" },
    { iso: "gq", file: "gq_canales.m3u", name: "Guinea Ecuatorial" },
    { iso: "gt", file: "gt_canales.m3u", name: "Guatemala" },
    { iso: "hn", file: "hn_canales.m3u", name: "Honduras" },
    { iso: "mx", file: "mx_canales.m3u", name: "México" },
    { iso: "ni", file: "ni_canales.m3u", name: "Nicaragua" },
    { iso: "pa", file: "pa_canales.m3u", name: "Panamá" },
    { iso: "py", file: "py_canales.m3u", name: "Paraguay" },
    { iso: "pe", file: "pe_canales.m3u", name: "Perú" },
    { iso: "pr", file: "pr_canales.m3u", name: "Puerto Rico" },
    { iso: "es", file: "es_canales.m3u", name: "España" },
    { iso: "uy", file: "uy_canales.m3u", name: "Uruguay" },
    { iso: "ve", file: "ve_canales.m3u", name: "Venezuela" },
    { iso: "",   file: "cine.m3u",        name: "Cine" },
];

let cache = {};       // cache[file] = texto M3U
let streamsApi = null; // Cache de la API de iptv-org

source.enable = function () {};

function fetchM3U(file) {
    if (cache[file]) return cache[file];
    const url = BASE + "/" + file;
    const resp = http.GET(url, { "User-Agent": UA }, false);
    if (!resp.isOk) throw new ScriptException("Error al descargar " + file + ": " + resp.code);
    return (cache[file] = resp.body);
}

// Obtiene la lista de streams de iptv-org (solo la primera vez)
function fetchStreamsApi() {
    if (streamsApi) return streamsApi;
    try {
        const resp = http.GET(IPTV_API, { "User-Agent": UA }, false);
        if (resp.isOk) {
            streamsApi = JSON.parse(resp.body);
        } else {
            streamsApi = [];
        }
    } catch (e) {
        streamsApi = [];
    }
    return streamsApi;
}

// Busca un stream en la API por su URL
function findStreamInfo(url) {
    const api = fetchStreamsApi();
    // La API devuelve un array de objetos { url, quality, labels, ... }
    // Buscamos coincidencia exacta de URL
    const found = api.find(s => s.url === url);
    return found || null;
}

function parseM3U(text) {
    const items = [];
    const lines = text.split("\n");
    let currentInfo = null;

    for (let line of lines) {
        line = line.trim();
        if (line.startsWith("#EXTINF:")) {
            const match = /^#EXTINF:-?\d+(?:\s+[^,]+)?,\s*(.+)$/i.exec(line);
            if (match) {
                const name = match[1].trim();
                let logo = "";
                const logoMatch = /tvg-logo="([^"]+)"/i.exec(line);
                if (logoMatch) logo = logoMatch[1];
                let group = "";
                const groupMatch = /group-title="([^"]+)"/i.exec(line);
                if (groupMatch) group = groupMatch[1];
                let tvgId = "";
                const idMatch = /tvg-id="([^"]+)"/i.exec(line);
                if (idMatch) tvgId = idMatch[1];

                currentInfo = { name, logo, group, tvgId };
            }
        } else if (line.startsWith("http") && currentInfo) {
            const url = line;
            const id = sansonPid(PLATFORM, url);
            
            // Intentamos obtener la calidad y etiquetas de la API
            let quality = "";
            let labels = [];
            const streamInfo = findStreamInfo(url);
            if (streamInfo) {
                quality = streamInfo.quality || "";
                labels = streamInfo.labels || [];
            }

            items.push(new PlatformVideo({
                id: id,
                name: currentInfo.name,
                thumbnails: sansonThumbs(currentInfo.logo),
                author: sansonAuthor(PLATFORM, "", currentInfo.group || "Latinoamérica", "", null),
                url: url,
                isLive: true,
                duration: 0,
                viewCount: -1,
                // Metadatos extra (no estándar, pero útil para el detalle)
                quality: quality,
                labels: labels,
                tvgId: currentInfo.tvgId
            }));
            currentInfo = null;
        }
    }
    return items;
}

// Tarjeta de país en el inicio.
function countryCard(c) {
    const flag = c.iso
        ? "https://flagcdn.com/w160/" + c.iso + ".png"
        : "https://flagcdn.com/w160/un.png";
    return new PlatformChannel({
        id: sansonPid(PLATFORM, "country:" + c.file),
        name: c.name,
        url: "country:" + c.file,
        thumbnail: flag,
        description: "Canales de " + c.name,
        subscribers: -1
    });
}

source.getHome = function () {
    return new VideoPager(COUNTRIES.map(countryCard), false);
};

source.search = function (query) {
    const text = fetchM3U("es_unificado.m3u");
    const items = parseM3U(text);
    const q = query.toLowerCase();
    const filtered = items.filter(it => it.name.toLowerCase().includes(q));
    return new VideoPager(filtered, false);
};

source.getContentDetails = function (url) {
    // 1. Buscamos el nombre real del canal en la lista M3U
    let channelName = "Canal en vivo";
    let groupName = "Latinoamérica";
    let qualityText = "Calidad desconocida";
    
    // Buscamos en todas las listas (es rápido porque ya están en caché o se descargan)
    // Para evitar descargar todo, podríamos buscar solo en la lista que corresponde, 
    // pero como el usuario pudo haber buscado, lo hacemos global.
    const allFiles = COUNTRIES.map(c => c.file).concat("es_unificado.m3u");
    for (const file of allFiles) {
        try {
            const text = fetchM3U(file);
            const lines = text.split("\n");
            let tempName = null;
            let tempGroup = null;
            for (const line of lines) {
                if (line.startsWith("#EXTINF:")) {
                    const match = /^#EXTINF:-?\d+(?:\s+[^,]+)?,\s*(.+)$/i.exec(line);
                    if (match) {
                        tempName = match[1].trim();
                        const groupMatch = /group-title="([^"]+)"/i.exec(line);
                        tempGroup = groupMatch ? groupMatch[1] : null;
                    }
                } else if (line.trim() === url && tempName) {
                    channelName = tempName;
                    groupName = tempGroup || groupName;
                    break;
                }
            }
            if (channelName !== "Canal en vivo") break; // Encontrado
        } catch (e) {
            // Ignorar errores de descarga de otras listas
        }
    }

    // 2. Intentamos obtener la calidad de la API
    const streamInfo = findStreamInfo(url);
    if (streamInfo) {
        if (streamInfo.quality) {
            qualityText = streamInfo.quality;
        }
        if (streamInfo.labels && streamInfo.labels.length > 0) {
            qualityText += " (" + streamInfo.labels.join(", ") + ")";
        }
    }

    // 3. Construimos la descripción con advertencia
    const description = `Canal: ${channelName}\nCategoría: ${groupName}\nCalidad: ${qualityText}\n\n️ AVISO: Algunos canales pueden no funcionar. Todos los enlaces son extraídos de fuentes libres y públicas. Si un canal no carga, prueba con otro. La resolucion y calidad depende completamente de la transmicion de ese canal no de la app`;

    const isHls = url.includes(".m3u8") || url.includes(".ts");

    if (isHls) {
        return new PlatformVideoDetails({
            id: sansonPid(PLATFORM, url),
            name: channelName, // Nombre real
            url: url,
            isLive: true,
            description: description, // Descripción con advertencia
            video: new VideoSourceDescriptor([
                new HLSSource({
                    name: "En Vivo",
                    url: url,
                    duration: 0
                })
            ])
        });
    }

    return new PlatformVideoDetails({
        id: sansonPid(PLATFORM, url),
        name: channelName,
        url: url,
        isLive: true,
        description: description,
        video: new VideoSourceDescriptor([
            new VideoUrlSource({
                name: "En Vivo",
                url: url,
                container: "mp4",
                codec: "h264",
                width: 0, height: 0, bitrate: 0, duration: 0
            })
        ])
    });
};

source.getChannel = function (url) {
    let name = "LatinoTV", desc = "Canales de Latinoamérica por país";
    if (url && url.startsWith("country:")) {
        const file = url.substring("country:".length);
        const c = COUNTRIES.find(x => x.file === file);
        if (c) {
            name = c.name;
            desc = "Canales de " + c.name;
        }
    }
    return new PlatformChannel({
        id: sansonPid(PLATFORM, url || "latinotv-all"),
        name: name,
        url: "https://github.com/IamJony/IPTVListaLibre",
        description: desc,
        subscribers: -1
    });
};

source.getChannelContents = function (url) {
    let file = "es_unificado.m3u";
    if (url && url.startsWith("country:"))
        file = url.substring("country:".length);
    const text = fetchM3U(file);
    const items = parseM3U(text);
    return new VideoPager(items, false);
};

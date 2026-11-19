// Sanson: helpers shared by its plugins. Evaluated before each script.js
// that lists it in "sansonIncludes". GPL-3.0-or-later.

const SANSON_UA = "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/140.0.0.0 Safari/537.36";

// Text of a YouTube-style text object: "x", {simpleText}, {runs: [{text}]}, {content}.
function sansonText(t) {
    if (!t) return "";
    if (typeof t === "string") return t;
    if (t.simpleText != null) return t.simpleText;
    if (t.content != null) return t.content;
    return (t.runs ?? []).map(r => r.text).join("");
}

// "1:02:03" -> 3723; anything else -> 0.
function sansonSecs(s) {
    s = String(s ?? "").trim();
    if (!/^\d+(:\d+){1,2}$/.test(s)) return 0;
    return s.split(":").reduce((a, b) => a * 60 + parseInt(b, 10), 0);
}

// "1,2 M de visualizaciones", "168 K", "15 mil", "1.234 vistas", "1.2M views" -> number, -1 if none.
function sansonCount(s) {
    s = String(s ?? "");
    const m = /(\d[\d.,\s]*)\s*(mil\s*M|mil|k|K|M|B|mill)?/.exec(s);
    if (!m) return -1;
    let digits = m[1].replace(/\s/g, "");
    let mult = 1;
    const u = (m[2] ?? "").replace(/\s/g, "");
    if (u === "k" || u === "K" || u === "mil") mult = 1e3;
    else if (u === "M" || u === "mill") mult = 1e6;
    else if (u === "B" || u === "milM") mult = 1e9;
    if (mult === 1) {
        digits = digits.replace(/[.,]/g, "");
    } else {
        // "1,2" (es) or "1.2" (en) is a decimal; "1.234" with a unit does not happen.
        digits = digits.replace(",", ".");
    }
    const n = parseFloat(digits);
    return isNaN(n) ? -1 : Math.round(n * mult);
}

// "hace 3 días", "Emitido hace 2 semanas", "3 days ago" -> epoch seconds (approximate), 0 if none.
function sansonAgo(s) {
    s = String(s ?? "").toLowerCase();
    const m = /(\d+)\s*(segundo|second|minuto|minute|min|hora|hour|día|dia|day|semana|week|mes|month|año|ano|year)/.exec(s);
    if (!m) return 0;
    const n = parseInt(m[1], 10);
    const unit = m[2];
    const secs = /^seg|^sec/.test(unit) ? 1 : /^min/.test(unit) ? 60 : /^hor|^hou/.test(unit) ? 3600
        : /^d/.test(unit) ? 86400 : /^sem|^wee/.test(unit) ? 604800 : /^mes|^mon/.test(unit) ? 2592000 : 31536000;
    return Math.floor(Date.now() / 1000) - n * secs;
}

function sansonIso(s) {
    const t = Date.parse(s ?? "");
    return isNaN(t) ? 0 : Math.floor(t / 1000);
}

function sansonPid(platform, id) {
    return new PlatformID(platform, String(id ?? ""), plugin.config?.id ?? "");
}

function sansonAuthor(platform, id, name, url, thumb, subs) {
    return new PlatformAuthorLink(sansonPid(platform, id), name ?? "", url ?? "", thumb ?? null, subs ?? null);
}

function sansonThumbs(url) {
    return new Thumbnails(url ? [new Thumbnail(url, 360)] : []);
}

// A page that asks to prove you are not a robot (Cloudflare, Google's "unusual traffic"): the app
// offers to pass it in the browser.
function sansonChallenge(resp, url) {
    if ((resp.code === 403 || resp.code === 429 || resp.code === 503) &&
        /challenge-platform|cf-chl|Just a moment|google\.com\/sorry|unusual traffic|captcha/i.test(resp.body ?? ""))
        throw new CaptchaRequiredException(url, "");
}

// HTTP that throws a readable error instead of returning a failed response.
function sansonRequest(method, url, body, headers, useAuth, what) {
    const h = Object.assign({ "User-Agent": SANSON_UA }, headers ?? {});
    const resp = method === "GET" ? http.GET(url, h, !!useAuth) : http.POST(url, body ?? "", h, !!useAuth);
    if (!resp.isOk) {
        sansonChallenge(resp, url);
        if (resp.code === 401 || resp.code === 403)
            throw new ScriptException((what ?? "El servidor") + " rechazó la petición [" + resp.code + "]");
        if (resp.code === 0)
            throw new ScriptException("Sin conexión con " + (what ?? url));
        throw new ScriptException((what ?? "El servidor") + " respondió " + resp.code);
    }
    return resp.body;
}

function sansonJson(method, url, body, headers, useAuth, what) {
    const text = sansonRequest(method, url, body, headers, useAuth, what);
    try {
        return JSON.parse(text);
    } catch (e) {
        throw new ScriptException("Respuesta inválida de " + (what ?? url));
    }
}

// A pager whose pages come from fetch(context) -> {results, more, context}.
class SansonPager extends VideoPager {
    constructor(fetch, context, first) {
        const page = first ?? fetch(context ?? {});
        super(page.results ?? [], !!page.more, page.context ?? {});
        this.fetch = fetch;
    }
    nextPage() {
        return new SansonPager(this.fetch, this.context);
    }
}

class SansonChannelPager extends ChannelPager {
    constructor(results) {
        super(results ?? [], false, {});
    }
}

// Interleaves lists: a1 b1 c1 a2 b2... without repeating urls.
function sansonMix(lists, seen) {
    seen = seen ?? new Set();
    const out = [];
    const max = Math.max(0, ...lists.map(l => l.length));
    for (let i = 0; i < max; i++)
        for (const l of lists) {
            const v = l[i];
            if (v && v.url && !seen.has(v.url)) {
                seen.add(v.url);
                out.push(v);
            }
        }
    return out;
}

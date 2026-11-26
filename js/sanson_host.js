"use strict";

const Type = {
    Feed: { Videos: "VIDEOS", Streams: "STREAMS", Mixed: "MIXED", Live: "LIVE", Subscriptions: "SUBSCRIPTIONS", Shorts: "SHORTS", Audio: "AUDIO" },
    Order: { Chronological: "CHRONOLOGICAL", Views: "VIEWS", Favorites: "FAVORITES" },
    Date: { LastHour: "LAST_HOUR", Today: "TODAY", LastWeek: "LAST_WEEK", LastMonth: "LAST_MONTH", LastYear: "LAST_YEAR" },
    Duration: { Short: "SHORT", Medium: "MEDIUM", Long: "LONG" },
    Text: { RAW: 0, HTML: 1, MARKUP: 2 },
    Chapter: { NORMAL: 0, SKIPPABLE: 5, SKIP: 6, SKIPONCE: 7 }
};

const Language = {
    UNKNOWN: "Unknown", ARABIC: "ar", SPANISH: "es", FRENCH: "fr", HINDI: "hi", INDONESIAN: "id",
    KOREAN: "ko", PORTBRAZIL: "pt-BR", RUSSIAN: "ru", THAI: "th", TURKISH: "tr", VIETNAMESE: "vi",
    ENGLISH: "en", GERMAN: "de", ITALIAN: "it", JAPANESE: "ja", CHINESE: "zh", PORTUGUESE: "pt"
};

class ScriptException extends Error {
    constructor(type, msg) {
        super(msg === undefined ? type : msg);
        this.plugin_type = msg === undefined ? "ScriptException" : type;
        this.msg = this.message;
    }
}
class ScriptImplementationException extends ScriptException { constructor(msg) { super("ScriptImplementationException", msg); } }
class TimeoutException extends ScriptException { constructor(msg) { super("TimeoutException", msg); } }
class UnavailableException extends ScriptException { constructor(msg) { super("UnavailableException", msg); } }
class LoginRequiredException extends ScriptException { constructor(msg) { super("LoginRequiredException", msg); } }
class AgeException extends ScriptException { constructor(msg) { super("AgeException", msg); } }
class ScriptLoginRequiredException extends ScriptException { constructor(msg) { super("ScriptLoginRequiredException", msg); } }
class CriticalException extends ScriptException { constructor(msg) { super("CriticalException", msg); } }
class ReloadRequiredException extends ScriptException {
    constructor(msg, reloadData) {
        super("ReloadRequiredException", msg);
        this.reloadData = reloadData;
    }
}
class CaptchaRequiredException extends ScriptException {
    constructor(url, body) {
        super("CaptchaRequiredException", "Captcha required: " + url);
        this.url = url;
        this.body = body;
    }
}

class PlatformID {
    constructor(platform, id, pluginId, claimType, claimFieldType) {
        this.platform = platform ?? "";
        this.value = id ?? "";
        this.pluginId = pluginId ?? "";
        this.claimType = claimType ?? 0;
        this.claimFieldType = claimFieldType ?? -1;
    }
}
class Thumbnail {
    constructor(url, quality) {
        this.url = url ?? "";
        this.quality = quality ?? 0;
    }
}
class Thumbnails {
    constructor(sources) { this.sources = sources ?? []; }
}
class PlatformAuthorLink {
    constructor(id, name, url, thumbnail, subscribers, membershipUrl) {
        this.id = id ?? new PlatformID();
        this.name = name ?? "";
        this.url = url ?? "";
        this.thumbnail = thumbnail ?? null;
        this.subscribers = subscribers ?? null;
        this.membershipUrl = membershipUrl ?? null;
    }
}

class PlatformContent {
    constructor(obj, contentType) {
        Object.assign(this, obj ?? {});
        this.contentType = contentType;
    }
}
class PlatformVideo extends PlatformContent {
    constructor(obj) { super(obj, 1); }
}
class PlatformVideoDetails extends PlatformVideo {
    constructor(obj) { super(obj); }
}
class PlatformLockedContent extends PlatformContent {
    constructor(obj) { super(obj, 70); }
}
class PlatformPost extends PlatformContent {
    constructor(obj) { super(obj, 2); }
}
class PlatformPlaylist extends PlatformContent {
    constructor(obj) { super(obj, 4); }
}
class PlatformPlaylistDetails extends PlatformPlaylist {}
class PlatformChannel {
    constructor(obj) { Object.assign(this, obj ?? {}); }
}

class VideoSourceDescriptor {
    constructor(videoSources) {
        this.isUnMuxed = false;
        this.videoSources = videoSources ?? [];
    }
}
class MuxVideoSourceDescriptor extends VideoSourceDescriptor {}
class UnMuxVideoSourceDescriptor {
    constructor(videoSources, audioSources) {
        this.isUnMuxed = true;
        if (Array.isArray(videoSources)) {
            this.videoSources = videoSources;
            this.audioSources = audioSources ?? [];
        } else {
            this.videoSources = videoSources?.videoSources ?? [];
            this.audioSources = videoSources?.audioSources ?? [];
        }
    }
}
class VideoUrlSource { constructor(obj) { Object.assign(this, obj ?? {}); } }
class VideoUrlRangeSource extends VideoUrlSource {}
class AudioUrlSource { constructor(obj) { Object.assign(this, obj ?? {}); } }
class AudioUrlRangeSource extends AudioUrlSource {}
class HLSSource { constructor(obj) { Object.assign(this, obj ?? {}); } }
class DashSource { constructor(obj) { Object.assign(this, obj ?? {}); } }
class DashManifestRawSource { constructor(obj) { Object.assign(this, obj ?? {}); } }
class DashManifestRawAudioSource { constructor(obj) { Object.assign(this, obj ?? {}); } }
class UMPSource { constructor(obj) { Object.assign(this, obj ?? {}); } }
class RequestModifier { constructor(obj) { Object.assign(this, obj ?? {}); } }
class PlaybackTracker {
    constructor(interval) { this.nextRequest = interval ?? 10000; }
    setProgress(seconds) {}
}
class SubtitleSource { constructor(obj) { Object.assign(this, obj ?? {}); } }
class RawPlayerSource { constructor(obj) { Object.assign(this, obj ?? {}); } }

class RatingLikes { constructor(likes) { this.type = 1; this.likes = likes; } }
class RatingLikesDislikes { constructor(likes, dislikes) { this.type = 2; this.likes = likes; this.dislikes = dislikes; } }
class RatingScaler { constructor(value) { this.type = 3; this.value = value; } }

class Comment { constructor(obj) { Object.assign(this, obj ?? {}); } }

class ResultCapabilities {
    constructor(types, sorts, filters) {
        this.types = types ?? [];
        this.sorts = sorts ?? [];
        this.filters = filters ?? [];
    }
}
class FilterGroup {
    constructor(name, filters, isMultiSelect, id) {
        this.name = name; this.filters = filters ?? []; this.isMultiSelect = !!isMultiSelect; this.id = id ?? name;
    }
}
class FilterCapability {
    constructor(name, value, id) { this.name = name; this.value = value; this.id = id ?? name; }
}

class LiveEvent { constructor(type) { this.type = type; } }
class LiveEventComment extends LiveEvent {
    constructor(name, message, thumbnail, colorName, badges) {
        super(1); this.name = name; this.message = message; this.thumbnail = thumbnail; this.colorName = colorName; this.badges = badges;
    }
}
class LiveEventEmojis extends LiveEvent { constructor(emojis) { super(4); this.emojis = emojis; } }
class LiveEventDonation extends LiveEvent {
    constructor(amount, name, message, thumbnail, expire, colorDonation) {
        super(5); this.amount = amount; this.name = name; this.message = message; this.thumbnail = thumbnail; this.expire = expire; this.colorDonation = colorDonation;
    }
}
class LiveEventViewCount extends LiveEvent { constructor(viewCount) { super(10); this.viewCount = viewCount; } }
class LiveEventRaid extends LiveEvent {
    constructor(targetUrl, targetName, targetThumbnail) { super(100); this.targetUrl = targetUrl; this.targetName = targetName; this.targetThumbnail = targetThumbnail; }
}

class BasePager {
    constructor(results, hasMore, context) {
        this.results = results ?? [];
        this.hasMore = hasMore ?? false;
        this.context = context ?? {};
    }
    hasMorePagers() { return this.hasMore; }
    nextPage() { return new this.constructor([], false, this.context); }
}
class ContentPager extends BasePager {}
class VideoPager extends ContentPager {}
class ChannelPager extends BasePager {}
class PlaylistPager extends BasePager {}
class CommentPager extends BasePager {}
class LiveEventPager extends BasePager {
    constructor(results, hasMore, context) {
        super(results, hasMore, context);
        this.nextRequest = 4000;
    }
}

const __URL_PORTS = { "http:": "80", "https:": "443", "ws:": "80", "wss:": "443", "ftp:": "21" };
function __urlDots(path) {
    const out = [];
    const parts = path.split("/");
    for (let i = 0; i < parts.length; i++) {
        const seg = parts[i];
        if (seg === "..") {
            if (out.length > 1) out.pop();
            if (i === parts.length - 1) out.push("");
        } else if (seg === ".") {
            if (i === parts.length - 1) out.push("");
        } else {
            out.push(seg);
        }
    }
    const r = out.join("/");
    return r.startsWith("/") ? r : "/" + r;
}
class URL {
    constructor(url, base) {
        let s = String(url).trim();
        if (base !== undefined && base !== null && !/^[a-z][a-z0-9+.-]*:/i.test(s)) {
            const b = base instanceof URL ? base : new URL(base);
            if (b._opaque) throw new TypeError("Invalid URL: " + s);
            if (s.startsWith("//")) s = b.protocol + s;
            else if (s.startsWith("/")) s = b.origin + s;
            else if (s.startsWith("?")) s = b.origin + b.pathname + s;
            else if (s.startsWith("#")) s = b.origin + b.pathname + b.search + s;
            else if (s === "") s = b.origin + b.pathname + b.search;
            else s = b.origin + b.pathname.replace(/[^/]*$/, "") + s;
        }
        this._parse(s);
    }
    static canParse(url, base) {
        try {
            new URL(url, base);
            return true;
        } catch (e) {
            return false;
        }
    }
    _parse(s) {
        let m = /^([a-z][a-z0-9+.-]*:)\/\/(?:([^:@/?#]*)(?::([^@/?#]*))?@)?(\[[^\]]*\]|[^/:?#]*)(?::(\d*))?([^?#]*)(\?[^#]*)?(#.*)?$/i.exec(s);
        if (m) {
            this._opaque = false;
            this._protocol = m[1].toLowerCase();
            this.username = m[2] ?? "";
            this.password = m[3] ?? "";
            this._hostname = m[4].toLowerCase();
            const port = m[5] ?? "";
            this._port = port === __URL_PORTS[this._protocol] ? "" : port;
            this._pathname = __urlDots((m[6] || "/").replace(/\\/g, "/"));
            this._query = m[7] && m[7] !== "?" ? m[7].slice(1) : "";
            this._hash = m[8] && m[8] !== "#" ? m[8] : "";
        } else {
            m = /^([a-z][a-z0-9+.-]*:)([^?#]*)(\?[^#]*)?(#.*)?$/i.exec(s);
            if (!m) throw new TypeError("Invalid URL: " + s);
            this._opaque = true;
            this._protocol = m[1].toLowerCase();
            this.username = "";
            this.password = "";
            this._hostname = "";
            this._port = "";
            this._pathname = m[2];
            this._query = m[3] && m[3] !== "?" ? m[3].slice(1) : "";
            this._hash = m[4] && m[4] !== "#" ? m[4] : "";
        }
        this._sp = new URLSearchParams(this._query);
        this._sp._url = this;
    }
    get protocol() { return this._protocol; }
    set protocol(v) { v = String(v).replace(/:?$/, ":").toLowerCase(); if (/^[a-z][a-z0-9+.-]*:$/.test(v)) this._protocol = v; }
    get hostname() { return this._hostname; }
    set hostname(v) { if (!this._opaque) this._hostname = String(v).toLowerCase(); }
    get port() { return this._port; }
    set port(v) { if (!this._opaque) { v = String(v); this._port = v === __URL_PORTS[this._protocol] ? "" : v; } }
    get host() { return this._hostname + (this._port ? ":" + this._port : ""); }
    set host(v) {
        if (this._opaque) return;
        const m = /^([^:]*)(?::(\d*))?$/.exec(String(v));
        if (m) {
            this.hostname = m[1];
            this.port = m[2] ?? "";
        }
    }
    get pathname() { return this._pathname; }
    set pathname(v) { if (!this._opaque) this._pathname = __urlDots(String(v).startsWith("/") ? String(v) : "/" + v); }
    get search() { return this._query ? "?" + this._query : ""; }
    set search(v) {
        v = String(v).replace(/^\?/, "");
        this._query = v;
        this._sp._p = new URLSearchParams(v)._p;
    }
    get searchParams() { return this._sp; }
    get hash() { return this._hash; }
    set hash(v) { v = String(v).replace(/^#/, ""); this._hash = v ? "#" + v : ""; }
    get origin() { return this._opaque || this._protocol === "file:" ? "null" : this._protocol + "//" + this.host; }
    get href() {
        if (this._opaque) return this._protocol + this._pathname + this.search + this._hash;
        const auth = this.username ? this.username + (this.password ? ":" + this.password : "") + "@" : "";
        return this._protocol + "//" + auth + this.host + this._pathname + this.search + this._hash;
    }
    set href(v) { this._parse(String(v)); }
    toString() { return this.href; }
    toJSON() { return this.href; }
}
class URLSearchParams {
    constructor(init) {
        this._p = [];
        this._url = null;
        if (init instanceof URLSearchParams) {
            this._p = init._p.map(x => [x[0], x[1]]);
        } else if (Array.isArray(init)) {
            for (const pair of init) this._p.push([String(pair[0]), String(pair[1])]);
        } else if (typeof init === "string") {
            for (const part of init.replace(/^\?/, "").split("&")) {
                if (!part) continue;
                const i = part.indexOf("=");
                const k = i < 0 ? part : part.slice(0, i);
                const v = i < 0 ? "" : part.slice(i + 1);
                this._p.push([URLSearchParams._dec(k), URLSearchParams._dec(v)]);
            }
        } else if (init && typeof init === "object") {
            for (const k of Object.keys(init)) this._p.push([k, String(init[k])]);
        }
    }
    static _dec(s) {
        s = s.replace(/\+/g, " ");
        try {
            return decodeURIComponent(s);
        } catch (e) {
            return s;
        }
    }
    static _enc(s) {
        return encodeURIComponent(s).replace(/%20/g, "+").replace(/[!'()~]/g, c => "%" + c.charCodeAt(0).toString(16).toUpperCase());
    }
    _update() { if (this._url) this._url._query = this.toString(); }
    get size() { return this._p.length; }
    get(k) { const e = this._p.find(x => x[0] === k); return e ? e[1] : null; }
    getAll(k) { return this._p.filter(x => x[0] === k).map(x => x[1]); }
    has(k, v) { return this._p.some(x => x[0] === k && (v === undefined || x[1] === String(v))); }
    set(k, v) {
        const i = this._p.findIndex(x => x[0] === k);
        if (i < 0) this._p.push([k, String(v)]);
        else {
            this._p[i][1] = String(v);
            this._p = this._p.filter((x, j) => j <= i || x[0] !== k);
        }
        this._update();
    }
    append(k, v) { this._p.push([k, String(v)]); this._update(); }
    delete(k, v) { this._p = this._p.filter(x => !(x[0] === k && (v === undefined || x[1] === String(v)))); this._update(); }
    sort() { this._p.sort((a, b) => a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : 0); this._update(); }
    forEach(fn, self) { for (const [k, v] of this._p) fn.call(self, v, k, this); }
    keys() { return this._p.map(x => x[0])[Symbol.iterator](); }
    values() { return this._p.map(x => x[1])[Symbol.iterator](); }
    entries() { return this._p.map(x => [x[0], x[1]])[Symbol.iterator](); }
    [Symbol.iterator]() { return this.entries(); }
    toString() { return this._p.map(x => URLSearchParams._enc(x[0]) + "=" + URLSearchParams._enc(x[1])).join("&"); }
}
const __B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
function btoa(input) {
    const str = String(input);
    let out = "";
    for (let i = 0; i < str.length; i += 3) {
        const a = str.charCodeAt(i), b = str.charCodeAt(i + 1), c = str.charCodeAt(i + 2);
        if (a > 255 || b > 255 || c > 255) throw new Error("btoa: character out of range");
        const n = (a << 16) | ((b || 0) << 8) | (c || 0);
        out += __B64[(n >> 18) & 63] + __B64[(n >> 12) & 63]
            + (i + 1 < str.length ? __B64[(n >> 6) & 63] : "=")
            + (i + 2 < str.length ? __B64[n & 63] : "=");
    }
    return out;
}
function atob(input) {
    const str = String(input).replace(/[^A-Za-z0-9+/]/g, "");
    let out = "";
    for (let i = 0; i < str.length; i += 4) {
        const n = (__B64.indexOf(str[i]) << 18) | (__B64.indexOf(str[i + 1]) << 12)
            | ((__B64.indexOf(str[i + 2]) & 63) << 6) | (__B64.indexOf(str[i + 3]) & 63);
        out += String.fromCharCode((n >> 16) & 255);
        if (str[i + 2] !== undefined) out += String.fromCharCode((n >> 8) & 255);
        if (str[i + 3] !== undefined) out += String.fromCharCode(n & 255);
    }
    return out;
}

function log(msg) { __sanson.log(typeof msg === "string" ? msg : JSON.stringify(msg)); }
const console = { log: log, warn: log, error: log, info: log, debug: log, clear() {} };

const bridge = {
    buildPlatform: "android",
    authUserAgent: null,
    captchaUserAgent: null,
    buildVersion: 330,
    buildSpecVersion: 1,
    toast(msg) { __sanson.log("[toast] " + msg); },
    log(msg) { log(msg); },
    isLoggedIn() { return __sanson.loggedIn(); },
    hasPackage(name) { return false; },
    devSubmit() {},
    throwTest() {},
    sleep(ms) { __sanson.sleep(Number(ms) || 0); }
};

const utility = {
    md5String(s) { return __sanson.md5(String(s)); },
    toBase64(v) {
        if (typeof v === "string") return btoa(unescape(encodeURIComponent(v)));
        let bin = "";
        for (let i = 0; i < v.length; i++) bin += String.fromCharCode(v[i] & 255);
        return btoa(bin);
    },
    fromBase64(s) {
        const bin = atob(s);
        const out = new Uint8Array(bin.length);
        for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
        return out;
    },
    randomUUID() {
        const h = "0123456789abcdef";
        let s = "";
        for (let i = 0; i < 36; i++) s += (i === 8 || i === 13 || i === 18 || i === 23) ? "-" : i === 14 ? "4" : h[(Math.random() * 16) | 0];
        return s;
    }
};

const domParser = {
    parseFromString(html) { throw new ScriptException("DOMParser not available"); }
};

const __sansonTimers = [];
let __sansonTimerSeq = 0;
let __sansonClock = 0;
function setTimeout(fn, ms, ...args) {
    const id = ++__sansonTimerSeq;
    __sansonTimers.push({ id: id, at: __sansonClock + (Number(ms) || 0), fn: fn, args: args });
    return id;
}
function clearTimeout(id) {
    const i = __sansonTimers.findIndex(t => t.id === id);
    if (i >= 0) __sansonTimers.splice(i, 1);
}
function setInterval(fn, ms, ...args) { return setTimeout(fn, ms, ...args); }
const clearInterval = clearTimeout;
function __sansonTick() {
    if (!__sansonTimers.length) return false;
    let k = 0;
    for (let i = 1; i < __sansonTimers.length; i++) if (__sansonTimers[i].at < __sansonTimers[k].at) k = i;
    const t = __sansonTimers.splice(k, 1)[0];
    if (t.at > __sansonClock) __sansonClock = t.at;
    try {
        if (typeof t.fn === "function") t.fn(...t.args);
    } catch (e) {
        log("timer failed: " + e);
    }
    return true;
}
function __sansonClearTimers() { __sansonTimers.length = 0; }

function __sansonResponse(raw) {
    const r = raw;
    return { code: r.code, body: r.body, headers: r.headers ?? {}, url: r.url, isOk: r.code >= 200 && r.code < 300 };
}
function __sansonBody(body) {
    if (body == null) return null;
    if (typeof body === "string") return body;
    if (body instanceof ArrayBuffer) body = new Uint8Array(body);
    if (ArrayBuffer.isView(body)) {
        let bin = "";
        for (let i = 0; i < body.length; i++) bin += String.fromCharCode(body[i] & 255);
        return { binary: bin };
    }
    return String(body);
}
function __sansonClient(useAuth) {
    const client = {
        _headers: {},
        _send(method, url, body, headers, auth) {
            const h = Object.assign({}, client._headers, headers ?? {});
            return __sansonResponse(__sanson.http(method, String(url), __sansonBody(body), h, !!(auth ?? useAuth)));
        },
        GET(url, headers, auth) { return client._send("GET", url, null, headers, auth); },
        POST(url, body, headers, auth) { return client._send("POST", url, body, headers, auth); },
        request(method, url, headers, auth) { return client._send(method, url, null, headers, auth); },
        requestWithBody(method, url, body, headers, auth) { return client._send(method, url, body, headers, auth); },
        setDefaultHeaders(h) { client._headers = Object.assign({}, h ?? {}); },
        setDoApplyCookies() {},
        setDoUpdateCookies() {},
        setDoAllowNewCookies() {},
        setTimeout() {}
    };
    return client;
}
const __sansonDefault = __sansonClient(false);
const http = {
    GET(url, headers, useAuth) { return __sansonDefault.GET(url, headers, useAuth); },
    POST(url, body, headers, useAuth) { return __sansonDefault.POST(url, body, headers, useAuth); },
    request(method, url, headers, useAuth) { return __sansonDefault.request(method, url, headers, useAuth); },
    requestWithBody(method, url, body, headers, useAuth) { return __sansonDefault.requestWithBody(method, url, body, headers, useAuth); },
    getDefaultClient(useAuth) { return __sansonDefault; },
    newClient(useAuth) { return __sansonClient(useAuth); },
    socket(url, headers, useAuth) { return __sansonSocket(url, headers); },
    batch() {
        const reqs = [];
        const b = {
            GET(url, headers, useAuth) { reqs.push(() => http.GET(url, headers, useAuth)); return b; },
            POST(url, body, headers, useAuth) { reqs.push(() => http.POST(url, body, headers, useAuth)); return b; },
            request(method, url, headers, useAuth) { reqs.push(() => http.request(method, url, headers, useAuth)); return b; },
            requestWithBody(method, url, body, headers, useAuth) { reqs.push(() => http.requestWithBody(method, url, body, headers, useAuth)); return b; },
            DUMMY() { reqs.push(() => null); return b; },
            clientGET(c, url, headers) { reqs.push(() => c.GET(url, headers)); return b; },
            clientPOST(c, url, body, headers) { reqs.push(() => c.POST(url, body, headers)); return b; },
            clientRequest(c, method, url, headers) { reqs.push(() => c.request(method, url, headers)); return b; },
            execute() { return reqs.map(f => f()); }
        };
        return b;
    }
};

const __sansonSockets = {};
let __sansonNewSockets = null;
function __sansonSocket(url, headers) {
    const socket = {
        _id: 0,
        _listeners: {},
        isOpen: false,
        connect(listeners) {
            socket._listeners = listeners ?? {};
            socket._id = Number(__sanson.socket("open", String(url), JSON.stringify(headers ?? {})));
            __sansonSockets[socket._id] = socket;
            if (__sansonNewSockets) __sansonNewSockets.push(socket._id);
            socket.isOpen = true;
            if (typeof socket._listeners.open === "function") socket._listeners.open();
        },
        send(msg) {
            if (socket.isOpen) __sanson.socket("send", String(socket._id), String(msg));
        },
        close(code, reason) {
            if (!socket.isOpen) return;
            socket.isOpen = false;
            delete __sansonSockets[socket._id];
            __sanson.socket("close", String(socket._id), "");
            if (typeof socket._listeners.closed === "function") socket._listeners.closed(code ?? 1000, reason ?? "");
        }
    };
    return socket;
}
function __sansonPump(ids) {
    for (const id of ids ?? []) {
        const socket = __sansonSockets[id];
        if (!socket) continue;
        const polled = JSON.parse(__sanson.socket("poll", String(id), ""));
        for (const frame of polled.messages) {
            for (const line of frame.split("\r\n")) {
                if (!line || !socket.isOpen) continue;
                try {
                    if (typeof socket._listeners.message === "function") socket._listeners.message(line);
                } catch (e) {
                    log("socket message failed: " + e);
                }
            }
        }
        if (polled.closed && socket.isOpen) {
            socket.isOpen = false;
            delete __sansonSockets[id];
            if (typeof socket._listeners.closed === "function") socket._listeners.closed(1000, "");
        }
    }
}

const IS_TESTING = false;
const source = {};
const plugin = { config: {}, settings: {} };

const __sansonPagers = {};
const __sansonPagerSockets = {};
let __sansonPagerSeq = 0;

function __sansonKind(v) {
    if (v instanceof PlatformVideoDetails) return "PlatformVideoDetails";
    if (v instanceof PlatformVideo) return "PlatformVideo";
    if (v instanceof PlatformLockedContent) return "PlatformLockedContent";
    if (v instanceof PlatformChannel) return "PlatformChannel";
    if (v instanceof UnMuxVideoSourceDescriptor) return "UnMuxVideoSourceDescriptor";
    if (v instanceof VideoSourceDescriptor) return "VideoSourceDescriptor";
    if (v instanceof HLSSource) return "HLSSource";
    if (v instanceof DashSource) return "DashSource";
    if (v instanceof VideoUrlSource) return "VideoUrlSource";
    if (v instanceof AudioUrlSource) return "AudioUrlSource";
    if (v instanceof RawPlayerSource) return "RawPlayerSource";
    if (v instanceof Comment) return "Comment";
    if (v instanceof LiveEvent) return "LiveEvent";
    return null;
}
function __sansonPagerKind(p) {
    if (p instanceof VideoPager || p instanceof ContentPager) return "content";
    if (p instanceof ChannelPager) return "channel";
    if (p instanceof CommentPager) return "comment";
    if (p instanceof PlaylistPager) return "playlist";
    if (p instanceof LiveEventPager) return "liveEvent";
    return "unknown";
}
function __sansonSerialize(v) {
    return JSON.stringify(v, function (k, val) {
        if (val && typeof val === "object" && !Array.isArray(val)) {
            if (typeof val.requestModifier === "object" || typeof val.getRequestModifier === "function") {
                val = Object.assign({}, val, { requestModifier: undefined, getRequestModifier: undefined, __hasRequestModifier: true });
            }
            const kind = __sansonKind(val);
            if (kind && !val.__type) return Object.assign({ __type: kind }, val);
        }
        return val;
    });
}
function __sansonWrap(result) {
    if (result instanceof BasePager) {
        const id = ++__sansonPagerSeq;
        __sansonPagers[id] = result;
        const wrapped = { __pager: id, kind: __sansonPagerKind(result), hasMore: result.hasMorePagers(), results: result.results };
        if (result instanceof LiveEventPager) wrapped.nextRequest = result.nextRequest;
        return __sansonSerialize(wrapped);
    }
    return __sansonSerialize({ value: result === undefined ? null : result });
}
function __sansonError(e) {
    return JSON.stringify({ __error: true, type: e?.plugin_type ?? e?.name ?? "Error", message: String(e?.message ?? e), stack: String(e?.stack ?? "") });
}
const __sansonAsync = {};
let __sansonAsyncSeq = 0;
function __sansonAwait(result, finish) {
    if (!result || typeof result.then !== "function") return finish(result);
    const id = ++__sansonAsyncSeq;
    __sansonAsync[id] = null;
    result.then(v => {
        try {
            __sansonAsync[id] = finish(v);
        } catch (e) {
            __sansonAsync[id] = __sansonError(e);
        }
    }, e => { __sansonAsync[id] = __sansonError(e); });
    return JSON.stringify({ __async: id });
}
function __sansonAsyncResult(id) {
    const r = __sansonAsync[id];
    if (r === null || r === undefined) return null;
    delete __sansonAsync[id];
    return r;
}
function __sansonCall(name, argsJson) {
    __sansonNewSockets = [];
    try {
        const fn = source[name];
        if (typeof fn !== "function") return JSON.stringify({ __error: true, type: "NotImplemented", message: name });
        const result = fn.apply(source, JSON.parse(argsJson));
        if (result && typeof result.then === "function") return __sansonAwait(result, v => __sansonWrap(v));
        const wrapped = __sansonWrap(result);
        if (result instanceof BasePager && __sansonNewSockets.length) __sansonPagerSockets[__sansonPagerSeq] = __sansonNewSockets;
        return wrapped;
    } catch (e) {
        return __sansonError(e);
    } finally {
        __sansonNewSockets = null;
    }
}
function __sansonNext(id, arg) {
    try {
        const p = __sansonPagers[id];
        if (!p) return JSON.stringify({ __error: true, type: "PagerGone", message: String(id) });
        const sockets = __sansonPagerSockets[id];
        __sansonPump(sockets);
        delete __sansonPagers[id];
        delete __sansonPagerSockets[id];
        const next = p.nextPage(arg);
        if (next && typeof next.then === "function") return __sansonAwait(next, v => __sansonWrap(v));
        const wrapped = __sansonWrap(next);
        if (sockets && next instanceof BasePager) __sansonPagerSockets[__sansonPagerSeq] = sockets;
        return wrapped;
    } catch (e) {
        return __sansonError(e);
    }
}
function __sansonPagerCall(id, name, argsJson) {
    try {
        const p = __sansonPagers[id];
        if (!p || typeof p[name] !== "function") return JSON.stringify({ missing: true });
        return __sansonSerialize({ value: p[name].apply(p, JSON.parse(argsJson)) });
    } catch (e) {
        return __sansonError(e);
    }
}
function __sansonDrop(id) {
    for (const socketId of __sansonPagerSockets[id] ?? []) {
        const socket = __sansonSockets[socketId];
        if (socket) socket.close();
    }
    delete __sansonPagers[id];
    delete __sansonPagerSockets[id];
    return JSON.stringify({ value: true });
}
function __sansonEnable(configJson, settingsJson, saveState) {
    try {
        plugin.config = JSON.parse(configJson);
        plugin.settings = JSON.parse(settingsJson);
        if (typeof source.enable === "function") {
            const r = source.enable(plugin.config, plugin.settings, saveState ?? null);
            if (r && typeof r.then === "function") return __sansonAwait(r, () => JSON.stringify({ value: true }));
        }
        return JSON.stringify({ value: true });
    } catch (e) {
        return __sansonError(e);
    }
}

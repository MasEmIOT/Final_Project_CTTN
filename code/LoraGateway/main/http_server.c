#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "http_server.h"
#include "node_store.h"
#include "packet.h"
#include "crypto.h"
#include "crypto_cfg.h"
#include "gw_config.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_system.h"

static const char *TAG = "httpd";
static httpd_handle_t s_server = NULL;
static char s_ip[20] = "0.0.0.0";
static int64_t s_boot_us;

void http_server_set_ip(const char *ip) { strncpy(s_ip, ip, sizeof(s_ip) - 1); }
const char *http_server_get_ip(void) { return s_ip; }

/* ---- JSON helpers cho POST body ---- */
static bool jget_str(const char *j, const char *key, char *out, size_t n)
{
    char pat[40];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(j, pat);
    if (!p) return false;
    p += strlen(pat);
    while (*p == ' ' || *p == ':') p++;
    if (*p != '"') return false;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i < n - 1) out[i++] = *p++;
    out[i] = '\0';
    return true;
}
static int jget_int(const char *j, const char *key, int def)
{
    char pat[40];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(j, pat);
    if (!p) return def;
    p += strlen(pat);
    while (*p == ' ' || *p == ':' || *p == '"') p++;
    return (*p >= '0' && *p <= '9') ? atoi(p) : def;
}

/* ================= Dashboard nhung ================= */
static const char DASH_HTML[] =
"<!doctype html><html lang=vi><head><meta charset=utf-8>"
"<meta name=viewport content='width=device-width,initial-scale=1'>"
"<title>LoRa Farm Gateway</title><style>"
"*{box-sizing:border-box}body{margin:0;font-family:system-ui,Segoe UI,Roboto,sans-serif;"
"background:#0f1419;color:#e6edf3}header{padding:14px 18px;background:#161b22;display:flex;"
"align-items:center;gap:12px;border-bottom:1px solid #222}h1{font-size:18px;margin:0;flex:1}"
".pill{font-size:12px;padding:3px 9px;border-radius:20px;background:#233}"
".wrap{padding:16px;display:grid;grid-template-columns:repeat(auto-fill,minmax(280px,1fr));gap:14px}"
".card{background:#161b22;border:1px solid #222;border-radius:12px;padding:14px}"
".card h2{margin:0 0 8px;font-size:15px;display:flex;justify-content:space-between}"
".g{display:grid;grid-template-columns:1fr 1fr;gap:6px 10px;font-size:13px}"
".g b{color:#7ee787}.SAFE{color:#3fb950}.WARN{color:#d29922}.EMERGENCY{color:#f85149}"
".otaok{color:#3fb950;font-size:12px;margin:6px 0;font-weight:600}"
".otarun{color:#d29922;font-size:12px;margin:6px 0;font-weight:600}"
".act button{margin:3px 3px 0 0;padding:5px 9px;border:0;border-radius:6px;cursor:pointer;"
"background:#21262d;color:#e6edf3}.on{background:#238636}.off{background:#30363d}"
".act button:disabled{opacity:.4;cursor:not-allowed}"
"#login input{padding:6px;margin-right:6px;border-radius:6px;border:1px solid #333;background:#0d1117;color:#eee}"
"button.p{background:#1f6feb;color:#fff}small{color:#8b949e}</style></head><body>"
"<header><h1>Chicken Farm — LoRa Gateway</h1>"
"<span class=pill id=st>...</span><span id=login>"
"<input id=u placeholder=user><input id=p type=password placeholder=pass>"
"<button class=p onclick=login()>Dang nhap</button></span></header>"
"<div class=wrap id=nodes></div>"
"<script>let role='',tok='';"
"function login(){fetch('/api/login',{method:'POST',body:JSON.stringify({user:u.value,pass:p.value})})"
".then(r=>r.json()).then(j=>{role=j.role||'';tok=(role=='Admin')?p.value:'';"
"login.innerHTML='<small>Vai tro: '+(role||'sai mat khau')+'</small>';})}"
"function cmd(n,c,m,v,url){fetch('/api/cmd',{method:'POST',headers:{'X-Token':tok},"
"body:JSON.stringify({node:n,cmd:c,act_mask:m,act_val:v,url:url||''})})"
".then(r=>r.json()).then(j=>alert(j.ok?'Da gui lenh':'Loi: '+(j.err||'?')))}"
"function actBtn(n,bit,label,st,manual){let on=(st&bit)!=0;"
"return `<button ${manual?'':'disabled'} class=${on?'on':'off'} onclick=\"cmd(${n},1,${bit},${on?0:bit})\">${label}:${on?'ON':'OFF'}</button>`}"
"function ep(s){return s?new Date(s*1000).toLocaleString('vi-VN'):'chua cap nhat'}"
"function otaStat(d){if(!d.ota_cmd_epoch)return '';"
"if(d.fw_ver>d.ota_from)return `<div class=otaok>&#10003; OTA thanh cong: v${d.ota_from} &#8594; v${d.fw_ver} (luc ${ep(d.ota_epoch)})</div>`;"
"if(!d.online)return '<div class=otarun>&#8635; OTA dang chay... (node offline de tai firmware)</div>';"
"return '<div class=otarun>&#8635; Da gui lenh OTA, cho node cap nhat...</div>'}"
"function draw(list){let h='';list.forEach(d=>{let manual=d.act_mode=='manual';h+=`<div class=card><h2>Node ${d.node} "
"<span class=${d.fsm}>${d.online?d.fsm:'OFFLINE'}</span></h2>${otaStat(d)}<div class=g>"
"<span>Nhiet do</span><b>${d.temp}&#8451;</b><span>Do am</span><b>${d.hum}%</b>"
"<span>Ap suat</span><b>${d.press}hPa</b><span>Anh sang</span><b>${d.lux}lx</b>"
"<span>NH3</span><b>${d.nh3}ppm</b><span>CO2</span><b>${d.co2}ppm</b>"
"<span>THI</span><b>${d.thi}</b><span>RSSI</span><b>${d.rssi}dBm</b>"
"<span>RTT</span><b>${d.rtt}ms</b><span>Decide</span><b>${d.decide_us}us</b>"
"<span>PDR</span><b>${Math.round(100*d.ack/(d.tx||1))}%</b><span>Buffer</span><b>${d.buf}</b>"
"<span>Che do</span><b class=${manual?'WARN':'SAFE'}>${manual?'Manual: Active '+d.manual_left+'s':'Auto: Active'}</b>"
"<span>Firmware</span><b>v${d.fw_ver}</b></div>"
"<div class=act>"
"${manual?`<button class=on onclick=\"cmd(${d.node},2,0,0)\">Bat Auto</button>`:`<button onclick=\"cmd(${d.node},1,0,0)\">Tat Auto (Manual 60s)</button>`}"
"${actBtn(d.node,1,'Quat',d.act,manual)}${actBtn(d.node,2,'Suong',d.act,manual)}${actBtn(d.node,4,'Act3',d.act,manual)}"
"<button onclick=\"let u=prompt('URL firmware .bin');if(u)cmd(${d.node},3,0,0,u)\">OTA</button>"
"</div><small>MAC ${d.mac} · algo ${d.algo} · seq ${d.seq} · FW v${d.fw_ver} · OTA: ${ep(d.ota_epoch)}</small></div>`});nodes.innerHTML=h}"
"function tick(){fetch('/api/nodes').then(r=>r.json()).then(draw);"
"fetch('/api/status').then(r=>r.json()).then(s=>st.textContent="
"'IP '+s.ip+' · Node '+s.online+'/'+s.total+' · Heap '+Math.round(s.heap/1024)+'KB · up '+s.uptime+'s')}"
"setInterval(tick,2000);tick();</script></body></html>";

static esp_err_t h_root(httpd_req_t *r)
{
    httpd_resp_set_type(r, "text/html");
    return httpd_resp_send(r, DASH_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t h_status(httpd_req_t *r)
{
    char buf[220];
    int total = node_store_total();
    int online = node_store_online_count(15000);
    snprintf(buf, sizeof(buf),
        "{\"ip\":\"%s\",\"heap\":%u,\"uptime\":%lld,\"online\":%d,\"total\":%d,\"algo\":\"%s\"}",
        s_ip, (unsigned)esp_get_free_heap_size(),
        (long long)((esp_timer_get_time() - s_boot_us) / 1000000),
        online, total, crypto_algo_name(CRYPTO_ALGO));
    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(r, buf, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t h_nodes(httpd_req_t *r)
{
    static char buf[4096];
    node_store_json_list(buf, sizeof(buf), 15000);
    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(r, buf, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t h_history(httpd_req_t *r)
{
    char q[32] = {0}; int node = 1;
    if (httpd_req_get_url_query_str(r, q, sizeof(q)) == ESP_OK) {
        char v[8]; if (httpd_query_key_value(q, "node", v, sizeof(v)) == ESP_OK) node = atoi(v);
    }
    static char buf[4096];
    node_store_json_history((uint8_t)node, buf, sizeof(buf));
    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(r, buf, HTTPD_RESP_USE_STRLEN);
}

static int read_body(httpd_req_t *r, char *buf, size_t n)
{
    int total = r->content_len; if (total >= (int)n) total = n - 1;
    int got = 0;
    while (got < total) {
        int k = httpd_req_recv(r, buf + got, total - got);
        if (k <= 0) return -1;
        got += k;
    }
    buf[got] = '\0';
    return got;
}

/* CORS: cho phep web app khac origin goi API (preflight OPTIONS + header X-Token) */
static void set_cors(httpd_req_t *r)
{
    httpd_resp_set_hdr(r, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(r, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    httpd_resp_set_hdr(r, "Access-Control-Allow-Headers", "Content-Type, X-Token");
}

static esp_err_t h_options(httpd_req_t *r)
{
    set_cors(r);
    httpd_resp_set_status(r, "204 No Content");
    return httpd_resp_send(r, NULL, 0);
}

static esp_err_t h_login(httpd_req_t *r)
{
    char body[128]; if (read_body(r, body, sizeof(body)) < 0) return ESP_FAIL;
    char u[32] = {0}, p[32] = {0};
    jget_str(body, "user", u, sizeof(u));
    jget_str(body, "pass", p, sizeof(p));
    const char *role = "none";
    if (!strcmp(u, GW_ADMIN_USER) && !strcmp(p, GW_ADMIN_PASS)) role = "Admin";
    else if (!strcmp(u, GW_USER_USER) && !strcmp(p, GW_USER_PASS)) role = "User";
    char out[48]; snprintf(out, sizeof(out), "{\"role\":\"%s\"}", role);
    httpd_resp_set_type(r, "application/json");
    set_cors(r);
    return httpd_resp_send(r, out, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t h_cmd(httpd_req_t *r)
{
    /* Yeu cau quyen Admin: header X-Token phai bang admin pass */
    char tok[40] = {0};
    httpd_req_get_hdr_value_str(r, "X-Token", tok, sizeof(tok));
    httpd_resp_set_type(r, "application/json");
    set_cors(r);
    if (strcmp(tok, GW_ADMIN_PASS) != 0) {
        httpd_resp_set_status(r, "403 Forbidden");
        return httpd_resp_send(r, "{\"ok\":false,\"err\":\"need admin\"}", HTTPD_RESP_USE_STRLEN);
    }
    char body[160]; if (read_body(r, body, sizeof(body)) < 0) return ESP_FAIL;
    int node = jget_int(body, "node", 0);
    int cmd  = jget_int(body, "cmd", 0);
    int mask = jget_int(body, "act_mask", 0);
    int val  = jget_int(body, "act_val", 0);
    char url[CMD_OTA_URL_MAX] = {0};
    jget_str(body, "url", url, sizeof(url));

    bool ok = node_store_queue_cmd((uint8_t)node, (uint8_t)cmd,
                                   (uint8_t)mask, (uint8_t)val, url[0] ? url : NULL);
    const char *msg = ok ? "{\"ok\":true}" : "{\"ok\":false,\"err\":\"queue full\"}";
    ESP_LOGI(TAG, "CMD tu App: node=%d cmd=%d mask=%d val=%d url=%s -> %s",
             node, cmd, mask, val, url, ok ? "queued" : "fail");
    return httpd_resp_send(r, msg, HTTPD_RESP_USE_STRLEN);
}

esp_err_t http_server_start(void)
{
    s_boot_us = esp_timer_get_time();
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = GW_HTTP_PORT;
    cfg.max_uri_handlers = 14;
    cfg.lru_purge_enable = true;
    cfg.stack_size = 8192;
    cfg.uri_match_fn = httpd_uri_match_wildcard;   /* bat OPTIONS preflight cho moi /api/ endpoint */
    if (httpd_start(&s_server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Khong khoi dong duoc HTTP server");
        return ESP_FAIL;
    }
    httpd_uri_t uris[] = {
        { .uri="/",            .method=HTTP_GET,     .handler=h_root },
        { .uri="/api/status",  .method=HTTP_GET,     .handler=h_status },
        { .uri="/api/nodes",   .method=HTTP_GET,     .handler=h_nodes },
        { .uri="/api/history", .method=HTTP_GET,     .handler=h_history },
        { .uri="/api/login",   .method=HTTP_POST,    .handler=h_login },
        { .uri="/api/cmd",     .method=HTTP_POST,    .handler=h_cmd },
        { .uri="/api/*",       .method=HTTP_OPTIONS, .handler=h_options },
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++)
        httpd_register_uri_handler(s_server, &uris[i]);

    ESP_LOGI(TAG, "HTTP server chay tai http://%s:%d/", s_ip, GW_HTTP_PORT);
    return ESP_OK;
}

#include "h/web_bridge.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <string>
#include <vector>

namespace {

static int g_http_listen_fd = -1;
static int g_mud_port = 4100;

struct WebClient {
  int fd;
  int mud_fd;
  bool is_ws;
  bool closed;
  std::string http_in;
  std::vector<uint8_t> ws_in;
  std::vector<uint8_t> ws_out;
  std::string mud_out;
  int iac_state;
  uint8_t iac_cmd;
  bool iac_sb;
};

static std::vector<WebClient> g_clients;

static void set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags >= 0) {
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  }
}

/* --- Minimal RFC 3174 SHA-1 for RFC 6455 WebSocket handshake --- */
static uint32_t rol32(uint32_t v, int bits) {
  return (v << bits) | (v >> (32 - bits));
}

static void sha1_digest(const uint8_t *data, size_t len, uint8_t out[20]) {
  uint32_t h0 = 0x67452301;
  uint32_t h1 = 0xEFCDAB89;
  uint32_t h2 = 0x98BADCFE;
  uint32_t h3 = 0x10325476;
  uint32_t h4 = 0xC3D2E1F0;

  std::vector<uint8_t> msg(data, data + len);
  msg.push_back(0x80);
  while ((msg.size() % 64) != 56) {
    msg.push_back(0x00);
  }
  uint64_t bit_len = static_cast<uint64_t>(len) * 8ULL;
  for (int i = 7; i >= 0; --i) {
    msg.push_back(static_cast<uint8_t>((bit_len >> (i * 8)) & 0xFF));
  }

  for (size_t offset = 0; offset < msg.size(); offset += 64) {
    uint32_t w[80];
    for (int i = 0; i < 16; ++i) {
      size_t idx = offset + i * 4;
      w[i] = (static_cast<uint32_t>(msg[idx]) << 24) |
             (static_cast<uint32_t>(msg[idx + 1]) << 16) |
             (static_cast<uint32_t>(msg[idx + 2]) << 8) |
             static_cast<uint32_t>(msg[idx + 3]);
    }
    for (int i = 16; i < 80; ++i) {
      w[i] = rol32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }

    uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
    for (int i = 0; i < 80; ++i) {
      uint32_t f = 0, k = 0;
      if (i < 20) {
        f = (b & c) | ((~b) & d);
        k = 0x5A827999;
      } else if (i < 40) {
        f = b ^ c ^ d;
        k = 0x6ED9EBA1;
      } else if (i < 60) {
        f = (b & c) | (b & d) | (c & d);
        k = 0x8F1BBCDC;
      } else {
        f = b ^ c ^ d;
        k = 0xCA62C1D6;
      }
      uint32_t temp = rol32(a, 5) + f + e + k + w[i];
      e = d;
      d = c;
      c = rol32(b, 30);
      b = a;
      a = temp;
    }

    h0 += a;
    h1 += b;
    h2 += c;
    h3 += d;
    h4 += e;
  }

  uint32_t hs[5] = {h0, h1, h2, h3, h4};
  for (int i = 0; i < 5; ++i) {
    out[i * 4 + 0] = static_cast<uint8_t>((hs[i] >> 24) & 0xFF);
    out[i * 4 + 1] = static_cast<uint8_t>((hs[i] >> 16) & 0xFF);
    out[i * 4 + 2] = static_cast<uint8_t>((hs[i] >> 8) & 0xFF);
    out[i * 4 + 3] = static_cast<uint8_t>(hs[i] & 0xFF);
  }
}

static std::string base64_encode(const uint8_t *in, size_t len) {
  static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve(((len + 2) / 3) * 4);
  for (size_t i = 0; i < len; i += 3) {
    uint32_t b = static_cast<uint32_t>(in[i]) << 16;
    if (i + 1 < len) b |= static_cast<uint32_t>(in[i + 1]) << 8;
    if (i + 2 < len) b |= static_cast<uint32_t>(in[i + 2]);
    out.push_back(tbl[(b >> 18) & 0x3F]);
    out.push_back(tbl[(b >> 12) & 0x3F]);
    out.push_back((i + 1 < len) ? tbl[(b >> 6) & 0x3F] : '=');
    out.push_back((i + 2 < len) ? tbl[b & 0x3F] : '=');
  }
  return out;
}

static std::string extract_header(const std::string &req, const std::string &name) {
  std::string lower_req = req;
  std::string lower_name = name;
  for (size_t i = 0; i < lower_req.size(); ++i) {
    if (lower_req[i] >= 'A' && lower_req[i] <= 'Z') lower_req[i] += 32;
  }
  for (size_t i = 0; i < lower_name.size(); ++i) {
    if (lower_name[i] >= 'A' && lower_name[i] <= 'Z') lower_name[i] += 32;
  }
  std::string needle = "\n" + lower_name + ":";
  size_t pos = lower_req.find(needle);
  if (pos == std::string::npos) return "";
  pos += needle.size();
  while (pos < req.size() && (req[pos] == ' ' || req[pos] == '\t')) pos++;
  size_t end = req.find('\n', pos);
  if (end == std::string::npos) end = req.size();
  while (end > pos && (req[end - 1] == '\r' || req[end - 1] == ' ')) end--;
  return req.substr(pos, end - pos);
}

static void queue_ws_frame(WebClient &c, uint8_t opcode, const uint8_t *payload, size_t len) {
  c.ws_out.push_back(static_cast<uint8_t>(0x80 | (opcode & 0x0F)));
  if (len < 126) {
    c.ws_out.push_back(static_cast<uint8_t>(len));
  } else if (len <= 65535) {
    c.ws_out.push_back(126);
    c.ws_out.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
    c.ws_out.push_back(static_cast<uint8_t>(len & 0xFF));
  } else {
    c.ws_out.push_back(127);
    uint64_t l64 = static_cast<uint64_t>(len);
    for (int i = 7; i >= 0; --i) {
      c.ws_out.push_back(static_cast<uint8_t>((l64 >> (i * 8)) & 0xFF));
    }
  }
  c.ws_out.insert(c.ws_out.end(), payload, payload + len);
}

static const char *get_html_client() {
  return R"HTML(<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0" />
  <title>6Dragons Reborn — C/C++ MUD Terminal</title>
  <style>
    * { box-sizing: border-box; }
    html, body { margin: 0; padding: 0; height: 100%; background: #090b0f; color: #d4d4d8; font-family: ui-sans-serif, system-ui, -apple-system, sans-serif; }
    .app { display: flex; flex-direction: column; height: 100dvh; }
    header { display: flex; align-items: center; justify-content: space-between; padding: 10px 16px; background: #0d1017; border-bottom: 1px solid #27272a; }
    .title { font-size: 16px; font-weight: 700; color: #fde68a; letter-spacing: 0.4px; }
    .sub { font-size: 11px; color: #a1a1aa; }
    .badge { font-size: 11px; padding: 3px 10px; border-radius: 999px; font-weight: 600; }
    .badge.ok { background: #052e16; color: #86efac; border: 1px solid #166534; }
    .badge.off { background: #450a0a; color: #fca5a5; border: 1px solid #991b1b; }
    #term { flex: 1; overflow-y: auto; padding: 14px 18px; background: #07090d; font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace; font-size: 13px; line-height: 1.48; white-space: pre-wrap; word-break: break-word; }
    .bar { display: flex; flex-wrap: wrap; gap: 6px; padding: 7px 14px; background: #0b0e14; border-top: 1px solid #1f2937; }
    .btn { background: #18181b; color: #d4d4d8; border: 1px solid #27272a; border-radius: 5px; padding: 4px 9px; font-size: 11px; cursor: pointer; font-family: ui-monospace, monospace; }
    .btn:hover { border-color: #d97706; color: #fde68a; }
    form { display: flex; align-items: center; gap: 8px; padding: 10px 14px; background: #0d1017; border-top: 1px solid #27272a; }
    .prompt { color: #f59e0b; font-family: ui-monospace, monospace; font-weight: 700; }
    #cmd { flex: 1; background: transparent; border: none; outline: none; color: #f4f4f5; font-family: ui-monospace, monospace; font-size: 14px; }
    .send { background: #d97706; color: #090b0f; border: none; border-radius: 6px; padding: 7px 14px; font-size: 12px; font-weight: 700; cursor: pointer; }
    .send:hover { background: #f59e0b; }
  </style>
</head>
<body>
  <div class="app">
    <header>
      <div>
        <div class="title">6Dragons Reborn (Native C/C++ Build)</div>
        <div class="sub">Direct WebSocket Bridge to Compiled SMAUG MUD Server (Port 4100)</div>
      </div>
      <div style="display:flex;align-items:center;gap:8px;">
        <button class="btn" onclick="reconnect()" type="button">Reconnect</button>
        <span id="status" class="badge off">Connecting...</span>
      </div>
    </header>
    <div id="term" onclick="document.getElementById('cmd').focus()"></div>
    <div class="bar">
      <button class="btn" onclick="sendCmd('look')">Look</button>
      <button class="btn" onclick="sendCmd('north')">N</button>
      <button class="btn" onclick="sendCmd('south')">S</button>
      <button class="btn" onclick="sendCmd('east')">E</button>
      <button class="btn" onclick="sendCmd('west')">W</button>
      <button class="btn" onclick="sendCmd('up')">Up</button>
      <button class="btn" onclick="sendCmd('down')">Down</button>
      <button class="btn" onclick="sendCmd('exits')">Exits</button>
      <button class="btn" onclick="sendCmd('score')">Score</button>
      <button class="btn" onclick="sendCmd('inventory')">Inv</button>
      <button class="btn" onclick="sendCmd('equipment')">Eq</button>
      <button class="btn" onclick="sendCmd('who')">Who</button>
      <button class="btn" onclick="sendCmd('areas')">Areas</button>
      <button class="btn" onclick="sendCmd('recall')">Recall</button>
      <button class="btn" onclick="sendCmd('help')">Help</button>
      <button class="btn" onclick="sendCmd('commands')">Commands</button>
    </div>
    <form id="form">
      <span class="prompt">&gt;</span>
      <input id="cmd" type="text" autocomplete="off" autofocus placeholder="Enter name (e.g. Admin / 12345 or new hero) or command..." />
      <button class="send" type="submit">Send</button>
    </form>
  </div>
  <script>
    const term = document.getElementById('term');
    const cmdEl = document.getElementById('cmd');
    const statusEl = document.getElementById('status');
    const history = [];
    let histIdx = -1;
    let ws = null;

    const FG = {
      30: '#27272a', 31: '#dc2626', 32: '#16a34a', 33: '#d97706',
      34: '#2563eb', 35: '#9333ea', 36: '#0891b2', 37: '#d4d4d8',
      90: '#71717a', 91: '#f87171', 92: '#4ade80', 93: '#facc15',
      94: '#60a5fa', 95: '#f472b6', 96: '#22d3ee', 97: '#ffffff'
    };
    const BRIGHT = {
      30: '#71717a', 31: '#f87171', 32: '#4ade80', 33: '#facc15',
      34: '#60a5fa', 35: '#f472b6', 36: '#22d3ee', 37: '#ffffff'
    };

    let curColor = '#d4d4d8';
    let curBold = false;
    let curCode = 37;

    function appendAnsi(chunk) {
      const re = /\x1b\[([0-9;]*)m/g;
      let last = 0;
      let m;
      const frag = document.createDocumentFragment();
      while ((m = re.exec(chunk)) !== null) {
        if (m.index > last) {
          const sp = document.createElement('span');
          sp.style.color = curColor;
          if (curBold) sp.style.fontWeight = '600';
          sp.textContent = chunk.slice(last, m.index);
          frag.appendChild(sp);
        }
        const codes = (m[1] || '0').split(';').map(n => parseInt(n || '0', 10));
        for (const c of codes) {
          if (c === 0) {
            curBold = false;
            curCode = 37;
            curColor = '#d4d4d8';
          } else if (c === 1) {
            curBold = true;
            if (BRIGHT[curCode]) curColor = BRIGHT[curCode];
          } else if (c >= 30 && c <= 37) {
            curCode = c;
            curColor = curBold ? (BRIGHT[c] || FG[c]) : FG[c];
          } else if (c >= 90 && c <= 97) {
            curCode = c;
            curColor = FG[c];
          }
        }
        last = re.lastIndex;
      }
      if (last < chunk.length) {
        const sp = document.createElement('span');
        sp.style.color = curColor;
        if (curBold) sp.style.fontWeight = '600';
        sp.textContent = chunk.slice(last);
        frag.appendChild(sp);
      }
      term.appendChild(frag);
      while (term.childNodes.length > 3500) {
        term.removeChild(term.firstChild);
      }
      term.scrollTop = term.scrollHeight;
    }

    function sendCmd(text) {
      if (!ws || ws.readyState !== 1) return;
      if (text !== '') {
        appendAnsi('\x1b[90m> ' + text + '\x1b[0m\n');
      }
      ws.send(text + '\r\n');
      cmdEl.focus();
    }

    function connect() {
      const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
      ws = new WebSocket(proto + '//' + location.host + '/ws');
      ws.onopen = () => {
        statusEl.textContent = 'Connected';
        statusEl.className = 'badge ok';
      };
      ws.onmessage = (ev) => {
        appendAnsi(ev.data.replace(/\r/g, ''));
      };
      ws.onclose = () => {
        statusEl.textContent = 'Disconnected';
        statusEl.className = 'badge off';
        appendAnsi('\n\x1b[31m[Connection closed]\x1b[0m\n');
      };
    }

    function reconnect() {
      if (ws) ws.close();
      connect();
    }

    document.getElementById('form').addEventListener('submit', (e) => {
      e.preventDefault();
      const v = cmdEl.value;
      if (v.trim()) {
        history.unshift(v);
        if (history.length > 50) history.pop();
      }
      histIdx = -1;
      cmdEl.value = '';
      sendCmd(v);
    });

    cmdEl.addEventListener('keydown', (e) => {
      if (e.key === 'ArrowUp') {
        e.preventDefault();
        if (histIdx + 1 < history.length) {
          histIdx++;
          cmdEl.value = history[histIdx];
        }
      } else if (e.key === 'ArrowDown') {
        e.preventDefault();
        if (histIdx > 0) {
          histIdx--;
          cmdEl.value = history[histIdx];
        } else {
          histIdx = -1;
          cmdEl.value = '';
        }
      }
    });

    connect();
  </script>
</body>
</html>)HTML";
}

static void send_http_response(int fd, const char *status, const char *ctype, const std::string &body) {
  char hdr[512];
  int hlen = snprintf(
      hdr, sizeof(hdr),
      "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\nCache-Control: no-store\r\n\r\n",
      status, ctype, body.size());
  if (hlen > 0) {
    send(fd, hdr, static_cast<size_t>(hlen), MSG_NOSIGNAL);
  }
  size_t sent = 0;
  while (sent < body.size()) {
    ssize_t n = send(fd, body.data() + sent, body.size() - sent, MSG_NOSIGNAL);
    if (n > 0) {
      sent += static_cast<size_t>(n);
    } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      usleep(1000);
    } else {
      break;
    }
  }
}

static int connect_local_mud(int mud_port) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return -1;
  set_nonblocking(fd);
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(mud_port));
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  int rc = connect(fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr));
  if (rc < 0 && errno != EINPROGRESS) {
    close(fd);
    return -1;
  }
  return fd;
}

static void close_client(WebClient &c) {
  if (c.fd >= 0) {
    close(c.fd);
    c.fd = -1;
  }
  if (c.mud_fd >= 0) {
    close(c.mud_fd);
    c.mud_fd = -1;
  }
  c.closed = true;
}

static void handle_http_request(WebClient &c) {
  size_t hdr_end = c.http_in.find("\r\n\r\n");
  if (hdr_end == std::string::npos) return;

  std::string header_block = c.http_in.substr(0, hdr_end);
  size_t line_end = header_block.find("\r\n");
  std::string req_line = (line_end == std::string::npos) ? header_block : header_block.substr(0, line_end);

  std::string upgrade = extract_header(header_block, "Upgrade");
  for (size_t i = 0; i < upgrade.size(); ++i) {
    if (upgrade[i] >= 'A' && upgrade[i] <= 'Z') upgrade[i] += 32;
  }

  if (upgrade == "websocket") {
    std::string ws_key = extract_header(header_block, "Sec-WebSocket-Key");
    if (ws_key.empty()) {
      send_http_response(c.fd, "400 Bad Request", "text/plain", "Missing Sec-WebSocket-Key");
      close_client(c);
      return;
    }
    std::string concat = ws_key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    uint8_t digest[20];
    sha1_digest(reinterpret_cast<const uint8_t *>(concat.data()), concat.size(), digest);
    std::string accept_key = base64_encode(digest, 20);

    std::string resp =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " +
        accept_key + "\r\n\r\n";
    send(c.fd, resp.data(), resp.size(), MSG_NOSIGNAL);

    c.is_ws = true;
    c.http_in.erase(0, hdr_end + 4);
    if (!c.http_in.empty()) {
      c.ws_in.insert(c.ws_in.end(), c.http_in.begin(), c.http_in.end());
      c.http_in.clear();
    }
    c.mud_fd = connect_local_mud(g_mud_port);
    if (c.mud_fd < 0) {
      const char *err_msg = "\r\n[Failed to connect to local 6Dragons MUD socket]\r\n";
      queue_ws_frame(c, 0x01, reinterpret_cast<const uint8_t *>(err_msg), strlen(err_msg));
    }
    return;
  }

  if (req_line.find("GET /health") == 0) {
    send_http_response(c.fd, "200 OK", "application/json", "{\"ok\":true,\"mud\":\"6Dragons Reborn\"}");
  } else {
    send_http_response(c.fd, "200 OK", "text/html; charset=utf-8", get_html_client());
  }
  close_client(c);
}

static void process_ws_frames(WebClient &c) {
  while (c.ws_in.size() >= 2) {
    uint8_t b0 = c.ws_in[0];
    uint8_t b1 = c.ws_in[1];
    uint8_t opcode = b0 & 0x0F;
    bool masked = (b1 & 0x80) != 0;
    uint64_t plen = b1 & 0x7F;
    size_t pos = 2;

    if (plen == 126) {
      if (c.ws_in.size() < pos + 2) return;
      plen = (static_cast<uint64_t>(c.ws_in[pos]) << 8) | c.ws_in[pos + 1];
      pos += 2;
    } else if (plen == 127) {
      if (c.ws_in.size() < pos + 8) return;
      plen = 0;
      for (int i = 0; i < 8; ++i) {
        plen = (plen << 8) | c.ws_in[pos + i];
      }
      pos += 8;
    }

    uint8_t mask[4] = {0, 0, 0, 0};
    if (masked) {
      if (c.ws_in.size() < pos + 4) return;
      memcpy(mask, &c.ws_in[pos], 4);
      pos += 4;
    }

    if (plen > 65536 || c.ws_in.size() < pos + plen) return;

    std::vector<uint8_t> payload(plen);
    for (size_t i = 0; i < plen; ++i) {
      payload[i] = masked ? (c.ws_in[pos + i] ^ mask[i % 4]) : c.ws_in[pos + i];
    }
    c.ws_in.erase(c.ws_in.begin(), c.ws_in.begin() + pos + static_cast<size_t>(plen));

    if (opcode == 0x08) {
      queue_ws_frame(c, 0x08, NULL, 0);
      close_client(c);
      return;
    } else if (opcode == 0x09) {
      queue_ws_frame(c, 0x0A, payload.data(), payload.size());
    } else if (opcode == 0x01 || opcode == 0x02 || opcode == 0x00) {
      c.mud_out.append(reinterpret_cast<const char *>(payload.data()), payload.size());
    }
  }
}

static void filter_telnet_to_ws(WebClient &c, const uint8_t *buf, size_t len) {
  std::string clean;
  clean.reserve(len);
  for (size_t i = 0; i < len; ++i) {
    uint8_t ch = buf[i];
    if (c.iac_sb) {
      if (c.iac_state == 1 && ch == 240) {  // IAC SE
        c.iac_sb = false;
        c.iac_state = 0;
      } else {
        c.iac_state = (ch == 255) ? 1 : 0;
      }
      continue;
    }
    if (c.iac_state == 0) {
      if (ch == 255) {  // IAC
        c.iac_state = 1;
      } else if (ch != '\0') {
        clean.push_back(static_cast<char>(ch));
      }
    } else if (c.iac_state == 1) {
      if (ch == 255) {
        clean.push_back(static_cast<char>(255));
        c.iac_state = 0;
      } else if (ch == 250) {  // SB
        c.iac_sb = true;
        c.iac_state = 0;
      } else if (ch >= 251 && ch <= 254) {  // WILL/WONT/DO/DONT
        c.iac_cmd = ch;
        c.iac_state = 2;
      } else {
        c.iac_state = 0;
      }
    } else if (c.iac_state == 2) {
      c.iac_state = 0;
    }
  }

  if (!clean.empty()) {
    queue_ws_frame(c, 0x01, reinterpret_cast<const uint8_t *>(clean.data()), clean.size());
  }
}

}  // namespace

extern "C" int web_bridge_init(int http_port, int mud_port) {
  g_mud_port = mud_port;
  g_http_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (g_http_listen_fd < 0) {
    perror("web_bridge_init: socket");
    return -1;
  }

  int opt = 1;
  setsockopt(g_http_listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
  set_nonblocking(g_http_listen_fd);

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(static_cast<uint16_t>(http_port));

  if (bind(g_http_listen_fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0) {
    perror("web_bridge_init: bind");
    close(g_http_listen_fd);
    g_http_listen_fd = -1;
    return -1;
  }

  if (listen(g_http_listen_fd, 32) < 0) {
    perror("web_bridge_init: listen");
    close(g_http_listen_fd);
    g_http_listen_fd = -1;
    return -1;
  }

  fprintf(stderr, "Web/WebSocket bridge listening on 0.0.0.0:%d -> MUD port %d\n", http_port, mud_port);
  return 0;
}

extern "C" void web_bridge_poll(void) {
  if (g_http_listen_fd < 0) return;

  for (int i = 0; i < 8; ++i) {
    struct sockaddr_in cli_addr;
    socklen_t cli_len = sizeof(cli_addr);
    int cfd = accept(g_http_listen_fd, reinterpret_cast<struct sockaddr *>(&cli_addr), &cli_len);
    if (cfd < 0) break;
    set_nonblocking(cfd);
    WebClient wc;
    wc.fd = cfd;
    wc.mud_fd = -1;
    wc.is_ws = false;
    wc.closed = false;
    wc.iac_state = 0;
    wc.iac_cmd = 0;
    wc.iac_sb = false;
    g_clients.push_back(wc);
  }

  uint8_t buf[4096];
  for (size_t idx = 0; idx < g_clients.size(); ++idx) {
    WebClient &c = g_clients[idx];
    if (c.closed) continue;

    /* Read from browser socket */
    for (;;) {
      ssize_t n = recv(c.fd, buf, sizeof(buf), 0);
      if (n > 0) {
        if (!c.is_ws) {
          c.http_in.append(reinterpret_cast<const char *>(buf), static_cast<size_t>(n));
          if (c.http_in.size() > 16384) {
            close_client(c);
            break;
          }
          handle_http_request(c);
          if (c.closed) break;
        } else {
          c.ws_in.insert(c.ws_in.end(), buf, buf + n);
          process_ws_frames(c);
          if (c.closed) break;
        }
      } else if (n == 0) {
        close_client(c);
        break;
      } else {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
          close_client(c);
        }
        break;
      }
    }
    if (c.closed) continue;

    /* Forward WebSocket input to MUD socket */
    if (c.is_ws && c.mud_fd >= 0 && !c.mud_out.empty()) {
      ssize_t n = send(c.mud_fd, c.mud_out.data(), c.mud_out.size(), MSG_NOSIGNAL);
      if (n > 0) {
        c.mud_out.erase(0, static_cast<size_t>(n));
      } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != ENOTCONN) {
        close_client(c);
        continue;
      }
    }

    /* Read from MUD socket and forward to WebSocket */
    if (c.is_ws && c.mud_fd >= 0) {
      for (;;) {
        ssize_t n = recv(c.mud_fd, buf, sizeof(buf), 0);
        if (n > 0) {
          filter_telnet_to_ws(c, buf, static_cast<size_t>(n));
        } else if (n == 0) {
          close_client(c);
          break;
        } else {
          if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINPROGRESS && errno != ENOTCONN) {
            close_client(c);
          }
          break;
        }
      }
    }
    if (c.closed) continue;

    /* Flush WebSocket outgoing frames */
    if (c.is_ws && !c.ws_out.empty()) {
      ssize_t n = send(c.fd, c.ws_out.data(), c.ws_out.size(), MSG_NOSIGNAL);
      if (n > 0) {
        c.ws_out.erase(c.ws_out.begin(), c.ws_out.begin() + n);
      } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        close_client(c);
      }
    }
  }

  g_clients.erase(
      std::remove_if(g_clients.begin(), g_clients.end(), [](const WebClient &c) { return c.closed; }),
      g_clients.end());
}

extern "C" void web_bridge_shutdown(void) {
  for (size_t i = 0; i < g_clients.size(); ++i) {
    close_client(g_clients[i]);
  }
  g_clients.clear();
  if (g_http_listen_fd >= 0) {
    close(g_http_listen_fd);
    g_http_listen_fd = -1;
  }
}

// =========================== 局域网在线发现 (无服务器 P2P) ===========================
// 协议: 每 2 秒向同路由器广播 UDP(NCMPEER2|...); CCWHO2 带登录用户名;
// classChat 聊天/文件/屏幕协议(CCMSG2/CCFILE2/CCGRP2/CCCAST2/CCMON2)见聊天模块。
static void ccOnNet(const std::string& s, unsigned long fromIp);   // 前向声明 (聊天模块)
static void ccLog(const std::wstring& tag, const std::wstring& entry);   // 操作日志 (聊天模块)

namespace lan {

static const unsigned short PORT = 47833;
static const int PEER_TIMEOUT_MS = 7000;

struct Peer {
    unsigned long long id;
    unsigned long long boot;
    bool isAdmin;
    std::wstring name;
    long long lastMs;
};

static SOCKET g_sock = INVALID_SOCKET;
static unsigned long long g_myId = 0;
static unsigned long long g_myBoot = 0;
static bool g_myAdmin = false;
static wchar_t g_myName[128] = {0};
static std::map<unsigned long long, Peer> g_peers;
// classChat 在线成员: 登录用户名 -> (IP 主机序字节串, 最后心跳毫秒)
struct Who { unsigned long ip; long long ms; };
static std::map<std::wstring, Who> g_who;

static long long nowMs() {
    FILETIME ft; GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u; u.LowPart = ft.dwLowDateTime; u.HighPart = ft.dwHighDateTime;
    return (long long)(u.QuadPart / 10000 - 11644473600000ULL);
}

static void init() {
    g_myAdmin = g_isAdmin;   // 每次进入主窗口都刷新当前登录身份
    if (g_sock != INVALID_SOCKET) return;
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return;
    g_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_sock == INVALID_SOCKET) return;
    BOOL yes = TRUE;
    setsockopt(g_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(PORT);
    if (bind(g_sock, (sockaddr*)&addr, sizeof(addr)) != 0) {
        closesocket(g_sock); g_sock = INVALID_SOCKET; return;
    }
    setsockopt(g_sock, SOL_SOCKET, SO_BROADCAST, (const char*)&yes, sizeof(yes));
    u_long nb = 1;
    ioctlsocket(g_sock, FIONBIO, &nb);

    g_myBoot = (unsigned long long)nowMs();
    srand((unsigned)g_myBoot ^ GetTickCount());
    g_myId = g_myBoot * 1000ull + (unsigned)(rand() % 1000);
    DWORD nlen = 127;
    GetComputerNameW(g_myName, &nlen);
}

static void broadcast() {
    if (g_sock == INVALID_SOCKET) return;
    std::string msg = "NCMPEER2|" + std::to_string(g_myId) + "|" +
                      std::to_string(g_myBoot) + "|" + (g_myAdmin ? "1|" : "0|") +
                      wToUtf8(g_myName);
    sockaddr_in to;
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons(PORT);
    to.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    sendto(g_sock, msg.data(), (int)msg.size(), 0, (sockaddr*)&to, sizeof(to));
}

// ---- VIP 解锁申请 / 审批结果 ----
static unsigned long long genReqId() {
    return (unsigned long long)nowMs() * 1000ull + (unsigned)(rand() % 1000);
}
static void sendVipRequest(unsigned long long reqId, const std::wstring& user) {
    if (g_sock == INVALID_SOCKET) return;
    std::string msg = "VIPREQ2|" + std::to_string(reqId) + "|" +
                      wToUtf8(user) + "|" + wToUtf8(g_myName);
    sockaddr_in to;
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons(PORT);
    to.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    sendto(g_sock, msg.data(), (int)msg.size(), 0, (sockaddr*)&to, sizeof(to));
}
static void sendVipResult(unsigned long long reqId, bool ok) {
    if (g_sock == INVALID_SOCKET) return;
    std::string msg = (ok ? "VIPOK2|" : "VIPNO2|") + std::to_string(reqId);
    sockaddr_in to;
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons(PORT);
    to.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    sendto(g_sock, msg.data(), (int)msg.size(), 0, (sockaddr*)&to, sizeof(to));
}

// ---- VIP 配置同步 (管理员机为权威来源) ----
static std::string hexEnc(const std::string& s) {
    static const char hx[] = "0123456789ABCDEF";
    std::string o; o.reserve(s.size() * 2);
    for (unsigned char c : s) { o.push_back(hx[c >> 4]); o.push_back(hx[c & 15]); }
    return o;
}
static std::string hexDec(const std::string& s) {
    std::string o;
    auto dv = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    for (size_t i = 0; i + 1 < s.size(); i += 2) {
        int a = dv(s[i]), b = dv(s[i + 1]);
        if (a < 0 || b < 0) break;
        o.push_back((char)((a << 4) | b));
    }
    return o;
}
static void bcastRaw(const std::string& msg) {
    if (g_sock == INVALID_SOCKET || msg.size() > 1900) return;
    sockaddr_in to;
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons(PORT);
    to.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    sendto(g_sock, msg.data(), (int)msg.size(), 0, (sockaddr*)&to, sizeof(to));
}
// ---- classChat: 广播团队+登录用户名 (在线成员列表/监视目标选择) ----
static std::wstring whoKey(const std::wstring& team, const std::wstring& name) { return team + L"\x1f" + name; }
static void sayWho() {
    if (g_sock == INVALID_SOCKET || g_currentUser.empty()) return;
    bcastRaw("CCWHO3|" + hexEnc(wToUtf8(g_team)) + "|" + hexEnc(wToUtf8(g_currentUser)));
}
static bool whoOnline(const std::wstring& name) {
    long long t = nowMs();
    auto it = g_who.find(whoKey(g_team, name));
    return it != g_who.end() && t - it->second.ms <= PEER_TIMEOUT_MS;
}
static bool whoIp(const std::wstring& name, unsigned long& ip) {
    auto it = g_who.find(whoKey(g_team, name));
    if (it == g_who.end() || nowMs() - it->second.ms > PEER_TIMEOUT_MS) return false;
    ip = it->second.ip; return true;
}
// 管理员广播权威配置:
// VIPCFG2|id|enabled|approval|needpass|trigm|trigsecs|trigt|hex口令|hex密码|hex逗号名单
static void sendVipCfg() {
    if (!g_isAdmin) return;
    VipCfg cfg = loadVipCfg();
    std::string needCsv;
    for (auto& u : cfg.need) {
        if (!needCsv.empty()) needCsv += ",";
        needCsv += wToUtf8(u);
    }
    char t[32];
    snprintf(t, sizeof(t), "%02d:%02d", cfg.trigHour, cfg.trigMin);
    std::string msg = "VIPCFG2|" + std::to_string(g_myId) + "|" +
                      (cfg.enabled ? "1|" : "0|") +
                      (cfg.needApproval ? "1|" : "0|") +
                      (cfg.needPass ? "1|" : "0|") +
                      std::to_string(cfg.trigMode) + "|" +
                      std::to_string(cfg.trigSecs) + "|" + t + "|" +
                      hexEnc(wToUtf8(cfg.phrase)) + "|" +
                      hexEnc(wToUtf8(cfg.pass)) + "|" + hexEnc(needCsv);
    bcastRaw(msg);
}
// 普通用户请求管理员下发配置
static void requestVipCfg() { bcastRaw("VIPCFGQ2"); }
// 新注册用户上报管理员, 要求纳入名单
static void vipAddNotice(const std::wstring& user) {
    bcastRaw("VIPADD2|" + hexEnc(wToUtf8(user)));
}

static void receiveAll() {
    if (g_sock == INVALID_SOCKET) return;
    char buf[2048];
    sockaddr_in from;
    int fromLen = sizeof(from);
    long long ts = nowMs();
    for (;;) {
        int n = recvfrom(g_sock, buf, sizeof(buf) - 1, 0,
                         (sockaddr*)&from, &fromLen);
        if (n <= 0) break;
        buf[n] = 0;
        std::string s(buf, n);
        unsigned long fromIp = ntohl(from.sin_addr.s_addr);

        // ---- classChat: 团队+在线用户名心跳 ----
        if (s.compare(0, 7, "CCWHO3|") == 0) {
            size_t bar = s.find('|', 7);
            if (bar != std::string::npos) {
                std::wstring tm = utf8ToW(hexDec(s.substr(7, bar - 7)));
                std::wstring u  = utf8ToW(hexDec(s.substr(bar + 1)));
                if (!tm.empty() && !u.empty()) {
                    std::wstring k = whoKey(tm, u);
                    bool isNew = g_who.find(k) == g_who.end();
                    Who w{fromIp, nowMs()}; g_who[k] = w;
                    if (isNew && g_isAdmin && tm == g_team && u != g_currentUser) {
                        unsigned char* b = (unsigned char*)&fromIp;
                        wchar_t ip[64]; wsprintfW(ip, L"%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
                        ccLog(L"上下线", L"用户 " + u + L" 上线 (IP: " + ip + L")");
                    }
                }
            }
            continue;
        }
        // ---- classChat: 聊天/文件/建群/屏幕/ACK/团队 协议转交聊天模块 ----
        if (s.compare(0, 3, "CCM") == 0 || s.compare(0, 3, "CCF") == 0 ||
            s.compare(0, 3, "CCG") == 0 || s.compare(0, 3, "CCC") == 0 ||
            s.compare(0, 3, "CCA") == 0 || s.compare(0, 3, "CCT") == 0 ||
            s.compare(0, 3, "CCR") == 0) {
            ccOnNet(s, fromIp);
            continue;
        }

        // ---- VIP 配置下发: 仅普通用户接受管理员的权威配置 ----
        if (s.compare(0, 8, "VIPCFG2|") == 0) {
            if (!g_isAdmin) {
                // VIPCFG2|id|enabled|approval|needpass|trigm|trigsecs|trigt|phraseHex|passHex|needHex
                size_t q[10];
                q[0] = s.find('|');
                bool okFmt = q[0] != std::string::npos;
                for (int i = 1; i < 10 && okFmt; ++i) {
                    q[i] = s.find('|', q[i - 1] + 1);
                    if (i < 9 && q[i] == std::string::npos) okFmt = false;
                }
                if (okFmt) {
                    VipCfg cfg;
                    cfg.enabled      = s[q[1] + 1] == '1';
                    cfg.needApproval = s[q[2] + 1] == '1';
                    cfg.needPass     = s[q[3] + 1] == '1';
                    cfg.trigMode     = s[q[4] + 1] == '1' ? 1 : 0;
                    cfg.trigSecs     = atoi(s.c_str() + q[5] + 1);
                    if (cfg.trigSecs < 1 || cfg.trigSecs > 86400) cfg.trigSecs = 30;
                    sscanf(s.c_str() + q[6] + 1, "%d:%d", &cfg.trigHour, &cfg.trigMin);
                    if (cfg.trigHour < 0 || cfg.trigHour > 23 || cfg.trigMin < 0 || cfg.trigMin > 59) {
                        cfg.trigHour = 9; cfg.trigMin = 0;
                    }
                    cfg.phrase = utf8ToW(hexDec(s.substr(q[7] + 1, q[8] - q[7] - 1)));
                    cfg.pass   = utf8ToW(hexDec(s.substr(q[8] + 1, q[9] - q[8] - 1)));
                    std::string csv = hexDec(s.substr(q[9] + 1));
                    size_t c = 0;
                    while (c < csv.size()) {
                        size_t e = csv.find(',', c);
                        if (e == std::string::npos) e = csv.size();
                        std::wstring u = utf8ToW(csv.substr(c, e - c));
                        if (!u.empty()) cfg.need.push_back(u);
                        c = e + 1;
                    }
                    // 配置签名去重: 内容相同的重复广播(心跳重播/多管理员)不再落盘和通知,
                    // 避免反复重置触发倒计时和解锁后被无端重锁(计时触发变成每 N 秒锁一次)。
                    static std::string s_lastCfgSig;
                    std::string sig = s.substr(q[1] + 1);
                    if (!s_lastCfgSig.empty() && sig == s_lastCfgSig) {
                        // 重复广播, 忽略
                    } else if (saveVipCfg(cfg)) {
                        s_lastCfgSig = sig;
                        // 配置被关闭或本用户被移出名单: 此前的"已解锁"消耗标记作废,
                        // 管理员下次开启(即使内容相同)将重新触发锁屏。
                        if (!cfg.enabled || !userNeedsVip(cfg, g_currentUser))
                            clearVipCfgConsumed();
                        // 快速同步: 立即通知锁屏窗口(可能需要立刻解锁)和主窗口(重新布防)
                        if (g_vipLock) PostMessageW(g_vipLock, WM_APP_VIPCFGCHG, 0, 0);
                        if (g_mainWnd) PostMessageW(g_mainWnd, WM_APP_VIPCFGCHG, 0, 0);
                    }
                }
            }
            continue;
        }
        // ---- 普通用户请求配置: 管理员立即下发 ----
        if (s.compare(0, 9, "VIPCFGQ2|") == 0 || s == "VIPCFGQ2") {
            if (g_isAdmin) sendVipCfg();
            continue;
        }
        // ---- 新注册用户上报: 模式开启时管理员补入名单并重新下发 ----
        if (s.compare(0, 8, "VIPADD2|") == 0) {
            if (g_isAdmin) {
                std::wstring u = utf8ToW(hexDec(s.substr(8)));
                if (!u.empty()) {
                    VipCfg cfg = loadVipCfg();
                    if (cfg.enabled && !userNeedsVip(cfg, u)) {
                        cfg.need.push_back(u);
                        if (saveVipCfg(cfg)) sendVipCfg();
                    }
                }
            }
            continue;
        }

        // ---- VIP 审批结果: 锁屏窗口等待中 ----
        if (s.compare(0, 7, "VIPOK2|") == 0 || s.compare(0, 7, "VIPNO2|") == 0) {
            unsigned long long rid = _strtoui64(s.c_str() + 7, nullptr, 10);
            if (g_vipLock && rid && rid == g_vipReqId) {
                VipResultPayload* rp = new VipResultPayload();
                rp->id = rid; rp->ok = (s[0] == 'V' && s[3] == 'O');
                PostMessageW(g_vipLock, WM_APP_VIP_RESULT, (WPARAM)rp, 0);
            }
            continue;
        }
        // ---- VIP 解锁申请: 仅管理员端受理 ----
        if (s.compare(0, 8, "VIPREQ2|") == 0) {
            if (g_isAdmin && g_mainWnd) {
                size_t q1 = s.find('|'), q2 = s.find('|', q1 + 1);
                size_t q3 = s.find('|', q2 + 1);
                if (q1 != std::string::npos && q2 != std::string::npos &&
                    q3 != std::string::npos) {
                    VipReqPayload* vp = new VipReqPayload();
                    vp->id = _strtoui64(s.c_str() + q1 + 1, nullptr, 10);
                    vp->user = utf8ToW(s.substr(q2 + 1, q3 - q2 - 1));
                    vp->machine = utf8ToW(s.substr(q3 + 1));
                    PostMessageW(g_mainWnd, WM_APP_VIP_REQ, (WPARAM)vp, 0);
                }
            }
            continue;
        }

        // NCMPEER2|id|boot|isAdmin|name
        size_t p1 = s.find('|');
        size_t p2 = (p1 == std::string::npos) ? p1 : s.find('|', p1 + 1);
        size_t p3 = (p2 == std::string::npos) ? p2 : s.find('|', p2 + 1);
        size_t p4 = (p3 == std::string::npos) ? p3 : s.find('|', p3 + 1);
        if (p1 == std::string::npos || p2 == std::string::npos ||
            p3 == std::string::npos || p4 == std::string::npos ||
            s.compare(0, 9, "NCMPEER2|") != 0) continue;
        unsigned long long id = _strtoui64(s.c_str() + p1 + 1, nullptr, 10);
        unsigned long long boot = _strtoui64(s.c_str() + p2 + 1, nullptr, 10);
        bool isAdmin = s[p3 + 1] == '1';
        if (id == 0 || id == g_myId) continue;   // 忽略自己
        Peer& pr = g_peers[id];
        pr.id = id; pr.boot = boot; pr.isAdmin = isAdmin;
        pr.name = utf8ToW(s.substr(p4 + 1));
        if (pr.name.empty()) {
            wchar_t ipb[64];
            DWORD ip = ntohl(from.sin_addr.s_addr);
            swprintf(ipb, 64, L"%u.%u.%u.%u",
                     (ip >> 24) & 0xff, (ip >> 16) & 0xff,
                     (ip >> 8) & 0xff, ip & 0xff);
            pr.name = ipb;
        }
        pr.lastMs = ts;
    }
    // 清理超时主机
    for (auto it = g_peers.begin(); it != g_peers.end();) {
        if (ts - it->second.lastMs > PEER_TIMEOUT_MS) it = g_peers.erase(it);
        else ++it;
    }
}

// 返回状态文字与颜色; bOnline=false 表示离线(灰), true=在线(绿)
static std::wstring statusText(bool& bOnline) {
    // 只在"管理员登录的实例"中选举主机; 本机若为管理员也是候选
    bool haveAdmin = g_myAdmin;
    unsigned long long hostId = g_myId, hostBoot = g_myBoot;
    std::wstring hostName = g_myName;
    bool iAmHost = g_myAdmin;
    for (auto& kv : g_peers) {
        const Peer& p = kv.second;
        if (!p.isAdmin) continue;
        haveAdmin = true;
        if (!iAmHost || p.boot < hostBoot ||
            (p.boot == hostBoot && p.id < hostId)) {
            hostBoot = p.boot; hostId = p.id; hostName = p.name; iAmHost = false;
        }
    }
    if (iAmHost && g_myAdmin) {
        // 本机即管理员主机
        bOnline = true;
        int others = (int)g_peers.size();
        return others > 0
            ? (L"● 在线 · 管理员运行中 · 局域网内 " + std::to_wstring(others) + L" 台设备")
            : L"● 在线 · 管理员运行中";
    }
    // 管理员主机是别的实例: 心跳新鲜才算已联系
    auto it = g_peers.find(hostId);
    if (haveAdmin && it != g_peers.end() &&
        nowMs() - it->second.lastMs <= PEER_TIMEOUT_MS) {
        bOnline = true;
        return L"● 在线 · 已与管理员联系: " + hostName;
    }
    bOnline = false;
    return L"● 离线 · 未与管理员联系";
}

// 当前是否有在线的管理员实例 (含本机)
static bool adminOnline() {
    if (g_myAdmin) return true;
    long long ts = nowMs();
    for (auto& kv : g_peers)
        if (kv.second.isAdmin && ts - kv.second.lastMs <= PEER_TIMEOUT_MS)
            return true;
    return false;
}

static int g_syncTick = 0;

static void tick(HWND hStatus) {
    receiveAll();
    broadcast();
    sayWho();   // classChat 在线用户名
    // 前 3 个心跳立即同步配置, 之后每 8 个心跳(约16秒)一次
    if (g_syncTick < 3 || (g_syncTick % 8) == 0) {
        if (g_isAdmin) sendVipCfg();
        else           requestVipCfg();
    }
    ++g_syncTick;
    bool online = false;
    std::wstring t = statusText(online);
    if (hStatus) {
        // 仅在文本/状态变化时更新, 避免每 2 秒整条状态栏闪烁重绘
        static std::wstring s_lastText;
        static int s_lastOnline = -1;
        if (t != s_lastText) { SetWindowTextW(hStatus, t.c_str()); s_lastText = t; }
        if ((int)(online ? 1 : 0) != s_lastOnline) {
            SetPropW(hStatus, L"onlineColor", (HANDLE)(INT_PTR)(online ? 1 : 0));
            InvalidateRect(hStatus, nullptr, TRUE);
            s_lastOnline = online ? 1 : 0;
        }
    }
}

static void shutdown() {
    if (g_sock != INVALID_SOCKET) { closesocket(g_sock); g_sock = INVALID_SOCKET; }
    WSACleanup();
}

} // namespace lan


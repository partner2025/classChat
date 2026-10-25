// =========================== 主窗口 ===========================
// =========================== classChat 聊天模块 ===========================
// 群聊(全员群/管理员学生群/两两群/自建群)、文字与表情消息、文件直传(TCP)、
// 教师屏幕共享、管理员屏幕监视(TCP+JPEG)、炸弹全屏特效。
#define CC_FILE_PORT   47834
#define CC_SCREEN_PORT 47835
#define CC_CAM_PORT    47836

// 旧版 w32api 缺 GetTickCount64 原型 (Vista+, kernel32 实际导出)
extern "C" WINBASEAPI ULONGLONG WINAPI GetTickCount64(VOID);

struct CcGroup { std::wstring id, name; std::vector<std::wstring> mem; };
struct CcMsg {
    std::wstring from, body, time, note, id;
    wchar_t kind;
    ULONGLONG sendTime;                   // GetTickCount64, 用于 2 分钟撤回时限
    std::set<std::wstring> readBy;       // 已读回执(阅读者)
};  // kind: T文本 E表情(L/C/B) F文件 S系统提示; note=文件状态
static std::vector<CcGroup> g_ccGroups;
static std::map<std::wstring, std::vector<CcMsg>> g_ccMsgs;
static std::vector<int> g_ccVisible;   // 群列表中可见(含自己)的群 -> g_ccGroups 下标
static std::wstring g_ccCur;
static int g_ccvUp = 0;                // 消息区从底部向上滚动的像素
static HWND g_ccList = nullptr, g_ccView = nullptr, g_ccInput = nullptr, g_ccTitle = nullptr;
static std::map<std::wstring, int> g_ccUnread;   // 未读计数 (gid -> 条数, 微信式红点)
static bool g_ccSharing = false;   // 教师正在共享屏幕
// --- 主题/搜索/撤回命中/已读去重/操作日志 ---
static bool g_darkMode = false;
static std::wstring g_ccSearchText;
struct CcHit { int top, bottom; size_t idx; bool mine; };
static std::vector<CcHit> g_ccHits;    // 当前群消息绘制时的命中条(右键撤回用)
static std::set<std::wstring> g_readSent;   // 已发过的已读回执 gid|msgId
static std::vector<std::wstring> g_ccLogs;  // "[时间][标签] 内容"
static std::wstring g_ccLogFilter;
static HWND g_ccLogWnd = nullptr, g_ccLogList = nullptr;
static HWND g_ccCmdWnd = nullptr;

static bool ccIEq(const std::wstring& a, const std::wstring& b);   // 前向
static std::wstring ccNewMsgId() {
    wchar_t b[32];
    swprintf(b, 32, L"%llx%04x", (unsigned long long)GetTickCount64(),
             (unsigned)(rand() & 0xFFFF));
    return b;
}
static COLORREF ccAvatarColor(const std::wstring& nm) {
    static const COLORREF pal[] = {
        RGB(0x1A,0x73,0xE8), RGB(0xE8,0x4D,0x1A), RGB(0x2E,0x7D,0x32),
        RGB(0x8E,0x24,0xAA), RGB(0x00,0x8A,0x8A), RGB(0xC6,0x28,0x28),
        RGB(0x6A,0x4C,0xC0), RGB(0xAD,0x6A,0x00)};
    for (size_t i = 0; i < CC_NAMES.size(); ++i)
        if (ccIEq(CC_NAMES[i], nm)) return pal[i % 8];
    unsigned h = 0; for (wchar_t c : nm) h = h * 31 + (unsigned)c;
    return pal[h % 8];
}

static bool ccIEq(const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) == 0; }
static bool ccInGroup(const CcGroup& g, const std::wstring& u) {
    for (auto& m : g.mem) if (ccIEq(m, u)) return true;
    return false;
}
static CcGroup* ccFind(const std::wstring& id) {
    for (auto& g : g_ccGroups) if (g.id == id) return &g;
    return nullptr;
}
static std::wstring ccGroupsPath() {
    return dataPathFor(L"cgroups.dat");
}
static void ccSaveCustomGroups() {
    FILE* fp = _wfopen(ccGroupsPath().c_str(), L"wb");
    if (!fp) return;
    for (auto& g : g_ccGroups) {
        if (g.id.empty() || g.id[0] != L'c') continue;
        std::string csv;
        for (auto& m : g.mem) { if (!csv.empty()) csv += ","; csv += wToUtf8(m); }
        std::string line = wToUtf8(g.id) + "|" + lan::hexEnc(wToUtf8(g.name)) + "|" +
                           lan::hexEnc(csv) + "\n";
        fwrite(line.data(), 1, line.size(), fp);
    }
    fclose(fp);
}
static void ccLoadCustomGroups() {
    FILE* fp = _wfopen(ccGroupsPath().c_str(), L"rb");
    if (!fp) return;
    std::string all; char buf[4096]; size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) all.append(buf, n);
    fclose(fp);
    size_t pos = 0;
    while (pos < all.size()) {
        size_t e = all.find('\n', pos);
        if (e == std::string::npos) e = all.size();
        std::string line = all.substr(pos, e - pos);
        pos = e + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t p1 = line.find('|'), p2 = line.find('|', p1 + 1);
        if (p1 == std::string::npos || p2 == std::string::npos) continue;
        CcGroup g;
        g.id = utf8ToW(line.substr(0, p1));
        g.name = utf8ToW(lan::hexDec(line.substr(p1 + 1, p2 - p1 - 1)));
        std::string csv = lan::hexDec(line.substr(p2 + 1));
        size_t c = 0;
        while (c < csv.size()) {
            size_t x = csv.find(',', c);
            if (x == std::string::npos) x = csv.size();
            std::wstring u = utf8ToW(csv.substr(c, x - c));
            if (!u.empty()) g.mem.push_back(u);
            c = x + 1;
        }
        if (!g.id.empty() && !ccFind(g.id)) g_ccGroups.push_back(g);
    }
}
static void ccInitGroups() {
    g_ccGroups.clear();
    int n = (int)CC_NAMES.size();
    CcGroup all; all.id = L"a"; all.name = L"全员群 (" + std::to_wstring(n) + L"人)";
    for (auto& nm : CC_NAMES) all.mem.push_back(nm);
    g_ccGroups.push_back(all);
    // 管理员 + 学生 群(排除纯教师)
    CcGroup as; as.id = L"s";
    as.name = L"管理员·学生群";
    for (auto& nm : CC_NAMES)
        if (ccHasRole(nm, L"管理员") ||
            (!ccHasRole(nm, L"教师") && !ccHasRole(nm, L"管理员")))
            as.mem.push_back(nm);
    as.name += L" (" + std::to_wstring(as.mem.size()) + L"人)";
    g_ccGroups.push_back(as);
    // 两两群
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j) {
            CcGroup g;
            wchar_t ib[8], jb[8]; swprintf(ib, 8, L"%d", i); swprintf(jb, 8, L"%d", j);
            g.id = std::wstring(ib) + L"_" + jb;
            g.name = std::wstring(CC_NAMES[i]) + L" · " + CC_NAMES[j];
            g.mem.push_back(CC_NAMES[i]); g.mem.push_back(CC_NAMES[j]);
            g_ccGroups.push_back(g);
        }
    ccLoadCustomGroups();
    g_ccCur = g_ccGroups.empty() ? L"" : g_ccGroups[0].id;
}
static bool ccMyGroup(const CcGroup& g) {
    for (auto& m : g.mem) if (ccIEq(m, g_currentUser)) return true;
    return false;
}
static void ccRefreshList() {
    if (!g_ccList) return;
    SendMessageW(g_ccList, LB_RESETCONTENT, 0, 0);
    g_ccVisible.clear();
    int sel = 0;
    for (size_t i = 0; i < g_ccGroups.size(); ++i) {
        if (!ccMyGroup(g_ccGroups[i])) continue;
        g_ccVisible.push_back((int)i);
        SendMessageW(g_ccList, LB_ADDSTRING, 0, (LPARAM)g_ccGroups[i].name.c_str());
        if (g_ccGroups[i].id == g_ccCur) sel = (int)g_ccVisible.size() - 1;
    }
    SendMessageW(g_ccList, LB_SETCURSEL, sel, 0);
}
static void ccBroadcastReads();   // 前向: 切群后对该群消息广播已读
static void ccRefreshMessages() {
    CcGroup* g = ccFind(g_ccCur);
    if (g_ccTitle && g) SetWindowTextW(g_ccTitle, (g->name + L"    [" +
        std::to_wstring(g->mem.size()) + L"人]").c_str());
    g_ccUnread.erase(g_ccCur);   // 正在查看/恢复窗口: 当前群未读清零
    g_ccvUp = 0;   // 新消息/切群: 滚回底部
    if (g_ccView) InvalidateRect(g_ccView, nullptr, FALSE);
    ccBroadcastReads();
}
static std::wstring ccNowHM() {
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t t[16]; swprintf(t, 16, L"%02d:%02d", st.wHour, st.wMinute);
    return t;
}
static void ccHistAppend(const std::wstring&, const CcMsg&);   // 前向
static void ccAppend(const std::wstring& gid, const CcMsg& m) {
    g_ccMsgs[gid].push_back(m);
    auto& v = g_ccMsgs[gid];
    if (v.size() > 500) v.erase(v.begin(), v.begin() + 100);
    ccHistAppend(gid, m);   // 教师角色: 持久化到 AppData
    // 未读红点: 他人消息且 (不在当前群 或 主窗隐藏在托盘) 时计数
    bool fromOther = !m.from.empty() && m.from != g_currentUser
                     && (m.kind == L'T' || m.kind == L'E' || m.kind == L'F');
    if (fromOther && (gid != g_ccCur || g_trayHidden)) {
        if (++g_ccUnread[gid] > 999) g_ccUnread[gid] = 999;
        InvalidateRect(g_ccList, nullptr, FALSE);
    }
    if (gid == g_ccCur && !g_trayHidden) ccRefreshMessages();
}
// ---------------- 聊天历史持久化 (教师角色, %APPDATA%\classChat\history) ----------------
static std::wstring ccHistDir() {
    wchar_t b[MAX_PATH] = {0};
    ExpandEnvironmentStringsW(L"%APPDATA%\\classChat", b, MAX_PATH);
    CreateDirectoryW(b, nullptr);
    wcscat(b, L"\\history");
    CreateDirectoryW(b, nullptr);
    return b;
}
static std::wstring ccHistEscape(const std::wstring& s, bool enc) {
    std::wstring r;
    if (enc) {
        for (wchar_t c : s) {
            if (c == L'\n') r += L"\\n";
            else if (c == L'\t') r += L"\\t";
            else if (c == L'\\') r += L"\\\\";
            else r += c;
        }
    } else {
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == L'\\' && i + 1 < s.size()) {
                if (s[i+1] == L'n') { r += L'\n'; ++i; continue; }
                if (s[i+1] == L't') { r += L'\t'; ++i; continue; }
                if (s[i+1] == L'\\') { r += L'\\'; ++i; continue; }
            }
            r += s[i];
        }
    }
    return r;
}
static void ccHistAppend(const std::wstring& gid, const CcMsg& m) {
    if (!ccHasRole(g_currentUser, L"教师")) return;   // 仅教师角色持久化
    if (m.kind == L'S') return;                     // 系统提示不存
    std::wstring p = ccHistDir() + L"\\" + gid + L".log";
    FILE* fp = _wfopen(p.c_str(), L"ab");
    if (!fp) return;
    std::wstring w = m.time + L"\t" + m.from + L"\t" + wchar_t(m.kind) + L"\t" +
                     ccHistEscape(m.body, true);
    if (m.kind == L'F' && !m.note.empty()) w += L"\t" + ccHistEscape(m.note, true);
    w += L"\n";
    std::string u = wToUtf8(w);
    fwrite(u.data(), 1, u.size(), fp);
    fclose(fp);
}
static void ccHistLoad() {
    if (!ccHasRole(g_currentUser, L"教师")) return;
    std::wstring dir = ccHistDir();
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*.log").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        std::wstring gid = fd.cFileName;
        gid = gid.substr(0, gid.size() - 4);
        FILE* fp = _wfopen((dir + L"\\" + fd.cFileName).c_str(), L"rb");
        if (!fp) continue;
        fseek(fp, 0, SEEK_END); long sz = ftell(fp); fseek(fp, 0, SEEK_SET);
        if (sz > 4 * 1024 * 1024) sz = 4 * 1024 * 1024;   // 单群最多读 4MB
        std::string buf(sz, '\0');
        fread(&buf[0], 1, sz, fp); fclose(fp);
        std::wstring all = utf8ToW(buf);
        auto& v = g_ccMsgs[gid];
        size_t pos = 0;
        while (pos < all.size() && v.size() < 400) {
            size_t e = all.find(L'\n', pos);
            std::wstring line = all.substr(pos, e == std::wstring::npos ? std::wstring::npos : e - pos);
            pos = (e == std::wstring::npos) ? all.size() : e + 1;
            if (!line.empty() && line.back() == L'\r') line.pop_back();
            std::vector<std::wstring> f;
            size_t c = 0;
            while (true) {   // 按 \t 分 4~5 段
                size_t t = line.find(L'\t', c);
                if (t == std::wstring::npos || f.size() >= 4) {
                    f.push_back(line.substr(c)); break;
                }
                f.push_back(line.substr(c, t - c)); c = t + 1;
            }
            if (f.size() < 4 || f[2].empty()) continue;
            wchar_t kind = f[2][0];
            if (kind != L'T' && kind != L'E' && kind != L'F') continue;
            CcMsg m{f[1], ccHistEscape(f[3], false), f[0], L"", L"", kind, 0};
            if (kind == L'F' && f.size() >= 5) m.note = ccHistEscape(f[4], false);
            v.push_back(m);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}
// 更新某条文件消息的状态(接收中/已保存/对方已接收)
static void ccSetFileNote(const std::wstring& gid, const std::wstring& from,
                          const std::wstring& fname, const std::wstring& note) {
    auto it = g_ccMsgs.find(gid);
    if (it == g_ccMsgs.end()) return;
    for (int i = (int)it->second.size() - 1; i >= 0; --i) {
        CcMsg& m = it->second[i];
        if (m.kind == 'F' && ccIEq(m.from, from) && m.body.find(fname) != std::wstring::npos) {
            m.note = note;
            if (gid == g_ccCur) { g_ccvUp = 0; InvalidateRect(g_ccView, nullptr, FALSE); }
            return;
        }
    }
    // 没找到对应气泡 -> 作为系统提示追加
    ccAppend(gid, CcMsg{L"", note, ccNowHM(), L"", L"", 'S'});
}
static void ccSendRaw(const std::string& msg) { lan::bcastRaw(msg); }
static void ccPlayBomb();   // 前向声明
static std::string ccTeamHex() { return lan::hexEnc(wToUtf8(g_team)); }
static std::wstring ccGroupName(const std::wstring& gid) {
    CcGroup* gp = ccFind(gid);
    return gp ? gp->name : gid;
}
static void ccLog(const std::wstring& tag, const std::wstring& entry) {
    std::wstring line = L"[" + ccNowHM() + L"][" + tag + L"] " + entry;
    g_ccLogs.push_back(line);
    if (g_ccLogs.size() > 2000) g_ccLogs.erase(g_ccLogs.begin());
    std::wstring p = dataPathFor(L"classChat.log");
    FILE* fp = _wfopen(p.c_str(), L"ab");
    if (fp) { std::string u = wToUtf8(line) + "\r\n"; fwrite(u.data(), 1, u.size(), fp); fclose(fp); }
    if (g_ccLogWnd && g_ccLogList) {
        if (g_ccLogFilter.empty() || tag.find(g_ccLogFilter) != std::wstring::npos)
            SendMessageW(g_ccLogList, LB_ADDSTRING, 0, (LPARAM)line.c_str());
    }
}
static void ccSendText(const std::wstring& text) {
    if (g_ccCur.empty() || text.empty()) return;
    std::wstring id = ccNewMsgId();
    std::string m = "CCMSG3|" + ccTeamHex() + "|" + lan::hexEnc(wToUtf8(g_ccCur)) + "|" +
                    lan::hexEnc(wToUtf8(g_currentUser)) + "|T|" + lan::hexEnc(wToUtf8(text)) +
                    "|" + lan::hexEnc(wToUtf8(id));
    ccSendRaw(m);
    CcMsg mm{g_currentUser, text, ccNowHM(), L"", id, 'T', GetTickCount64()};
    ccAppend(g_ccCur, mm);
    ccLog(L"消息", g_currentUser + L" 在 [" + ccGroupName(g_ccCur) + L"] 发送消息 (" +
                   std::to_wstring(text.size()) + L"字)");
}
static void ccSendEmoji(const wchar_t kind) {
    if (g_ccCur.empty()) return;
    std::wstring body(1, kind), id = ccNewMsgId();
    std::string m = "CCMSG3|" + ccTeamHex() + "|" + lan::hexEnc(wToUtf8(g_ccCur)) + "|" +
                    lan::hexEnc(wToUtf8(g_currentUser)) + "|E|" +
                    lan::hexEnc(std::string(1, (char)kind)) + "|" + lan::hexEnc(wToUtf8(id));
    ccSendRaw(m);
    ccAppend(g_ccCur, CcMsg{g_currentUser, body, ccNowHM(), L"", id, 'E', GetTickCount64()});
    if (kind == 'B') ccPlayBomb();
}

// ---------------- 微信式气泡消息区 (自绘) ----------------
// 自己的消息: 右侧绿色气泡; 其他人: 左侧白色气泡; 表情为图形; 不显示时间
static const int CCV_MAXBUB = 360;      // 气泡最大宽度
static const int CCV_EMO   = 36;        // 表情尺寸
static std::wstring ccvFileLine1(const CcMsg& m) {   // 文件名行
    size_t cut = m.body.find(L" (");
    std::wstring fn = (cut != std::wstring::npos && m.kind == 'F') ? m.body.substr(0, cut) : m.body;
    return L"📄 " + fn;
}
static int ccvMsgH(HDC dc, const CcMsg& m, int maxW) {
    int readH = (ccIEq(m.from, g_currentUser) && !m.readBy.empty()) ? 13 : 0;
    if (m.kind == 'S') {                 // 系统提示: 居中自动换行
        HFONT old = (HFONT)SelectObject(dc, hFontSmall);
        RECT r = {0, 0, maxW + 60, 0};
        DrawTextW(dc, m.body.c_str(), -1, &r, DT_CALCRECT | DT_WORDBREAK | DT_CENTER);
        SelectObject(dc, old);
        return (r.bottom - r.top) + 12;
    }
    if (m.kind == 'E') return 16 + 4 + CCV_EMO + 10 + readH;
    if (m.kind == 'F') {
        HFONT old = (HFONT)SelectObject(dc, hFontUI);
        RECT r1 = {0, 0, CCV_MAXBUB - 28, 0};
        DrawTextW(dc, ccvFileLine1(m).c_str(), -1, &r1, DT_CALCRECT | DT_WORDBREAK);
        SelectObject(dc, old);
        int h = r1.bottom - r1.top;
        int h2 = 0;
        if (!m.note.empty()) {
            old = (HFONT)SelectObject(dc, hFontSmall);
            RECT r2 = {0, 0, CCV_MAXBUB - 28, 0};
            DrawTextW(dc, m.note.c_str(), -1, &r2, DT_CALCRECT | DT_WORDBREAK);
            SelectObject(dc, old);
            h2 = r2.bottom - r2.top + 3;
        }
        return 16 + 4 + h + h2 + 18 + 10 + readH;   // 昵称+间距+内容+内边距+底距+已读
    }
    HFONT old = (HFONT)SelectObject(dc, hFontUI);
    RECT r = {0, 0, (maxW < CCV_MAXBUB ? maxW : CCV_MAXBUB) - 24, 0};
    DrawTextW(dc, m.body.c_str(), -1, &r, DT_CALCRECT | DT_WORDBREAK);
    SelectObject(dc, old);
    return 16 + 4 + (r.bottom - r.top) + 16 + 10 + readH;
}
// 画表情图: 笑=黄脸微笑, 哭=黄脸+泪滴, 炸弹=黑球+引信火花
static void ccvDrawEmoji(HDC dc, wchar_t k, int x, int y, int s) {
    HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
    if (k == 'B') {
        HBRUSH b = cachedBrush(RGB(45, 45, 55));
        HGDIOBJ ob = SelectObject(dc, b);
        Ellipse(dc, x, y + 8, x + s - 8, y + s);
        HBRUSH hl = cachedBrush(RGB(130, 130, 145));
        SelectObject(dc, hl);
        Ellipse(dc, x + 7, y + 15, x + 16, y + 24);
        SelectObject(dc, ob);
        static HPEN fuse = CreatePen(PS_SOLID, 3, RGB(140, 100, 60));
        HGDIOBJ op = SelectObject(dc, fuse);
        MoveToEx(dc, x + s - 12, y + 12, nullptr); LineTo(dc, x + s - 4, y + 2);
        static HPEN spark = CreatePen(PS_SOLID, 2, RGB(255, 170, 0));
        SelectObject(dc, spark);
        MoveToEx(dc, x + s - 4, y + 2, nullptr); LineTo(dc, x + s + 2, y - 4);
        MoveToEx(dc, x + s - 4, y + 2, nullptr); LineTo(dc, x + s + 4, y + 5);
        MoveToEx(dc, x + s - 4, y + 2, nullptr); LineTo(dc, x + s - 1, y - 5);
        SelectObject(dc, op);
    } else {
        HBRUSH f = cachedBrush(k == 'L' ? RGB(255, 212, 60) : RGB(255, 205, 85));
        HGDIOBJ ob = SelectObject(dc, f);
        Ellipse(dc, x, y, x + s, y + s);
        SelectObject(dc, ob);
        HBRUSH e = cachedBrush(RGB(70, 55, 40));
        ob = SelectObject(dc, e);
        Ellipse(dc, x + s * 28 / 100, y + s * 30 / 100, x + s * 28 / 100 + 6, y + s * 30 / 100 + 8);
        Ellipse(dc, x + s * 62 / 100, y + s * 30 / 100, x + s * 62 / 100 + 6, y + s * 30 / 100 + 8);
        SelectObject(dc, ob);
        static HPEN p = CreatePen(PS_SOLID, 2, RGB(70, 55, 40));
        HGDIOBJ op = SelectObject(dc, p);
        if (k == 'L')   // 微笑弧线
            Arc(dc, x + s / 4, y + s / 3, x + s * 3 / 4, y + s * 5 / 6,
                x + s * 3 / 10, y + s * 3 / 5, x + s * 7 / 10, y + s * 3 / 5);
        else {          // 哭: 下弯嘴 + 泪滴
            Arc(dc, x + s / 4, y + s * 3 / 5, x + s * 3 / 4, y + s * 11 / 10,
                x + s * 7 / 10, y + s * 4 / 5, x + s * 3 / 10, y + s * 4 / 5);
            SelectObject(dc, GetStockObject(NULL_PEN));
            HBRUSH tear = cachedBrush(RGB(90, 165, 255));
            SelectObject(dc, tear);
            Ellipse(dc, x + s * 20 / 100, y + s * 45 / 100, x + s * 20 / 100 + 7, y + s * 45 / 100 + 12);
            SelectObject(dc, op);
        }
        SelectObject(dc, op);
    }
    SelectObject(dc, oldPen);
}
// ---- 深色模式配色 ----
static COLORREF ccvBg()        { return g_darkMode ? RGB(0x1E,0x1E,0x2E) : RGB(245,245,245); }
static COLORREF ccvOtherBub()  { return g_darkMode ? RGB(0x2A,0x2A,0x3C) : RGB(255,255,255); }
static COLORREF ccvBorder()    { return g_darkMode ? RGB(0x3A,0x3A,0x50) : RGB(210,210,210); }
static COLORREF ccvFg()        { return g_darkMode ? RGB(0xE0,0xE0,0xE8) : RGB(20,20,20); }
static COLORREF ccvNameCol()   { return g_darkMode ? RGB(0x9A,0x9A,0xB0) : RGB(140,140,140); }
static COLORREF ccvNoteCol()   { return g_darkMode ? RGB(0x96,0x96,0xAA) : RGB(120,120,120); }
// ---- 头像 ----
static void ccvAvatar(HDC dc, const std::wstring& nm, int cx, int cy, int d) {
    HBRUSH b = cachedBrush(ccAvatarColor(nm));
    HGDIOBJ ob = SelectObject(dc, b);
    HPEN p = cachedPen(g_darkMode ? RGB(0x3A,0x3A,0x50) : RGB(255,255,255));
    HGDIOBJ op = SelectObject(dc, p);
    Ellipse(dc, cx, cy, cx + d, cy + d);
    SelectObject(dc, op);
    SelectObject(dc, ob);
    wchar_t ch = nm.empty() ? L'?' : nm[0];
    HGDIOBJ of = SelectObject(dc, hFontSmall);
    SetTextColor(dc, RGB(255,255,255));
    SetBkMode(dc, TRANSPARENT);
    RECT r = {cx, cy - 1, cx + d, cy + d};
    DrawTextW(dc, &ch, 1, &r, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    SelectObject(dc, of);
}
// ---- 搜索: 大小写不敏感包含 ----
static bool ccContainsI(const std::wstring& hay, const std::wstring& needle) {
    if (needle.empty()) return true;
    return _wcsicmp(hay.c_str(), needle.c_str()) == 0 ||
           std::search(hay.begin(), hay.end(), needle.begin(), needle.end(),
               [](wchar_t a, wchar_t b){ return towlower(a) == towlower(b); }) != hay.end();
}
static int g_ccSearchCount = 0;
static int ccSearchCount() { return g_ccSearchCount; }
// ---- 富文本绘制(自动换行 + 关键词黄底高亮), draw=false 仅算高 ----
struct CcSeg { std::wstring s; bool hl; };
static void ccvSegSplit(const std::wstring& text, const std::wstring& hl, std::vector<CcSeg>& out) {
    if (hl.empty()) { out.push_back({text, false}); return; }
    size_t pos = 0;
    while (pos < text.size()) {
        size_t hit = std::wstring::npos;
        for (size_t i = pos; i + hl.size() <= text.size(); ++i) {
            bool eq = true;
            for (size_t k = 0; k < hl.size(); ++k)
                if (towlower(text[i+k]) != towlower(hl[k])) { eq = false; break; }
            if (eq) { hit = i; break; }
        }
        if (hit == std::wstring::npos) { out.push_back({text.substr(pos), false}); break; }
        if (hit > pos) out.push_back({text.substr(pos, hit - pos), false});
        out.push_back({text.substr(hit, hl.size()), true});
        pos = hit + hl.size();
    }
}
static int ccvRichDraw(HDC dc, const std::wstring& text, const RECT& rc, COLORREF fg,
                       const std::wstring& hl, bool draw) {
    std::vector<CcSeg> segs; ccvSegSplit(text, hl, segs);
    TEXTMETRICW tm; GetTextMetricsW(dc, &tm);
    int lh = tm.tmHeight + 3, x = rc.left, y = rc.top;
    HBRUSH yellow = cachedBrush(RGB(0xFF,0xE2,0x7A));
    for (auto& sg : segs) {
        for (size_t k = 0; k < sg.s.size(); ++k) {
            wchar_t ch = sg.s[k];
            if (ch == L'\n') { x = rc.left; y += lh; continue; }
            SIZE sz; GetTextExtentPoint32W(dc, &ch, 1, &sz);
            if (x + sz.cx > rc.right && x > rc.left) { x = rc.left; y += lh; }
            if (draw) {
                if (sg.hl) {
                    RECT hr = {x, y - 1, x + sz.cx, y + lh - 2};
                    FillRect(dc, &hr, yellow);
                }
                SetTextColor(dc, sg.hl ? RGB(0x30,0x20,0x00) : fg);
                ExtTextOutW(dc, x, y, 0, nullptr, &ch, 1, nullptr);
            }
            x += sz.cx;
        }
    }
    return (y - rc.top) + lh;
}
static void ccvPaint(HWND hWnd, HDC dc, const RECT& rc) {
    HBRUSH bg = cachedBrush(ccvBg());
    FillRect(dc, &rc, bg);
    auto it = g_ccMsgs.find(g_ccCur);
    g_ccHits.clear();
    g_ccSearchCount = 0;
    if (it == g_ccMsgs.end()) return;
    const auto& v = it->second;
    bool searching = !g_ccSearchText.empty();
    int maxW = rc.right - 200; if (maxW < 220) maxW = 220;
    int y = rc.bottom - 8 + g_ccvUp;    // 从最底部向上叠
    SetBkMode(dc, TRANSPARENT);
    for (int i = (int)v.size() - 1; i >= 0; --i) {
        const CcMsg& m = v[i];
        bool mine = ccIEq(m.from, g_currentUser);
        if (searching) {
            if (m.kind == 'S') continue;
            if (!ccContainsI(m.kind == 'F' ? ccvFileLine1(m) : m.body, g_ccSearchText)) continue;
            ++g_ccSearchCount;
        }
        int mh = ccvMsgH(dc, m, maxW);
        y -= mh;
        if (y >= rc.bottom) continue;   // 还在屏幕下方(被滚动推下去)
        if (y + mh <= 0) break;         // 已超出顶部
        if (m.kind != 'S') g_ccHits.push_back({y, y + mh, (size_t)i, mine});
        if (m.kind == 'S') {            // 系统提示: 居中灰字, 自动换行
            HFONT of = (HFONT)SelectObject(dc, hFontSmall);
            SetTextColor(dc, g_darkMode ? RGB(0x88,0x88,0xA0) : RGB(150,150,150));
            RECT r = {rc.left + 40, y + 4, rc.right - 40, y + mh};
            DrawTextW(dc, m.body.c_str(), -1, &r, DT_CENTER | DT_WORDBREAK);
            SelectObject(dc, of);
            continue;
        }
        // 头像 (圆 30x30): 他人在左, 自己在右
        int avX = 12, avR = rc.right - 12 - 30, bubL = 50, bubR = rc.right - 50;
        ccvAvatar(dc, mine ? g_currentUser : m.from, mine ? avR : avX, y + 20, 30);
        // 昵称行 (无时间)
        HFONT of = (HFONT)SelectObject(dc, hFontSmall);
        SetTextColor(dc, ccvNameCol());
        std::wstring nm = mine ? L"我" : m.from;
        RECT nr = mine ? RECT{rc.left, y + 2, bubR - 6, y + 18}
                       : RECT{bubL, y + 2, rc.right, y + 18};
        DrawTextW(dc, nm.c_str(), -1, &nr, (mine ? DT_RIGHT : DT_LEFT) | DT_SINGLELINE);
        SelectObject(dc, of);
        int top = y + 16 + 4;
        if (m.kind == 'E') {            // 表情图片
            int x = mine ? bubR - CCV_EMO : bubL;
            ccvDrawEmoji(dc, m.body.empty() ? L'L' : m.body[0], x, top, CCV_EMO);
            continue;
        }
        bool isF = (m.kind == 'F');
        int bubMaxW = maxW < CCV_MAXBUB ? maxW : CCV_MAXBUB;
        std::wstring l1 = isF ? ccvFileLine1(m) : m.body;
        SelectObject(dc, hFontUI);
        RECT tr = {0, 0, bubMaxW - 28, 0};
        DrawTextW(dc, l1.c_str(), -1, &tr, DT_CALCRECT | DT_WORDBREAK);
        int t1h = tr.bottom - tr.top;
        int t2h = 0;
        RECT tr2 = {0, 0, 0, 0};
        if (isF && !m.note.empty()) {
            SelectObject(dc, hFontSmall);
            tr2 = RECT{0, 0, bubMaxW - 28, 0};
            DrawTextW(dc, m.note.c_str(), -1, &tr2, DT_CALCRECT | DT_WORDBREAK);
            t2h = (tr2.bottom - tr2.top) + 3;
        }
        int bw = (tr.right > tr2.right ? tr.right : tr2.right) + 28;
        if (bw < 96) bw = 96;
        int bh = t1h + t2h + 18 + (mine && !m.readBy.empty() ? 13 : 0);
        RECT bub;
        if (mine) bub = RECT{bubR - bw, top, bubR, top + bh};
        else      bub = RECT{bubL, top, bubL + bw, top + bh};
        HBRUSH bb = cachedBrush(mine ? RGB(149, 236, 105) : ccvOtherBub());
        HGDIOBJ ob = SelectObject(dc, bb);
        HPEN pen = cachedPen(ccvBorder());
        HGDIOBJ op = SelectObject(dc, pen);
        RoundRect(dc, bub.left, bub.top, bub.right, bub.bottom, 10, 10);
        SelectObject(dc, op);
        SelectObject(dc, ob);
        SelectObject(dc, hFontUI);
        RECT tx = {bub.left + 14, bub.top + 8, bub.right - 14, bub.top + 8 + t1h + 4};
        if (isF || !searching)
            DrawTextW(dc, l1.c_str(), -1, &tx, DT_WORDBREAK);
        else
            ccvRichDraw(dc, l1, tx, ccvFg(), g_ccSearchText, true);
        if (isF && !m.note.empty()) {
            SetTextColor(dc, ccvNoteCol());
            SelectObject(dc, hFontSmall);
            RECT tx2 = {bub.left + 14, bub.top + 8 + t1h + 3, bub.right - 14, bub.bottom - 6};
            DrawTextW(dc, m.note.c_str(), -1, &tx2, DT_WORDBREAK);
        }
        if (mine && !m.readBy.empty()) {   // 已读回执
            SetTextColor(dc, ccvNoteCol());
            SelectObject(dc, hFontSmall);
            std::wstring rd = L"已读";
            if (m.readBy.size() == 1) rd = *m.readBy.begin() + L" 已读";
            else rd = std::to_wstring(m.readBy.size()) + L" 人已读";
            RECT rr = {bub.left - 60, bub.bottom - 13, bub.right - 4, bub.bottom};
            DrawTextW(dc, rd.c_str(), -1, &rr, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
        }
        SelectObject(dc, of);
    }
}
static int ccvTotalH(HDC dc, HWND hWnd) {
    RECT rc; GetClientRect(hWnd, &rc);
    int maxW = rc.right - 200; if (maxW < 220) maxW = 220;
    auto it = g_ccMsgs.find(g_ccCur);
    if (it == g_ccMsgs.end()) return 0;
    int total = 16;
    for (auto& m : it->second) {
        if (!g_ccSearchText.empty()) {
            if (m.kind == 'S') continue;
            if (!ccContainsI(m.kind == 'F' ? ccvFileLine1(m) : m.body, g_ccSearchText)) continue;
        }
        total += ccvMsgH(dc, m, maxW);
    }
    return total;
}
// ---------------- 消息撤回 (2 分钟内) ----------------
static void ccDoRevoke(size_t idx) {
    auto it = g_ccMsgs.find(g_ccCur);
    if (it == g_ccMsgs.end() || idx >= it->second.size()) return;
    CcMsg m = it->second[idx];
    if (m.id.empty()) return;
    ccSendRaw("CCREVOKE3|" + ccTeamHex() + "|" + lan::hexEnc(wToUtf8(g_ccCur)) + "|" +
              lan::hexEnc(wToUtf8(m.id)));
    it->second.erase(it->second.begin() + idx);
    ccAppend(g_ccCur, CcMsg{L"", g_currentUser + L" 撤回了一条消息", ccNowHM(), L"", L"", 'S'});
    g_ccvUp = 0;
    InvalidateRect(g_ccView, nullptr, FALSE);
}
static void ccRevokeById(const std::wstring& gid, const std::wstring& id) {
    auto it = g_ccMsgs.find(gid);
    if (it == g_ccMsgs.end() || id.empty()) return;
    std::wstring who;
    for (auto jt = it->second.begin(); jt != it->second.end(); ++jt) {
        if (jt->id == id) { who = jt->from; it->second.erase(jt); break; }
    }
    if (who.empty()) return;
    ccAppend(gid, CcMsg{L"", who + L" 撤回了一条消息", ccNowHM(), L"", L"", 'S'});
    if (gid == g_ccCur) { g_ccvUp = 0; InvalidateRect(g_ccView, nullptr, FALSE); }
}
// ---------------- 已读回执 ----------------
static bool ccReadEligible(const CcGroup& g) {   // 两两群与自建小群显示已读
    return g.mem.size() == 2 || (!g.id.empty() && g.id[0] == L'c');
}
static void ccBroadcastReads() {
    CcGroup* gp = ccFind(g_ccCur);
    if (!gp || !ccReadEligible(*gp)) return;
    auto it = g_ccMsgs.find(g_ccCur);
    if (it == g_ccMsgs.end()) return;
    for (auto& m : it->second) {
        if (m.kind == 'S' || ccIEq(m.from, g_currentUser) || m.id.empty()) continue;
        std::wstring key = g_ccCur + L"|" + m.id;
        if (g_readSent.count(key)) continue;
        g_readSent.insert(key);
        ccSendRaw("CCREAD3|" + ccTeamHex() + "|" + lan::hexEnc(wToUtf8(g_ccCur)) + "|" +
                  lan::hexEnc(wToUtf8(g_currentUser)) + "|" + lan::hexEnc(wToUtf8(m.id)));
    }
}
static void ccOnRead(const std::wstring& gid, const std::wstring& reader, const std::wstring& id) {
    CcGroup* gp = ccFind(gid);
    if (!gp || !ccInGroup(*gp, reader)) return;
    auto it = g_ccMsgs.find(gid);
    if (it == g_ccMsgs.end()) return;
    for (auto& m : it->second)
        if (m.id == id) { m.readBy.insert(reader); break; }
    if (gid == g_ccCur && g_ccView) InvalidateRect(g_ccView, nullptr, FALSE);
}
static LRESULT CALLBACK CcMsgViewProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_RBUTTONUP: {            // 右键自己 2 分钟内的消息 -> 撤回
        int mx = (int)(short)LOWORD(lParam), my = (int)(short)HIWORD(lParam);
        int pick = -1;
        for (size_t k = 0; k < g_ccHits.size(); ++k)
            if (g_ccHits[k].mine && mx >= 0 && my >= g_ccHits[k].top && my < g_ccHits[k].bottom)
                pick = (int)k;
        if (pick < 0) break;
        auto it = g_ccMsgs.find(g_ccCur);
        if (it == g_ccMsgs.end()) break;
        const CcMsg& mm = it->second[g_ccHits[pick].idx];
        if (mm.kind == 'S' || mm.id.empty() ||
            GetTickCount64() - mm.sendTime > 120000) break;
        HMENU pm = CreatePopupMenu();
        AppendMenuW(pm, MF_STRING, 6201, L"撤回");
        POINT pt = {mx, my}; ClientToScreen(hWnd, &pt);
        int cmd = TrackPopupMenu(pm, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                 pt.x, pt.y, 0, hWnd, nullptr);
        DestroyMenu(pm);
        if (cmd == 6201) ccDoRevoke(g_ccHits[pick].idx);
        return 0;
    }
    case WM_MOUSEWHEEL: {
        g_ccvUp -= GET_WHEEL_DELTA_WPARAM(wParam) / 2;   // 滚一格60px
        HDC dc = GetDC(hWnd);
        RECT rc; GetClientRect(hWnd, &rc);
        int maxUp = ccvTotalH(dc, hWnd) - rc.bottom;
        ReleaseDC(hWnd, dc);
        if (maxUp < 0) maxUp = 0;
        if (g_ccvUp < 0) g_ccvUp = 0;
        if (g_ccvUp > maxUp) g_ccvUp = maxUp;
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    }
    case WM_SIZE: InvalidateRect(hWnd, nullptr, FALSE); return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hWnd, &ps);
        RECT rc; GetClientRect(hWnd, &rc);
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
        HGDIOBJ ob = SelectObject(mem, bmp);
        ccvPaint(hWnd, mem, rc);
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, ob); DeleteObject(bmp); DeleteDC(mem);
        EndPaint(hWnd, &ps);
        return 0;
    }
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}
static void ccRegisterMsgView(HINSTANCE inst) {
    static bool done = false;
    if (done) return;
    done = true;
    WNDCLASSW wc = {};
    wc.lpfnWndProc = CcMsgViewProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"CCMsgView";
    RegisterClassW(&wc);
}

// ---------------- 炸弹特效窗口 ----------------
static LRESULT CALLBACK BombProc(HWND hWnd, UINT msg, WPARAM, LPARAM) {
    switch (msg) {
    case WM_CREATE: SetTimer(hWnd, 1, 30, nullptr); return 0;
    case WM_TIMER: {
        int* fr = (int*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
        int f = fr ? *fr + 1 : 1;
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)(fr ? fr : new int(0)));
        if (fr) *fr = f;
        if (f > 38) { DestroyWindow(hWnd); return 0; }
        InvalidateRect(hWnd, nullptr, TRUE);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(hWnd, &ps);
        RECT rc; GetClientRect(hWnd, &rc);
        int f = *(int*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
        HBRUSH bg = CreateSolidBrush(RGB(20, 20, 24));
        FillRect(dc, &rc, bg); DeleteObject(bg);
        int cx = rc.right / 2, cy = rc.bottom / 2;
        if (f < 5) {   // 白闪
            HBRUSH wb = CreateSolidBrush(RGB(255, 255 - f * 30, 180));
            FillRect(dc, &rc, wb); DeleteObject(wb);
        }
        int r = f * 9;
        for (int k = 0; k < 6; ++k) {
            int rr = r - k * 12; if (rr < 4) continue;
            COLORREF co = k % 2 ? RGB(255, 120, 0) : RGB(255, 210, 40);
            HBRUSH b = CreateSolidBrush(co);
            HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
            HPEN pen = CreatePen(PS_SOLID, 6, co);
            HGDIOBJ op = SelectObject(dc, pen);
            Ellipse(dc, cx - rr, cy - rr, cx + rr, cy + rr);
            SelectObject(dc, op); SelectObject(dc, ob);
            DeleteObject(pen); DeleteObject(b);
        }
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 230, 80));
        SelectObject(dc, hFontTitle);
        RECT tr = {0, cy - 24, rc.right, cy + 24};
        DrawTextW(dc, L"BOOM!", -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_DESTROY: {
        int* fr = (int*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
        delete fr; KillTimer(hWnd, 1); return 0;
    }
    }
    return DefWindowProcW(hWnd, msg, 0, 0);
}
static void ccPlayBomb() {
    static bool reg = false;
    if (!reg) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = BombProc; wc.hInstance = g_hInst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        wc.lpszClassName = L"CCBombClass";
        RegisterClassW(&wc); reg = true;
    }
    int sx = GetSystemMetrics(SM_CXSCREEN), sy = GetSystemMetrics(SM_CYSCREEN);
    int x = 100 + (rand() % (sx - 520)), y = 80 + (rand() % (sy - 380));
    HWND b = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"CCBombClass", L"",
        WS_POPUP | WS_VISIBLE, x, y, 420, 300, nullptr, nullptr, g_hInst, nullptr);
    SetWindowLongPtrW(b, GWLP_USERDATA, (LONG_PTR)new int(0));
    SetForegroundWindow(b);
    MessageBeep(MB_ICONEXCLAMATION);
}

// ---------------- 摄像头常驻捕获 (VFW, 画中画叠加到屏幕流) ----------------
// 原理: 独立线程建隐藏 VFW 捕获窗 + 帧回调, 把最新帧存为 DIB; 截屏时叠加右下角
#include <vfw.h>
static std::mutex g_camMtx;
static std::vector<char> g_camDib;              // 最新摄像头帧 (packed DIB, 无文件头)
static std::atomic<int>  g_camViewers{0};       // >0 时才打开摄像头; 0 时释放设备
static std::atomic<bool> g_camOk{false};
static std::atomic<long> g_camSeq{0};   // 帧序号: 每存入一帧 +1, 发送线程据此判断有无新帧
// 采集回调存帧: 换入最新帧并递增序号 (慢网络下自动丢旧帧, 不排队积压)
static void ccCamStore(std::vector<char>&& buf) {
    { std::lock_guard<std::mutex> lk(g_camMtx); g_camDib.swap(buf); }
    ++g_camSeq;
}
// YUY2 -> 24bit RGB (BT.601), 现代摄像头驱动经常只输出 YUY2, 直接渲染就是黑屏
static void ccYuy2ToRgb(const BYTE* src, BYTE* dst, int w, int h) {
    for (int i = 0; i < w * h / 2; ++i) {
        int y0 = src[0], u = src[1] - 128, y1 = src[2], v = src[3] - 128;
        src += 4;
        int c0 = y0 - 16, c1 = y1 - 16; if (c0 < 0) c0 = 0; if (c1 < 0) c1 = 0;
        c0 *= 298; c1 *= 298;
        int r0 = (c0 + 409 * v + 128) >> 8, g0 = (c0 - 100 * u - 208 * v + 128) >> 8, b0 = (c0 + 516 * u + 128) >> 8;
        int r1 = (c1 + 409 * v + 128) >> 8, g1 = (c1 - 100 * u - 208 * v + 128) >> 8, b1 = (c1 + 516 * u + 128) >> 8;
        dst[0] = (BYTE)(b0 < 0 ? 0 : b0 > 255 ? 255 : b0);
        dst[1] = (BYTE)(g0 < 0 ? 0 : g0 > 255 ? 255 : g0);
        dst[2] = (BYTE)(r0 < 0 ? 0 : r0 > 255 ? 255 : r0);
        dst[3] = (BYTE)(b1 < 0 ? 0 : b1 > 255 ? 255 : b1);
        dst[4] = (BYTE)(g1 < 0 ? 0 : g1 > 255 ? 255 : g1);
        dst[5] = (BYTE)(r1 < 0 ? 0 : r1 > 255 ? 255 : r1);
        dst += 6;
    }
}
// 连接时缓存格式, 回调里不再调 capGetVideoFormat (缓冲尺寸不足会失败)
static BITMAPINFOHEADER g_camFmt = {};
static LRESULT CALLBACK ccCamFrameCb(HWND hWnd, LPVIDEOHDR lpVHdr) {
    (void)hWnd;
    if (!lpVHdr || !lpVHdr->lpData) return 0;
    size_t used = lpVHdr->dwBytesUsed ? lpVHdr->dwBytesUsed : lpVHdr->dwBufferLength;
    if (used < 64) return 0;
    BITMAPINFOHEADER bh = g_camFmt;
    int w = bh.biWidth, h = bh.biHeight; if (h < 0) h = -h;
    if (w <= 0 || h <= 0) return 0;
    DWORD comp = bh.biCompression;
    if (comp == mmioFOURCC('M','J','P','G')) {
        // 驱动直出 JPEG: 原样存储, GDI+ 可直接解码
        std::vector<char> buf(used);
        memcpy(buf.data(), lpVHdr->lpData, used);
        ccCamStore(std::move(buf));
        g_camFmt.biCompression = comp;   // 保持 MJPG 标记
        g_camOk = true;
        return 0;
    }
    if (comp == BI_RGB && (bh.biBitCount == 24 || bh.biBitCount == 32)) {
        size_t expect = (size_t)((w * bh.biBitCount / 8 + 3) & ~3) * h;
        if (used < expect) expect = used;
        std::vector<char> buf(sizeof(BITMAPINFOHEADER) + expect);
        BITMAPINFOHEADER nh = bh; nh.biSize = sizeof(nh);
        nh.biHeight = h; nh.biCompression = BI_RGB; nh.biSizeImage = (DWORD)expect;
        memcpy(buf.data(), &nh, sizeof(nh));
        memcpy(buf.data() + sizeof(nh), lpVHdr->lpData, expect);
        ccCamStore(std::move(buf));
        g_camOk = true;
    } else if (comp == mmioFOURCC('Y','U','Y','2') ||
               comp == mmioFOURCC('Y','U','Y','V') ||
               comp == mmioFOURCC('Y','U','Y','S')) {
        size_t imgSz = (size_t)w * h * 3;
        std::vector<char> buf(sizeof(BITMAPINFOHEADER) + imgSz);
        BITMAPINFOHEADER nh = {};
        nh.biSize = sizeof(nh); nh.biWidth = w; nh.biHeight = h;
        nh.biPlanes = 1; nh.biBitCount = 24; nh.biCompression = BI_RGB;
        nh.biSizeImage = (DWORD)imgSz;
        memcpy(buf.data(), &nh, sizeof(nh));
        ccYuy2ToRgb((const BYTE*)lpVHdr->lpData, (BYTE*)buf.data() + sizeof(nh), w, h);
        ccCamStore(std::move(buf));
        g_camOk = true;
    }
    return 0;
}
// 摄像头捕获线程: 仅在有监视者(g_camViewers>0)时连接设备, 无人看时断开释放
static void ccCamThread() {
    HWND hCap = capCreateCaptureWindowW(L"cccam", WS_CHILD | WS_CLIPSIBLINGS,
        0, 0, 320, 240, GetDesktopWindow(), 0);
    if (!hCap) return;
    bool linked = false;
    for (;;) {
        if (g_camViewers.load() > 0 && !linked) {
            if (capDriverConnect(hCap, 0)) {
                // 缓存视频格式, 回调直接用, 避免回调内 capGetVideoFormat 缓冲不足失败
                DWORD fsz = capGetVideoFormatSize(hCap);
                memset(&g_camFmt, 0, sizeof(g_camFmt));
                if (fsz >= sizeof(BITMAPINFOHEADER)) {
                    std::vector<char> fb(fsz);
                    if (capGetVideoFormat(hCap, fb.data(), fsz))
                        memcpy(&g_camFmt, fb.data(), sizeof(g_camFmt));
                }
                // 尝试协商 640x480 (默认可能只有 320x240, 画面模糊)
                if (g_camFmt.biSize >= sizeof(BITMAPINFOHEADER) && g_camFmt.biWidth < 640) {
                    BITMAPINFOHEADER want = g_camFmt;
                    want.biWidth = 640; want.biHeight = 480;
                    want.biSizeImage = 0;
                    capSetVideoFormat(hCap, &want, sizeof(want));   // 失败则保持默认, 不致命
                    DWORD fsz2 = capGetVideoFormatSize(hCap);
                    if (fsz2 >= sizeof(BITMAPINFOHEADER)) {
                        std::vector<char> fb2(fsz2);
                        if (capGetVideoFormat(hCap, fb2.data(), fsz2))
                            memcpy(&g_camFmt, fb2.data(), sizeof(g_camFmt));
                    }
                }
                // 用流式捕获回调: 隐藏窗口 preview 模式多数驱动不出帧
                capSetCallbackOnVideoStream(hCap, ccCamFrameCb);
                capCaptureSequenceNoFile(hCap);
                linked = true;
            } else Sleep(2000);              // 无摄像头: 重试间隔, 不打扰
        } else if (g_camViewers.load() <= 0 && linked) {
            capCaptureStop(hCap);
            capCaptureAbort(hCap);
            capSetCallbackOnVideoStream(hCap, nullptr);
            capDriverDisconnect(hCap);
            linked = false; g_camOk = false;
            std::lock_guard<std::mutex> lk(g_camMtx); g_camDib.clear();
        }
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg); DispatchMessageW(&msg);
        }
        Sleep(60);
    }
}
static void ccCamStart() {
    static bool started = false;
    if (started) return; started = true;
    std::thread(ccCamThread).detach();
}
static int ccJpegClsid(CLSID* p);   // 前向声明: 定义在下方屏幕服务段
// DIB -> JPEG
static bool ccCamJpeg(const std::vector<char>& dib, std::string& out) {
    if (dib.size() < 8) return false;
    // MJPG 直通: 数据本身就是 JPEG (0xFFD8 开头), 无需再编码
    if ((unsigned char)dib[0] == 0xFF && (unsigned char)dib[1] == 0xD8) {
        out.assign(dib.data(), dib.size());
        return true;
    }
    if (dib.size() < sizeof(BITMAPINFOHEADER)) return false;
    BITMAPINFO* p = (BITMAPINFO*)dib.data();
    size_t hdrSz = sizeof(BITMAPINFOHEADER);
    if (p->bmiHeader.biBitCount <= 8)
        hdrSz += ((size_t)1 << p->bmiHeader.biBitCount) * sizeof(RGBQUAD);
    if (dib.size() < hdrSz + p->bmiHeader.biSizeImage) return false;
    Gdiplus::Bitmap g(p, const_cast<char*>(dib.data() + hdrSz));
    CLSID clsid;
    if (ccJpegClsid(&clsid) != 0) return false;
    IStream* stm = nullptr;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stm))) return false;
    Gdiplus::EncoderParameters ep; ep.Count = 1;
    ULONG q = 70;   // 摄像头清晰度 55->70
    ep.Parameter[0].Guid = Gdiplus::EncoderQuality;
    ep.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
    ep.Parameter[0].NumberOfValues = 1;
    ep.Parameter[0].Value = &q;
    bool ok = false;
    if (g.Save(stm, &clsid, &ep) == Gdiplus::Ok) {
        HGLOBAL hg = nullptr;
        if (SUCCEEDED(GetHGlobalFromStream(stm, &hg))) {
            SIZE_T n2 = GlobalSize(hg); void* gp = GlobalLock(hg);
            out.assign((const char*)gp, n2); GlobalUnlock(hg); ok = true;
        }
    }
    stm->Release();
    return ok;
}

// ---------------- 屏幕 JPEG 抓取 / 直播服务 ----------------
static int ccJpegClsid(CLSID* p) {
    UINT n = 0, sz = 0;
    Gdiplus::GetImageEncodersSize(&n, &sz);
    std::vector<BYTE> b(sz);
    Gdiplus::ImageCodecInfo* ci = (Gdiplus::ImageCodecInfo*)b.data();
    Gdiplus::GetImageEncoders(n, sz, ci);
    for (UINT i = 0; i < n; ++i)
        if (wcscmp(ci[i].MimeType, L"image/jpeg") == 0) { *p = ci[i].Clsid; return 0; }
    return -1;
}
static bool ccCaptureJpeg(std::string& out) {
    int w = GetSystemMetrics(SM_CXSCREEN), h = GetSystemMetrics(SM_CYSCREEN);
    HDC scr = GetDC(nullptr);
    HDC mc = CreateCompatibleDC(scr);
    HBITMAP bmp = CreateCompatibleBitmap(scr, w, h);
    HGDIOBJ oldo = SelectObject(mc, bmp);
    BitBlt(mc, 0, 0, w, h, scr, 0, 0, SRCCOPY | CAPTUREBLT);
    SelectObject(mc, oldo);
    bool ok = false;
    {
        Gdiplus::Bitmap g(bmp, (HPALETTE)nullptr);
        CLSID clsid;
        if (ccJpegClsid(&clsid) == 0) {
            IStream* stm = nullptr;
            if (SUCCEEDED(CreateStreamOnHGlobal(nullptr, TRUE, &stm))) {
                Gdiplus::EncoderParameters ep; ep.Count = 1;
                ULONG q = 55;        // 清晰度优先: 30->55
                ep.Parameter[0].Guid = Gdiplus::EncoderQuality;
                ep.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
                ep.Parameter[0].NumberOfValues = 1;
                ep.Parameter[0].Value = &q;
                if (g.Save(stm, &clsid, &ep) == Gdiplus::Ok) {
                    HGLOBAL hg = nullptr;
                    if (SUCCEEDED(GetHGlobalFromStream(stm, &hg))) {
                        SIZE_T n2 = GlobalSize(hg); void* p = GlobalLock(hg);
                        out.assign((const char*)p, n2); GlobalUnlock(hg); ok = true;
                    }
                }
                stm->Release();
            }
        }
    }
    SelectObject(mc, oldo);
    DeleteObject(bmp); DeleteDC(mc); ReleaseDC(nullptr, scr);
    return ok;
}
static bool ccSendAll(SOCKET s, const char* p, int len) {
    while (len > 0) {
        int n = send(s, p, len, 0);
        if (n <= 0) return false;
        p += n; len -= n;
    }
    return true;
}
static bool ccRecvAll(SOCKET s, char* p, int len) {
    while (len > 0) {
        int n = recv(s, p, len, 0);
        if (n <= 0) return false;
        p += n; len -= n;
    }
    return true;
}
static std::atomic<bool> g_scOn{false};
// ---- 远程操控: 监视端经屏幕连接反向发输入包, 本端执行 ----
// 包格式: "CCIN"(4) | type(1) | a(int32 LE) | b(int32 LE) = 13 字节
// type: 1=移动 2=左键下 3=左键上 4=右键下 5=右键上 6=键下 7=键上 8=滚轮
// 鼠标坐标为归一化 0..10000, 由执行端换算到实际分辨率
static void ccInputReaderThread(SOCKET c) {
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    for (;;) {
        char p[13];
        if (!ccRecvAll(c, p, 13) || memcmp(p, "CCIN", 4) != 0) break;
        unsigned char t = (unsigned char)p[4];
        int32_t a, b; memcpy(&a, p + 5, 4); memcpy(&b, p + 9, 4);
        INPUT in; memset(&in, 0, sizeof(in));
        if (t >= 1 && t <= 5) {
            long x = (long)((__int64)a * sw / 10000);
            long y = (long)((__int64)b * sh / 10000);
            in.type = INPUT_MOUSE;
            in.mi.dx = x * 65535 / sw; in.mi.dy = y * 65535 / sh;
            in.mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE;
            if (t == 2) in.mi.dwFlags |= MOUSEEVENTF_LEFTDOWN;
            if (t == 3) in.mi.dwFlags |= MOUSEEVENTF_LEFTUP;
            if (t == 4) in.mi.dwFlags |= MOUSEEVENTF_RIGHTDOWN;
            if (t == 5) in.mi.dwFlags |= MOUSEEVENTF_RIGHTUP;
            SendInput(1, &in, sizeof(in));
        } else if (t == 6 || t == 7) {
            in.type = INPUT_KEYBOARD;
            in.ki.wVk = (WORD)a;
            if (t == 7) in.ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(1, &in, sizeof(in));
        } else if (t == 8) {
            in.type = INPUT_MOUSE;
            in.mi.dwFlags = MOUSEEVENTF_WHEEL; in.mi.mouseData = (DWORD)a;
            SendInput(1, &in, sizeof(in));
        }
    }
}
static void ccScreenClientThread(SOCKET c) {
    std::thread(ccInputReaderThread, c).detach();   // 反向输入通道
    int fails = 0;
    while (g_scOn.load() && fails < 25) {
        std::string jpg;
        if (!ccCaptureJpeg(jpg)) { Sleep(100); ++fails; continue; }
        uint32_t L = (uint32_t)jpg.size();
        char hdr[8]; memcpy(hdr, "CCSC", 4); memcpy(hdr + 4, &L, 4);
        if (!ccSendAll(c, hdr, 8) || !ccSendAll(c, jpg.data(), (int)L)) break;
        Sleep(15);        // 目标 60Hz: 编码耗时任其占满, 间隔只留 15ms 抬满刷新
    }
    closesocket(c);
}
static void ccScreenServeThread() {
    SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    BOOL yes = TRUE; setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
    sockaddr_in a; memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_ANY); a.sin_port = htons(CC_SCREEN_PORT);
    if (bind(ls, (sockaddr*)&a, sizeof(a)) != 0 || listen(ls, 8) != 0) {
        closesocket(ls);
        g_scOn = false;   // bind 失败必须复位, 否则服务假死、共享黑屏
        return;
    }
    while (g_scOn.load()) {
        fd_set rf; FD_ZERO(&rf); FD_SET(ls, &rf);
        timeval tv{0, 250000};
        if (select((int)ls + 1, &rf, nullptr, nullptr, &tv) > 0) {
            SOCKET c = accept(ls, nullptr, nullptr);
            if (c != INVALID_SOCKET) std::thread(ccScreenClientThread, c).detach();
        }
    }
    closesocket(ls);
}
static void ccStartServe() {
    bool f = false;
    if (g_scOn.compare_exchange_strong(f, true))
        std::thread(ccScreenServeThread).detach();
}
static void ccStopServe() { g_scOn = false; }

// ---------------- 摄像头直播服务 (TCP 47836, 有人连才开摄像头, 断开即关) ----------------
static std::atomic<bool> g_camSrvOn{false};
static void ccCamClientThread(SOCKET c) {
    ++g_camViewers;
    BOOL nd = TRUE; setsockopt(c, IPPROTO_TCP, TCP_NODELAY, (const char*)&nd, sizeof(nd));   // 禁 Nagle: 消除每帧 ~200ms 延迟ACK停顿
    long lastSeq = 0;
    int fails = 0;
    while (g_camSrvOn.load() && fails < 75) {   // 首帧需等摄像头驱动打开, 容忍 ~6s
        long seq = g_camSeq.load();
        if (!seq || seq == lastSeq) { Sleep(80); ++fails; continue; }   // 无新帧: 不重复拷贝/编码
        std::vector<char> dib;
        { std::lock_guard<std::mutex> lk(g_camMtx); dib = g_camDib; }   // 锁内只拷贝
        std::string jpg;
        if (dib.empty() || !ccCamJpeg(dib, jpg)) { Sleep(150); ++fails; continue; }
        fails = 0; lastSeq = seq;
        std::string pkt;                       // 包头+包体一次发送 (两次 send 会触发 Nagle 停顿)
        pkt.reserve(8 + jpg.size());
        char hdr[8]; uint32_t L = (uint32_t)jpg.size();
        memcpy(hdr, "CCSC", 4); memcpy(hdr + 4, &L, 4);
        pkt.append(hdr, 8); pkt.append(jpg);
        if (!ccSendAll(c, pkt.data(), (int)pkt.size())) break;
        Sleep(80);    // 上限 ~12fps, 只在有新帧时发送
    }
    --g_camViewers;    // 关闭后摄像头也关闭(计数归零时捕获线程自动断开设备)
    closesocket(c);
}
static void ccCamServeThread() {
    SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    BOOL yes = TRUE; setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
    sockaddr_in a; memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_ANY); a.sin_port = htons(CC_CAM_PORT);
    if (bind(ls, (sockaddr*)&a, sizeof(a)) != 0 || listen(ls, 8) != 0) {
        closesocket(ls); g_camSrvOn = false; return;
    }
    while (g_camSrvOn.load()) {
        fd_set rf; FD_ZERO(&rf); FD_SET(ls, &rf);
        timeval tv{0, 250000};
        if (select((int)ls + 1, &rf, nullptr, nullptr, &tv) > 0) {
            SOCKET c = accept(ls, nullptr, nullptr);
            if (c != INVALID_SOCKET) std::thread(ccCamClientThread, c).detach();
        }
    }
    closesocket(ls);
}
static void ccCamServeStart() {
    bool f = false;
    if (g_camSrvOn.compare_exchange_strong(f, true))
        std::thread(ccCamServeThread).detach();
}

// ---------------- 屏幕查看窗口 ----------------
static std::mutex g_vwMtx;
static HBITMAP g_vwHbm = nullptr;   // 解码好的 DIB 帧(网络线程产出), 绘制走 GDI 快速路径
static SOCKET g_vwSock = INVALID_SOCKET;
static HWND g_vwWnd = nullptr;
static HWND g_vwEdit = nullptr;     // 顶部"打开网页"输入框(仅可操控时存在)
static long g_vwGen = 0;
static std::wstring g_vwWho;   // 当前 viewer 正在观看的对象, 避免重复开窗
static void ccSendCmd(const std::wstring& target, const std::string& act,
                      const std::wstring& extra = L"");   // 定义在"远程指令"一节

static void ccViewerThread(long gen, unsigned long ip, int port) {
    SOCKET sk = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in a; memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET; a.sin_port = htons((u_short)port);
    a.sin_addr.s_addr = htonl(ip);   // g_who 存的是主机序 IP
    bool connected = false;
    for (int i = 0; i < 20 && gen == g_vwGen; ++i) {
        if (connect(sk, (sockaddr*)&a, sizeof(a)) == 0) { connected = true; break; }
        Sleep(300);
    }
    if (!connected) { closesocket(sk); return; }
    { std::lock_guard<std::mutex> lk(g_vwMtx);
      if (gen == g_vwGen) g_vwSock = sk; else { closesocket(sk); return; } }
    while (gen == g_vwGen) {
        char hdr[8];
        if (!ccRecvAll(sk, hdr, 8) || memcmp(hdr, "CCSC", 4) != 0) break;
        uint32_t L; memcpy(&L, hdr + 4, 4);
        if (L == 0 || L > 5 * 1024 * 1024) break;
        std::vector<char> buf(L);
        if (!ccRecvAll(sk, buf.data(), (int)L)) break;
        HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, L);
        memcpy(GlobalLock(hg), buf.data(), L); GlobalUnlock(hg);
        IStream* stm = nullptr;
        HBITMAP fresh = nullptr;
        if (SUCCEEDED(CreateStreamOnHGlobal(hg, FALSE, &stm))) {
            Gdiplus::Bitmap* src = Gdiplus::Bitmap::FromStream(stm);
            if (src && src->GetLastStatus() == Gdiplus::Ok)
                src->GetHBITMAP(Gdiplus::Color(0, 0, 0, 0), &fresh);   // 每帧只解码一次, 绘制走 GDI
            delete src; stm->Release();
        }
        GlobalFree(hg);
        if (fresh) {
            HBITMAP old = nullptr;
            { std::lock_guard<std::mutex> lk(g_vwMtx); old = g_vwHbm; g_vwHbm = fresh; }
            if (old) DeleteObject(old);
            if (g_vwWnd) PostMessageW(g_vwWnd, WM_APP_FRAME, 0, 0);
        }
    }
    bool mine = false;
    { std::lock_guard<std::mutex> lk(g_vwMtx); if (g_vwSock == sk) { g_vwSock = INVALID_SOCKET; mine = true; } }
    if (mine) closesocket(sk);
}
// 管理员远程操控: 把 viewer 窗口内的鼠标/键盘事件发给被控端执行
static bool g_vwCtrl = false;   // 当前 viewer 是否启用操控(仅管理员监视屏幕)
static bool g_vwIsScreen = false; // 当前 viewer 是否为屏幕监视(决定顶部快捷栏显示, 单机测试也显示)
static int ccVwTbH() { return g_vwIsScreen ? 44 : 0; }   // 顶部快捷栏高度
static std::wstring ccVwTargetName() {               // 从观看键还原目标姓名
    std::wstring who = g_vwWho;
    size_t p = who.find(L"|cam");
    if (p != std::wstring::npos) who = who.substr(0, p);
    return who;
}
// 顶部快捷功能: 音量-/音量+/打开网页 (CCCMD3 广播, 对方执行)
static void ccVwQuickCmd(const wchar_t* act) {
    std::wstring who = ccVwTargetName();
    if (who.empty()) return;
    ccSendCmd(who, wToUtf8(act));
}
static void ccVwOpenWeb(HWND hWnd) {
    if (!g_vwEdit) return;
    wchar_t buf[512] = {0};
    GetWindowTextW(g_vwEdit, buf, 512);
    std::wstring u = buf;
    size_t a = u.find_first_not_of(L" \t");
    if (a == std::wstring::npos) return;
    u = u.substr(a);
    if (u.find(L"://") == std::wstring::npos) u = L"https://" + u;
    std::wstring who = ccVwTargetName();
    if (who.empty()) return;
    ccSendCmd(who, "OPENWEB", u);
    SetWindowTextW(g_vwEdit, L"");
}
static void ccVwUpdateWebBtn(HWND hWnd) {   // 按钮文案随输入变化: 打开【xx】网页
    HWND btn = GetDlgItem(hWnd, 911);
    if (!btn) return;
    wchar_t buf[64] = {0};
    if (g_vwEdit) GetWindowTextW(g_vwEdit, buf, 64);
    std::wstring t = buf, name = L"网页";
    size_t a = t.find_first_not_of(L" \t");
    if (a != std::wstring::npos) {
        name = t.substr(a);
        if (name.size() > 12) name = name.substr(0, 12) + L"…";
    }
    SetWindowTextW(btn, (L"打开【" + name + L"】网页").c_str());
}
static void ccSendInput(unsigned char t, int32_t a, int32_t b) {
    char p[13]; memcpy(p, "CCIN", 4); p[4] = (char)t;
    memcpy(p + 5, &a, 4); memcpy(p + 9, &b, 4);
    SOCKET s;
    { std::lock_guard<std::mutex> lk(g_vwMtx); s = g_vwSock; }
    if (s != INVALID_SOCKET) send(s, p, 13, 0);
}
// 本机 IP 检测: 同机双开测试时远程操控会拖走自己的光标, 必须禁用
static bool ccIsLocalIp(unsigned long ip) {   // ip 为主机序
    static std::vector<unsigned long> locals; static bool init = false;
    if (!init) {
        init = true;
        locals.push_back(0x7F000001);   // 127.0.0.1
        char hn[256];
        if (gethostname(hn, sizeof(hn)) == 0) {
            hostent* he = gethostbyname(hn);
            if (he) for (int i = 0; he->h_addr_list[i]; ++i)
                locals.push_back(ntohl(((in_addr*)he->h_addr_list[i])->s_addr));
        }
    }
    for (unsigned long v : locals) if (v == ip) return true;
    return false;
}
static void ccVwMouse(HWND hWnd, LPARAM lp, unsigned char t) {
    if (!g_vwCtrl) return;
    if (t == 1) {   // 移动事件 40ms 节流, 避免刷屏
        static DWORD last = 0;
        DWORD now = GetTickCount();
        if (now - last < 40) return;
        last = now;
    }
    RECT rc; GetClientRect(hWnd, &rc);
    int tb = ccVwTbH();
    int x = (short)LOWORD(lp), y = (short)HIWORD(lp) - tb;
    if (y < 0 || rc.right <= 0 || rc.bottom <= tb) return;
    ccSendInput(t, (int32_t)((__int64)x * 10000 / rc.right),
                   (int32_t)((__int64)y * 10000 / (rc.bottom - tb)));
}
static LRESULT CALLBACK CcViewerProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_APP_FRAME: InvalidateRect(hWnd, nullptr, FALSE); return 0;
    case WM_MOUSEMOVE:  ccVwMouse(hWnd, lParam, 1); return 0;
    case WM_LBUTTONDOWN: SetFocus(hWnd); ccVwMouse(hWnd, lParam, 2); SetCapture(hWnd); return 0;
    case WM_LBUTTONUP:   ccVwMouse(hWnd, lParam, 3); ReleaseCapture(); return 0;
    case WM_RBUTTONDOWN: ccVwMouse(hWnd, lParam, 4); return 0;
    case WM_RBUTTONUP:   ccVwMouse(hWnd, lParam, 5); return 0;
    case WM_KEYDOWN:
        if (g_vwCtrl) ccSendInput(6, (int32_t)wParam, 0);
        return 0;
    case WM_KEYUP:   if (g_vwCtrl) ccSendInput(7, (int32_t)wParam, 0); return 0;
    case WM_MOUSEWHEEL:
        if (g_vwCtrl) ccSendInput(8, (int32_t)(short)HIWORD(wParam), 0);
        return 0;
    case WM_COMMAND: {
        WORD id = LOWORD(wParam), ev = HIWORD(wParam);
        if (!g_vwIsScreen) return 0;
        if (id == 914) ccVwQuickCmd(L"VOLDOWN");
        else if (id == 915) ccVwQuickCmd(L"VOLUP");
        else if (id == 916) ccVwQuickCmd(L"VOLMAX");
        else if (id == 911) ccVwOpenWeb(hWnd);
        else if (id == 910 && ev == EN_CHANGE) ccVwUpdateWebBtn(hWnd);
        return 0;
    }
    case WM_SIZE: {
        if (!g_vwIsScreen || !g_vwEdit) return 0;
        int w = LOWORD(lParam);
        MoveWindow(g_vwEdit, 220, 8, w - 220 - 156, 28, TRUE);
        HWND b;
        if ((b = GetDlgItem(hWnd, 911))) MoveWindow(b, w - 152, 7, 140, 30, TRUE);
        if ((b = GetDlgItem(hWnd, 914))) MoveWindow(b, 8, 7, 56, 30, TRUE);
        if ((b = GetDlgItem(hWnd, 915))) MoveWindow(b, 68, 7, 56, 30, TRUE);
        if ((b = GetDlgItem(hWnd, 916))) MoveWindow(b, 128, 7, 72, 30, TRUE);
        return 0;
    }
    case WM_ERASEBKGND: { RECT rc; GetClientRect(hWnd, &rc);
        HBRUSH b = CreateSolidBrush(RGB(0, 0, 0)); FillRect((HDC)wParam, &rc, b);
        DeleteObject(b); return 1; }
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(hWnd, &ps);
        RECT rc; GetClientRect(hWnd, &rc);
        int tb = ccVwTbH();
        if (tb) {   // 顶部快捷栏底色
            RECT tr = {0, 0, rc.right, tb};
            HBRUSH b = CreateSolidBrush(RGB(45, 45, 48));
            FillRect(dc, &tr, b); DeleteObject(b);
        }
        HBITMAP hbm = nullptr;
        { std::lock_guard<std::mutex> lk(g_vwMtx); hbm = g_vwHbm; }
        if (hbm && rc.bottom > tb) {
            HDC mdc = CreateCompatibleDC(dc);
            HGDIOBJ ob = SelectObject(mdc, hbm);
            BITMAP bm; GetObjectW(hbm, sizeof(bm), &bm);
            SetStretchBltMode(dc, COLORONCOLOR);   // GDI 硬件路径拉伸, 全屏不再卡顿
            StretchBlt(dc, 0, tb, rc.right, rc.bottom - tb, mdc, 0, 0,
                       bm.bmWidth, bm.bmHeight, SRCCOPY);
            SelectObject(mdc, ob); DeleteDC(mdc);
        } else if (rc.bottom > tb) {
            RECT ir = {0, tb, rc.right, rc.bottom};
            FillRect(dc, &ir, (HBRUSH)GetStockObject(BLACK_BRUSH));
        }
        EndPaint(hWnd, &ps); return 0;
    }
    case WM_DESTROY: {
        ++g_vwGen;
        std::lock_guard<std::mutex> lk(g_vwMtx);
        if (g_vwSock != INVALID_SOCKET) { closesocket(g_vwSock); g_vwSock = INVALID_SOCKET; }
        if (g_vwHbm) { DeleteObject(g_vwHbm); g_vwHbm = nullptr; }
        g_vwEdit = nullptr; g_vwWnd = nullptr;
        g_vwWho.clear();
        return 0;
    }
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}
// port: CC_SCREEN_PORT=屏幕, CC_CAM_PORT=摄像头; 界面相同, 仅数据源不同
static void ccOpenViewer(const std::wstring& who, unsigned long ip,
                         int port = CC_SCREEN_PORT) {
    static bool reg = false;
    if (!reg) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = CcViewerProc; wc.hInstance = g_hInst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        wc.lpszClassName = L"CCViewerClass";
        RegisterClassW(&wc); reg = true;
    }
    std::wstring key = who + (port == CC_CAM_PORT ? L"|cam" : L"");
    if (g_vwWnd && ccIEq(g_vwWho, key)) {   // 已在观看此源, 只前置不重复开窗/起线程
        ShowWindow(g_vwWnd, SW_SHOW); SetForegroundWindow(g_vwWnd);
        return;
    }
    long gen = ++g_vwGen;
    if (!g_vwWnd) {
        g_vwWnd = CreateWindowExW(WS_EX_TOPMOST, L"CCViewerClass", L"直播",
            WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
            960, 580, nullptr, nullptr, g_hInst, nullptr);
    }
    g_vwWho = key;
    g_vwCtrl = (port == CC_SCREEN_PORT && g_isAdmin && !ccIsLocalIp(ip));
    g_vwIsScreen = (port == CC_SCREEN_PORT && g_isAdmin);   // 快捷栏仅管理员的屏幕监视窗显示
    std::wstring title = (port == CC_CAM_PORT ? L"摄像头直播 - " : L"屏幕直播 - ") + who;
    if (g_vwCtrl) title += L"  [可操控]";
    else if (port == CC_SCREEN_PORT && g_isAdmin) title += L"  [本机不可操控]";
    SetWindowTextW(g_vwWnd, title.c_str());
    if (g_vwIsScreen && !GetDlgItem(g_vwWnd, 910)) {   // 顶部快捷栏: 音量-/音量+/声音最大/打开网页
        CreateWindowW(L"BUTTON", L"音量-", WS_CHILD | WS_VISIBLE,
            8, 7, 56, 30, g_vwWnd, (HMENU)914, g_hInst, nullptr);
        CreateWindowW(L"BUTTON", L"音量+", WS_CHILD | WS_VISIBLE,
            68, 7, 56, 30, g_vwWnd, (HMENU)915, g_hInst, nullptr);
        CreateWindowW(L"BUTTON", L"声音最大", WS_CHILD | WS_VISIBLE,
            128, 7, 72, 30, g_vwWnd, (HMENU)916, g_hInst, nullptr);
        g_vwEdit = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            220, 8, 442, 28, g_vwWnd, (HMENU)910, g_hInst, nullptr);
        CreateWindowW(L"BUTTON", L"打开【网页】网页", WS_CHILD | WS_VISIBLE,
            0, 7, 140, 30, g_vwWnd, (HMENU)911, g_hInst, nullptr);
        HFONT f = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        int ids[] = {910, 911, 914, 915, 916};
        for (int id : ids)
            SendMessageW(GetDlgItem(g_vwWnd, id), WM_SETFONT, (WPARAM)f, TRUE);
    }
    g_vwEdit = g_vwIsScreen ? GetDlgItem(g_vwWnd, 910) : nullptr;
    int tbIds[] = {910, 911, 914, 915, 916};
    for (int id : tbIds) {
        HWND c = GetDlgItem(g_vwWnd, id);
        if (c) ShowWindow(c, g_vwIsScreen ? SW_SHOW : SW_HIDE);
    }
    RECT vrc; GetClientRect(g_vwWnd, &vrc);
    SendMessageW(g_vwWnd, WM_SIZE, 0, MAKELPARAM(vrc.right, vrc.bottom));
    InvalidateRect(g_vwWnd, nullptr, TRUE);
    ShowWindow(g_vwWnd, SW_SHOW); SetForegroundWindow(g_vwWnd);
    std::thread(ccViewerThread, gen, ip, port).detach();
}

// ---------------- 文件传输 (TCP 47834) ----------------
static void ccSetFolderIcon(const std::wstring& dir) {
    wchar_t app[MAX_PATH] = {0};
    SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, app);
    std::wstring ccDir = std::wstring(app) + L"\\classChat";
    SHCreateDirectoryExW(nullptr, ccDir.c_str(), nullptr);
    std::wstring src = ccDir + L"\\folder.ico";
    // AppData 无图标 -> 从 exe 资源释放
    if (GetFileAttributesW(src.c_str()) == INVALID_FILE_ATTRIBUTES) {
        HRSRC hR = FindResourceW(g_hInst, MAKEINTRESOURCEW(101), RT_RCDATA);
        if (hR) {
            HGLOBAL hG = LoadResource(g_hInst, hR);
            DWORD sz = SizeofResource(g_hInst, hR);
            void* p = LockResource(hG);
            if (p && sz) {
                FILE* fp = _wfopen(src.c_str(), L"wb");
                if (fp) { fwrite(p, 1, sz, fp); fclose(fp); }
            }
            FreeResource(hG);
        }
    }
    std::wstring dst = dir + L"\\folder.ico";
    if (GetFileAttributesW(src.c_str()) != INVALID_FILE_ATTRIBUTES) {
        WIN32_FILE_ATTRIBUTE_DATA sd = {}, dd = {};
        bool needCopy = (GetFileAttributesExW(dst.c_str(), GetFileExInfoStandard, &dd) == 0) ||
            (GetFileAttributesExW(src.c_str(), GetFileExInfoStandard, &sd) != 0 &&
             (sd.nFileSizeLow != dd.nFileSizeLow ||
              CompareFileTime(&sd.ftLastWriteTime, &dd.ftLastWriteTime) > 0));
        if (needCopy) CopyFileW(src.c_str(), dst.c_str(), FALSE);
    }
    std::wstring ini = dir + L"\\desktop.ini";
    FILE* fp = _wfopen(ini.c_str(), L"wb");
    if (fp) {
        std::string content = "[.ShellClassInfo]\r\nIconResource=folder.ico,0\r\nIconFile=folder.ico\r\nIconIndex=0\r\n";
        fwrite(content.data(), 1, content.size(), fp);
        fclose(fp);
        SetFileAttributesW(ini.c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
    }
    SetFileAttributesW(dir.c_str(), FILE_ATTRIBUTE_READONLY);
    SHChangeNotify(SHCNE_ATTRIBUTES, SHCNF_PATH | SHCNF_FLUSH, dir.c_str(), nullptr);
    SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATH | SHCNF_FLUSH, dir.c_str(), nullptr);
}
static std::wstring ccRecvDir() {
    wchar_t desk[MAX_PATH] = {0};
    SHGetFolderPathW(nullptr, CSIDL_DESKTOPDIRECTORY, nullptr, 0, desk);
    std::wstring d = std::wstring(desk) + L"\\classChat接收文件";
    CreateDirectoryW(d.c_str(), nullptr);
    ccSetFolderIcon(d);
    return d;
}
struct CcDlArg { unsigned long ip; std::wstring gid, from, fname; long long size; };
static void ccFileRecvThread(CcDlArg a) {
    SOCKET sk = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in sa; memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET; sa.sin_port = htons(CC_FILE_PORT);
    bool ok = false, saved_ok = false;
    std::wstring saved;
    for (int i = 0; i < 15; ++i) {
        sa.sin_addr.s_addr = htonl(a.ip);
        if (connect(sk, (sockaddr*)&sa, sizeof(sa)) == 0) { ok = true; break; }
        Sleep(300);
    }
    if (ok) {
        char hdr[4];
        if (ccRecvAll(sk, hdr, 4) && memcmp(hdr, "CCF1", 4) == 0) {
            uint16_t nl = 0;
            if (ccRecvAll(sk, (char*)&nl, 2)) {
                std::string nm(nl, '\0');
                if (ccRecvAll(sk, &nm[0], nl)) {
                    uint64_t sz = 0;
                    if (ccRecvAll(sk, (char*)&sz, 8)) {
                        std::wstring base = sanitizeName(utf8ToW(nm));
                        if (base.empty()) base = L"文件";
                        std::wstring path = ccRecvDir() + L"\\" + base;
                        int dup = 1;
                        while (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
                            size_t dot = base.find_last_of(L'.');
                            std::wstring stem = (dot == std::wstring::npos) ? base : base.substr(0, dot);
                            std::wstring ext  = (dot == std::wstring::npos) ? L"" : base.substr(dot);
                            path = ccRecvDir() + L"\\" + stem + L"(" + std::to_wstring(++dup) + L")" + ext;
                        }
                        HANDLE hf = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                        if (hf != INVALID_HANDLE_VALUE) {
                            char buf[65536]; long long left = (long long)sz; saved_ok = true;
                            while (left > 0) {
                                int want = left > (long long)sizeof(buf) ? (int)sizeof(buf) : (int)left;
                                if (!ccRecvAll(sk, buf, want)) { saved_ok = false; break; }
                                DWORD wr; WriteFile(hf, buf, want, &wr, nullptr);
                                left -= want;
                            }
                            CloseHandle(hf);
                            if (saved_ok) saved = path; else DeleteFileW(path.c_str());
                        }
                    }
                }
            }
        }
    }
    closesocket(sk);
    CcFilePayload* p = new CcFilePayload{a.gid, a.from, a.fname, saved, a.size, saved_ok};
    if (g_mainWnd) PostMessageW(g_mainWnd, WM_APP_CCFILE, (WPARAM)p, 0);
}
static void ccFileSendThread(std::wstring path, std::wstring gid, std::wstring from) {
    std::string raw;
    HANDLE hf = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hf == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER fs; GetFileSizeEx(hf, &fs);
    std::string fn = wToUtf8(path.substr(path.find_last_of(L"\\/") + 1));
    uint16_t nl = (uint16_t)fn.size();
    uint64_t sz = (uint64_t)fs.QuadPart;
    // 1) 监听
    SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    BOOL yes = TRUE; setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
    sockaddr_in a; memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_ANY); a.sin_port = htons(CC_FILE_PORT);
    if (bind(ls, (sockaddr*)&a, sizeof(a)) != 0 || listen(ls, 8) != 0) {
        closesocket(ls); CloseHandle(hf); return;
    }
    // 2) 广播通知, 等待接收方连接(最多约 12 秒, 支持多人)
    {
        std::string m = "CCFILE3|" + ccTeamHex() + "|" + lan::hexEnc(wToUtf8(gid)) + "|" +
                        lan::hexEnc(wToUtf8(from)) + "|" + lan::hexEnc(fn) + "|" +
                        std::to_string(sz);
        lan::bcastRaw(m);
    }
    int waited = 0;
    while (waited < 12000) {
        fd_set rf; FD_ZERO(&rf); FD_SET(ls, &rf);
        timeval tv{0, 300000};
        if (select((int)ls + 1, &rf, nullptr, nullptr, &tv) <= 0) { waited += 300; continue; }
        SOCKET c = accept(ls, nullptr, nullptr);
        if (c == INVALID_SOCKET) continue;
        waited = 0;
        // 发头
        ccSendAll(c, "CCF1", 4);
        ccSendAll(c, (const char*)&nl, 2);
        ccSendAll(c, fn.data(), nl);
        ccSendAll(c, (const char*)&sz, 8);
        LARGE_INTEGER off; off.QuadPart = 0; SetFilePointerEx(hf, off, nullptr, FILE_BEGIN);
        char buf[65536]; DWORD rd;
        while (ReadFile(hf, buf, sizeof(buf), &rd, nullptr) && rd > 0)
            if (!ccSendAll(c, buf, (int)rd)) break;
        closesocket(c);
        Sleep(200);   // 给下一个接收方一点连接窗口
        waited = 8000; // 再等一轮无连接即结束
    }
    closesocket(ls); CloseHandle(hf);
}
static void ccPickAndSendFile() {
    if (g_ccCur.empty()) return;
    wchar_t buf[MAX_PATH] = {0};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_mainWnd;
    ofn.lpstrFilter = L"所有文件\0*.*\0";
    ofn.lpstrFile = buf; ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&ofn)) {
        std::wstring path = buf;
        std::wstring fname = path.substr(path.find_last_of(L"\\/") + 1);
        ccAppend(g_ccCur, CcMsg{g_currentUser, fname, ccNowHM(), L"等待对方接收…", ccNewMsgId(), 'F', GetTickCount64()});
        std::thread(ccFileSendThread, path, g_ccCur, g_currentUser).detach();
        WIN32_FILE_ATTRIBUTE_DATA fad;
        if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) {
            long long sz = ((long long)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
            ccLog(L"文件", g_currentUser + L" 向 [" + ccGroupName(g_ccCur) +
                           L"] 发送文件: " + fname + L" (" + fmtSize(sz) + L")");
        }
    }
}

// ---------------- 建群对话框 ----------------
static HWND g_ngWnd = nullptr;
static LRESULT CALLBACK CcNewGrpProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
    int n = (int)CC_NAMES.size();
    switch (msg) {
    case WM_CREATE:
        CreateWindowW(L"STATIC", L"群名称:", WS_CHILD | WS_VISIBLE, 16, 12, 60, 22,
            hWnd, (HMENU)7001, g_hInst, nullptr);
        CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            80, 10, 210, 26, hWnd, (HMENU)7002, g_hInst, nullptr);
        CreateWindowW(L"STATIC", L"选择成员 (含自己):", WS_CHILD | WS_VISIBLE, 16, 44, 200, 20,
            hWnd, (HMENU)7003, g_hInst, nullptr);
        for (int i = 0; i < n; ++i) {
            std::wstring lab = std::wstring(CC_NAMES[i]) + L" (" + CC_ROLES[i] + L")";
            CreateWindowW(L"BUTTON", lab.c_str(), WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                18, 68 + i * 26, 270, 22, hWnd, (HMENU)(INT_PTR)(7100 + i), g_hInst, nullptr);
        }
        CreateWindowW(L"BUTTON", L"创建群聊", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            80, 68 + n * 26 + 12, 100, 30, hWnd, (HMENU)IDOK, g_hInst, nullptr);
        CreateWindowW(L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE,
            196, 68 + n * 26 + 12, 90, 30, hWnd, (HMENU)IDCANCEL, g_hInst, nullptr);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) {
            wchar_t nm[64] = {0}; GetDlgItemTextW(hWnd, 7002, nm, 64);
            std::vector<int> pick;
            for (int i = 0; i < n; ++i)
                if (SendMessageW(GetDlgItem(hWnd, 7100 + i), BM_GETCHECK, 0, 0) == BST_CHECKED)
                    pick.push_back(i);
            bool meIn = false;
            for (int i : pick) if (ccIEq(CC_NAMES[i], g_currentUser)) meIn = true;
            if (!meIn || pick.size() < 2) {
                MessageBoxW(hWnd, L"请勾选至少 2 人, 且必须包含你自己。", L"提示", MB_OK | MB_ICONWARNING);
                return 0;
            }
            CcGroup g;
            g.id = L"c" + std::to_wstring(lan::g_myId) + L"_" + std::to_wstring(GetTickCount());
            g.name = nm[0] ? std::wstring(nm) : L"新群聊";
            for (int i : pick) g.mem.push_back(CC_NAMES[i]);
            g_ccGroups.push_back(g);
            g_ccCur = g.id;
            ccSaveCustomGroups();
            ccRefreshList(); ccRefreshMessages();
            std::string csv;
            for (auto& m : g.mem) { if (!csv.empty()) csv += ","; csv += wToUtf8(m); }
            lan::bcastRaw("CCGRP3|" + ccTeamHex() + "|" + lan::hexEnc(wToUtf8(g.id)) + "|" +
                          lan::hexEnc(wToUtf8(g.name)) + "|" + lan::hexEnc(csv));
            DestroyWindow(hWnd);
            return 0;
        }
        if (LOWORD(wp) == IDCANCEL) { DestroyWindow(hWnd); return 0; }
        return 0;
    case WM_DESTROY:
        if (g_mainWnd) EnableWindow(g_mainWnd, TRUE);
        g_ngWnd = nullptr;
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wp, lp);
}
static void ccOpenNewGroup() {
    int n = (int)CC_NAMES.size();
    if (g_ngWnd) { SetForegroundWindow(g_ngWnd); return; }
    static bool reg = false;
    if (!reg) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = CcNewGrpProc; wc.hInstance = g_hInst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"CCNewGrpClass";
        RegisterClassW(&wc); reg = true;
    }
    EnableWindow(g_mainWnd, FALSE);
    int H = 68 + n * 26 + 90;
    g_ngWnd = CreateWindowExW(0, L"CCNewGrpClass", L"创建群聊",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 320, H, g_mainWnd, nullptr, g_hInst, nullptr);
    for (int i = 0; i < n; ++i)
        if (ccIEq(CC_NAMES[i], g_currentUser))
            SendMessageW(GetDlgItem(g_ngWnd, 7100 + i), BM_SETCHECK, BST_CHECKED, 0);
}

// ---------------- 教师共享 / 管理员监视 ----------------
static void ccToggleShare() {
    if (g_ccSharing) {
        // 停止共享只标记状态, 不关闭底层 serve(保持可被管理员直接监视)
        g_ccSharing = false;
        ccLog(L"屏幕", g_currentUser + L" 停止屏幕共享");
        MessageBoxW(g_mainWnd, L"已停止屏幕共享。", L"屏幕共享", MB_OK | MB_ICONINFORMATION);
        return;
    }
    ccStartServe(); g_ccSharing = true;
    ccLog(L"屏幕", g_currentUser + L" 开始屏幕共享");
    lan::bcastRaw("CCCAST3|" + ccTeamHex() + "|" + lan::hexEnc(wToUtf8(g_currentUser)) + "|S|");
    MessageBoxW(g_mainWnd,
        L"已开始向全班共享屏幕!\n再次点击\"教师屏幕\"可停止。", L"屏幕共享", MB_OK | MB_ICONINFORMATION);
}
// 学生/其他人点击"教师屏幕": 直接打开教师的共享画面
static void ccWatchTeacher(HWND hWnd) {
    for (size_t i = 0; i < CC_NAMES.size(); ++i) {
        if (!ccHasRole(CC_NAMES[i], L"教师")) continue;
        std::wstring tn = CC_NAMES[i];
        unsigned long ip = 0;
        if (lan::whoIp(tn, ip)) { ccOpenViewer(tn, ip); return; }
    }
    MessageBoxW(hWnd, L"教师当前不在线。\n教师开始共享屏幕后, 再点击\"教师屏幕\"即可观看。",
                L"教师屏幕", MB_OK | MB_ICONINFORMATION);
}
// 管理员监视: 选择成员 + 监视屏幕(47835)或监视摄像头(47836), 直连不询问对方
static LRESULT CALLBACK CcMonPickProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
    int n = (int)CC_NAMES.size();
    switch (msg) {
    case WM_CREATE: {
        CreateWindowW(L"STATIC", L"选择要监视的在线成员:", WS_CHILD | WS_VISIBLE, 14, 12, 240, 20,
            hWnd, (HMENU)7201, g_hInst, nullptr);
        for (int i = 0, row = 0; i < n; ++i) {
            if (ccIEq(CC_NAMES[i], g_currentUser)) continue;
            std::wstring lab = std::wstring(CC_NAMES[i]) + L" (" + CC_ROLES[i] + L")";
            bool on = lan::whoOnline(CC_NAMES[i]);
            if (on) lab += L"  ●在线";
            CreateWindowW(L"BUTTON", lab.c_str(), WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON |
                          (on ? 0 : WS_DISABLED), 18, 38 + row * 28, 250, 24,
                          hWnd, (HMENU)(INT_PTR)(7210 + i), g_hInst, nullptr);
            ++row;
        }
        int by = 38 + n * 28 + 8;
        CreateWindowW(L"BUTTON", L"监视屏幕", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            18, by, 116, 30, hWnd, (HMENU)7301, g_hInst, nullptr);
        CreateWindowW(L"BUTTON", L"监视摄像头", WS_CHILD | WS_VISIBLE,
            142, by, 116, 30, hWnd, (HMENU)7302, g_hInst, nullptr);
        CreateWindowW(L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE,
            94, by + 38, 90, 28, hWnd, (HMENU)IDCANCEL, g_hInst, nullptr);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == 7301 || LOWORD(wp) == 7302) {
            int port = (LOWORD(wp) == 7302) ? CC_CAM_PORT : CC_SCREEN_PORT;
            for (int i = 0; i < n; ++i) {
                HWND b = GetDlgItem(hWnd, 7210 + i);
                if (b && IsWindowEnabled(b) &&
                    SendMessageW(b, BM_GETCHECK, 0, 0) == BST_CHECKED) {
                    unsigned long ip = 0;
                    if (!lan::whoIp(CC_NAMES[i], ip)) {
                        MessageBoxW(hWnd, L"该成员当前不在线。", L"提示", MB_OK | MB_ICONWARNING);
                        return 0;
                    }
                    ccOpenViewer(CC_NAMES[i], ip, port);
                    ccLog(L"屏幕", L"管理员 " + g_currentUser +
                          (port == CC_CAM_PORT ? L" 监视摄像头 " : L" 监视屏幕 ") + CC_NAMES[i]);
                    DestroyWindow(hWnd);
                    return 0;
                }
            }
            MessageBoxW(hWnd, L"请选择一名在线成员。", L"提示", MB_OK | MB_ICONWARNING);
            return 0;
        }
        if (LOWORD(wp) == IDCANCEL) { DestroyWindow(hWnd); return 0; }
        return 0;
    case WM_DESTROY:
        if (g_mainWnd) EnableWindow(g_mainWnd, TRUE);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wp, lp);
}
static void ccOpenMonitorPick() {
    int n = (int)CC_NAMES.size();
    static bool reg = false;
    if (!reg) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = CcMonPickProc; wc.hInstance = g_hInst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"CCMonPickClass";
        RegisterClassW(&wc); reg = true;
    }
    EnableWindow(g_mainWnd, FALSE);
    CreateWindowExW(0, L"CCMonPickClass", L"监视成员 (管理员)",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 292, 38 + n * 28 + 8 + 30 + 38 + 56, g_mainWnd, nullptr, g_hInst, nullptr);
}

// ---------------- 团队名单 UDP 同步 ----------------
static void ccBroadcastTeam(const std::wstring& teamName) {
    lan::init();   // 登录前注册团队时网络可能尚未初始化
    int ti = -1;
    for (size_t i = 0; i < g_teams.size(); ++i) if (g_teams[i].name == teamName) ti = (int)i;
    if (ti < 0) return;
    std::wstring blob = teamName;
    for (auto& u : g_teams[ti].users) {
        std::wstring rs;
        for (auto& r : u.roles) { if (!rs.empty()) rs += L","; rs += r; }
        blob += L"\n" + u.name + L"\t" + rs;
    }
    lan::bcastRaw("CCTEAM3|" + lan::hexEnc(wToUtf8(teamName)) + "|" +
                  lan::hexEnc(wToUtf8(blob)));
}
static void ccApplyTeamBlob(const std::wstring& blob) {
    std::vector<std::wstring> lines;
    size_t c = 0;
    while (c <= blob.size()) {
        size_t e = blob.find(L'\n', c);
        lines.push_back(blob.substr(c, e == std::wstring::npos ? std::wstring::npos : e - c));
        if (e == std::wstring::npos) break;
        c = e + 1;
    }
    if (lines.empty()) return;
    Team nt; nt.name = lines[0];
    if (nt.name.empty()) return;
    for (size_t i = 1; i < lines.size(); ++i) {
        size_t tab = lines[i].find(L'\t');
        TeamUser u;
        if (tab == std::wstring::npos) { u.name = lines[i]; }
        else {
            u.name = lines[i].substr(0, tab);
            std::wstring rs = lines[i].substr(tab + 1);
            size_t rc = 0;
            while (rc <= rs.size()) {
                size_t re = rs.find(L',', rc);
                std::wstring r = rs.substr(rc, re == std::wstring::npos ? std::wstring::npos : re - rc);
                if (!r.empty()) u.roles.push_back(r);
                if (re == std::wstring::npos) break;
                rc = re + 1;
            }
        }
        if (u.name.empty()) continue;
        if (u.roles.empty()) u.roles.push_back(L"学生");
        nt.users.push_back(u);
    }
    if (nt.users.empty()) return;
    int ti = -1;
    for (size_t i = 0; i < g_teams.size(); ++i) if (g_teams[i].name == nt.name) ti = (int)i;
    if (ti >= 0) g_teams[ti] = nt; else g_teams.push_back(nt);
    saveTeams();
    if (nt.name == g_team) {
        ccRefreshNameCache();
        ccInitGroups();
        ccRefreshList(); ccRefreshMessages();
    }
}

// ---------------- 文件接收 ACK ----------------
static void ccSendAck(const std::wstring& gid, const std::wstring& receiver,
                      const std::wstring& fname, bool ok) {
    lan::bcastRaw("CCACK3|" + ccTeamHex() + "|" + lan::hexEnc(wToUtf8(gid)) + "|" +
                  lan::hexEnc(wToUtf8(receiver)) + "|" + lan::hexEnc(wToUtf8(fname)) + "|" +
                  (ok ? "1" : "0"));
}

// ---------------- 主题持久化 (theme.dat) ----------------
static std::wstring ccThemePath() {
    return dataPathFor(L"theme.dat");
}
static void ccLoadTheme() {
    FILE* fp = _wfopen(ccThemePath().c_str(), L"rb");
    if (fp) { char b[8] = {0}; fread(b, 1, 7, fp); fclose(fp); g_darkMode = (b[0] == '1'); }
}
static void ccSaveTheme() {
    FILE* fp = _wfopen(ccThemePath().c_str(), L"wb");
    if (fp) { fputc(g_darkMode ? '1' : '0', fp); fclose(fp); }
}
static void ccApplyTheme(HWND hWnd) {
    ccSaveTheme();
    InvalidateRect(hWnd, nullptr, TRUE);
    HWND ch = nullptr;
    while ((ch = FindWindowExW(hWnd, ch, nullptr, nullptr))) {
        InvalidateRect(ch, nullptr, TRUE);
        SendMessageW(ch, WM_THEMECHANGED, 0, 0);
    }
}

// ---------------- 操作日志窗口 ----------------
static void ccLogRefresh() {
    if (!g_ccLogList) return;
    SendMessageW(g_ccLogList, LB_RESETCONTENT, 0, 0);
    for (auto& l : g_ccLogs)
        if (g_ccLogFilter.empty() || l.find(L"][" + g_ccLogFilter) != std::wstring::npos)
            SendMessageW(g_ccLogList, LB_ADDSTRING, 0, (LPARAM)l.c_str());
    int c = (int)SendMessageW(g_ccLogList, LB_GETCOUNT, 0, 0);
    if (c > 0) SendMessageW(g_ccLogList, LB_SETTOPINDEX, c - 1, 0);
}
static LRESULT CALLBACK CcLogProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        CreateWindowW(L"STATIC", L"筛选:", WS_CHILD | WS_VISIBLE, 10, 10, 36, 22,
            hWnd, (HMENU)6100, g_hInst, nullptr);
        HWND cb = CreateWindowW(L"COMBOBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
            48, 8, 120, 300, hWnd, (HMENU)6101, g_hInst, nullptr);
        const wchar_t* fz[] = {L"全部", L"上下线", L"消息", L"文件", L"屏幕", L"指令"};
        for (auto z : fz) SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)z);
        SendMessageW(cb, CB_SETCURSEL, 0, 0);
        SendMessageW(cb, WM_SETFONT, (WPARAM)hFontUI, TRUE);
        g_ccLogList = CreateWindowW(L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_BORDER | LBS_NOINTEGRALHEIGHT,
            10, 40, 680, 380, hWnd, (HMENU)6102, g_hInst, nullptr);
        SendMessageW(g_ccLogList, WM_SETFONT, (WPARAM)hFontSmall, TRUE);
        CreateWindowW(L"BUTTON", L"关闭", WS_CHILD | WS_VISIBLE,
            600, 430, 90, 28, hWnd, (HMENU)IDCANCEL, g_hInst, nullptr);
        ccLogRefresh();
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == 6101 && HIWORD(wp) == CBN_SELCHANGE) {
            wchar_t b[32] = {0}; GetDlgItemTextW(hWnd, 6101, b, 32);
            g_ccLogFilter = (b[0] && wcscmp(b, L"全部") != 0) ? b : L"";
            ccLogRefresh();
        }
        if (LOWORD(wp) == IDCANCEL) { DestroyWindow(hWnd); return 0; }
        return 0;
    case WM_DESTROY:
        g_ccLogWnd = nullptr; g_ccLogList = nullptr; g_ccLogFilter.clear();
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wp, lp);
}
static void ccOpenLog() {
    if (g_ccLogWnd) { SetForegroundWindow(g_ccLogWnd); return; }
    static bool reg = false;
    if (!reg) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = CcLogProc; wc.hInstance = g_hInst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"CCLogClass";
        RegisterClassW(&wc); reg = true;
    }
    g_ccLogWnd = CreateWindowExW(0, L"CCLogClass", L"操作日志",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 716, 508, g_mainWnd, nullptr, g_hInst, nullptr);
}

// ---------------- 远程指令: 锁定 / 关机 ----------------
typedef int (WINAPI* PFN_MsgsTimeoutW)(HWND, LPCWSTR, LPCWSTR, UINT, WORD, DWORD);
static bool ccEnableShutdownPriv() {
    HANDLE hTok = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hTok))
        return false;
    TOKEN_PRIVILEGES tp = {};
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    bool ok = LookupPrivilegeValueW(nullptr, SE_SHUTDOWN_NAME, &tp.Privileges[0].Luid) &&
              AdjustTokenPrivileges(hTok, FALSE, &tp, 0, nullptr, nullptr) &&
              GetLastError() == ERROR_SUCCESS;
    CloseHandle(hTok);
    return ok;
}
static void ccHandleRemoteCmd(const std::wstring& from, const std::string& act,
                              const std::wstring& url = L"") {
    if (act == "VOLUP" || act == "VOLDOWN") {   // 远程音量: 模拟系统音量键
        ccLog(L"指令", L"管理员 " + from + (act == "VOLUP" ? L" 调高音量" : L" 调低音量"));
        INPUT in[2] = {};
        in[0].type = INPUT_KEYBOARD;
        in[0].ki.wVk = (WORD)((act == "VOLUP") ? VK_VOLUME_UP : VK_VOLUME_DOWN);
        in[1] = in[0]; in[1].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(2, in, sizeof(INPUT));
        return;
    }
    if (act == "VOLMAX") {   // 声音开最大: 连发音量上键 60 次, 确保到顶
        ccLog(L"指令", L"管理员 " + from + L" 将音量开到最大");
        for (int i = 0; i < 60; ++i) {
            INPUT in[2] = {};
            in[0].type = INPUT_KEYBOARD;
            in[0].ki.wVk = VK_VOLUME_UP;
            in[1] = in[0]; in[1].ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(2, in, sizeof(INPUT));
            Sleep(8);
        }
        return;
    }
    if (act == "OPENWEB") {
        ccLog(L"指令", L"管理员 " + from + L" 要求打开网页: " + url);
        std::wstring u = url;
        if (u.find(L"://") == std::wstring::npos) u = L"https://" + u;
        ShellExecuteW(nullptr, L"open", u.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return;
    }
    if (act == "LOCK") {
        ccLog(L"指令", L"管理员 " + from + L" 发送 LOCK, 已锁定本机");
        LockWorkStation();
        return;
    }
    if (act == "SHUTDOWN") {
        // 不询问用户, 直接强制关机
        ccLog(L"指令", L"管理员 " + from + L" 发送 SHUTDOWN, 已直接关机");
        ccEnableShutdownPriv();
        ExitWindowsEx(EWX_SHUTDOWN | EWX_FORCE, 0);
    }
}
static void ccSendCmd(const std::wstring& target, const std::string& act,
                      const std::wstring& extra) {
    std::string p = "CCCMD3|" + ccTeamHex() + "|" + lan::hexEnc(wToUtf8(g_currentUser)) + "|" +
                  lan::hexEnc(wToUtf8(target)) + "|" + act;
    if (!extra.empty()) p += "|" + lan::hexEnc(wToUtf8(extra));
    lan::bcastRaw(p);
    ccLog(L"指令", L"管理员 " + g_currentUser + L" 向 " + target + L" 发送 " +
        (act == "LOCK" ? L"LOCK(锁屏)"
         : act == "SHUTDOWN" ? L"SHUTDOWN(关机)"
         : act == "OPENWEB" ? L"OPENWEB(打开网页 " + extra + L")"
         : act == "VOLMAX" ? L"VOLMAX(声音最大)"
         : act == "VOLUP" ? L"VOLUP(音量+)"
         : act == "VOLDOWN" ? L"VOLDOWN(音量-)"
         : utf8ToW(act)));
}
static LRESULT CALLBACK CcCmdPickProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
    int n = (int)CC_NAMES.size();
    switch (msg) {
    case WM_CREATE: {
        CreateWindowW(L"STATIC", L"选择在线成员, 发送远程指令:", WS_CHILD | WS_VISIBLE, 14, 12, 260, 20,
            hWnd, (HMENU)7301, g_hInst, nullptr);
        for (int i = 0, row = 0; i < n; ++i) {
            if (ccIEq(CC_NAMES[i], g_currentUser)) continue;
            std::wstring lab = std::wstring(CC_NAMES[i]) + L" (" + CC_ROLES[i] + L")";
            bool on = lan::whoOnline(CC_NAMES[i]);
            if (on) lab += L"  ●在线";
            CreateWindowW(L"BUTTON", lab.c_str(), WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON |
                          (on ? 0 : WS_DISABLED), 18, 38 + row * 30, 250, 24,
                          hWnd, (HMENU)(INT_PTR)(7310 + i), g_hInst, nullptr);
            ++row;
        }
        int by = 38 + n * 30 + 6;
        CreateWindowW(L"BUTTON", L"锁定屏幕", WS_CHILD | WS_VISIBLE,
            30, by, 100, 30, hWnd, (HMENU)7320, g_hInst, nullptr);
        CreateWindowW(L"BUTTON", L"远程关机", WS_CHILD | WS_VISIBLE,
            140, by, 100, 30, hWnd, (HMENU)7321, g_hInst, nullptr);
        return 0;
    }
    case WM_COMMAND: {
        WORD id = LOWORD(wp);
        if (id != 7320 && id != 7321) {
            if (id == IDCANCEL) DestroyWindow(hWnd);
            return 0;
        }
        int sel = -1;
        for (int i = 0; i < n; ++i) {
            HWND b = GetDlgItem(hWnd, 7310 + i);
            if (b && SendMessageW(b, BM_GETCHECK, 0, 0) == BST_CHECKED) { sel = i; break; }
        }
        if (sel < 0) {
            MessageBoxW(hWnd, L"请先选择一名在线成员。", L"提示", MB_OK | MB_ICONWARNING);
            return 0;
        }
        std::string act = (id == 7320) ? "LOCK" : "SHUTDOWN";
        std::wstring q = (id == 7320)
            ? L"确定锁定 [" + std::wstring(CC_NAMES[sel]) + L"] 的屏幕吗?"
            : L"确定远程关闭 [" + std::wstring(CC_NAMES[sel]) + L"] 的电脑吗?";
        if (MessageBoxW(hWnd, q.c_str(), L"二次确认",
                MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) return 0;
        ccSendCmd(CC_NAMES[sel], act);
        DestroyWindow(hWnd);
        return 0;
    }
    case WM_DESTROY:
        if (g_mainWnd) EnableWindow(g_mainWnd, TRUE);
        g_ccCmdWnd = nullptr;
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wp, lp);
}
static void ccOpenCmdPick() {
    if (g_ccCmdWnd) { SetForegroundWindow(g_ccCmdWnd); return; }
    int n = (int)CC_NAMES.size();
    static bool reg = false;
    if (!reg) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = CcCmdPickProc; wc.hInstance = g_hInst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"CCCmdPickClass";
        RegisterClassW(&wc); reg = true;
    }
    EnableWindow(g_mainWnd, FALSE);
    g_ccCmdWnd = CreateWindowExW(0, L"CCCmdPickClass", L"远程指令 (管理员)",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 300, 38 + n * 30 + 60,
        g_mainWnd, nullptr, g_hInst, nullptr);
}

// ---------------- 网络协议入口 (运行于 UI 心跳线程) ----------------
static std::vector<std::string> ccSplit(const std::string& s) {
    std::vector<std::string> v; size_t c = 0;
    while (c <= s.size()) {
        size_t e = s.find('|', c);
        v.push_back(s.substr(c, e == std::string::npos ? std::string::npos : e - c));
        if (e == std::string::npos) break;
        c = e + 1;
    }
    return v;
}
static void ccOnNet(const std::string& s, unsigned long fromIp) {
    // ---- 团队名单同步 (任何团队都接收) ----
    if (s.compare(0, 7, "CCTEAM3|") == 0) {
        auto f = ccSplit(s);
        if (f.size() >= 3)
            ccApplyTeamBlob(utf8ToW(lan::hexDec(f[2])));
        return;
    }
    auto f = ccSplit(s);
    if (f.size() < 3) return;
    std::wstring team = utf8ToW(lan::hexDec(f[1]));
    if (team != g_team) return;   // 只处理本团队消息
    std::string tag = f[0];

    if (tag == "CCMSG3" && f.size() >= 6) {
        std::wstring gid  = utf8ToW(lan::hexDec(f[2]));
        std::wstring from = utf8ToW(lan::hexDec(f[3]));
        std::string type  = f[4];
        std::string body  = f[5];
        std::wstring mid  = f.size() >= 7 ? utf8ToW(lan::hexDec(f[6])) : ccNewMsgId();
        if (from == g_currentUser) return;
        CcGroup* g = ccFind(gid);
        if (!g || !ccInGroup(*g, g_currentUser)) return;
        ULONGLONG now = GetTickCount64();
        if (type == "T") {
            ccAppend(gid, CcMsg{from, utf8ToW(lan::hexDec(body)), ccNowHM(), L"", mid, 'T', now});
        } else if (type == "E") {
            std::string e = lan::hexDec(body);
            if (!e.empty()) {
                ccAppend(gid, CcMsg{from, utf8ToW(e), ccNowHM(), L"", mid, 'E', now});
                MessageBeep(MB_OK);
                if (e == "B") ccPlayBomb();
            }
        }
        return;
    }
    if (tag == "CCREVOKE3" && f.size() >= 4) {
        ccRevokeById(utf8ToW(lan::hexDec(f[2])), utf8ToW(lan::hexDec(f[3])));
        return;
    }
    if (tag == "CCREAD3" && f.size() >= 5) {
        ccOnRead(utf8ToW(lan::hexDec(f[2])), utf8ToW(lan::hexDec(f[3])),
                 utf8ToW(lan::hexDec(f[4])));
        return;
    }
    if (tag == "CCCMD3" && f.size() >= 5) {
        std::wstring from = utf8ToW(lan::hexDec(f[2]));
        std::wstring target = utf8ToW(lan::hexDec(f[3]));
        std::string act = f[4];
        if (target != g_currentUser || !ccHasRole(from, L"管理员")) return;
        std::wstring extra = (f.size() >= 6) ? utf8ToW(lan::hexDec(f[5])) : L"";
        ccHandleRemoteCmd(from, act, extra);
        return;
    }
    if (tag == "CCFILE3" && f.size() >= 6) {
        std::wstring gid  = utf8ToW(lan::hexDec(f[2]));
        std::wstring from = utf8ToW(lan::hexDec(f[3]));
        std::wstring fn   = utf8ToW(lan::hexDec(f[4]));
        long long size = _strtoi64(f[5].c_str(), nullptr, 10);
        if (from == g_currentUser) return;
        CcGroup* g = ccFind(gid);
        if (!g || !ccInGroup(*g, g_currentUser)) return;
        ccAppend(gid, CcMsg{from, fn + L" (" + fmtSize(size) + L")", ccNowHM(), L"接收中…", ccNewMsgId(), 'F', GetTickCount64()});
        CcDlArg a{fromIp, gid, from, fn, size};
        std::thread(ccFileRecvThread, a).detach();
        return;
    }
    if (tag == "CCACK3" && f.size() >= 6) {
        std::wstring gid  = utf8ToW(lan::hexDec(f[2]));
        std::wstring who  = utf8ToW(lan::hexDec(f[3]));
        std::wstring fn   = utf8ToW(lan::hexDec(f[4]));
        bool ok = f[5] == "1";
        if (who == g_currentUser) return;
        ccSetFileNote(gid, g_currentUser, fn,
                      ok ? (L"✓ " + who + L" 已接收") : (who + L" 接收失败"));
        return;
    }
    if (tag == "CCGRP3" && f.size() >= 5) {
        std::wstring gid  = utf8ToW(lan::hexDec(f[2]));
        std::wstring name = utf8ToW(lan::hexDec(f[3]));
        std::string csv  = lan::hexDec(f[4]);
        std::vector<std::wstring> mem; size_t c = 0;
        while (c < csv.size()) {
            size_t e = csv.find(',', c);
            if (e == std::string::npos) e = csv.size();
            std::wstring u = utf8ToW(csv.substr(c, e - c));
            if (!u.empty()) mem.push_back(u);
            c = e + 1;
        }
        bool mine = false;
        for (auto& u : mem) if (ccIEq(u, g_currentUser)) mine = true;
        if (!mine) return;
        CcGroup* ex = ccFind(gid);
        if (ex) { ex->name = name; ex->mem = mem; }
        else { g_ccGroups.push_back(CcGroup{gid, name, mem}); g_ccCur = gid; }
        ccSaveCustomGroups();
        ccRefreshList(); ccRefreshMessages();
        return;
    }
    if (tag == "CCCAST3" && f.size() >= 5) {
        std::wstring from = utf8ToW(lan::hexDec(f[2]));
        std::string kind  = f[3];
        std::wstring forWho = utf8ToW(lan::hexDec(f[4]));
        if (from == g_currentUser) return;
        if (kind == "S") ccOpenViewer(from, fromIp);
        else if (kind == "M" && forWho == g_currentUser) ccOpenViewer(from, fromIp);
        return;
    }
}

// 自适应布局: 所有控件按客户区尺寸重排
static HMENU g_ccMainMenu = nullptr;
static void ccDoLayout(HWND hWnd) {
    RECT rc; GetClientRect(hWnd, &rc);
    int cw = rc.right, ch = rc.bottom;
    int sideW = cw * 24 / 100; if (sideW > 260) sideW = 260; if (sideW < 180) sideW = 180;
    int rightX = 16 + sideW + 16, rightW = cw - rightX - 16;
    if (g_ccList)    MoveWindow(g_ccList, 16, 76, sideW, ch - 76 - 40, TRUE);
    if (g_ccTitle)   MoveWindow(g_ccTitle, rightX, 76, rightW, 20, TRUE);
    HWND sch = GetDlgItem(hWnd, IDC_CC_SEARCH);
    HWND clr = GetDlgItem(hWnd, IDC_CC_SEARCHCLR);
    HWND inf = GetDlgItem(hWnd, IDC_CC_SINFO);
    if (sch) MoveWindow(sch, rightX, 100, rightW - 140, 24, TRUE);
    if (clr) MoveWindow(clr, rightX + rightW - 134, 100, 26, 24, TRUE);
    if (inf) MoveWindow(inf, rightX + rightW - 104, 102, 104, 20, TRUE);
    if (g_ccView)    MoveWindow(g_ccView, rightX, 128, rightW, ch - 128 - 104, TRUE);
    if (g_ccInput)   MoveWindow(g_ccInput, rightX, ch - 76, rightW - 110, 28, TRUE);
    HWND snd = GetDlgItem(hWnd, IDC_CC_SEND);
    if (snd) MoveWindow(snd, cw - 16 - 94, ch - 76, 94, 28, TRUE);
    int emoId[3] = {IDC_CC_EMO_LAUGH, IDC_CC_EMO_CRY, IDC_CC_EMO_BOMB};
    for (int i = 0; i < 3; ++i) {
        HWND b = GetDlgItem(hWnd, emoId[i]);
        if (b) MoveWindow(b, rightX + i * 40, ch - 42, 36, 30, TRUE);
    }
    HWND fb = GetDlgItem(hWnd, IDC_CC_FILE);
    if (fb) MoveWindow(fb, rightX + 124, ch - 42, 88, 30, TRUE);
    if (g_onlineLabel) MoveWindow(g_onlineLabel, 16, ch - 26, sideW, 20, TRUE);
}
static LRESULT CALLBACK MainProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    LRESULT ret;
    switch (msg) {
    case WM_CREATE: {
        // 左: 群聊列表 (微信风格浅灰, 无边框)
        g_ccList = CreateWindowW(L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | LBS_NOTIFY | WS_VSCROLL | LBS_OWNERDRAWFIXED | LBS_NOINTEGRALHEIGHT,
            0, 0, 10, 10, hWnd, (HMENU)IDC_CC_GROUPS, g_hInst, nullptr);
        SendMessageW(g_ccList, LB_SETITEMHEIGHT, 0, 34);
        SendMessageW(g_ccList, WM_SETFONT, (WPARAM)hFontUI, TRUE);
        // 右: 当前群标题
        g_ccTitle = CreateWindowW(L"STATIC", L"",
            WS_CHILD | WS_VISIBLE, 0, 0, 10, 10,
            hWnd, (HMENU)IDC_CC_TITLE, g_hInst, nullptr);
        SendMessageW(g_ccTitle, WM_SETFONT, (WPARAM)hFontBold, TRUE);
        // 搜索框 + 清除 + 结果数
        CreateWindowW(L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL,
            0, 0, 10, 10, hWnd, (HMENU)IDC_CC_SEARCH, g_hInst, nullptr);
        SendMessageW(GetDlgItem(hWnd, IDC_CC_SEARCH), WM_SETFONT, (WPARAM)hFontUI, TRUE);
        SendMessageW(GetDlgItem(hWnd, IDC_CC_SEARCH), EM_SETCUEBANNER, TRUE, (LPARAM)L"搜索聊天记录");
        CreateWindowW(L"BUTTON", L"×",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 10, 10, hWnd, (HMENU)IDC_CC_SEARCHCLR, g_hInst, nullptr);
        HWND sinfo = CreateWindowW(L"STATIC", L"",
            WS_CHILD | WS_VISIBLE, 0, 0, 10, 10,
            hWnd, (HMENU)IDC_CC_SINFO, g_hInst, nullptr);
        SendMessageW(sinfo, WM_SETFONT, (WPARAM)hFontSmall, TRUE);
        // 右: 消息区 (微信式气泡自绘, 无边框)
        ccRegisterMsgView(g_hInst);
        g_ccView = CreateWindowExW(0, L"CCMsgView", L"",
            WS_CHILD | WS_VISIBLE,
            0, 0, 10, 10, hWnd, (HMENU)IDC_CC_MSGS, g_hInst, nullptr);
        // 右下: 输入框 + 发送 (微信风格白底单边框)
        g_ccInput = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            0, 0, 10, 10, hWnd, (HMENU)IDC_CC_INPUT, g_hInst, nullptr);
        SendMessageW(g_ccInput, WM_SETFONT, (WPARAM)hFontUI, TRUE);
        SendMessageW(g_ccInput, EM_SETCUEBANNER, TRUE, (LPARAM)L"输入消息…");
        CreateWindowW(L"BUTTON", L"发送",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW | BS_DEFPUSHBUTTON,
            0, 0, 10, 10, hWnd, (HMENU)IDC_CC_SEND, g_hInst, nullptr);
        // 表情 / 文件 (按钮直接显示 emoji 字符, 不用文字描述)
        const wchar_t* emo[3] = {L"😀", L"😭", L"💣"};
        int emoId[3] = {IDC_CC_EMO_LAUGH, IDC_CC_EMO_CRY, IDC_CC_EMO_BOMB};
        for (int i = 0; i < 3; ++i)
            CreateWindowW(L"BUTTON", emo[i],
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                0, 0, 10, 10, hWnd, (HMENU)(INT_PTR)emoId[i], g_hInst, nullptr);
        CreateWindowW(L"BUTTON", L"📎 文件",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 10, 10, hWnd, (HMENU)IDC_CC_FILE, g_hInst, nullptr);
        for (int id : {IDC_CC_EMO_LAUGH, IDC_CC_EMO_CRY, IDC_CC_EMO_BOMB, IDC_CC_FILE,
                       IDC_CC_SEARCHCLR})
            SendMessageW(GetDlgItem(hWnd, id), WM_SETFONT, (WPARAM)hFontUI, TRUE);

        // 左下角: 在线状态
        g_onlineLabel = CreateWindowW(L"STATIC", L"● 正在检测局域网...",
            WS_CHILD | WS_VISIBLE, 0, 0, 10, 10,
            hWnd, (HMENU)IDC_ONLINE, g_hInst, nullptr);
        SendMessageW(g_onlineLabel, WM_SETFONT, (WPARAM)hFontSmall, TRUE);

        ccDoLayout(hWnd);   // 自适应布局
        ccRecvDir();        // 确保接收文件夹存在并设置图标
        ccLog(L"上下线", g_currentUser + L" 登录进入主界面");

        // 初始化群聊
        ccInitGroups();
        ccHistLoad();   // 教师角色: 从 AppData 恢复历史消息
        ccRefreshList();
        ccRefreshMessages();

        // 启动无服务器局域网发现, 每 2 秒一次心跳
        lan::init();
        ccStartServe(); // 必须在 lan::init(WSAStartup) 之后, 否则 socket 失败=监视黑屏
        ccCamStart();   // 摄像头捕获线程(仅被监视时才真正打开设备)
        ccCamServeStart(); // 摄像头直播服务 47836
        SetTimer(hWnd, 1, 2000, nullptr);
        lan::tick(g_onlineLabel);

        // VIP 触发: 曾被锁定且配置仍有效 -> 立即重锁; 否则按"计时/定时"模式布防。
        trayAdd(hWnd);
        VipCfg vcfgNow = loadVipCfg();
        bool willLock = !g_isAdmin && vcfgNow.enabled && userNeedsVip(vcfgNow, g_currentUser);
        if (willLock && vcfgNow.trigMode != 1 && !isVipLocked(g_currentUser) &&
            vipCfgIsConsumed(vcfgNow))
            willLock = false;
        if (!g_isAdmin && isVipLocked(g_currentUser) && willLock) {
            SetTimer(hWnd, 3, 300, nullptr);
        } else {
            if (!g_isAdmin && isVipLocked(g_currentUser))
                clearVipLock(g_currentUser);
            if (!willLock) {
                if (deskicon::anyTrace()) deskicon::restore();
                else deskicon::healDesktop();
            }
            vipArm(hWnd, vcfgNow);
        }
        return 0;
    }
    case WM_TIMER:
        if (wParam == 1) { lan::tick(g_onlineLabel); return 0; }
        if (wParam == 2) {
            KillTimer(hWnd, 2);
            VipCfg vcfg = loadVipCfg();
            if (!g_isAdmin && vcfg.enabled && userNeedsVip(vcfg, g_currentUser) &&
                !vipCfgIsConsumed(vcfg))
                ShowVipLock();
            return 0;
        }
        if (wParam == 3) {
            KillTimer(hWnd, 3);
            VipCfg vcfg3 = loadVipCfg();
            if (vcfg3.trigMode != 1 && vipCfgIsConsumed(vcfg3)) {
                // 已消费, 不补锁
            } else if (!g_isAdmin && vcfg3.enabled && userNeedsVip(vcfg3, g_currentUser)) {
                ShowVipLock();
            }
            return 0;
        }
        if (wParam == 4) { vipScheduledTick(hWnd); return 0; }
        break;
    case WM_APP_VIPCFGCHG: {
        VipCfg cfg = loadVipCfg();
        bool need = !g_isAdmin && cfg.enabled && userNeedsVip(cfg, g_currentUser);
        if (!g_vipLock) {
            if (need) vipArm(hWnd, cfg);
            else { KillTimer(hWnd, 2); KillTimer(hWnd, 4); }
        }
        return 0;
    }
    case WM_APP_TRAY:
        if (lParam == WM_LBUTTONDBLCLK) { showFromTray(hWnd); return 0; }
        if (lParam == WM_RBUTTONUP || LOWORD(lParam) == WM_CONTEXTMENU) {
            trayContext(hWnd); return 0;
        }
        return 0;
    case WM_GETMINMAXINFO: {   // 最小 780x500
        MINMAXINFO* mm = (MINMAXINFO*)lParam;
        mm->ptMinTrackSize.x = 800;
        mm->ptMinTrackSize.y = 540;
        return 0;
    }
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) ccDoLayout(hWnd);
        return 0;
    case WM_ERASEBKGND:
        if (g_darkMode) {
            RECT rc; GetClientRect(hWnd, &rc);
            FillRect((HDC)wParam, &rc, cachedBrush(RGB(0x1E,0x1E,0x2E)));
        } else {
            RECT rc; GetClientRect(hWnd, &rc);
            // 微信风格: 浅灰顶栏(#EDEDED) + 浅灰消息底(#F5F5F5)
            RECT hr = {0, 0, rc.right, 70};
            FillRect((HDC)wParam, &hr, cachedBrush(RGB(0xED, 0xED, 0xED)));
            RECT br = {0, 70, rc.right, rc.bottom};
            FillRect((HDC)wParam, &br, cachedBrush(RGB(0xF5, 0xF5, 0xF5)));
            RECT lr = {0, 69, rc.right, 70};
            FillRect((HDC)wParam, &lr, cachedBrush(RGB(0xE0, 0xE0, 0xE0)));
        }
        return 1;
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)wParam;
        HWND hw = (HWND)lParam;
        int id = GetDlgCtrlID(hw);
        if (g_darkMode) {
            SetBkColor(dc, RGB(0x33,0x33,0x48));
            SetTextColor(dc, RGB(0xE0,0xE0,0xE8));
            static HBRUSH s_darkEdit = nullptr;
            if (!s_darkEdit) s_darkEdit = CreateSolidBrush(RGB(0x33,0x33,0x48));
            return (LRESULT)s_darkEdit;
        }
        // 微信浅色: 侧栏列表=#EDEDED, 输入/搜索框=白底
        if (id == IDC_CC_GROUPS) {
            SetBkColor(dc, RGB(0xED, 0xED, 0xED));
            SetTextColor(dc, RGB(0x1A, 0x1F, 0x2E));
            static HBRUSH sb = CreateSolidBrush(RGB(0xED, 0xED, 0xED));
            return (LRESULT)sb;
        }
        SetBkColor(dc, RGB(0xFF, 0xFF, 0xFF));
        SetTextColor(dc, RGB(0x1A, 0x1F, 0x2E));
        static HBRUSH sw = CreateSolidBrush(RGB(0xFF, 0xFF, 0xFF));
        return (LRESULT)sw;
    }
    case WM_CTLCOLORSTATIC:
        if (g_darkMode) {
            HDC dc = (HDC)wParam;
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(0xC8,0xC8,0xD8));
            return (LRESULT)GetStockObject(NULL_BRUSH);
        }
        // 微信浅色: 标题行(#EDEDED), 其余(#F5F5F5)
        {
            HDC dc = (HDC)wParam;
            HWND hw = (HWND)lParam;
            int id = GetDlgCtrlID(hw);
            static HBRUSH hHdr = CreateSolidBrush(RGB(0xED, 0xED, 0xED));
            static HBRUSH hBody = CreateSolidBrush(RGB(0xF5, 0xF5, 0xF5));
            if (id == IDC_CC_TITLE) {
                SetBkColor(dc, RGB(0xED, 0xED, 0xED));
                SetTextColor(dc, RGB(0x1A, 0x1F, 0x2E));
                return (LRESULT)hHdr;
            }
            SetBkColor(dc, RGB(0xF5, 0xF5, 0xF5));
            if (id == IDC_ONLINE) {
                bool online = GetPropW(hw, L"onlineColor") != (HANDLE)0;
                SetTextColor(dc, online ? RGB(0x1F, 0xA8, 0x4B) : RGB(0xA0, 0xA6, 0xB0));
            } else if (id == IDC_CC_SINFO) {
                SetTextColor(dc, RGB(0x6B, 0x76, 0x85));
            } else {
                SetTextColor(dc, RGB(0x1A, 0x1F, 0x2E));
            }
            return (LRESULT)hBody;
        }
    case WM_INITMENUPOPUP:
        if (g_ccMainMenu)
            CheckMenuItem(g_ccMainMenu, IDM_DARK,
                          MF_BYCOMMAND | (g_darkMode ? MF_CHECKED : MF_UNCHECKED));
        break;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hWnd, &ps);
        SetBkMode(dc, TRANSPARENT);
        RECT rc = {30, 8, 600, 38};
        SetTextColor(dc, g_darkMode ? RGB(0xE0,0xE0,0xE8) : RGB(0x1A,0x1F,0x2E));
        SelectObject(dc, hFontTitle);
        DrawTextW(dc, L"班聊", -1, &rc, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        std::wstring sub = L"当前用户: " + g_currentUser + L"  [" + g_role + L"]";
        RECT rc2 = {32, 40, 600, 62};
        SetTextColor(dc, g_darkMode ? RGB(0xA8,0xA8,0xC0) : RGB(0x6B,0x76,0x85));
        SelectObject(dc, hFontSmall);
        DrawTextW(dc, sub.c_str(), -1, &rc2, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT di = (LPDRAWITEMSTRUCT)lParam;
        if (di->CtlID == IDC_CC_GROUPS && di->itemID >= 0
            && di->itemID < (int)g_ccVisible.size()) {
            const CcGroup& g = g_ccGroups[g_ccVisible[di->itemID]];
            bool sel = (di->itemState & ODS_SELECTED) != 0;
            FillRect(di->hDC, &di->rcItem, cachedBrush(sel ? ACCENT_SOFT
                              : (g_darkMode ? RGB(0x25,0x25,0x38) : COL_SIDEBAR)));
            if (sel) {   // 选中条左侧主题色竖条
                RECT bar = {di->rcItem.left, di->rcItem.top, di->rcItem.left + 4, di->rcItem.bottom};
                FillRect(di->hDC, &bar, cachedBrush(ACCENT));
            }
            SetBkMode(di->hDC, TRANSPARENT);
            RECT tx = {di->rcItem.left + 14, di->rcItem.top, di->rcItem.right - 44, di->rcItem.bottom};
            SetTextColor(di->hDC, sel ? ACCENT_DARK
                         : (g_darkMode ? RGB(0xD8,0xD8,0xE4) : COL_TEXT));
            SelectObject(di->hDC, hFontUI);
            DrawTextW(di->hDC, g.name.c_str(), -1, &tx,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            auto it = g_ccUnread.find(g.id);
            if (it != g_ccUnread.end() && it->second > 0) {   // 右上角红圈未读数
                int cx = di->rcItem.right - 20, cy = (di->rcItem.top + di->rcItem.bottom) / 2 - 2;
                HGDIOBJ op = SelectObject(di->hDC, GetStockObject(NULL_PEN));
                HGDIOBJ ob = SelectObject(di->hDC, cachedBrush(RGB(0xF0, 0x3E, 0x3E)));
                Ellipse(di->hDC, cx - 11, cy - 11, cx + 11, cy + 11);
                SelectObject(di->hDC, op);
                SelectObject(di->hDC, ob);
                wchar_t nb[8];
                if (it->second > 99) wcscpy(nb, L"99+");
                else swprintf(nb, 8, L"%d", it->second);
                SetTextColor(di->hDC, COL_WHITE);
                SelectObject(di->hDC, hFontSmall);
                RECT bd = {cx - 12, cy - 12, cx + 12, cy + 12};
                DrawTextW(di->hDC, nb, -1, &bd, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
        } else if (di->CtlID == IDC_CC_GROUPS) {
            FillRect(di->hDC, &di->rcItem, cachedBrush(g_darkMode ? RGB(0x25,0x25,0x38) : COL_SIDEBAR));
        } else {
            drawButton(di, di->CtlID == IDC_CC_SEND);
        }
        return TRUE; }
    case WM_COMMAND: {
        WORD id = LOWORD(wParam);
        WORD ev = HIWORD(wParam);
        if (id == IDC_CC_GROUPS && ev == LBN_SELCHANGE) {
            int sel = (int)SendMessageW(g_ccList, LB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < (int)g_ccVisible.size()) {
                g_ccCur = g_ccGroups[g_ccVisible[sel]].id;
                g_ccUnread.erase(g_ccCur);   // 进入会话清零未读
                InvalidateRect(g_ccList, nullptr, FALSE);
                ccRefreshMessages();
            }
            return 0;
        }
        if (id == IDC_CC_SEARCH && ev == EN_CHANGE) {   // 实时搜索过滤+高亮
            wchar_t sb[256] = {0};
            GetDlgItemTextW(hWnd, IDC_CC_SEARCH, sb, 256);
            g_ccSearchText = sb;
            g_ccvUp = 0;
            InvalidateRect(g_ccView, nullptr, FALSE);
            UpdateWindow(g_ccView);   // 同步绘制后读取命中数
            int n = ccSearchCount();
            std::wstring info = g_ccSearchText.empty() ? L"" :
                                (L"找到 " + std::to_wstring(n) + L" 条");
            SetDlgItemTextW(hWnd, IDC_CC_SINFO, info.c_str());
            return 0;
        }
        if (id == IDC_CC_SEARCHCLR) {
            SetDlgItemTextW(hWnd, IDC_CC_SEARCH, L"");
            SetDlgItemTextW(hWnd, IDC_CC_SINFO, L"");
            g_ccSearchText.clear();
            g_ccvUp = 0;
            InvalidateRect(g_ccView, nullptr, FALSE);
            SetFocus(g_ccInput);
            return 0;
        }
        if (id == IDM_DARK) {
            g_darkMode = !g_darkMode;
            ccApplyTheme(hWnd);
            return 0;
        }
        if (id == IDM_LOG) {
            if (g_isAdmin) ccOpenLog();
            return 0;
        }
        if (id == IDM_HIST) {   // 打开聊天记录文件夹
            std::wstring d = ccHistDir();
            ShellExecuteW(nullptr, L"explore", d.c_str(), nullptr, nullptr, SW_SHOW);
            return 0;
        }
        if (id == IDM_CMD) {
            if (g_isAdmin) ccOpenCmdPick();
            else MessageBoxW(hWnd, L"远程指令仅管理员可用。", L"提示", MB_OK | MB_ICONWARNING);
            return 0;
        }
        if (id == IDM_DIMO) {
            if (g_isAdmin) dm::dmOpenDash();
            return 0;
        }
        if (id == IDC_CC_SEND || id == IDOK) {
            wchar_t buf[1024] = {0};
            GetDlgItemTextW(hWnd, IDC_CC_INPUT, buf, 1024);
            std::wstring t = buf;
            if (!t.empty()) { ccSendText(t); SetDlgItemTextW(hWnd, IDC_CC_INPUT, L""); }
            return 0;
        }
        if (id == IDC_CC_EMO_LAUGH) { ccSendEmoji('L'); return 0; }
        if (id == IDC_CC_EMO_CRY)   { ccSendEmoji('C'); return 0; }
        if (id == IDC_CC_EMO_BOMB)  { ccSendEmoji('B'); return 0; }
        if (id == IDC_CC_FILE)      { ccPickAndSendFile(); return 0; }
        if (id == IDM_NEWGROUP)    { ccOpenNewGroup(); return 0; }
        if (id == IDM_SHARE) {
            if (ccHasRole(g_currentUser, L"教师")) ccToggleShare();   // 仅教师可共享
            else ccWatchTeacher(hWnd);                                // 其他人只能观看
            return 0;
        }
        if (id == IDM_MONITOR) {
            if (g_isAdmin) ccOpenMonitorPick();
            else MessageBoxW(hWnd, L"屏幕监视仅管理员可用。", L"提示", MB_OK | MB_ICONWARNING);
            return 0;
        }
        if (id == IDM_USERS) {
            if (g_isAdmin) ShowUserList();
            else MessageBoxW(hWnd, L"无权限, 仅管理员可查看成员。", L"提示", MB_OK);
            return 0;
        }
        if (id == IDM_VIP) {
            if (g_isAdmin) ShowVipAdmin();
            else MessageBoxW(hWnd, L"无权限, 仅管理员可管理 VIP 模式。", L"提示", MB_OK);
            return 0;
        }
        if (id == IDM_LOGOUT) {
            g_loggingOut = true;
            DestroyWindow(hWnd);
            return 0;
        }
        if (id >= IDM_LOGINAS && id < IDM_LOGINAS + (int)CC_NAMES.size()) {   // 管理员: 以其他成员身份开新窗口
            int idx = id - IDM_LOGINAS;
            if (g_isAdmin && idx < (int)CC_NAMES.size() && !ccIEq(g_currentUser, CC_NAMES[idx])) {
                wchar_t exe[MAX_PATH] = {0};
                GetModuleFileNameW(nullptr, exe, MAX_PATH);
                std::wstring args = L"--as " + std::wstring(CC_NAMES[idx]);
                ShellExecuteW(nullptr, L"open", exe, args.c_str(), nullptr, SW_SHOW);
            }
            return 0;
        }
        if (id == IDM_ABOUT) {
            MessageBoxW(hWnd,
                L"classChat 班聊 (局域网版)\n\n"
                L"  · 群聊: 全员群 / 管理员·学生群 / 两两群 / 自建群\n"
                L"  · 文字消息、表情 (笑/哭/炸弹特效)、文件直传\n"
                L"  · 教师一键共享屏幕, 管理员远程在线监视屏幕\n"
                L"  · 无服务器 P2P, 同一路由器下自动发现\n\n"
                L"登录: 选择团队 + 姓名即可, 无需密码\n"
                L"团队可注册, 成员与角色(管理员/教师/学生)由建团时批量录入。",
                L"关于 classChat", MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        if (id == IDM_TRAY_SHOW) { showFromTray(hWnd); return 0; }
        if (id == IDM_TRAY_EXIT) { g_forceExit = true; DestroyWindow(hWnd); return 0; }
        if (id == IDM_EXIT) { g_forceExit = true; DestroyWindow(hWnd); return 0; }
        break;
    }
    case WM_APP_VIP_REQ: {
        std::unique_ptr<VipReqPayload> vp((VipReqPayload*)wParam);
        if (!g_isAdmin) return 0;
        long long now = lan::nowMs();
        if (vp->id == g_lastVipReqId && now - g_lastVipReqMs < 20000) return 0;
        g_lastVipReqId = vp->id;
        g_lastVipReqMs = now;
        std::wstring msg = L"成员 [" + vp->user + L"] 在设备 [" + vp->machine +
                           L"] 上申请解锁 VIP。\n是否同意? ";
        int r = MessageBoxW(hWnd, msg.c_str(), L"VIP 解锁审批",
            MB_YESNO | MB_ICONQUESTION | MB_TOPMOST | MB_SETFOREGROUND);
        lan::sendVipResult(vp->id, r == IDYES);
        return 0;
    }
    case WM_APP_CCFILE: {
        std::unique_ptr<CcFilePayload> p((CcFilePayload*)wParam);
        // 更新本气泡状态并向发送者回 ACK, 双方状态同步
        if (p->ok) {
            ccSetFileNote(p->gid, p->from, p->fname, L"✓ 已保存到: " + p->saved);
            ccSendAck(p->gid, g_currentUser, p->fname, true);
        } else {
            ccSetFileNote(p->gid, p->from, p->fname, L"接收失败");
            ccSendAck(p->gid, g_currentUser, p->fname, false);
        }
        return 0;
    }
    case WM_CLOSE:
        if (g_trayEnabled && !g_forceExit) {   // 非管理员设备: 关闭按钮=隐藏到托盘
            hideToTray(hWnd);
            return 0;
        }
        DestroyWindow(hWnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hWnd, 1); KillTimer(hWnd, 2); KillTimer(hWnd, 3); KillTimer(hWnd, 4);
        trayRemove();
        g_onlineLabel = nullptr;
        g_ccList = g_ccView = g_ccInput = g_ccTitle = nullptr;
        if (g_vipLock) DestroyWindow(g_vipLock);
        if (g_loggingOut) {
            g_loggingOut = false;
            clearSession();
            g_isAdmin = false;
            g_currentUser.clear();
            g_role.clear();
            CreateLoginWindow();
        } else {
            lan::shutdown();
            PostQuitMessage(0);
        }
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

static void CreateMainWindow() {
    g_trayEnabled = !g_isAdmin;   // 管理员点 X 直接退出; 教师/学生点 X 隐藏到托盘
    WNDCLASSW wc = {};
    wc.lpfnWndProc = MainProc;
    wc.hInstance = g_hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(g_hInst, MAKEINTRESOURCEW(1));
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"NCMMainClass";
    RegisterClassW(&wc);

    HMENU bar = CreateMenu();
    HMENU mChat = CreatePopupMenu();
    AppendMenuW(mChat, MF_STRING, IDM_NEWGROUP, L"创建群聊(&N)");
    HMENU mFunc = CreatePopupMenu();
    if (ccHasRole(g_currentUser, L"教师"))
        AppendMenuW(mFunc, MF_STRING, IDM_SHARE, L"教师屏幕 (开始/停止共享)(&S)");
    else
        AppendMenuW(mFunc, MF_STRING, IDM_SHARE, L"观看教师屏幕(&S)");
    if (g_isAdmin)
        AppendMenuW(mFunc, MF_STRING, IDM_MONITOR, L"监视屏幕 (&M)");
    if (g_isAdmin) AppendMenuW(mFunc, MF_STRING, IDM_CMD, L"远程指令: 锁定/关机(&R)");
    if (g_isAdmin) AppendMenuW(mFunc, MF_STRING, IDM_DIMO, L"dimo 课堂控制台 (监视/监听/喊话)(&K)");
    HMENU mView = CreatePopupMenu();
    AppendMenuW(mView, MF_STRING, IDM_DARK, L"深色模式(&D)");
    HMENU mAcc = CreatePopupMenu();
    if (g_isAdmin) {
        AppendMenuW(mAcc, MF_STRING, IDM_USERS, L"成员名单(&U)");
        AppendMenuW(mAcc, MF_STRING, IDM_VIP, L"VIP 模式管理(&V)");
        AppendMenuW(mAcc, MF_STRING, IDM_LOG, L"操作日志(&O)");
        AppendMenuW(mAcc, MF_SEPARATOR, 0, nullptr);
    }
    if (g_isAdmin || ccHasRole(g_currentUser, L"教师")) {
        AppendMenuW(mAcc, MF_STRING, IDM_HIST, L"聊天记录文件夹(&C)");
        AppendMenuW(mAcc, MF_SEPARATOR, 0, nullptr);
    }
    if (g_isAdmin) {
        // 管理员可一键打开以其他成员身份登录的新窗口
        for (size_t i = 0; i < CC_NAMES.size(); ++i) {
            if (ccIEq(CC_NAMES[i], g_currentUser)) continue;
            std::wstring t = L"登录【"; t += CC_NAMES[i]; t += L"】";
            AppendMenuW(mAcc, MF_STRING, IDM_LOGINAS + (int)i, t.c_str());
        }
        AppendMenuW(mAcc, MF_SEPARATOR, 0, nullptr);
    }
    AppendMenuW(mAcc, MF_STRING, IDM_LOGOUT, L"注销登录(&L)");
    HMENU mHelp = CreatePopupMenu();
    AppendMenuW(mHelp, MF_STRING, IDM_ABOUT, L"关于(&A)");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)mChat, L"聊天(&C)");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)mFunc, L"功能(&F)");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)mView, L"查看(&V)");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)mAcc,  L"账号(&A)");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)mHelp, L"帮助(&H)");
    g_ccMainMenu = bar;

    ccLoadTheme();   // 读取 theme.dat 恢复深色/浅色

    RECT rc = {0, 0, 880, 600};
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, TRUE);
    g_mainWnd = CreateWindowW(L"NCMMainClass", L"classChat 班聊",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
        nullptr, bar, g_hInst, nullptr);
    centerWindow(g_mainWnd);
    if (g_startHidden && g_trayEnabled) {
        ShowWindow(g_mainWnd, SW_HIDE);   // 开机静默驻留托盘, 不自动弹出
        g_trayHidden = true;
        g_startHidden = false;
    } else {
        ShowWindow(g_mainWnd, SW_SHOW);
    }
    UpdateWindow(g_mainWnd);
    dm::dmStartSession(g_currentUser, g_isAdmin);   // dimo: 学生端静默服务 / 教师端心跳
}


// =========================== 全局状态 ===========================
static HINSTANCE g_hInst = nullptr;
static HWND g_loginWnd = nullptr;
static HWND g_mainWnd  = nullptr;
static HWND g_progress = nullptr;
static HWND g_status   = nullptr;
static HWND g_saveBtn  = nullptr;
static HWND g_listview = nullptr;

static HFONT hFontUI = nullptr, hFontBold = nullptr, hFontTitle = nullptr,
             hFontSmall = nullptr, hFontZoneT = nullptr, hFontZoneS = nullptr;

static bool   g_isAdmin = false;
static std::wstring g_currentUser, g_role;
static bool   g_busy = false;
static bool   g_loggingOut = false;
static bool   g_registerOpen = false;
static HANDLE g_thread = nullptr;

static std::vector<std::wstring> g_queue;
static int       g_queueIndex = 0;
static long long g_totalBytes = 0, g_doneBytes = 0, g_curFileSize = 0;
static int       g_fmtChoice = FMT_AUTO;   // 当前选择的解密格式
static HWND      g_fmtCombo = nullptr;
static HWND      g_onlineLabel = nullptr;

// VIP 锁屏
static HWND               g_vipLock = nullptr;   // 全屏锁屏窗口
static unsigned long long g_vipReqId = 0;        // 待审批的申请号
static bool               g_lockCfgNeedPass = false;      // 锁屏窗口创建时的配置快照
static bool               g_lockCfgNeedApproval = false;  // (用于配置变化时判断是否重建 UI)
static unsigned long long g_lastVipReqId = 0;    // 管理员端去重
static long long          g_lastVipReqMs = 0;
static int                g_schedFiredKey = -1;  // 定时触发当天已触发标记(月*32+日)

// 各格式对应扩展名(小写)
static const wchar_t* fmtExt(int choice) {
    switch (choice) {
    case FMT_NCM:  return L"ncm";
    case FMT_MG3D: return L"mg3d";
    }
    return nullptr;
}
static bool isSupportedExt(const std::wstring& e) {
    return e == L"ncm" || e == L"mg3d";
}

struct ResultRec {
    std::wstring path, song, err, fmt;
    bool ok = false;
    long long size = 0;
    std::vector<uint8_t> data;
};
static std::vector<ResultRec> g_results;

static RECT g_dropRect = {30, 116, 610, 288};

// 前向声明
static void CreateLoginWindow();
static void CreateMainWindow();
static void ShowUserList();
static void OpenChooseFiles(HWND owner);
static void StartQueue();

// =========================== 记住登录状态 ===========================
static std::wstring sessionPath() {
    return dataPathFor(L"session.db");
}
static void saveSession(const std::wstring& name, const std::wstring& pass) {
    FILE* fp = _wfopen(sessionPath().c_str(), L"wb");
    if (!fp) return;
    std::string line = wToUtf8(name) + "|" + wToUtf8(pass) + "\n";
    fwrite(line.data(), 1, line.size(), fp);
    fclose(fp);
}
static void clearSession() { DeleteFileW(sessionPath().c_str()); dm::dmLeave(); }
// 读取保存的登录信息并校验, 成功则填充全局用户状态
static bool tryAutoLogin() {
    FILE* fp = _wfopen(sessionPath().c_str(), L"rb");
    if (!fp) return false;
    std::string all;
    char buf[256]; size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) all.append(buf, n);
    fclose(fp);
    size_t p1 = all.find('|');
    if (p1 == std::string::npos) return false;
    std::wstring user = utf8ToW(all.substr(0, p1));
    if (user.empty() || !ccFindTeamUser(user)) return false;
    g_isAdmin = ccIsAdmin(user); g_currentUser = user; g_role = ccPrimaryRole(user);
    return true;
}

// =========================== 设备绑定 (一机一号) ===========================
// 除管理员开发机(计算机名 Zhao)外, 每台设备首次登录后绑定该账号, 以后免登录;
// 绑定后其他账号无法在本机登录。
static std::wstring machineNameW() {
    wchar_t buf[256] = {0};
    DWORD n = 256;
    GetComputerNameW(buf, &n);
    return buf;
}
static bool isDevMachine() {
    std::wstring n = machineNameW();
    return _wcsicmp(n.c_str(), L"Zhao") == 0;
}
// 开机自启: 除开发机(赵昱淏, 计算机名 Zhao)外全部写入用户启动项 --tray 静默驻留
// 若已由 Setup 写入 HKLM 全用户自启(per-machine 安装), 则不再重复写 HKCU
static bool syncAutoRun() {
    // per-machine 已配置 → 不写用户级, 避免开机双启动
    {
        HKEY hkAll = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                0, KEY_READ, &hkAll) == ERROR_SUCCESS) {
            bool exists = RegQueryValueExW(hkAll, L"classChat", nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
            RegCloseKey(hkAll);
            if (exists) return true;
        }
    }
    HKEY hk = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
            0, nullptr, 0, KEY_SET_VALUE, nullptr, &hk, nullptr) != ERROR_SUCCESS)
        return false;
    bool ok;
    if (isDevMachine()) {
        ok = (RegDeleteValueW(hk, L"classChat") == ERROR_SUCCESS || GetLastError() == ERROR_FILE_NOT_FOUND);
    } else {
        wchar_t exe[MAX_PATH] = {0};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring v = std::wstring(L"\"") + exe + L"\" --tray";
        ok = RegSetValueExW(hk, L"classChat", 0, REG_SZ, (const BYTE*)v.c_str(),
                            (DWORD)((v.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
    }
    RegCloseKey(hk);
    return ok;
}

// =========================== 强制更新 (1.0.X) ===========================
// 更新源写在程序目录 upd.cfg, 每行一个候选目录 (可直接写 UNC 共享), '#' 开头为注释。
// 启动(含开机静默启动)时若发现更新源里有更高版本的 classChat.exe, 立即静默替换并重启。
static std::wstring appDirPath() {
    wchar_t exe[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring d(exe);
    size_t p = d.find_last_of(L"\\/");
    if (p != std::wstring::npos) d = d.substr(0, p + 1);
    return d;
}
static std::wstring selfExeFullPath() {
    wchar_t exe[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    return exe;
}
// 读 exe 的 VS_FIXEDFILEINFO 版本号
static bool fileVerOf(const std::wstring& path, DWORD& ms, DWORD& ls) {
    DWORD dummy = 0;
    DWORD sz = GetFileVersionInfoSizeW(path.c_str(), &dummy);
    if (!sz) return false;
    std::vector<BYTE> buf(sz);
    if (!GetFileVersionInfoW(path.c_str(), 0, sz, buf.data())) return false;
    VS_FIXEDFILEINFO* fi = nullptr;
    UINT len = 0;
    if (!VerQueryValueW(buf.data(), L"\\", (void**)&fi, &len) || !fi) return false;
    ms = fi->dwFileVersionMS;
    ls = fi->dwFileVersionLS;
    return true;
}
static std::vector<std::wstring> updSources() {
    std::vector<std::wstring> out;
    FILE* fp = _wfopen((appDirPath() + L"upd.cfg").c_str(), L"rb");
    if (!fp) return out;
    std::string all;
    char b[512]; size_t n;
    while ((n = fread(b, 1, sizeof(b), fp)) > 0) all.append(b, n);
    fclose(fp);
    size_t pos = 0;
    while (true) {
        size_t e = all.find('\n', pos);
        if (e == std::string::npos) e = all.size();
        std::string line = all.substr(pos, e - pos);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
            line.pop_back();
        if (!line.empty() && line[0] != '#') out.push_back(utf8ToW(line));
        if (e >= all.size()) break;
        pos = e + 1;
    }
    return out;
}
// 检查并执行强制更新; 返回 true = 已拉起更新器(调用方应立即退出)
static bool updCheckAndApply() {
    std::vector<std::wstring> srcs = updSources();
    if (srcs.empty()) return false;
    for (size_t i = 0; i < srcs.size(); ++i) {
        std::wstring cand = srcs[i] + L"\\classChat.exe";
        if (GetFileAttributesW(cand.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
        DWORD ms = 0, ls = 0;
        if (!fileVerOf(cand, ms, ls)) continue;
        bool newer = (ms > (DWORD)APP_VER_MS) || (ms == (DWORD)APP_VER_MS && ls > (DWORD)APP_VER_LS);
        if (!newer) continue;
        std::wstring tmp = dataDirPath() + L"classChat.upd.exe";   // 数据目录可写(Program Files 不可写)
        DeleteFileW(tmp.c_str());
        if (!CopyFileW(cand.c_str(), tmp.c_str(), FALSE)) continue;   // 复制失败换下一个源
        std::wstring me = selfExeFullPath();
        std::wstring cs = L"\"" + tmp + L"\" --apply \"" + me + L"\" " +
                          std::to_wstring((unsigned long)GetCurrentProcessId());
        std::vector<wchar_t> cmdbuf(cs.begin(), cs.end());
        cmdbuf.push_back(0);
        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi = {};
        if (!CreateProcessW(nullptr, cmdbuf.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
            DeleteFileW(tmp.c_str());
            continue;
        }
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        return true;
    }
    return false;
}
// 更新器模式: classChat.upd.exe --apply "<目标exe>" <旧进程pid> → 等旧进程退出, 覆盖自身后静默重启
static void updApplyFromCmdLine(const std::wstring& cl) {
    size_t ap = cl.find(L"--apply");
    if (ap == std::wstring::npos) return;
    std::wstring rest = cl.substr(ap + 7);
    size_t q1 = rest.find(L'"');
    if (q1 == std::wstring::npos) return;
    size_t q2 = rest.find(L'"', q1 + 1);
    if (q2 == std::wstring::npos) return;
    std::wstring target = rest.substr(q1 + 1, q2 - q1 - 1);
    unsigned pid = (unsigned)_wtoi(rest.substr(q2 + 1).c_str());
    if (pid) {
        HANDLE hp = OpenProcess(SYNCHRONIZE, FALSE, pid);
        if (hp) { WaitForSingleObject(hp, 30000); CloseHandle(hp); }
    }
    Sleep(700);                                  // 等旧进程彻底释放映像
    std::wstring old = target + L".old";
    DeleteFileW(old.c_str());
    MoveFileExW(target.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING);
    bool ok = MoveFileExW(selfExeFullPath().c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING);
    if (!ok && !IsUserAnAdmin() && cl.find(L"--elev") == std::wstring::npos) {
        // Program Files 不可写: 以管理员身份重跑一次更新器 (UAC 由用户确认, 拒绝则放弃本次更新)
        std::wstring args = L"\"" + selfExeFullPath() + L"\" --apply \"" + target + L"\" " +
                            std::to_wstring((unsigned long)pid) + L" --elev";
        ShellExecuteW(nullptr, L"runas", selfExeFullPath().c_str(), args.c_str(), nullptr, SW_HIDE);
        return;
    }
    if (ok) {
        std::wstring args = L"\"" + target + L"\" --tray";   // 更新后静默回到后台
        std::vector<wchar_t> ab(args.begin(), args.end());
        ab.push_back(0);
        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi = {};
        if (CreateProcessW(nullptr, ab.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
            CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        }
    }
}
// 清理上一次更新遗留的 classChat.exe.old
static void updCleanupOld() {
    std::wstring old = selfExeFullPath() + L".old";
    if (GetFileAttributesW(old.c_str()) != INVALID_FILE_ATTRIBUTES) DeleteFileW(old.c_str());
    std::wstring tmp = dataDirPath() + L"classChat.upd.exe";
    if (GetFileAttributesW(tmp.c_str()) != INVALID_FILE_ATTRIBUTES) DeleteFileW(tmp.c_str());
}
static std::wstring devicePath() {
    return dataPathFor(L"device.db");
}
struct DeviceBind { bool valid = false; std::wstring team; std::wstring user; };
static DeviceBind loadDeviceBind() {
    DeviceBind b;
    FILE* fp = _wfopen(devicePath().c_str(), L"rb");
    if (!fp) return b;
    std::string all;
    char buf[256]; size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) all.append(buf, n);
    fclose(fp);
    size_t p1 = all.find('|');
    if (p1 == std::string::npos) return b;
    b.team = utf8ToW(all.substr(0, p1));
    b.user = utf8ToW(all.substr(p1 + 1));
    while (!b.user.empty() && (b.user.back() == L'\n' || b.user.back() == L'\r')) b.user.pop_back();
    b.valid = !b.team.empty() && !b.user.empty();
    return b;
}
static void saveDeviceBind(const std::wstring& team, const std::wstring& user) {
    FILE* fp = _wfopen(devicePath().c_str(), L"wb");
    if (!fp) return;
    std::string line = wToUtf8(team) + "|" + wToUtf8(user) + "\n";
    fwrite(line.data(), 1, line.size(), fp);
    fclose(fp);
}
// 用绑定信息自动登录, 成功则填充全局用户状态
static bool tryDeviceLogin() {
    DeviceBind b = loadDeviceBind();
    if (!b.valid) return false;
    g_team = b.team;
    ccRefreshNameCache();
    if (!ccFindTeamUser(b.user)) return false;   // 团队/成员失效则回退登录窗
    g_currentUser = b.user;
    g_isAdmin = ccIsAdmin(b.user);
    g_role = ccPrimaryRole(b.user);
    g_teamConfirmed = true;
    return true;
}

// =========================== 系统托盘 ===========================
static bool g_trayEnabled = false;   // 非管理员设备: 关闭按钮隐藏到托盘
static bool g_trayHidden  = false;   // 主窗口当前是否隐藏在托盘
static bool g_trayAdded   = false;
static bool g_forceExit   = false;   // 托盘菜单"退出": 真正结束进程
static bool g_startHidden = false;   // --tray 启动: 自动登录后静默驻留托盘
static HWND g_trayWnd    = nullptr;  // 当前托盘图标的宿主窗口 (静默模式下不是主窗口)
static const UINT kTrayId = 7701;
static void trayAdd(HWND hWnd) {
    if (!g_trayEnabled || g_trayAdded) return;
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hWnd;
    nid.uID = kTrayId;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_APP_TRAY;
    nid.hIcon = LoadIconW(g_hInst, MAKEINTRESOURCEW(1));
    wcscpy(nid.szTip, L"classChat 班聊 (后台运行中)");
    Shell_NotifyIconW(NIM_ADD, &nid);
    g_trayAdded = true;
    g_trayWnd = hWnd;
}
static void trayRemove() {
    if (!g_trayAdded) return;
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_trayWnd ? g_trayWnd : g_mainWnd;
    nid.uID = kTrayId;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    g_trayAdded = false;
    g_trayWnd = nullptr;
}
static void trayNotify(HWND hWnd, const wchar_t* msg) {
    if (!g_trayAdded) return;
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hWnd;
    nid.uID = kTrayId;
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = NIIF_INFO;
    wcsncpy(nid.szInfoTitle, L"NCM 音乐解密工具", ARRAYSIZE(nid.szInfoTitle) - 1);
    wcsncpy(nid.szInfo, msg, ARRAYSIZE(nid.szInfo) - 1);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}
static void hideToTray(HWND hWnd) {
    ShowWindow(hWnd, SW_HIDE);
    g_trayHidden = true;   // 静默: 不弹气泡通知
}
static void showFromTray(HWND hWnd) {
    ShowWindow(hWnd, SW_SHOW);
    ShowWindow(hWnd, SW_RESTORE);
    SetForegroundWindow(hWnd);
    g_trayHidden = false;
}
static void trayContext(HWND hWnd) {
    POINT pt; GetCursorPos(&pt);
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, IDM_TRAY_SHOW, L"显示主界面");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, IDM_TRAY_EXIT, L"退出程序");
    SetForegroundWindow(hWnd);
    TrackPopupMenu(m, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, nullptr);
    DestroyMenu(m);
}


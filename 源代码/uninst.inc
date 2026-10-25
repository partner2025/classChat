// =============================================================================
//  uninst.inc - 卸载模块 (classChat 1.1.X)
//
//  设计原则:
//    · 卸载需要管理员密码 (机房统一管理, 防止误删/恶意脱离)
//    · 卸载前向局域网广播 UNINST, 管理员端(赵昱淏)可收到通知
//    · 管理员可选择「放任」或「请求重装」; 请求重装时本机会弹出明确确认窗,
//      用户同意 → 取消卸载(程序保留); 用户拒绝 → 继续卸载
//    · 不做任何静默自恢复/隐藏标记, 最终决定权始终在这台机器上
//    · 卸载完整清理: 程序目录、AppData/ProgramData、桌面与开始菜单快捷方式、
//      HKLM/HKCU 自启项、HKLM 卸载注册项、HKLM\SOFTWARE\classChat
//
//  入口:  classChat.exe --uninstall
//         (--uninstall-run 由临时副本使用, 负责删除自身所在安装目录)
// =============================================================================

namespace un {

// 卸载协调端口 (与业务端口 47933/47935/47936/47937 隔离, 避免抢包)
static const WORD UN_PORT = 47938;

// ------------------------------ 哈希 (CryptoAPI SHA1) ------------------------------
static std::string sha1hex(const std::string& s) {
    HCRYPTPROV hProv = 0;
    std::string out;
    if (!CryptAcquireContextW(&hProv, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
        return out;
    HCRYPTHASH hHash = 0;
    if (CryptCreateHash(hProv, CALG_SHA1, 0, 0, &hHash)) {
        CryptHashData(hHash, (const BYTE*)s.data(), (DWORD)s.size(), 0);
        BYTE buf[20]; DWORD len = 20;
        if (CryptGetHashParam(hHash, HP_HASHVAL, buf, &len, 0)) {
            char t[8];
            for (DWORD i = 0; i < len; ++i) { sprintf(t, "%02x", buf[i]); out += t; }
        }
        CryptDestroyHash(hHash);
    }
    CryptReleaseContext(hProv, 0);
    return out;
}
static std::string pwdHashOf(const std::wstring& pwd) {
    return sha1hex("classChat:" + wToUtf8(pwd));
}
// 读 HKLM\SOFTWARE\classChat\UninstPwd (Setup 安装时写入); 空串表示未设置密码
static std::wstring storedPwdHash() {
    std::wstring v;
    HKEY hk = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\classChat", 0, KEY_READ, &hk) == ERROR_SUCCESS) {
        wchar_t buf[128] = {0}; DWORD sz = sizeof(buf); DWORD ty = 0;
        if (RegQueryValueExW(hk, L"UninstPwd", nullptr, &ty, (BYTE*)buf, &sz) == ERROR_SUCCESS)
            v = buf;
        RegCloseKey(hk);
    }
    return v;
}

// ------------------------------ 管理员密码验证窗 ------------------------------
static HWND  g_pwEdit = nullptr;
static bool  g_pwPass = false;

static LRESULT CALLBACK unPwProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id == 1) {
            wchar_t buf[128] = {0};
            GetWindowTextW(g_pwEdit, buf, 128);
            std::wstring want = storedPwdHash();
            if (want.empty() || utf8ToW(pwdHashOf(buf)) == want) {
                g_pwPass = true;
                DestroyWindow(h);
            } else {
                MessageBoxW(h, L"密码错误，无法卸载。\n如需卸载，请联系管理员（赵昱淏）。",
                            L"验证失败", MB_OK | MB_ICONWARNING);
            }
            return 0;
        }
        if (id == 2) { DestroyWindow(h); return 0; }
        return 0; }
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
static bool askPassword() {
    std::wstring want = storedPwdHash();
    if (want.empty()) return true;   // 未设置密码(开发机) → 直接放行

    WNDCLASSW wc = {};
    wc.hInstance = g_hInst; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpfnWndProc = unPwProc; wc.lpszClassName = L"CCUNPW";
    RegisterClassW(&wc);

    int W = 480, H = 200;
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    HWND h = CreateWindowExW(WS_EX_DLGMODALFRAME, L"CCUNPW", L"卸载 classChat",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        (sw - W) / 2, (sh - H) / 2, W, H, nullptr, nullptr, g_hInst, nullptr);
    if (!h) return false;

    HFONT f = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    HWND st = CreateWindowExW(0, L"STATIC",
        L"classChat 由机房统一部署，卸载需要管理员密码。\n\n请输入管理员密码后继续：",
        WS_CHILD | WS_VISIBLE, 22, 18, W - 44, 46, h, nullptr, nullptr, nullptr);
    SendMessageW(st, WM_SETFONT, (WPARAM)f, TRUE);

    g_pwEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_PASSWORD | ES_AUTOHSCROLL,
        22, 78, W - 44, 28, h, (HMENU)10, nullptr, nullptr);
    SendMessageW(g_pwEdit, WM_SETFONT, (WPARAM)f, TRUE);

    HWND b1 = CreateWindowExW(0, L"BUTTON", L"确定卸载",
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
        W - 260, 122, 110, 34, h, (HMENU)1, nullptr, nullptr);
    HWND b2 = CreateWindowExW(0, L"BUTTON", L"取消",
        WS_CHILD | WS_VISIBLE, W - 140, 122, 110, 34, h, (HMENU)2, nullptr, nullptr);
    SendMessageW(b1, WM_SETFONT, (WPARAM)f, TRUE);
    SendMessageW(b2, WM_SETFONT, (WPARAM)f, TRUE);

    ShowWindow(h, SW_SHOW);
    SetForegroundWindow(h);
    SetFocus(g_pwEdit);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (IsDialogMessageW(h, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return g_pwPass;
}

// ------------------------------ 卸载上报 / 等待管理员 ------------------------------
static SOCKET      g_unSock = INVALID_SOCKET;
static volatile LONG g_unReinst = 0;      // 1 = 收到管理员"请求重装"
static volatile LONG g_unStop = 0;        // 1 = 用户点了跳过等待
static volatile LONG g_unWaitDone = 0;    // 1 = 监听线程已结束(25 秒到)

static void sendUninstNotify() {
    SOCKET s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == INVALID_SOCKET) return;
    BOOL b = TRUE;
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, (char*)&b, sizeof(b));
    std::string pkt = std::string("UNINST|") + dm::g_dmInst + "|" +
                      wToUtf8(g_currentUser) + "|" + wToUtf8(machineNameW());
    sockaddr_in to = {};
    to.sin_family = AF_INET; to.sin_port = htons(47933); to.sin_addr.S_un.S_addr = INADDR_BROADCAST;
    for (int i = 0; i < 3; ++i) {   // 广播多发两次, 降低丢包概率
        sendto(s, pkt.c_str(), (int)pkt.size(), 0, (sockaddr*)&to, sizeof(to));
        Sleep(120);
    }
    closesocket(s);
}
static DWORD WINAPI unWaitThread(LPVOID) {
    sockaddr_in a = {};
    a.sin_family = AF_INET; a.sin_port = htons(UN_PORT); a.sin_addr.S_un.S_addr = INADDR_ANY;
    if (bind(g_unSock, (sockaddr*)&a, sizeof(a)) != 0) { InterlockedExchange(&g_unWaitDone, 1); return 0; }
    DWORD end = GetTickCount() + 25000;
    while (!InterlockedCompareExchange(&g_unStop, 0, 0) && GetTickCount() < end) {
        fd_set r; FD_ZERO(&r); FD_SET(g_unSock, &r);
        timeval tv = {0, 200000};
        if (select(0, &r, nullptr, nullptr, &tv) <= 0) continue;
        char buf[512]; int n = recv(g_unSock, buf, sizeof(buf) - 1, 0);
        if (n <= 0) continue;
        buf[n] = 0;
        if (!strncmp(buf, "REINSCMD|", 9)) {
            InterlockedExchange(&g_unReinst, 1);
            break;
        }
    }
    InterlockedExchange(&g_unWaitDone, 1);
    return 0;
}
// 等待窗: "已通知管理员…" + 跳过按钮; 25 秒无回应自动关闭
static LRESULT CALLBACK unWaitProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_COMMAND:
        if (LOWORD(w) == 1 || LOWORD(w) == 2) { InterlockedExchange(&g_unStop, 1); DestroyWindow(h); }
        return 0;
    case WM_CLOSE: InterlockedExchange(&g_unStop, 1); DestroyWindow(h); return 0;
    case WM_TIMER: {
        static int sec = 25;
        if (sec > 0) --sec;
        SetWindowTextW(GetDlgItem(h, 11), (L"已向管理员发送卸载通知，等待回应… (" +
                      std::to_wstring(sec) + L" 秒)").c_str());
        if (InterlockedCompareExchange(&g_unWaitDone, 0, 0)) DestroyWindow(h);
        return 0; }
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
static void waitAdminReply() {
    WNDCLASSW wc = {};
    wc.hInstance = g_hInst; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpfnWndProc = unWaitProc; wc.lpszClassName = L"CCUNWAIT";
    RegisterClassW(&wc);
    int W = 520, H = 170;
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    HWND h = CreateWindowExW(WS_EX_DLGMODALFRAME, L"CCUNWAIT", L"卸载 classChat - 等待管理员",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        (sw - W) / 2, (sh - H) / 2, W, H, nullptr, nullptr, g_hInst, nullptr);
    if (!h) return;
    HFONT f = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    HWND st = CreateWindowExW(0, L"STATIC",
        L"已向管理员发送卸载通知，等待回应… (25 秒)",
        WS_CHILD | WS_VISIBLE, 22, 30, W - 44, 26, h, (HMENU)11, nullptr, nullptr);
    SendMessageW(st, WM_SETFONT, (WPARAM)f, TRUE);
    HWND b = CreateWindowExW(0, L"BUTTON", L"跳过等待，直接卸载",
        WS_CHILD | WS_VISIBLE, W - 220, 80, 190, 34, h, (HMENU)1, nullptr, nullptr);
    SendMessageW(b, WM_SETFONT, (WPARAM)f, TRUE);
    ShowWindow(h, SW_SHOW);
    SetForegroundWindow(h);
    SetTimer(h, 1, 1000, nullptr);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (IsDialogMessageW(h, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

// ------------------------------ 进程 / 文件 / 注册表清理 ------------------------------
static void killOtherInstances() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W pe = { sizeof(pe) };
    DWORD self = GetCurrentProcessId();
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"classChat.exe") != 0) continue;
            if (pe.th32ProcessID == self) continue;
            HANDLE hp = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
            if (hp) { TerminateProcess(hp, 0); CloseHandle(hp); }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    Sleep(400);
}
static void delTree(const std::wstring& dir) {
    WIN32_FIND_DATAW fd;
    HANDLE hf = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (hf != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
            std::wstring p = dir + L"\\" + fd.cFileName;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) delTree(p);
            else {
                SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(p.c_str());
            }
        } while (FindNextFileW(hf, &fd));
        FindClose(hf);
    }
    SetFileAttributesW(dir.c_str(), FILE_ATTRIBUTE_NORMAL);
    RemoveDirectoryW(dir.c_str());
}
static void delRegValue(HKEY root, const wchar_t* key, const wchar_t* name) {
    HKEY hk = nullptr;
    if (RegOpenKeyExW(root, key, 0, KEY_SET_VALUE, &hk) == ERROR_SUCCESS) {
        RegDeleteValueW(hk, name);
        RegCloseKey(hk);
    }
}
static void delDesktopLnk(const std::wstring& lnk) {
    DeleteFileW(lnk.c_str());
}
// 删除桌面上所有指向本程序的快捷方式 (含泛滥快捷方式残留)
static void purgeShortcuts(const std::wstring& exePath) {
    wchar_t buf[MAX_PATH] = {0};
    SHGetFolderPathW(nullptr, CSIDL_DESKTOPDIRECTORY, nullptr, 0, buf);
    std::wstring dirs[2] = { buf, L"" };
    wchar_t cbuf[MAX_PATH] = {0};
    SHGetFolderPathW(nullptr, CSIDL_COMMON_DESKTOPDIRECTORY, nullptr, 0, cbuf);
    dirs[1] = cbuf;

    IShellLinkW* sl = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                IID_IShellLinkW, (void**)&sl)))
        sl = nullptr;

    for (int d = 0; d < 2; ++d) {
        if (dirs[d].empty()) continue;
        WIN32_FIND_DATAW fd;
        HANDLE hf = FindFirstFileW((dirs[d] + L"\\*.lnk").c_str(), &fd);
        if (hf == INVALID_HANDLE_VALUE) continue;
        do {
            std::wstring full = dirs[d] + L"\\" + fd.cFileName;
            bool rm = (wcsstr(fd.cFileName, L"classChat") != nullptr);
            if (!rm && sl) {
                IPersistFile* pf = nullptr;
                if (SUCCEEDED(sl->QueryInterface(IID_IPersistFile, (void**)&pf))) {
                    if (SUCCEEDED(pf->Load(full.c_str(), STGM_READ))) {
                        wchar_t tgt[MAX_PATH] = {0};
                        if (SUCCEEDED(sl->GetPath(tgt, MAX_PATH, nullptr, 0)) &&
                            _wcsicmp(tgt, exePath.c_str()) == 0)
                            rm = true;
                    }
                    pf->Release();
                }
            }
            if (rm) {
                SetFileAttributesW(full.c_str(), FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(full.c_str());
            }
        } while (FindNextFileW(hf, &fd));
        FindClose(hf);
    }
    if (sl) sl->Release();
}
static void purgeStartMenu() {
    wchar_t buf[MAX_PATH] = {0};
    SHGetFolderPathW(nullptr, CSIDL_COMMON_PROGRAMS, nullptr, 0, buf);
    std::wstring base = buf;
    if (!base.empty()) {
        DeleteFileW((base + L"\\classChat 班聊\\classChat 班聊.lnk").c_str());
        RemoveDirectoryW((base + L"\\classChat 班聊").c_str());
        DeleteFileW((base + L"\\classChat 班聊.lnk").c_str());
    }
    wchar_t ubuf[MAX_PATH] = {0};
    SHGetFolderPathW(nullptr, CSIDL_PROGRAMS, nullptr, 0, ubuf);
    std::wstring ubase = ubuf;
    if (!ubase.empty()) {
        DeleteFileW((ubase + L"\\classChat 班聊\\classChat 班聊.lnk").c_str());
        RemoveDirectoryW((ubase + L"\\classChat 班聊").c_str());
        DeleteFileW((ubase + L"\\classChat 班聊.lnk").c_str());
    }
}
static void purgeAppData() {
    wchar_t buf[MAX_PATH] = {0};
    SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, buf);
    std::wstring roam = buf;
    SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, buf);
    std::wstring local = buf;
    SHGetFolderPathW(nullptr, CSIDL_COMMON_APPDATA, nullptr, 0, buf);
    std::wstring prog = buf;
    if (!roam.empty())  delTree(roam + L"\\classChat");
    if (!local.empty()) {
        delTree(local + L"\\classChat");
        delTree(local + L"\\Programs\\classChat");   // 旧版 per-user 安装位置
    }
    if (!prog.empty())  delTree(prog + L"\\classChat");
    // 临时目录里的更新器残留
    wchar_t tmp[MAX_PATH] = {0};
    GetTempPathW(MAX_PATH, tmp);
    DeleteFileW((std::wstring(tmp) + L"classChat.upd.exe").c_str());
    DeleteFileW((std::wstring(tmp) + L"cc_uninst_tmp.exe").c_str());
}
static void purgeRegistry() {
    delRegValue(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", L"classChat");
    delRegValue(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", L"classChat");
    SHDeleteKeyW(HKEY_LOCAL_MACHINE,
                 L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\classChat");
    SHDeleteKeyW(HKEY_LOCAL_MACHINE, L"Software\\classChat");
    SHDeleteKeyW(HKEY_CURRENT_USER, L"Software\\classChat");
}

// ------------------------------ 卸载结束后删除自身 ------------------------------
static bool deferredSelfDelete(const std::wstring& installDir) {
    wchar_t tmp[MAX_PATH] = {0};
    GetTempPathW(MAX_PATH, tmp);
    std::wstring dst = std::wstring(tmp) + L"cc_uninst_tmp.exe";
    DeleteFileW(dst.c_str());
    if (!CopyFileW(selfExeFullPath().c_str(), dst.c_str(), FALSE)) return false;
    std::wstring args = L"\"" + dst + L"\" --uninstall-run \"" + installDir + L"\" " +
                        std::to_wstring((unsigned long)GetCurrentProcessId());
    std::vector<wchar_t> ab(args.begin(), args.end());
    ab.push_back(0);
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(nullptr, ab.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
        return false;
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return true;
}
// --uninstall-run: 等主卸载进程退出后删除安装目录, 再把自己登记为重启后删除
static int runDeferred(const std::wstring& cl) {
    size_t ap = cl.find(L"--uninstall-run");
    std::wstring rest = (ap == std::wstring::npos) ? L"" : cl.substr(ap + 14);
    size_t q1 = rest.find(L'"');
    if (q1 == std::wstring::npos) return 0;
    size_t q2 = rest.find(L'"', q1 + 1);
    if (q2 == std::wstring::npos) return 0;
    std::wstring dir = rest.substr(q1 + 1, q2 - q1 - 1);
    unsigned pid = (unsigned)_wtoi(rest.substr(q2 + 1).c_str());
    if (pid) {
        HANDLE hp = OpenProcess(SYNCHRONIZE, FALSE, pid);
        if (hp) { WaitForSingleObject(hp, 20000); CloseHandle(hp); }
    }
    Sleep(600);
    for (int i = 0; i < 20; ++i) {       // 最多重试 5 秒
        delTree(dir);
        if (GetFileAttributesW(dir.c_str()) == INVALID_FILE_ATTRIBUTES) break;
        Sleep(250);
    }
    MoveFileExW(selfExeFullPath().c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
    return 0;
}

// ------------------------------ 主流程 ------------------------------
static int runUninstall() {
    std::wstring installDir = appDirPath();
    // 开发目录保护: 只清注册表/快捷方式/数据, 不删源码目录
    bool isDevDir = (installDir.find(L"Desktop\\classChat") != std::wstring::npos ||
                     installDir.find(L"Desktop/classChat") != std::wstring::npos);

    // 整机安装(Program Files)的卸载需要管理员权限: 未提权则以管理员身份重启自身
    bool inProgramFiles = (installDir.find(L"Program Files") != std::wstring::npos);
    if (inProgramFiles && !IsUserAnAdmin()) {
        HINSTANCE r = ShellExecuteW(nullptr, L"runas", selfExeFullPath().c_str(),
                                    L"--uninstall", nullptr, SW_SHOWNORMAL);
        if ((INT_PTR)r > 32) return 0;
        MessageBoxW(nullptr, L"需要管理员权限才能完成卸载。", L"卸载 classChat",
                    MB_OK | MB_ICONWARNING);
        return 1;
    }

    WSADATA wsa = {};
    WSAStartup(MAKEWORD(2, 2), &wsa);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);   // 解析 .lnk 目标需要 COM

    if (!askPassword()) { CoUninitialize(); WSACleanup(); return 1; }

    killOtherInstances();

    // 1) 上报管理员
    sendUninstNotify();

    // 2) 等待管理员回应 (25 秒, 可跳过; 25 秒后自动关闭)
    g_unSock = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_unSock != INVALID_SOCKET) {
        CreateThread(nullptr, 0, unWaitThread, nullptr, 0, nullptr);
        waitAdminReply();
        InterlockedExchange(&g_unStop, 1);
        Sleep(200);
        closesocket(g_unSock);
        g_unSock = INVALID_SOCKET;
    }

    // 3) 管理员请求重装 → 本机知情确认 (最终决定权在本机)
    if (InterlockedCompareExchange(&g_unReinst, 0, 0)) {
        int r = MessageBoxW(nullptr,
            L"管理员（赵昱淏）请求保留并重新安装 classChat。\n\n"
            L"  【是】同意：取消本次卸载，程序保留\n"
            L"  【否】拒绝：按你的意愿继续卸载",
            L"管理员请求", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
        if (r == IDYES) {
            MessageBoxW(nullptr, L"已按管理员请求取消卸载，classChat 保持安装状态。",
                        L"卸载已取消", MB_OK | MB_ICONINFORMATION);
            CoUninitialize();
            WSACleanup();
            return 0;
        }
    }

    // 4) 完整清理
    purgeShortcuts(selfExeFullPath());
    purgeStartMenu();
    purgeAppData();
    purgeRegistry();

    // 5) 删除安装目录 (开发目录除外)
    bool dirGone = false;
    if (!isDevDir) dirGone = deferredSelfDelete(installDir);

    std::wstring msg = L"classChat 已卸载。\n\n已清理：\n"
                       L"· 桌面与开始菜单快捷方式\n"
                       L"· AppData / ProgramData 用户数据\n"
                       L"· 开机自启项与卸载注册项\n";
    msg += isDevDir ? L"· 开发目录已保留（未删除源码）" : L"· 程序安装目录";
    MessageBoxW(nullptr, msg.c_str(), L"卸载完成", MB_OK | MB_ICONINFORMATION);

    if (dirGone) ExitProcess(0);
    CoUninitialize();
    WSACleanup();
    return 0;
}

}   // namespace un

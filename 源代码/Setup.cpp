// =============================================================================
//  classChat 班聊 - 安装程序 (classChat 1.1.X)
//
//  1.1.X 起改为「整机安装 (per-machine)」:
//    · 安装到 C:\Program Files\classChat, 需要管理员权限
//    · 卸载入口写入 HKLM 卸载注册项, 卸载需要管理员密码 (机房统一管控)
//    · 开机自启写 HKLM\...\Run (全用户), 学生账户无法删除
//    · 桌面/开始菜单快捷方式建在「公共」位置
//  开发机(桌面存在 classChat\classChat.exe)只覆盖 exe, 不写 HKLM, 便于本地调试。
// =============================================================================

#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef UNICODE
#define UNICODE
#endif
#define WINVER       0x0501
#define _WIN32_WINNT 0x0501
#define _WIN32_IE    0x0501

#include <winsock2.h>
#include <windows.h>
#include <shlobj.h>
#include <shlguid.h>
#include <shlwapi.h>
#include <wincrypt.h>
#include <tlhelp32.h>
#include <aclapi.h>
#include <objbase.h>
#include <string>
#include <vector>

static const wchar_t* APP_DIR_NAME  = L"classChat";
static const wchar_t* APP_FILE_NAME = L"classChat.exe";
static const wchar_t* APP_TITLE     = L"classChat 班聊 (局域网版)";
static const wchar_t* APP_VER       = L"1.1.X";
static const wchar_t* APP_VER_NUM   = L"1.1.0.0";
static const wchar_t* RUN_VALUE     = L"classChat";
static const wchar_t* UNINST_KEY    = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\classChat";
static const wchar_t* CFG_KEY       = L"Software\\classChat";

// 安装包版本: -DCC_ADMIN_SETUP = 管理员安装包; 否则 = 学生(用户)安装包
#ifdef CC_ADMIN_SETUP
static const wchar_t* SETUP_EDITION = L"管理员版";
static const bool SETUP_IS_ADMIN = true;
#else
static const wchar_t* SETUP_EDITION = L"学生端";
static const bool SETUP_IS_ADMIN = false;
#endif

// ------------------------------ 密码哈希 (SHA1) ------------------------------
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
static std::string pwdHashUtf8(const std::wstring& pwd) {
    int n = WideCharToMultiByte(CP_UTF8, 0, pwd.c_str(), (int)pwd.size(), nullptr, 0, nullptr, nullptr);
    std::string u((size_t)n, 0);
    if (n) WideCharToMultiByte(CP_UTF8, 0, pwd.c_str(), (int)pwd.size(), &u[0], n, nullptr, nullptr);
    return sha1hex("classChat:" + u);
}

// ------------------------------ 卸载密码设置窗 ------------------------------
static HWND   g_pwEdit = nullptr;
static bool   g_pwDone = false;
static std::wstring g_pwValue;

static LRESULT CALLBACK SetupPwProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_COMMAND:
        if (LOWORD(w) == 1) {
            wchar_t buf[128] = {0};
            GetWindowTextW(g_pwEdit, buf, 128);
            if (wcslen(buf) < 4) {
                MessageBoxW(h, L"卸载密码至少 4 个字符。", L"提示", MB_OK | MB_ICONWARNING);
                return 0;
            }
            g_pwValue = buf;
            g_pwDone = true;
            DestroyWindow(h);
            return 0;
        }
        if (LOWORD(w) == 2) { DestroyWindow(h); return 0; }
        return 0;
    case WM_CLOSE: DestroyWindow(h); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
// 返回 false = 用户取消安装
static bool askUninstPassword(std::wstring& out) {
    WNDCLASSW wc = {};
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpfnWndProc = SetupPwProc;
    wc.lpszClassName = L"CCSETUPPW";
    RegisterClassW(&wc);

    int W = 500, H = 250;
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    HWND h = CreateWindowExW(WS_EX_DLGMODALFRAME, L"CCSETUPPW", L"设置卸载密码 - classChat",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        (sw - W) / 2, (sh - H) / 2, W, H, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!h) return false;

    HFONT f = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    HWND st = CreateWindowExW(0, L"STATIC",
        L"机房整机安装：卸载本软件需要管理员密码。\n"
        L"学生账户无法通过控制面板卸载。请设置并妥善保管该密码：",
        WS_CHILD | WS_VISIBLE, 22, 16, W - 44, 46, h, nullptr, nullptr, nullptr);
    SendMessageW(st, WM_SETFONT, (WPARAM)f, TRUE);

    g_pwEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"classChat@2026",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        22, 74, W - 44, 28, h, (HMENU)10, nullptr, nullptr);
    SendMessageW(g_pwEdit, WM_SETFONT, (WPARAM)f, TRUE);

    HWND tip = CreateWindowExW(0, L"STATIC",
        L"提示：密码以 SHA1 哈希形式保存在 HKLM，无法反查。遗忘后需重装系统前先记录。",
        WS_CHILD | WS_VISIBLE, 22, 110, W - 44, 34, h, nullptr, nullptr, nullptr);
    SendMessageW(tip, WM_SETFONT, (WPARAM)f, TRUE);

    HWND b1 = CreateWindowExW(0, L"BUTTON", L"确定并安装",
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
        W - 300, 170, 130, 36, h, (HMENU)1, nullptr, nullptr);
    HWND b2 = CreateWindowExW(0, L"BUTTON", L"取消",
        WS_CHILD | WS_VISIBLE, W - 150, 170, 120, 36, h, (HMENU)2, nullptr, nullptr);
    SendMessageW(b1, WM_SETFONT, (WPARAM)f, TRUE);
    SendMessageW(b2, WM_SETFONT, (WPARAM)f, TRUE);

    ShowWindow(h, SW_SHOW);
    SetForegroundWindow(h);
    SetFocus(g_pwEdit);
    SendMessageW(g_pwEdit, EM_SETSEL, 0, -1);

    MSG msg;
    while (IsWindow(h) && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (IsDialogMessageW(h, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (!g_pwDone) return false;
    out = g_pwValue;
    return true;
}

// ------------------------------ 资源释放 ------------------------------
static bool ExtractApp(const std::wstring& dir, std::wstring& outPath, std::wstring& err) {
    HRSRC hRes = FindResourceW(nullptr, MAKEINTRESOURCEW(100), RT_RCDATA);
    if (!hRes) { err = L"安装包中找不到主程序资源"; return false; }
    HGLOBAL hData = LoadResource(nullptr, hRes);
    if (!hData) { err = L"加载主程序资源失败"; return false; }
    DWORD size = SizeofResource(nullptr, hRes);
    void* data = LockResource(hData);
    if (!data || size == 0) { err = L"主程序资源为空"; return false; }

    if (SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr) != ERROR_SUCCESS &&
        GetFileAttributesW(dir.c_str()) == INVALID_FILE_ATTRIBUTES) {
        err = L"无法创建安装目录:\n" + dir;
        return false;
    }
    outPath = dir + L"\\" + APP_FILE_NAME;

    // 覆盖前先结束正在运行的实例
    {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W pe = { sizeof(pe) };
            if (Process32FirstW(snap, &pe)) {
                do {
                    if (_wcsicmp(pe.szExeFile, APP_FILE_NAME) == 0) {
                        HANDLE hp = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                        if (hp) { TerminateProcess(hp, 0); CloseHandle(hp); }
                    }
                } while (Process32NextW(snap, &pe));
            }
            CloseHandle(snap);
        }
        Sleep(500);
    }

    HANDLE hf = CreateFileW(outPath.c_str(), GENERIC_WRITE, 0, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hf == INVALID_HANDLE_VALUE) {
        err = L"无法写入文件(请先关闭正在运行的旧版本):\n" + outPath;
        return false;
    }
    DWORD written = 0;
    BOOL ok = WriteFile(hf, data, size, &written, nullptr);
    CloseHandle(hf);
    if (!ok || written != size) { err = L"写入主程序不完整"; return false; }

    auto extractRes = [&](int id, const wchar_t* name) {
        HRSRC hr = FindResourceW(nullptr, MAKEINTRESOURCEW(id), RT_RCDATA);
        if (!hr) return;
        HGLOBAL hg = LoadResource(nullptr, hr);
        DWORD sz = hg ? SizeofResource(nullptr, hr) : 0;
        void* pd = hg ? LockResource(hg) : nullptr;
        if (!pd || sz == 0) return;
        std::wstring p = dir + L"\\" + name;
        HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD w = 0; WriteFile(h, pd, sz, &w, nullptr); CloseHandle(h);
        }
    };
    extractRes(101, L"qr.png");
    extractRes(102, L"panda.png");
    extractRes(103, L"panda.ico");
    if (GetFileAttributesW((dir + L"\\upd.cfg").c_str()) == INVALID_FILE_ATTRIBUTES)
        extractRes(104, L"upd.cfg");
    return true;
}

// 数据目录: %ProgramData%\classChat, 授权 Users 完全控制
// (程序装在 Program Files 后标准用户不可写, 可写数据必须落在这里)
static bool PrepareDataDir() {
    wchar_t prog[MAX_PATH] = {0};
    if (SHGetFolderPathW(nullptr, CSIDL_COMMON_APPDATA, nullptr, 0, prog) != S_OK)
        return false;
    std::wstring dir = std::wstring(prog) + L"\\classChat";
    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    if (GetFileAttributesW(dir.c_str()) == INVALID_FILE_ATTRIBUTES) return false;

    PSID usersSid = nullptr;
    SID_IDENTIFIER_AUTHORITY ntAuth = SECURITY_NT_AUTHORITY;
    if (!AllocateAndInitializeSid(&ntAuth, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                  DOMAIN_ALIAS_RID_USERS, 0, 0, 0, 0, 0, 0, &usersSid))
        return false;

    EXPLICIT_ACCESSW ea = {};
    ea.grfAccessPermissions = GENERIC_ALL;
    ea.grfAccessMode = SET_ACCESS;
    ea.grfInheritance = OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE;
    ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ea.Trustee.TrusteeType = TRUSTEE_IS_GROUP;
    ea.Trustee.ptstrName = (LPWSTR)usersSid;

    PACL newAcl = nullptr;
    bool ok = false;
    if (SetEntriesInAclW(1, &ea, nullptr, &newAcl) == ERROR_SUCCESS) {
        ok = SetNamedSecurityInfoW((LPWSTR)dir.c_str(), SE_FILE_OBJECT,
                                   DACL_SECURITY_INFORMATION, nullptr, nullptr, newAcl, nullptr) == ERROR_SUCCESS;
        LocalFree(newAcl);
    }
    FreeSid(usersSid);
    return ok;
}

// ------------------------------ 进程 / 文件 / 注册表 ------------------------------
static bool WriteRunHKLM(const std::wstring& exePath) {
    HKEY hk = nullptr;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
            0, nullptr, 0, KEY_SET_VALUE, nullptr, &hk, nullptr) != ERROR_SUCCESS)
        return false;
    std::wstring v = L"\"" + exePath + L"\" --tray";
    bool ok = RegSetValueExW(hk, RUN_VALUE, 0, REG_SZ, (const BYTE*)v.c_str(),
                             (DWORD)((v.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
    RegCloseKey(hk);
    // 清掉可能残留的用户级自启, 避免双启动
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
            0, KEY_SET_VALUE, &hk) == ERROR_SUCCESS) {
        RegDeleteValueW(hk, RUN_VALUE);
        RegCloseKey(hk);
    }
    return ok;
}
// 写卸载注册项: 控制面板/设置的"应用"列表里可见, 卸载入口是带密码的 classChat.exe --uninstall
static bool WriteUninstKey(const std::wstring& exePath, const std::wstring& dir,
                           const std::string& pwdHashUtf8) {
    HKEY hk = nullptr;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, UNINST_KEY, 0, nullptr, 0,
                        KEY_WRITE, nullptr, &hk, nullptr) != ERROR_SUCCESS)
        return false;
    auto setSz = [&](const wchar_t* name, const std::wstring& val) {
        RegSetValueExW(hk, name, 0, REG_SZ, (const BYTE*)val.c_str(),
                       (DWORD)((val.size() + 1) * sizeof(wchar_t)));
    };
    std::wstring displayName = std::wstring(APP_TITLE) + L" - " + SETUP_EDITION;
    setSz(L"DisplayName",     displayName);
    setSz(L"DisplayVersion",  APP_VER_NUM);
    setSz(L"Publisher",       L"classChat");
    setSz(L"InstallLocation", dir);
    setSz(L"DisplayIcon",     exePath);
    setSz(L"UninstallString", L"\"" + exePath + L"\" --uninstall");
    setSz(L"QuietUninstallString", L"\"" + exePath + L"\" --uninstall");
    setSz(L"Comments",        L"局域网课堂聊天与教学管理工具");
    setSz(L"InstallSource",   dir);
    // 安装日期 YYYYMMDD (控制面板"安装日期"列)
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t dateBuf[16];
    swprintf(dateBuf, 16, L"%04d%02d%02d", st.wYear, st.wMonth, st.wDay);
    setSz(L"InstallDate", dateBuf);
    // 估算大小 (KB) - 供"应用和功能"显示占用空间
    WIN32_FILE_ATTRIBUTE_DATA fad;
    DWORD sizeKb = 0;
    if (GetFileAttributesExW(exePath.c_str(), GetFileExInfoStandard, &fad))
        sizeKb = fad.nFileSizeLow / 1024 + 1;
    RegSetValueExW(hk, L"EstimatedSize", 0, REG_DWORD, (const BYTE*)&sizeKb, sizeof(sizeKb));
    // SystemComponent=0 -> 允许在控制面板/应用和功能中显示与卸载
    DWORD zero = 0;
    RegSetValueExW(hk, L"SystemComponent", 0, REG_DWORD, (const BYTE*)&zero, sizeof(zero));
    DWORD dw = 1;
    RegSetValueExW(hk, L"NoModify", 0, REG_DWORD, (const BYTE*)&dw, sizeof(dw));
    RegSetValueExW(hk, L"NoRepair", 0, REG_DWORD, (const BYTE*)&dw, sizeof(dw));
    RegCloseKey(hk);

    // 程序配置项: 卸载密码哈希 + 安装目录
    HKEY ck = nullptr;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, CFG_KEY, 0, nullptr, 0,
                        KEY_WRITE, nullptr, &ck, nullptr) == ERROR_SUCCESS) {
        std::wstring h(pwdHashUtf8.begin(), pwdHashUtf8.end());
        RegSetValueExW(ck, L"UninstPwd", 0, REG_SZ, (const BYTE*)h.c_str(),
                       (DWORD)((h.size() + 1) * sizeof(wchar_t)));
        RegSetValueExW(ck, L"InstallDir", 0, REG_SZ, (const BYTE*)dir.c_str(),
                       (DWORD)((dir.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(ck);
    }
    return true;
}

static bool CreateShortcut(const std::wstring& target, const std::wstring& workDir,
                           const std::wstring& lnkPath) {
    IShellLinkW* sl = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                IID_IShellLinkW, (void**)&sl)))
        return false;
    bool ok = false;
    do {
        sl->SetPath(target.c_str());
        sl->SetWorkingDirectory(workDir.c_str());
        sl->SetDescription(APP_TITLE);
        sl->SetIconLocation(target.c_str(), 0);
        IPersistFile* pf = nullptr;
        if (FAILED(sl->QueryInterface(IID_IPersistFile, (void**)&pf))) break;
        ok = SUCCEEDED(pf->Save(lnkPath.c_str(), TRUE));
        pf->Release();
    } while (false);
    sl->Release();
    return ok;
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    (void)hInst;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    wchar_t desktop[MAX_PATH] = {0}, progFiles[MAX_PATH] = {0};
    wchar_t commonDesktop[MAX_PATH] = {0}, commonPrograms[MAX_PATH] = {0};
    SHGetFolderPathW(nullptr, CSIDL_DESKTOPDIRECTORY, nullptr, 0, desktop);
    SHGetFolderPathW(nullptr, CSIDL_PROGRAM_FILES, nullptr, 0, progFiles);
    SHGetFolderPathW(nullptr, CSIDL_COMMON_DESKTOPDIRECTORY, nullptr, 0, commonDesktop);
    SHGetFolderPathW(nullptr, CSIDL_COMMON_PROGRAMS, nullptr, 0, commonPrograms);

    // 开发机模式: 仅当安装包本身就在桌面开发目录中(且含 main.cpp)时启用,
    // 避免在普通学生机上因桌面恰有 classChat 文件夹而跳过正式安装/注册
    std::wstring devDir = std::wstring(desktop) + L"\\classChat";
    std::wstring devExe = devDir + L"\\" + APP_FILE_NAME;
    wchar_t selfPath[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, selfPath, MAX_PATH);
    std::wstring selfDir = selfPath;
    size_t sp = selfDir.find_last_of(L"\\/");
    if (sp != std::wstring::npos) selfDir = selfDir.substr(0, sp);
    std::wstring devMain = devDir + L"\\main.cpp";
    bool devMode = (_wcsicmp(selfDir.c_str(), devDir.c_str()) == 0) &&
                   (GetFileAttributesW(devMain.c_str()) != INVALID_FILE_ATTRIBUTES) &&
                   (GetFileAttributesW(devExe.c_str()) != INVALID_FILE_ATTRIBUTES);

    if (devMode) {
        if (MessageBoxW(nullptr,
                L"检测到桌面开发目录 classChat。\n\n"
                L"将以开发机模式安装：只更新该目录下的 classChat.exe，\n"
                L"不写入 Program Files / 开机自启 / 卸载注册项。\n\n是否继续？",
                L"安装 classChat (开发机模式)",
                MB_YESNO | MB_ICONQUESTION) != IDYES) {
            CoUninitialize();
            return 0;
        }
        std::wstring realExe, err;
        if (!ExtractApp(devDir, realExe, err)) {
            MessageBoxW(nullptr, err.c_str(), L"安装失败", MB_OK | MB_ICONERROR);
            CoUninitialize();
            return 1;
        }
        std::wstring deskLnk = std::wstring(desktop) + L"\\classChat 班聊.lnk";
        CreateShortcut(realExe, devDir, deskLnk);
        MessageBoxW(nullptr, (L"开发机模式已更新:\n" + realExe).c_str(),
                    L"更新完成", MB_OK | MB_ICONINFORMATION);
        CoUninitialize();
        return 0;
    }

    // ---- 普通(机房)整机安装 ----
    std::wstring confirm =
        L"即将安装 “" + std::wstring(APP_TITLE) + L" " + APP_VER + L" - " + SETUP_EDITION + L"”。\n\n"
        L"· 安装到 C:\\Program Files\\classChat（整机安装，需管理员权限）\n"
        L"· 开机自动在后台静默运行（托盘驻留）\n"
        L"· 桌面与开始菜单快捷方式建在「公共」位置\n";
#ifdef CC_ADMIN_SETUP
    confirm += L"· 管理员版可在程序中注册团队与成员\n"
               L"· 卸载需要管理员密码，学生账户无法自动卸载\n"
               L"· 会在「设置 → 应用」中注册为正式应用，可正常卸载\n";
#else
    confirm += L"· 学生端不可注册团队，仅加入管理员已创建的团队\n";
#endif
    confirm += L"\n是否继续？";
    if (MessageBoxW(nullptr, confirm.c_str(), L"安装 classChat",
                    MB_YESNO | MB_ICONQUESTION) != IDYES) {
        CoUninitialize();
        return 0;
    }

    std::wstring pwd;
#ifdef CC_ADMIN_SETUP
    if (!askUninstPassword(pwd)) {
        MessageBoxW(nullptr, L"已取消安装。", L"classChat", MB_OK | MB_ICONINFORMATION);
        CoUninitialize();
        return 0;
    }
#endif

    std::wstring appDir = std::wstring(progFiles) + L"\\" + APP_DIR_NAME;
    std::wstring realExe, err;
    if (!ExtractApp(appDir, realExe, err)) {
        MessageBoxW(nullptr, err.c_str(), L"安装失败", MB_OK | MB_ICONERROR);
        CoUninitialize();
        return 1;
    }

    std::string h = pwdHashUtf8(pwd);
    bool dataOk = PrepareDataDir();
    bool runOk  = WriteRunHKLM(realExe);
#ifdef CC_ADMIN_SETUP
    bool unOk   = WriteUninstKey(realExe, appDir, h);
#else
    bool unOk   = true;   // 学生端不注册控制面板卸载入口, 由管理员统一管理
#endif

    // 公共桌面快捷方式
    std::wstring deskLnk = std::wstring(commonDesktop) + L"\\classChat 班聊.lnk";
    bool d1 = CreateShortcut(realExe, appDir, deskLnk);
    // 公共开始菜单
    std::wstring smDir = std::wstring(commonPrograms) + L"\\classChat 班聊";
    std::wstring smLnk = smDir + L"\\classChat 班聊.lnk";
    SHCreateDirectoryExW(nullptr, smDir.c_str(), nullptr);
    bool d2 = CreateShortcut(realExe, appDir, smLnk);

    // 旧版按用户安装的残留清理 (只删该项的值和目录, 不动整个 Run 键)
    {
        wchar_t localApp[MAX_PATH] = {0};
        SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, localApp);
        HKEY hk = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER,
                L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                0, KEY_SET_VALUE, &hk) == ERROR_SUCCESS) {
            RegDeleteValueW(hk, RUN_VALUE);
            RegCloseKey(hk);
        }
        std::wstring oldExe = std::wstring(localApp) + L"\\Programs\\classChat\\" + APP_FILE_NAME;
        if (GetFileAttributesW(oldExe.c_str()) != INVALID_FILE_ATTRIBUTES)
            DeleteFileW(oldExe.c_str());
        RemoveDirectoryW((std::wstring(localApp) + L"\\Programs\\classChat").c_str());
        // 旧版用户桌面快捷方式
        DeleteFileW((std::wstring(desktop) + L"\\classChat 班聊.lnk").c_str());
    }

    std::wstring msg = std::wstring(L"安装完成!\n\n") + SETUP_EDITION +
                       L"\n程序位置:\n" + realExe +
                       L"\n\n版本: classChat " + APP_VER +
                       L"\n数据目录: " + (dataOk ? L"%ProgramData%\\classChat (已授权所有用户可写)"
                                                  : L"创建失败(标准用户可能无法登录)") +
                       L"\n开启自启: " + (runOk ? L"已写入(全用户)" : L"写入失败") +
#ifdef CC_ADMIN_SETUP
                       L"\n控制面板: 设置 → 应用 → classChat 班聊（管理员版）(可卸载, 需管理员密码)" +
#else
                       L"\n控制面板: 学生端不提供卸载入口, 由管理员统一管理" +
#endif
                       L"\n桌面快捷方式: " + std::wstring(d1 ? L"已创建(公共桌面)" : L"创建失败") +
                       L"\n开始菜单: " + std::wstring(d2 ? L"已创建" : L"创建失败");
#ifdef CC_ADMIN_SETUP
    msg += L"\n\n卸载密码已设置（请记录，遗忘需手工清理注册表）。";
#endif

    int run = MessageBoxW(nullptr, msg.c_str(), L"安装成功", MB_YESNO | MB_ICONINFORMATION);
    if (run == IDYES)
        ShellExecuteW(nullptr, L"open", realExe.c_str(), L"--tray", appDir.c_str(), SW_SHOWNORMAL);

    CoUninitialize();
    return unOk ? 0 : 2;
}

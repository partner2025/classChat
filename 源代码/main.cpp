// =============================================================================
//  classChat - 局域网课堂聊天管理系统
//  单文件 Win32 GUI 程序 (无第三方库依赖, GDI+ 自绘界面, UDP 广播在线发现)
//
//  功能:
//    1. 登录/注册 (users.db 存账号, 团队批量录入成员, 一机一号设备绑定)
//    2. 聊天: 微信式气泡群聊、表情、文件直传(TCP 47834)、撤回/已读回执、深色模式
//    3. 教师共享屏幕 (TCP 47835) / 摄像头直播 (TCP 47836), 管理员监视与反向操控
//    4. 远程指令: 锁定/关机/音量/网页, 操作日志
//    5. dimo 课堂控制台: 监视屏幕(47935)/摄像头(47936)/麦克风(47937)、隔空喊话、锁屏
//    6. VIP 管理 (vip.dat 签名校验), VIP 全屏锁屏
//    7. 附带 NCM/MG3D 音乐解密工具 (流式, 低内存)
//
//  编译 (Dev-C++ 自带 MinGW64, 或直接运行 build.cmd):
//    g++ -std=c++14 -municode -mwindows -O2 main.cpp app_res.o -o classChat.exe ^
//        -luser32 -lgdi32 -lcomctl32 -lcomdlg32 -lole32 -lshell32 ^
//        -lws2_32 -lwinmm -lgdiplus -lavicap32 -luuid
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
#include <ws2tcpip.h>
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shlguid.h>
#include <shlwapi.h>
#include <winver.h>
#include <wincrypt.h>
#include <tlhelp32.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <set>
#include <map>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cctype>
#include <memory>
#include <algorithm>
#include <functional>
#include <ctime>
#include <thread>
#include <atomic>
#include <mutex>

// =========================== 常量 / 配色 ===========================
static const COLORREF ACCENT      = RGB(0x1A, 0x73, 0xE8);   // Google Blue 风格主题蓝
static const COLORREF ACCENT_DARK = RGB(0x15, 0x58, 0xB0);   // 深蓝(按下态)
static const COLORREF ACCENT_LIGHT= RGB(0x4D, 0x9A, 0xF0);   // 浅蓝(hover/选中)
static const COLORREF ACCENT_SOFT = RGB(0xE3, 0xF0, 0xFF);   // 极浅蓝底
static const COLORREF COL_BG      = RGB(0xF0, 0xF4, 0xFA);   // 界面灰蓝底
static const COLORREF COL_SIDEBAR = RGB(0xEB, 0xF1, 0xFB);   // 左侧群列表背景
static const COLORREF COL_TEXT    = RGB(0x1A, 0x1F, 0x2E);
static const COLORREF COL_GRAY    = RGB(0x6B, 0x76, 0x85);
static const COLORREF COL_WHITE   = RGB(0xFF, 0xFF, 0xFF);
static const COLORREF COL_BUBBLE_ME = RGB(0x95, 0xEC, 0x69); // 自己气泡绿(微信)

// 控件 ID
#define IDC_USER_EDIT   1001
#define IDC_PASS_EDIT   1002
#define IDC_LOGIN_BTN   1003
#define IDC_REG_BTN     1005
#define IDC_LB_USER     1101
#define IDC_LB_PASS     1102
#define IDC_LB_HINT     1103

#define IDC_RUSER_EDIT  3001
#define IDC_RPASS_EDIT  3002
#define IDC_RPASS2_EDIT 3003
#define IDC_ROK_BTN     3004
#define IDC_RCANCEL_BTN 3005
#define IDC_RLB_USER    3101
#define IDC_RLB_PASS    3102
#define IDC_RLB_PASS2   3103
#define IDC_RLB_HINT    3104
// 注册板块: 先填用户名, 后按 管理员/教师/学生 顺序勾选身份
#define IDC_RROLE_ADMIN   3006
#define IDC_RROLE_TEACHER 3007
#define IDC_RROLE_STUDENT 3008
#define IDC_RTEAM_EDIT    3009
#define IDC_RLB_ROLE      3105
#define IDC_RLB_TEAM      3106

#define IDC_SELECT_BTN  2002
#define IDC_SAVE_BTN    2003
#define IDC_LISTVIEW    2004
#define IDC_STATUS      2006
#define IDC_ONLINE      2007
#define IDC_FMT_COMBO   2008
#define IDC_FMT_LABEL   2009

#define IDC_UL_LIST     301
#define IDC_UL_CLOSE    302

#define IDM_USERS   4001
#define IDM_LOGOUT  4002
#define IDM_ABOUT   4004
#define IDM_EXIT    4003
#define IDM_OPEN    4005
#define IDM_SAVE    4006
#define IDM_VIP     4007
#define IDM_NEWGROUP 4008
#define IDM_SHARE    4009
#define IDM_MONITOR  4010
#define IDM_LOGINAS  4011   // +0..31 (团队成员数)
#define IDM_DARK     4060   // 深色模式切换
#define IDM_LOG      4061   // 操作日志
#define IDM_CMD      4062   // 远程指令(锁定/关机)
#define IDM_HIST     4063   // 打开聊天记录文件夹(教师/管理员)
#define IDM_DIMO     4064   // dimo 课堂控制台(管理员)
namespace dm { void dmStartSession(const std::wstring&, bool); void dmOpenDash(); void dmLeave(); }

// 聊天控件
#define IDC_CC_GROUPS  6001
#define IDC_CC_MSGS    6002
#define IDC_CC_INPUT   6003
#define IDC_CC_SEND    6004
#define IDC_CC_EMO_LAUGH 6005
#define IDC_CC_EMO_CRY   6006
#define IDC_CC_EMO_BOMB  6007
#define IDC_CC_FILE   6008
#define IDC_CC_TITLE  6009
#define IDC_CC_SEARCH     6010
#define IDC_CC_SEARCHCLR  6011
#define IDC_CC_SINFO      6012

#define WM_APP_CCMSG   (WM_APP + 20)
#define WM_APP_CCFILE  (WM_APP + 21)
#define WM_APP_CCGRP   (WM_APP + 22)
#define WM_APP_CCCAST  (WM_APP + 23)
#define WM_APP_CCMON   (WM_APP + 24)
#define WM_APP_FRAME   (WM_APP + 25)

struct CcMsgPayload { std::wstring gid, from, type, text; };
struct CcFilePayload { std::wstring gid, from, fname, saved; long long size; bool ok; };
struct CcGrpPayload { std::wstring gid, name, csv; };
struct CcCastPayload { std::wstring from, kind, forWho; unsigned long ip; };

// VIP 锁屏控件
#define IDC_VL_PHRASE  5001
#define IDC_VL_PASS    5002
#define IDC_VL_BTN     5003
#define IDC_VL_STATUS  5004
// VIP 管理窗口控件
#define IDC_VA_ENABLE  5101
#define IDC_VA_PHRASE  5102
#define IDC_VA_PASS    5103
#define IDC_VA_LIST    5104
#define IDC_VA_SAVE     5105
#define IDC_VA_CLOSE    5106
#define IDC_VA_APPROVAL 5107
#define IDC_VA_NEEDPASS 5108
#define IDC_VA_RADIO_T  5109
#define IDC_VA_RADIO_C  5110
#define IDC_VA_SECS     5111
#define IDC_VA_TIME     5112

#define WM_APP_PROGRESS (WM_APP + 1)
#define WM_APP_DONE     (WM_APP + 2)
#define WM_APP_VIP_REQ    (WM_APP + 3)
#define WM_APP_VIP_RESULT (WM_APP + 4)
#define WM_APP_VIPCFGCHG  (WM_APP + 5)   // VIP 配置刚被管理员同步, 立即重评估
#define WM_APP_TRAY       (WM_APP + 6)   // 系统托盘回调
#define IDM_TRAY_SHOW     5301
#define IDM_TRAY_EXIT     5302

// 局域网 VIP 解锁申请 / 审批结果 (WM_APP_VIP_REQ 的载荷)
struct VipReqPayload {
    unsigned long long id;
    std::wstring user;
    std::wstring machine;
};
struct VipResultPayload {
    unsigned long long id;
    bool ok;
};

// ==== 版本: classChat 1.1.X (机房管控加固版) ====
#define APP_VER      L"1.1.X"       // 界面显示版本号
#define APP_VER_MS   0x00010100u    // 数值版本 1.1.0.0 (强制更新比较, 与 app.rc FILEVERSION 一致)
#define APP_VER_LS   0x00000000u

// ==== 模块化拆分: 按原文件顺序包含各模块 (单编译单元, 顺序即依赖) ====
#include "utils.inc"
#include "vip.inc"
#include "decrypt.inc"
#include "core.inc"
#include "lan.inc"
#include "mainwin.inc"
#include "chat_mod.inc"
#include "dimo_mod.inc"
#include "uninst.inc"    // 卸载模块 (依赖 dimo 的实例ID / core 的机器名)

// =========================== 版本 / 更新欢迎窗 ===========================
static std::wstring verMarkPath() {
    return dataPathFor(L"ver.db");
}
// 蓝渐变欢迎窗: "您已更新到 classChat 1.0 版" + 蓝色「开始使用」按钮 (仅版本变化后首次启动)
static LRESULT CALLBACK WelcomeProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE:
        CreateWindowExW(0, L"BUTTON", L"开始使用", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            190, 296, 180, 44, h, (HMENU)1, nullptr, nullptr);
        SendMessageW(GetDlgItem(h, 1), WM_SETFONT, (WPARAM)hFontBold, TRUE);
        return 0;
    case WM_COMMAND:
        if (LOWORD(w) == 1) { PostMessageW(h, WM_CLOSE, 0, 0); return 0; }
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        Gdiplus::Graphics g(dc);
        Gdiplus::RectF full(0, 0, (Gdiplus::REAL)rc.right, (Gdiplus::REAL)rc.bottom);
        Gdiplus::LinearGradientBrush bg(full, Gdiplus::Color(255, 0x1A, 0x73, 0xE8),
                                        Gdiplus::Color(255, 0x8A, 0xB4, 0xF8), 90.0f);
        g.FillRectangle(&bg, full);
        Gdiplus::SolidBrush wt(Gdiplus::Color(255, 255, 255, 255));
        Gdiplus::Font fS(L"Microsoft YaHei UI", 15, Gdiplus::FontStyleRegular);
        Gdiplus::Font fT(L"Microsoft YaHei UI", 34, Gdiplus::FontStyleBold);
        Gdiplus::StringFormat cf; cf.SetAlignment(Gdiplus::StringAlignmentCenter);
        g.DrawString(L"您已更新到", -1, &fS,
                     Gdiplus::RectF(0, 118, (Gdiplus::REAL)rc.right, 34), &cf, &wt);
        g.DrawString(L"classChat " APP_VER L" 版", -1, &fT,
                     Gdiplus::RectF(0, 160, (Gdiplus::REAL)rc.right, 70), &cf, &wt);
        EndPaint(h, &ps);
        return 0; }
    case WM_CLOSE: DestroyWindow(h); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
static void showWelcomeOnce() {
    std::wstring mark;
    FILE* fp = _wfopen(verMarkPath().c_str(), L"rb");
    if (fp) {
        char buf[64] = {0};
        size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
        fclose(fp);
        while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = 0;
        mark = utf8ToW(buf);
    }
    if (mark == APP_VER) return;   // 本版本已看过
    WNDCLASSW wc = {};
    wc.hInstance = g_hInst; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr; wc.lpfnWndProc = WelcomeProc;
    wc.lpszClassName = L"CCWELCOME";
    RegisterClassW(&wc);
    int w = 560, hh = 400;
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    HWND h = CreateWindowExW(0, L"CCWELCOME", L"classChat - 更新完成",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        (sw - w) / 2, (sh - hh) / 2, w, hh, nullptr, nullptr, g_hInst, nullptr);
    if (h) {
        ShowWindow(h, SW_SHOW);
        SetForegroundWindow(h);
        MSG m2;
        while (IsWindow(h) && GetMessageW(&m2, nullptr, 0, 0) > 0) {
            if (IsDialogMessageW(h, &m2)) continue;
            TranslateMessage(&m2);
            DispatchMessageW(&m2);
        }
    }
    fp = _wfopen(verMarkPath().c_str(), L"wb");
    if (fp) { std::string v = wToUtf8(APP_VER); fwrite(v.data(), 1, v.size(), fp); fclose(fp); }
}

// =========================== 静默后台模式 (开机启动项) ===========================
// 开机以 --tray 运行: 不创建任何界面窗口, 只保留 dimo 服务能力 + 强制更新检查。
// 托盘图标可唤醒主界面; 唤醒后才真正创建主窗口。
static LRESULT CALLBACK SilentProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_APP_TRAY: {
        UINT ev = (UINT)l;
        if (ev == WM_LBUTTONUP || ev == WM_LBUTTONDBLCLK) {
            PostMessageW(h, WM_APP + 90, 0, 0);
            return 0;
        }
        if (ev == WM_RBUTTONUP) {
            POINT pt; GetCursorPos(&pt);
            HMENU mu = CreatePopupMenu();
            AppendMenuW(mu, MF_STRING, IDM_TRAY_SHOW, L"显示主界面");
            AppendMenuW(mu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(mu, MF_STRING, IDM_TRAY_EXIT, L"退出程序");
            SetForegroundWindow(h);
            int r = (int)TrackPopupMenu(mu, TPM_RIGHTBUTTON | TPM_RETURNCMD, pt.x, pt.y, 0, h, nullptr);
            DestroyMenu(mu);
            if (r == IDM_TRAY_SHOW) PostMessageW(h, WM_APP + 90, 0, 0);
            else if (r == IDM_TRAY_EXIT) { trayRemove(); DestroyWindow(h); }
            return 0;
        }
        return 0; }
    case WM_APP + 90: {   // 唤醒: 托盘交给主窗口, 创建主界面
        static bool woke = false;
        g_startHidden = false;
        g_trayHidden = false;
        trayRemove();
        if (!woke) { woke = true; CreateMainWindow(); }
        else if (g_mainWnd) showFromTray(g_mainWnd);
        return 0; }
    case WM_TIMER:
        if (w == 91) {                        // 每小时复查一次强制更新
            if (updCheckAndApply()) ExitProcess(0);
            return 0;
        }
        return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
static int silentRun() {
    WNDCLASSW wc = {};
    wc.hInstance = g_hInst;
    wc.hIcon = LoadIconW(g_hInst, MAKEINTRESOURCEW(1));
    wc.lpfnWndProc = SilentProc;
    wc.lpszClassName = L"CCSILENT";
    RegisterClassW(&wc);
    HWND h = CreateWindowExW(WS_EX_TOOLWINDOW, L"CCSILENT", L"classChat 后台服务",
                             0, 0, 0, 0, 0, nullptr, nullptr, g_hInst, nullptr);
    if (!h) return 1;
    if (g_trayEnabled) trayAdd(h);                       // 托盘驻留 (非开发机)
    SetTimer(h, 91, 60u * 60u * 1000u, nullptr);         // 每小时强制更新检查
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}

// =========================== WinMain ===========================
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR lpCmdLine, int) {
    g_hInst = hInstance;

    // 更新器模式 (被更新器自身拉起): 等旧进程退出→覆盖 exe→静默重启
    {
        std::wstring cl0 = GetCommandLineW();
        if (cl0.find(L"--apply") != std::wstring::npos) {
            updApplyFromCmdLine(cl0);
            return 0;
        }
        // 卸载 / 卸载收尾删除自身 (临时副本)
        if (cl0.find(L"--uninstall-run") != std::wstring::npos)
            return un::runDeferred(cl0);
#ifndef CC_USER_EDITION
        if (cl0.find(L"--uninstall") != std::wstring::npos)
            return un::runUninstall();
#else
        // 学生端不提供卸载能力, 必须由管理员统一卸载
        if (cl0.find(L"--uninstall") != std::wstring::npos) {
            MessageBoxW(nullptr, L"学生端不支持自行卸载，请联系管理员统一处理。",
                        L"classChat 学生端", MB_OK | MB_ICONINFORMATION);
            return 0;
        }
#endif
    }

    // --tray: 开机自启动时静默驻留(自动登录成功才隐藏, 否则仍显示登录窗)
    if (lpCmdLine && wcsstr(lpCmdLine, L"--tray")) g_startHidden = true;
    // 除开发机(计算机名 Zhao)外: 启用托盘驻留(关闭按钮隐藏而非退出)
    g_trayEnabled = !isDevMachine();

    (void)lpCmdLine;

    // GDI+ (屏幕 JPEG 抓帧/解码)
    Gdiplus::GdiplusStartupInput gpsi;
    ULONG_PTR gdipToken = 0;
    Gdiplus::GdiplusStartup(&gdipToken, &gpsi, nullptr);
    INITCOMMONCONTROLSEX icc = { sizeof(icc) };
    icc.dwICC = ICC_PROGRESS_CLASS | ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&icc);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // 字体
    hFontUI    = CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FF_DONTCARE, L"Microsoft YaHei UI");
    hFontBold  = CreateFontW(-15, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET,
                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FF_DONTCARE, L"Microsoft YaHei UI");
    hFontTitle = CreateFontW(-21, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FF_DONTCARE, L"Microsoft YaHei UI");
    hFontSmall = CreateFontW(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FF_DONTCARE, L"Microsoft YaHei UI");
    hFontZoneT = CreateFontW(-19, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET,
                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FF_DONTCARE, L"Microsoft YaHei UI");
    hFontZoneS = CreateFontW(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FF_DONTCARE, L"Microsoft YaHei UI");

    WNDCLASSW reg = {};
    reg.lpfnWndProc = RegisterProc;
    reg.hInstance = hInstance;
    reg.hCursor = LoadCursor(nullptr, IDC_ARROW);
    reg.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(1));
    reg.hbrBackground = nullptr;
    reg.lpszClassName = L"NCMRegisterClass";
    RegisterClassW(&reg);

    loadTeams();
    ccRefreshNameCache();
    syncAutoRun();   // 除开发机外写入用户启动项 (开机 --tray 静默驻留托盘)
    updCleanupOld(); // 清掉上次更新残留的 .old / .upd.exe
    // 上线即强制更新: 更新源有更高版本则立即静默替换并重启
    if (updCheckAndApply()) ExitProcess(0);
    // --as <姓名>: 管理员辅助窗口, 直接以指定账号登录(跳过设备绑定)
    {
        std::wstring cmd = GetCommandLineW();
        size_t ap = cmd.find(L"--as ");
        if (ap != std::wstring::npos) {
            std::wstring rest = cmd.substr(ap + 5);
            while (!rest.empty() && (rest[0] == L' ' || rest[0] == L'"')) rest.erase(0, 1);
            while (!rest.empty() && (rest.back() == L' ' || rest.back() == L'"')) rest.pop_back();
            std::wstring wantTeam, nm = rest;
            size_t sp = rest.find(L' ');
            if (sp != std::wstring::npos) { wantTeam = rest.substr(0, sp); nm = rest.substr(sp + 1); }
            for (size_t ti = 0; ti < g_teams.size(); ++ti) {
                if (!wantTeam.empty() && g_teams[ti].name != wantTeam) continue;
                for (auto& u : g_teams[ti].users) {
                    if (u.name == nm) {
                        g_team = g_teams[ti].name; ccRefreshNameCache();
                        g_currentUser = u.name;
                        g_isAdmin = ccIsAdmin(u.name);
                        g_role = ccPrimaryRole(u.name);
                        g_teamConfirmed = true;
                        break;
                    }
                }
                if (!g_currentUser.empty()) break;
            }
        }
    }
    // 版本更新后首次启动: 显示蓝渐变欢迎窗 (静默托盘启动不弹)
    if (!g_startHidden) showWelcomeOnce();

    // 开机启动项静默模式: 不创建任何界面窗口, 只跑 dimo 服务 + 强制更新
    if (g_startHidden) {
        if (g_currentUser.empty() && !tryDeviceLogin()) return 0;   // 无绑定账号则静默退出(不弹窗)
        dm::dmStartSession(g_currentUser, g_isAdmin);
        int rc = silentRun();
        DeleteObject(hFontUI); DeleteObject(hFontBold); DeleteObject(hFontTitle);
        DeleteObject(hFontSmall); DeleteObject(hFontZoneT); DeleteObject(hFontZoneS);
        Gdiplus::GdiplusShutdown(gdipToken);
        CoUninitialize();
        return rc;
    }

    // 设备已绑定账号则免登录直接进入; 未绑定/凭据失效 -> 登录窗
    if (!g_currentUser.empty()) {
        CreateMainWindow();
    } else if (tryDeviceLogin()) {
        CreateMainWindow();
    } else {
        g_startHidden = false;   // 需要手动登录: 登录后正常显示主窗口
        CreateLoginWindow();
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        HWND act = GetActiveWindow();
        if (act && IsDialogMessageW(act, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    DeleteObject(hFontUI); DeleteObject(hFontBold); DeleteObject(hFontTitle);
    DeleteObject(hFontSmall); DeleteObject(hFontZoneT); DeleteObject(hFontZoneS);
    Gdiplus::GdiplusShutdown(gdipToken);
    CoUninitialize();
    return (int)msg.wParam;
}



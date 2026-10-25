// =========================== VIP 配置 (本地 vip.dat) ===========================
struct VipCfg {
    bool enabled = false;                  // 是否开启 VIP 锁屏模式
    bool needApproval = true;              // 解锁是否需要管理员同意
    bool needPass = true;                  // 解锁是否需要输入口令密码
    int  trigMode = 0;                     // 0=计时触发(登录/开启后 N 秒) 1=定时触发(每日 HH:MM)
    int  trigSecs = 30;                    // 计时触发秒数
    int  trigHour = 9;                     // 定时触发: 时
    int  trigMin  = 0;                     // 定时触发: 分
    std::wstring phrase = L"很好用";        // 默认口令
    std::wstring pass   = L"111111";       // 默认密码
    std::vector<std::wstring> need;        // 需要 VIP 解锁的用户名
};

static std::wstring vipCfgPath() {
    return dataPathFor(L"vip.dat");
}

static VipCfg loadVipCfg() {
    VipCfg cfg;
    FILE* fp = _wfopen(vipCfgPath().c_str(), L"rb");
    if (!fp) return cfg;   // 不存在则用默认值, 默认关闭
    std::string all;
    char buf[1024]; size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) all.append(buf, n);
    fclose(fp);
    size_t pos = 0;
    while (pos < all.size()) {
        size_t e = all.find('\n', pos);
        if (e == std::string::npos) e = all.size();
        std::string line = all.substr(pos, e - pos);
        pos = e + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.compare(0, 8, "enabled=") == 0)
            cfg.enabled = line.substr(8) == "1";
        else if (line.compare(0, 9, "approval=") == 0)
            cfg.needApproval = line.substr(9) == "1";
        else if (line.compare(0, 9, "needpass=") == 0)
            cfg.needPass = line.substr(9) == "1";
        else if (line.compare(0, 6, "trigm=") == 0)
            cfg.trigMode = atoi(line.c_str() + 6) == 1 ? 1 : 0;
        else if (line.compare(0, 9, "trigsecs=") == 0) {
            int v = atoi(line.c_str() + 9);
            if (v >= 1 && v <= 86400) cfg.trigSecs = v;
        }
        else if (line.compare(0, 6, "trigt=") == 0) {
            std::string t = line.substr(6);
            int h = 0, m = 0;
            if (sscanf(t.c_str(), "%d:%d", &h, &m) == 2 && h >= 0 && h < 24 && m >= 0 && m < 60) {
                cfg.trigHour = h; cfg.trigMin = m;
            }
        }
        else if (line.compare(0, 7, "phrase=") == 0)
            cfg.phrase = utf8ToW(line.substr(7));
        else if (line.compare(0, 5, "pass=") == 0)
            cfg.pass = utf8ToW(line.substr(5));
        else if (line.compare(0, 5, "need=") == 0) {
            std::wstring u = utf8ToW(line.substr(5));
            if (!u.empty()) cfg.need.push_back(u);
        }
    }
    return cfg;
}

static bool saveVipCfg(const VipCfg& cfg) {
    FILE* fp = _wfopen(vipCfgPath().c_str(), L"wb");
    if (!fp) return false;
    std::string all = "enabled=";
    all += cfg.enabled ? "1\n" : "0\n";
    all += "approval=";
    all += cfg.needApproval ? "1\n" : "0\n";
    all += "needpass=";
    all += cfg.needPass ? "1\n" : "0\n";
    char num[40];
    snprintf(num, sizeof(num), "trigm=%d\n", cfg.trigMode); all += num;
    snprintf(num, sizeof(num), "trigsecs=%d\n", cfg.trigSecs); all += num;
    snprintf(num, sizeof(num), "trigt=%02d:%02d\n", cfg.trigHour, cfg.trigMin); all += num;
    all += "phrase=" + wToUtf8(cfg.phrase) + "\n";
    all += "pass=" + wToUtf8(cfg.pass) + "\n";
    for (auto& u : cfg.need) all += "need=" + wToUtf8(u) + "\n";
    bool ok = fwrite(all.data(), 1, all.size(), fp) == all.size();
    fclose(fp);
    return ok;
}

static bool userNeedsVip(const VipCfg& cfg, const std::wstring& user) {
    for (auto& u : cfg.need)
        if (_wcsicmp(u.c_str(), user.c_str()) == 0) return true;
    return false;
}

static std::wstring vipLockPath() {
    return dataPathFor(L"viplock.dat");
}
// 被锁定且尚未解锁过的用户列表 (每行一个 locked=<utf8用户名>)
static std::vector<std::wstring> loadVipLocks() {
    std::vector<std::wstring> out;
    FILE* fp = _wfopen(vipLockPath().c_str(), L"rb");
    if (!fp) return out;
    std::string all;
    char buf[256]; size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) all.append(buf, n);
    fclose(fp);
    size_t pos = 0;
    while (pos < all.size()) {
        size_t e = all.find('\n', pos);
        if (e == std::string::npos) e = all.size();
        std::string line = all.substr(pos, e - pos);
        pos = e + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.compare(0, 7, "locked=") == 0) {
            std::wstring u = utf8ToW(line.substr(7));
            if (!u.empty()) out.push_back(u);
        }
    }
    return out;
}
static bool isVipLocked(const std::wstring& user) {
    for (auto& u : loadVipLocks())
        if (_wcsicmp(u.c_str(), user.c_str()) == 0) return true;
    return false;
}
static bool writeVipLocks(const std::vector<std::wstring>& users) {
    if (users.empty()) { DeleteFileW(vipLockPath().c_str()); return true; }
    FILE* fp = _wfopen(vipLockPath().c_str(), L"wb");
    if (!fp) return false;
    std::string s;
    for (auto& u : users) s += "locked=" + wToUtf8(u) + "\n";
    bool ok = fwrite(s.data(), 1, s.size(), fp) == s.size();
    fclose(fp);
    return ok;
}
static void saveVipLock(const std::wstring& user) {
    if (user.empty() || isVipLocked(user)) return;
    std::vector<std::wstring> v = loadVipLocks();
    v.push_back(user);
    writeVipLocks(v);
}
static void clearVipLock(const std::wstring& user) {
    std::vector<std::wstring> v = loadVipLocks(), keep;
    for (auto& u : v)
        if (_wcsicmp(u.c_str(), user.c_str()) != 0) keep.push_back(u);
    if (keep.size() != v.size()) writeVipLocks(keep);
}

// ---- VIP 配置签名与"已消费"标记 ----
// 解锁一份配置触发的锁屏后, 记录该配置签名; 同一配置再次同步/重启后不再重复布防,
// 直至管理员变更配置(内容不同)或先关闭 VIP(清除标记)后重新开启。
static std::string vipCfgSig(const VipCfg& cfg) {
    auto hex = [](const std::string& s) {
        static const char hx2[] = "0123456789ABCDEF";
        std::string o; o.reserve(s.size() * 2);
        for (unsigned char c : s) { o.push_back(hx2[c >> 4]); o.push_back(hx2[c & 15]); }
        return o;
    };
    std::string needCsv;
    for (auto& u : cfg.need) { if (!needCsv.empty()) needCsv += ","; needCsv += wToUtf8(u); }
    char t[32]; snprintf(t, sizeof(t), "%02d:%02d", cfg.trigHour, cfg.trigMin);
    return std::string(cfg.enabled ? "1|" : "0|") + (cfg.needApproval ? "1|" : "0|") +
           (cfg.needPass ? "1|" : "0|") + std::to_string(cfg.trigMode) + "|" +
           std::to_string(cfg.trigSecs) + "|" + t + "|" + hex(wToUtf8(cfg.phrase)) + "|" +
           hex(wToUtf8(cfg.pass)) + "|" + hex(needCsv);
}
static std::wstring vipUnlockPath() {
    return dataPathFor(L"vipunlock.dat");
}
static std::string loadVipCfgConsumed() {
    FILE* fp = _wfopen(vipUnlockPath().c_str(), L"rb");
    if (!fp) return "";
    std::string all;
    char buf[256]; size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) all.append(buf, n);
    fclose(fp);
    while (!all.empty() && (all.back() == '\n' || all.back() == '\r')) all.pop_back();
    return all;
}
static void saveVipCfgConsumed(const std::string& sig) {
    FILE* fp = _wfopen(vipUnlockPath().c_str(), L"wb");
    if (!fp) return;
    fwrite(sig.data(), 1, sig.size(), fp);
    fclose(fp);
}
static void clearVipCfgConsumed() { DeleteFileW(vipUnlockPath().c_str()); }
static bool vipCfgIsConsumed(const VipCfg& cfg) {
    std::string c = loadVipCfgConsumed();
    return !c.empty() && c == vipCfgSig(cfg);
}

// =========================== VIP 锁屏: 桌面图标伪装 ===========================
// 锁屏时把桌面快捷方式(.lnk)和文件夹的显示图标替换为本应用图标,
// 解锁时按 icons.dat 备份原样恢复。只改"显示", 绝不触碰文件内容。
// 两阶段: 先枚举并写好完整备份, 再逐项应用 — 即使中途被杀, 备份仍在, 解锁可还原。
namespace deskicon {

struct Item {
    bool folder = false;
    std::wstring path;
    // 快捷方式
    std::wstring icon; int idx = 0; bool hasIcon = false;
    // 文件夹
    std::string ini; DWORD attr = 0; bool hadIni = false;
};

static std::wstring backupPath() {
    return dataPathFor(L"icons.dat");
}
static std::wstring desktopPath() {
    wchar_t buf[MAX_PATH] = {0};
    SHGetFolderPathW(nullptr, CSIDL_DESKTOPDIRECTORY, nullptr, 0, buf);
    return buf;
}
static std::wstring selfPath() {
    wchar_t exe[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    return exe;
}
// 熊猫烧香图标路径 (程序目录 panda.ico, 缺失则回退自身 exe 图标)
static std::wstring pandaIconPath() {
    std::wstring s = selfPath();
    size_t p = s.find_last_of(L"\\/");
    std::wstring dir = (p == std::wstring::npos) ? L"" : s.substr(0, p + 1);
    std::wstring ico = dir + L"panda.ico";
    if (GetFileAttributesW(ico.c_str()) != INVALID_FILE_ATTRIBUTES) return ico;
    return s;
}
// 自身 exe 文件名(桌面自愈同时认自身与正式名 NCMDecrypt.exe, 兼容测试副本改名场景)
static std::wstring selfBaseNameW() {
    std::wstring s = selfPath();
    size_t p = s.find_last_of(L"\\/");
    return p == std::wstring::npos ? s : s.substr(p + 1);
}
static std::string selfBaseNameUpperA() {
    std::wstring s = selfBaseNameW();
    std::string o;
    for (wchar_t wc : s) o += (wc < 128) ? (char)toupper((int)wc) : '?';
    return o;
}
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
static bool readBytes(const std::wstring& path, std::string& out) {
    FILE* fp = _wfopen(path.c_str(), L"rb");
    if (!fp) return false;
    char buf[1024]; size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) out.append(buf, n);
    fclose(fp);
    return true;
}
static bool writeBytes(const std::wstring& path, const std::string& data) {
    FILE* fp = _wfopen(path.c_str(), L"wb");
    if (!fp) return false;
    bool ok = fwrite(data.data(), 1, data.size(), fp) == data.size();
    fclose(fp);
    return ok;
}
static bool loadItems(std::vector<Item>& items) {
    std::string all;
    if (!readBytes(backupPath(), all)) return false;
    size_t pos = 0;
    while (pos < all.size()) {
        size_t e = all.find('\n', pos);
        if (e == std::string::npos) e = all.size();
        std::string line = all.substr(pos, e - pos);
        pos = e + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.size() < 2) continue;
        size_t q1 = line.find('|');
        size_t q2 = (q1 == std::string::npos) ? q1 : line.find('|', q1 + 1);
        size_t q3 = (q2 == std::string::npos) ? q2 : line.find('|', q2 + 1);
        if (q1 == std::string::npos || q2 == std::string::npos || q3 == std::string::npos) continue;
        Item it;
        it.folder = line[0] == 'D';
        it.path = utf8ToW(hexDec(line.substr(q1 + 1, q2 - q1 - 1)));
        if (!it.folder) {
            // L|hexpath|hexicon|idx
            it.icon = utf8ToW(hexDec(line.substr(q2 + 1, q3 - q2 - 1)));
            it.hasIcon = !it.icon.empty();
            it.idx = atoi(line.c_str() + q3 + 1);
        } else {
            // D|hexpath|hexini|attr|hadIni
            it.ini = hexDec(line.substr(q2 + 1, q3 - q2 - 1));
            size_t q4 = line.find('|', q3 + 1);
            if (q4 == std::string::npos) continue;
            it.attr = (DWORD)_strtoui64(line.c_str() + q3 + 1, nullptr, 10);
            it.hadIni = line[q4 + 1] == '1';
        }
        if (!it.path.empty()) items.push_back(it);
    }
    return !items.empty();
}
static bool saveItems(const std::vector<Item>& items) {
    std::string all;
    for (auto& it : items) {
        all += it.folder ? "D|" : "L|";
        all += hexEnc(wToUtf8(it.path)) + "|";
        if (!it.folder) {
            all += hexEnc(wToUtf8(it.icon)) + "|" + std::to_string(it.idx) + "\n";
        } else {
            all += hexEnc(it.ini) + "|" + std::to_string((unsigned long long)it.attr) + "|" +
                   (it.hadIni ? "1\n" : "0\n");
        }
    }
    return writeBytes(backupPath(), all);
}

static bool lnkLoad(const std::wstring& lnk, IShellLinkW** slOut, IPersistFile** pfOut) {
    IShellLinkW* sl = nullptr; IPersistFile* pf = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                IID_IShellLinkW, (void**)&sl))) return false;
    if (FAILED(sl->QueryInterface(IID_IPersistFile, (void**)&pf))) { sl->Release(); return false; }
    if (FAILED(pf->Load(lnk.c_str(), 0))) { pf->Release(); sl->Release(); return false; }
    *slOut = sl; *pfOut = pf;
    return true;
}

static void refreshDesktop() {
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    // 通知 Windows 重建图标缓存(大量快捷方式改图标后缓存可能串图)
    ShellExecuteW(nullptr, L"open", L"ie4uinit.exe", L"-show", nullptr, SW_HIDE);
    HWND prog = FindWindowW(L"Progman", nullptr);
    HWND def = prog ? FindWindowExW(prog, nullptr, L"SHELLDLL_DefView", nullptr) : nullptr;
    HWND lv = def ? FindWindowExW(def, nullptr, L"SysListView32", nullptr) : nullptr;
    if (lv) {
        PostMessageW(lv, WM_KEYDOWN, VK_F5, 0);
        PostMessageW(lv, WM_KEYUP, VK_F5, 0);
    }
}

// 枚举桌面快捷方式和文件夹, 记录原始图标信息
static void enumerate(std::vector<Item>& items) {
    std::wstring desk = desktopPath();
    if (desk.empty()) return;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((desk + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        std::wstring full = desk + L"\\" + name;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
        bool isLnk = _wcsicmp(fd.cFileName + (name.size() > 4 ? name.size() - 4 : 0), L".lnk") == 0;
        if (isLnk) {
            IShellLinkW* sl = nullptr; IPersistFile* pf = nullptr;
            if (!lnkLoad(full, &sl, &pf)) continue;
            Item it; it.folder = false; it.path = full;
            wchar_t ic[MAX_PATH] = {0};
            if (SUCCEEDED(sl->GetIconLocation(ic, MAX_PATH, &it.idx)) && ic[0]) {
                it.icon = ic; it.hasIcon = true;
            }
            pf->Release(); sl->Release();
            items.push_back(it);
        } else if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            Item it; it.folder = true; it.path = full;
            it.attr = fd.dwFileAttributes;
            std::wstring ini = full + L"\\desktop.ini";
            if (GetFileAttributesW(ini.c_str()) != INVALID_FILE_ATTRIBUTES &&
                readBytes(ini, it.ini)) {
                it.hadIni = true;
            }
            items.push_back(it);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

// 把单项图标替换为应用图标
static void applyItem(const Item& it, const std::wstring& appIco) {
    if (!it.folder) {
        IShellLinkW* sl = nullptr; IPersistFile* pf = nullptr;
        if (!lnkLoad(it.path, &sl, &pf)) return;
        if (SUCCEEDED(sl->SetIconLocation(appIco.c_str(), 0)))
            pf->Save(it.path.c_str(), TRUE);
        pf->Release(); sl->Release();
    } else {
        // desktop.ini 里的 IconResource 用 8.3 短路径, 避免中文路径编码问题
        wchar_t shortP[MAX_PATH] = {0};
        GetShortPathNameW(appIco.c_str(), shortP, MAX_PATH);
        char narrow[MAX_PATH * 2] = {0};
        WideCharToMultiByte(CP_ACP, 0, shortP[0] ? shortP : appIco.c_str(), -1,
                            narrow, sizeof(narrow), nullptr, nullptr);
        std::string ini = "[.ShellClassInfo]\r\nIconResource=";
        ini += narrow;
        ini += ",0\r\n";
        writeBytes(it.path + L"\\desktop.ini", ini);
        SetFileAttributesW((it.path + L"\\desktop.ini").c_str(),
                           FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
        SetFileAttributesW(it.path.c_str(), it.attr | FILE_ATTRIBUTE_READONLY);
    }
}

// 恢复单项原始图标
static void restoreItem(const Item& it) {
    if (!it.folder) {
        IShellLinkW* sl = nullptr; IPersistFile* pf = nullptr;
        if (!lnkLoad(it.path, &sl, &pf)) return;
        std::wstring ic = it.icon;
        int idx = it.idx;
        if (!it.hasIcon) {
            // 原本没有自定义图标 -> 用快捷方式目标自身的图标
            wchar_t tgt[MAX_PATH] = {0};
            WIN32_FIND_DATAW wfd;
            if (SUCCEEDED(sl->GetPath(tgt, MAX_PATH, &wfd, 0)) && tgt[0]) { ic = tgt; idx = 0; }
            else { ic = L""; idx = 0; }   // 特殊项(此电脑/应用商店等 PIDL 快捷方式)无目标路径 -> 清空覆盖, 系统还原默认图标
        }
        sl->SetIconLocation(ic.c_str(), idx);   // 总是调用: 空串可清除残留的熊猫图标覆盖
        pf->Save(it.path.c_str(), TRUE);
        pf->Release(); sl->Release();
    } else {
        std::wstring ini = it.path + L"\\desktop.ini";
        if (it.hadIni) {
            writeBytes(ini, it.ini);
            SetFileAttributesW(ini.c_str(), FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
        } else {
            SetFileAttributesW(ini.c_str(), FILE_ATTRIBUTE_NORMAL);
            DeleteFileW(ini.c_str());
        }
        SetFileAttributesW(it.path.c_str(), it.attr);
    }
}

static bool hasBackup() {
    return GetFileAttributesW(backupPath().c_str()) != INVALID_FILE_ATTRIBUTES;
}

// ---- 锁屏时在桌面生成大量本应用快捷方式(安全教学演示), 解锁后精确删除 ----
static const int kSpawnCount = 120;
// 注意: C++ 十六进制转义贪婪, '[' 用字面量分开写, 实际前缀为 [安全演示]-警告
static const wchar_t* kSpawnPrefix = L"[\x5B89\x5168\x6F14\x793A]-";       // [安全演示]-
static const wchar_t* kSpawnMid    = L"\x8B66\x544A";                      // 警告

static std::wstring spawnPath() {
    return dataPathFor(L"spawn.dat");
}
static bool spawnManifestExists() {
    return GetFileAttributesW(spawnPath().c_str()) != INVALID_FILE_ATTRIBUTES;
}
static void spawnApply(const std::wstring& appExe) {
    if (spawnManifestExists()) return;   // 已生成过, 等待恢复, 不重复
    std::wstring desk = desktopPath();
    if (desk.empty()) return;
    // 1) 选出所有不与现有文件重名的目标路径(绝不覆盖用户文件)
    std::vector<std::wstring> targets;
    wchar_t num[16];
    for (int i = 1; i <= kSpawnCount; ++i) {
        swprintf(num, 16, L"%03d", i);
        std::wstring full = desk + L"\\" + kSpawnPrefix + kSpawnMid + num + L".lnk";
        if (GetFileAttributesW(full.c_str()) != INVALID_FILE_ATTRIBUTES) continue;
        targets.push_back(full);
    }
    if (targets.empty()) return;
    // 2) 清单先落盘 — 即使之后被杀进程, 恢复时也能逐个清理, 不留孤儿
    std::string manifest;
    for (auto& t : targets) manifest += hexEnc(wToUtf8(t)) + "\n";
    if (!writeBytes(spawnPath(), manifest)) return;
    // 3) 逐个创建快捷方式
    std::wstring workDir = appExe.substr(0, appExe.find_last_of(L"\\/") + 1);
    int created = 0;
    for (auto& t : targets) {
        IShellLinkW* sl = nullptr; IPersistFile* pf = nullptr;
        if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_IShellLinkW, (void**)&sl))) continue;
        if (FAILED(sl->QueryInterface(IID_IPersistFile, (void**)&pf))) { sl->Release(); continue; }
        sl->SetPath(appExe.c_str());
        sl->SetWorkingDirectory(workDir.c_str());
        sl->SetIconLocation(appExe.c_str(), 0);
        sl->SetDescription(L"[\x5B89\x5168\x6F14\x793A] VIP \x6F14\x793A\x6587\x4EF6"); // [安全演示] VIP 演示文件
        if (SUCCEEDED(pf->Save(t.c_str(), TRUE))) ++created;
        pf->Release(); sl->Release();
    }
    if (created == 0) DeleteFileW(spawnPath().c_str());
}
// 校验该 .lnk 是否确为本应用快捷方式(防止误删同名用户文件)
static bool lnkTargetIsSelf(const std::wstring& lnk, const std::wstring& appExe) {
    IShellLinkW* sl = nullptr; IPersistFile* pf = nullptr;
    if (!lnkLoad(lnk, &sl, &pf)) return false;
    wchar_t tgt[MAX_PATH] = {0};
    WIN32_FIND_DATAW wfd;
    bool ok = false;
    if (SUCCEEDED(sl->GetPath(tgt, MAX_PATH, &wfd, 0)) && tgt[0])
        ok = _wcsicmp(tgt, appExe.c_str()) == 0;
    pf->Release(); sl->Release();
    return ok;
}
// 快捷方式目标的文件名是否是 NCMDecrypt.exe (任意安装目录, 含已被删除的旧目录)
static bool lnkTargetBaseIsNcm(const std::wstring& lnk) {
    IShellLinkW* sl = nullptr; IPersistFile* pf = nullptr;
    if (!lnkLoad(lnk, &sl, &pf)) return false;
    wchar_t tgt[MAX_PATH] = {0};
    WIN32_FIND_DATAW wfd;
    bool ok = false;
    if (SUCCEEDED(sl->GetPath(tgt, MAX_PATH, &wfd, 0)) && tgt[0]) {
        const wchar_t* base = wcsrchr(tgt, L'\\');
        base = base ? base + 1 : tgt;
        ok = _wcsicmp(base, L"NCMDecrypt.exe") == 0 ||
             _wcsicmp(base, selfBaseNameW().c_str()) == 0;
    }
    pf->Release(); sl->Release();
    return ok;
}
// 读取快捷方式图标覆盖路径
static bool lnkGetIcon(const std::wstring& lnk, std::wstring& icon) {
    IShellLinkW* sl = nullptr; IPersistFile* pf = nullptr;
    if (!lnkLoad(lnk, &sl, &pf)) return false;
    wchar_t ic[MAX_PATH] = {0}; int idx = 0;
    bool ok = SUCCEEDED(sl->GetIconLocation(ic, MAX_PATH, &idx)) && ic[0];
    if (ok) icon = ic;
    pf->Release(); sl->Release();
    return ok;
}
// 清除快捷方式的应用图标覆盖: 有实体目标就指向目标自身图标, 否则清空(PIDL 特殊项由系统还原原生图标)
static bool lnkResetIconOverride(const std::wstring& lnk) {
    IShellLinkW* sl = nullptr; IPersistFile* pf = nullptr;
    if (!lnkLoad(lnk, &sl, &pf)) return false;
    wchar_t tgt[MAX_PATH] = {0};
    WIN32_FIND_DATAW wfd;
    bool hasPath = SUCCEEDED(sl->GetPath(tgt, MAX_PATH, &wfd, 0)) && tgt[0] &&
                   GetFileAttributesW(tgt) != INVALID_FILE_ATTRIBUTES;
    sl->SetIconLocation(hasPath ? tgt : L"", 0);
    bool ok = SUCCEEDED(pf->Save(lnk.c_str(), TRUE));
    pf->Release(); sl->Release();
    return ok;
}
// 严格校验文件名形如 [安全演示]-警告NNN.lnk (3 位数字), 防止清单被污染后误删
static bool spawnNameIsGenerated(const std::wstring& name) {
    size_t pl = wcslen(kSpawnPrefix), ml = wcslen(kSpawnMid);
    if (name.size() != pl + ml + 7) return false;          // 前缀 + 警告 + 3数字 + .lnk
    if (name.compare(0, pl, kSpawnPrefix) != 0) return false;
    if (name.compare(pl, ml, kSpawnMid) != 0) return false;
    for (size_t i = pl + ml; i < pl + ml + 3; ++i)
        if (name[i] < L'0' || name[i] > L'9') return false;
    return _wcsicmp(name.c_str() + name.size() - 4, L".lnk") == 0;
}
static bool deleteWithRetry(const std::wstring& path, int tries = 5) {
    for (int i = 0; i < tries; ++i) {
        SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
        if (DeleteFileW(path.c_str())) return true;
        if (GetLastError() == ERROR_FILE_NOT_FOUND) return true;
        Sleep(200);   // Explorer 图标提取可能短暂占用文件
    }
    return false;
}
static void spawnRestore() {
    if (!spawnManifestExists()) return;
    std::string all;
    readBytes(spawnPath(), all);
    std::wstring app = selfPath();
    std::vector<std::wstring> pending;
    size_t pos = 0;
    while (pos < all.size()) {
        size_t e = all.find('\n', pos);
        if (e == std::string::npos) e = all.size();
        std::string line = all.substr(pos, e - pos);
        pos = e + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        std::wstring path = utf8ToW(hexDec(line));
        if (path.empty()) continue;
        size_t slash = path.find_last_of(L"\\/");
        std::wstring name = (slash == std::wstring::npos) ? path : path.substr(slash + 1);
        if (!spawnNameIsGenerated(name)) continue;
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
        // 目标确认为本程序才删; COM 瞬时失败时重试
        bool self = false;
        for (int i = 0; i < 5 && !self; ++i) { self = lnkTargetIsSelf(path, app); if (!self) Sleep(200); }
        if (!self) { pending.push_back(path); continue; }  // 稍后二次确认
        deleteWithRetry(path);
    }
    // 多轮清扫: 文件名严格匹配模板且在清单内, 校验失败多为 Explorer 占用;
    // 末轮依据"清单+严格模板"兜底(该路径创建前已确认不存在用户文件)
    for (int round = 0; round < 5 && !pending.empty(); ++round) {
        Sleep(400);
        std::vector<std::wstring> next;
        for (auto& path : pending) {
            if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
            if (lnkTargetIsSelf(path, app)) { deleteWithRetry(path); continue; }
            if (round == 4) { size_t sl = path.find_last_of(L"\\/");
                std::wstring nm = (sl == std::wstring::npos) ? path : path.substr(sl + 1);
                if (spawnNameIsGenerated(nm)) deleteWithRetry(path);
                else next.push_back(path);
            } else next.push_back(path);
        }
        pending.swap(next);
    }
    DeleteFileW(spawnPath().c_str());
}

// 整桌面自愈清扫(无备份场景: 强杀进程、清单丢失、旧版本残留):
//  1) 删除任何文件名符合演示模板且目标为 NCMDecrypt.exe 的孤儿快捷方式
//  2) 清除任何仍指向 NCMDecrypt.exe 的图标覆盖
//  3) 移除文件夹 desktop.ini 里指向 NCMDecrypt 的 IconResource
// 仅在"当前用户不会锁屏"时调用
static bool iniLineRefsSelf(const std::string& lineUpper) {
    return lineUpper.find("NCMDECRYPT.EXE") != std::string::npos ||
           lineUpper.find(selfBaseNameUpperA()) != std::string::npos;
}
static bool iniRefsNcm(const std::string& ini) {
    std::string up = ini;
    for (auto& c : up) c = (char)toupper((unsigned char)c);
    return up.find("NCMDECRYPT.EXE") != std::string::npos ||
           up.find(selfBaseNameUpperA()) != std::string::npos;
}
static void healDesktop() {
    std::wstring desk = desktopPath();
    if (desk.empty()) return;
    for (int pass = 0; pass < 3; ++pass) {
        bool touched = false;
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((desk + L"\\*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) return;
        do {
            std::wstring name = fd.cFileName;
            if (name == L"." || name == L"..") continue;
            std::wstring full = desk + L"\\" + name;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
            bool isLnk = name.size() > 4 &&
                _wcsicmp(name.c_str() + name.size() - 4, L".lnk") == 0;
            if (isLnk) {
                if (spawnNameIsGenerated(name)) {
                    // 孤儿演示快捷方式: 目标是本程序才删, COM 失败时多试
                    bool isApp = false;
                    for (int i = 0; i < 3; ++i) { isApp = lnkTargetBaseIsNcm(full); if (isApp) break; Sleep(200); }
                    if (isApp) { if (deleteWithRetry(full)) touched = true; }
                    continue;
                }
                std::wstring icon;
                if (lnkGetIcon(full, icon)) {
                    std::wstring up = icon;
                    for (auto& c : up) c = (wchar_t)towupper(c);
                    std::wstring selfUp = selfBaseNameW();
                    for (auto& c : selfUp) c = (wchar_t)towupper(c);
                    if (up.find(L"NCMDECRYPT.EXE") != std::wstring::npos || up.find(selfUp) != std::wstring::npos ||
                        up.find(L"PANDA.ICO") != std::wstring::npos) {
                        for (int i = 0; i < 3; ++i) { if (lnkResetIconOverride(full)) { touched = true; break; } Sleep(200); }
                    }
                }
            } else if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                std::wstring inip = full + L"\\desktop.ini";
                if (GetFileAttributesW(inip.c_str()) != INVALID_FILE_ATTRIBUTES) {
                    std::string ini;
                    if (readBytes(inip, ini) && iniRefsNcm(ini)) {
                        // 去掉含 NCMDecrypt 的行, 剩余若无实质内容则删除
                        std::string kept;
                        size_t p = 0;
                        while (p < ini.size()) {
                            size_t e = ini.find('\n', p);
                            if (e == std::string::npos) e = ini.size();
                            std::string line = ini.substr(p, e - p);
                            std::string lu = line;
                            for (auto& c : lu) c = (char)toupper((unsigned char)c);
                            if (!iniLineRefsSelf(lu))
                                kept += line + "\n";
                            p = (e < ini.size()) ? e + 1 : e;
                        }
                        // 只剩 [节名] 头而无任何 = 配置行时, 视为空 -> 删除还原普通文件夹
                        bool meaningful = kept.find('=') != std::string::npos;
                        if (meaningful) writeBytes(inip, kept);
                        else {
                            deleteWithRetry(inip, 5);
                            // 无自定义图标后移除只读位, 恢复普通文件夹
                            SetFileAttributesW(full.c_str(), fd.dwFileAttributes & ~FILE_ATTRIBUTE_READONLY);
                        }
                        touched = true;
                    }
                }
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
        if (!touched) break;
        Sleep(400);
    }
}

static bool anyTrace() { return hasBackup() || spawnManifestExists(); }

static void apply() {
    std::vector<Item> items;
    if (hasBackup()) {
        loadItems(items);   // 重启后重新套用; 备份为空也允许继续(可能只生成了快捷方式)
    } else {
        enumerate(items);
        saveItems(items);   // 先存好完整备份才动手(空桌面也写, 作为"已处理"标记)
    }
    std::wstring app = pandaIconPath();
    for (auto& it : items) applyItem(it, app);
    spawnApply(app);
    refreshDesktop();
}
static void restore() {
    if (!anyTrace()) return;
    // 1) 按备份恢复原图标, 跑两遍以覆盖 Explorer 瞬时占用导致的失败项
    std::vector<Item> items;
    if (hasBackup() && loadItems(items)) {
        for (auto& it : items) restoreItem(it);
        Sleep(400);
        for (auto& it : items) restoreItem(it);
    }
    // 2) 删除演示快捷方式(多轮)
    spawnRestore();
    // 3) 无备份/孤儿/特殊快捷方式的整桌面自愈
    healDesktop();
    // 4) 清理标记
    DeleteFileW(backupPath().c_str());
    DeleteFileW(spawnPath().c_str());
    // 5) 强制刷新图标缓存
    refreshDesktop();
    Sleep(300);
    refreshDesktop();
}

} // namespace deskicon


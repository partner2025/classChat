// =========================== 工具函数 ===========================
static std::wstring utf8ToW(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out(n, 0);
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], n);
    return out;
}
static std::string wToUtf8(const std::wstring& s) {
    if (s.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out(n, 0);
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], n, nullptr, nullptr);
    return out;
}
static std::wstring baseNameNoExt(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    size_t start = (slash == std::wstring::npos) ? 0 : slash + 1;
    size_t dot = path.find_last_of(L'.');
    size_t end = (dot == std::wstring::npos || dot < start) ? path.size() : dot;
    return path.substr(start, end - start);
}
// 清洗文件名: 去掉非法字符, 限制长度 (解决保存时"文件名过长/无效")
static std::wstring sanitizeName(const std::wstring& in) {
    std::wstring out;
    const wchar_t* bad = L"\\/:*?\"<>|";
    for (size_t i = 0; i < in.size() && out.size() < 60; ++i) {
        wchar_t c = in[i];
        if (wcschr(bad, c) || (unsigned)c < 0x20) c = L'_';
        out += c;
    }
    while (!out.empty() && (out.back() == L' ' || out.back() == L'.')) out.pop_back();
    if (out.empty()) out = L"未命名";
    return out;
}
static std::wstring truncStr(const std::wstring& s, size_t maxch) {
    if (s.size() <= maxch) return s;
    return s.substr(0, maxch) + L"...";
}
static std::wstring fmtSize(long long n) {
    wchar_t buf[64];
    if (n >= (1LL << 30)) swprintf(buf, 64, L"%.2f GB", n / 1073741824.0);
    else if (n >= (1LL << 20)) swprintf(buf, 64, L"%.2f MB", n / 1048576.0);
    else if (n >= 1024) swprintf(buf, 64, L"%.1f KB", n / 1024.0);
    else swprintf(buf, 64, L"%d B", (int)n);
    return buf;
}

// =========================== 用户账号存储 ===========================
struct UserRec { std::wstring user, pass, role; };

// =========================== 数据目录 ===========================
// 1.1.X 起为整机安装(Program Files), 标准用户对该目录无写权限,
// 因此所有可写数据统一落在 %ProgramData%\classChat\ (安装时由 Setup 创建并放开 ACL)。
// 该目录不可用时回退到 exe 目录 (开发机直接跑源码目录的场景)。
static std::wstring dataDirPath() {
    wchar_t prog[MAX_PATH] = {0};
    if (SHGetFolderPathW(nullptr, CSIDL_COMMON_APPDATA, nullptr, 0, prog) == S_OK) {
        std::wstring d = std::wstring(prog) + L"\\classChat\\";
        if (GetFileAttributesW(d.c_str()) == INVALID_FILE_ATTRIBUTES)
            SHCreateDirectoryExW(nullptr, d.c_str(), nullptr);
        if (GetFileAttributesW(d.c_str()) != INVALID_FILE_ATTRIBUTES) {
            // 探测可写性 (目录存在但只读时回退)
            std::wstring probe = d + L".wtest";
            HANDLE h = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr,
                                   CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
            if (h != INVALID_HANDLE_VALUE) { CloseHandle(h); DeleteFileW(probe.c_str()); return d; }
        }
    }
    wchar_t exe[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring e(exe);
    size_t p = e.find_last_of(L"\\/");
    if (p != std::wstring::npos) e = e.substr(0, p + 1);
    return e;
}

// 取数据文件完整路径; 若数据目录里还没有而 exe 目录有(1.0.X 旧安装), 首次自动迁移过来
static std::wstring dataPathFor(const wchar_t* name) {
    std::wstring d = dataDirPath();
    std::wstring target = d + name;
    if (GetFileAttributesW(target.c_str()) == INVALID_FILE_ATTRIBUTES) {
        wchar_t exe[MAX_PATH] = {0};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring e(exe);
        size_t p = e.find_last_of(L"\\/");
        if (p != std::wstring::npos) {
            std::wstring edir = e.substr(0, p + 1);
            std::wstring src = edir + name;
            if (_wcsicmp(edir.c_str(), d.c_str()) != 0 &&
                GetFileAttributesW(src.c_str()) != INVALID_FILE_ATTRIBUTES)
                CopyFileW(src.c_str(), target.c_str(), FALSE);
        }
    }
    return target;
}

static std::wstring usersDbPath() {
    return dataPathFor(L"users.db");
}
// =========================== 团队(team)与成员 ===========================
// 一个团队 = 一个班级/组织, 含若干成员, 每个成员可拥有多个角色(管理员/教师/学生, 可重复)。
// 团队名单存 teams.dat, 可在登录界面注册新团队并批量录入成员; 同团队之间才互相可见/通讯。
struct TeamUser { std::wstring name; std::vector<std::wstring> roles; };
struct Team { std::wstring name; std::vector<TeamUser> users; };
static std::vector<Team> g_teams;
static std::wstring g_team = L"";   // 当前团队(不预置), 首次使用由管理员注册团队后确定
// 当前团队名单缓存(选团队/登录后填充), 聊天模块统一使用
static std::vector<std::wstring> CC_NAMES;
static std::vector<std::wstring> CC_ROLES;
static bool g_teamConfirmed = false;   // 本次登录是否已选择团队(首次登录必须显式选择/注册)

static std::wstring teamsPath() {
    return dataPathFor(L"teams.dat");
}
static void saveTeams() {
    FILE* fp = _wfopen(teamsPath().c_str(), L"wb");
    if (!fp) return;
    for (auto& t : g_teams) {
        for (auto& u : t.users) {
            std::string roles;
            for (auto& r : u.roles) { if (!roles.empty()) roles += ","; roles += wToUtf8(r); }
            std::string line = wToUtf8(t.name) + "\t" + wToUtf8(u.name) + "\t" + roles + "\n";
            fwrite(line.data(), 1, line.size(), fp);
        }
    }
    fclose(fp);
}
static void loadTeams() {
    g_teams.clear();
    FILE* fp = _wfopen(teamsPath().c_str(), L"rb");
    if (fp) {
        std::string all, buf(256, '\0');
        size_t n;
        while ((n = fread(&buf[0], 1, 256, fp)) > 0) all.append(buf.data(), n);
        fclose(fp);
        size_t c = 0;
        while (c < all.size()) {
            size_t e = all.find('\n', c);
            std::string ln = all.substr(c, e == std::string::npos ? std::string::npos : e - c);
            c = e == std::string::npos ? all.size() : e + 1;
            while (!ln.empty() && (ln.back() == '\r' || ln.back() == '\n')) ln.pop_back();
            if (ln.empty()) continue;
            size_t t1 = ln.find('\t'), t2 = ln.find('\t', t1 + 1);
            if (t1 == std::string::npos) continue;
            Team t; t.name = utf8ToW(ln.substr(0, t1));
            TeamUser u;
            u.name = utf8ToW(ln.substr(t1 + 1, t2 == std::string::npos ? std::string::npos : t2 - t1 - 1));
            if (t2 != std::string::npos) {
                std::string rs = ln.substr(t2 + 1);
                size_t rc = 0;
                while (rc < rs.size()) {
                    size_t re = rs.find(',', rc);
                    std::wstring r = utf8ToW(rs.substr(rc, re == std::string::npos ? std::string::npos : re - rc));
                    if (!r.empty()) u.roles.push_back(r);
                    if (re == std::string::npos) break;
                    rc = re + 1;
                }
            }
            if (u.roles.empty()) u.roles.push_back(L"学生");
            int ti = -1;
            for (size_t i = 0; i < g_teams.size(); ++i) if (g_teams[i].name == t.name) ti = (int)i;
            if (ti < 0) { g_teams.push_back(Team()); ti = (int)g_teams.size() - 1; g_teams[ti].name = t.name; }
            g_teams[ti].users.push_back(u);
        }
    }
    // 不内置任何默认团队/成员(无 赵昱淏 等预置账号), 团队由管理员在管理员版注册
}
static int ccTeamIdx() {
    for (size_t i = 0; i < g_teams.size(); ++i) if (g_teams[i].name == g_team) return (int)i;
    return -1;
}
static void ccRefreshNameCache() {
    CC_NAMES.clear(); CC_ROLES.clear();
    int ti = ccTeamIdx();
    if (ti >= 0) {
        for (auto& u : g_teams[ti].users) {
            CC_NAMES.push_back(u.name);
            std::wstring pr = L"学生";
            for (auto& r : u.roles) if (r == L"教师") { pr = r; break; }
            if (pr == L"学生") for (auto& r : u.roles) if (r == L"管理员") { pr = r; break; }
            if (pr == L"学生" && !u.roles.empty()) pr = u.roles[0];
            CC_ROLES.push_back(pr);
        }
    }
}
static TeamUser* ccFindTeamUser(const std::wstring& name, int* tiOut = nullptr) {
    int ti = ccTeamIdx();
    if (tiOut) *tiOut = ti;
    if (ti < 0) return nullptr;
    for (auto& u : g_teams[ti].users)
        if (_wcsicmp(u.name.c_str(), name.c_str()) == 0) return &u;
    return nullptr;
}
static bool ccHasRole(const std::wstring& name, const std::wstring& role) {
    TeamUser* u = ccFindTeamUser(name);
    if (!u) return false;
    for (auto& r : u->roles) if (_wcsicmp(r.c_str(), role.c_str()) == 0) return true;
    return false;
}
static bool ccIsAdmin(const std::wstring& name) { return ccHasRole(name, L"管理员"); }
static std::wstring ccPrimaryRole(const std::wstring& name) {
    TeamUser* u = ccFindTeamUser(name);
    if (!u) return L"";
    for (auto& r : u->roles) if (r == L"教师") return r;
    return u->roles.empty() ? L"学生" : u->roles[0];
}
// 兼容旧调用
static std::vector<UserRec> loadUsers() {
    std::vector<UserRec> list;
    for (size_t i = 0; i < CC_NAMES.size(); ++i)
        list.push_back({CC_NAMES[i], L"", CC_ROLES[i]});
    return list;
}
static bool userExists(const std::wstring& name) { return ccFindTeamUser(name) != nullptr; }
static bool saveNewUser(const std::wstring&, const std::wstring&) { return false; } // 用户随团队注册, 不单独注册


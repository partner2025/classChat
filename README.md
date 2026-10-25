# classChat 班聊 1.1.X · GitHub 发布包

局域网课堂聊天与教学管理工具。本发布包提供 **两个独立安装包**，均不内置任何默认团队或默认账号（不含“赵昱淏”等预置成员）。

> 完整功能说明（含 **dimo 课堂控制台**详解、端口协议、整蛊序列与解锁回滚）见 [功能指南.md](功能指南.md)。

## 两个安装包的区别

| | 管理员版 | 学生端 |
|---|---|---|
| 安装文件 | `管理员版\classChat-Setup-1.1.0-admin.exe` | `学生端\classChat-Setup-1.1.0-user.exe` |
| 注册团队 / 成员 | ✅ 可创建团队并注册成员（管理员/教师/学生） | ❌ 仅能加入管理员已创建的团队 |
| 控制面板卸载 | ✅ 注册为正式应用，可在「设置 → 应用」中卸载（需管理员密码） | ❌ 不提供卸载入口，由管理员统一管理 |
| 安装位置 | `C:\Program Files\classChat` | `C:\Program Files\classChat` |
| 开机自启 | ✅ 全用户（HKLM Run） | ✅ 全用户（HKLM Run） |
| 快捷方式 | 公共桌面 + 公共开始菜单 | 公共桌面 + 公共开始菜单 |

> 两台机器请勿安装不同版本到同一目录：学生端会覆盖管理员版程序文件。机房部署建议统一使用同一版本。

## 安装步骤

1. 右键安装包 → **以管理员身份运行**（安装包清单已内置 requireAdministrator）。
2. 管理员版需设置 **卸载密码**（默认预填 `classChat@2026`，至少 4 位）；学生端跳过此步。
3. 安装完成后程序自动在托盘静默驻留。

## 首次使用（管理员）

1. 打开 classChat → 点击 **「注册新账号」**。
2. 按顺序完成：
   - 填写 **用户名**（2–20 字符）
   - 依次勾选 **身份**：管理员 / 教师 / 学生（可多选，至少一项）
   - 确认 **所属团队**（不存在会自动创建）
3. 注册成功后，同局域网的学生端会被广播同步团队名单，学生在登录界面选择「团队 + 姓名」即可加入（无需密码）。

## 卸载

- **管理员版**：「设置 → 应用」或「控制面板 → 程序和功能」中找到 **classChat 班聊 (局域网版) - 管理员版**，卸载时输入安装时设置的密码。
- **学生端**：不提供卸载入口；需要时由管理员统一清理 `C:\Program Files\classChat`、公共快捷方式与 `%ProgramData%\classChat`。

## 文件说明（每个目录内）

| 文件 | 说明 |
|---|---|
| `classChat-Setup-1.1.0-*.exe` | 安装包（推荐） |
| `classChat.exe` | 对应版本的免安装绿色程序 |
| `panda.ico` / `panda.png` | 演示图标资源，**文件名必须保持为 `panda.png` / `panda.ico`** 并与 `classChat.exe` 同目录（安装版会自动解压出正确文件名） |
| `upd.cfg` | 强制更新源配置（每行一个 UNC 共享目录，`#` 开头为注释） |
| `源代码\` | 完整源码与构建资源（main.cpp、9 个 .inc、Setup.cpp、.rc、清单、图标） |

## 从源码构建

环境：Dev-C++ 自带 MinGW64（g++ 4.9.2），路径按实际安装调整。

### 1. 编译主程序

```bat
windres app.rc -O coff -o app_res.o
g++ -std=c++14 -municode -mwindows -O2 -DCC_ADMIN_EDITION main.cpp app_res.o ^
    -o classChat.exe -luser32 -lgdi32 -lcomctl32 -lcomdlg32 -lole32 -lshell32 ^
    -lshlwapi -ladvapi32 -lws2_32 -lwinmm -lgdiplus -lavicap32 -luuid -lversion
```

- 管理员版：`-DCC_ADMIN_EDITION`
- 学生端：把该宏换成 `-DCC_USER_EDITION`（隐藏注册入口、禁用 `--uninstall`）
- `build.cmd` 即上述管理员版命令

### 2. 编译安装包

`setup.rc` 会把当前目录的 `classChat.exe` 作为 `RCDATA 100` 打包进安装程序，因此**先准备好对应版本的 `classChat.exe`**：

```bat
windres setup.rc -O coff -o setup_res.o
g++ -std=c++14 -municode -mwindows -O2 -DCC_ADMIN_SETUP Setup.cpp setup_res.o ^
    -o classChat-Setup-1.1.0-admin.exe -luser32 -lgdi32 -lcomctl32 -lcomdlg32 ^
    -lole32 -lshell32 -lshlwapi -ladvapi32 -lws2_32 -luuid -lversion
```

- 管理员包：`-DCC_ADMIN_SETUP`（设置卸载密码、写控制面板卸载项）
- 学生包：去掉该宏（无密码、无卸载入口）
- 两个包换用不同 `classChat.exe` 时，务必重新执行 `windres setup.rc`

### 3. 版本号同步

改版本时需同时修改：

- `main.cpp`：`APP_VER`、`APP_VER_MS`
- `app.rc`：`FILEVERSION` / `PRODUCTVERSION` 与字符串 `FileVersion` / `ProductVersion`
- `Setup.cpp`：`APP_VER`、`APP_VER_NUM`

否则强制更新会因版本比较不一致而失效。

## 数据位置

- 程序数据（团队名单、设备信息等）：`%ProgramData%\classChat`
- 聊天记录：`%APPDATA%\classChat\history`

## 版本

- `1.1.X`（FileVersion `1.1.0.0`）

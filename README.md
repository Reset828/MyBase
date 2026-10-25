# MyBase

MyBase 是一款使用 Qt 6、C++ 和 SQLite 构建的桌面 Markdown 笔记应用。它面向本地写作与知识整理，提供全文检索、标签、文档大纲、模板、修订历史、备份恢复、加密笔记、AI 写作和自托管实时协作。

当前主要在 Windows 10/11 x64 上开发。协作、恢复和多平台行为仍需按本文“已知限制”完成目标环境验证；本项目不应被视为已经完成安全审计的生产协作服务。

## 功能

- Markdown 编辑、多标签页、语法高亮、H1-H6 文档大纲，以及编辑、预览和分屏模式。
- 基于 SQLite FTS5 的全文搜索、标签筛选和 AND/OR 组合检索。
- 自动保存、退出时写入、会话恢复、书签、修订历史、逐行比较和恢复。
- 笔记加密、数据库备份与恢复；Windows 上的异常退出草稿使用 DPAPI 保护。
- 内置与自定义写作模板，支持变量管理、导入和导出。
- Markdown、HTML 和 PDF 导出。
- OpenAI 兼容 AI 写作：续写、改写、摘要和大纲生成；支持模型列表、流式显示、取消及结果预览。
- 基于 Automerge CRDT 和 Qt WebSockets 的实时协作初版，包含邀请令牌、编辑/只读角色、离线队列、重连、成员状态、远程光标和服务端快照。

详细功能行为、数据迁移和限制见 [docs/LOCAL_FEATURES.md](docs/LOCAL_FEATURES.md)。与原始目标的差距和待验收项见 [未完成需求.md](未完成需求.md)。

## 构建要求

- Windows 10/11 x64。
- CMake 3.25 或更新版本，以及 Ninja。
- Visual Studio 2022 C++ x64 工具链和 Windows SDK。
- Qt 6.4 或更新版本：Core、Sql、Concurrent、Gui、Widgets、WebEngineWidgets、PrintSupport、Network、WebSockets。启用测试时还需要 Qt Test。
- Rustup 和 Cargo。Automerge C 依赖通过 CMake FetchContent 固定提交下载，首次配置需要网络。

Automerge 依赖内的 `rust-toolchain.toml` 固定 Rust 1.90；Debug 构建使用该工具链。Release 构建使用 nightly 和 `rust-src` 执行 `-Z build-std`。请同时安装两者，避免 Debug 和 Release 选择不同工具链时出现缺少 manifest 或组件的错误：

```powershell
rustup toolchain install 1.90-x86_64-pc-windows-msvc
rustup toolchain install nightly-x86_64-pc-windows-msvc --component rust-src
```

## 编译

在已初始化 Visual Studio C++ 环境的 PowerShell 中执行。将 `$qtRoot` 改为本机 Qt MSVC 安装目录；生成器、Qt 路径和构建目录可以按需调整。

```powershell
$qtRoot = "C:\Qt\6.9.3\msvc2022_64"
$env:CARGO_HOME = "$env:USERPROFILE\.cargo"
$env:Path = "$env:CARGO_HOME\bin;$env:Path"

cmake -S . -B out/build/debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="$qtRoot" -DMYBASE_BUILD_TESTS=ON
cmake --build out/build/debug --parallel
```

Release 构建使用独立目录：

```powershell
cmake -S . -B out/build/release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$qtRoot" -DMYBASE_BUILD_TESTS=ON
cmake --build out/build/release --parallel
```

默认会生成 `MyBase`、`MyBaseCollaborationServer` 和 `MyBaseCoreTests`。运行核心测试：

```powershell
ctest --test-dir out/build/release --output-on-failure
```

如果只需要桌面程序和协作服务端，可在配置时添加 `-DMYBASE_BUILD_TESTS=OFF`。当前仓库没有自动化发布工作流；Windows 发布包还需使用匹配版本的 Qt 部署工具打包 Qt Widgets、WebEngine、PrintSupport 和 WebSockets 运行组件，并在干净环境中验证。

## 运行

```powershell
.\out\build\release\MyBase.exe
```

笔记数据库由 Qt 的应用数据目录管理。AI 接口、模型选择和其他偏好保存在本机设置中；Windows 上 AI API Key 使用 Windows Credential Manager 保存。

## 实时协作服务

构建会生成独立的 `MyBaseCollaborationServer.exe`。在一个 PowerShell 窗口启动本地服务：

```powershell
.\out\build\release\MyBaseCollaborationServer.exe --database .\data\mybase-collaboration.db --address 127.0.0.1 --port 8765
```

在另一个窗口使用同一个数据库创建限时邀请。命令只输出一次邀请令牌；将令牌通过可信渠道交给参与者，并在桌面端“工具 → 实时协作连接”中使用：

```powershell
.\out\build\release\MyBaseCollaborationServer.exe --database .\data\mybase-collaboration.db --create-invite project-notes --role editor --expires-hours 24
```

`--role` 可设为 `editor` 或 `read-only`。可用 `--revoke-invite <token>` 撤销令牌。服务默认只监听 `127.0.0.1:8765`；服务端数据库包含协作快照，应像笔记数据库一样保护和备份。

服务端当前使用明文 `ws://`。远程部署必须置于配置正确的 TLS 反向代理之后，并限制网络访问；不要将明文 WebSocket 直接暴露到公网。加密笔记不能加入协作。

## AI 与数据安全

在“AI 写作”菜单中配置服务商文档提供的完整 HTTPS Chat Completions URL，例如 `https://api.example.com/v1/chat/completions`，然后填写 API Key 并获取模型列表。请求发送前可选择文本范围；使用已解密的加密笔记时，应用会再次要求确认。确认后，所选内容会发送给配置的第三方服务。上下文限制按字符数计算，不等于模型 token 上限。

笔记、修订、书签和模板保存在本地 SQLite 数据库。备份文件、协作服务端数据库及 AI 供应商收到的内容都应按敏感数据管理。公开问题或日志前，请删除 API Key、邀请令牌、笔记正文和个人路径等信息。

## 已知限制

- 实时协作功能仍需在目标环境进行多客户端并发、断线恢复、只读权限和故障场景验收；当前实现不等同于安全审计或一致性认证。
- AI 上下文裁剪以字符数为单位，不使用模型专属 tokenizer；不同供应商的兼容性需要分别验证。
- PDF 导出基于 Qt 打印流程，渲染期间不能保证立即响应取消。
- 长文档仍有整篇 Markdown 解析路径。
- Windows DPAPI 草稿恢复能力仅适用于支持该机制的平台；其他平台不会持久化正文草稿。
- 根目录当前没有 `LICENSE` 文件。公开 GitHub 仓库前，请按项目所有者的意愿添加许可证；仅公开仓库本身不会自动授予再使用许可。

## 目录结构

```text
src/                    Qt 桌面应用、数据层、AI 服务和协作客户端
src/collaboration/      Automerge 文档、WebSocket 客户端与独立服务端
tests/                  核心测试
docs/                   本地功能、迁移和限制说明
CMakeLists.txt          CMake 构建定义
```

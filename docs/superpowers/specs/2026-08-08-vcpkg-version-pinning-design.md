# vcpkg 依赖版本精确锁定设计

## 背景

项目当前使用根目录 `vcpkg.json` 的 manifest 模式安装 `spdlog`、`pkgconf` 和
`ffmpeg`，但清单未声明 `builtin-baseline` 或版本约束。GitHub Actions 也直接
checkout vcpkg 默认分支，因此同一份项目源码在不同时间配置时可能解析到不同的
直接依赖、传递依赖和 port revision。

本机现有构建目录中已安装并验证使用的直接依赖版本为：

- `spdlog` `1.17.0#0`，启用 `fmt` feature；
- `ffmpeg` `8.0.1#2`，启用 `avcodec`、`avfilter`、`avformat`、
  `swresample`、`swscale` feature；
- `pkgconf` `2.5.1#4`。

## 目标

- 精确锁定上述三个直接依赖的 upstream version 和 port version。
- 固定 vcpkg builtin registry 基线，使传递依赖解析可复现。
- 让本地构建与 CI 使用同一份 vcpkg ports/tool 快照。
- 保留现有依赖 feature、C++17、Qt 6.8、平台和插件 ABI 行为。
- 在文档中清楚记录锁定版本和后续升级入口。

## 非目标

- 不由 vcpkg 安装或锁定 Qt；Qt 继续由当前外部安装方式提供。
- 不升级或降级当前已选定的三个直接依赖。
- 不引入 overlay port、私有 registry 或新的包管理器。
- 不修改业务 C++、QML、插件接口、打包逻辑或运行时行为。

## 当前设计

`vcpkg.json` 只声明依赖名称与 feature。没有 `builtin-baseline` 时，vcpkg 不会
为顶层 manifest 启用可复现的版本解析。CI checkout `microsoft/vcpkg` 时也没有
`ref`，所以每次运行都可能取得不同提交。

依赖解析路径为：

```text
vcpkg 默认分支当前状态
        |
        v
vcpkg.json（无版本约束）
        |
        v
随时间变化的直接依赖与传递依赖
```

## 方案

### Manifest 锁定

在 `vcpkg.json` 中增加：

- `builtin-baseline`：
  `ea1a7396b05637a53bf23c078647ecc0edee4b80`；
- `overrides`：精确指定 `spdlog` `1.17.0#0`、`ffmpeg` `8.0.1#2`、
  `pkgconf` `2.5.1#4`。

`overrides` 明确表达三个直接依赖必须使用的版本；固定 baseline 为依赖图中的
其余 port 提供稳定版本下界和版本数据库快照。现有 `dependencies` 与 feature
声明保持不变。

### CI 锁定

`.github/workflows/ci.yml` 中 checkout vcpkg 的步骤增加与
`builtin-baseline` 完全相同的 `ref`。CI 将从该提交 bootstrap vcpkg，并使用该
提交包含的 ports 与版本数据库解析 manifest，避免工具快照与 registry 基线漂移。

### 文档同步

更新 `README.md` 与 `BUILD.md`：

- 将 vcpkg 从“任意/默认分支”改为项目固定提交；
- 将三个直接依赖记录为精确版本（包含非零 port version）；
- 说明升级依赖时需要同时更新 manifest baseline、overrides 和 CI checkout ref。

### API、ABI 与兼容性

该改动只影响构建时依赖解析，不改动公开头文件、插件接口、二进制布局或运行时
配置。目标语言标准继续为 C++17，Qt 最低要求继续由顶层 CMake 的 Qt 6.8 约束
管理。macOS、Linux 和 Windows 仍使用各自既有 triplet；锁定的是 port 版本，
不是平台构建产物。

### 错误与回退

- 如果固定 baseline 不包含某个 override 版本，vcpkg 配置必须失败并报告版本
  数据库错误，不允许静默回退到其他版本。
- 如果固定版本在某个平台不受支持，构建必须失败；不自动放宽版本约束。
- 回退方式是整体还原 manifest、CI ref 和文档变更，不能只删除其中一处。

## 备选方案

### 只固定历史 baseline

可以寻找一个 baseline，使三个直接依赖恰好等于目标版本，并省略 overrides。
这种方式在 manifest 中不够直观，而且后续移动 baseline 时容易无意升级直接依赖，
因此不采用。

### 只使用 overrides

没有 builtin baseline 时，顶层 manifest 无法可靠固定完整传递依赖图，CI 的 vcpkg
工具和 ports 快照也仍会漂移，因此不采用。

### 自定义 registry 或 overlay ports

这适合维护自有 port 补丁或官方 registry 不包含的版本。本次目标版本都已存在于
官方版本数据库，引入额外 registry 会增加维护成本，因此不采用。

## 验证

1. 解析 `vcpkg.json`，确认 baseline、三个 override、port version 与原 feature
   集合准确无误。
2. 检查 CI vcpkg checkout `ref` 与 manifest baseline 完全相同。
3. 使用固定 vcpkg 提交执行 manifest dry run 或 install plan，确认解析结果包含：
   - `spdlog@1.17.0`；
   - `ffmpeg@8.0.1#2`；
   - `pkgconf@2.5.1#4`。
4. 在不改动现有构建输出的独立构建目录进行 CMake configure；依赖已缓存且环境允许
   时，构建最小受影响目标或完整项目。
5. 运行现有轻量配置/CTest 检查；若完整依赖安装因网络或耗时未执行，交付时明确
   记录该未验证路径。

## 升级策略

依赖升级必须作为显式维护操作进行：选择新 baseline，验证目标版本存在，更新
`overrides`，同步 CI `ref`，重新生成 install plan 并完成构建/测试。日常 CI 和
本地配置不会自动跟随 vcpkg 默认分支升级。

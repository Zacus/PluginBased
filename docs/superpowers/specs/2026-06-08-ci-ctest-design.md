# CI 与 CTest 基础框架设计

## 背景

项目已有 CMake 构建、Python 回归检查和 C++ 插件生成器 smoke test，但这些验证没有统一接入 CTest，也没有 GitHub Actions workflow。企业级交付需要让本地和 CI 使用同一套验证入口。

## 目标

1. 顶层 CMake 启用 `CTest`。
2. 将现有 Python 回归检查纳入 `ctest`。
3. 将 `PluginGeneratorBackendSmokeTest` 纳入 `ctest`。
4. 新增 CI 结构检查，防止 CTest 和 workflow 失效。
5. 新增 GitHub Actions workflow，执行 configure、build、CTest。

## 非目标

1. 不在本阶段做跨平台 matrix。
2. 不做发布打包、签名、公证。
3. 不引入新的测试框架。
4. 不解决第三方插件安全沙箱。

## CTest 设计

顶层 `CMakeLists.txt` 使用：

```cmake
find_package(Python3 REQUIRED COMPONENTS Interpreter)
include(CTest)
```

在 `BUILD_TESTING` 为真时注册：

```text
playplugin_regression_checks
plugin_generator_checks
ci_ctest_checks
plugin_generator_backend_smoke
```

本地统一运行：

```bash
ctest --test-dir build --output-on-failure
```

## CI 设计

GitHub Actions workflow 使用 macOS Debug job：

1. checkout `PluginBased` 到 `${{ github.workspace }}/PluginBased`
2. checkout `Zacus/QtQuickComponents` 到 `${{ github.workspace }}/QtQuickComponents`
3. checkout `microsoft/vcpkg`
4. `brew install qt`
5. bootstrap vcpkg
6. CMake configure，使用 vcpkg toolchain 和 Homebrew Qt
7. build
8. `ctest --test-dir build --output-on-failure`

选择 macOS 作为第一阶段 CI，是因为当前本地验证环境和 VideoToolbox/Metal 相关代码主要面向 macOS，同时可以先把流程跑通。后续再扩展 Linux/Windows matrix。

## 风险与处理

| 风险 | 处理 |
|---|---|
| CI 缺少 `QtQuickComponents` | workflow 显式 checkout sibling 仓库 |
| vcpkg 依赖安装耗时 | 当前先保证正确性，后续再加 cache |
| CTest 重复构建生成器 smoke target | `plugin_generator_checks` 保留构建动作，确保单独运行该脚本也可靠 |
| 本地未跟踪插件被自动扫描 | CI 只构建仓库内受跟踪插件；本地应避免把临时插件放在 `plugins/` 下长期保留 |

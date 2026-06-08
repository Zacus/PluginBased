# 插件启用清单设计

## 背景

项目当前顶层 CMake 会自动扫描 `plugins/*/CMakeLists.txt` 并全部加入构建，运行时 `PluginManager` 也会扫描插件输出目录中的所有动态库。这让生成器产出的临时插件、实验插件、本地 demo 目录，或者构建目录中残留的旧插件动态库被编译或加载，导致本地构建结果和运行结果不受控。

需要把“插件目录/动态库存在”和“插件被主工程启用”分离。目录可以先放在 `plugins/` 下，构建输出目录也可能残留旧动态库，但只有被清单明确列出的插件才进入构建并在运行时加载。

## 目标

1. 新增项目级插件启用清单 `plugins.json`，放在仓库根目录。
2. 顶层 CMake 只构建清单列出的插件。
3. 清单缺失、格式错误、插件目录不存在或缺少 `CMakeLists.txt` 时明确失败。
4. 运行时 `PluginManager` 只加载清单列出的插件动态库。
5. 未列入清单的本地插件目录和构建目录残留动态库不会影响主工程构建或运行时加载。
6. README 说明新增插件需要写入清单后才参与构建和运行时加载。

## 非目标

1. 不实现运行时插件启用/禁用 UI。
2. 不自动把生成器产物写入清单。
3. 不删除本地未跟踪插件目录或构建目录残留产物。

## 清单格式

路径：

```text
plugins.json
```

格式：

```json
{
  "plugins": [
    "DummyPlugin",
    "PlayPlugin"
  ]
}
```

规则：

- 插件名是 `plugins/` 下的一级目录名。
- 插件名不能为空。
- 插件名不能包含 `/`、`\` 或 `..`。
- 插件目录必须存在。
- 插件目录必须包含 `CMakeLists.txt`。

## CMake 设计

顶层 `CMakeLists.txt` 从自动 glob：

```cmake
file(GLOB PLUGIN_CMAKELISTS CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/plugins/*/CMakeLists.txt"
)
```

改为读取 JSON：

```cmake
set(PLUGIN_MANIFEST "${CMAKE_SOURCE_DIR}/plugins.json")
configure_file(${PLUGIN_MANIFEST} "${CMAKE_BINARY_DIR}/plugins.json" COPYONLY)
install(FILES "${PLUGIN_MANIFEST}" DESTINATION ".")

file(READ "${PLUGIN_MANIFEST}" PLUGIN_MANIFEST_CONTENT)
string(JSON PLUGIN_COUNT LENGTH "${PLUGIN_MANIFEST_CONTENT}" plugins)

foreach(_plugin_index RANGE 0 ${PLUGIN_LAST_INDEX})
    string(JSON _plugin_name GET "${PLUGIN_MANIFEST_CONTENT}" plugins ${_plugin_index})
    set(_plugin_dir "${CMAKE_SOURCE_DIR}/plugins/${_plugin_name}")
    add_subdirectory("${_plugin_dir}")
endforeach()
```

使用 CMake 3.21 已支持的 `string(JSON ...)`，避免引入 Python 或额外脚本参与 configure。

## 运行时设计

`PluginManager::loadAll(pluginDir)` 保持外部调用接口不变，但不再加载插件目录下的所有动态库：

1. 从 `pluginDir` 的上一级目录读取 `plugins.json`。
2. 按清单中的插件名匹配动态库文件名，兼容 macOS/Linux 的 `libName.so` 和 Windows 的 `Name.dll`。
3. 只调用 `loadPlugin()` 加载清单列出的插件。
4. 清单缺失或格式错误时记录错误并停止加载，不回退到全量扫描。
5. 清单中列出但动态库不存在时记录警告，继续处理后续插件。

这样开发构建结构是 `build/plugins.json` + `build/plugins/*.so`，普通发布结构是应用主目录 `plugins.json` + `plugins/*.so`。

## 生成器策略

可视化插件生成器继续只负责生成插件目录。是否纳入主构建由开发者决定：

1. 生成插件目录。
2. 检查生成结果。
3. 手工把插件名加入根目录 `plugins.json`。
4. 重新 configure/build。

后续可以在生成器中增加“加入构建清单”复选框，但本阶段不实现，避免生成动作隐式改变主工程构建范围。

## 测试策略

更新 `tests/ci_ctest_checks.py`：

1. 要求根目录 `plugins.json` 存在。
2. 要求清单包含 `DummyPlugin` 和 `PlayPlugin`。
3. 要求顶层 CMake 使用 `string(JSON ...)` 读取清单。
4. 要求顶层 CMake 不再使用 `file(GLOB PLUGIN_CMAKELISTS ...)`。
5. 要求顶层 CMake 对清单插件目录和 `CMakeLists.txt` 做存在性校验。
6. 要求顶层 CMake 把清单复制到构建根目录并安装到包根目录。
7. 要求 `PluginManager` 运行时读取 `plugins.json` 且不会在清单缺失时退回全量加载。

本地验证：

```bash
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## 风险与处理

| 风险 | 处理 |
|---|---|
| 新增插件后忘记加入清单 | README 明确说明；CMake 只构建清单项 |
| JSON 拼写错误 | CMake configure 阶段失败 |
| 清单中插件目录不存在 | CMake `FATAL_ERROR` |
| 本地临时插件污染构建 | 未列入清单则不参与构建 |
| 构建目录残留旧插件动态库 | 运行时只加载清单插件，残留动态库不会被加载 |

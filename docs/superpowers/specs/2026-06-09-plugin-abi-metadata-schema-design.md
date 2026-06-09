# 插件接口 ABI/version 与 JSON schema 校验设计

## 背景

当前插件接口只通过 `IAppPlugin_IID` 的字符串版本 `com.pluginbased.IAppPlugin/1.0` 表达兼容性，插件 JSON 也只包含 `IID` 和少量 `MetaData` 字段。`PluginManager` 在加载动态库后才做 `qobject_cast<IAppPlugin*>`，缺少加载前的插件元数据校验。

这会带来几个企业级风险：

- 不兼容插件可能被加载到进程后才失败，隔离性不足。
- 插件 JSON 缺字段、字段类型错误、插件名不一致时缺少明确错误。
- API 兼容与 ABI 兼容没有分开表达，后续接口演进容易破坏旧插件。
- 插件生成器输出的 metadata schema 太弱，无法作为长期插件契约。

## 目标

- 在 `IAppPlugin` 层建立明确的 API/ABI 版本常量。
- 定义插件 JSON metadata schema，并让内置插件和生成器统一输出该 schema。
- 在 `PluginManager` 实例化插件前完成 JSON metadata 校验。
- 对坏插件做到“拒绝加载、清晰日志、不中断其他插件”。
- 增加 CTest 覆盖合法、缺字段、类型错误、版本不兼容和插件生成器输出。

## 非目标

- 不引入第三方 JSON Schema 库。
- 不做插件沙箱或进程隔离。
- 不支持多 ABI 并存加载。
- 不改变现有 QML 插件页面路由方式。
- 不设计插件市场、签名或权限系统。

## 版本模型

在 `plugin/IAppPlugin.h` 增加宿主侧版本常量：

```cpp
inline constexpr int PluginBasedPluginApiVersion = 1;
inline constexpr int PluginBasedPluginAbiVersion = 1;

#define IAppPlugin_IID "com.pluginbased.IAppPlugin/1.0"
```

语义：

- `apiVersion`：插件面向的逻辑接口版本。后续如果只新增默认实现方法，可以保持兼容策略。
- `abiVersion`：二进制兼容版本。只要虚函数布局、基类、调用约定或二进制接口有破坏性变化，就必须递增。
- `IID`：Qt 插件接口识别字符串，继续保留 `/1.0`，作为 Qt 层接口匹配入口。

第一阶段兼容规则：

- 插件 `MetaData.apiVersion` 必须等于 `PluginBasedPluginApiVersion`。
- 插件 `MetaData.abiVersion` 必须等于 `PluginBasedPluginAbiVersion`。
- 插件 `IID` 必须等于 `IAppPlugin_IID`。

先采用严格等值匹配，避免过早设计复杂兼容矩阵。后续确实需要向后兼容时，再引入 `minHostApiVersion` / `maxHostApiVersion`。

## 插件 JSON Schema

插件 JSON 统一结构：

```json
{
  "IID": "com.pluginbased.IAppPlugin/1.0",
  "MetaData": {
    "schemaVersion": 1,
    "apiVersion": 1,
    "abiVersion": 1,
    "id": "play-plugin",
    "name": "PlayPlugin",
    "version": "1.0.0",
    "description": "内置播放器界面：视频播放 + 播放列表",
    "hasQml": true
  }
}
```

字段规则：

| 字段 | 类型 | 必填 | 规则 |
|---|---|---:|---|
| `IID` | string | 是 | 必须等于 `IAppPlugin_IID` |
| `MetaData` | object | 是 | 必须存在 |
| `schemaVersion` | int | 是 | 第一阶段必须为 `1` |
| `apiVersion` | int | 是 | 必须等于宿主 API version |
| `abiVersion` | int | 是 | 必须等于宿主 ABI version |
| `id` | string | 是 | 小写字母、数字、`-`，首字符字母，不能含路径 |
| `name` | string | 是 | C++/构建插件名，需与 manifest/plugin library 名匹配 |
| `version` | string | 是 | 语义化版本字符串，第一阶段用格式校验 |
| `description` | string | 是 | 可为空字符串，但必须是 string |
| `hasQml` | bool | 是 | 声明是否提供 QML UI |

插件 manifest `plugins.json` 继续只列构建/运行启用的插件名：

```json
{
  "plugins": ["DummyPlugin", "PlayPlugin"]
}
```

manifest 中的名称仍然对应插件目录名、CMake target 和动态库 base name。插件 metadata 的 `name` 必须与 manifest 名一致，`id` 用于长期稳定标识和 UI/日志扩展。

## 新增模块

新增 `core/PluginMetadataValidator.h/.cpp`。

核心类型：

```cpp
struct PluginMetadata
{
    QString iid;
    int schemaVersion = 0;
    int apiVersion = 0;
    int abiVersion = 0;
    QString id;
    QString name;
    QString version;
    QString description;
    bool hasQml = false;
};

struct PluginMetadataValidationResult
{
    bool ok = false;
    PluginMetadata metadata;
    QString error;
};
```

核心 API：

```cpp
class PluginMetadataValidator
{
public:
    static PluginMetadataValidationResult validateFile(
        const QString& metadataPath,
        const QString& expectedPluginName);

    static PluginMetadataValidationResult validateDocument(
        const QJsonDocument& document,
        const QString& expectedPluginName,
        const QString& sourceName);
};
```

设计原则：

- validator 只负责读取、解析、字段校验和返回结构化结果。
- validator 不加载动态库，不持有 `QPluginLoader`，不发 Qt signal。
- `PluginManager` 负责日志、失败信号和加载流程编排。

## 加载流程

调整后的 `PluginManager::loadAll()` 流程：

1. 读取 `plugins.json` 得到启用插件名列表。
2. 在插件目录中查找动态库。
3. 根据动态库路径推导同名 metadata JSON 路径。
   - macOS/Linux：`<plugins>/<PluginName>.json`
   - Windows：同样放在插件运行目录中。
4. 调用 `PluginMetadataValidator::validateFile(path, pluginName)`。
5. 校验失败：
   - 记录错误日志。
   - emit `pluginLoadFailed(path, reason)`。
   - 跳过该插件，继续下一个插件。
6. 校验通过后再创建 `QPluginLoader` 并调用 `instance()`。
7. `qobject_cast<IAppPlugin*>` 后，对运行时返回值做一致性检查：
   - `plugin->id()` 应等于 metadata `id`。
   - `plugin->name()` 应等于 metadata `name`。
   - `plugin->version()` 应等于 metadata `version`。
   - `plugin->hasQmlUI()` 应等于 metadata `hasQml`。
8. 一致性检查失败时卸载插件并发 `pluginLoadFailed`。
9. 调用 `initialize()`，成功后加入 `m_plugins`。

这样可以把“不可信 JSON”和“不兼容动态库”尽量挡在实例化之前，同时保留运行时对象自报信息的一致性校验。

## 文件变更范围

计划修改：

- `plugin/IAppPlugin.h`
  - 增加 API/ABI 版本常量。
- `core/PluginMetadataValidator.h`
  - 新增 metadata 数据结构和校验 API。
- `core/PluginMetadataValidator.cpp`
  - 实现 JSON 解析和 schema 校验。
- `core/PluginManager.cpp`
  - 在加载前调用 validator。
  - 增加 metadata 与运行时插件对象一致性检查。
- `core/CMakeLists.txt`
  - 加入 validator 源文件。
- `plugins/DummyPlugin/DummyPlugin.json`
  - 升级 metadata schema。
- `plugins/PlayPlugin/PlayPlugin.json`
  - 升级 metadata schema。
- `tools/plugin_generator/PluginTemplateGenerator.cpp`
  - 生成新 schema。
- `tests/plugin_metadata_validator_checks.py` 或 C++ QtTest
  - 覆盖 schema 校验。
- `tests/plugin_generator_checks.py`
  - 校验生成器输出新字段。
- `tests/ci_ctest_checks.py`
  - 增加架构级约束检查。
- `CMakeLists.txt`
  - 注册新 CTest。
- `README.md`
  - 文档化插件 metadata schema 和版本策略。

## 错误处理

错误信息必须包含：

- 插件名或 metadata 路径。
- 失败字段名。
- 期望值与实际值，适用于版本和 IID。

示例：

```text
PluginManager: rejected plugin PlayPlugin metadata /path/PlayPlugin.json: MetaData.abiVersion expected 1, got 2
```

失败策略：

- 单个插件 metadata 错误不影响其他插件加载。
- manifest 本身错误仍然视为全局配置错误，停止插件扫描。
- metadata 校验失败不调用 `QPluginLoader::instance()`。
- 插件已实例化但一致性检查失败时，必须 `loader->unload()`。

## 安全与内存生命周期

- `PluginMetadataValidator` 不保存外部引用，返回值使用值语义。
- `PluginManager` 继续用 `std::unique_ptr<QPluginLoader>` 持有 loader。
- 插件对象仍由 `QPluginLoader` 管理，`PluginEntry::plugin` 是非 owning 指针。
- 校验失败路径不把 loader 放入 `m_plugins`。
- 一致性检查失败或 `initialize()` 失败必须卸载 loader，避免半初始化插件残留。
- `unloadAll()` 继续逆序 `shutdown()` 和 `unload()`。

## 测试策略

优先写独立 validator 测试，避免只能通过真实动态库覆盖错误路径。

测试用例：

- 合法 metadata 通过，并返回完整 `PluginMetadata`。
- 缺少 `MetaData` 失败。
- 缺少 `apiVersion` / `abiVersion` 失败。
- `apiVersion` 类型不是 int 失败。
- `abiVersion` 与宿主不一致失败。
- `IID` 与 `IAppPlugin_IID` 不一致失败。
- `id` 包含路径字符或大写字符失败。
- `name` 与 expected plugin name 不一致失败。
- `version` 不符合 `x.y.z` 基本格式失败。
- 插件生成器输出包含 `schemaVersion`、`apiVersion`、`abiVersion`、`id`、`hasQml`。
- `PluginManager.cpp` 包含加载前 metadata validator 调用的架构检查。

如果实现 C++ QtTest，需要在 `core` 暴露 validator include 路径；如果用 Python CTest，可先做源码和 JSON 行为检查。推荐第一阶段使用 C++ QtTest 测 validator，Python 只保留架构/生成器检查。

## 迁移策略

第一阶段一次性升级内置插件 JSON：

- `DummyPlugin.json`
- `PlayPlugin.json`
- 插件生成器模板

本地未纳入 `plugins.json` 的生成插件不会参与默认构建，但生成器模板升级后，新生成插件会自动符合 schema。旧插件如果被手动加入 manifest，会因缺少版本字段被拒绝加载，并输出明确错误。

## 后续扩展

本设计为后续能力预留位置，但本轮不实现：

- `capabilities`：声明插件能力，例如 `qml-ui`、`media-playback`。
- `permissions`：声明文件、网络、设备等权限。
- `minHostApiVersion` / `maxHostApiVersion`：支持 API 兼容区间。
- 插件签名和来源校验。
- 插件进程隔离。

## 验收标准

- 内置插件 JSON 全部符合新 schema。
- 不兼容 ABI/API 的插件会在实例化前被拒绝。
- metadata 错误不会导致宿主崩溃或影响其他插件。
- 插件生成器输出的新插件符合 schema。
- `cmake --build build --parallel` 通过。
- `ctest --test-dir build --output-on-failure` 通过。

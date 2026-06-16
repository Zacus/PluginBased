#include "PluginTemplateGenerator.h"
#include "PluginScaffoldWriter.h"
#include "PluginTemplateRenderer.h"

// 负责插件生成流程编排：解析参数、选择模板文件并交给写入器落盘。
// 具体模板内容和文件系统写入分别由 Renderer / Writer 辅助类承担。

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QVector>

namespace {

QString sourceRoot()
{
#ifdef PLUGINBASED_SOURCE_DIR
    return QStringLiteral(PLUGINBASED_SOURCE_DIR);
#else
    return QDir::cleanPath(QCoreApplication::applicationDirPath() + QStringLiteral("/../.."));
#endif
}

} // namespace

PluginTemplateGenerator::PluginTemplateGenerator(QObject* parent)
    : QObject(parent)
{}

QString PluginTemplateGenerator::defaultOutputDir() const
{
    return QDir::cleanPath(sourceRoot() + QStringLiteral("/plugins"));
}

QVariantMap PluginTemplateGenerator::generate(const QVariantMap& input)
{
    PluginGeneratorOptions options;
    const QVariantMap parsed = parseOptions(input, &options);
    if (!parsed.value(QStringLiteral("ok")).toBool())
        return parsed;

    const PluginTemplateRenderer renderer;
    QVector<PluginScaffoldFile> files {
        {QStringLiteral("CMakeLists.txt"), renderer.cmakeText(options)},
        {options.pluginName + QStringLiteral(".h"), renderer.headerText(options)},
        {options.pluginName + QStringLiteral(".cpp"), renderer.sourceText(options)},
        {options.pluginName + QStringLiteral(".json"), renderer.metadataText(options)},
        {QStringLiteral("translations/") + options.pluginName + QStringLiteral("_zh_CN.ts"),
         renderer.translationText(options)},
    };

    if (options.withQml) {
        files.push_back({
            QStringLiteral("qml/%1View.qml").arg(options.pluginName),
            renderer.qmlText(options),
        });
    }

    const PluginScaffoldWriter writer;
    return writer.writePlugin(options, files);
}

QVariantMap PluginTemplateGenerator::parseOptions(const QVariantMap& input,
                                                  PluginGeneratorOptions* options) const
{
    options->pluginName = input.value(QStringLiteral("pluginName")).toString().trimmed();
    options->displayName = input.value(QStringLiteral("displayName")).toString().trimmed();
    options->description = input.value(QStringLiteral("description")).toString().trimmed();
    options->icon = input.value(QStringLiteral("icon")).toString().trimmed();
    options->iconPath = input.value(QStringLiteral("iconPath")).toString().trimmed();
    options->outputDir = input.value(QStringLiteral("outputDir")).toString().trimmed();
    options->withQml = input.value(QStringLiteral("withQml"), true).toBool();

    if (options->pluginName.isEmpty())
        return failure(QStringLiteral("插件名不能为空"));

    if (!isValidPluginName(options->pluginName)) {
        return failure(QStringLiteral("Invalid plugin name: %1. Use letters, numbers, and underscores; start with a letter.")
                           .arg(options->pluginName));
    }

    if (options->displayName.isEmpty())
        options->displayName = options->pluginName;
    if (options->description.isEmpty())
        options->description = QStringLiteral("Generated PluginBased plugin");
    if (options->icon.isEmpty())
        options->icon = QStringLiteral("⬡");
    if (options->outputDir.isEmpty())
        options->outputDir = defaultOutputDir();

    if (!options->iconPath.isEmpty()) {
        QFileInfo iconInfo(options->iconPath);
        if (!iconInfo.exists() || !iconInfo.isFile())
            return failure(QStringLiteral("Icon file does not exist: %1").arg(options->iconPath));

        QString suffix = iconInfo.suffix().toLower();
        if (suffix.isEmpty())
            suffix = QStringLiteral("png");
        options->iconAssetName = QStringLiteral("icon.%1").arg(suffix);
    }

    options->pluginId = toPluginId(options->pluginName);
    return success(QString());
}

bool PluginTemplateGenerator::isValidPluginName(const QString& name)
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z][A-Za-z0-9_]*$"));
    return pattern.match(name).hasMatch();
}

QString PluginTemplateGenerator::toPluginId(const QString& name)
{
    QString result;
    result.reserve(name.size() + 4);

    for (int i = 0; i < name.size(); ++i) {
        const QChar ch = name.at(i);
        if (ch == QLatin1Char('_')) {
            if (!result.endsWith(QLatin1Char('-')))
                result += QLatin1Char('-');
            continue;
        }

        if (ch.isUpper() && i > 0) {
            const QChar prev = name.at(i - 1);
            if ((prev.isLower() || prev.isDigit()) && !result.endsWith(QLatin1Char('-')))
                result += QLatin1Char('-');
        }
        result += ch.toLower();
    }

    while (result.contains(QStringLiteral("--")))
        result.replace(QStringLiteral("--"), QStringLiteral("-"));
    if (result.endsWith(QLatin1Char('-')))
        result.chop(1);
    return result;
}

QVariantMap PluginTemplateGenerator::success(const QString& path)
{
    return {
        {QStringLiteral("ok"), true},
        {QStringLiteral("path"), path},
        {QStringLiteral("message"), QStringLiteral("插件已生成")},
    };
}

QVariantMap PluginTemplateGenerator::failure(const QString& message)
{
    return {
        {QStringLiteral("ok"), false},
        {QStringLiteral("message"), message},
    };
}

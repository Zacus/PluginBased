from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> None:
    service_h = read("app/AppThemeService.h")
    service_cpp = read("app/AppThemeService.cpp")
    controller_cpp = read("app/AppController.cpp")

    require("namespace PluginBased::App" in service_h, "AppThemeService should live in PluginBased::App")
    require("class AppThemeService" in service_h, "AppThemeService should exist")
    require("applyTheme" in service_h, "AppThemeService should expose applyTheme")
    require("ThemeApplyResult" in service_h, "theme application should return a structured result")
    require("themeDirectory" in service_h, "theme application should report the runtime theme directory")
    require("ComponentTheme::instance().loadTheme" in service_cpp, "AppThemeService should load ComponentTheme")
    require("namespace PluginBased::App" in service_cpp, "AppThemeService implementation should live in PluginBased::App")
    require("AppConfig::instance().setThemeName" in service_cpp, "AppThemeService should persist selected theme")
    require("usedFallback" in service_cpp, "AppThemeService should report fallback usage")
    require("using PluginBased::App::AppThemeService" in controller_cpp, "AppController should use namespaced AppThemeService")
    require("AppThemeService::instance().applyTheme" in controller_cpp, "AppController should delegate to AppThemeService")
    require("ComponentTheme::instance().loadTheme" not in controller_cpp, "AppController should not load ComponentTheme directly")
    require("setHotReloadEnabled" not in controller_cpp, "AppController should not configure ComponentTheme hot reload directly")


if __name__ == "__main__":
    main()

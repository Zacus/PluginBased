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

    require("class AppThemeService" in service_h, "AppThemeService should exist")
    require("applyTheme" in service_h, "AppThemeService should expose applyTheme")
    require("themeDirectoryCandidates" in service_h, "theme directory resolution should be in AppThemeService")
    require("ComponentTheme::instance().loadTheme" in service_cpp, "AppThemeService should load ComponentTheme")
    require("ComponentTheme::instance().loadTheme" not in controller_cpp, "AppController should not load ComponentTheme directly")
    require("setHotReloadEnabled" not in controller_cpp, "AppController should not configure ComponentTheme hot reload directly")


if __name__ == "__main__":
    main()

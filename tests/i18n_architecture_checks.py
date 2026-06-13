from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> None:
    config_h = read("app/AppConfig.h")
    config_cpp = read("app/AppConfig.cpp")
    root_cmake = read("CMakeLists.txt")

    require("QString languageName() const" in config_h, "AppConfig should expose languageName()")
    require("void setLanguageName(const QString& languageName)" in config_h, "AppConfig should expose setLanguageName()")
    require('m_languageName { "en_US" }' in config_h, "AppConfig should default language to en_US")
    require(
        'm_settings->setValue(QStringLiteral("language"), QStringLiteral("en_US"))' in config_cpp,
        "AppConfig should write default ui/language=en_US",
    )
    require(
        'setLanguageName(m_settings->value(QStringLiteral("language"), QStringLiteral("en_US")).toString())' in config_cpp,
        "AppConfig should load persisted ui/language",
    )
    require(
        'm_settings->setValue(QStringLiteral("language"), m_languageName)' in config_cpp,
        "AppConfig should save persisted ui/language",
    )
    require(
        '^[a-z]{2}_[A-Z]{2}$' in config_cpp,
        "AppConfig should validate language IDs as xx_YY",
    )
    require('QStringLiteral("en_US")' in config_cpp, "AppConfig should use en_US fallback")
    require("LinguistTools" in root_cmake, "root CMake should include Qt LinguistTools")


if __name__ == "__main__":
    main()

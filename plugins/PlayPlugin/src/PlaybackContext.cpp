#include "PlaybackContext.h"

// PlaybackContext 的实现全部在头文件内联。
// 此 .cpp 文件存在是为了让 CMake 的 AUTOMOC 能为 QML_ELEMENT / QML_SINGLETON
// 宏生成正确的 moc_PlaybackContext.cpp，确保 Qt 元对象系统正常工作。

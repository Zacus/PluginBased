#pragma once

// Installs the FFmpeg global log callback used by PlayPlugin.
// The callback is process-global and does not capture decoder instances or QObject state.

void installFFmpegLogBridge();

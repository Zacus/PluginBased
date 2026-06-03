#pragma once

#include <QtGlobal>

enum class NativeFrameKind
{
    None,
    VideoToolbox
};

struct NativeVideoFrame
{
    NativeFrameKind kind = NativeFrameKind::None;
    int pixelFormat = 0;
    bool fullRange = false;
    bool bt709 = false;
    bool is10bit = false;

    bool isValid() const
    {
        return kind != NativeFrameKind::None && pixelFormat != 0;
    }
};

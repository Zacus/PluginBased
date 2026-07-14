#pragma once

#include <string>

namespace media_sdk {

enum class MediaErrorCode {
    None,
    OpenFailed,
    StreamInfoFailed,
    DecoderOpenFailed,
    DecodeFailed,
    SeekFailed,
    UnsupportedFormat,
    InternalStateError,
    InvalidArgument
};

struct MediaError {
    MediaErrorCode code = MediaErrorCode::None;
    std::string message;
    std::string detail;
};

} // namespace media_sdk

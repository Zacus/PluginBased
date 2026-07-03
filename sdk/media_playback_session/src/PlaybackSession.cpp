#include "media_sdk/session/PlaybackSession.h"

#include "media_sdk/Error.h"

#include <utility>

namespace media_sdk::session {
namespace {

Result<void> notWired()
{
    return Result<void>::failure({
        .code = MediaErrorCode::InternalStateError,
        .message = "PlaybackSession is not wired yet",
        .detail = {},
    });
}

} // namespace

struct PlaybackSession::Impl {
    PlaybackSessionConfig config;
    PlaybackSessionDependencies dependencies;
};

PlaybackSession::PlaybackSession(PlaybackSessionConfig config,
                                 PlaybackSessionDependencies dependencies)
    : m_impl(std::make_unique<Impl>(Impl {
          .config = std::move(config),
          .dependencies = dependencies,
      }))
{
}

PlaybackSession::~PlaybackSession() = default;

Result<void> PlaybackSession::open(const std::filesystem::path&)
{
    return notWired();
}

void PlaybackSession::play()
{
}

void PlaybackSession::pause()
{
}

void PlaybackSession::stop()
{
}

Result<void> PlaybackSession::seek(std::chrono::milliseconds)
{
    return notWired();
}

runtime::RuntimeTimeline PlaybackSession::timeline() const
{
    return {};
}

runtime::RuntimeDiagnostics PlaybackSession::diagnostics() const
{
    return {};
}

} // namespace media_sdk::session

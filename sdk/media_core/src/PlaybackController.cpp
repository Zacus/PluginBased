#include "PlaybackController.h"

#include <utility>

namespace media_sdk {

PlaybackController::PlaybackController(PlayerConfig config, IEventSink& events, IDecodeFrameSink& frames)
    : m_worker(std::move(config), events, frames)
{
}

PlaybackController::~PlaybackController() = default;

Result<void> PlaybackController::open(const std::filesystem::path& path)
{
    return m_worker.submitOpen(path);
}

void PlaybackController::play()
{
    m_worker.submitPlay();
}

void PlaybackController::pause()
{
    m_worker.submitPause();
}

void PlaybackController::stop()
{
    m_worker.submitStop();
}

Result<void> PlaybackController::seek(std::chrono::milliseconds position)
{
    return m_worker.submitSeek(position);
}

Result<void> PlaybackController::seek(std::chrono::milliseconds position, SeekPlaybackMode mode)
{
    return m_worker.submitSeek(position, mode);
}

Result<void> PlaybackController::seek(std::chrono::milliseconds position,
                                      SeekPlaybackMode mode,
                                      SeekRequestId requestId)
{
    return m_worker.submitSeek(position, mode, requestId);
}

PlayerDiagnostics PlaybackController::diagnostics() const
{
    return m_worker.diagnostics();
}

} // namespace media_sdk

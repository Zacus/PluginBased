#include "media_sdk/session/PlaybackSession.h"

#include "PlaybackSessionTestHooks.h"
#include "SessionEventRouter.h"
#include "SessionFrameRouter.h"
#include "SessionTimeline.h"

#include "media_sdk/Error.h"

#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

namespace media_sdk::session {
namespace {

MediaError makeSessionError(MediaErrorCode code, std::string message)
{
    return {
        .code = code,
        .message = std::move(message),
        .detail = {},
    };
}

Result<void> sessionFailure(MediaErrorCode code, std::string message)
{
    return Result<void>::failure(makeSessionError(code, std::move(message)));
}

Result<runtime::RuntimeTimeline> runtimeFailure(MediaErrorCode code, std::string message)
{
    return Result<runtime::RuntimeTimeline>::failure(makeSessionError(code, std::move(message)));
}

bool sameTimeline(runtime::RuntimeTimeline lhs, runtime::RuntimeTimeline rhs)
{
    return lhs.sessionId == rhs.sessionId && lhs.generation == rhs.generation;
}

bool hasAudio(const MediaInfo& info)
{
    return info.sampleRate > 0 && info.channels > 0;
}

runtime::RuntimePlayerConfig runtimeConfigForMedia(PlaybackSessionConfig sessionConfig,
                                                   const MediaInfo& info)
{
    auto config = sessionConfig.runtime;
    const bool mediaHasAudio = hasAudio(info);
    config.audioFormat = {
        .sampleRate = mediaHasAudio ? info.sampleRate : sessionConfig.runtime.audioFormat.sampleRate,
        .channels = mediaHasAudio ? info.channels : sessionConfig.runtime.audioFormat.channels,
        .sampleFormat = runtime::AudioSampleFormat::Float32,
    };
    config.outputPolicy = sessionConfig.preferNativeVideoFrames
        ? runtime::VideoOutputPolicy::PreferNative
        : runtime::VideoOutputPolicy::CpuOnly;
    config.audioClockEnabled = mediaHasAudio;
    return config;
}

class CorePlayerAdapter final : public detail::IPlaybackSessionCorePlayer {
public:
    CorePlayerAdapter(PlayerConfig config, IEventSink& events, IDecodeFrameSink& frames)
        : m_player(std::move(config), events, frames)
    {
    }

    Result<void> open(const std::filesystem::path& path) override
    {
        return m_player.open(path);
    }

    void play() override
    {
        m_player.play();
    }

    void pause() override
    {
        m_player.pause();
    }

    void stop() override
    {
        m_player.stop();
    }

    Result<void> seek(std::chrono::milliseconds position) override
    {
        return m_player.seek(position);
    }

private:
    Player m_player;
};

class RuntimePlayerAdapter final : public detail::IPlaybackSessionRuntimePlayer {
public:
    RuntimePlayerAdapter(runtime::RuntimePlayerConfig config,
                         runtime::RuntimePlayerDependencies dependencies)
        : m_player(std::move(config), dependencies)
    {
    }

    Result<void> open() override
    {
        return m_player.open();
    }

    runtime::RuntimeFramePushResult enqueueAudio(runtime::RuntimeAudioFrame frame) override
    {
        return m_player.enqueueAudio(std::move(frame));
    }

    runtime::RuntimeFramePushResult enqueueVideo(runtime::RuntimeVideoFrame frame) override
    {
        return m_player.enqueueVideo(std::move(frame));
    }

    void enqueueEndOfStream(runtime::SessionId sessionId, runtime::Generation generation) override
    {
        m_player.enqueueEndOfStream(sessionId, generation);
    }

    void pause() override
    {
        m_player.pause();
    }

    void resume() override
    {
        m_player.resume();
    }

    void seek(std::chrono::microseconds position) override
    {
        m_player.seek(position);
    }

    void completeSeek(runtime::SessionId sessionId, runtime::Generation generation) override
    {
        m_player.completeSeek(sessionId, generation);
    }

    void stop() override
    {
        m_player.stop();
    }

    runtime::RuntimeDiagnostics diagnostics() const override
    {
        return m_player.diagnostics();
    }

    runtime::RuntimeTimeline timeline() const override
    {
        return m_player.timeline();
    }

private:
    runtime::RuntimePlayer m_player;
};

std::shared_ptr<detail::PlaybackSessionFactories> makeDefaultFactories()
{
    auto factories = std::make_shared<detail::PlaybackSessionFactories>();
    factories->createCore =
        [](PlayerConfig config, IEventSink& events, IDecodeFrameSink& frames) {
            return std::make_shared<CorePlayerAdapter>(std::move(config), events, frames);
        };
    factories->createRuntime =
        [](runtime::RuntimePlayerConfig config, runtime::RuntimePlayerDependencies dependencies) {
            return std::make_shared<RuntimePlayerAdapter>(std::move(config), dependencies);
        };
    return factories;
}

} // namespace

class PlaybackSessionRuntimeControl {
public:
    [[nodiscard("runtime open result controls frame acceptance")]]
    Result<runtime::RuntimeTimeline> openRuntimeForMedia(const MediaInfo& info)
    {
        if (openRuntimeForMediaHandler)
            return openRuntimeForMediaHandler(info);
        return runtimeFailure(MediaErrorCode::InternalStateError,
                              "PlaybackSession runtime open handler is not configured");
    }

    void completeSeek(runtime::RuntimeTimeline runtimeTimeline)
    {
        if (completeSeekHandler)
            completeSeekHandler(runtimeTimeline);
    }

    void enqueueEndOfStream(runtime::RuntimeTimeline runtimeTimeline)
    {
        if (enqueueEndOfStreamHandler)
            enqueueEndOfStreamHandler(runtimeTimeline);
    }

    std::function<Result<runtime::RuntimeTimeline>(const MediaInfo&)> openRuntimeForMediaHandler;
    std::function<void(runtime::RuntimeTimeline)> completeSeekHandler;
    std::function<void(runtime::RuntimeTimeline)> enqueueEndOfStreamHandler;
};

struct PlaybackSession::Impl final
    : private IEventSink
    , private IDecodeFrameSink
    , private runtime::IRuntimePlayerEvents
{
    enum class PlaybackCommandState {
        Idle,
        Playing,
        Paused,
    };

    Impl(PlaybackSessionConfig sessionConfig,
         PlaybackSessionDependencies sessionDependencies,
         std::shared_ptr<detail::PlaybackSessionFactories> sessionFactories)
        : config(std::move(sessionConfig))
        , dependencies(sessionDependencies)
        , factories(std::move(sessionFactories))
        , eventRouter(runtimeControl, timelineState, dependencies.events)
    {
        runtimeControl.openRuntimeForMediaHandler =
            [this](const MediaInfo& info) { return openRuntimeForMedia(info); };
        runtimeControl.completeSeekHandler =
            [this](runtime::RuntimeTimeline runtimeTimeline) { completeSeek(runtimeTimeline); };
        runtimeControl.enqueueEndOfStreamHandler =
            [this](runtime::RuntimeTimeline runtimeTimeline) { enqueueEndOfStream(runtimeTimeline); };
    }

    ~Impl() override
    {
        stop();
    }

    Result<void> open(const std::filesystem::path& path)
    {
        if (path.empty())
            return sessionFailure(MediaErrorCode::OpenFailed, "Cannot open an empty media path");
        if (!dependencies.audioOutput) {
            return sessionFailure(MediaErrorCode::InternalStateError,
                                  "PlaybackSession requires audio output");
        }
        if (!dependencies.videoPresenter) {
            return sessionFailure(MediaErrorCode::InternalStateError,
                                  "PlaybackSession requires video presenter");
        }
        if (!factories || !factories->createCore || !factories->createRuntime) {
            return sessionFailure(MediaErrorCode::InternalStateError,
                                  "PlaybackSession factories are not configured");
        }

        stop();

        auto newCore = factories->createCore(config.core,
                                             static_cast<IEventSink&>(*this),
                                             static_cast<IDecodeFrameSink&>(*this));
        if (!newCore) {
            return sessionFailure(MediaErrorCode::InternalStateError,
                                  "PlaybackSession core factory returned null");
        }

        {
            std::lock_guard lock(m_mutex);
            currentPath = path;
            core = newCore;
        }

        const auto result = newCore->open(path);
        if (!result.ok())
            stop();
        return result;
    }

    void play()
    {
        {
            std::lock_guard lock(m_mutex);
            commandState = PlaybackCommandState::Playing;
        }

        auto handles = currentHandles();
        if (handles.runtimePlayer)
            handles.runtimePlayer->resume();
        if (handles.corePlayer)
            handles.corePlayer->play();
    }

    void pause()
    {
        {
            std::lock_guard lock(m_mutex);
            commandState = PlaybackCommandState::Paused;
        }

        auto handles = currentHandles();
        if (handles.runtimePlayer)
            handles.runtimePlayer->pause();
        if (handles.corePlayer)
            handles.corePlayer->pause();
    }

    void stop()
    {
        std::shared_ptr<detail::IPlaybackSessionCorePlayer> coreToStop;
        std::shared_ptr<detail::IPlaybackSessionRuntimePlayer> runtimeToStop;

        eventRouter.cancelFrameAcceptance();

        {
            std::lock_guard lock(m_mutex);
            coreToStop = std::move(core);
            runtimeToStop = std::move(runtimePlayer);
            currentPath.clear();
            handledFallbackTimeline.reset();
            commandState = PlaybackCommandState::Idle;
        }

        if (coreToStop)
            coreToStop->stop();
        if (runtimeToStop)
            runtimeToStop->stop();
    }

    Result<void> seek(std::chrono::milliseconds position)
    {
        if (position < std::chrono::milliseconds { 0 })
            return sessionFailure(MediaErrorCode::SeekFailed, "Cannot seek to a negative position");

        auto handles = currentHandles();
        if (!handles.corePlayer)
            return sessionFailure(MediaErrorCode::InternalStateError, "PlaybackSession is not open");

        if (handles.runtimePlayer) {
            handles.runtimePlayer->seek(std::chrono::duration_cast<std::chrono::microseconds>(position));
            eventRouter.beginSeek(handles.runtimePlayer->timeline());
        } else {
            eventRouter.cancelFrameAcceptance();
        }

        return handles.corePlayer->seek(position);
    }

    runtime::RuntimeTimeline timeline() const
    {
        std::lock_guard lock(m_mutex);
        if (runtimePlayer)
            return runtimePlayer->timeline();
        return {};
    }

    runtime::RuntimeDiagnostics diagnostics() const
    {
        const auto runtime = currentRuntimePlayer();
        return runtime ? runtime->diagnostics() : runtime::RuntimeDiagnostics {};
    }

    Result<runtime::RuntimeTimeline> openRuntimeForMedia(const MediaInfo& info)
    {
        if (hasAudio(info) && !dependencies.audioOutput) {
            return runtimeFailure(MediaErrorCode::InternalStateError,
                                  "PlaybackSession requires audio output for media with audio");
        }
        if (!dependencies.videoPresenter) {
            return runtimeFailure(MediaErrorCode::InternalStateError,
                                  "PlaybackSession requires video presenter");
        }

        auto newRuntime = factories->createRuntime(
            runtimeConfigForMedia(config, info),
            runtime::RuntimePlayerDependencies {
                .audioOutput = dependencies.audioOutput,
                .videoPresenter = dependencies.videoPresenter,
                .events = static_cast<runtime::IRuntimePlayerEvents*>(this),
            });
        if (!newRuntime) {
            return runtimeFailure(MediaErrorCode::InternalStateError,
                                  "PlaybackSession runtime factory returned null");
        }

        const auto openResult = newRuntime->open();
        if (!openResult.ok())
            return Result<runtime::RuntimeTimeline>::failure(openResult.error());

        const auto runtimeTimeline = newRuntime->timeline();
        auto runtimeToSynchronize = newRuntime;
        std::shared_ptr<detail::IPlaybackSessionRuntimePlayer> previousRuntime;
        {
            std::lock_guard lock(m_mutex);
            previousRuntime = std::move(runtimePlayer);
            runtimePlayer = std::move(newRuntime);
            handledFallbackTimeline.reset();
        }
        if (previousRuntime)
            previousRuntime->stop();
        synchronizeRuntimePlaybackState(runtimeToSynchronize);
        notifyRuntimeDiagnostics();
        return Result<runtime::RuntimeTimeline>::success(runtimeTimeline);
    }

    void completeSeek(runtime::RuntimeTimeline runtimeTimeline)
    {
        std::shared_ptr<detail::IPlaybackSessionRuntimePlayer> currentRuntime;
        {
            std::lock_guard lock(m_mutex);
            currentRuntime = runtimePlayer;
        }
        if (currentRuntime) {
            currentRuntime->completeSeek(runtimeTimeline.sessionId, runtimeTimeline.generation);
            synchronizeRuntimePlaybackState(currentRuntime);
            notifyRuntimeDiagnostics();
        }
    }

    void enqueueEndOfStream(runtime::RuntimeTimeline runtimeTimeline)
    {
        std::shared_ptr<detail::IPlaybackSessionRuntimePlayer> currentRuntime;
        {
            std::lock_guard lock(m_mutex);
            currentRuntime = runtimePlayer;
        }
        if (currentRuntime) {
            currentRuntime->enqueueEndOfStream(runtimeTimeline.sessionId, runtimeTimeline.generation);
            notifyRuntimeDiagnostics();
        }
    }

private:
    struct Handles {
        std::shared_ptr<detail::IPlaybackSessionCorePlayer> corePlayer;
        std::shared_ptr<detail::IPlaybackSessionRuntimePlayer> runtimePlayer;
    };

    Handles currentHandles() const
    {
        std::lock_guard lock(m_mutex);
        return {
            .corePlayer = core,
            .runtimePlayer = runtimePlayer,
        };
    }

    void onEvent(const PlayerEvent& event) override
    {
        eventRouter.onEvent(event);
    }

    DecodeFramePushResult pushAudio(AudioFrame frame, DecodeFrameMetadata metadata) override
    {
        std::shared_ptr<detail::IPlaybackSessionRuntimePlayer> currentRuntime;
        {
            std::lock_guard lock(m_mutex);
            currentRuntime = runtimePlayer;
        }
        if (!currentRuntime)
            return { .status = DecodeFramePushStatus::StaleGeneration };

        BasicSessionFrameRouter<detail::IPlaybackSessionRuntimePlayer> router(
            *currentRuntime,
            timelineState);
        return router.pushAudio(std::move(frame), metadata);
    }

    DecodeFramePushResult pushVideo(VideoFrame frame, DecodeFrameMetadata metadata) override
    {
        std::shared_ptr<detail::IPlaybackSessionRuntimePlayer> currentRuntime;
        {
            std::lock_guard lock(m_mutex);
            currentRuntime = runtimePlayer;
        }
        if (!currentRuntime)
            return { .status = DecodeFramePushStatus::StaleGeneration };

        BasicSessionFrameRouter<detail::IPlaybackSessionRuntimePlayer> router(
            *currentRuntime,
            timelineState);
        return router.pushVideo(std::move(frame), metadata);
    }

    void onFallbackToCpuRequested(runtime::RuntimeFallbackAction action) override
    {
        handleFallbackToCpu(action);
    }

    void onEndOfStreamPresented(runtime::RuntimeTimeline runtimeTimeline) override
    {
        if (!timelineState.acceptsRuntimeTimeline(runtimeTimeline))
            return;
        eventRouter.onEndOfStreamPresented(runtimeTimeline);
        notifyRuntimeDiagnostics();
    }

    void handleFallbackToCpu(runtime::RuntimeFallbackAction action)
    {
        const runtime::RuntimeTimeline fallbackTimeline {
            .sessionId = action.sessionId,
            .generation = action.generation,
        };
        const auto coreMetadata = timelineState.coreForRuntimeTimeline(fallbackTimeline);
        if (!coreMetadata.has_value())
            return;

        std::filesystem::path path;
        PlayerConfig fallbackCoreConfig;
        std::shared_ptr<detail::PlaybackSessionFactories> currentFactories;
        {
            std::lock_guard lock(m_mutex);
            if (handledFallbackTimeline.has_value()
                && sameTimeline(*handledFallbackTimeline, fallbackTimeline)) {
                return;
            }
            if (currentPath.empty() || !factories || !factories->createCore)
                return;

            handledFallbackTimeline = fallbackTimeline;
            config.core.preferNativeVideoFrames = false;
            config.preferNativeVideoFrames = false;
            path = currentPath;
            fallbackCoreConfig = config.core;
            currentFactories = factories;
        }

        eventRouter.beginFallbackSeek(fallbackTimeline);

        auto fallbackCore = currentFactories->createCore(
            fallbackCoreConfig,
            static_cast<IEventSink&>(*this),
            static_cast<IDecodeFrameSink&>(*this));
        if (!fallbackCore) {
            emitError(*coreMetadata,
                      makeSessionError(MediaErrorCode::InternalStateError,
                                       "PlaybackSession fallback core factory returned null"));
            return;
        }

        std::shared_ptr<detail::IPlaybackSessionCorePlayer> previousCore;
        {
            std::lock_guard lock(m_mutex);
            previousCore = std::move(core);
            core = fallbackCore;
        }
        if (previousCore)
            previousCore->stop();

        const auto openResult = fallbackCore->open(path);
        if (!openResult.ok()) {
            emitError(*coreMetadata, openResult.error());
            return;
        }

        fallbackCore->play();
        const auto seekResult = fallbackCore->seek(
            std::chrono::duration_cast<std::chrono::milliseconds>(action.resumePosition));
        if (!seekResult.ok()) {
            emitError(*coreMetadata, seekResult.error());
            return;
        }
        if (playbackCommandState() == PlaybackCommandState::Paused)
            fallbackCore->pause();

        if (dependencies.events)
            dependencies.events->onNativeRenderingFailed();
        notifyRuntimeDiagnostics();
    }

    void synchronizeRuntimePlaybackState(
        const std::shared_ptr<detail::IPlaybackSessionRuntimePlayer>& runtime) const
    {
        PlaybackCommandState state = PlaybackCommandState::Idle;
        {
            std::lock_guard lock(m_mutex);
            if (runtimePlayer != runtime)
                return;
            state = commandState;
        }

        if (state == PlaybackCommandState::Paused)
            runtime->pause();
        else if (state == PlaybackCommandState::Playing)
            runtime->resume();
    }

    PlaybackCommandState playbackCommandState() const
    {
        std::lock_guard lock(m_mutex);
        return commandState;
    }

    std::shared_ptr<detail::IPlaybackSessionRuntimePlayer> currentRuntimePlayer() const
    {
        std::lock_guard lock(m_mutex);
        return runtimePlayer;
    }

    void notifyRuntimeDiagnostics() const
    {
        if (!dependencies.events)
            return;

        const auto runtime = currentRuntimePlayer();
        if (!runtime)
            return;

        dependencies.events->onRuntimeDiagnostics(runtime->diagnostics());
    }

    void emitError(EventMetadata metadata, MediaError error)
    {
        if (!dependencies.events)
            return;

        dependencies.events->onEvent({
            .metadata = metadata,
            .payload = ErrorEvent {
                .error = std::move(error),
            },
        });
    }

    PlaybackSessionConfig config;
    PlaybackSessionDependencies dependencies;
    std::shared_ptr<detail::PlaybackSessionFactories> factories;
    mutable std::mutex m_mutex;
    SessionTimeline timelineState;
    PlaybackSessionRuntimeControl runtimeControl;
    SessionEventRouter<PlaybackSessionRuntimeControl> eventRouter;
    std::shared_ptr<detail::IPlaybackSessionCorePlayer> core;
    std::shared_ptr<detail::IPlaybackSessionRuntimePlayer> runtimePlayer;
    std::filesystem::path currentPath;
    std::optional<runtime::RuntimeTimeline> handledFallbackTimeline;
    PlaybackCommandState commandState = PlaybackCommandState::Idle;
};

PlaybackSession::PlaybackSession(PlaybackSessionConfig config,
                                 PlaybackSessionDependencies dependencies)
    : PlaybackSession(std::move(config), dependencies, makeDefaultFactories())
{
}

PlaybackSession::PlaybackSession(PlaybackSessionConfig config,
                                 PlaybackSessionDependencies dependencies,
                                 std::shared_ptr<detail::PlaybackSessionFactories> factories)
    : m_impl(std::make_unique<Impl>(std::move(config), dependencies, std::move(factories)))
{
}

PlaybackSession::~PlaybackSession() = default;

Result<void> PlaybackSession::open(const std::filesystem::path& path)
{
    return m_impl->open(path);
}

void PlaybackSession::play()
{
    m_impl->play();
}

void PlaybackSession::pause()
{
    m_impl->pause();
}

void PlaybackSession::stop()
{
    m_impl->stop();
}

Result<void> PlaybackSession::seek(std::chrono::milliseconds position)
{
    return m_impl->seek(position);
}

runtime::RuntimeTimeline PlaybackSession::timeline() const
{
    return m_impl->timeline();
}

runtime::RuntimeDiagnostics PlaybackSession::diagnostics() const
{
    return m_impl->diagnostics();
}

std::unique_ptr<PlaybackSession> detail::PlaybackSessionTestHooks::create(
    PlaybackSessionConfig config,
    PlaybackSessionDependencies dependencies,
    PlaybackSessionFactories factories)
{
    return std::unique_ptr<PlaybackSession>(new PlaybackSession(
        std::move(config),
        dependencies,
        std::make_shared<PlaybackSessionFactories>(std::move(factories))));
}

} // namespace media_sdk::session

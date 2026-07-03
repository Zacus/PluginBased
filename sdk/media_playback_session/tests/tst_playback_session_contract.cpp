#include "media_sdk/session/PlaybackSession.h"

#include <cassert>
#include <chrono>
#include <filesystem>

using namespace std::chrono_literals;

namespace {

class RecordingSessionEvents final : public media_sdk::session::ISessionEvents {
public:
    void onEvent(const media_sdk::PlayerEvent& event) override
    {
        ++eventCount;
        lastEvent = event;
    }

    void onRuntimeDiagnostics(media_sdk::runtime::RuntimeDiagnostics diagnostics) override
    {
        ++diagnosticsCount;
        lastDiagnostics = diagnostics;
    }

    int eventCount = 0;
    int diagnosticsCount = 0;
    media_sdk::PlayerEvent lastEvent {};
    media_sdk::runtime::RuntimeDiagnostics lastDiagnostics {};
};

void constructsAndReportsUnwiredState()
{
    RecordingSessionEvents events;
    media_sdk::session::PlaybackSession session(
        {},
        {
            .audioOutput = nullptr,
            .videoPresenter = nullptr,
            .events = &events,
        });

    const auto openResult = session.open(std::filesystem::path("sample.mov"));
    assert(!openResult.ok());
    assert(openResult.error().code == media_sdk::MediaErrorCode::InternalStateError);
    assert(session.timeline().sessionId == 0);
    assert(session.timeline().generation == 0);
    assert(session.diagnostics().videoPresented == 0);

    session.play();
    session.pause();
    session.stop();

    const auto seekResult = session.seek(100ms);
    assert(!seekResult.ok());
    assert(seekResult.error().code == media_sdk::MediaErrorCode::InternalStateError);
    assert(events.eventCount == 0);
    assert(events.diagnosticsCount == 0);
}

} // namespace

int main()
{
    constructsAndReportsUnwiredState();
}

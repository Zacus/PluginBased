#include "SessionTimeline.h"

#include <cassert>

namespace {

media_sdk::EventMetadata coreTimeline(std::uint64_t sessionId, std::uint64_t generation)
{
    return {
        .sessionId = sessionId,
        .generation = generation,
    };
}

media_sdk::runtime::RuntimeTimeline runtimeTimeline(std::uint64_t sessionId,
                                                    std::uint64_t generation)
{
    return {
        .sessionId = sessionId,
        .generation = generation,
    };
}

void initialStateRejectsEverything()
{
    media_sdk::session::SessionTimeline timeline;

    assert(!timeline.acceptsCoreEvent(coreTimeline(1, 1)));
    assert(!timeline.runtimeForCoreEvent(coreTimeline(1, 1)).has_value());
    assert(!timeline.acceptsRuntimeTimeline(runtimeTimeline(1, 1)));
}

void acceptedCoreTimelineReturnsRuntimeTimeline()
{
    media_sdk::session::SessionTimeline timeline;
    timeline.acceptCoreTimeline(coreTimeline(10, 3), runtimeTimeline(20, 7));

    assert(timeline.acceptsCoreEvent(coreTimeline(10, 3)));
    const auto runtime = timeline.runtimeForCoreEvent(coreTimeline(10, 3));
    assert(runtime.has_value());
    assert(runtime->sessionId == 20);
    assert(runtime->generation == 7);
    assert(timeline.acceptsRuntimeTimeline(runtimeTimeline(20, 7)));
}

void staleCoreSessionIsRejected()
{
    media_sdk::session::SessionTimeline timeline;
    timeline.acceptCoreTimeline(coreTimeline(10, 3), runtimeTimeline(20, 7));

    assert(!timeline.acceptsCoreEvent(coreTimeline(9, 3)));
    assert(!timeline.runtimeForCoreEvent(coreTimeline(9, 3)).has_value());
}

void staleCoreGenerationIsRejected()
{
    media_sdk::session::SessionTimeline timeline;
    timeline.acceptCoreTimeline(coreTimeline(10, 3), runtimeTimeline(20, 7));

    assert(!timeline.acceptsCoreEvent(coreTimeline(10, 2)));
    assert(!timeline.runtimeForCoreEvent(coreTimeline(10, 4)).has_value());
}

void staleRuntimeTimelineIsRejected()
{
    media_sdk::session::SessionTimeline timeline;
    timeline.acceptCoreTimeline(coreTimeline(10, 3), runtimeTimeline(20, 7));

    assert(!timeline.acceptsRuntimeTimeline(runtimeTimeline(20, 6)));
    assert(!timeline.acceptsRuntimeTimeline(runtimeTimeline(19, 7)));
}

void replacingTimelineRejectsOldValues()
{
    media_sdk::session::SessionTimeline timeline;
    timeline.acceptCoreTimeline(coreTimeline(10, 3), runtimeTimeline(20, 7));
    timeline.acceptCoreTimeline(coreTimeline(11, 1), runtimeTimeline(21, 1));

    assert(!timeline.acceptsCoreEvent(coreTimeline(10, 3)));
    assert(!timeline.acceptsRuntimeTimeline(runtimeTimeline(20, 7)));
    assert(timeline.acceptsCoreEvent(coreTimeline(11, 1)));
    assert(timeline.acceptsRuntimeTimeline(runtimeTimeline(21, 1)));
}

void clearRejectsEverything()
{
    media_sdk::session::SessionTimeline timeline;
    timeline.acceptCoreTimeline(coreTimeline(10, 3), runtimeTimeline(20, 7));
    timeline.clear();

    assert(!timeline.acceptsCoreEvent(coreTimeline(10, 3)));
    assert(!timeline.runtimeForCoreEvent(coreTimeline(10, 3)).has_value());
    assert(!timeline.acceptsRuntimeTimeline(runtimeTimeline(20, 7)));
}

} // namespace

int main()
{
    initialStateRejectsEverything();
    acceptedCoreTimelineReturnsRuntimeTimeline();
    staleCoreSessionIsRejected();
    staleCoreGenerationIsRejected();
    staleRuntimeTimelineIsRejected();
    replacingTimelineRejectsOldValues();
    clearRejectsEverything();
}

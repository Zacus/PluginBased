#include "playback/PlaybackCompletionTracker.h"

#include <cassert>

void seekDuringDrainAfterDecoderEofResumesPlayback()
{
    PlaybackCompletionTracker completion;
    completion.resetForOpen();
    completion.setStreams(true, true);
    completion.markDecoderFinished();

    assert(!completion.shouldFinish());
    assert(completion.resumeAfterFinishedSeek(true));
    assert(!completion.shouldFinish());
}

void seekDuringPausedDrainAfterDecoderEofDoesNotForceResume()
{
    PlaybackCompletionTracker completion;
    completion.resetForOpen();
    completion.setStreams(true, true);
    completion.markDecoderFinished();

    assert(!completion.shouldFinish());
    assert(!completion.resumeAfterFinishedSeek(false));
    assert(!completion.shouldFinish());
}

void seekAfterFullyFinishedMediaStillResumesPlayback()
{
    PlaybackCompletionTracker completion;
    completion.resetForOpen();
    completion.setStreams(true, true);
    completion.markDecoderFinished();
    completion.markAudioFinished();
    completion.markVideoFinished();

    assert(completion.shouldFinish());
    assert(completion.finish());
    assert(completion.resumeAfterFinishedSeek(false));
    assert(!completion.shouldFinish());
}

void seekDuringActivePlaybackDoesNotForceResumeTransition()
{
    PlaybackCompletionTracker completion;
    completion.resetForOpen();
    completion.setStreams(true, true);

    assert(!completion.resumeAfterFinishedSeek(true));
}

int main()
{
    seekDuringDrainAfterDecoderEofResumesPlayback();
    seekDuringPausedDrainAfterDecoderEofDoesNotForceResume();
    seekAfterFullyFinishedMediaStillResumesPlayback();
    seekDuringActivePlaybackDoesNotForceResumeTransition();
    return 0;
}

#include "playback/PlaybackCompletionTracker.h"

#include <cassert>

void seekDuringDrainAfterDecoderEofResumesPlayback()
{
    PlaybackCompletionTracker completion;
    completion.resetForOpen();
    completion.setStreams(true, true);
    completion.markDecoderFinished();

    assert(!completion.shouldFinish());
    assert(completion.resumeAfterFinishedSeek());
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
    assert(completion.resumeAfterFinishedSeek());
    assert(!completion.shouldFinish());
}

void seekDuringActivePlaybackDoesNotForceResumeTransition()
{
    PlaybackCompletionTracker completion;
    completion.resetForOpen();
    completion.setStreams(true, true);

    assert(!completion.resumeAfterFinishedSeek());
}

int main()
{
    seekDuringDrainAfterDecoderEofResumesPlayback();
    seekAfterFullyFinishedMediaStillResumesPlayback();
    seekDuringActivePlaybackDoesNotForceResumeTransition();
    return 0;
}

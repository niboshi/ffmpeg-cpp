#ifndef FRAME_ENUMERATOR_H_
#define FRAME_ENUMERATOR_H_

#include "MediaDecoder.hpp"
#include "libav.hpp"
#include <iterator>

class FrameEnumerator
{
public:
    typedef bool(*FramePredicate)(const AVIndexEntry*);
    
private:
    MediaDecoder*       mMediaDecoder;
    AVStream*           mStream;
    int64_t             mLastTimestamp;
    int                 mLastIndex;
    
    FramePredicate      mFramePred;

public:
    FrameEnumerator(MediaDecoder* mediaDecoder, AVStream* stream);
    bool moveNext();
    int64_t timestamp() const { return mLastTimestamp; }
    int index() const { return mLastIndex; }
    void reset();

    int seek();

private:
    static bool FramePredicate_KeyFrames(const AVIndexEntry* indexEntry);
};

#endif

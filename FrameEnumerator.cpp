
#include "FrameEnumerator.hpp"
#include <cassert>
#include <cstdio>

FrameEnumerator::FrameEnumerator(MediaDecoder* mediaDecoder, AVStream* stream)
    : mMediaDecoder(mediaDecoder),
      mStream(stream),
      mLastTimestamp(AV_NOPTS_VALUE),
      mLastIndex(-1),
      mFramePred(FramePredicate_KeyFrames)
{
    if (mStream->nb_index_entries < mStream->nb_frames) {
        mMediaDecoder->seek(0);
        AVPacket pkt;
        while (true) {
            int ret = mMediaDecoder->readPacket(&pkt);
            av_free_packet(&pkt);
            if (ret == AVERROR_EOF) { break; }
            if (ret < 0) { break; }
        }
    }
}

bool
FrameEnumerator::moveNext()
{
    assert(-1 <= mLastIndex);
    if (mLastIndex >= 0 &&
        (mStream->nb_index_entries <= mLastIndex || mStream->index_entries[mLastIndex].timestamp != mLastTimestamp)) {
        // The index has changed.
        // It seems that something to handle this situation should be implemented.
        assert(false);
    }
        
    AVIndexEntry* entries = mStream->index_entries;
    int nIndexEntries = mStream->nb_index_entries;
    for (mLastIndex++; mLastIndex < nIndexEntries; ++mLastIndex) {
        AVIndexEntry* ent = &entries[mLastIndex];
        if (this->mFramePred(ent)) {
            mLastTimestamp = ent->timestamp;
            return true;
        }
    }
    
    mLastIndex = nIndexEntries;
    mLastTimestamp = AV_NOPTS_VALUE;
    return false;
}

void
FrameEnumerator::reset()
{
    mLastIndex = -1;
    mLastTimestamp = AV_NOPTS_VALUE;
}

int
FrameEnumerator::seek()
{
    if (mLastIndex < 0) { return -1; }
    return mMediaDecoder->seekToIndex(mStream->index, mLastIndex);
}

bool
FrameEnumerator::FramePredicate_KeyFrames(const AVIndexEntry* indexEntry)
{
    return 0 != (indexEntry->flags & AVINDEX_KEYFRAME);
}


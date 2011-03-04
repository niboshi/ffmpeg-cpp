#include "MediaDecoder.hpp"

#include <cassert>

void
MediaDecoder::initFfmpeg()
{
    //av_log_set_callback(NULL);
    avcodec_register_all();
    av_register_all();
}

void
MediaDecoder::outputMessage(int level, const char* format, ...)
{
    va_list vl;
    va_start(vl, format);
    
    vprintf(format, vl);
    printf("\n");
    
    va_end(vl);
}

MediaDecoder::MediaDecoder()
  : mFormatContext(NULL),
    mSwsContext(NULL),
    mFirstVideoStreamIndex(-1),
    mScaleWidth(-1), mScaleHeight(-1), mScaleStride(-1), mScalePixelFormat(PIX_FMT_NONE)
{
    mFrame = avcodec_alloc_frame();
}

MediaDecoder::~MediaDecoder()
{
    if (mSwsContext) {
        sws_freeContext(mSwsContext);
        mSwsContext = NULL;
    }
    if (mFrame) {
        av_free(mFrame);
        mFrame = NULL;
    }
    if (mFormatContext) {
        av_close_input_file(mFormatContext);
        mFormatContext = NULL;
    }
}


int
MediaDecoder::openFile(const char* filename)
{
    int intRet;
	
    // open the input file.
    if (0 > (intRet = av_open_input_file(&mFormatContext, filename, NULL, 0, NULL))) {
        this->outputMessage(0, "Could not open input file: ret=%d file=%s", intRet, filename);
        return intRet;
    }
    
    if (0 > (intRet = av_find_stream_info(mFormatContext))) {
        this->outputMessage(0, "av_find_stream_info() failed: ret=%d", intRet);
        return intRet;
    }
    
    int streamCount = mFormatContext->nb_streams;
    
    // prepare the streams.
    for (int i = 0; i < streamCount; ++i) {
        if (0 > (intRet = this->prepareStream(i))) {
            this->outputMessage(0, "prepareStream() failed: ret=%d", intRet);
        }
        if (mFirstVideoStreamIndex == -1 && mFormatContext->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            mFirstVideoStreamIndex = i;
        }
    }
    
    if (mFirstVideoStreamIndex != -1) {
        this->outputMessage(0, "Video stream is %d", mFirstVideoStreamIndex);
        AVCodecContext* codecContext = mFormatContext->streams[mFirstVideoStreamIndex]->codec;
        this->setScaleParameters(codecContext->width, codecContext->height, PIX_FMT_RGB32, Stride_4bytes);
    }
    
    return 0;
}

static int
getSmallestPowerOfTwo(int n) {
    int f = 0;
    int c = 0;
    while (n > 1) {
        f |= (n & 1);
        n >>= 1;
        c++;
    }
    if (f) { c++; }
    return  n << c;
}

int
MediaDecoder::setScaleParameters(int width, int height, PixelFormat pixelFormat, StrideMode strideMode)
{
    AVCodecContext* codecContext = this->getVideoCodec();
    if (codecContext == NULL) {
        this->outputMessage(0, "No video stream found. Cannot set scale parameters.");
        return -1;
    }
    
    mSwsContext = sws_getCachedContext(
        mSwsContext,
        codecContext->width,
        codecContext->height,
        codecContext->pix_fmt,
        width,
        height,
        pixelFormat,
//      SWS_LANCZOS,
//      SWS_BILINEAR,
        SWS_FAST_BILINEAR,
        NULL, NULL, NULL);
    
    if (!mSwsContext) {
        this->outputMessage(0, "sws_getContext failed.");
        return -1;
    }
        
    // calc stride
    int tightLineSize = avpicture_get_size(pixelFormat, width, 1);
    int stride = -1;
    switch (strideMode) {
    case Stride_Tight:
        stride = tightLineSize;
        break;
    case Stride_PowerOfTwo:
        stride = getSmallestPowerOfTwo(tightLineSize);
        break;
    case Stride_4bytes:
        stride = ((tightLineSize + 3) >> 2) << 2;
        break;
    default:
        this->outputMessage(0, "Unknown stride mode: %d", strideMode);
        return -1;
    }
    
    mScaleStride = stride;
    mScaleWidth = width;
    mScaleHeight = height;
    mScalePixelFormat = pixelFormat;
    return 0;
}

int
MediaDecoder::seek(int streamIndex, int64_t timestamp)
{
    return av_seek_frame(mFormatContext, streamIndex, timestamp, 0);
}

int
MediaDecoder::seek(double timestamp)
{
    return this->seek(-1, static_cast<uint64_t>(timestamp * AV_TIME_BASE));
}

int
MediaDecoder::seekToIndex(int streamIndex, int index) {
    AVStream* stream = this->getStream(streamIndex);
    if (!stream) {
        this->outputMessage(0, "Stream index %d not found.", streamIndex);
        return -1; }
    
    AVIndexEntry* entries = stream->index_entries;
    if (!entries) {
        this->outputMessage(0, "Tried to seek by index, but there is no index entries.");
        return -1; }

    if (index < 0 || stream->nb_index_entries <= index) {
        this->outputMessage(0, "Index out of bound: %d / %d", index, stream->nb_index_entries);
        return -1;
    }

    AVIndexEntry* entry = &entries[index];
    int ret = url_fseek(mFormatContext->pb, entry->pos, SEEK_SET);
    if (ret < 0) { return ret; }
    av_update_cur_dts(mFormatContext, stream, entry->timestamp);
    return 0;
}

int
MediaDecoder::readPacket(AVPacket* packet)
{
    int ret = av_read_frame(mFormatContext, packet);
    if (ret == AVERROR_EOF) { return ret; }
    if (ret < 0) {
        this->outputMessage(0, "av_read_frame() failed: %d", ret);
    }
    return ret;
}

double
MediaDecoder::getVideoTimestamp(AVPacket* packet, AVFrame* frame)
{
    AVStream* stream = mFormatContext->streams[packet->stream_index];
    int64_t pts = 0;
    if (frame->reordered_opaque != (signed)AV_NOPTS_VALUE) {
        pts = frame->reordered_opaque;
    } else if (packet->dts != (signed)AV_NOPTS_VALUE) {
        pts = packet->dts;
    }

    double timeBase = av_q2d(stream->time_base);
    return pts * timeBase;
}

int
MediaDecoder::decodeVideoFrame(AVPacket* packet, AVFrame* outFrame, double* outTimestamp)
{
    int             intRet;
    int             gotPicture;
    int             streamIndex = packet->stream_index;
    AVCodecContext* codecContext = mFormatContext->streams[streamIndex]->codec;

    codecContext->reordered_opaque = packet->pts;

    intRet = avcodec_decode_video2(codecContext, outFrame, &gotPicture, packet);
    if (0 > intRet) { return intRet; }

    if (!gotPicture) { return 1; }
    
    if (outTimestamp) {
        *outTimestamp = this->getVideoTimestamp(packet, outFrame);
    }

    return 0;
}

int
MediaDecoder::scaleVideoFrame(AVFrame* frame, uint8_t* buffer, int bufferSize)
{
    if (!mSwsContext) {
        this->outputMessage(0, "sws context is not initialized.");
        return -1; }
    
    int      intRet;
    uint8_t* data[]     = { (uint8_t*)buffer, NULL, NULL, NULL };
    int      lineSize[] = { mScaleStride, 0, 0, 0 };
    if (bufferSize < this->getScaleBufferSize()) {
        this->outputMessage(0, "Insufficient buffer size (%d). %d is required.", bufferSize, this->getScaleBufferSize());
        return -1;
    }
    
    intRet = sws_scale(
        mSwsContext,
        frame->data,     // src
        frame->linesize, // src stride
        0,               // src slice Y
        mScaleHeight,    // src slice H
        data,            // dst
        lineSize         // dst stride
        );
    return intRet;
}

int
MediaDecoder::decodeVideo(AVPacket* packet, uint8_t* buffer, int bufferSize, double thresholdTimestamp, double* outTimestamp, int* outDataSize, bool* outSkipped)
{
    int             intRet;
    int             result = 0;
    double          timestamp;

    if (outTimestamp) { *outTimestamp = 0.0; }
    if (outDataSize)  { *outDataSize = 0; }
    if (outSkipped)   { *outSkipped = false; }


    // Decode the next frame
    intRet = this->decodeVideoFrame(packet, mFrame, &timestamp);
    if (intRet != 0) {
        result = intRet;
        goto CLEANUP;
    }

    if (timestamp < thresholdTimestamp) {
        if (outSkipped) { *outSkipped = true; }
        result = 1;
        goto CLEANUP;
    }

    // scale and transform
    intRet = this->scaleVideoFrame(mFrame, buffer, bufferSize);
    if (intRet < 0) {
        result = intRet;
        goto CLEANUP;
    }

    if (outTimestamp) { *outTimestamp  = timestamp; }
    if (outDataSize)  { *outDataSize   = mScaleHeight * mScaleStride; }

CLEANUP:
    return result;
}

double
MediaDecoder::getAudioTimestamp(const AVPacket* packet)
{
    AVStream* stream = mFormatContext->streams[packet->stream_index];
    double timeBase = av_q2d(stream->time_base);
    if (packet->pts == (signed)AV_NOPTS_VALUE) {
        return 0.0;
    } else {
        return packet->pts * timeBase;
    }
}

int
MediaDecoder::decodeAudio(AVPacket* packet, uint8_t* buffer, int bufferSize, double* outTimestamp, int* outDataSize)
{
    int     readCount = 0;
    AVCodecContext* codecContext = mFormatContext->streams[packet->stream_index]->codec;

    while (packet->size > 0) {
        int bufSize = bufferSize - readCount;

        int size = avcodec_decode_audio3(codecContext, (int16_t*)(buffer + readCount), &bufSize, packet);
        if (size == 0) { break; }
        if (size < 0) {
            this->outputMessage(0, "avcodec_decode_audio3() failed: %d", size);
            break;
        }

        if (bufSize < 0) { continue; }

        packet->size -= size;
        readCount += bufSize;
    }

    double timestamp = this->getAudioTimestamp(packet);
    *outTimestamp  = timestamp;
    *outDataSize = readCount;

    return 0;
}

int
MediaDecoder::prepareStream(int streamIndex)
{
    int intRet;
    
    AVStream*       stream          = mFormatContext->streams[streamIndex];
    AVCodecContext* codecContext    = stream->codec;
    AVCodec*        codec           = avcodec_find_decoder(codecContext->codec_id);
    
    if (!codec) {
        this->outputMessage(0, "Failed to find decoder: streamIndex=%d codecId=%d codecType=%d", streamIndex, codecContext->codec_id, codecContext->codec_type);
        return -1;
    }
    
    // Handle truncated bitstreams
    if (codec->capabilities & CODEC_CAP_TRUNCATED) {
        codecContext->flags |= CODEC_FLAG_TRUNCATED;
    }
    
    codecContext->debug             = 0;
    codecContext->debug_mv          = 0;
    codecContext->lowres            = 0;  // lowres decoding: 1->1/2   2->1/4
    codecContext->workaround_bugs   = 1;
    codecContext->idct_algo         = FF_IDCT_AUTO;
    codecContext->skip_frame        = AVDISCARD_DEFAULT;
    codecContext->skip_idct         = AVDISCARD_DEFAULT;
    codecContext->skip_loop_filter  = AVDISCARD_DEFAULT;
    codecContext->error_recognition = FF_ER_CAREFUL;
    codecContext->error_concealment = FF_EC_GUESS_MVS | FF_EC_DEBLOCK;

    // open the codec.
    if (0 > (intRet = avcodec_open(codecContext, codec))) {
        this->outputMessage(0, "avcodec_open() failed: %d", intRet);
        return intRet;
    }
    
    return 0;
}



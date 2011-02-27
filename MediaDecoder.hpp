#ifndef MEDIA_DECODER_H_
#define MEDIA_DECODER_H_

#ifndef __STDC_CONSTANT_MACROS
#define __STDC_CONSTANT_MACROS
#endif

extern "C" {
#include <libavutil/avutil.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}


typedef enum {
    Stride_Tight,
    Stride_PowerOfTwo,
    Stride_4bytes,
} StrideMode;


class MediaDecoder
{

private:
	AVFormatContext*			mFormatContext;
    SwsContext*                 mSwsContext;

    AVFrame*                    mFrame;
	
	int							mFirstVideoStreamIndex;
	
	int							mScaleWidth;
	int							mScaleHeight;
    int                         mScaleStride;
    PixelFormat                 mScalePixelFormat;
	
public:
	MediaDecoder();
    ~MediaDecoder();
	
	void outputMessage(int level, const char* format, ...);

    // init
    int openFile(const char* filename);

    // properties

    AVFormatContext* getFormatContext() const { return mFormatContext; }
    SwsContext* getSwsContext() const { return mSwsContext; }
    
    double getDuration() const { return (double)mFormatContext->duration / AV_TIME_BASE; }
    
	int setScaleParameters(int width, int height, PixelFormat pixelFormat, StrideMode strideMode);
    int getScaleWidth() const { return mScaleWidth; }
    int getScaleHeight() const { return mScaleHeight; }
    int getScaleStride() const { return mScaleStride; }
    int getScalePixelFormat() const { return mScalePixelFormat; }
    int getScaleBufferSize() const { return mScaleStride * mScaleHeight; }

    AVStream* getStream(int streamIndex) const {
        if (streamIndex < 0 || getStreamCount() <= streamIndex) { return NULL; }
        return mFormatContext->streams[streamIndex];
    }
    AVCodecContext* getCodec(int streamIndex) const {
        AVStream* stream = getStream(streamIndex);
        if (!stream) { return NULL; }
        return stream->codec;
    }
    int getStreamCount() const { return mFormatContext->nb_streams; }
    int getVideoStreamIndex() const {
        return mFirstVideoStreamIndex;
    }
    AVCodecContext* getVideoCodec() const {
        int i = this->getVideoStreamIndex();
        return getCodec(i);
    }
    AVMediaType getStreamMediaType(AVPacket* packet) const {
        return getCodec(packet->stream_index)->codec_type;
    }

    // decode
    int seek(double timestamp);
    int seekToIndex(int streamIndex, int index);
    int decodeAudio(AVPacket* packet, uint8_t* buffer, int bufferSize, double* outTimestamp, int* outDataSize);
    int decodeVideo(AVPacket* packet, uint8_t* buffer, int bufferSize, double thresholdTimestamp, double* outTimestamp, int* outDataSize, bool* outSkipped);
    int readPacket(AVPacket* packet);

private:
    // init
	int prepareStream(int streamIndex);

    // decode
    int decodeVideoFrame(AVPacket* packet, AVFrame* outFrame, double* outTimestamp);
    double getVideoTimestamp(AVPacket* packet, AVFrame* frame);
    int scaleVideoFrame(AVFrame* frame, uint8_t* buffer, int bufferSize);
    double getAudioTimestamp(const AVPacket* packet);
    
public:
    static void initFfmpeg();
    static AVPacket* newPacket() { AVPacket* p = new AVPacket; av_init_packet(p); return p; }
    static void freePacket(AVPacket* packet) { av_free_packet(packet); }
};

#endif

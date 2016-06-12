/*
 * H264_Live_Video_Stream.hh
 *
 *  Created on: 2013-10-23
 *      Author: root
 */

#ifndef _FRAMED_FILTER_HH
#include "FramedSource.hh"
#include "H264VideoRTPSink.hh"
#include "H264VideoStreamFramer.hh"
#include "ByteStreamMemoryBufferSource.hh"
#include "ByteStreamFileSource.hh"
#include <pthread.h>
#endif

//*********************************************************************
struct frame_I{
    unsigned char data[546000];
    int size;
    pthread_rwlock_t rwlock;//读写锁
};
//*********************************************************************


class H264LiveVideoSource: public FramedSource {
public:
  static H264LiveVideoSource* createNew(UsageEnvironment& env,
                         frame_I* frame,
                         Boolean deleteBufferOnClose = True,
                         unsigned preferredFrameSize = 0,
                         unsigned playTimePerFrame = 0 ,int fd_pipe[2]=NULL);
      // "preferredFrameSize" == 0 means 'no preference'
      // "playTimePerFrame" is in microseconds

  u_int64_t bufferSize() const { return fBuffer->size; }

  void seekToByteAbsolute(u_int64_t byteNumber, u_int64_t numBytesToStream = 0);
    // if "numBytesToStream" is >0, then we limit the stream to that number of bytes, before treating it as EOF
  void seekToByteRelative(int64_t offset);

protected:
  H264LiveVideoSource(UsageEnvironment& env,
                   frame_I* frame,
                   Boolean deleteBufferOnClose,
                   unsigned preferredFrameSize,
                   unsigned playTimePerFrame ,int pipe[2]);
    // called only by createNew()

  virtual ~H264LiveVideoSource();

private:
  // redefined virtual functions:
  virtual void doGetNextFrame();

private:
  int fd[2];
  frame_I* fBuffer;
  Boolean fDeleteBufferOnClose;
  unsigned fPreferredFrameSize;
  unsigned fPlayTimePerFrame;
  unsigned fLastPlayTime;
  Boolean fLimitNumBytesToStream;
  u_int64_t fNumBytesToStream; // used iff "fLimitNumBytesToStream" is True
};


//////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////H264LiveVideoServerMediaSubsession//////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef H264_LIVE_VIDEO_STREAM_HH_
#include "H264VideoFileServerMediaSubsession.hh"
#define H264_LIVE_VIDEO_STREAM_HH_



class H264LiveVideoServerMediaSubsession: public OnDemandServerMediaSubsession{
public:
  static H264LiveVideoServerMediaSubsession*
                    createNew( UsageEnvironment& env, Boolean reuseFirstSource ,frame_I* frame, int fifo[2]);
  void checkForAuxSDPLine1();
  void afterPlayingDummy1();
private:
              H264LiveVideoServerMediaSubsession(
                    UsageEnvironment& env, Boolean reuseFirstSource ,
                    frame_I* frame, int fifo[2]);
  virtual ~H264LiveVideoServerMediaSubsession();

  void setDoneFlag() { fDoneFlag = ~0; }

private: // redefined virtual functions
  virtual FramedSource* createNewStreamSource(unsigned clientSessionId,
                          unsigned& estBitrate);
  virtual RTPSink* createNewRTPSink(Groupsock* rtpGroupsock,
                                    unsigned char rtpPayloadTypeIfDynamic,
                                    FramedSource* inputSource);
  virtual char const* getAuxSDPLine(RTPSink* rtpSink,
                    FramedSource* inputSource);

private:
  frame_I *pNal;
  int m_fifo[2];
  char* fAuxSDPLine;
  char fDoneFlag; // used when setting up "fAuxSDPLine"
  RTPSink* fDummyRTPSink; // ditto

};


#endif /* H264_LIVE_VIDEO_STREAM_HH_ */

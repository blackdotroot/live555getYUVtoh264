#include <GroupsockHelper.hh>
#include "H264_Live_Video_Stream.hh"
////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////// H264LiveVideoSource ///////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////
H264LiveVideoSource*
H264LiveVideoSource::createNew(UsageEnvironment& env,
                    frame_I* frame,
                    Boolean deleteBufferOnClose,
                    unsigned preferredFrameSize,
                    unsigned playTimePerFrame ,
                            int fd_pipe[2]) {

  if (frame == NULL) return NULL;

    H264LiveVideoSource* videosource = new H264LiveVideoSource(env,  
                    frame, deleteBufferOnClose, 
                    preferredFrameSize, playTimePerFrame ,fd_pipe);

  return videosource;

}

H264LiveVideoSource::H264LiveVideoSource(UsageEnvironment& env,
                               frame_I* frame,
                               Boolean deleteBufferOnClose,
                               unsigned preferredFrameSize,
                               unsigned playTimePerFrame,int fd_pipe[2])
  : FramedSource(env), fDeleteBufferOnClose(deleteBufferOnClose),
    fPreferredFrameSize(preferredFrameSize), fPlayTimePerFrame(playTimePerFrame), fLastPlayTime(0){

    fBuffer  = (struct frame_I*) malloc(sizeof(struct frame_I*)); 
     fBuffer=frame;
    fd[0] = fd_pipe[0];
    fd[1] = fd_pipe[1];

}

H264LiveVideoSource::~H264LiveVideoSource() {

  if (fDeleteBufferOnClose) delete[] fBuffer;
}

void H264LiveVideoSource::seekToByteAbsolute(u_int64_t byteNumber, u_int64_t numBytesToStream) {


}

void H264LiveVideoSource::seekToByteRelative(int64_t offset) {



}

void H264LiveVideoSource::doGetNextFrame() {


    int ss[2];

    read(fd[0],ss,8);//读管道，读出内容为当前缓冲区的有效长度

    //如果数据源不可用则返回
      if (0 ) {
        handleClosure(this);
        return;
      }
 pthread_rwlock_rdlock(&(fBuffer->rwlock));//获取读取锁
        fFrameSize = ss[0];

      if (fFrameSize > fMaxSize) {
        fNumTruncatedBytes = fFrameSize - fMaxSize;
        fFrameSize = fMaxSize;
      } else {
        fNumTruncatedBytes = 0;
      }

      memmove(fTo, fBuffer->data, fFrameSize);
    if(fNumTruncatedBytes > 0 && fNumTruncatedBytes <= fFrameSize)
     memmove(fTo, fBuffer->data + fFrameSize , fNumTruncatedBytes);


 pthread_rwlock_unlock(&(fBuffer->rwlock));//解锁

  if (fPlayTimePerFrame > 0 && fPreferredFrameSize > 0) {

    if (fPresentationTime.tv_sec == 0 && fPresentationTime.tv_usec == 0) {

      // This is the first frame, so use the current time:
      gettimeofday(&fPresentationTime, NULL);
    } else {

      // Increment by the play time of the previous data:
      unsigned uSeconds    = fPresentationTime.tv_usec + fLastPlayTime;
      fPresentationTime.tv_sec += uSeconds/1000000;
      fPresentationTime.tv_usec = uSeconds%1000000;

    }

    // Remember the play time of this data:
    fLastPlayTime = (fPlayTimePerFrame*fFrameSize)/fPreferredFrameSize;
    fDurationInMicroseconds = fLastPlayTime;
  } else {
    // We don't know a specific play time duration for this data,
    // so just record the current time as being the 'presentation time':
    gettimeofday(&fPresentationTime, NULL);
  }

  FramedSource::afterGetting(this);

}


//////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////H264LiveVideoServerMediaSubsession//////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////
H264LiveVideoServerMediaSubsession*
H264LiveVideoServerMediaSubsession::createNew(
        UsageEnvironment& env, Boolean reuseFirstSource ,
        frame_I* frame,int fifo[2]) {

    return new H264LiveVideoServerMediaSubsession(env, reuseFirstSource,frame,fifo);
}

static void checkForAuxSDPLine(void* clientData) {

    H264LiveVideoServerMediaSubsession* subsess = (H264LiveVideoServerMediaSubsession*)clientData;
  subsess->checkForAuxSDPLine1();
}


void H264LiveVideoServerMediaSubsession::checkForAuxSDPLine1() {
  char const* dasl;

  if (fAuxSDPLine != NULL) {
    // Signal the event loop that we're done:
    setDoneFlag();
  } else if (fDummyRTPSink != NULL && (dasl = fDummyRTPSink->auxSDPLine()) != NULL) {
    fAuxSDPLine = strDup(dasl);
    fDummyRTPSink = NULL;

    // Signal the event loop that we're done:
    setDoneFlag();
  } else {
    // try again after a brief delay:
    int uSecsToDelay = 100000; // 100 ms
    nextTask() = envir().taskScheduler().scheduleDelayedTask(uSecsToDelay,
                  (TaskFunc*)checkForAuxSDPLine, this);
  }
}


void H264LiveVideoServerMediaSubsession::afterPlayingDummy1() {

  // Unschedule any pending 'checking' task:
  envir().taskScheduler().unscheduleDelayedTask(nextTask());
  // Signal the event loop that we're done:
  setDoneFlag();
}


H264LiveVideoServerMediaSubsession::H264LiveVideoServerMediaSubsession(
        UsageEnvironment& env, Boolean reuseFirstSource ,
        frame_I* frame, int fifo[2]) :
    OnDemandServerMediaSubsession(env, reuseFirstSource),
    fAuxSDPLine(NULL), fDoneFlag(0), fDummyRTPSink(NULL) {

    pNal  = (struct frame_I*) malloc(sizeof(struct frame_I*)); 
    pNal = frame;
    m_fifo[0] = fifo[0];
    m_fifo[1] = fifo[1];

}


H264LiveVideoServerMediaSubsession::~H264LiveVideoServerMediaSubsession() {

    delete[] fAuxSDPLine;delete []pNal;
}


static void afterPlayingDummy(void* clientData) {

    H264LiveVideoServerMediaSubsession* subsess = (H264LiveVideoServerMediaSubsession*)clientData;
  subsess->afterPlayingDummy1();
}


char const* H264LiveVideoServerMediaSubsession::getAuxSDPLine(RTPSink* rtpSink, FramedSource* inputSource) {

  if (fAuxSDPLine != NULL) return fAuxSDPLine; // it's already been set up (for a previous client)

  if (fDummyRTPSink == NULL) { // we're not already setting it up for another, concurrent stream
    // Note: For H264 video files, the 'config' information ("profile-level-id" and "sprop-parameter-sets") isn't known
    // until we start reading the file.  This means that "rtpSink"s "auxSDPLine()" will be NULL initially,
    // and we need to start reading data from our file until this changes.
    fDummyRTPSink = rtpSink;

    // Start reading the file:
    fDummyRTPSink->startPlaying(*inputSource, afterPlayingDummy, this);

    // Check whether the sink's 'auxSDPLine()' is ready:
    checkForAuxSDPLine(this);
  }

  envir().taskScheduler().doEventLoop(&fDoneFlag);

  return fAuxSDPLine;
}

FramedSource* H264LiveVideoServerMediaSubsession::createNewStreamSource(
        unsigned /*clientSessionId*/, unsigned& estBitrate) {

    estBitrate = 500;
    H264LiveVideoSource *buffsource = H264LiveVideoSource::
            createNew(envir() ,pNal, false, 15000,40,m_fifo);

      if (buffsource == NULL) return NULL;

  FramedSource* videoES = buffsource;

  H264VideoStreamFramer* videoSource = H264VideoStreamFramer::createNew(envir(), videoES);

    return videoSource;

}

// 实例化SDP
RTPSink* H264LiveVideoServerMediaSubsession::createNewRTPSink(
        Groupsock* rtpGroupsock, unsigned char rtpPayloadTypeIfDynamic,
        FramedSource* /*inputSource*/) {

      return H264VideoRTPSink::createNew(envir(), rtpGroupsock, rtpPayloadTypeIfDynamic);

}

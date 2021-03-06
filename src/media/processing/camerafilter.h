#pragma once
#include "filter.h"

#include <QCamera>
#include <QVideoFrame>

class CameraFrameGrabber;

class CameraFilter : public Filter
{
  Q_OBJECT

public:
  CameraFilter(QString id, StatisticsInterface* stats);
  ~CameraFilter();

  // setup camera device
  virtual bool init();

  void uninit();

  virtual void start();

  // stop camera device and this filter.
  virtual void stop();

  virtual void updateSettings();

private slots:
  // qcamera calls this when frame available
  // copies the frame to buffer for further processing
  void handleFrame(const QVideoFrame &frame);

protected:

  void run();

  // this sends the video frame forward instead of handle frame, because
  // then we get the processing to a separate non GUI thread which
  // might be a problem with high resolutions.
  void process();

private:

  bool initialCameraSetup();

  // setup camera device. For some reason this has to be called from main thread
  bool cameraSetup();

  QCamera *camera_;
  CameraFrameGrabber *cameraFrameGrabber_;

  uint16_t framerate_;

  QMutex frameMutex_;
  std::deque<QVideoFrame> frames_;

  QString currentDeviceName_;
  int currentDeviceID_;
  QString currentInputFormat_;
  int currentResolutionID_;
  int currentFramerateID_;
};

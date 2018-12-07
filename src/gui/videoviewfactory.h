#pragma once

#include <stdint.h>

#include <map>

// handles creating and managing the view components.

class QWidget;
class VideoInterface;
class ConferenceView;

class VideoviewFactory
{
public:
  VideoviewFactory();

  // conferenceview is needed for connecting reattach signal, because I couldn't get the
  // the interface signal connected for some reason.
  void createWidget(uint32_t sessionID, QWidget* parent, ConferenceView* conf);

  // set self view which is part of the
  void setSelfview(VideoInterface* video, QWidget* view);

  // nullptr is for selfview
  QWidget* getView(uint32_t sessionID);

  VideoInterface* getVideo(uint32_t sessionID);

private:

  std::map<uint32_t, QWidget*> widgets_;
  std::map<uint32_t, VideoInterface*> videos_;

  bool opengl_;
};

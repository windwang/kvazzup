#include "globalsdpstate.h"
#include <QDateTime>

const uint16_t MIN_SIP_PORT   = 21500;
const uint16_t MAX_SIP_PORT   = 22000;

const uint16_t MAX_PORTS = 42;

GlobalSDPState::GlobalSDPState():
  localUsername_("")
{
  ice_ = std::make_unique<ICE>();
  parameters_.setPortRange(MIN_SIP_PORT, MAX_SIP_PORT, MAX_PORTS);
}

void GlobalSDPState::setLocalInfo(QString username)
{
  localUsername_ = username;
}

std::shared_ptr<SDPMessageInfo> GlobalSDPState::localSDPSuggestion(QHostAddress localAddress)
{
  qDebug() << "Getting local SDP suggestion";
  return generateSDP(localAddress);
}

std::shared_ptr<SDPMessageInfo> GlobalSDPState::generateSDP(QHostAddress localAddress)
{
  // TODO: This should ask media manager, what options it supports.
  qDebug() << "Generating new SDP message with our address as:" << localAddress;

  if(localAddress == QHostAddress::Null
     || localAddress == QHostAddress("0.0.0.0")
     || localUsername_ == "")
  {
    qWarning() << "WARNING: Necessary info not set for SDP generation:" << localAddress.toString()
               << localUsername_;
    return nullptr;
  }

  if(!parameters_.enoughFreePorts())
  {
    qDebug() << "Not enough free ports to create SDP";
    return nullptr;
  }

  // TODO: Get suitable SDP from media manager
  std::shared_ptr<SDPMessageInfo> newInfo = std::shared_ptr<SDPMessageInfo> (new SDPMessageInfo);
  newInfo->version = 0;
  newInfo->originator_username = localUsername_;
  newInfo->sess_id = QDateTime::currentMSecsSinceEpoch();
  newInfo->sess_v = QDateTime::currentMSecsSinceEpoch();
  newInfo->host_nettype = "IN";
  newInfo->host_address = localAddress.toString();
  if(localAddress.protocol() == QAbstractSocket::IPv6Protocol)
  {
    newInfo->host_addrtype = "IP6";
    newInfo->connection_addrtype = "IP6";
  }
  else
  {
    newInfo->host_addrtype = "IP4";
    newInfo->connection_addrtype = "IP4";
  }

  newInfo->sessionName = parameters_.sessionName();
  newInfo->sessionDescription = parameters_.sessionDescription();

  newInfo->connection_address = localAddress.toString();
  newInfo->connection_nettype = "IN";

  newInfo->timeDescriptions.push_back(TimeInfo{0,0, "", "", {}});

  MediaInfo audio;
  MediaInfo video;

  if(!generateAudioMedia(audio) || !generateVideoMedia(video))
  {
    return nullptr;
  }

  newInfo->media = {audio, video};
  newInfo->candidates = ice_->generateICECandidates();

  return newInfo;
}

bool GlobalSDPState::generateAudioMedia(MediaInfo &audio)
{
  // we ignore nettype, addrtype and address, because we use a global c=
  audio = {"audio", parameters_.nextAvailablePortPair(), "RTP/AVP", {},
           "", "", "", "", {},"", parameters_.audioCodecs(),{A_SENDRECV},{}};

  if(audio.receivePort == 0)
  {
    parameters_.makePortPairAvailable(audio.receivePort);
    qWarning() << "WARNING: Ran out of ports to assign to audio media in SDP. Should be checked earlier.";
    return false;
  }

  // add all the dynamic numbers first because we want to favor dynamic type codecs.
  for(RTPMap codec : audio.codecs)
  {
    audio.rtpNums.push_back(codec.rtpNum);
  }

  audio.rtpNums += parameters_.audioPayloadTypes();
  return true;
}

bool GlobalSDPState::generateVideoMedia(MediaInfo& video)
{
  // we ignore nettype, addrtype and address, because we use a global c=
  video = {"video", parameters_.nextAvailablePortPair(), "RTP/AVP", {},
           "", "", "", "", {},"", parameters_.videoCodecs(),{A_SENDRECV},{}};

  if(video.receivePort == 0)
  {
    parameters_.makePortPairAvailable(video.receivePort);
    qWarning() << "WARNING: Ran out of ports to assign to video media in SDP. Should be checked earlier.";
    return false;
  }

  for(RTPMap codec : video.codecs)
  {
    video.rtpNums.push_back(codec.rtpNum);
  }

  // just for completeness, we will probably never support any of the pre-set video types.
  video.rtpNums += parameters_.videoPayloadTypes();
  return true;
}

// returns nullptr if suitable could not be found
std::shared_ptr<SDPMessageInfo>
GlobalSDPState::localFinalSDP(SDPMessageInfo &remoteSDP, QHostAddress localAddress,
                              std::shared_ptr<SDPMessageInfo> localSuggestion, uint32_t sessionID)
{
  // check if suitable.
  if(!checkSDPOffer(remoteSDP))
  {
    qDebug() << "Incoming SDP did not have Opus and H265 in their offer.";
    return nullptr;
  }
  std::shared_ptr<SDPMessageInfo> sdp;

  // check if we have made a suggestion. If we have, modify that to accommodate their sdp
  // if we have not made a suggestion, then base our final SDP on their suggestion.
  if(localSuggestion == nullptr)
  {
    sdp = generateSDP(localAddress);
    sdp->sessionName = remoteSDP.sessionName;
  }
  else
  {
    // The suggestion is generated by us so its safe to use.
    qDebug() << "Using our previous suggestion.";
    sdp = localSuggestion;

    // spawn ICE controller/controllee threads and start the candidate 
    // exchange and nomination
    ice_->respondToNominations(sdp->candidates, remoteSDP.candidates, sessionID);

    // wait until the nomination has finished (or failed)
    //
    // The call won't start until ICE has finished its job
    if (!ice_->callerConnectionNominated(sessionID))
    {
      qDebug() << "ERROR: Failed to nominate ICE candidates!";
      return nullptr;
    }

    ICEMediaInfo nominated = ice_->getNominated(sessionID);

    // first is RTP, second is RTCP
    if (nominated.audio.first != nullptr && nominated.audio.second != nullptr)
    {
      setMediaPair(sdp->media[0],      nominated.audio.first->local);
      setMediaPair(remoteSDP.media[0], nominated.audio.first->remote);
    }

    if (nominated.video.first != nullptr && nominated.video.second != nullptr)
    {
      setMediaPair(sdp->media[1],      nominated.video.first->local);
      setMediaPair(remoteSDP.media[1], nominated.video.first->remote);
    }
  }

  sdp->sess_v = remoteSDP.sess_v + 1;

  return sdp;
}

bool GlobalSDPState::remoteFinalSDP(SDPMessageInfo &remoteInviteSDP, uint32_t sessionID)
{
  // this function blocks until a candidate is nominated or all candidates are considered
  // invalid in which case it returns false to indicate error
  if (!ice_->calleeConnectionNominated(sessionID))
  {
    qDebug() << "ERROR: Failed to nominate ICE candidates!";
    return false;
  }

  return checkSDPOffer(remoteInviteSDP);
}

bool GlobalSDPState::checkSDPOffer(SDPMessageInfo &offer)
{
  // TODO: check everything.

  bool hasOpus = false;
  bool hasH265 = false;

  if(offer.connection_address == "0.0.0.0")
  {
    qDebug() << "Got bad global address from SDP:" << offer.connection_address;
    return false;
  }

  if(offer.version != 0)
  {
    qDebug() << "Their offer had not 0 version:" << offer.version;
    return false;
  }

  for(MediaInfo media : offer.media)
  {
    for(RTPMap rtp : media.codecs)
    {
      if(rtp.codec == "opus")
      {
        qDebug() << "Found Opus";
        hasOpus = true;
      }
      else if(rtp.codec == "h265")
      {
        qDebug() << "Found H265";
        hasH265 = true;
      }
    }
  }

  return hasOpus && hasH265;
}

// frees the ports when they are not needed in rest of the program
void GlobalSDPState::endSession(std::shared_ptr<SDPMessageInfo> sessionSDP)
{
  if(sessionSDP == nullptr)
  {
    return;
  }
  for(auto mediaStream : sessionSDP->media)
  {
    parameters_.makePortPairAvailable(mediaStream.receivePort);
  }
}

void GlobalSDPState::startICECandidateNegotiation(QList<std::shared_ptr<ICEInfo>>& local, QList<std::shared_ptr<ICEInfo>>& remote, uint32_t sessionID)
{
  ice_->startNomination(local, remote, sessionID);
}

void GlobalSDPState::setMediaPair(MediaInfo& media, std::shared_ptr<ICEInfo> mediaInfo)
{
  if (mediaInfo == nullptr)
  {
    return;
  }

  media.connection_address = mediaInfo->address;
  media.receivePort        = mediaInfo->port;
}

void GlobalSDPState::updateFinalSDPs(SDPMessageInfo& localSDP, SDPMessageInfo& remoteSDP, uint32_t sessionID)
{
  ICEMediaInfo nominated = ice_->getNominated(sessionID);

  // first is RTP, second is RTCP
  if (nominated.audio.first != nullptr && nominated.audio.second != nullptr)
  {
    setMediaPair(localSDP.media[0],  nominated.audio.first->local);
    setMediaPair(remoteSDP.media[0], nominated.audio.first->remote);
  }

  if (nominated.video.first != nullptr && nominated.video.second != nullptr)
  {
    setMediaPair(localSDP.media[1],  nominated.video.first->local);
    setMediaPair(remoteSDP.media[1], nominated.video.first->remote);
  }
}

void GlobalSDPState::ICECleanup(uint32_t sessionID)
{
  ice_->cleanupSession(sessionID);
}

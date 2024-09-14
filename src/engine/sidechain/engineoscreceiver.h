//#ifndef EngineOscReceiver_H
//#ifndef EngineOscReceiver_H

#include "control/controlobject.h"
#include "control/controlproxy.h"
#include "engine/sidechain/sidechainworker.h"
//#include "lo/lo.h"
#include "preferences/usersettings.h"
#include "track/track.h"
#include <QList>
#include <QTime>

class ConfigKey;
class Encoder;

//class EngineOscReceiver : public QObject, public SideChainWorker {
class EngineOscReceiver{
  Q_OBJECT
public:
  EngineOscReceiver(UserSettingsPointer &pConfig);
  virtual ~EngineOscReceiver();

public slots:
//  void sendState();
//  void maybeSendState();
//  void connectServer();
  void sendStart();

  // interface SideChainWorker
  //void process(const CSAMPLE *pBuffer, const int iBufferSize);
  //void shutdown() {}

private:
//  QTime m_time;
//  lo_address m_serverAddress;
  UserSettingsPointer m_pConfig;
//  QList<ControlProxy *> m_connectedControls;
//  ControlProxy m_prefUpdate;
};

//void sendStart();
//#endif

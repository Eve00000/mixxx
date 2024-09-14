#ifndef INCLUDED_OSCRECEIVETEST_H
#define INCLUDED_OSCRECEIVETEST_H

//#include "preferences/colorpalettesettings.h"
#include "preferences/usersettings.h"
#include "control/pollingcontrolproxy.h"
#include "util/class.h"
#include "control/controlproxy.h"

#include <QThread>
#include <QSharedPointer>
#include <memory>

//class ConfigKey;
//UserSettingsPointer m_pConfig;

class oscResult {
    public:
        QString oscAddress;
        QString oscGroup;
        QString oscKey;
        float oscValue;
};

class OscReceiveTest : public QObject {
//    Q_OBJECT
  public:

    OscReceiveTest(QObject* pParent,
            UserSettingsPointer pConfig);

//    int OscReceiveTestMain();

  private slots:
    void RunReceiveTest(int oscportin);
     
    
  protected:

  private:
    UserSettingsPointer m_pConfig;

//    ControlProxy* m_pCOCrossfader;
//    ControlProxy* m_pCOCrossfaderReverse;

};

#endif /* INCLUDED_OSCSENDTESTS_H */



#include "engine/sidechain/engineoscreceiver.h"

#include "control/controlobject.h"
#include "control/controlproxy.h"
//#include "oscclient/defs_oscclient.h"
#include "preferences/usersettings.h"

#include "errordialoghandler.h"
#include "mixer/playerinfo.h"
#include "util/event.h"

#include "mixer/playermanager.h"
#include <QDebug>

//EngineOscReceiver::EngineOscReceiver(UserSettingsPointer &pConfig)
//    : m_pConfig(pConfig), m_prefUpdate(ConfigKey("[Preferences]", "updated")) {

//    EngineOscReceiver::EngineOscReceiver() {
//EngineOscReceiver::EngineOscReceiver(UserSettingsPointer &pConfig)
//    : m_pConfig(pConfig), m_prefUpdate(ConfigKey("[Preferences]", "updated")) {
EngineOscReceiver::EngineOscReceiver(UserSettingsPointer& pConfig)
        : m_pConfig(pConfig) {
    sendStart();
}

EngineOscReceiver::~EngineOscReceiver() {
}


void EngineOscReceiver::sendStart() {
//    void sendStart() {

//    QString MixxxOSCStatusFilePath = m_pConfig->getSettingsPath();
    QString MixxxOSCStatusFileLocation = MixxxOSCStatusFilePath + "/MixxxOSCStatus.txt";
    QFile MixxxOSCStatusFile(MixxxOSCStatusFileLocation);
//    MixxxOSCStatusFile.remove();
    MixxxOSCStatusFile.open(QIODevice::ReadWrite | QIODevice::Append);
    QTextStream MixxxOSCStatusTxt(&MixxxOSCStatusFile);
    MixxxOSCStatusTxt << QString("ENGINEOSCRECEIVER------------------------") << "\n";

//    if (m_pConfig->getValue<bool>(ConfigKey("[OSC]", "OscEnabled"))) {
//        QString CKOscPortOut = m_pConfig->getValue(ConfigKey("[OSC]", "OscPortOut"));
//        QString CKOscPortIn = m_pConfig->getValue(ConfigKey("[OSC]", "OscPortIn"));
        //        QString CKOscBuffer = m_pCoreServices->getSettings()->getValue(ConfigKey("[OSC]", "OscOutputBufferSize"));
        //        QString CKOscMtuSize = m_pCoreServices->getSettings()->getValue(ConfigKey("[OSC]", "OscIpMtuSize"));
//        QString CKOscClient1Ip = m_pConfig->getValue(ConfigKey("[OSC]", "OscReceiver1Ip"));

//        int CKOscPortOutInt = CKOscPortOut.toInt();
//        int CKOscPortInInt = CKOscPortIn.toInt();
        //        int CKOscBufferInt = CKOscBuffer.toInt();
        //        int CKOscMtuSizeInt = CKOscMtuSize.toInt();
        //        const int CKOscMtuSizeIntConst = CKOscMtuSizeInt;

//        QByteArray CKOscClient1Ipba = CKOscClient1Ip.toLocal8Bit();
//        const char* CKOscClient1IpChar = CKOscClient1Ipba.data();
        //       const char* CKOscClient1IpChar = CKOscClient1Ip.toLocal8Bit().data();

        MixxxOSCStatusTxt << QString("OSC enabled") << "\n";
//        MixxxOSCStatusTxt << QString("portin : %1").arg(CKOscPortInInt) << "\n";
//        MixxxOSCStatusTxt << QString("portout : %1").arg(CKOscPortOutInt) << "\n";
//        MixxxOSCStatusTxt << QString("client ipaddress : %1").arg(CKOscClient1IpChar) << "\n";
        MixxxOSCStatusTxt << QString("ENGINEOSCRECEIVER------------------------") << "\n";
        MixxxOSCStatusFile.close();

//        char buffer[IP_MTU_SIZE];
//        osc::OutboundPacketStream p(buffer, IP_MTU_SIZE);
        //        UdpTransmitSocket transmitSocket(IpEndpointName("192.168.0.125", CKOscPortOutInt));
//        UdpTransmitSocket transmitSocket(IpEndpointName(CKOscClient1IpChar, CKOscPortOutInt));

//        p.Clear();
//        p << osc::BeginBundle();
//        p << osc::BeginMessage("/Open") << "Start" << osc::EndMessage;
//        p << osc::EndBundle;
//        transmitSocket.Send(p.Data(), p.Size());
    }

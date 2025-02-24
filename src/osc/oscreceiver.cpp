#include "oscreceiver.h"

#include <QThread>
// #include <QDebug>
// #include <cstdlib>
// #include <cstring>
#include <stdio.h>

#include <iostream>

#ifndef WIN32
#include <sys/time.h>
#include <unistd.h>
#endif
#include <signal.h>
// #include <lo/lo.h>
//  #include <lo/lo_cpp.h>

#include "control/controlobject.h"
#include "control/controlproxy.h"
#include "control/pollingcontrolproxy.h"
#include "moc_oscreceiver.cpp"
#include "oscfunctions.h"

std::atomic<bool> s_oscEnabled(false);
int s_ckOscPortOutInt = 0;
QList<std::pair<bool, QString>> s_receiverConfigs;
static std::atomic<bool> s_configLoaded1stTimeFromFile(false);
static bool s_oscSendSyncTriggers(false);
static int s_oscSendSyncTriggersInterval;
static int s_lastCheckStamp;
inline std::atomic<qint64> s_lastTriggerTime = 0;

// constexpr int kMaxOscTracks = 70;
// extern std::array<std::tuple<QString, QString, QString>, kMaxOscTracks> g_oscTrackTable;
// extern QMutex g_oscTrackTableMutex;

// std::array<std::tuple<QString, QString, QString>, kMaxOscTracks> g_oscTrackTable;
// extern QMutex g_oscTrackTableMutex;

namespace {
// const bool sDebug = true;
} // namespace

void oscFunctionsSendPtrType(
        // UserSettingsPointer pConfig,
        const QString& oscGroup,
        const QString& oscKey,
        enum DefOscBodyType oscBodyType,
        const QString& oscMessageBodyQString,
        int oscMessageBodyInt,
        double oscMessageBodyDouble,
        float oscMessageBodyFloat);

OscReceiver::OscReceiver(
        UserSettingsPointer pConfig,
        QObject* parent)
        : m_pConfig(pConfig),
          m_stopFlag(false),
          m_lastCheckStamp(0) {};

OscReceiver::~OscReceiver() {
    stop();
}

static void errorCallback(int num, const char* msg, const char* where) {
    qWarning() << "[OSC] [OSCRECEIVER] -> Error" << num << "at" << where << ":" << msg;
}

static int quit_handler(const char* path,
        const char* types,
        lo_arg** argv,
        int argc,
        lo_message data,
        void* user_data) {
    qDebug() << "[OSC] [OSCRECEIVER] -> Quitting";
    return 0;
}

void OscReceiver::stop() {
    qDebug() << "[OSC] [OSCRECEIVER] -> Stop requested";
    // Set a flag to stop the receiver loop
    m_stopFlag = true;

    if (QThread::currentThread()->isInterruptionRequested()) {
        qDebug() << "[OSC] [OSCRECEIVER] -> Thread requested to stop.";
    }
    qDebug() << "[OSC] [OSCRECEIVER] -> OSC server stopped";
}

static int messageCallback(const char* path,
        const char* types,
        lo_arg** argv,
        int argc,
        lo_message data,
        void* user_data) {
    Q_UNUSED(types);
    auto* worker = static_cast<OscReceiver*>(user_data);
    if (!worker) {
        qWarning() << "[OSC] [OSCRECEIVER] -> OSC message callback: Invalid user_data!";
        return 0;
    }
    if (argc < 1) {
        qWarning() << "[OSC] [OSCRECEIVER] -> Received OSC message on" << path
                   << "with no arguments";
        return 1;
    }

    OscResult oscIn;
    oscIn.oscAddress = QString::fromUtf8(path);
    // oscIn.oscAddressURL = QString("osc://localhost%1").arg(path);
    oscIn.oscAddressURL = QString("%1").arg(path);
    oscIn.oscValue = argv[0]->f; // Assuming first argument is a float

    qDebug() << "[OSC] [OSCRECEIVER] -> Received OSC message:"
             << oscIn.oscAddress << "Value:" << oscIn.oscValue;
    // oscIn.oscAddress.replace("/", "").replace("(", "[").replace(")", "]");
    if (sDebug) {
        // qDebug() << "[OSC] [OSCRECEIVER] -> Before translation " << oscIn.oscAddressURL;
    }
    if (s_oscSendSyncTriggers) {
        worker->sendOscSyncTriggers();
    }
    worker->determineOscAction(oscIn);
    return 0;
}

void OscReceiver::determineOscAction(OscResult& oscIn) {
    // if (s_oscSendSyncTriggers) {
    //     sendOscSyncTriggers();
    // }
    bool oscGetP = oscIn.oscAddressURL.startsWith("/Get/cop", Qt::CaseInsensitive);
    bool oscGetV = oscIn.oscAddressURL.startsWith("/Get/cov", Qt::CaseInsensitive);
    bool oscGetT = oscIn.oscAddressURL.startsWith("/Get/cot", Qt::CaseInsensitive);
    bool oscSet = !(oscGetP || oscGetV || oscGetT);

    oscIn.oscAddressURL.replace("/Get/cop", "").replace("/Get/cov", "").replace("/Get/cot", "");

    oscIn.oscAddress = translatePath(oscIn.oscAddressURL);
    if (sDebug) {
        qDebug() << "[OSC] [OSCRECEIVER] -> Before translation " << oscIn.oscAddressURL;
        qDebug() << "[OSC] [OSCRECEIVER] -> After translation " << oscIn.oscAddress;
        qDebug() << "[OSC] [OSCRECEIVER] -> oscGetP " << oscGetP;
        qDebug() << "[OSC] [OSCRECEIVER] -> oscGetV " << oscGetV;
        qDebug() << "[OSC] [OSCRECEIVER] -> oscGetT " << oscGetT;
        qDebug() << "[OSC] [OSCRECEIVER] -> oscSet " << oscSet;
    }
    // int posDel = oscIn.oscAddress.indexOf("@", 0, Qt::CaseInsensitive);
    int posDel = oscIn.oscAddress.indexOf(",", 0, Qt::CaseInsensitive);
    if (posDel > 0) {
        if (oscSet) {
            // translatePath(oscIn.oscAddress);
            oscIn.oscGroup = oscIn.oscAddress.mid(0, posDel);
            oscIn.oscKey = oscIn.oscAddress.mid(posDel + 1, oscIn.oscAddress.length());

        } else {
            // oscIn.oscGroup = oscIn.oscAddress.mid(5, posDel - 5);
            // oscIn.oscKey = oscIn.oscAddress.mid(posDel + 1);
            oscIn.oscGroup = oscIn.oscAddress.mid(0, posDel);
            oscIn.oscKey = oscIn.oscAddress.mid(posDel + 1);
        }

        if (oscGetP) {
            doGetP(oscIn);
        } else if (oscGetV) {
            doGetV(oscIn);
        } else if (oscGetT) {
            doGetT(oscIn);
        } else if (oscSet) {
            doSet(oscIn, oscIn.oscValue);
        }
    }
}

// OSC wants info from Mixxx -> Parameter
void OscReceiver::doGetP(OscResult& oscIn) {
    if (sDebug) {
        qDebug() << "[OSC] [OSCRECEIVER] -> doGetP triggered oscIn.oscGroup" << oscIn.oscGroup
                 << " oscIn.oscKey " << oscIn.oscKey;
    }
    if (ControlObject::exists(ConfigKey(oscIn.oscGroup, oscIn.oscKey))) {
        auto proxy = std::make_unique<PollingControlProxy>(oscIn.oscGroup, oscIn.oscKey);
        // for future use when prefix /cop is introduced in osc-messages
        // oscIn.oscGroup = QString("%1%2").arg("/cop", oscIn.oscGroup);
        oscFunctionsSendPtrType(
                //            m_pConfig,
                oscIn.oscGroup,
                oscIn.oscKey,
                DefOscBodyType::FLOATBODY,
                // DefOscBodyType::DOUBLEBODY,
                "",
                0,
                0,
                static_cast<float>(proxy->getParameter()));
        // proxy->getParameter());
        if (sDebug) {
            qDebug() << "[OSC] [OSCRECEIVER] -> Msg Snd: Group, Key: Value:" << oscIn.oscGroup
                     << "," << oscIn.oscKey << ":" << proxy->getParameter();
        }
    }
}

// OSC wants info from Mixxx -> Value
void OscReceiver::doGetV(OscResult& oscIn) {
    if (sDebug) {
        qDebug() << "[OSC] [OSCRECEIVER] -> doGetV triggered oscIn.oscGroup" << oscIn.oscGroup
                 << " oscIn.oscKey " << oscIn.oscKey;
    }
    if (ControlObject::exists(ConfigKey(oscIn.oscGroup, oscIn.oscKey))) {
        auto proxy = std::make_unique<PollingControlProxy>(oscIn.oscGroup, oscIn.oscKey);
        // for future use when prefix /cop is introduced in osc-messages
        // oscIn.oscGroup = QString("%1%2").arg("/cov", oscIn.oscGroup);
        oscFunctionsSendPtrType(
                // m_pConfig,
                oscIn.oscGroup,
                oscIn.oscKey,
                DefOscBodyType::FLOATBODY,
                "",
                0,
                0,
                static_cast<float>(proxy->get()));
        // static_cast<float>(ControlObject::getControl(
        //         oscIn.oscGroup, oscIn.oscKey)
        //                 ->get()));
        if (sDebug) {
            qDebug() << "[OSC] [OSCRECEIVER] -> Msg Rcvd: Get Group, Key: Value:" << oscIn.oscGroup
                     << "," << oscIn.oscKey << ":" << oscIn.oscValue;
        }
    }
}

void OscReceiver::doGetT(OscResult& oscIn) {
    if (sDebug) {
        qDebug() << "[OSC] [OSCRECEIVER] -> doGetT triggered oscIn.oscGroup" << oscIn.oscGroup
                 << " oscIn.oscKey " << oscIn.oscKey;
    }

    QString searchOscKey = QString(oscIn.oscGroup + oscIn.oscKey);
    if (sDebug) {
        qDebug() << "[OSC] [OSCRECEIVER] -> Msg Rcvd: Get Group, TrackInfo: " << oscIn.oscGroup
                 << "," << oscIn.oscKey;
    }

    // Lock the mutex to protect access to m_pConfig
    // QMutexLocker<QMutex> locker(&m_mutex);

    // Read the value from m_pConfig
    //    const QString sendOscValue = m_pConfig->getValue(ConfigKey("[OSC]", searchOscKey));

    QString sendOscValue = getTrackInfo(oscIn.oscGroup, oscIn.oscKey);

    // QString sendOscValue;

    //{
    //    QMutexLocker locker(&g_oscTrackTableMutex); // Lock mutex
    //    for (const auto& entry : g_oscTrackTable) {
    //        if (std::get<0>(entry) == oscIn.oscGroup) { // Find matching group
    //            if (oscIn.oscKey == "TrackArtist") {
    //                sendOscValue = std::get<1>(entry);
    //            } else if (oscIn.oscKey == "TrackTitle") {
    //                sendOscValue = std::get<2>(entry);
    //            }
    //            break;
    //        }
    //    }
    //}
    // if (sendOscValue.isEmpty()) {
    //    sendOscValue = "Unknown"; // Default if not found
    //}

    // Send the OSC message using the stored value
    oscFunctionsSendPtrType(
            // m_pConfig,
            oscIn.oscGroup,
            oscIn.oscKey,
            DefOscBodyType::STRINGBODY,
            escapeStringToJsonUnicode(sendOscValue),
            0,
            0,
            0);

    if (sDebug) {
        qDebug() << "[OSC] [OSCRECEIVER] -> Msg Rcvd: Get TrackInfo, Key: Value:" << oscIn.oscGroup
                 << "," << oscIn.oscKey << ":" << sendOscValue;
    }
}

// test readlocjer
// void OscReceiver::doGetT(OscResult& oscIn) {
//    if (sDebug) {
//        qDebug() << "[OSC] [OSCRECEIVER] -> doGetT triggered oscIn.oscGroup"
//        << oscIn.oscGroup
//                 << " oscIn.oscKey " << oscIn.oscKey;
//    }
//    QString searchOscKey = QString(oscIn.oscGroup + oscIn.oscKey);
//    if (sDebug) {
//        qDebug() << "[OSC] [OSCRECEIVER] -> Msg Rcvd: Get Group, TrackInfo: "
//        << oscIn.oscGroup
//                 << "," << oscIn.oscKey;
//    }
//    // Lock the read-write lock for reading
//    QReadLocker locker(&m_configLock);
//
//    // Read the value from m_pConfig
//    const QString sendOscValue = m_pConfig->getValue(ConfigKey("[OSC]",
//    searchOscKey));
//
//    // Unlock the read-write lock immediately after reading
//    locker.unlock();
//
//    // Send the OSC message using the stored value
//    oscFunctionsSendPtrType(m_pConfig,
//            oscIn.oscGroup,
//            oscIn.oscKey,
//            DefOscBodyType::STRINGBODY,
//            escapeStringToJsonUnicode(sendOscValue),
//            0,
//            0,
//            0);
//
//    if (sDebug) {
//        qDebug() << "[OSC] [OSCRECEIVER] -> Msg Rcvd: Get TrackInfo, Key:
//        Value:" << oscIn.oscGroup
//                 << "," << oscIn.oscKey << ":" << sendOscValue;
//    }
//}

// void OscReceiver::doGetT(OscResult& oscIn) {
//     if (sDebug) {
//         qDebug() << "[OSC] [OSCRECEIVER] -> doGetT triggered oscIn.oscGroup"
//         << oscIn.oscGroup
//                  << " oscIn.oscKey " << oscIn.oscKey;
//     }
//     QString searchOscKey = QString(oscIn.oscGroup + oscIn.oscKey);
//     if (sDebug) {
//         qDebug() << "[OSC] [OSCRECEIVER] -> Msg Rcvd: Get Group, TrackInfo: "
//         << oscIn.oscGroup
//                  << "," << oscIn.oscKey;
//     }
//     // if (!m_pConfig->getValue(ConfigKey("[OSC]", searchOscKey)) {
//     const QString& sendOscValue = m_pConfig->getValue(ConfigKey("[OSC]",
//     searchOscKey));
//     // for future use when prefix /cop is introduced in osc-messages
//     // oscIn.oscGroup = QString("%1%2").arg("/cot", oscIn.oscGroup);
//      oscFunctionsSendPtrType(m_pConfig,
//            oscIn.oscGroup,
//            oscIn.oscKey,
//            DefOscBodyType::STRINGBODY,
//            escapeStringToJsonUnicode(sendOscValue),
//            0,
//            0,
//            0);
//     //}
//     if (sDebug) {
//         qDebug() << "[OSC] [OSCRECEIVER] -> Msg Rcvd: Get TrackInfo, Key:
//         Value:" << oscIn.oscGroup
//                  << "," << oscIn.oscKey << ":" << sendOscValue;
//     }
// }

// Input from OSC -> Changes in Mixxx
void OscReceiver::doSet(OscResult& oscIn, float value) {
    if (sDebug) {
        qDebug() << "[OSC] [OSCRECEIVER] -> doSet triggered oscIn.oscGroup" << oscIn.oscGroup
                 << " oscIn.oscKey " << oscIn.oscKey;
    }
    if (ControlObject::exists(ConfigKey(oscIn.oscGroup, oscIn.oscKey))) {
        auto proxy = std::make_unique<PollingControlProxy>(oscIn.oscGroup, oscIn.oscKey);
        // proxy->set(value);
        proxy->setParameter(value);
        oscFunctionsSendPtrType(
                //            m_pConfig,
                oscIn.oscGroup,
                oscIn.oscKey,
                DefOscBodyType::FLOATBODY,
                "",
                0,
                0,
                value);
        if (sDebug) {
            qDebug() << "[OSC] [OSCRECEIVER] -> Msg Rcvd: Group, Key: Value:" << oscIn.oscGroup
                     << "," << oscIn.oscKey << ":" << value;
        }
    } else {
        if (sDebug) {
            qDebug() << "[OSC] [OSCRECEIVER] -> Msg Rcvd for non-existing Control Object: Group, "
                        "Key: Value:"
                     << oscIn.oscGroup << "," << oscIn.oscKey << ":" << value;
        }
    }
}

// trigger OSC to sync

void OscReceiver::sendOscSyncTriggers() {
    if (sDebug) {
        qDebug() << "[OSC] [OSCRECEIVER] -> Mixxx OSC SendSyncTrigger";
    }

    // Get current timestamp in milliseconds
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();

    // Check if enough time has passed since the last trigger
    if (currentTime - s_lastTriggerTime >= s_oscSendSyncTriggersInterval) {
        // Execute the OSC send function
        oscFunctionsSendPtrType(
                "[Osc]",
                "OscSync",
                DefOscBodyType::FLOATBODY,
                "",
                0,
                0,
                1);

        // Update the last trigger timestamp
        s_lastTriggerTime = currentTime;

        if (sDebug) {
            qDebug() << "[OSC] [OSCRECEIVER] -> Mixxx OSC SENT SendSyncTrigger at" << currentTime;
        }
    } else {
        if (sDebug) {
            qDebug() << "[OSC] [OSCRECEIVER] -> Mixxx OSC NO SendSyncTrigger SENT. "
                     << "Last trigger:" << s_lastTriggerTime
                     << " | Current time:" << currentTime
                     << " | Interval required:" << s_oscSendSyncTriggersInterval
                     << " | Time since last trigger:" << (currentTime - s_lastTriggerTime);
        }
    }
}

// void OscReceiver::sendOscSyncTriggers() {
//     if (sDebug) {
//         qDebug() << "[OSC] [OSCRECEIVER] -> Mixxx OSC SendSyncTrigger";
//     }
//     //int interval = m_pConfig
//     //                       ->getValue(ConfigKey(
//     //                               "[OSC]", "OscSendSyncTriggersInterval"))
//     //                       .toInt() /
//     //        1000;
//     int interval = s_oscSendSyncTriggersInterval/1000;
//
//     int checkStamp = QDateTime::currentDateTime().toString("ss").toInt();
//     if (sDebug) {
//         qDebug() << "[OSC] [OSCRECEIVER] -> Mixxx OSC SendSyncTrigger check:
//         "
//                     "checkStamp:"
//                  << checkStamp
//                  << " - lastCheckStamp: "
//                  << s_lastCheckStamp
//                  << " - interval: "
//                  << interval;
//     }
//
//     //if ((checkStamp > m_lastCheckStamp + 4) && (checkStamp % interval >=
//     0)) { if ((checkStamp > s_lastCheckStamp + 4) && (checkStamp % interval
//     >= 0)) {
//
//     //if (checkStamp % interval >= 0) {
//         oscFunctionsSendPtrType(
//             //m_pConfig,
//                 "[Osc]",
//                 "OscSync",
//                 DefOscBodyType::FLOATBODY,
//                 "",
//                 0,
//                 0,
//                 1);
//         if (sDebug) {
//             qDebug() << "[OSC] [OSCRECEIVER] -> Mixxx OSC SENT
//             SendSyncTrigger";
//         }
//     } else {
//         if (sDebug) {
//             qDebug() << "[OSC] [OSCRECEIVER] -> Mixxx OSC NO SendSyncTrigger
//             SENT: "
//                         "checkStamp:"
//                      << checkStamp;
//         }
//     }
//     if (checkStamp > 54) {
//         s_lastCheckStamp = 0;
//     } else {
//         s_lastCheckStamp = checkStamp;
//     }
//
//     //QMutexLocker locker(&m_mutex);
//     // m_lastResponseTime = QDateTime::currentDateTime();
//     //qDebug() << "[OSC] [OSCRECEIVER] -> Processing OSC message:" <<
//     oscIn.oscAddress << "Value:" << oscIn.oscValue;
//
// }

// with QT Event Loop
// void OscReceiver::startOscReceiver(int oscPortin) {
//    std::string portStr = std::to_string(oscPortin);
//
//    // Create a new OSC server (not a server thread)
//    lo_server server = lo_server_new_with_proto(portStr.c_str(), LO_UDP,
//    errorCallback); if (!server) {
//        qWarning() << "[OSC] [OSCRECEIVER] -> Failed to create OSC server.";
//        return;
//    }
//
//    // Add methods to the server
//    lo_server_add_method(server, "/quit", "", quit_handler, nullptr);
//    lo_server_add_method(server, nullptr, nullptr, messageCallback, this); //
//    Pass `this` as user_data
//
//    qDebug() << "[OSC] Receiver started and awaiting messages...";
//
//    // Use a QTimer to periodically process OSC messages
//    QTimer* timer = new QTimer(this);
//    connect(timer, &QTimer::timeout, this, [server]() {
//        // Process OSC messages (non-blocking)
//        lo_server_recv_noblock(server, 0);
//    });
//    timer->start(100); // Process messages every 100ms
//}

// liblo without own thread
// int OscReceiver::startOscReceiver(int oscPortin, UserSettingsPointer m_pConfig) {
// int OscReceiver::startOscReceiver(int oscPortin) {
//    std::string portStr = std::to_string(oscPortin);
//
//    // Create a new OSC server (not a server thread)
//    lo_server server = lo_server_new_with_proto(portStr.c_str(), LO_UDP, errorCallback);
//    if (!server) {
//        qWarning() << "[OSC] [OSCRECEIVER] -> Failed to create OSC server.";
//        return -1;
//    }
//    // Add methods to the server
//    lo_server_add_method(server, "/quit", "", quit_handler, nullptr);
//    lo_server_add_method(server,
//            nullptr,
//            nullptr,
//            messageCallback,
//            this); // Pass `this` as user_data
//    qDebug() << "[OSC] Receiver started and awaiting messages...";
//    // Main loop to process OSC messages
//    while (!m_stopFlag) {
//        // Process OSC messages (non-blocking)
//        int timeout_ms = 100; // Wait for 100ms for incoming messages
//        lo_server_recv_noblock(server, timeout_ms);
//    }
//    // Stop the server when done
//    lo_server_free(server);
//
//    qDebug() << "[OSC] [OSCRECEIVER] -> Receiver stopped";
//}

// liblo in own thread
// int OscReceiver::startOscReceiver(int oscPortin, UserSettingsPointer m_pConfig) {
int OscReceiver::startOscReceiver(int oscPortin) {
    m_oscPortIn = oscPortin;
    std::string portStr = std::to_string(oscPortin);
    lo_server_thread st = lo_server_thread_new_with_proto(portStr.c_str(), LO_UDP, errorCallback);
    lo_server s = lo_server_thread_get_server(st);
    lo_server_thread_add_method(st, "/quit", "", quit_handler, NULL);
    lo_server_thread_add_method(st, NULL, NULL, messageCallback, s);
    lo_server_thread_start(st);
    // lo_address a = 0;
    // a = lo_address_new_with_proto(LO_UDP, "192.168.0.125", "9000");
    // if (!a) {
    //     qDebug() << "EVE -> LIBLO -> Error creating destination address.\n";
    //     exit(1);
    // }
    // qDebug() << "EVE -> LIBLO -> Sending message to " << a;
    // int r = lo_send_from(a, s, LO_TT_IMMEDIATE, "/test", "ifs", 1, 2.0f, "3");
    // if (r < 0)
    //     qDebug() << "EVE -> LIBLO -> Error sending initial message.\n";
    qDebug() << "[OSC] Receiver started and awaiting messages...";

    while (!m_stopFlag) {
        QThread::msleep(100); // Sleep for a short period
    }

    // while (true) {
    //     // mutexlocker added for safe adding flag
    //     QMutexLocker locker(&m_mutex);
    //     if (m_stopFlag) {
    //         break; // If the stop flag is set, exit the loop
    //     }

    //    // Sleep for a short period before checking the flag again
    //    QThread::msleep(100);
    //}

    // Stop the server when done
    lo_server_thread_stop(st);
    lo_server_thread_free(st);

    qDebug() << "[OSC] [OSCRECEIVER] -> Receiver stopped";

    return 0;
}

void OscReceiver::checkResponsiveness() {
    // QMutexLocker locker(&m_mutex);

    // Check if the last response was more than 5 seconds ago
    if (m_lastResponseTime.secsTo(QDateTime::currentDateTime()) > 5) {
        qWarning() << "[OSC] [OSCRECEIVER] -> OSC server is unresponsive. Restarting...";
        restartOscReceiver(m_oscPortIn); // Restart the OSC server
    }
}

void OscReceiver::restartOscReceiver(int oscPortin) {
    // Stop the current OSC server
    stop();

    // Wait for the server to stop
    QThread::msleep(100);

    // Reset the stop flag
    m_stopFlag = false;

    // Restart the OSC server
    startOscReceiver(oscPortin);
}

void OscReceiver::oscReceiverMain(UserSettingsPointer pConfig) {
    if (pConfig->getValue<bool>(ConfigKey("[OSC]", "OscEnabled"))) {
        if (sDebug) {
            qDebug() << "[OSC] [OSCRECEIVER] -> Enabled -> Started";
        }
        loadOscConfiguration(pConfig);
        for (const auto& receiver : s_receiverConfigs) {
            int i = 1;
            if (receiver.first) { // Check if the receiver is active
                QByteArray receiverIpBa = receiver.second.toLocal8Bit();
                if (sDebug) {
                    qDebug() << QString(
                            "[OSC] [OSCRECEIVER] -> Mixxx OSC Receiver %1 with "
                            "ip-address: %2 Activated")
                                        .arg(i)
                                        .arg(receiverIpBa);
                }
            } else {
                if (sDebug) {
                    qDebug()
                            << QString("[OSC] [OSCRECEIVER] -> Mixxx OSC "
                                       "Receiver %1 Not Activated")
                                       .arg(i);
                }
            }
            i = i++;
        }

        //// Check which receivers are activated in prefs
        // for (int i = 1; i <= 5; ++i) {
        //     QString receiverActive = QString("OscReceiver%1Active").arg(i);
        //     QString receiverIp = QString("OscReceiver%1Ip").arg(i);
        //     if (pConfig->getValue<bool>(ConfigKey("[OSC]", receiverActive))) {
        //         const QString& ckOscRecIp = pConfig->getValue(ConfigKey("[OSC]", receiverIp));
        //         if (sDebug) {
        //             qDebug() << QString(
        //                     "[OSC] [OSCRECEIVER] -> Mixxx OSC Receiver %1 with "
        //                     "ip-address: %2 Activated")
        //                                 .arg(i)
        //                                 .arg(ckOscRecIp);
        //         }
        //     } else {
        //         if (sDebug) {
        //             qDebug()
        //                     << QString("[OSC] [OSCRECEIVER] -> Mixxx OSC "
        //                                "Receiver %1 Not Activated")
        //                                .arg(i);
        //         }
        //     }
        // }
    } else {
        if (sDebug) {
            qDebug() << "[OSC] [OSCRECEIVER] -> Mixxx OSC Service NOT Enabled";
        }
    }
}

//
// void OscReceiver::oscReceiverMain(UserSettingsPointer pConfig) {
//    //    //if (!pConfig) {
//    //    //    qWarning() << "[OSC] [OSCRECEIVER] -> Config not loaded yet.
//    Delaying OSC startup...";
//    //    //    QTimer::singleShot(1000, this, [this, pConfig]() {
//    oscReceiverMain(pConfig); });
//    //    //    return;
//    //    //}
//    if (pConfig->getValue<bool>(ConfigKey("[OSC]", "OscEnabled"))) {
//        int ckOscPortInInt = pConfig->getValue(ConfigKey("[OSC]",
//        "OscPortIn")).toInt(); if (sDebug) {
//            qDebug() << "[OSC] [OSCRECEIVER] -> Enabled -> Started";
//        }
//        // Check which receivers are activated in prefs
//        for (int i = 1; i <= 5; ++i) {
//            QString receiverActive = QString("OscReceiver%1Active").arg(i);
//            QString receiverIp = QString("OscReceiver%1Ip").arg(i);
//            if (pConfig->getValue<bool>(ConfigKey("[OSC]", receiverActive))) {
//                const QString& ckOscRecIp =
//                pConfig->getValue(ConfigKey("[OSC]", receiverIp)); if (sDebug)
//                {
//                    qDebug() << QString("[OSC] [OSCRECEIVER] -> Mixxx OSC
//                    Receiver %1 with ip-address: %2
//                    Activated").arg(i).arg(ckOscRecIp);
//                }
//            } else {
//                if (sDebug) {
//                    qDebug() << QString("[OSC] [OSCRECEIVER] -> Mixxx OSC
//                    Receiver %1 Not Activated").arg(i);
//                }
//            }
//        }
//        // Start thread
//        if (sDebug) {
//            qDebug() << "[OSC] [OSCRECEIVER] -> Mixxx OSC Service Thread
//            starting";
//        }
//        // Create the receiver
//        OscReceiver* oscReceiver = new OscReceiver(pConfig);
//        // Create the thread
//        QThread* oscThread = new QThread();
//        oscReceiver->moveToThread(oscThread);
//        // thread -> sifnal to startOscReceiver()
//        QObject::connect(oscThread, &QThread::started, oscReceiver,
//        [oscReceiver, ckOscPortInInt]() {
//        oscReceiver->startOscReceiver(ckOscPortInInt);
//        });
//
//        // Connect the thread finished signal to delete oscReceiver after it's
//        finished QObject::connect(oscThread, &QThread::finished, oscReceiver,
//        &QObject::deleteLater);
//        // Connect the thread finished signal to stop the thread safely
//        QObject::connect(oscThread, &QThread::finished, oscThread,
//        &QThread::deleteLater);
//        // Start the thread
//        oscThread->start();
//
//        if (sDebug) {
//            qDebug() << "[OSC] [OSCRECEIVER] -> Mixxx OSC Service Thread
//            quit";
//        }
//    } else {
//        if (sDebug) {
//            qDebug() << "[OSC] [OSCRECEIVER] -> Mixxx OSC Service NOT
//            Enabled";
//        }
//    }
//}

void OscReceiver::loadOscConfiguration(UserSettingsPointer pConfig) {
    if (!s_configLoaded1stTimeFromFile.load()) {
        QMutexLocker locker(&s_configMutex);
        qDebug() << "[OSC] -> loadOscConfiguration -> start loading config";
        if (!pConfig) {
            qWarning() << "[OSC] [OSCFUNCTIONS] -> pConfig is nullptr! Aborting OSC send.";
            return;
        }

        if (!pConfig) {
            qWarning() << "[OSC] [OSCFUNCTIONS] -> pConfig is nullptr! Aborting reload.";
            return;
        }

        // Read all necessary configuration values
        // s_oscEnabled = pConfig->getValue<bool>(ConfigKey("[OSC]", "OscEnabled"));
        if (pConfig->getValue<bool>(ConfigKey("[OSC]", "OscEnabled"))) {
            s_oscEnabled.store(true);
            qDebug() << "[OSC] -> loadOscConfiguration -> s_oscEnabled set to TRUE";
        }

        if (pConfig->getValue<bool>(ConfigKey("[OSC]", "OscEnabled"))) {
            s_oscEnabled = true;
        }
        s_oscSendSyncTriggers = m_pConfig->getValue<bool>(
                ConfigKey("[OSC]", "OscSendSyncTriggers"));

        s_oscSendSyncTriggersInterval = m_pConfig
                                                ->getValue(ConfigKey(
                                                        "[OSC]", "OscSendSyncTriggersInterval"))
                                                .toInt();
        m_lastCheckStamp = 0;

        s_ckOscPortOutInt = pConfig->getValue(ConfigKey("[OSC]", "OscPortOut")).toInt();

        // Clear existing receiver configurations
        s_receiverConfigs.clear();

        // List of receiver configurations
        const QList<std::pair<QString, QString>> receivers = {
                {"[OSC]", "OscReceiver1"},
                {"[OSC]", "OscReceiver2"},
                {"[OSC]", "OscReceiver3"},
                {"[OSC]", "OscReceiver4"},
                {"[OSC]", "OscReceiver5"}};

        // Store receiver configurations
        for (const auto& receiver : receivers) {
            bool active = pConfig->getValue<bool>(
                    ConfigKey(receiver.first, receiver.second + "Active"));
            QString ip = pConfig->getValue(ConfigKey(receiver.first, receiver.second + "Ip"));
            s_receiverConfigs.append({active, ip});
        }
        // Mark configuration as initialized
        s_configLoaded1stTimeFromFile.store(true);
        sendNoTrackLoadedToOscClients("[Channel1]");
        sendNoTrackLoadedToOscClients("[Channel2]");
        sendNoTrackLoadedToOscClients("[Channel3]");
        sendNoTrackLoadedToOscClients("[Channel4]");

        //  EveOSC end

        qDebug() << "[OSC] [OSCFUNCTIONS] -> OSC configuration loaded.";
    }
}

#pragma once

#include <QChar>
#include <QDataStream>
#include <QDebug>
#include <QFile>
#include <QMutex>
#include <QRegularExpression>
#include <QSharedPointer>
#include <QString>
#include <atomic>
#include <iostream>
#include <memory>

#include "lo/lo.h"

// Global variables (declarations)
extern QMutex s_configMutex;
extern QReadWriteLock g_oscTrackTableLock;
extern std::atomic<bool> s_oscEnabled;
extern int s_ckOscPortOutInt;
extern QList<std::pair<bool, QString>> s_receiverConfigs;
extern std::atomic<bool> s_configLoaded1stTimeFromFile;

constexpr int kMaxOscTracks = 70;
extern std::array<std::tuple<QString, QString, QString>, kMaxOscTracks> g_oscTrackTable;
extern QMutex g_oscTrackTableMutex;

extern const bool sDebugOSCFunctions;

enum class DefOscBodyType {
    STRINGBODY,
    INTBODY,
    DOUBLEBODY,
    FLOATBODY
};

// function to convert and carry special characters in TrackArtist & TrackTitle to ASCII
QString escapeStringToJsonUnicode(const QString& input);

// Function to send OSC message with liblo
void oscFunctionsSendPtrType(
        const QString& oscGroup,
        const QString& oscKey,
        enum DefOscBodyType oscBodyType,
        const QString& oscMessageBodyQString,
        int oscMessageBodyInt,
        double oscMessageBodyDouble,
        float oscMessageBodyFloat);

// function to reload the config OSC settinfs -> maybe call if they changed
// not used at the moment
void reloadOscConfiguration(UserSettingsPointer pConfig);

void storeTrackInfo(const QString& oscGroup,
        const QString& trackArtist,
        const QString& trackTitle);

QString getTrackInfo(const QString& oscGroup, const QString& oscKey);

void sendNoTrackLoadedToOscClients(const QString& oscGroup);

void sendTrackInfoToOscClients(
        const QString& oscGroup,
        const QString& trackArtist,
        const QString& trackTitle,
        float track_loaded,
        float duration,
        float playposition);

void oscChangedPlayState(
        const QString& oscGroup,
        float playstate);

// function to convert url-style /tree/branch/leaf to Mixxx-CO's
QString translatePath(const QString& inputPath);

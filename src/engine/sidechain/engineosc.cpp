#include "engine/sidechain/engineosc.h"

#include "control/controlproxy.h"
#include "encoder/encoder.h"
#include "mixer/playerinfo.h"
#include "moc_engineosc.cpp"
#include "preferences/usersettings.h"
#include "osc/defs_osc.h"
#include "track/track.h"
#include "util/event.h"

constexpr int kMetaDataLifeTimeout = 16;

EngineOsc::EngineOsc(UserSettingsPointer pConfig)
        : m_pConfig(pConfig),
          m_sampleRateControl(QStringLiteral("[App]"), QStringLiteral("samplerate")),
          m_frames(0),
          m_oscDuration(0),
          m_iMetaDataLife(0),
          m_cueTrack(0),
          m_bCueIsEnabled(false) {
    m_pOscReady = new ControlProxy(OSC_PREF_KEY, "status", this);
    m_sampleRate = mixxx::audio::SampleRate::fromDouble(m_sampleRateControl.get());
}

EngineOsc::~EngineOsc() {
    closeCueFile();
    closeFile();
    delete m_pOscReady;
}

int EngineOsc::updateFromPreferences() {
    m_fileName = m_pConfig->getValueString(ConfigKey(OSC_PREF_KEY, "Path"));
    m_baTitle = m_pConfig->getValueString(ConfigKey(OSC_PREF_KEY, "Title"));
    m_baAuthor = m_pConfig->getValueString(ConfigKey(OSC_PREF_KEY, "Author"));
    m_baAlbum = m_pConfig->getValueString(ConfigKey(OSC_PREF_KEY, "Album"));
    m_cueFileName = m_pConfig->getValueString(ConfigKey(OSC_PREF_KEY, "CuePath"));
    m_bCueIsEnabled = m_pConfig->getValueString(ConfigKey(OSC_PREF_KEY, "CueEnabled")).toInt();
    m_sampleRate = mixxx::audio::SampleRate::fromDouble(m_sampleRateControl.get());

    // Delete m_pEncoder if it has been initialized (with maybe) different bitrate.
    if (m_pEncoder) {
        m_pEncoder.reset();
    }
    Encoder::Format format = EncoderFactory::getFactory().getSelectedFormat(m_pConfig);
    m_encoding = format.internalName;
    m_pEncoder = EncoderFactory::getFactory().createOscEncoder(
            format, m_pConfig, this);

    QString userErrorMsg;
    int ret = -1;
    if (m_pEncoder) {
        m_pEncoder->updateMetaData(m_baAuthor, m_baTitle, m_baAlbum);
        ret = m_pEncoder->initEncoder(m_sampleRate, &userErrorMsg);
    }

    if (ret < 0) {
        ErrorDialogProperties* props = ErrorDialogHandler::instance()->newDialogProperties();
        props->setType(DLG_WARNING);
        props->setTitle(format.label + QChar(' ') + QObject::tr(" encoder failure"));
        if (userErrorMsg.isEmpty()) {
            userErrorMsg = QObject::tr(
                    "Failed to apply the selected settings.");
        }
        props->setText(userErrorMsg);
        ErrorDialogHandler::instance()->requestErrorDialog(props);
        m_pEncoder.reset();
    }
    return ret;
}

bool EngineOsc::metaDataHasChanged()
{
    //Originally, m_iMetaDataLife was used so that getCurrentPlayingTrack was called
    //less often, because it was calculating it.
    //Nowadays (since Mixxx 1.11), it just accesses a map on a thread safe method.
    TrackPointer pTrack = PlayerInfo::instance().getCurrentPlayingTrack();
    if (!pTrack) {
        m_iMetaDataLife = kMetaDataLifeTimeout;
        return false;
    }

    //The counter is kept so that changes back and forth with the faders/crossfader
    //(like in scratching or other effects) are not counted as multiple track changes
    //in the cue file. A better solution could consist of a signal from PlayerInfo and
    //a slot that decides if the changes received are valid or are to be ignored once
    //the next process call comes. This could also help improve the time written in the CUE.
    if (m_iMetaDataLife < kMetaDataLifeTimeout) {
        m_iMetaDataLife++;
        return false;
    }
    m_iMetaDataLife = 0;

    if (m_pCurrentTrack) {
        if (!pTrack->getId().isValid() || !m_pCurrentTrack->getId().isValid()) {
            if ((pTrack->getArtist() == m_pCurrentTrack->getArtist()) &&
                (pTrack->getTitle() == m_pCurrentTrack->getArtist())) {
                return false;
            }
        }
        else if (pTrack->getId() == m_pCurrentTrack->getId()) {
            return false;
        }
    }

    m_pCurrentTrack = pTrack;
    return true;
}

void EngineOsc::process(const CSAMPLE* pBuffer, const int iBufferSize) {
    const auto oscStatus = static_cast<int>(m_pOscReady->get());
    static const QString tag("EngineOsc recording");

    if (oscStatus == OSC_OFF) {
        //qDebug("Setting osc flag to: OFF");
        if (fileOpen()) {
            Event::end(tag);
            closeFile();  // Close file and free encoder.
            if (m_bCueIsEnabled) {
                closeCueFile();
            }
            emit isOsc(false, false);
        }
    } else if (oscStatus == OSC_READY) {
        // If we are ready for recording, i.e, the output file has been selected, we
        // open a new file.

        // Update file location from preferences.
        if (updateFromPreferences() < 0) {
            // Maybe the encoder could not be initialized
            qDebug() << "Setting osc flag to: OFF";
            m_pOscReady->set(OSC_OFF);
            // Just report that we don't record
            // There was already a message Box
            emit isOsc(false, false);
        } else if (openFile()) {
            Event::start(tag);
            qDebug("Setting osc flag to: ON");
            m_pRecReady->set(OSC_ON);
            emit isOsc(true, false);  // will notify the OscManager

            // Since we just started recording, timeout and clear the metadata.
            m_iMetaDataLife = kMetaDataLifeTimeout;
            m_pCurrentTrack.reset();

            // clean frames counting and get current sample rate.
            m_frames = 0;
            m_sampleRate = mixxx::audio::SampleRate::fromDouble(m_sampleRateControl.get());

            if (m_bCueIsEnabled) {
                openCueFile();
                m_cueTrack = 0;
            }
        } else {
            qDebug() << "Could not open" << m_fileName << "for writing.";
            qDebug("Setting osc flag to: OFF");
            m_pOscReady->set(OSC_OFF);
            // An error occurred.
            emit isOsc(false, true);
        }
    } else if (oscStatus == OSC_SPLIT_CONTINUE) {
        if (fileOpen()) {
            closeFile();  // Close file and free encoder.
            if (m_bCueIsEnabled) {
                closeCueFile();
            }
        }
        updateFromPreferences();  // Update file location from preferences.
        if (openFile()) {
            qDebug() << "Splitting to a new file: "<< m_fileName;
            m_pOscReady->set(OSC_ON);
            emit isOsc(true, false);  // will notify the OscManager

            // Since we just started recording, timeout and clear the metadata.
            m_iMetaDataLife = kMetaDataLifeTimeout;
            m_pCurrentTrack.reset();

            // clean frames counting and get current sample rate.
            m_frames = 0;
            m_sampleRate = mixxx::audio::SampleRate::fromDouble(m_sampleRateControl.get());
            m_oscDuration = 0;

            if (m_bCueIsEnabled) {
                openCueFile();
                m_cueTrack = 0;
            }
        } else {  // Maybe the encoder could not be initialized
            qDebug() << "Could not open" << m_fileName << "for writing.";
            Event::end(tag);
            qDebug("Setting record flag to: OFF");
            m_pOscReady->set(OSC_OFF);
            // An error occurred.
            emit isOsc(false, true);
        }
    }

    // Checking again from m_pRecReady since its status might have changed
    // in the previous "if" blocks.
    if (m_pOscReady->get() == OSC_ON) {
        // Compress audio. Encoder will call method 'write()' below to
        // write a file stream and emit bytesRecorded.
        m_pEncoder->encodeBuffer(pBuffer, iBufferSize);

        //Writing cueLine before updating the time counter since we prefer to be ahead
        //rather than late.
        if (m_bCueIsEnabled && metaDataHasChanged()) {
            m_cueTrack++;
            writeCueLine();
            m_cueFile.flush();
        }

        // update frames counting and recorded duration (seconds)
        m_frames += iBufferSize / 2;
        unsigned long lastDuration = m_oscDuration;
        m_oscDuration = m_frames / m_sampleRate;

        // gets recorded duration and emit signal that will be used
        // by RecordingManager to update the label besides start/stop button
        if (lastDuration != m_oscDuration) {
            emit durationOsc(m_oscDuration);
        }
    }
}

QString EngineOsc::getOscDurationStr() {
    return QString("%1:%2")
                 .arg(m_oscDuration / 60, 2, 'f', 0, '0')   // minutes
                 .arg(m_oscDuration % 60, 2, 'f', 0, '0');  // seconds
}

void EngineOsc::writeCueLine() {
    if (!m_pCurrentTrack) {
        return;
    }

    // CDDA is specified as having 75 frames a second
    unsigned long cueFrame = ((unsigned long)
                                ((m_frames / (m_sampleRate / 75)))
                                    % 75);

    m_cueFile.write(QString("  TRACK %1 AUDIO\n")
                            .arg((double)m_cueTrack, 2, 'f', 0, '0')
                            .toUtf8());

    m_cueFile.write(QString("    TITLE \"%1\"\n")
                            .arg(m_pCurrentTrack->getTitle())
                            .toUtf8());
    m_cueFile.write(QString("    PERFORMER \"%1\"\n")
                            .arg(m_pCurrentTrack->getArtist())
                            .toUtf8());

    // Woefully inaccurate (at the seconds level anyways).
    // We'd need a signal fired state tracker
    // for the track detection code.
    m_cueFile.write(QString("    INDEX 01 %1:%2\n")
                            .arg(getOscDurationStr())
                            .arg(static_cast<double>(cueFrame), 2, 'f', 0, '0')
                            .toUtf8());
}

// Encoder calls this method to write compressed audio
void EngineOsc::write(const unsigned char *header, const unsigned char *body,
                         int headerLen, int bodyLen) {
    if (!fileOpen()) {
        return;
    }
    // Relevant for OGG
    if (headerLen > 0) {
        m_dataStream.writeRawData((const char*) header, headerLen);
    }
    // Always write body
    m_dataStream.writeRawData((const char*) body, bodyLen);
    emit bytesOsc((headerLen+bodyLen));

}
// Encoder calls this method to write compressed audio
int EngineOsc::tell() {
    if (!fileOpen()) {
        return -1;
    }
    return m_dataStream.device()->pos();
}
// Encoder calls this method to write compressed audio
void EngineOsc::seek(int pos) {
    if (!fileOpen()) {
        return;
    }
    m_dataStream.device()->seek(static_cast<qint64>(pos));
}
// These are not used for streaming, but the interface requires them
int EngineOsc::filelen() {
    if (!fileOpen()) {
        return 0;
    }
    return m_dataStream.device()->size();
}

bool EngineOsc::fileOpen() {
    return (m_file.handle() != -1);
}

bool EngineOsc::openFile() {
    // We can use a QFile to write compressed audio.
    if (m_pEncoder) {
        m_file.setFileName(m_fileName);
        if (!m_file.open(QIODevice::WriteOnly)) {
            qDebug() << "EngineOsc::openFile() failed for"
                     << m_fileName
                     << m_file.errorString();
            return false;
        }
        if (m_file.handle() != -1) {
            m_dataStream.setDevice(&m_file);
        }
    } else {
        return false;
    }

    // Return whether the file is really open.
    return fileOpen();
}

bool EngineOsc::openCueFile() {
    if (m_cueFileName.length() <= 0) {
        return false;
    }

    qDebug() << "Opening Cue File:" << m_cueFileName;
    m_cueFile.setFileName(m_cueFileName);

    // TODO(rryan): maybe we need to use the sandbox to get read/write rights on Mac OS ?!
    if (!m_cueFile.open(QIODevice::WriteOnly)) {
        qDebug() << "Could not write Cue File:"
                 << m_cueFileName
                 << m_cueFile.errorString();
        return false;
    }

    if (m_baAuthor.length() > 0) {
        m_cueFile.write(QString("PERFORMER \"%1\"\n")
                                .arg(QString(m_baAuthor).replace(QString("\""), QString("\\\"")))
                                .toUtf8());
    }

    if (m_baTitle.length() > 0) {
        m_cueFile.write(QString("TITLE \"%1\"\n")
                                .arg(QString(m_baTitle).replace(QString("\""), QString("\\\"")))
                                .toUtf8());
    }

    m_cueFile.write(
            QString("FILE \"%1\" %2\n")
                    .arg(QFileInfo(m_fileName)
                                    .fileName() //strip path
                                    .replace(QString("\""),
                                            QString("\\\"")), // escape doublequote
                            (m_encoding == ENCODING_MP3)
                                    ? ENCODING_MP3
                                    : (m_encoding == ENCODING_AIFF)
                                            ? ENCODING_AIFF
                                            : "WAVE" // MP3 and AIFF are recognized but other formats just use WAVE.
                            )
                    .toUtf8());
    return true;
}

void EngineOsc::closeFile() {
    if (m_file.handle() != -1) {
        // Close QFile and encoder, if open.
        if (m_pEncoder) {
            m_pEncoder->flush();
            m_pEncoder.reset();
        }
        m_file.close();
    }
}

void EngineOsc::closeCueFile() {
    if (m_cueFile.handle() != -1) {
        m_cueFile.close();
    }
}
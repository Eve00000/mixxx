#include "library/autosuggestions/autosuggestionsfeature.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QMenu>
#include <QTextStream>
#include <QtDebug>

#include "controllers/keyboard/keyboardeventfilter.h"
#include "library/autosuggestions/autosuggestionsprocessor.h"
#include "library/autosuggestions/dlgautosuggestions.h"
#include "library/dao/trackschema.h"
#include "library/library.h"
#include "library/parser.h"
#include "library/trackcollection.h"
#include "library/trackcollectionmanager.h"
#include "library/trackset/crate/cratestorage.h"
#include "library/treeitem.h"
#include "moc_autosuggestionsfeature.cpp"
#include "sources/soundsourceproxy.h"
#include "track/track.h"
#include "util/clipboard.h"
#include "util/defs.h"
#include "util/dnd.h"
#include "widget/wlibrary.h"
#include "widget/wlibrarysidebar.h"

namespace {
const bool sDebugAutoSuggestion = false;

int findOrCrateAutoDjPlaylistId(PlaylistDAO& playlistDAO) {
    int playlistId = playlistDAO.getPlaylistIdFromName(AUTOSUGGESTIONS_TABLE);
    if (playlistId < 0) {
        playlistId = playlistDAO.createPlaylist(
                AUTOSUGGESTIONS_TABLE, PlaylistDAO::PLHT_AUTO_SUGGESTIONS);
        VERIFY_OR_DEBUG_ASSERT(playlistId >= 0) {
            qWarning() << "Failed to create Auto Suggestions playlist!";
        }
    }
    return playlistId;
}
} // anonymous namespace

AutoSuggestionsFeature::AutoSuggestionsFeature(Library* pLibrary,
        UserSettingsPointer pConfig)
        : LibraryFeature(pLibrary, pConfig, QStringLiteral("autosuggestions")),
          m_pTrackCollection(pLibrary->trackCollectionManager()->internalCollection()),
          m_playlistDao(m_pTrackCollection->getPlaylistDAO()),
          m_iAutoSuggestionsPlaylistId(findOrCrateAutoDjPlaylistId(m_playlistDao)),
          m_pAutoSuggestionsProcessor(nullptr),
          m_pSidebarModel(make_parented<TreeItemModel>(this)),
          m_pAutoSuggestionsView(nullptr),
          m_viewName(Library::kAutoSuggestionsViewName),
          m_lastFileTimestamp(0),
          m_lastFileHash(QByteArray()),
          m_lastAutoSuggestionsFile(QString()) {
    qRegisterMetaType<AutoSuggestionsProcessor::AutoSuggestionsState>("AutoSuggestionsState");
    m_pAutoSuggestionsProcessor = new AutoSuggestionsProcessor(this,
            m_pConfig,
            pLibrary->trackCollectionManager(),
            m_iAutoSuggestionsPlaylistId);

    connect(m_pAutoSuggestionsProcessor,
            &AutoSuggestionsProcessor::loadTrackToPlayer,
            this,
            &LibraryFeature::loadTrackToPlayer,
            Qt::QueuedConnection);

    connect(m_pAutoSuggestionsProcessor,
            &AutoSuggestionsProcessor::CheckAutoSuggestionsFileNow,
            this,
            &AutoSuggestionsFeature::SlotCheckAutoSuggestionsFileNow);

    connect(m_pAutoSuggestionsProcessor,
            &AutoSuggestionsProcessor::ForceCheckAutoSuggestionsFileNow,
            this,
            &AutoSuggestionsFeature::SlotForceCheckAutoSuggestionsFileNow);

    m_playlistDao.setAutoSuggestionsProcessor(m_pAutoSuggestionsProcessor);

    std::unique_ptr<TreeItem> pRootItem = TreeItem::newRoot(this);
    m_pSidebarModel->setRootItem(std::move(pRootItem));

    m_pEnableAutoSuggestionsAction = make_parented<QAction>(tr("Enable Auto Suggestions"), this);
    connect(m_pEnableAutoSuggestionsAction.get(),
            &QAction::triggered,
            this,
            &AutoSuggestionsFeature::slotEnableAutoSuggestions);

    m_pDisableAutoSuggestionsAction = make_parented<QAction>(tr("Disable Auto Suggestions"), this);
    connect(m_pDisableAutoSuggestionsAction.get(),
            &QAction::triggered,
            this,
            &AutoSuggestionsFeature::slotDisableAutoSuggestions);

    m_pClearQueueAction = make_parented<QAction>(tr("Clear Auto Suggestions Queue"), this);
    const auto removeKeySequence =
            QKeySequence(static_cast<int>(kHideRemoveShortcutModifier) |
                    kHideRemoveShortcutKey);
    m_pClearQueueAction->setShortcut(removeKeySequence);
    connect(m_pClearQueueAction.get(),
            &QAction::triggered,
            this,
            &AutoSuggestionsFeature::slotRemoveAllSuggestions);
}

AutoSuggestionsFeature::~AutoSuggestionsFeature() {
    delete m_pAutoSuggestionsProcessor;
}

QVariant AutoSuggestionsFeature::title() {
    return tr("Auto Suggestions");
}

void AutoSuggestionsFeature::bindLibraryWidget(
        WLibrary* libraryWidget,
        KeyboardEventFilter* keyboard) {
    m_pAutoSuggestionsView = new DlgAutoSuggestions(
            libraryWidget,
            m_pConfig,
            m_pLibrary,
            m_pAutoSuggestionsProcessor,
            keyboard);
    libraryWidget->registerView(m_viewName, m_pAutoSuggestionsView);
    connect(m_pAutoSuggestionsView,
            &DlgAutoSuggestions::loadTrack,
            this,
            &AutoSuggestionsFeature::loadTrack);
    connect(m_pAutoSuggestionsView,
            &DlgAutoSuggestions::loadTrackToPlayer,
            this,
            &LibraryFeature::loadTrackToPlayer);

    connect(m_pAutoSuggestionsView,
            &DlgAutoSuggestions::trackSelected,
            this,
            &AutoSuggestionsFeature::trackSelected);

    QKeySequence toggleAutoSuggestionsShortcut =
            QKeySequence(keyboard->getKeyboardConfig()->getValueString(
                                 ConfigKey("[AutoSuggestions]", "enabled")),
                    QKeySequence::PortableText);
    m_pEnableAutoSuggestionsAction->setShortcut(toggleAutoSuggestionsShortcut);
    m_pDisableAutoSuggestionsAction->setShortcut(toggleAutoSuggestionsShortcut);
}

void AutoSuggestionsFeature::bindSidebarWidget(WLibrarySidebar* pSidebarWidget) {
    m_pSidebarWidget = pSidebarWidget;
}

TreeItemModel* AutoSuggestionsFeature::sidebarModel() const {
    return m_pSidebarModel;
}

void AutoSuggestionsFeature::activate() {
    emit switchToView(m_viewName);
    emit disableSearch();
    emit enableCoverArtDisplay(true);
}

void AutoSuggestionsFeature::slotRemoveAllSuggestions() {
    clear();
}

void AutoSuggestionsFeature::clear() {
    QMessageBox::StandardButton btn = QMessageBox::question(nullptr,
            tr("Confirmation Clear"),
            tr("Do you really want to remove all tracks from the Auto Suggestions queue?") +
                    tr("This can not be undone."),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
    if (btn == QMessageBox::Yes) {
        m_playlistDao.clearAutoSuggestionsQueue();
    }
}

void AutoSuggestionsFeature::paste() {
    emit pasteFromSidebar();
}

void AutoSuggestionsFeature::deleteItem(const QModelIndex& index) {
    Q_UNUSED(index);
}

bool AutoSuggestionsFeature::dropAccept(const QList<QUrl>& urls, QObject* pSource) {
    Q_UNUSED(urls);
    Q_UNUSED(pSource);
    return false;
}

bool AutoSuggestionsFeature::dragMoveAccept(const QList<QUrl>& urls) {
    Q_UNUSED(urls);
    return true;
}

void AutoSuggestionsFeature::slotEnableAutoSuggestions() {
    m_pAutoSuggestionsProcessor->toggleAutoSuggestions(true);
}

void AutoSuggestionsFeature::slotDisableAutoSuggestions() {
    m_pAutoSuggestionsProcessor->toggleAutoSuggestions(false);
}

void AutoSuggestionsFeature::onRightClick(const QPoint& globalPos) {
    QMenu menu(m_pSidebarWidget);
    if (m_pAutoSuggestionsProcessor->getState() == AutoSuggestionsProcessor::ASS_DISABLED) {
        menu.addAction(m_pEnableAutoSuggestionsAction.get());
    } else {
        menu.addAction(m_pDisableAutoSuggestionsAction.get());
    }
    menu.addAction(m_pClearQueueAction.get());
    menu.exec(globalPos);
}

void AutoSuggestionsFeature::onRightClickChild(const QPoint& globalPos,
        const QModelIndex& index) {
    Q_UNUSED(globalPos);
    Q_UNUSED(index);
}

void AutoSuggestionsFeature::SlotCheckAutoSuggestionsFileNow() {
    ImportAutoSuggestionFile();
}

void AutoSuggestionsFeature::slotClearQueue() {
    m_playlistDao.clearAutoSuggestionsQueue();
}

void AutoSuggestionsFeature::ImportAutoSuggestionFile() {
    const QString settingsPath = m_pConfig->getSettingsPath();
    QString autoSuggestionsFile = settingsPath + "/AutoSuggestions.m3u8";

    if (autoSuggestionsFile.isEmpty()) {
        if (sDebugAutoSuggestion) {
            qDebug() << "[AutoSuggestionsFeature] -> Empty file path";
        }
        qDebug() << "[AutoSuggestionsFeature] -> Empty file path";
        return;
    }

    QFile file(autoSuggestionsFile);
    if (!file.exists()) {
        if (sDebugAutoSuggestion) {
            qDebug() << "[AutoSuggestionsFeature] -> File does not exist:" << autoSuggestionsFile;
        }
        return;
    }

    QFileInfo fileInfo(autoSuggestionsFile);
    qint64 currentTimestamp = fileInfo.lastModified().toSecsSinceEpoch();

    QByteArray currentHash;
    if (file.open(QIODevice::ReadOnly)) {
        QCryptographicHash hash(QCryptographicHash::Md5);
        if (hash.addData(&file)) {
            currentHash = hash.result();
        }
        file.close();
    }

    QString lastFile = m_pConfig->getValueString(ConfigKey("[AutoSuggestions]", "LastFile"));
    qint64 lastTimestamp =
            m_pConfig
                    ->getValueString(
                            ConfigKey("[AutoSuggestions]", "LastTimestamp"))
                    .toLongLong();

    QString lastHashHex = m_pConfig->getValueString(ConfigKey("[AutoSuggestions]", "LastHash"));
    QByteArray lastHash = QByteArray::fromHex(lastHashHex.toLatin1());

    QString currentHashHex = QString::fromLatin1(currentHash.toHex());

    if (sDebugAutoSuggestion) {
        qDebug() << "[AutoSuggestionsFeature] -> Current hash (raw):" << currentHash.toHex();
        qDebug() << "[AutoSuggestionsFeature] -> Last hash (hex from config):" << lastHashHex;
        qDebug() << "[AutoSuggestionsFeature] -> Last hash (converted back):" << lastHash.toHex();
    }
    // bool fileChanged = false;

    if (lastFile != autoSuggestionsFile) {
        // fileChanged = true;
        if (sDebugAutoSuggestion) {
            qDebug() << "[AutoSuggestionsFeature] -> File path changed from"
                     << lastFile << "to" << autoSuggestionsFile;
        }
    } else if (currentTimestamp != lastTimestamp) {
        // fileChanged = true;
        if (sDebugAutoSuggestion) {
            qDebug() << "[AutoSuggestionsFeature] -> Timestamp changed from"
                     << lastTimestamp << "to" << currentTimestamp;
        }
    } else if (lastHashHex.isEmpty() || currentHash != lastHash) {
        // fileChanged = true;
        if (sDebugAutoSuggestion) {
            qDebug() << "[AutoSuggestionsFeature] -> Content hash changed";
            // qDebug() << "Old hash (hex):" << lastHashHex;
            // qDebug() << "New hash (hex):" << currentHashHex;
            // qDebug() << "Old hash (raw):" << lastHash.toHex();
            // qDebug() << "New hash (raw):" << currentHash.toHex();
        }
    } else {
        if (sDebugAutoSuggestion) {
            qDebug() << "[AutoSuggestionsFeature] -> File hasn't changed since last import."
                     << "Timestamp:" << currentTimestamp
                     << "Hash:" << currentHashHex
                     << "Skipping import to preserve playlist.";
        }
        return;
    }

    if (sDebugAutoSuggestion) {
        qDebug() << "[AutoSuggestionsFeature] -> File has changed - importing.";
    }

    if (m_playlistDao.isPlaylistLocked(m_iAutoSuggestionsPlaylistId)) {
        if (sDebugAutoSuggestion) {
            qDebug() << "[AutoSuggestionsFeature] -> Can't import a playlist into "
                        "locked playlist"
                     << m_iAutoSuggestionsPlaylistId
                     << m_playlistDao.getPlaylistName(m_iAutoSuggestionsPlaylistId);
        }
        return;
    }

    m_playlistDao.clearAutoSuggestionsQueue();

    std::unique_ptr<PlaylistTableModel> pPlaylistTableModel =
            std::make_unique<PlaylistTableModel>(this,
                    m_pLibrary->trackCollectionManager(),
                    m_pConfig,
                    "mixxx.db.model.playlist_export");
    pPlaylistTableModel->selectPlaylist(m_iAutoSuggestionsPlaylistId);
    pPlaylistTableModel->setSort(
            pPlaylistTableModel->fieldIndex(
                    ColumnCache::COLUMN_PLAYLISTTRACKSTABLE_POSITION),
            Qt::AscendingOrder);
    pPlaylistTableModel->select();

    QString processedFilePath = settingsPath + "/AutoSuggestions_processed.m3u8";

    QFile originalFile(autoSuggestionsFile);
    QFile processedFile(processedFilePath);

    QList<QString> locations;

    if (originalFile.open(QIODevice::ReadOnly | QIODevice::Text) &&
            processedFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (sDebugAutoSuggestion) {
            qDebug() << "[AutoSuggestionsFeature] -> Processing file with placeholder replacement";
        }

        QTextStream readStream(&originalFile);
        QTextStream writeStream(&processedFile);

        const QString kSettingsDirPlaceholder = QStringLiteral("{{MIXXX_SETTINGS_DIR}}");
        const QString kAutoSuggestionsPlaceholder = QStringLiteral("{{AUTOSUGGESTIONS}}");

        int replacementCount = 0;

        while (!readStream.atEnd()) {
            QString line = readStream.readLine();
            QString originalLine = line;

            if (line.contains(kSettingsDirPlaceholder)) {
                line.replace(kSettingsDirPlaceholder, settingsPath);
                replacementCount++;
                if (sDebugAutoSuggestion) {
                    qDebug() << "[AutoSuggestionsFeature] -> Replaced "
                                "{{MIXXX_SETTINGS_DIR}} in line:"
                             << originalLine;
                    qDebug() << "[AutoSuggestionsFeature] -> With:" << line;
                }
            }
            if (line.contains(kAutoSuggestionsPlaceholder)) {
                line.replace(kAutoSuggestionsPlaceholder, "AutoSuggestions");
                replacementCount++;
                if (sDebugAutoSuggestion) {
                    qDebug() << "[AutoSuggestionsFeature] -> Replaced "
                                "{{AutoSuggestions}} in line:"
                             << originalLine;
                    qDebug() << "[AutoSuggestionsFeature] -> With:" << line;
                }
            }

            writeStream << line << "\n";
        }

        originalFile.close();
        processedFile.close();

        if (sDebugAutoSuggestion) {
            qDebug() << "[AutoSuggestionsFeature] -> Performed" << replacementCount
                     << "placeholder replacements";
        }

        locations = Parser::parse(processedFilePath);

        if (sDebugAutoSuggestion) {
            qDebug() << "[AutoSuggestionsFeature] Raw locations from parser after replacement:";
        }
        for (const QString& location : std::as_const(locations)) {
            if (sDebugAutoSuggestion) {
                qDebug() << "  -" << location;
            }
        }

        if (processedFile.exists()) {
            processedFile.remove();
            if (sDebugAutoSuggestion) {
                qDebug() << "[AutoSuggestionsFeature] -> Removed temporary processed file";
            }
        }

    } else {
        if (sDebugAutoSuggestion) {
            qDebug() << "[AutoSuggestionsFeature] -> ERROR: Could not open files for processing";
            qDebug() << "[AutoSuggestionsFeature] -> Original file open:" << originalFile.isOpen();
            qDebug() << "[AutoSuggestionsFeature] -> Processed file open:"
                     << processedFile.isOpen();
        }
        return;
    }

    if (locations.isEmpty()) {
        if (sDebugAutoSuggestion) {
            qDebug() << "[AutoSuggestionsFeature] -> No valid tracks found in file";
        }
        return;
    }

    pPlaylistTableModel->addTracks(QModelIndex(), locations);

    m_pConfig->set(ConfigKey("[AutoSuggestions]", "LastFile"),
            ConfigValue(autoSuggestionsFile));
    m_pConfig->set(ConfigKey("[AutoSuggestions]", "LastTimestamp"),
            ConfigValue(QString::number(currentTimestamp)));
    m_pConfig->set(ConfigKey("[AutoSuggestions]", "LastHash"),
            ConfigValue(currentHashHex));

    if (sDebugAutoSuggestion) {
        qDebug() << "[AutoSuggestions] -> Successfully imported" << locations.size()
                 << "tracks. Timestamp:" << currentTimestamp
                 << "Hash (hex):" << currentHashHex;
    }
}

void AutoSuggestionsFeature::SlotForceCheckAutoSuggestionsFileNow() {
    if (sDebugAutoSuggestion) {
        qDebug() << "[AutoSuggestionsFeature] -> Force import requested - "
                    "resetting timestamp and hash";
    }

    m_lastFileTimestamp = 0;
    m_lastFileHash.clear();
    m_lastAutoSuggestionsFile.clear();

    m_pConfig->set(ConfigKey("[AutoSuggestions]", "LastFile"), ConfigValue(""));
    m_pConfig->set(ConfigKey("[AutoSuggestions]", "LastTimestamp"), ConfigValue("0"));
    m_pConfig->set(ConfigKey("[AutoSuggestions]", "LastHash"), ConfigValue(""));

    ImportAutoSuggestionFile();
}

void AutoSuggestionsFeature::ResetFileTracking() {
    if (sDebugAutoSuggestion) {
        qDebug() << "AutoSuggestionsFeature: Resetting file tracking data";
    }
    m_lastFileTimestamp = 0;
    m_lastFileHash.clear();
    m_lastAutoSuggestionsFile.clear();
}

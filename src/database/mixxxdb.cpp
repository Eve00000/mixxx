#include "database/mixxxdb.h"

#include <QDir>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include "database/schemamanager.h"
#include "moc_mixxxdb.cpp"
#include "util/assert.h"
#include "util/logger.h"

// The schema XML is baked into the binary via Qt resources.
//static
const QString MixxxDb::kDefaultSchemaFile(":/schema.xml");

//static
const int MixxxDb::kRequiredSchemaVersion = 40;

namespace {

const mixxx::Logger kLogger("MixxxDb");

const QString kType = QStringLiteral("QSQLITE");

const QString kConnectOptions = QStringLiteral("QSQLITE_OPEN_URI");

const QString kUriPrefix = QStringLiteral("file://");

const QString kDefaultFileName = QStringLiteral("mixxxdb.sqlite");

const QString kUserName = QStringLiteral("mixxx");

const QString kPassword = QStringLiteral("mixxx");

// The connection parameters for the main Mixxx DB
mixxx::DbConnection::Params dbConnectionParams(
        const UserSettingsPointer& pConfig,
        bool inMemoryConnection) {
    mixxx::DbConnection::Params params;
    params.type = kType;
    params.connectOptions = kConnectOptions;
    params.filePath = kUriPrefix;
    const QString absFilePath =
            QDir(pConfig->getSettingsPath()).absoluteFilePath(kDefaultFileName);
    // On Windows absFilePath starts with a drive letter instead of
    // the leading '/' as required.
    // https://www.sqlite.org/c3ref/open.html#urifilenameexamples
    if (!absFilePath.startsWith(QChar('/'))) {
        params.filePath += QChar('/');
    }
    params.filePath += absFilePath;
    // Allow multiple connections to the same in-memory database by
    // using a named connection. This is needed to make the database
    // connection pool work correctly even during tests.
    //
    // See also:
    // https://www.sqlite.org/inmemorydb.html
    if (inMemoryConnection) {
        params.filePath += QStringLiteral("?mode=memory&cache=shared");
    }
    params.userName = kUserName;
    params.password = kPassword;
    return params;
}

} // anonymous namespace

MixxxDb::MixxxDb(const UserSettingsPointer& pConfig, bool inMemoryConnection)
        : m_pDbConnectionPool(std::make_shared<mixxx::DbConnectionPool>(
                  dbConnectionParams(pConfig, inMemoryConnection), "MIXXX")),
          m_pConfig(pConfig) {
}

bool MixxxDb::initDatabaseSchema(
        const QSqlDatabase& database,
        int schemaVersion,
        const QString& schemaFile) {
    QString okToExit = tr("Click OK to exit.");
    QString upgradeFailed = tr("Cannot upgrade database schema");
    QString upgradeToVersionFailed =
            tr("Unable to upgrade your database schema to version %1")
            .arg(QString::number(schemaVersion));
    QString helpContact = tr("For help with database issues consult:") + "\n" +
            "https://www.mixxx.org/support";

    switch (SchemaManager(database).upgradeToSchemaVersion(schemaVersion, schemaFile)) {
    case SchemaManager::Result::CurrentVersion:
    case SchemaManager::Result::UpgradeSucceeded:
    case SchemaManager::Result::NewerVersionBackwardsCompatible:
        return true; // done
    case SchemaManager::Result::UpgradeFailed:
        QMessageBox::warning(nullptr,
                upgradeFailed,
                upgradeToVersionFailed + "\n" +
                        tr("Your mixxxdb.sqlite file may be corrupt.") +
                        "\n" + tr("Try renaming it and restarting Mixxx.") +
                        "\n" + helpContact + "\n\n" + okToExit,
                QMessageBox::Ok);
        return false; // abort
    case SchemaManager::Result::NewerVersionIncompatible:
        QMessageBox::warning(nullptr,
                upgradeFailed,
                upgradeToVersionFailed + "\n" +
                        tr("Your mixxxdb.sqlite file was created by a newer "
                           "version of Mixxx and is incompatible.") +
                        "\n\n" + okToExit,
                QMessageBox::Ok);
        return false; // abort
    case SchemaManager::Result::SchemaError:
        QMessageBox::warning(nullptr,
                upgradeFailed,
                upgradeToVersionFailed + "\n" +
                        tr("The database schema file is invalid.") + "\n" +
                        helpContact + "\n\n" + okToExit,
                QMessageBox::Ok);
        return false; // abort
    }
    // Suppress compiler warning
    DEBUG_ASSERT(!"unhandled switch/case");
    return false;
}

bool MixxxDb::createSlimAiSnapshot(const QString& targetPath) {
    kLogger.info() << "[MixxxDB] -> Snapshot for AI: Creating slim AI snapshot at" << targetPath;

    if (!m_pConfig) {
        kLogger.warning() << "[MixxxDB] -> Snapshot for AI: No config available";
        return false;
    }

    QDir settingsDir(m_pConfig->getSettingsPath());
    QString sourcePath = settingsDir.absoluteFilePath("mixxxdb.sqlite");

    // Open source database
    QSqlDatabase sourceDb = QSqlDatabase::addDatabase("QSQLITE", "sourceConnection");
    sourceDb.setDatabaseName(sourcePath);
    sourceDb.setConnectOptions("QSQLITE_OPEN_READONLY");

    if (!sourceDb.open()) {
        kLogger.warning() << "[MixxxDB] -> Snapshot for AI: Failed to open source database";
        QSqlDatabase::removeDatabase("sourceConnection");
        return false;
    }

    // Remove existing snapshot file if exists
    if (QFile::exists(targetPath)) {
        if (!QFile::remove(targetPath)) {
            kLogger.warning() << "[MixxxDB] -> Snapshot for AI: Failed to remove existing snapshot";
            sourceDb.close();
            QSqlDatabase::removeDatabase("sourceConnection");
            return false;
        }
    }

    // Create target database
    QSqlDatabase targetDb = QSqlDatabase::addDatabase("QSQLITE", "slimSnapshot");
    targetDb.setDatabaseName(targetPath);

    if (!targetDb.open()) {
        kLogger.warning() << "[MixxxDB] -> Snapshot for AI: Failed to create target database";
        sourceDb.close();
        QSqlDatabase::removeDatabase("sourceConnection");
        QSqlDatabase::removeDatabase("slimSnapshot");
        return false;
    }

    QSqlQuery query(targetDb);
    targetDb.transaction();

    // Create simplified library
    if (!query.exec(
                "CREATE TABLE library ("
                "id INTEGER PRIMARY KEY,"
                "artist VARCHAR(64),"
                "title VARCHAR(64),"
                "bpm FLOAT,"
                "key VARCHAR(8),"
                "genre VARCHAR(64),"
                "year VARCHAR(16),"
                "comment VARCHAR(256),"
                "duration FLOAT,"
                "mixxx_deleted INTEGER,"
                "location INTEGER"
                ")")) {
        kLogger.warning() << "[MixxxDB] -> Snapshot for AI: Failed to create "
                             "library table:"
                          << query.lastError().text();
        targetDb.rollback();
        return false;
    }

    // Create simplified track_locations
    if (!query.exec(
                "CREATE TABLE track_locations ("
                "id INTEGER PRIMARY KEY,"
                "location VARCHAR(512) UNIQUE"
                ")")) {
        kLogger.warning() << "[MixxxDB] -> Snapshot for AI: Failed to create "
                             "track_locations table:"
                          << query.lastError().text();
        targetDb.rollback();
        return false;
    }

    // Create simplified Playlists
    if (!query.exec(
                "CREATE TABLE Playlists ("
                "id INTEGER PRIMARY KEY,"
                "name VARCHAR(48),"
                "hidden INTEGER"
                ")")) {
        kLogger.warning() << "[MixxxDB] -> Snapshot for AI: Failed to create "
                             "Playlists table:"
                          << query.lastError().text();
        targetDb.rollback();
        return false;
    }

    // Create simplified playlist_tracks
    if (!query.exec(
                "CREATE TABLE PlaylistTracks ("
                "id INTEGER PRIMARY KEY,"
                "playlist_id INTEGER,"
                "track_id INTEGER,"
                "position INTEGER"
                ")")) {
        kLogger.warning() << "[MixxxDB] -> Snapshot for AI: Failed to create "
                             "PlaylistTracks table:"
                          << query.lastError().text();
        targetDb.rollback();
        return false;
    }

    // Create simplified crates
    if (!query.exec(
                "CREATE TABLE crates ("
                "id INTEGER PRIMARY KEY,"
                "name VARCHAR(48) UNIQUE"
                ")")) {
        kLogger.warning() << "[MixxxDB] -> Snapshot for AI: Failed to create "
                             "crates table:"
                          << query.lastError().text();
        targetDb.rollback();
        return false;
    }

    // Create simplified crate_tracks
    if (!query.exec(
                "CREATE TABLE crate_tracks ("
                "crate_id INTEGER,"
                "track_id INTEGER,"
                "UNIQUE(crate_id, track_id)"
                ")")) {
        kLogger.warning() << "[MixxxDB] -> Snapshot for AI: Failed to create "
                             "crate_tracks table:"
                          << query.lastError().text();
        targetDb.rollback();
        return false;
    }

    // Copy data using QSqlQuery
    QSqlQuery selectQuery(sourceDb);
    QSqlQuery insertQuery(targetDb);

    // Copy library data
    selectQuery.exec(
            "SELECT id, artist, title, bpm, key, genre, year, comment, "
            "duration, mixxx_deleted, location FROM library WHERE "
            "mixxx_deleted = 0");
    insertQuery.prepare(
            "INSERT INTO library (id, artist, title, bpm, key, genre, year, "
            "comment, duration, mixxx_deleted, location) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    while (selectQuery.next()) {
        for (int i = 0; i < 11; ++i) {
            insertQuery.bindValue(i, selectQuery.value(i));
        }
        if (!insertQuery.exec()) {
            kLogger.warning() << "[MixxxDB] -> Snapshot for AI: Failed to "
                                 "insert library data:"
                              << insertQuery.lastError().text();
        }
        insertQuery.finish();
        insertQuery.prepare(
                "INSERT INTO library (id, artist, title, bpm, key, genre, "
                "year, comment, duration, mixxx_deleted, location) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    }

    // Copy track_locations
    selectQuery.exec("SELECT id, location FROM track_locations");
    insertQuery.prepare("INSERT INTO track_locations (id, location) VALUES (?, ?)");
    while (selectQuery.next()) {
        insertQuery.bindValue(0, selectQuery.value(0));
        insertQuery.bindValue(1, selectQuery.value(1));
        if (!insertQuery.exec()) {
            kLogger.warning() << "[MixxxDB] -> Snapshot for AI: Failed to "
                                 "insert track_locations data:"
                              << insertQuery.lastError().text();
        }
        insertQuery.finish();
        insertQuery.prepare("INSERT INTO track_locations (id, location) VALUES (?, ?)");
    }

    // Copy playlists
    selectQuery.exec("SELECT id, name, hidden FROM Playlists");
    insertQuery.prepare("INSERT INTO Playlists (id, name, hidden) VALUES (?, ?, ?)");
    while (selectQuery.next()) {
        insertQuery.bindValue(0, selectQuery.value(0));
        insertQuery.bindValue(1, selectQuery.value(1));
        insertQuery.bindValue(2, selectQuery.value(2));
        if (!insertQuery.exec()) {
            kLogger.warning() << "[MixxxDB] -> Snapshot for AI: Failed to "
                                 "insert Playlists data:"
                              << insertQuery.lastError().text();
        }
        insertQuery.finish();
        insertQuery.prepare("INSERT INTO Playlists (id, name, hidden) VALUES (?, ?, ?)");
    }

    // Copy playlist_tracks
    selectQuery.exec("SELECT id, playlist_id, track_id, position FROM PlaylistTracks");
    insertQuery.prepare(
            "INSERT INTO PlaylistTracks (id, playlist_id, track_id, position) "
            "VALUES (?, ?, ?, ?)");
    while (selectQuery.next()) {
        for (int i = 0; i < 4; ++i) {
            insertQuery.bindValue(i, selectQuery.value(i));
        }
        if (!insertQuery.exec()) {
            kLogger.warning() << "[MixxxDB] -> Snapshot for AI: Failed to "
                                 "insert PlaylistTracks data:"
                              << insertQuery.lastError().text();
        }
        insertQuery.finish();
        insertQuery.prepare(
                "INSERT INTO PlaylistTracks (id, playlist_id, track_id, "
                "position) VALUES (?, ?, ?, ?)");
    }

    // Copy crates
    selectQuery.exec("SELECT id, name FROM crates");
    insertQuery.prepare("INSERT INTO crates (id, name) VALUES (?, ?)");
    while (selectQuery.next()) {
        insertQuery.bindValue(0, selectQuery.value(0));
        insertQuery.bindValue(1, selectQuery.value(1));
        if (!insertQuery.exec()) {
            kLogger.warning() << "[MixxxDB] -> Snapshot for AI: Failed to "
                                 "insert crates data:"
                              << insertQuery.lastError().text();
        }
        insertQuery.finish();
        insertQuery.prepare("INSERT INTO crates (id, name) VALUES (?, ?)");
    }

    // Copy crate_tracks
    selectQuery.exec("SELECT crate_id, track_id FROM crate_tracks");
    insertQuery.prepare("INSERT INTO crate_tracks (crate_id, track_id) VALUES (?, ?)");
    while (selectQuery.next()) {
        insertQuery.bindValue(0, selectQuery.value(0));
        insertQuery.bindValue(1, selectQuery.value(1));
        if (!insertQuery.exec()) {
            kLogger.warning() << "Failed to insert crate_tracks data:"
                              << insertQuery.lastError().text();
        }
        insertQuery.finish();
        insertQuery.prepare("INSERT INTO crate_tracks (crate_id, track_id) VALUES (?, ?)");
    }

    targetDb.commit();

    // Cleanup
    sourceDb.close();
    targetDb.close();
    QSqlDatabase::removeDatabase("sourceConnection");
    QSqlDatabase::removeDatabase("slimSnapshot");

    QFileInfo info(targetPath);
    kLogger.info()
            << "[MixxxDB] -> Snapshot for AI: Slim snapshot for AI created:"
            << info.size() / 1024 << "KB";

    return true;
}

#include "backupworker.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTemporaryFile>

// Needs to be uncommented when bit7z and 7z are in the dependencies
// and the parts in the CMakeLists are uncommented too.
#include <bit7z/bit7z.hpp>

#include "moc_backupworker.cpp"

#if defined(Q_OS_WIN)
#include <QSettings>
#endif

namespace {

#if defined(Q_OS_WIN)
// instead of searching for a 7-Zip installation directory
// like C:\Program Files\7-Zip
// -> look up HKLM\SOFTWARE\7-Zip\Path in the Windows registry.
QString find7ZipFromRegistry() {
    const QStringList registryKeys = {
            QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\7-Zip"),
            QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\7-Zip"),
    };
    for (const QString& key : registryKeys) {
        QSettings reg(key, QSettings::NativeFormat);
        const QString dir = reg.value(QStringLiteral("Path")).toString();
        if (dir.isEmpty()) {
            continue;
        }
        const QString exe = QDir(dir).filePath(QStringLiteral("7z.exe"));
        if (QFile::exists(exe)) {
            return exe;
        }
    }
    return QString();
}
#endif

// Locate an external 7z executable,
// -> if not found -> empty string
QString findExternal7z() {
#if defined(Q_OS_WIN)
    QString exe = QStandardPaths::findExecutable(QStringLiteral("7z"));
    if (!exe.isEmpty()) {
        return exe;
    }
    exe = find7ZipFromRegistry();
    if (!exe.isEmpty()) {
        return exe;
    }
    const QStringList winPaths = {
            QDir::homePath() + "/scoop/apps/7zip/current/7z.exe",
            QStringLiteral("C:\\Program Files\\7-Zip\\7z.exe"),
            QStringLiteral("C:\\Program Files (x86)\\7-Zip\\7z.exe")};
    for (const QString& path : winPaths) {
        if (QFile::exists(path)) {
            return path;
        }
    }
    return QString();
#elif defined(Q_OS_LINUX)
    QString exe = QStandardPaths::findExecutable(QStringLiteral("7z"));
    if (!exe.isEmpty()) {
        return exe;
    }
    const QStringList linuxPaths = {
            QStringLiteral("/usr/bin/7z"),
            QStringLiteral("/usr/local/bin/7z"),
            QStringLiteral("/bin/7z")};
    for (const QString& path : linuxPaths) {
        if (QFile::exists(path)) {
            return path;
        }
    }
    return QString();
#else
    // macOS and others: no external 7z lookup
    // -> fall back on bit7z
    // or on the built-in zip utility on on macos
    return QString();
#endif
}

// path to included bit7z library
// -> 7z library next to the Mixxx executable
// -> QDir::filePath()
// -> QDir::cleanPath() removes "/./"
// or duplicate separators
QString shippedBit7zLibraryPath() {
#if defined(Q_OS_WIN)
    const QString fileName = QStringLiteral("7z.dll");
#else
    const QString fileName = QStringLiteral("7z");
#endif
    const QDir appDir(QCoreApplication::applicationDirPath());
    return QDir::cleanPath(appDir.filePath(fileName));
}

} // anonymous namespace

BackUpWorker::BackUpWorker(
        UserSettingsPointer config,
        int keepBackUps,
        bool upgradeBU,
        QObject* parent)
        : QObject(parent),
          m_pConfig(config),
          m_keepBackUps(keepBackUps),
          m_upgradeBU(upgradeBU) {
    currentMixxxVersion = m_pConfig->getValue(ConfigKey("[Config]", "Version"));
    useBit7z = false;
}

// Helper for Windows: resolve the users Documents folder
// -> localized Windows installs must return correct path
// -> QStandardPaths::DocumentsLocation may return a localized
//    display name like "Documenten" in NL
// -> Robocopy fails on the translated name
// -> If that happens -> fall back to the English folder name
//    under the user's home directory.
// A folder is only usable if we can actually write a file into it.
// QDir::exists() returns true for localized shell aliases like
// "Documenten" NL -> problem for Win32 tools (robocopy)
// -> real file test
QString BackUpWorker::resolveDocumentsDir() {
    auto isWritableDir = [](const QString& dir) -> bool {
        if (dir.isEmpty() || !QDir(dir).exists()) {
            return false;
        }
        QTemporaryFile probe(QDir(dir).filePath(QStringLiteral("mixxx-write-test-XXXXXX")));
        return probe.open();
    };

    // The standard Documents location.
    // -> On English Windows this is normally "C:/Users/<user>/Documents".
    const QString standard = QStandardPaths::writableLocation(
            QStandardPaths::DocumentsLocation);
    if (isWritableDir(standard)) {
        return standard;
    }
    qWarning() << "[BackUp] -> [BackUpWorker] -> DocumentsLocation"
               << standard << "not writable, trying fallbacks";

    // "Documents" under the user's home.
    //  This is what the real folder is named on disk even
    const QString homeDocuments = QDir::homePath() + "/Documents";
    if (isWritableDir(homeDocuments)) {
        qWarning() << "[BackUp] -> [BackUpWorker] -> using" << homeDocuments;
        return homeDocuments;
    }

    // Last resort: root of the system disk (C:\)
    // Always writable for the current user via UAC-compatible locations
    // independent of any localization.
#if defined(Q_OS_WIN)
    QString systemRoot = QDir::rootPath();
    while (systemRoot.endsWith('/')) {
        systemRoot.chop(1);
    }
    if (systemRoot.isEmpty()) {
        systemRoot = QStringLiteral("C:");
    }
#else
    QString systemRoot = QDir::rootPath(); // "/"
#endif
    qWarning() << "[BackUp] -> [BackUpWorker] -> falling back to system disk root:"
               << systemRoot;
    return systemRoot;
}

bool BackUpWorker::copySettingsToTempDir(const QString& settingsDir, const QString& tempDirPath) {
#if defined(Q_OS_WIN)
    // Robocopy parses its own command line
    // -> split any argument that contains spaces
    const QString robocopyLog = QDir::toNativeSeparators(
            QDir::tempPath() + "/mixxx-backup-robocopy.log");

    qDebug() << "[BackUp] -> [BackUpWorker] -> start creation tempdir (robocopy)";

    QProcess robocopy;
    robocopy.setProcessChannelMode(QProcess::MergedChannels);

    // Check for robocopy.exe
    QObject::connect(&robocopy, &QProcess::errorOccurred, [](QProcess::ProcessError e) {
        qWarning() << "[BackUp] -> [BackUpWorker] -> robocopy process error:" << e;
    });

    robocopy.start(QStringLiteral("robocopy"),
            {QDir::toNativeSeparators(settingsDir),
                    QDir::toNativeSeparators(tempDirPath),
                    "/E", // copy subdirectories, including empty ones
                    "/XD",
                    "analysis", // exclude analysis
                    "/XD",
                    "lut", // exclude timecode lut
                    "/XD",
                    "samples", // exclude samples
                    "/XD",
                    "bpmcurve", // exclude bpmcurve
                    "/XD",
                    "keycurve", // exclude keycurve
                    "/XD",
                    "fingerprints", // exclude keycurve
                    "/R:3",         // retry 3 times if file is locked
                    "/W:2",         // wait 2 seconds between retries
                    "/NP",          // progress display off
                    "/NFL",         // no file list (keep log small)
                    "/NDL",         // no dir list
                    "/LOG+:" + robocopyLog});

    if (!robocopy.waitForFinished(300000)) {
        qCritical() << "[BackUp] -> [BackUpWorker] -> robocopy timed out! "
                       "See log:"
                    << robocopyLog;
        return false;
    }

    // Robocopy exit codes are bitfields, NOT standard exit codes:
    //   0 = no files copied, no failures
    //   1 = files copied successfully
    //   2 = extra files/dirs detected
    //   4 = mismatched files/dirs
    //   8 = some files/dirs could not be copied (copy errors)
    //  16 = serious error, no copy performed
    // Anything with the 8 or 16 bit set means a real failure.
    const int rc = robocopy.exitCode();
    if (rc >= 8) {
        qCritical() << "[BackUp] -> [BackUpWorker] -> robocopy failed with exit code"
                    << rc << "- see log:" << robocopyLog;
        // Dump the log to the Mixxx log
        QFile log(robocopyLog);
        if (log.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qCritical().noquote() << log.readAll();
        }
        return false;
    }
    return true;

#elif defined(Q_OS_LINUX)
    qDebug() << "[BackUp] -> [BackUpWorker] -> start creation tempdir (rsync)";

    QProcess rsync;
    rsync.setProgram(QStringLiteral("rsync"));
    rsync.setArguments({"-a",
            "--exclude=analysis/", // exclude analysis
            "--exclude=lut/",      // exclude timecode lut
            settingsDir + "/",
            tempDirPath + "/"});
    rsync.start();
    rsync.waitForFinished();

    qDebug() << "stdout:" << rsync.readAllStandardOutput();
    qDebug() << "stderr:" << rsync.readAllStandardError();

    if (rsync.exitCode() != 0) {
        qCritical() << "[BackUp] -> [BackUpWorker] -> rsync failed! Exit code:" << rsync.exitCode();
        return false;
    }
    return true;

#else
    // macos ea: use Qts file copy
    QDirIterator it(settingsDir,
            QDir::Files | QDir::NoDotAndDotDot,
            QDirIterator::Subdirectories);

    while (it.hasNext()) {
        QString srcPath = it.next();
        QString relativePath = QDir(settingsDir).relativeFilePath(srcPath);

        // Skip analysis folders at any level.
        if (relativePath.contains("analysis/") ||
                relativePath.startsWith("analysis/") ||
                relativePath.endsWith("/analysis")) {
            continue;
        }
        // Skip lut folders at any level.
        if (relativePath.contains("lut/") ||
                relativePath.startsWith("lut/") ||
                relativePath.endsWith("/lut")) {
            continue;
        }
        // Skip samples folders at any level.
        if (relativePath.contains("samples/") ||
                relativePath.startsWith("samples/") ||
                relativePath.endsWith("/samples")) {
            continue;
        }
        // Skip bpmcurve folders at any level.
        if (relativePath.contains("bpmcurve/") ||
                relativePath.startsWith("bpmcurve/") ||
                relativePath.endsWith("/bpmcurve")) {
            continue;
        }
        // Skip keycurve folders at any level.
        if (relativePath.contains("keycurve/") ||
                relativePath.startsWith("keycurve/") ||
                relativePath.endsWith("/keycurve")) {
            continue;
        }
        // Skip fingerprints folders at any level.
        if (relativePath.contains("fingerprints/") ||
                relativePath.startsWith("fingerprints/") ||
                relativePath.endsWith("/fingerprints")) {
            continue;
        }

        QString destPath = tempDirPath + "/" + relativePath;
        QFileInfo(destPath).dir().mkpath(".");
        if (!QFile::copy(srcPath, destPath)) {
            qCritical() << "Failed to copy file:" << srcPath << "to" << destPath;
            return false;
        }
    }
    return true;
#endif
}

void BackUpWorker::performBackUp() {
    QString backupDir, archivePath, archivePath7zExt, archivePathZipExt, zipExecutable;
    const QString settingsDir = m_pConfig->getSettingsPath();
    const QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");

    // Resolve the Documents folder in a way that works on localized
    // Windows installs. QStandardPaths::DocumentsLocation can return a
    // display name that robocopy cannot resolve.
    const QString documentsDir = resolveDocumentsDir();

    if (m_upgradeBU) {
        backupDir = documentsDir + "/Mixxx-BackUps/UpgradeBUs";
        archivePath = backupDir + "/MixxxSettings-Upgrade-" +
                currentMixxxVersion + "-" + timestamp;
    } else {
        backupDir = documentsDir + "/Mixxx-BackUps";
        archivePath = backupDir + "/MixxxSettings-" + timestamp;
    }

    archivePath7zExt = archivePath + ".7z";
    archivePathZipExt = archivePath + ".zip";

    qDebug() << "[BackUp] -> [BackUpWorker] -> documentsDir:" << documentsDir;
    qDebug() << "[BackUp] -> [BackUpWorker] -> backupDir:" << backupDir;

    // Ensure the backup directory exists
    // mkpath returns false on failure unresolvable parent
    // a redirected/synced folder... which is the only way
    // we'd know about a problem before robocopy fails with
    // ERROR 2.
    if (!QDir().mkpath(backupDir)) {
        qCritical() << "[BackUp] -> [BackUpWorker] -> could not create backup dir:"
                    << backupDir
                    << "- check that the parent folder is writable and not a"
                       " redirected/synced/network path.";
        emit backUpFinished(false,
                QStringLiteral("Backup failed: could not create %1").arg(backupDir));
        return;
    }

#if defined(Q_OS_MACOS)
    // macOS has no 7z dependency; we use the system zip directly.
    zipExecutable = QStringLiteral("/usr/bin/zip");
#else
    // Windows & Linux: prefer an external 7z if the user has one installed.
    zipExecutable = findExternal7z();
#endif

    ///////////////////////////////////////////////////////////////////
    // Phase 1: create the temp directory with a copy of the settings
    ///////////////////////////////////////////////////////////////////
    QString tempBackupDir = archivePath + "_temp";

    if (!zipExecutable.isEmpty()) {
        qDebug() << "[BackUp] -> [BackUpWorker] -> 7z/Zip found in:" << zipExecutable;
        qDebug() << "[BackUp] -> [BackUpWorker] -> tempBackupDir:" << tempBackupDir;

        if (!QDir().mkpath(tempBackupDir)) {
            qCritical() << "[BackUp] -> [BackUpWorker] -> could not create temp dir:"
                        << tempBackupDir;
            emit backUpFinished(false,
                    QStringLiteral("Backup failed: could not create %1").arg(tempBackupDir));
            return;
        }

        if (!copySettingsToTempDir(settingsDir, tempBackupDir)) {
            QDir(tempBackupDir).removeRecursively();
            qCritical() << "[BackUp] -> [BackUpWorker] -> error creating tempdir";
            emit backUpFinished(false, "Backup failed: could not copy settings");
            return;
        }

#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
        QProcess process;
        process.setProcessChannelMode(QProcess::MergedChannels);
        process.start(zipExecutable,
                {"a",
                        "-t7z",
                        archivePath7zExt,
                        tempBackupDir,
                        "-xr!analysis",
                        "-xr!lut",
                        "-mx=6"});

        if (!process.waitForStarted(10000)) {
            qCritical() << "[BackUp] -> [BackUpWorker] -> 7z failed to start:"
                        << process.errorString();
        } else if (!process.waitForFinished(300000)) {
            qCritical() << "[BackUp] -> [BackUpWorker] -> 7z compression timed out!";
            process.kill();
        } else if (process.exitCode() != 0) {
            qCritical() << "[BackUp] -> [BackUpWorker] -> 7z failed:"
                        << process.readAllStandardOutput();
        } else {
            qDebug() << "[BackUp] -> [BackUpWorker] -> Backup succeeded! "
                        "Archive:"
                     << archivePath7zExt;
            QDir(tempBackupDir).removeRecursively();
            emit backUpFinished(true, archivePath7zExt);
            useBit7z = false;
            return;
        }

        // 7z failed.
        // -> bit7z fallback
        qWarning() << "[BackUp] -> [BackUpWorker] -> 7z failed, "
                      "falling back to bit7z";
        useBit7z = true;

#elif defined(Q_OS_MACOS)
        QStringList arguments = {"-r",
                archivePathZipExt,
                settingsDir,
                "-x",
                settingsDir + "/analysis/*",
                "-x",
                settingsDir + "/lut/*"};
        qDebug() << "[BackUp] -> [BackUpWorker] -> Executing:" << zipExecutable
                 << arguments.join(" ");
        bool started = QProcess::startDetached(zipExecutable, arguments);
        if (started) {
            qDebug() << "[BackUp] -> [BackUpWorker] -> MacOS zip backup "
                        "started to:"
                     << archivePathZipExt;
            QDir(tempBackupDir).removeRecursively();
            emit backUpFinished(true, archivePathZipExt);
            return;
        }
        qWarning() << "[BackUp] -> [BackUpWorker] -> MacOS zip backup failed.";
        useBit7z = true;
#endif
    } else {
        qWarning() << "[BackUp] -> [BackUpWorker] -> 7z/Zip not found. "
                      "-> using the shipped 7z.dll (bit7z).";
        useBit7z = true;
    }

    ///////////////////////////////////////////////////////////////////
    // Phase 2: bit7z fallback using the 7z.dll in Mixxx folder
    // -> no user-installed 7z
    // -> external 7z failed (already created temp dir in Phase 1)
    ///////////////////////////////////////////////////////////////////
    if (useBit7z) {
        qDebug() << "[BackUp] -> [BackUpWorker] -> Bit7z started";
        emit progressChanged(0);

        const QString path7z = shippedBit7zLibraryPath();

        if (!QFile::exists(path7z)) {
            qWarning() << "[BackUp] -> [BackUpWorker] -> shipped bit7z library not found:"
                       << path7z;
            QDir(tempBackupDir).removeRecursively();
            emit backUpFinished(false,
                    QStringLiteral("Backup failed: no 7z backend available "
                                   "(no external 7z, and shipped %1 missing)")
                            .arg(path7z));
            return;
        }

        try {
            // create + populate the temp dir if Stage 1 didn't succeed
            if (zipExecutable.isEmpty()) {
                qDebug() << "[BackUp] -> [BackUpWorker] -> tempBackupDir (bit7z):"
                         << tempBackupDir;
                if (!QDir().mkpath(tempBackupDir)) {
                    qCritical() << "[BackUp] -> [BackUpWorker] -> could not create temp dir:"
                                << tempBackupDir;
                    emit backUpFinished(false,
                            QStringLiteral("Backup failed: could not create %1")
                                    .arg(tempBackupDir));
                    return;
                }
                if (!copySettingsToTempDir(settingsDir, tempBackupDir)) {
                    QDir(tempBackupDir).removeRecursively();
                    emit errorOccurred(
                            "Could not create temporary directory for backup.");
                    emit backUpFinished(false, "Backup failed: tempdir");
                    return;
                }
            }

            bit7z::Bit7zLibrary lib(path7z.toStdString());
            bit7z::BitFileCompressor compressor(lib, bit7z::BitFormat::SevenZip);

            emit progressChanged(10);
            compressor.compressDirectory(
                    tempBackupDir.toStdString(),
                    archivePath7zExt.toStdString());
            emit progressChanged(80);
            QDir(tempBackupDir).removeRecursively();

            emit progressChanged(100);
            qDebug() << "[BackUp] -> [BackUpWorker] -> Backup succeeded (bit7z)! "
                        "Archive:"
                     << archivePath7zExt;
            emit backUpFinished(true, archivePath7zExt);

        } catch (const bit7z::BitException& ex) {
            const QString msg = QString::fromStdString(ex.what());
            emit errorOccurred(msg);
            qCritical() << "[BackUp] -> [BackUpWorker] -> bit7z error:" << msg;
            QDir(tempBackupDir).removeRecursively();
            emit backUpFinished(false, "Backup failed: " + msg);
        }

        qDebug() << "[BackUpWorker] --> Bit7z ended";
    }
}

void BackUpWorker::deleteOldBackUps() {
    // Use the same Documents resolution as performBackUp() so cleanup
    // actually targets the folder where the backups live.
    const QString backupDir = resolveDocumentsDir() + "/Mixxx-BackUps";
    QDir dir(backupDir);
    dir.setNameFilters({"MixxxSettings-*.7z"});
    dir.setSorting(QDir::Time);

    const auto backups = dir.entryInfoList();
    for (int i = m_keepBackUps; i < backups.size(); ++i) {
        dir.remove(backups[i].fileName());
        emit backUpRemoved(backups[i].fileName());
    }
    // upgrade backups are created in Mixxx-BackUps/UpgradeBUs/
    // -> "MixxxSettings-Upgrade-..."
    // -> they will not be removed
}

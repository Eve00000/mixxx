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

// Needs to be uncommented when bit7z and 7z are in the dependencies
// and the parts in the CMakeLists are uncommented too.
#include <bit7z/bit7z.hpp>

#include "moc_backupworker.cpp"

#if defined(Q_OS_WIN)
#include <QSettings>
#endif

namespace {

#if defined(Q_OS_WIN)
// Look up the 7-Zip installation directory in the Windows registry.
// 7-Zip writes its install path to HKLM\SOFTWARE\7-Zip\Path (and to the
// 32-bit view on WOW64 systems). This is more reliable than hard-coding
// C:\Program Files\7-Zip because users may install to a different drive
// or a portable location that also registers itself.
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

// Locate an external 7z executable. Returns an empty string if none found.
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
    // macOS and others: no external 7z lookup; caller falls back to bit7z
    // (or, on macOS, to the built-in zip utility, depending on your wiring).
    return QString();
#endif
}

// Resolve the path of the shipped bit7z backend library, i.e. the 7z
// shared library that sits next to the Mixxx executable. QDir::filePath()
// handles the join correctly and QDir::cleanPath() removes any stray
// "/./" or duplicate separators that could otherwise sneak in.
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

bool BackUpWorker::copySettingsToTempDir(const QString& settingsDir, const QString& tempDirPath) {
#if defined(Q_OS_WIN)
    // Robocopy parses its own command line and will split any argument that
    // contains spaces. Two consequences:
    //   1. Every path we pass must already be in native separators.
    //   2. The /LOG: path must not contain spaces. The user's Documents
    //      folder often does, so put the log in the system temp dir which
    //      is guaranteed space-free.
    const QString robocopyLog = QDir::toNativeSeparators(
            QDir::tempPath() + "/mixxx-backup-robocopy.log");

    qDebug() << "[BackUp] -> [BackUpWorker] -> start creation tempdir (robocopy)";

    QProcess robocopy;
    robocopy.setProcessChannelMode(QProcess::MergedChannels);

    // Without this, a missing robocopy.exe would go unnoticed until
    // waitForFinished() returns false and we'd report a generic timeout.
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
                    "lut",  // exclude timecode lut
                    "/R:3", // retry 3 times if file is locked
                    "/W:2", // wait 2 seconds between retries
                    "/NP",  // progress display off
                    "/NFL", // no file list (keep log small)
                    "/NDL", // no dir list
                    "/LOG+:" + robocopyLog});

    if (!robocopy.waitForFinished(30000)) {
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
        // Dump the log to the Mixxx log so the user (and you) can actually
        // see what went wrong instead of just being told to check a file.
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
    // macOS and other platforms: use Qt's file copy. Slower but portable.
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

    if (m_upgradeBU) {
        backupDir = QStandardPaths::writableLocation(
                            QStandardPaths::DocumentsLocation) +
                "/Mixxx-BackUps/UpgradeBUs";
        archivePath = backupDir + "/MixxxSettings-Upgrade-" +
                currentMixxxVersion + "-" + timestamp;
    } else {
        backupDir = QStandardPaths::writableLocation(
                            QStandardPaths::DocumentsLocation) +
                "/Mixxx-BackUps";
        archivePath = backupDir + "/MixxxSettings-" + timestamp;
    }

    archivePath7zExt = archivePath + ".7z";
    archivePathZipExt = archivePath + ".zip";
    QDir().mkpath(backupDir);

#if defined(Q_OS_MACOS)
    // macOS has no 7z dependency; we use the system zip directly.
    zipExecutable = QStringLiteral("/usr/bin/zip");
#else
    // Windows & Linux: prefer an external 7z if the user has one installed.
    zipExecutable = findExternal7z();
#endif

    // ---------------------------------------------------------------
    // Stage 1: prepare the temp directory with a copy of the settings.
    // This is shared between the 7z path and the bit7z path.
    // ---------------------------------------------------------------
    QString tempBackupDir = archivePath + "_temp";

    if (!zipExecutable.isEmpty()) {
        qDebug() << "[BackUp] -> [BackUpWorker] -> 7z/Zip found in:" << zipExecutable;
        QDir().mkpath(tempBackupDir);

        if (!copySettingsToTempDir(settingsDir, tempBackupDir)) {
            QDir(tempBackupDir).removeRecursively();
            qCritical() << "[BackUp] -> [BackUpWorker] -> error creating tempdir";
            emit backUpFinished(false, "Backup failed: could not copy settings");
            return;
        }

#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
        QProcess process;
        process.setProcessChannelMode(QProcess::MergedChannels);
        // Pass the directory itself instead of "<dir>/*". 7z recurses into
        // directories on its own, and this avoids the wildcard-with-spaces
        // problem on Windows.
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
            // Fall through to bit7z below.
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

        // 7z failed. Do NOT return here — we want the bit7z fallback
        // below to actually run. The temp dir is still populated, so bit7z
        // only needs to compress it (no re-copy).
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

    // ---------------------------------------------------------------
    // Stage 2: bit7z fallback using the 7z.dll that ships next to
    // mixxx.exe. Reached when:
    //   (a) no external 7z executable was found at all, OR
    //   (b) an external 7z was found but failed.
    // In case (b) the temp dir was already populated in Stage 1 and we
    // must NOT re-copy; only create it if it doesn't exist yet.
    // ---------------------------------------------------------------
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
            // Only create + populate the temp dir if Stage 1 didn't
            // already do it. Stage 1 populates it whenever
            // copySettingsToTempDir() ran, i.e. whenever zipExecutable
            // was non-empty.
            if (zipExecutable.isEmpty()) {
                QDir().mkpath(tempBackupDir);
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
    const QString backupDir = QStandardPaths::writableLocation(
                                      QStandardPaths::DocumentsLocation) +
            "/Mixxx-BackUps";
    QDir dir(backupDir);
    dir.setNameFilters({"MixxxSettings-*.7z"});
    dir.setSorting(QDir::Time);

    const auto backups = dir.entryInfoList();
    for (int i = m_keepBackUps; i < backups.size(); ++i) {
        dir.remove(backups[i].fileName());
        emit backUpRemoved(backups[i].fileName());
    }
    // NOTE: upgrade backups live in Mixxx-BackUps/UpgradeBUs/ and are named
    // "MixxxSettings-Upgrade-...", so they are NOT matched by the filter
    // above and will accumulate forever. If you want them pruned too, add a
    // second pass over that subdirectory with the appropriate filter.
}

// #include "backupworker.h"
//
// #include <QCoreApplication>
// #include <QDateTime>
// #include <QDebug>
// #include <QDir>
// #include <QDirIterator>
// #include <QFile>
// #include <QProcess>
// #include <QStandardPaths>
// #include <QTemporaryDir>
//
//// Needs to be uncommented when bit7z and 7z are in the dependencies
//// and the parts in the CMakeLists are uncommented too.
// #include <bit7z/bit7z.hpp>
//
// #include "moc_backupworker.cpp"
//
// #if defined(Q_OS_WIN)
// #include <QSettings>
// #endif
//
// namespace {
//
// #if defined(Q_OS_WIN)
//// Look up the 7-Zip installation directory in the Windows registry.
//// 7-Zip writes its install path to HKLM\SOFTWARE\7-Zip\Path (and to the
//// 32-bit view on WOW64 systems). This is more reliable than hard-coding
//// C:\Program Files\7-Zip because users may install to a different drive
//// or a portable location that also registers itself.
// QString find7ZipFromRegistry() {
//     const QStringList registryKeys = {
//             QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\7-Zip"),
//             QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\7-Zip"),
//     };
//     for (const QString& key : registryKeys) {
//         QSettings reg(key, QSettings::NativeFormat);
//         const QString dir = reg.value(QStringLiteral("Path")).toString();
//         if (dir.isEmpty()) {
//             continue;
//         }
//         const QString exe = QDir(dir).filePath(QStringLiteral("7z.exe"));
//         if (QFile::exists(exe)) {
//             return exe;
//         }
//     }
//     return QString();
// }
// #endif
//
//// Locate an external 7z executable. Returns an empty string if none found.
// QString findExternal7z() {
// #if defined(Q_OS_WIN)
//     QString exe = QStandardPaths::findExecutable(QStringLiteral("7z"));
//     if (!exe.isEmpty()) {
//         return exe;
//     }
//     exe = find7ZipFromRegistry();
//     if (!exe.isEmpty()) {
//         return exe;
//     }
//     const QStringList winPaths = {
//             QDir::homePath() + "/scoop/apps/7zip/current/7z.exe",
//             QStringLiteral("C:\\Program Files\\7-Zip\\7z.exe"),
//             QStringLiteral("C:\\Program Files (x86)\\7-Zip\\7z.exe")};
//     for (const QString& path : winPaths) {
//         if (QFile::exists(path)) {
//             return path;
//         }
//     }
//     return QString();
// #elif defined(Q_OS_LINUX)
//     QString exe = QStandardPaths::findExecutable(QStringLiteral("7z"));
//     if (!exe.isEmpty()) {
//         return exe;
//     }
//     const QStringList linuxPaths = {
//             QStringLiteral("/usr/bin/7z"),
//             QStringLiteral("/usr/local/bin/7z"),
//             QStringLiteral("/bin/7z")};
//     for (const QString& path : linuxPaths) {
//         if (QFile::exists(path)) {
//             return path;
//         }
//     }
//     return QString();
// #else
//     // macOS and others: no external 7z lookup; caller falls back to bit7z
//     // (or, on macOS, to the built-in zip utility, depending on your wiring).
//     return QString();
// #endif
// }
//
// } // anonymous namespace
//
// BackUpWorker::BackUpWorker(
//         UserSettingsPointer config,
//         int keepBackUps,
//         bool upgradeBU,
//         QObject* parent)
//         : QObject(parent),
//           m_pConfig(config),
//           m_keepBackUps(keepBackUps),
//           m_upgradeBU(upgradeBU) {
//     currentMixxxVersion = m_pConfig->getValue(ConfigKey("[Config]",
//     "Version")); useBit7z = false;
// }
//
// bool BackUpWorker::copySettingsToTempDir(const QString& settingsDir, const
// QString& tempDirPath) { #if defined(Q_OS_WIN)
//     // Robocopy parses its own command line and will split any argument that
//     // contains spaces. Two consequences:
//     //   1. Every path we pass must already be in native separators.
//     //   2. The /LOG: path must not contain spaces. The user's Documents
//     //      folder often does, so put the log in the system temp dir which
//     //      is guaranteed space-free.
//     const QString robocopyLog = QDir::toNativeSeparators(
//             QDir::tempPath() + "/mixxx-backup-robocopy.log");
//
//     qDebug() << "[BackUp] -> [BackUpWorker] -> start creation tempdir
//     (robocopy)";
//
//     QProcess robocopy;
//     robocopy.setProcessChannelMode(QProcess::MergedChannels);
//
//     // Without this, a missing robocopy.exe would go unnoticed until
//     // waitForFinished() returns false and we'd report a generic timeout.
//     QObject::connect(&robocopy, &QProcess::errorOccurred,
//     [](QProcess::ProcessError e) {
//         qWarning() << "[BackUp] -> [BackUpWorker] -> robocopy process error:"
//         << e;
//     });
//
//     robocopy.start(QStringLiteral("robocopy"),
//             {QDir::toNativeSeparators(settingsDir),
//                     QDir::toNativeSeparators(tempDirPath),
//                     "/E", // copy subdirectories, including empty ones
//                     "/XD",
//                     "analysis", // exclude analysis
//                     "/XD",
//                     "lut",  // exclude timecode lut
//                     "/R:3", // retry 3 times if file is locked
//                     "/W:2", // wait 2 seconds between retries
//                     "/NP",  // progress display off
//                     "/NFL", // no file list (keep log small)
//                     "/NDL", // no dir list
//                     "/LOG+:" + robocopyLog});
//
//     if (!robocopy.waitForFinished(30000)) {
//         qCritical() << "[BackUp] -> [BackUpWorker] -> robocopy timed out! "
//                        "See log:"
//                     << robocopyLog;
//         return false;
//     }
//
//     // Robocopy exit codes are bitfields, NOT standard exit codes:
//     //   0 = no files copied, no failures
//     //   1 = files copied successfully
//     //   2 = extra files/dirs detected
//     //   4 = mismatched files/dirs
//     //   8 = some files/dirs could not be copied (copy errors)
//     //  16 = serious error, no copy performed
//     // Anything with the 8 or 16 bit set means a real failure.
//     const int rc = robocopy.exitCode();
//     if (rc >= 8) {
//         qCritical() << "[BackUp] -> [BackUpWorker] -> robocopy failed with
//         exit code"
//                     << rc << "- see log:" << robocopyLog;
//         // Dump the log to the Mixxx log so the user (and you) can actually
//         // see what went wrong instead of just being told to check a file.
//         QFile log(robocopyLog);
//         if (log.open(QIODevice::ReadOnly | QIODevice::Text)) {
//             qCritical().noquote() << log.readAll();
//         }
//         return false;
//     }
//     return true;
//
// #elif defined(Q_OS_LINUX)
//     qDebug() << "[BackUp] -> [BackUpWorker] -> start creation tempdir
//     (rsync)";
//
//     QProcess rsync;
//     rsync.setProgram(QStringLiteral("rsync"));
//     rsync.setArguments({"-a",
//             "--exclude=analysis/", // exclude analysis
//             "--exclude=lut/",      // exclude timecode lut
//             settingsDir + "/",
//             tempDirPath + "/"});
//     rsync.start();
//     rsync.waitForFinished();
//
//     qDebug() << "stdout:" << rsync.readAllStandardOutput();
//     qDebug() << "stderr:" << rsync.readAllStandardError();
//
//     if (rsync.exitCode() != 0) {
//         qCritical() << "[BackUp] -> [BackUpWorker] -> rsync failed! Exit
//         code:" << rsync.exitCode(); return false;
//     }
//     return true;
//
// #else
//     // macOS and other platforms: use Qt's file copy. Slower but portable.
//     QDirIterator it(settingsDir,
//             QDir::Files | QDir::NoDotAndDotDot,
//             QDirIterator::Subdirectories);
//
//     while (it.hasNext()) {
//         QString srcPath = it.next();
//         QString relativePath = QDir(settingsDir).relativeFilePath(srcPath);
//
//         // Skip analysis folders at any level.
//         if (relativePath.contains("analysis/") ||
//                 relativePath.startsWith("analysis/") ||
//                 relativePath.endsWith("/analysis")) {
//             continue;
//         }
//         // Skip lut folders at any level.
//         if (relativePath.contains("lut/") ||
//                 relativePath.startsWith("lut/") ||
//                 relativePath.endsWith("/lut")) {
//             continue;
//         }
//
//         QString destPath = tempDirPath + "/" + relativePath;
//         QFileInfo(destPath).dir().mkpath(".");
//         if (!QFile::copy(srcPath, destPath)) {
//             qCritical() << "Failed to copy file:" << srcPath << "to" <<
//             destPath; return false;
//         }
//     }
//     return true;
// #endif
// }
//
// void BackUpWorker::performBackUp() {
//     QString backupDir, archivePath, archivePath7zExt, archivePathZipExt,
//     zipExecutable; const QString settingsDir = m_pConfig->getSettingsPath();
//     const QString timestamp =
//     QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
//
//     if (m_upgradeBU) {
//         backupDir = QStandardPaths::writableLocation(
//                             QStandardPaths::DocumentsLocation) +
//                 "/Mixxx-BackUps/UpgradeBUs";
//         archivePath = backupDir + "/MixxxSettings-Upgrade-" +
//                 currentMixxxVersion + "-" + timestamp;
//     } else {
//         backupDir = QStandardPaths::writableLocation(
//                             QStandardPaths::DocumentsLocation) +
//                 "/Mixxx-BackUps";
//         archivePath = backupDir + "/MixxxSettings-" + timestamp;
//     }
//
//     archivePath7zExt = archivePath + ".7z";
//     archivePathZipExt = archivePath + ".zip";
//     QDir().mkpath(backupDir);
//
// #if defined(Q_OS_MACOS)
//     // macOS has no 7z dependency; we use the system zip directly.
//     zipExecutable = QStringLiteral("/usr/bin/zip");
// #else
//     // Windows & Linux: prefer an external 7z if the user has one installed.
//     zipExecutable = findExternal7z();
// #endif
//
//     // ---------------------------------------------------------------
//     // Stage 1: prepare the temp directory with a copy of the settings.
//     // This is shared between the 7z path and the bit7z path.
//     // ---------------------------------------------------------------
//     QString tempBackupDir = archivePath + "_temp";
//
//     if (!zipExecutable.isEmpty()) {
//         qDebug() << "[BackUp] -> [BackUpWorker] -> 7z/Zip found in:" <<
//         zipExecutable; QDir().mkpath(tempBackupDir);
//
//         if (!copySettingsToTempDir(settingsDir, tempBackupDir)) {
//             QDir(tempBackupDir).removeRecursively();
//             qCritical() << "[BackUp] -> [BackUpWorker] -> error creating
//             tempdir"; emit backUpFinished(false, "Backup failed: could not
//             copy settings"); return;
//         }
//
// #if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
//         QProcess process;
//         process.setProcessChannelMode(QProcess::MergedChannels);
//         // Pass the directory itself instead of "<dir>/*". 7z recurses into
//         // directories on its own, and this avoids the wildcard-with-spaces
//         // problem on Windows.
//         process.start(zipExecutable,
//                 {"a",
//                         "-t7z",
//                         archivePath7zExt,
//                         tempBackupDir,
//                         "-xr!analysis",
//                         "-xr!lut",
//                         "-mx=6"});
//
//         if (!process.waitForStarted(10000)) {
//             qCritical() << "[BackUp] -> [BackUpWorker] -> 7z failed to
//             start:"
//                         << process.errorString();
//             // Fall through to bit7z below.
//         } else if (!process.waitForFinished(300000)) {
//             qCritical() << "[BackUp] -> [BackUpWorker] -> 7z compression
//             timed out!"; process.kill();
//         } else if (process.exitCode() != 0) {
//             qCritical() << "[BackUp] -> [BackUpWorker] -> 7z failed:"
//                         << process.readAllStandardOutput();
//         } else {
//             qDebug() << "[BackUp] -> [BackUpWorker] -> Backup succeeded! "
//                         "Archive:"
//                      << archivePath7zExt;
//             QDir(tempBackupDir).removeRecursively();
//             emit backUpFinished(true, archivePath7zExt);
//             useBit7z = false;
//             return;
//         }
//
//         // 7z failed. Do NOT return here — we want the bit7z fallback
//         // below to actually run. The temp dir is still populated, so bit7z
//         // only needs to compress it (no re-copy).
//         qWarning() << "[BackUp] -> [BackUpWorker] -> 7z failed, "
//                       "falling back to bit7z";
//         useBit7z = true;
//
// #elif defined(Q_OS_MACOS)
//         QStringList arguments = {"-r",
//                 archivePathZipExt,
//                 settingsDir,
//                 "-x",
//                 settingsDir + "/analysis/*",
//                 "-x",
//                 settingsDir + "/lut/*"};
//         qDebug() << "[BackUp] -> [BackUpWorker] -> Executing:" <<
//         zipExecutable
//                  << arguments.join(" ");
//         bool started = QProcess::startDetached(zipExecutable, arguments);
//         if (started) {
//             qDebug() << "[BackUp] -> [BackUpWorker] -> MacOS zip backup "
//                         "started to:"
//                      << archivePathZipExt;
//             QDir(tempBackupDir).removeRecursively();
//             emit backUpFinished(true, archivePathZipExt);
//             return;
//         }
//         qWarning() << "[BackUp] -> [BackUpWorker] -> MacOS zip backup
//         failed."; useBit7z = true;
// #endif
//     } else {
//         qWarning() << "[BackUp] -> [BackUpWorker] -> 7z/Zip not found. "
//                       "-> using the shipped 7z.dll (bit7z).";
//         useBit7z = true;
//     }
//
//     // ---------------------------------------------------------------
//     // Stage 2: bit7z fallback using the 7z.dll that ships next to
//     // mixxx.exe. Reached when:
//     //   (a) no external 7z executable was found at all, OR
//     //   (b) an external 7z was found but failed.
//     // In case (b) the temp dir was already populated in Stage 1 and we
//     // must NOT re-copy; only create it if it doesn't exist yet.
//     // ---------------------------------------------------------------
//     if (useBit7z) {
//         qDebug() << "[BackUp] -> [BackUpWorker] -> Bit7z started";
//         emit progressChanged(0);
//
//         QString path7z;
// #if defined(Q_OS_WIN)
//         path7z = QCoreApplication::applicationDirPath() + "/7z.dll";
// #else
//         path7z = QCoreApplication::applicationDirPath() + "/7z";
// #endif
//
//         if (!QFile::exists(path7z)) {
//             qWarning() << "[BackUp] -> [BackUpWorker] -> shipped bit7z
//             library not found:"
//                        << path7z;
//             QDir(tempBackupDir).removeRecursively();
//             emit backUpFinished(false,
//                     QStringLiteral("Backup failed: no 7z backend available "
//                                    "(no external 7z, and shipped %1
//                                    missing)")
//                             .arg(path7z));
//             return;
//         }
//
//         try {
//             // Only create + populate the temp dir if Stage 1 didn't
//             // already do it. Stage 1 populates it whenever
//             // copySettingsToTempDir() ran, i.e. whenever zipExecutable
//             // was non-empty.
//             if (zipExecutable.isEmpty()) {
//                 QDir().mkpath(tempBackupDir);
//                 if (!copySettingsToTempDir(settingsDir, tempBackupDir)) {
//                     QDir(tempBackupDir).removeRecursively();
//                     emit errorOccurred(
//                             "Could not create temporary directory for
//                             backup.");
//                     emit backUpFinished(false, "Backup failed: tempdir");
//                     return;
//                 }
//             }
//
//             bit7z::Bit7zLibrary lib(path7z.toStdString());
//             bit7z::BitFileCompressor compressor(lib,
//             bit7z::BitFormat::SevenZip);
//
//             emit progressChanged(10);
//             compressor.compressDirectory(
//                     tempBackupDir.toStdString(),
//                     archivePath7zExt.toStdString());
//             emit progressChanged(80);
//             QDir(tempBackupDir).removeRecursively();
//
//             emit progressChanged(100);
//             qDebug() << "[BackUp] -> [BackUpWorker] -> Backup succeeded
//             (bit7z)! "
//                         "Archive:"
//                      << archivePath7zExt;
//             emit backUpFinished(true, archivePath7zExt);
//
//         } catch (const bit7z::BitException& ex) {
//             const QString msg = QString::fromStdString(ex.what());
//             emit errorOccurred(msg);
//             qCritical() << "[BackUp] -> [BackUpWorker] -> bit7z error:" <<
//             msg; QDir(tempBackupDir).removeRecursively(); emit
//             backUpFinished(false, "Backup failed: " + msg);
//         }
//
//         qDebug() << "[BackUpWorker] --> Bit7z ended";
//     }
// }
//
// void BackUpWorker::deleteOldBackUps() {
//     const QString backupDir = QStandardPaths::writableLocation(
//                                       QStandardPaths::DocumentsLocation) +
//             "/Mixxx-BackUps";
//     QDir dir(backupDir);
//     dir.setNameFilters({"MixxxSettings-*.7z"});
//     dir.setSorting(QDir::Time);
//
//     const auto backups = dir.entryInfoList();
//     for (int i = m_keepBackUps; i < backups.size(); ++i) {
//         dir.remove(backups[i].fileName());
//         emit backUpRemoved(backups[i].fileName());
//     }
//     // NOTE: upgrade backups live in Mixxx-BackUps/UpgradeBUs/ and are named
//     // "MixxxSettings-Upgrade-...", so they are NOT matched by the filter
//     // above and will accumulate forever. If you want them pruned too, add a
//     // second pass over that subdirectory with the appropriate filter.
// }

//
// #include "backupworker.h"
//
// #include <QCoreApplication>
// #include <QDateTime>
// #include <QDebug>
// #include <QDir>
// #include <QDirIterator>
// #include <QFile>
// #include <QProcess>
// #include <QStandardPaths>
// #include <QTemporaryDir>
//
//// Needs to be uncommented when bit7z and 7z are in the dependencies
//// and the parts in the CMakeLists are uncommented too.
// #include <bit7z/bit7z.hpp>
//
// #include "moc_backupworker.cpp"
//
// #if defined(Q_OS_WIN)
// #include <QSettings>
// #endif
//
// namespace {
//
// #if defined(Q_OS_WIN)
//// Look up the 7-Zip installation directory in the Windows registry.
//// 7-Zip writes its install path to HKLM\SOFTWARE\7-Zip\Path (and to the
//// 32-bit view on WOW64 systems). This is more reliable than hard-coding
//// C:\Program Files\7-Zip because users may install to a different drive
//// or a portable location that also registers itself.
// QString find7ZipFromRegistry() {
//     const QStringList registryKeys = {
//             QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\7-Zip"),
//             QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\7-Zip"),
//     };
//     for (const QString& key : registryKeys) {
//         QSettings reg(key, QSettings::NativeFormat);
//         const QString dir = reg.value(QStringLiteral("Path")).toString();
//         if (dir.isEmpty()) {
//             continue;
//         }
//         const QString exe = QDir(dir).filePath(QStringLiteral("7z.exe"));
//         if (QFile::exists(exe)) {
//             return exe;
//         }
//     }
//     return QString();
// }
// #endif
//
// } // anonymous namespace
//
// BackUpWorker::BackUpWorker(
//         UserSettingsPointer config,
//         int keepBackUps,
//         bool upgradeBU,
//         QObject* parent)
//         : QObject(parent),
//           m_pConfig(config),
//           m_keepBackUps(keepBackUps),
//           m_upgradeBU(upgradeBU) {
//     currentMixxxVersion = m_pConfig->getValue(ConfigKey("[Config]",
//     "Version")); useBit7z = false;
// }
//
// bool BackUpWorker::copySettingsToTempDir(const QString& settingsDir, const
// QString& tempDirPath) { #if defined(Q_OS_WIN)
//     // Robocopy parses its own command line and will split any argument that
//     // contains spaces. Two consequences:
//     //   1. Every path we pass must already be in native separators (done via
//     //      QDir::toNativeSeparators below).
//     //   2. The /LOG: path must not contain spaces. The user's Documents
//     //      folder often does ("C:\Users\First Last\Documents\..."), so put
//     //      the log in the system temp dir which is guaranteed space-free.
//     const QString robocopyLog = QDir::toNativeSeparators(
//             QDir::tempPath() + "/mixxx-backup-robocopy.log");
//
//     qDebug() << "[BackUp] -> [BackUpWorker] -> start creation tempdir
//     (robocopy)";
//
//     QProcess robocopy;
//     robocopy.setProcessChannelMode(QProcess::MergedChannels);
//
//     // Without this, a missing robocopy.exe would go unnoticed until
//     // waitForFinished() returns false and we'd report a generic timeout.
//     QObject::connect(&robocopy, &QProcess::errorOccurred,
//     [](QProcess::ProcessError e) {
//         qWarning() << "[BackUp] -> [BackUpWorker] -> robocopy process error:"
//         << e;
//     });
//
//     robocopy.start(QStringLiteral("robocopy"),
//             {QDir::toNativeSeparators(settingsDir),
//                     QDir::toNativeSeparators(tempDirPath),
//                     "/E", // copy subdirectories, including empty ones
//                     "/XD",
//                     "analysis", // exclude analysis
//                     "/XD",
//                     "lut",  // exclude timecode lut
//                     "/R:3", // retry 3 times if file is locked
//                     "/W:2", // wait 2 seconds between retries
//                     "/NP",  // progress display off
//                     "/NFL", // no file list (keep log small)
//                     "/NDL", // no dir list
//                     "/LOG+:" + robocopyLog});
//
//     if (!robocopy.waitForFinished(30000)) {
//         qCritical() << "[BackUp] -> [BackUpWorker] -> robocopy timed out! "
//                        "See log:"
//                     << robocopyLog;
//         return false;
//     }
//
//     // Robocopy exit codes are bitfields, NOT standard exit codes:
//     //   0 = no files copied, no failures
//     //   1 = files copied successfully
//     //   2 = extra files/dirs detected
//     //   4 = mismatched files/dirs
//     //   8 = some files/dirs could not be copied (copy errors)
//     //  16 = serious error, no copy performed
//     // Anything with the 8 or 16 bit set means a real failure.
//     const int rc = robocopy.exitCode();
//     if (rc >= 8) {
//         qCritical() << "[BackUp] -> [BackUpWorker] -> robocopy failed with
//         exit code"
//                     << rc << "- see log:" << robocopyLog;
//         // Dump the log to the Mixxx log so the user (and you) can actually
//         // see what went wrong instead of just being told to check a file.
//         QFile log(robocopyLog);
//         if (log.open(QIODevice::ReadOnly | QIODevice::Text)) {
//             qCritical().noquote() << log.readAll();
//         }
//         return false;
//     }
//     return true;
//
// #elif defined(Q_OS_LINUX)
//     qDebug() << "[BackUp] -> [BackUpWorker] -> start creation tempdir
//     (rsync)";
//
//     QProcess rsync;
//     rsync.setProgram(QStringLiteral("rsync"));
//     rsync.setArguments({"-a",
//             "--exclude=analysis/", // exclude analysis
//             "--exclude=lut/",      // exclude timecode lut
//             settingsDir + "/",
//             tempDirPath + "/"});
//     rsync.start();
//     rsync.waitForFinished();
//
//     qDebug() << "stdout:" << rsync.readAllStandardOutput();
//     qDebug() << "stderr:" << rsync.readAllStandardError();
//
//     if (rsync.exitCode() != 0) {
//         qCritical() << "[BackUp] -> [BackUpWorker] -> rsync failed! Exit
//         code:" << rsync.exitCode(); return false;
//     }
//     return true;
//
// #else
//     // macOS and other platforms: use Qt's file copy. Slower but portable.
//     QDirIterator it(settingsDir,
//             QDir::Files | QDir::NoDotAndDotDot,
//             QDirIterator::Subdirectories);
//
//     while (it.hasNext()) {
//         QString srcPath = it.next();
//         QString relativePath = QDir(settingsDir).relativeFilePath(srcPath);
//
//         // Skip analysis folders at any level.
//         if (relativePath.contains("analysis/") ||
//                 relativePath.startsWith("analysis/") ||
//                 relativePath.endsWith("/analysis")) {
//             continue;
//         }
//         // Skip lut folders at any level.
//         if (relativePath.contains("lut/") ||
//                 relativePath.startsWith("lut/") ||
//                 relativePath.endsWith("/lut")) {
//             continue;
//         }
//
//         QString destPath = tempDirPath + "/" + relativePath;
//         QFileInfo(destPath).dir().mkpath(".");
//         if (!QFile::copy(srcPath, destPath)) {
//             qCritical() << "Failed to copy file:" << srcPath << "to" <<
//             destPath; return false;
//         }
//     }
//     return true;
// #endif
// }
//
// void BackUpWorker::performBackUp() {
//     QString backupDir, archivePath, archivePath7zExt, archivePathZipExt,
//     zipExecutable; const QString settingsDir = m_pConfig->getSettingsPath();
//     const QString timestamp =
//     QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
//
//     if (m_upgradeBU) {
//         backupDir = QStandardPaths::writableLocation(
//                             QStandardPaths::DocumentsLocation) +
//                 "/Mixxx-BackUps/UpgradeBUs";
//         archivePath = backupDir + "/MixxxSettings-Upgrade-" +
//                 currentMixxxVersion + "-" + timestamp;
//     } else {
//         backupDir = QStandardPaths::writableLocation(
//                             QStandardPaths::DocumentsLocation) +
//                 "/Mixxx-BackUps";
//         archivePath = backupDir + "/MixxxSettings-" + timestamp;
//     }
//
//     archivePath7zExt = archivePath + ".7z";
//     archivePathZipExt = archivePath + ".zip";
//     QDir().mkpath(backupDir);
//
// #if defined(Q_OS_MACOS)
//     zipExecutable = QStringLiteral("/usr/bin/zip");
// #endif
//
// #if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
//     // Windows & Linux: use 7z if we can find it.
//     zipExecutable = QStandardPaths::findExecutable(QStringLiteral("7z"));
//
//     // If not found in PATH -> check additional locations.
//     if (zipExecutable.isEmpty()) {
// #if defined(Q_OS_WIN)
//         // Registry is the most reliable source — it tells us where 7-Zip
//         // was actually installed, regardless of PATH or drive letter.
//         zipExecutable = find7ZipFromRegistry();
// #endif
//     }
//
//     if (zipExecutable.isEmpty()) {
// #if defined(Q_OS_WIN)
//         const QStringList winPaths = {
//                 QDir::homePath() + "/scoop/apps/7zip/current/7z.exe",
//                 QStringLiteral("C:\\Program Files\\7-Zip\\7z.exe"),
//                 QStringLiteral("C:\\Program Files (x86)\\7-Zip\\7z.exe")};
//         for (const QString& path : winPaths) {
//             if (QFile::exists(path)) {
//                 zipExecutable = path;
//                 break;
//             }
//         }
// #elif defined(Q_OS_LINUX)
//         const QStringList linuxPaths = {
//                 QStringLiteral("/usr/bin/7z"),
//                 QStringLiteral("/usr/local/bin/7z"),
//                 QStringLiteral("/bin/7z")};
//         for (const QString& path : linuxPaths) {
//             if (QFile::exists(path)) {
//                 zipExecutable = path;
//                 break;
//             }
//         }
// #endif
//     }
// #endif
//
//     // ---------------------------------------------------------------
//     // Stage 1: prepare the temp directory with a copy of the settings.
//     // This is shared between the 7z path and the bit7z path.
//     // ---------------------------------------------------------------
//     QString tempBackupDir = archivePath + "_temp";
//
//     if (!zipExecutable.isEmpty()) {
//         qDebug() << "[BackUp] -> [BackUpWorker] -> 7z/Zip found in:" <<
//         zipExecutable; QDir().mkpath(tempBackupDir);
//
//         if (!copySettingsToTempDir(settingsDir, tempBackupDir)) {
//             QDir(tempBackupDir).removeRecursively();
//             qCritical() << "[BackUp] -> [BackUpWorker] -> error creating
//             tempdir"; emit backUpFinished(false, "Backup failed: could not
//             copy settings"); return;
//         }
//
// #if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
//         QProcess process;
//         process.setProcessChannelMode(QProcess::MergedChannels);
//
//         // Pass the directory itself instead of "<dir>/*". 7z recurses into
//         // directories on its own, and this avoids the wildcard-with-spaces
//         // problem on Windows.
//         process.start(zipExecutable,
//                 {"a",
//                         "-t7z",
//                         archivePath7zExt,
//                         tempBackupDir,
//                         "-xr!analysis",
//                         "-xr!lut",
//                         "-mx=6"});
//
//         if (!process.waitForStarted(10000)) {
//             qCritical() << "[BackUp] -> [BackUpWorker] -> 7z failed to
//             start:"
//                         << process.errorString();
//             // Fall through to bit7z below.
//         } else if (!process.waitForFinished(300000)) {
//             qCritical() << "[BackUp] -> [BackUpWorker] -> 7z compression
//             timed out!"; process.kill();
//         } else if (process.exitCode() != 0) {
//             qCritical() << "[BackUp] -> [BackUpWorker] -> 7z failed:"
//                         << process.readAllStandardOutput();
//         } else {
//             qDebug() << "[BackUp] -> [BackUpWorker] -> Backup succeeded! "
//                         "Archive:"
//                      << archivePath7zExt;
//             QDir(tempBackupDir).removeRecursively();
//             emit backUpFinished(true, archivePath7zExt);
//             return;
//         }
//
//         // 7z failed. Do NOT return here — we want the bit7z fallback
//         // below to actually run. The temp dir is still populated, so bit7z
//         // only needs to compress it (no re-copy).
//         qWarning() << "[BackUp] -> [BackUpWorker] -> 7z failed, "
//                       "falling back to bit7z";
//         useBit7z = true;
//
// #elif defined(Q_OS_MACOS)
//         QStringList arguments = {"-r",
//                 archivePathZipExt,
//                 settingsDir,
//                 "-x",
//                 settingsDir + "/analysis/*",
//                 "-x",
//                 settingsDir + "/lut/*"};
//         qDebug() << "[BackUp] -> [BackUpWorker] -> Executing:" <<
//         zipExecutable
//                  << arguments.join(" ");
//         bool started = QProcess::startDetached(zipExecutable, arguments);
//         if (started) {
//             qDebug() << "[BackUp] -> [BackUpWorker] -> MacOS zip backup "
//                         "started to:"
//                      << archivePathZipExt;
//             QDir(tempBackupDir).removeRecursively();
//             emit backUpFinished(true, archivePathZipExt);
//             return;
//         }
//         qWarning() << "[BackUp] -> [BackUpWorker] -> MacOS zip backup
//         failed."; useBit7z = true;
// #endif
//     } else {
//         qWarning() << "[BackUp] -> [BackUpWorker] -> 7z/Zip not found in PATH
//         "
//                       "or fallback locations. "
//                       "-> using 7zip bit7z from internal library.";
//         useBit7z = true;
//     }
//
//     // ---------------------------------------------------------------
//     // Stage 2: bit7z fallback. Reached when:
//     //   (a) no 7z executable was found at all, OR
//     //   (b) 7z was found but failed.
//     // In case (b) the temp dir was already populated in Stage 1 and we
//     // must NOT re-copy; only create it if it doesn't exist yet.
//     // ---------------------------------------------------------------
//     if (useBit7z) {
//         qDebug() << "[BackUp] -> [BackUpWorker] -> Bit7z started";
//         emit progressChanged(0);
//
//         QString path7z;
// #if defined(Q_OS_WIN)
//         path7z = QCoreApplication::applicationDirPath() + "/7z.dll";
// #else
//         path7z = QCoreApplication::applicationDirPath() + "/7z";
// #endif
//
//         if (!QFile::exists(path7z)) {
//             qWarning() << "[BackUp] -> [BackUpWorker] -> bit7z library not
//             found:"
//                        << path7z;
//             QDir(tempBackupDir).removeRecursively();
//             emit backUpFinished(false,
//                     QStringLiteral("Backup failed: 7z backend not available
//                     (missing %1)")
//                             .arg(path7z));
//             return;
//         }
//
//         try {
//             QTemporaryDir tempDir;
//             if (tempDir.isValid()) {
//                 // Only create + populate the temp dir if Stage 1 didn't
//                 // already do it. Stage 1 populates it whenever
//                 // copySettingsToTempDir() ran, i.e. whenever zipExecutable
//                 // was non-empty.
//                 if (zipExecutable.isEmpty()) {
//                     QDir().mkpath(tempBackupDir);
//                     if (!copySettingsToTempDir(settingsDir, tempBackupDir)) {
//                         QDir(tempBackupDir).removeRecursively();
//                         emit errorOccurred(
//                                 "Could not create temporary directory for
//                                 backup.");
//                         emit backUpFinished(false, "Backup failed: tempdir");
//                         return;
//                     }
//                 }
//
//                 bit7z::Bit7zLibrary lib(path7z.toStdString());
//                 bit7z::BitFileCompressor compressor(lib,
//                 bit7z::BitFormat::SevenZip);
//
//                 emit progressChanged(10);
//                 compressor.compressDirectory(
//                         tempBackupDir.toStdString(),
//                         archivePath7zExt.toStdString());
//                 emit progressChanged(80);
//                 QDir(tempBackupDir).removeRecursively();
//             } else {
//                 qWarning() << "[BackUp] -> [BackUpWorker] -> QTemporaryDir
//                 invalid:"
//                            << tempDir.errorString();
//                 QDir(tempBackupDir).removeRecursively();
//                 emit backUpFinished(false, "Backup failed: tempdir");
//                 return;
//             }
//
//             emit progressChanged(100);
//             emit backUpFinished(true, archivePath7zExt);
//
//         } catch (const bit7z::BitException& ex) {
//             const QString msg = QString::fromStdString(ex.what());
//             emit errorOccurred(msg);
//             qDebug() << "[BackUp] -> [BackUpWorker] -> bit7z error:" << msg;
//             QDir(tempBackupDir).removeRecursively();
//             emit backUpFinished(false, "Backup failed: " + msg);
//         }
//
//         qDebug() << "[BackUpWorker] --> Bit7z ended";
//     }
// }
//
// void BackUpWorker::deleteOldBackUps() {
//     const QString backupDir = QStandardPaths::writableLocation(
//                                       QStandardPaths::DocumentsLocation) +
//             "/Mixxx-BackUps";
//     QDir dir(backupDir);
//     dir.setNameFilters({"MixxxSettings-*.7z"});
//     dir.setSorting(QDir::Time);
//
//     const auto backups = dir.entryInfoList();
//     for (int i = m_keepBackUps; i < backups.size(); ++i) {
//         dir.remove(backups[i].fileName());
//         emit backUpRemoved(backups[i].fileName());
//     }
//     // NOTE: upgrade backups live in Mixxx-BackUps/UpgradeBUs/ and are named
//     // "MixxxSettings-Upgrade-...", so they are NOT matched by the filter
//     // above and will accumulate forever. If you want them pruned too, add a
//     // second pass over that subdirectory with the appropriate filter.
// }
//
////#include "backupworker.h"
////
////#include <QCoreApplication>
////#include <QDateTime>
////#include <QDebug>
////#include <QDir>
////#include <QDirIterator>
////#include <QProcess>
////#include <QStandardPaths>
////#include <QTemporaryDir>
////// Needs to be uncommented when bit7z and 7z are in the dependencies
////// and th parts in the CMakeLists are uncommented too.
////#include <bit7z/bit7z.hpp>
////
////#include "moc_backupworker.cpp"
////// #include "registercodecs.cpp"
////// #include "registerhashers.cpp"
////
////// extern "C" void RegisterCodecs();
////// extern "C" void RegisterHashers();
////
////BackUpWorker::BackUpWorker(
////        UserSettingsPointer config,
////        int keepBackUps,
////        bool upgradeBU,
////        QObject* parent)
////        : QObject(parent),
////          m_pConfig(config),
////          m_keepBackUps(keepBackUps),
////          m_upgradeBU(upgradeBU) {
////    currentMixxxVersion = m_pConfig->getValue(ConfigKey("[Config]",
///"Version")); /    useBit7z = false;
////}
////
////bool BackUpWorker::copySettingsToTempDir(const QString& settingsDir, const
/// QString& tempDirPath) {
//////#if defined(Q_OS_WIN)
//////    QProcess robocopy;
//////    qDebug() << "[BackUp] -> [BAckUpWorker] -> start creation tempdir
/// robocopy/rsync";
//////    robocopy.start("robocopy",
//////            {QDir::toNativeSeparators(settingsDir),
//////                    QDir::toNativeSeparators(tempDirPath),
//////                    "/E", // show dirs
//////                    "/XD",
//////                    "analysis", // exclude analysis
//////                    "/XD",
//////                    "lut",     // exclude timecode lut
//////                    "/R:3",    // retry 3 times if file is locked
//////                    "/W:2",    // wait 2 seconds between retries
//////                    "/NP",     // progress display off
//////                    "/LOG+:" + // write log file -> can be quoted later
//////                            QDir::toNativeSeparators(
//////                                    tempDirPath + "_robocopy.log")});
//////
//////    if (!robocopy.waitForFinished(30000) || robocopy.exitCode() >= 8) {
//////        qCritical()
//////                << "[BackUp] -> [BAckUpWorker] -> File copy failed! Check
/// log:"
//////                << tempDirPath + "_robocopy.log";
//////        return false;
//////    }
//////    return true;
//////
//////#elif defined(Q_OS_LINUX)
//////    QProcess rsync;
//////    rsync.setProgram("rsync");
//////    rsync.setArguments({"-a",
//////            "--exclude=analysis/", // exclude analysis
//////            "--exclude=lut/",      // exclude timecode lut
//////            settingsDir + "/",
//////            tempDirPath + "/"});
//////    rsync.start();
//////    rsync.waitForFinished();
//////
//////    qDebug() << "stdout:" << rsync.readAllStandardOutput();
//////    qDebug() << "stderr:" << rsync.readAllStandardError();
//////
//////    if (rsync.exitCode() != 0) {
//////        qCritical() << "[BackUp] -> [BackUpWorker] -> rsync failed! Exit
/// code:" << rsync.exitCode();
//////        return false;
//////    }
//////    return true;
//////#else
//////    // For macOS or other platforms, use Qt's file copy
//////    QDirIterator it(settingsDir,
//////            QDir::Files | QDir::NoDotAndDotDot,
//////            QDirIterator::Subdirectories);
//////
//////    while (it.hasNext()) {
//////        QString srcPath = it.next();
//////        QString relativePath =
/// QDir(settingsDir).relativeFilePath(srcPath);
//////
//////        // Skip analysis folders at any level
//////        if (relativePath.contains("analysis/") ||
//////                relativePath.startsWith("analysis/") ||
//////                relativePath.endsWith("/analysis")) {
//////            continue;
//////        }
//////        // Skip lut folders at any level
//////        if (relativePath.contains("lut/") ||
//////                relativePath.startsWith("lut/") ||
//////                relativePath.endsWith("/lut")) {
//////            continue;
//////        }
//////
//////        QString destPath = tempDirPath + "/" + relativePath;
//////        QFileInfo(destPath).dir().mkpath(".");
//////        if (!QFile::copy(srcPath, destPath)) {
//////            qCritical() << "Failed to copy file:" << srcPath << "to" <<
/// destPath;
//////            return false;
//////        }
//////    }
//////    return true;
//////#endif
////
//////#if defined(Q_OS_WIN)
//////// Write the robocopy log next to the temp dir but in a location that
//////// is guaranteed not to contain spaces, and quote the /LOG: path so
//////// robocopy's own command-line parser doesn't split it.
//////const QString robocopyLog = QDir::toNativeSeparators(
//////        QDir::tempPath() + "/mixxx-backup-robocopy.log");
//////
//////QProcess robocopy;
//////qDebug() << "[BackUp] -> [BackUpWorker] -> start creation tempdir
/// robocopy/rsync";
//////robocopy.start("robocopy",
//////        {QDir::toNativeSeparators(settingsDir),
//////                QDir::toNativeSeparators(tempDirPath),
//////                "/E", // copy subdirectories, including empty ones
//////                "/XD",
//////                "analysis", // exclude analysis
//////                "/XD",
//////                "lut",  // exclude timecode lut
//////                "/R:3", // retry 3 times if file is locked
//////                "/W:2", // wait 2 seconds between retries
//////                "/NP",  // progress display off
//////                "/NFL", // no file list (keep log small)
//////                "/NDL", // no dir list
//////                "/LOG+:" + robocopyLog});
//////
//////if (!robocopy.waitForFinished(30000)) {
//////    qCritical() << "[BackUp] -> [BackUpWorker] -> robocopy timed out! "
//////                   "See log:"
//////                << robocopyLog;
//////    return false;
//////}
//////
//////// Robocopy exit codes are bitfields, NOT standard exit codes:
////////   0 = no files copied, no failures
////////   1 = files copied successfully
////////   2 = extra files/dirs detected
////////   4 = mismatched files/dirs
////////   8 = some files/dirs could not be copied (copy errors)
////////  16 = serious error, no copy performed
//////// Anything with the 8 or 16 bit set means a real failure.
//////const int rc = robocopy.exitCode();
//////if (rc >= 8) {
//////    qCritical() << "[BackUp] -> [BackUpWorker] -> robocopy failed with
/// exit code"
//////                << rc << "- see log:" << robocopyLog;
//////    // Dump the tail of the log to the Mixxx log so the user (and you)
//////    // can actually see what went wrong instead of just "check log".
//////    QFile log(robocopyLog);
//////    if (log.open(QIODevice::ReadOnly | QIODevice::Text)) {
//////        qCritical().noquote() << log.readAll();
//////    }
//////    return false;
//////}
//////return true;
//////#endif
////
////#if defined(Q_OS_WIN)
////// Write the robocopy log next to the temp dir but in a location that
////// is guaranteed not to contain spaces, and quote the /LOG: path so
////// robocopy's own command-line parser doesn't split it.
////const QString robocopyLog = QDir::toNativeSeparators(
////        QDir::tempPath() + "/mixxx-backup-robocopy.log");
////
////QProcess robocopy;
////qDebug() << "[BackUp] -> [BackUpWorker] -> start creation tempdir
/// robocopy/rsync"; /robocopy.start("robocopy", /
///{QDir::toNativeSeparators(settingsDir), /
/// QDir::toNativeSeparators(tempDirPath), /                "/E", // copy
/// subdirectories, including empty ones /                "/XD", / "analysis", //
/// exclude analysis /                "/XD", /                "lut",  // exclude
/// timecode lut /                "/R:3", // retry 3 times if file is locked /
///"/W:2", // wait 2 seconds between retries /                "/NP",  //
/// progress display off /                "/NFL", // no file list (keep log
/// small) /                "/NDL", // no dir list /                "/LOG+:" +
/// robocopyLog});
////
////if (!robocopy.waitForFinished(30000)) {
////    qCritical() << "[BackUp] -> [BackUpWorker] -> robocopy timed out! "
////                   "See log:"
////                << robocopyLog;
////    return false;
////}
////
////// Robocopy exit codes are bitfields, NOT standard exit codes:
//////   0 = no files copied, no failures
//////   1 = files copied successfully
//////   2 = extra files/dirs detected
//////   4 = mismatched files/dirs
//////   8 = some files/dirs could not be copied (copy errors)
//////  16 = serious error, no copy performed
////// Anything with the 8 or 16 bit set means a real failure.
////const int rc = robocopy.exitCode();
////if (rc >= 8) {
////    qCritical() << "[BackUp] -> [BackUpWorker] -> robocopy failed with exit
/// code" /                << rc << "- see log:" << robocopyLog; /    // Dump the
/// tail of the log to the Mixxx log so the user (and you) /    // can actually
/// see what went wrong instead of just "check log". /    QFile log(robocopyLog);
////    if (log.open(QIODevice::ReadOnly | QIODevice::Text)) {
////        qCritical().noquote() << log.readAll();
////    }
////    return false;
////}
////return true;
////#endif
////
////}
////
//////void BackUpWorker::performBackUp() {
//////    QString backupDir, archivePath, archivePath7zExt, archivePathZipExt,
/// zipExecutable;
//////    const QString settingsDir = m_pConfig->getSettingsPath();
//////    const QString timestamp =
/// QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
//////
//////    if (m_upgradeBU) {
//////        backupDir = QStandardPaths::writableLocation(
//////                            QStandardPaths::DocumentsLocation) +
//////                "/Mixxx-BackUps/UpgradeBUs";
//////        archivePath = backupDir + "/MixxxSettings-Upgrrade-" +
//////                currentMixxxVersion + "-" + timestamp;
//////    } else {
//////        backupDir = QStandardPaths::writableLocation(
//////                            QStandardPaths::DocumentsLocation) +
//////                "/Mixxx-BackUps";
//////        archivePath = backupDir + "/MixxxSettings-" + timestamp;
//////    }
//////
//////    archivePath7zExt = archivePath + ".7z";
//////    archivePathZipExt = archivePath + ".zip";
//////    QDir().mkpath(backupDir);
//////
//////#if defined(Q_OS_MACOS)
//////    zipExecutable = "/usr/bin/zip";
//////#endif
//////
//////#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
//////    // Windows & Linux: use 7z
//////    zipExecutable = QStandardPaths::findExecutable("7z");
//////
//////    // If not found in PATH -> check additional locations
//////    if (zipExecutable.isEmpty()) {
//////#if defined(Q_OS_WIN)
//////        const QStringList winPaths = {
//////                QDir::homePath() + "/scoop/apps/7zip/current/7z.exe",
//////                "C:\\Program Files\\7-Zip\\7z.exe",
//////                "C:\\Program Files (x86)\\7-Zip\\7z.exe"};
//////        for (const QString& path : winPaths) {
//////            if (QFile::exists(path)) {
//////                zipExecutable = path;
//////                break;
//////            }
//////        }
//////#elif defined(Q_OS_LINUX)
//////        // Linux fallback paths (unchanged)
//////        const QStringList linuxPaths = {"/usr/bin/7z",
///"/usr/local/bin/7z", "/bin/7z"};
//////        for (const QString& path : linuxPaths) {
//////            if (QFile::exists(path)) {
//////                zipExecutable = path;
//////                break;
//////            }
//////        }
//////#endif
//////    }
//////#endif
//////
//////    if (!zipExecutable.isEmpty()) {
//////        qDebug() << "[BackUp] -> [BAckUpWorker] -> 7z/Zip found in: " <<
/// zipExecutable;
//////        QString tempBackupDir = archivePath + "_temp";
//////        QDir().mkpath(tempBackupDir);
//////
//////        if (!copySettingsToTempDir(settingsDir, tempBackupDir)) {
//////            QDir(tempBackupDir).removeRecursively();
//////            qDebug() << "[BackUp] -> [BAckUpWorker] -> error creating
/// tempdir";
//////            return;
//////        }
//////
//////#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
//////        //QProcess process;
//////        //process.setProcessChannelMode(QProcess::MergedChannels);
//////        //process.start(zipExecutable,
//////        //        {"a",
//////        //                "-t7z",
//////        //                archivePath7zExt,
//////        //                tempBackupDir + "/*",
//////        //                "-xr!analysis",
//////        //                "-xr!lut",
//////        //                "-mx=6"});
//////        QProcess process;
//////        process.setProcessChannelMode(QProcess::MergedChannels);
//////        process.setWorkingDirectory(tempBackupDir);
//////        //process.start(zipExecutable,
//////        //        {"a",
//////        //                "-t7z",
//////        //                archivePath7zExt,
//////        //                "*", // relative to working dir, no spaces
/// involved
//////        //                "-xr!analysis",
//////        //                "-xr!lut",
//////        //                "-mx=6"});
//////
//////        process.start(zipExecutable,
//////                {"a",
//////                        "-t7z",
//////                        archivePath7zExt,
//////                        tempBackupDir, // 7z accepts a directory; it
/// recurses
//////                        "-xr!analysis",
//////                        "-xr!lut",
//////                        "-mx=6"});
//////
//////        if (!process.waitForFinished(300000)) {
//////            qCritical() << "[BackUp] -> [BAckUpWorker] -> 7z compression
/// timed out!";
//////            process.kill();
//////            useBit7z = true;
//////        } else if (process.exitCode() != 0) {
//////            qCritical() << "[BackUp] -> [BAckUpWorker] -> 7z failed:"
//////                        << process.readAllStandardOutput();
//////            useBit7z = true;
//////        } else {
//////            qDebug() << "[BackUp] -> [BAckUpWorker] -> Backup succeeded! "
//////                        "Archive:"
//////                     << archivePath7zExt;
//////            // emit backUpFinished(true, archivePath);
//////            emit backUpFinished(true, archivePath7zExt);
//////            useBit7z = false;
//////        }
//////
//////        QDir(tempBackupDir).removeRecursively();
//////        return;
//////
//////#elif defined(Q_OS_MACOS)
//////        QStringList arguments = {"-r",
//////                archivePathZipExt,
//////                settingsDir,
//////                "-x",
//////                settingsDir + "/analysis/*",
//////                "-x",
//////                settingsDir + "/lut/*"};
//////        qDebug() << "[BackUp] -> [BAckUpWorker] -> Executing:" <<
/// zipExecutable
//////                 << arguments.join(" ");
//////        bool started = QProcess::startDetached(zipExecutable, arguments);
//////        if (started) {
//////            qDebug() << "[BackUp] -> [BAckUpWorker] -> MacOS zip backup "
//////                        "started to:"
//////                     << archivePathZipExt;
//////            //emit backUpFinished(true, archivePath);
//////            emit backUpFinished(true, archivePath7zExt);
//////        } else {
//////            qWarning() << "[BackUp] -> [BAckUpWorker] -> MacOS zip backup
/// failed.";
//////            useBit7z = true;
//////        }
//////        return;
//////#endif
//////
//////    } else {
//////        qWarning() << "[BackUp] -> [BAckUpWorker] -> 7z/Zip not found in
/// PATH "
//////                      "or fallback locations. "
//////                      "-> using  7zip bit7z from internal library.";
//////        useBit7z = true;
//////    }
//////
//////    if (useBit7z) {
//////        // All next lines need to be uncommented when bit7z and 7z are in
/// the dependencies
//////        // and the parts in the CMakeLists are uncommented too.
//////        qDebug() << "[BackUp] -> [BAckUpWorker] -> Bit7z started";
//////        QString path7z;
//////#if defined(Q_OS_WIN)
//////        path7z = QCoreApplication::applicationDirPath() + "/7z.dll";
//////#else
//////        path7z = QCoreApplication::applicationDirPath() + "/7z";
//////#endif
//////        emit progressChanged(0);
//////        if (!QFile::exists(path7z)) {
//////            qWarning() << "7z not found:" << path7z;
//////            emit backUpFinished(false, "Backup failed");
//////            return;
//////        }
//////        try {
//////            // bit7z::Bit7zLibrary lib("7zip.dll");
//////            // bit7z::Bit7zLibrary lib("7z.dll");
//////
//////            QTemporaryDir tempDir;
//////            if (tempDir.isValid()) {
//////                QString tempBackupDir = archivePath + "_temp";
//////                QDir().mkpath(tempBackupDir);
//////
//////                if (!copySettingsToTempDir(settingsDir, tempBackupDir)) {
//////                    emit errorOccurred("Could not create temporary
/// directory for backup.");
//////                    emit backUpFinished(false, "Backup failed");
//////                    return;
//////                }
//////
//////                bit7z::Bit7zLibrary lib(path7z.toStdString());
//////                // bit7z::Bit7zLibrary lib;
//////                // bit7z::Bit7zLibrary lib("7zip.dll");
//////                bit7z::BitFileCompressor compressor(lib,
/// bit7z::BitFormat::SevenZip);
//////
//////                archivePath7zExt = archivePath + ".7z";
//////                emit progressChanged(10);
//////                compressor.compressDirectory(
//////                        tempBackupDir.toStdString(),
//////                        archivePath7zExt.toStdString());
//////                emit progressChanged(80);
//////                QDir(tempBackupDir).removeRecursively();
//////            }
//////
//////            emit progressChanged(100);
//////            emit backUpFinished(true, archivePath);
//////
//////        } catch (const bit7z::BitException& ex) {
//////            emit errorOccurred(QString::fromStdString(ex.what()));
//////            qDebug() << "[BackUp] -> [BAckUpWorker] -> error " <<
/// QString::fromStdString(ex.what());
//////            emit backUpFinished(false, "Backup failed");
//////        }
//////
//////        qDebug() << "[BAckUpWorker] --> Bit7z ended";
//////    }
//////}
////
////void BackUpWorker::performBackUp() {
////    QString backupDir, archivePath, archivePath7zExt, archivePathZipExt,
/// zipExecutable; /    const QString settingsDir = m_pConfig->getSettingsPath();
////    const QString timestamp =
/// QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
////
////    if (m_upgradeBU) {
////        backupDir = QStandardPaths::writableLocation(
////                            QStandardPaths::DocumentsLocation) +
////                "/Mixxx-BackUps/UpgradeBUs";
////        archivePath = backupDir + "/MixxxSettings-Upgrrade-" +
////                currentMixxxVersion + "-" + timestamp;
////    } else {
////        backupDir = QStandardPaths::writableLocation(
////                            QStandardPaths::DocumentsLocation) +
////                "/Mixxx-BackUps";
////        archivePath = backupDir + "/MixxxSettings-" + timestamp;
////    }
////
////    archivePath7zExt = archivePath + ".7z";
////    archivePathZipExt = archivePath + ".zip";
////    QDir().mkpath(backupDir);
////
////#if defined(Q_OS_MACOS)
////    zipExecutable = "/usr/bin/zip";
////#endif
////
////#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
////    // Windows & Linux: use 7z
////    zipExecutable = QStandardPaths::findExecutable("7z");
////
////    // If not found in PATH -> check additional locations
////    if (zipExecutable.isEmpty()) {
////#if defined(Q_OS_WIN)
////        const QStringList winPaths = {
////                QDir::homePath() + "/scoop/apps/7zip/current/7z.exe",
////                "C:\\Program Files\\7-Zip\\7z.exe",
////                "C:\\Program Files (x86)\\7-Zip\\7z.exe"};
////        for (const QString& path : winPaths) {
////            if (QFile::exists(path)) {
////                zipExecutable = path;
////                break;
////            }
////        }
////#elif defined(Q_OS_LINUX)
////        const QStringList linuxPaths = {"/usr/bin/7z", "/usr/local/bin/7z",
///"/bin/7z"}; /        for (const QString& path : linuxPaths) { /            if
///(QFile::exists(path)) { /                zipExecutable = path; / break; / }
////        }
////#endif
////    }
////#endif
////
////    // ---------------------------------------------------------------
////    // Stage 1: prepare the temp directory with a copy of the settings.
////    // This is shared between the 7z path and the bit7z path.
////    // ---------------------------------------------------------------
////    QString tempBackupDir = archivePath + "_temp";
////
////    if (!zipExecutable.isEmpty()) {
////        qDebug() << "[BackUp] -> [BackUpWorker] -> 7z/Zip found in:" <<
/// zipExecutable; /        QDir().mkpath(tempBackupDir);
////
////        if (!copySettingsToTempDir(settingsDir, tempBackupDir)) {
////            QDir(tempBackupDir).removeRecursively();
////            qCritical() << "[BackUp] -> [BackUpWorker] -> error creating
/// tempdir"; /            emit backUpFinished(false, "Backup failed"); / return;
////        }
////
////#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
////        QProcess process;
////        process.setProcessChannelMode(QProcess::MergedChannels);
////        // Pass the directory itself instead of "<dir>/*". 7z recurses into
////        // directories on its own, and this avoids the wildcard-with-spaces
////        // problem on Windows.
////        process.start(zipExecutable,
////                {"a",
////                        "-t7z",
////                        archivePath7zExt,
////                        tempBackupDir,
////                        "-xr!analysis",
////                        "-xr!lut",
////                        "-mx=6"});
////
////        bool sevenZipOk = false;
////        if (!process.waitForFinished(300000)) {
////            qCritical() << "[BackUp] -> [BackUpWorker] -> 7z compression
/// timed out!"; /            process.kill(); /        } else if
///(process.exitCode() != 0) { /            qCritical() << "[BackUp] ->
///[BackUpWorker] -> 7z failed:" /                        <<
/// process.readAllStandardOutput(); /        } else { /            sevenZipOk =
/// true; /        }
////
////        if (sevenZipOk) {
////            qDebug() << "[BackUp] -> [BackUpWorker] -> Backup succeeded! "
////                        "Archive:"
////                     << archivePath7zExt;
////            QDir(tempBackupDir).removeRecursively();
////            emit backUpFinished(true, archivePath7zExt);
////            return;
////        }
////
////        // *** THE FIX ***
////        // 7z failed. Do NOT return here — we want the bit7z fallback
////        // below to actually run. Just signal that we're falling through.
////        qWarning() << "[BackUp] -> [BackUpWorker] -> 7z failed, "
////                      "falling back to bit7z";
////        useBit7z = true;
////
////#elif defined(Q_OS_MACOS)
////        QStringList arguments = {"-r",
////                archivePathZipExt,
////                settingsDir,
////                "-x",
////                settingsDir + "/analysis/*",
////                "-x",
////                settingsDir + "/lut/*"};
////        qDebug() << "[BackUp] -> [BackUpWorker] -> Executing:" <<
/// zipExecutable /                 << arguments.join(" "); /        bool started
///= QProcess::startDetached(zipExecutable, arguments); /        if (started) {
////            qDebug() << "[BackUp] -> [BackUpWorker] -> MacOS zip backup "
////                        "started to:"
////                     << archivePathZipExt;
////            emit backUpFinished(true, archivePathZipExt);
////            return;
////        }
////        qWarning() << "[BackUp] -> [BackUpWorker] -> MacOS zip backup
/// failed."; /        useBit7z = true;
////#endif
////    } else {
////        qWarning() << "[BackUp] -> [BackUpWorker] -> 7z/Zip not found in
/// PATH " /                      "or fallback locations. " / "-> using 7zip
/// bit7z from internal library."; /        useBit7z = true; /    }
////
////    // ---------------------------------------------------------------
////    // Stage 2: bit7z fallback. Reached when:
////    //   (a) no 7z executable was found at all, OR
////    //   (b) 7z was found but failed (the fix above).
////    // In case (b) the temp dir was already populated in Stage 1 and we
////    // must NOT re-copy; only create it if it doesn't exist yet.
////    // ---------------------------------------------------------------
////    if (useBit7z) {
////        qDebug() << "[BackUp] -> [BackUpWorker] -> Bit7z started";
////        QString path7z;
////#if defined(Q_OS_WIN)
////        path7z = QCoreApplication::applicationDirPath() + "/7z.dll";
////#else
////        path7z = QCoreApplication::applicationDirPath() + "/7z";
////#endif
////        emit progressChanged(0);
////        if (!QFile::exists(path7z)) {
////            qWarning() << "7z not found:" << path7z;
////            QDir(tempBackupDir).removeRecursively();
////            emit backUpFinished(false, "Backup failed");
////            return;
////        }
////        try {
////            QTemporaryDir tempDir;
////            if (tempDir.isValid()) {
////                // Only create + populate if Stage 1 didn't already do it.
////                // Stage 1 populates it whenever copySettingsToTempDir ran,
////                // which happens whenever zipExecutable was non-empty.
////                if (zipExecutable.isEmpty()) {
////                    QDir().mkpath(tempBackupDir);
////                    if (!copySettingsToTempDir(settingsDir, tempBackupDir))
///{ /                        emit errorOccurred( / "Could not create temporary
/// directory for backup."); / QDir(tempBackupDir).removeRecursively(); / emit
/// backUpFinished(false, "Backup failed"); /                        return; / }
////                }
////
////                bit7z::Bit7zLibrary lib(path7z.toStdString());
////                bit7z::BitFileCompressor compressor(lib,
/// bit7z::BitFormat::SevenZip);
////
////                emit progressChanged(10);
////                compressor.compressDirectory(
////                        tempBackupDir.toStdString(),
////                        archivePath7zExt.toStdString());
////                emit progressChanged(80);
////                QDir(tempBackupDir).removeRecursively();
////            }
////
////            emit progressChanged(100);
////            emit backUpFinished(true, archivePath7zExt);
////
////        } catch (const bit7z::BitException& ex) {
////            emit errorOccurred(QString::fromStdString(ex.what()));
////            qDebug() << "[BackUp] -> [BackUpWorker] -> error "
////                     << QString::fromStdString(ex.what());
////            QDir(tempBackupDir).removeRecursively();
////            emit backUpFinished(false, "Backup failed");
////        }
////
////        qDebug() << "[BackUpWorker] --> Bit7z ended";
////    }
////}
////
////
////void BackUpWorker::deleteOldBackUps() {
////    const QString backupDir = QStandardPaths::writableLocation(
////                                      QStandardPaths::DocumentsLocation) +
////            "/Mixxx-BackUps";
////    QDir dir(backupDir);
////    dir.setNameFilters({"MixxxSettings-*.7z"});
////    dir.setSorting(QDir::Time);
////
////    const auto backups = dir.entryInfoList();
////    for (int i = m_keepBackUps; i < backups.size(); ++i) {
////        dir.remove(backups[i].fileName());
////        // dir.remove(backups[i].fileName().replace(".7z",
///"_temp_robocopy.log")); /        emit backUpRemoved(backups[i].fileName());
////    }
////}

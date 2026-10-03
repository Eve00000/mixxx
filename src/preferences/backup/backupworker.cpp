#include "backupworker.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryFile>

// Uncomment when bit7z and 7z are active in dependencies & CMakeLists
#include <bit7z/bit7z.hpp>

#include "moc_backupworker.cpp"

#if defined(Q_OS_WIN)
#include <QSettings>
#endif

namespace {

#if defined(Q_OS_WIN)
// Look up HKLM\SOFTWARE\7-Zip\Path in the Windows registry instead of hardcoding install locations.
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

#if !defined(Q_OS_MACOS)
// Locate an external 7z executable, returns empty string if not found.
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
            QDir::homePath() + QStringLiteral("/scoop/apps/7zip/current/7z.exe"),
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
    return QString();
#endif
}
#endif

// Returns the path to the 7z library dynamic module next to the application executable.
QString shippedBit7zLibraryPath() {
#if defined(Q_OS_WIN)
    const QString fileName = QStringLiteral("7z.dll");
#else
    const QString fileName = QStringLiteral("7z");
#endif
    const QDir appDir(QCoreApplication::applicationDirPath());
    return QDir::cleanPath(appDir.filePath(fileName));
}

const QStringList kExcludedFolders = {
        QStringLiteral("analysis"),
        QStringLiteral("lut"),
        QStringLiteral("samples"),
        QStringLiteral("bpmcurve"),
        QStringLiteral("keycurve"),
        QStringLiteral("fingerprints")};

} // anonymous namespace

BackupWorker::BackupWorker(
        UserSettingsPointer config,
        int keepBackups,
        bool upgradeBu,
        QObject* parent)
        : QObject(parent),
          m_pConfig(config),
          m_keepBackups(keepBackups),
          m_upgradeBu(upgradeBu) {
    currentMixxxVersion = m_pConfig->getValue(ConfigKey("[Config]", "Version"));
    useBit7z = false;
}

// Helper for Windows: resolve the user's Documents folder.
// Standardized on writing test files to handle localized Win32 aliases (e.g. Dutch "Documenten").
QString BackupWorker::resolveDocumentsDir() {
    auto isWritableDir = [](const QString& dir) -> bool {
        if (dir.isEmpty() || !QDir(dir).exists()) {
            return false;
        }
        QTemporaryFile probe(QDir(dir).filePath(QStringLiteral("mixxx-write-test-XXXXXX")));
        return probe.open();
    };

    const QString standard = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (isWritableDir(standard)) {
        return standard;
    }
    qWarning() << "[Backup] -> [BackupWorker] -> DocumentsLocation"
               << standard << "not writable, trying fallbacks";

    const QString homeDocuments = QDir(QDir::homePath()).filePath(QStringLiteral("Documents"));
    if (isWritableDir(homeDocuments)) {
        qWarning() << "[Backup] -> [BackupWorker] -> using" << homeDocuments;
        return homeDocuments;
    }

#if defined(Q_OS_WIN)
    QString systemRoot = QDir::rootPath();
    while (systemRoot.endsWith('/')) {
        systemRoot.chop(1);
    }
    if (systemRoot.isEmpty()) {
        systemRoot = QStringLiteral("C:");
    }
#else
    QString systemRoot = QDir::rootPath();
#endif
    qWarning() << "[Backup] -> [BackupWorker] -> falling back to system disk root:" << systemRoot;
    return systemRoot;
}

bool BackupWorker::copySettingsToTempDir(const QString& settingsDir, const QString& tempDirPath) {
#if defined(Q_OS_WIN)
    const QString robocopyLog = QDir::toNativeSeparators(
            QDir::tempPath() + QStringLiteral("/mixxx-backup-robocopy.log"));

    qDebug() << "[Backup] -> [BackupWorker] -> start creation tempdir (robocopy)";

    QProcess robocopy;
    robocopy.setProcessChannelMode(QProcess::MergedChannels);

    QObject::connect(&robocopy, &QProcess::errorOccurred, [](QProcess::ProcessError e) {
        qWarning() << "[Backup] -> [BackupWorker] -> robocopy process error:" << e;
    });

    QStringList robocopyArgs = {
            QDir::toNativeSeparators(settingsDir),
            QDir::toNativeSeparators(tempDirPath),
            QStringLiteral("/E")};

    for (const QString& folder : kExcludedFolders) {
        robocopyArgs << QStringLiteral("/XD") << folder;
    }

    robocopyArgs << QStringLiteral("/R:3")
                 << QStringLiteral("/W:2")
                 << QStringLiteral("/NP")
                 << QStringLiteral("/NFL")
                 << QStringLiteral("/NDL")
                 << QStringLiteral("/LOG+:") + robocopyLog;

    robocopy.start(QStringLiteral("robocopy"), robocopyArgs);

    if (!robocopy.waitForFinished(300000)) {
        qCritical() << "[Backup] -> [BackupWorker] -> robocopy timed out! See log:" << robocopyLog;
        return false;
    }

    const int rc = robocopy.exitCode();
    if (rc >= 8) {
        qCritical() << "[Backup] -> [BackupWorker] -> robocopy failed with exit code"
                    << rc << "- see log:" << robocopyLog;
        QFile log(robocopyLog);
        if (log.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qCritical().noquote() << log.readAll();
        }
        return false;
    }
    return true;

#elif defined(Q_OS_LINUX)
    qDebug() << "[Backup] -> [BackupWorker] -> start creation tempdir (rsync)";

    QProcess rsync;
    rsync.setProgram(QStringLiteral("rsync"));

    QStringList rsyncArgs = {QStringLiteral("-a")};
    for (const QString& folder : kExcludedFolders) {
        rsyncArgs << QStringLiteral("--exclude=") + folder + QStringLiteral("/");
    }
    rsyncArgs << settingsDir + QStringLiteral("/") << tempDirPath + QStringLiteral("/");

    rsync.setArguments(rsyncArgs);
    rsync.start();
    rsync.waitForFinished();

    qDebug() << "stdout:" << rsync.readAllStandardOutput();
    qDebug() << "stderr:" << rsync.readAllStandardError();

    if (rsync.exitCode() != 0) {
        qCritical() << "[Backup] -> [BackupWorker] -> rsync failed! Exit code:" << rsync.exitCode();
        return false;
    }
    return true;

#else
    // Fallback: Pure Qt File Copy (e.g. macOS)
    QDirIterator it(settingsDir, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);

    while (it.hasNext()) {
        const QString srcPath = it.next();
        const QString relativePath = QDir(settingsDir).relativeFilePath(srcPath);

        bool skip = false;
        for (const QString& folder : kExcludedFolders) {
            if (relativePath.contains(folder + QStringLiteral("/")) ||
                    relativePath.startsWith(folder + QStringLiteral("/")) ||
                    relativePath.endsWith(QStringLiteral("/") + folder)) {
                skip = true;
                break;
            }
        }
        if (skip) {
            continue;
        }

        const QString destPath = QDir(tempDirPath).filePath(relativePath);
        QFileInfo(destPath).dir().mkpath(QStringLiteral("."));
        if (!QFile::copy(srcPath, destPath)) {
            qCritical() << "Failed to copy file:" << srcPath << "to" << destPath;
            return false;
        }
    }
    return true;
#endif
}

void BackupWorker::performBackup() {
    QString backupDir;
    QString archivePath;
    QString zipExecutable;

    const QString settingsDir = m_pConfig->getSettingsPath();
    const QString timestamp = QDateTime::currentDateTime().toString(
            QStringLiteral("yyyyMMdd-HHmmss"));
    const QString documentsDir = resolveDocumentsDir();

    if (m_upgradeBu) {
        backupDir = QDir(documentsDir).filePath(QStringLiteral("Mixxx-Backups/UpgradeBUs"));
        archivePath = QDir(backupDir).filePath(
                QStringLiteral("MixxxSettings-Upgrade-") + currentMixxxVersion +
                QStringLiteral("-") + timestamp);
    } else {
        backupDir = QDir(documentsDir).filePath(QStringLiteral("Mixxx-Backups"));
        archivePath = QDir(backupDir).filePath(QStringLiteral("MixxxSettings-") + timestamp);
    }

    const QString archivePath7zExt = archivePath + QStringLiteral(".7z");
    const QString archivePathZipExt = archivePath + QStringLiteral(".zip");

    qDebug() << "[Backup] -> [BackupWorker] -> documentsDir:" << documentsDir;
    qDebug() << "[Backup] -> [BackupWorker] -> backupDir:" << backupDir;

    if (!QDir().mkpath(backupDir)) {
        qCritical() << "[Backup] -> [BackupWorker] -> could not create backup dir:" << backupDir;
        emit backupFinished(false,
                QStringLiteral("Backup failed: could not create %1")
                        .arg(backupDir));
        return;
    }

#if defined(Q_OS_MACOS)
    zipExecutable = QStringLiteral("/usr/bin/zip");
#else
    zipExecutable = findExternal7z();
#endif

    const QString tempBackupDir = archivePath + QStringLiteral("_temp");

    if (!zipExecutable.isEmpty()) {
        qDebug() << "[Backup] -> [BackupWorker] -> 7z/Zip found in:" << zipExecutable;
        qDebug() << "[Backup] -> [BackupWorker] -> tempBackupDir:" << tempBackupDir;

        if (!QDir().mkpath(tempBackupDir)) {
            qCritical() << "[Backup] -> [BackupWorker] -> could not create "
                           "temp dir:"
                        << tempBackupDir;
            emit backupFinished(false,
                    QStringLiteral("Backup failed: could not create %1")
                            .arg(tempBackupDir));
            return;
        }

        if (!copySettingsToTempDir(settingsDir, tempBackupDir)) {
            QDir(tempBackupDir).removeRecursively();
            qCritical() << "[Backup] -> [BackupWorker] -> error creating tempdir";
            emit backupFinished(false, QStringLiteral("Backup failed: could not copy settings"));
            return;
        }

#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
        QProcess process;
        process.setProcessChannelMode(QProcess::MergedChannels);
        process.start(zipExecutable,
                {QStringLiteral("a"),
                        QStringLiteral("-t7z"),
                        archivePath7zExt,
                        tempBackupDir,
                        QStringLiteral("-xr!analysis"),
                        QStringLiteral("-xr!lut"),
                        QStringLiteral("-mx=6")});

        if (!process.waitForStarted(10000)) {
            qCritical() << "[Backup] -> [BackupWorker] -> 7z failed to start:"
                        << process.errorString();
        } else if (!process.waitForFinished(300000)) {
            qCritical() << "[Backup] -> [BackupWorker] -> 7z compression timed out!";
            process.kill();
        } else if (process.exitCode() != 0) {
            qCritical() << "[Backup] -> [BackupWorker] -> 7z failed:"
                        << process.readAllStandardOutput();
        } else {
            qDebug() << "[Backup] -> [BackupWorker] -> Backup succeeded! "
                        "Archive:"
                     << archivePath7zExt;
            QDir(tempBackupDir).removeRecursively();
            emit backupFinished(true, archivePath7zExt);
            useBit7z = false;
            return;
        }

        qWarning() << "[Backup] -> [BackupWorker] -> 7z failed, falling back to bit7z";
        useBit7z = true;

#elif defined(Q_OS_MACOS)
        const QStringList arguments = {
                QStringLiteral("-r"),
                archivePathZipExt,
                settingsDir,
                QStringLiteral("-x"),
                settingsDir + QStringLiteral("/analysis/*"),
                QStringLiteral("-x"),
                settingsDir + QStringLiteral("/lut/*")};

        qDebug() << "[Backup] -> [BackupWorker] -> Executing:" << zipExecutable
                 << arguments.join(" ");
        if (QProcess::startDetached(zipExecutable, arguments)) {
            qDebug() << "[Backup] -> [BackupWorker] -> MacOS zip backup "
                        "started to:"
                     << archivePathZipExt;
            QDir(tempBackupDir).removeRecursively();
            emit backupFinished(true, archivePathZipExt);
            return;
        }
        qWarning() << "[Backup] -> [BackupWorker] -> MacOS zip backup failed.";
        useBit7z = true;
#endif
    } else {
        qWarning() << "[Backup] -> [BackupWorker] -> 7z/Zip not found -> using shipped bit7z.";
        useBit7z = true;
    }

    if (useBit7z) {
        qDebug() << "[Backup] -> [BackupWorker] -> Bit7z started";
        emit progressChanged(0);

        const QString path7z = shippedBit7zLibraryPath();

        if (!QFile::exists(path7z)) {
            qWarning() << "[Backup] -> [BackupWorker] -> shipped bit7z library "
                          "not found:"
                       << path7z;
            QDir(tempBackupDir).removeRecursively();
            emit backupFinished(false,
                    QStringLiteral("Backup failed: no 7z backend available (no "
                                   "external 7z, and shipped %1 missing)")
                            .arg(path7z));
            return;
        }

        try {
            if (zipExecutable.isEmpty()) {
                qDebug() << "[Backup] -> [BackupWorker] -> tempBackupDir (bit7z):" << tempBackupDir;
                if (!QDir().mkpath(tempBackupDir)) {
                    qCritical() << "[Backup] -> [BackupWorker] -> could not "
                                   "create temp dir:"
                                << tempBackupDir;
                    emit backupFinished(false,
                            QStringLiteral("Backup failed: could not create %1")
                                    .arg(tempBackupDir));
                    return;
                }
                if (!copySettingsToTempDir(settingsDir, tempBackupDir)) {
                    QDir(tempBackupDir).removeRecursively();
                    emit errorOccurred(
                            QStringLiteral("Could not create temporary "
                                           "directory for backup."));
                    emit backupFinished(false, QStringLiteral("Backup failed: tempdir"));
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
            qDebug() << "[Backup] -> [BackupWorker] -> Backup succeeded "
                        "(bit7z)! Archive:"
                     << archivePath7zExt;
            emit backupFinished(true, archivePath7zExt);

        } catch (const bit7z::BitException& ex) {
            const QString msg = QString::fromStdString(ex.what());
            emit errorOccurred(msg);
            qCritical() << "[Backup] -> [BackupWorker] -> bit7z error:" << msg;
            QDir(tempBackupDir).removeRecursively();
            emit backupFinished(false, QStringLiteral("Backup failed: ") + msg);
        }

        qDebug() << "[BackupWorker] --> Bit7z ended";
    }
}

void BackupWorker::deleteOldBackups() {
    const QString backupDir = QDir(resolveDocumentsDir()).filePath(QStringLiteral("Mixxx-Backups"));
    QDir dir(backupDir);
    dir.setNameFilters({QStringLiteral("MixxxSettings-*.7z")});
    dir.setSorting(QDir::Time);

    if (m_keepBackups == 0) {
        qDebug() << "[Backup] -> [BackupWorker] -> keeping all backups (m_keepBackups=0)";
        return;
    }

    const auto backups = dir.entryInfoList();
    for (int i = m_keepBackups; i < backups.size(); ++i) {
        dir.remove(backups[i].fileName());
        emit backupRemoved(backups[i].fileName());
    }
}

// #include "backupworker.h"
//
// #include <QCoreApplication>
// #include <QDateTime>
// #include <QDebug>
// #include <QDir>
// #include <QDirIterator>
// #include <QFile>
// #include <QStandardPaths>
// #include <QTemporaryDir>
// #include <QTemporaryFile>
// #include <QProcess>
//
// #include "moc_backupworker.cpp"
//
// #if defined(Q_OS_MACOS)
// #include <QProcess>
// #else
//// bit7z headers for Windows and Linux
// #include <bit7z/bit7z.hpp>
// #endif
//
// #if defined(Q_OS_WIN)
// #include <QSettings>
// #endif
//
// namespace {
//
// #if defined(Q_OS_WIN)
//// instead of searching for a 7-Zip installation directory
//// like C:\Program Files\7-Zip
//// -> look up HKLM\SOFTWARE\7-Zip\Path in the Windows registry.
// QString find7ZipLibraryFromRegistry() {
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
//         const QString dll = QDir(dir).filePath(QStringLiteral("7z.dll"));
//         if (QFile::exists(dll)) {
//             return dll;
//         }
//     }
//     return QString();
// }
// #endif
//
// #if defined(Q_OS_MACOS)
//// not needed on macOS: use the built-in zip utility
// #else
//// Locate the 7z dynamic library (7z.dll / 7zip.dll / 7z.so) for bit7z
// QString find7ZipLibrary() {
// #if defined(Q_OS_WIN)
//     // Check local build directory / application directory first (shipped
//     7zip.dll) const QDir appDir(QCoreApplication::applicationDirPath());
//     const QStringList localDlls = {
//             appDir.filePath(QStringLiteral("7zip.dll")),
//             appDir.filePath(QStringLiteral("7z.dll"))};
//     for (const QString& dll : localDlls) {
//         if (QFile::exists(dll)) {
//             return dll;
//         }
//     }
//
//     // Fall back to registry lookup
//     const QString regDll = find7ZipLibraryFromRegistry();
//     if (!regDll.isEmpty()) {
//         return regDll;
//     }
//
//     // Fall back to standard Win installation folders
//     const QStringList winPaths = {
//             QStringLiteral("C:\\Program Files\\7-Zip\\7z.dll"),
//             QStringLiteral("C:\\Program Files (x86)\\7-Zip\\7z.dll")};
//     for (const QString& path : winPaths) {
//         if (QFile::exists(path)) {
//             return path;
//         }
//     }
////#elif defined(Q_OS_LINUX)
////    const QStringList linuxPaths = {
////            QStringLiteral("/usr/lib/7z.so"),
////            QStringLiteral("/usr/lib/x86_64-linux-gnu/7z.so"),
////            QStringLiteral("/usr/local/lib/7z.so"),
////
/// QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("7z.so"))};
////    for (const QString& path : linuxPaths) {
////        if (QFile::exists(path)) {
////            return path;
////        }
////    }
////#endif
//
// #elif defined(Q_OS_LINUX)
//    const QString appDir = QCoreApplication::applicationDirPath();
//    const QStringList linuxPaths = {
//            // Standard host paths
//            QStringLiteral("/usr/lib/7z.so"),
//            QStringLiteral("/usr/lib/x86_64-linux-gnu/7z.so"),
//            QStringLiteral("/usr/lib/p7zip/7z.so"),
//            QStringLiteral("/usr/libexec/p7zip/7z.so"),
//            QStringLiteral("/usr/local/lib/7z.so"),
//
//            // AppImage bundled paths (relative to application bin directory)
//            QDir(appDir).filePath(QStringLiteral("7z.so")),
//            QDir(appDir).filePath(QStringLiteral("../lib/7z.so")),
//            QDir(appDir).filePath(QStringLiteral("../lib/p7zip/7z.so")),
//
//            // Flatpak sandbox paths
//            QStringLiteral("/app/lib/7z.so"),
//            QStringLiteral("/app/lib/p7zip/7z.so"),
//            QStringLiteral("/app/libexec/p7zip/7z.so")};
//    for (const QString& path : linuxPaths) {
//        if (QFile::exists(path)) {
//            return path;
//        }
//    }
// #endif
//
//    return QString();
//}
// #endif
//
//} // anonymous namespace
//
// BackupWorker::BackupWorker(
//        UserSettingsPointer config,
//        int keepBackups,
//        bool upgradeBu,
//        QObject* parent)
//        : QObject(parent),
//          m_pConfig(config),
//          m_keepBackups(keepBackups),
//          m_upgradeBu(upgradeBu) {
//    currentMixxxVersion = m_pConfig->getValue(ConfigKey("[Config]",
//    "Version"));
//}
//
//// Helper for Windows: resolve the users Documents folder
//// -> localized Windows installs must return correct path
//// -> QStandardPaths::DocumentsLocation may return a localized
////    display name like "Documenten" in NL
//// -> Robocopy fails on the translated name
//// -> If that happens -> fall back to the English folder name
////    under the user's home directory.
//// A folder is only usable if we can actually write a file into it.
//// QDir::exists() returns true for localized shell aliases like
//// "Documenten" NL -> problem for Win32 tools (robocopy)
//// -> real file test
// QString BackupWorker::resolveDocumentsDir() {
//     auto isWritableDir = [](const QString& dir) -> bool {
//         if (dir.isEmpty() || !QDir(dir).exists()) {
//             return false;
//         }
//         QTemporaryFile
//         probe(QDir(dir).filePath(QStringLiteral("mixxx-write-test-XXXXXX")));
//         return probe.open();
//     };
//
//     // The standard Documents location.
//     // -> On English Windows this is normally "C:/Users/<user>/Documents".
//     const QString standard = QStandardPaths::writableLocation(
//             QStandardPaths::DocumentsLocation);
//     if (isWritableDir(standard)) {
//         return standard;
//     }
//     qWarning() << "[Backup] -> [BackupWorker] -> DocumentsLocation"
//                << standard << "not writable, trying fallbacks";
//
//     // "Documents" under the user's home.
//     //  This is what the real folder is named on disk even
//     const QString homeDocuments = QDir::homePath() + "/Documents";
//     if (isWritableDir(homeDocuments)) {
//         qWarning() << "[Backup] -> [BackupWorker] -> using" << homeDocuments;
//         return homeDocuments;
//     }
//
//     // Last resort: root of the system disk (C:\)
//     // Always writable for the current user via UAC-compatible locations
//     // independent of any localization.
// #if defined(Q_OS_WIN)
//     QString systemRoot = QDir::rootPath();
//     while (systemRoot.endsWith('/')) {
//         systemRoot.chop(1);
//     }
//     if (systemRoot.isEmpty()) {
//         systemRoot = QStringLiteral("C:");
//     }
// #else
//     QString systemRoot = QDir::rootPath(); // "/"
// #endif
//     qWarning() << "[Backup] -> [BackupWorker] -> falling back to system disk
//     root:"
//                << systemRoot;
//     return systemRoot;
// }
//
// bool BackupWorker::copySettingsToTempDir(const QString& settingsDir, const
// QString& tempDirPath) { #if defined(Q_OS_WIN)
//     // Robocopy parses its own command line
//     // -> split any argument that contains spaces
//     const QString robocopyLog = QDir::toNativeSeparators(
//             QDir::tempPath() + "/mixxx-backup-robocopy.log");
//
//     qDebug() << "[BackUp] -> [BackUpWorker] -> start creation tempdir
//     (robocopy)";
//
//     QProcess robocopy;
//     robocopy.setProcessChannelMode(QProcess::MergedChannels);
//
//     // Check for robocopy.exe
//     QObject::connect(&robocopy, &QProcess::errorOccurred,
//     [](QProcess::ProcessError e) {
//         qWarning() << "[Backup] -> [BackupWorker] -> robocopy process error:"
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
//                     "lut", // exclude timecode lut
//                     "/XD",
//                     "samples", // exclude samples
//                     "/XD",
//                     "bpmcurve", // exclude bpmcurve
//                     "/XD",
//                     "keycurve", // exclude keycurve
//                     "/XD",
//                     "fingerprints", // exclude keycurve
//                     "/R:3",         // retry 3 times if file is locked
//                     "/W:2",         // wait 2 seconds between retries
//                     "/NP",          // progress display off
//                     "/NFL",         // no file list (keep log small)
//                     "/NDL",         // no dir list
//                     "/LOG+:" + robocopyLog});
//
//     if (!robocopy.waitForFinished(300000)) {
//         qCritical() << "[Backup] -> [BackupWorker] -> robocopy timed out! "
//                        "See log:"
//                     << robocopyLog;
//         return false;
//     }
//
//     // Robocopy exit codes are bitfields, NOT standard exit codes:
//     //    0 = no files copied, no failures
//     //    1 = files copied successfully
//     //    2 = extra files/dirs detected
//     //    4 = mismatched files/dirs
//     //    8 = some files/dirs could not be copied (copy errors)
//     //   16 = serious error, no copy performed
//     // Anything with the 8 or 16 bit set means a real failure.
//     const int rc = robocopy.exitCode();
//     if (rc >= 8) {
//         qCritical() << "[Backup] -> [BackupWorker] -> robocopy failed with
//         exit code"
//                     << rc << "- see log:" << robocopyLog;
//         // Dump the log to the Mixxx log
//         QFile log(robocopyLog);
//         if (log.open(QIODevice::ReadOnly | QIODevice::Text)) {
//             qCritical().noquote() << log.readAll();
//         }
//         return false;
//     }
//     return true;
//
// #elif defined(Q_OS_LINUX)
//     qDebug() << "[Backup] -> [BackupWorker] -> start creation tempdir
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
//         qCritical() << "[Backup] -> [BackupWorker] -> rsync failed! Exit
//         code:" << rsync.exitCode(); return false;
//     }
//     return true;
//
// #else
//     // macos ea: use Qts file copy
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
//         // Skip samples folders at any level.
//         if (relativePath.contains("samples/") ||
//                 relativePath.startsWith("samples/") ||
//                 relativePath.endsWith("/samples")) {
//             continue;
//         }
//         // Skip bpmcurve folders at any level.
//         if (relativePath.contains("bpmcurve/") ||
//                 relativePath.startsWith("bpmcurve/") ||
//                 relativePath.endsWith("/bpmcurve")) {
//             continue;
//         }
//         // Skip keycurve folders at any level.
//         if (relativePath.contains("keycurve/") ||
//                 relativePath.startsWith("keycurve/") ||
//                 relativePath.endsWith("/keycurve")) {
//             continue;
//         }
//         // Skip fingerprints folders at any level.
//         if (relativePath.contains("fingerprints/") ||
//                 relativePath.startsWith("fingerprints/") ||
//                 relativePath.endsWith("/fingerprints")) {
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
// void BackupWorker::performBackup() {
//     QString backupDir, archivePath, archivePath7zExt, archivePathZipExt;
//     const QString settingsDir = m_pConfig->getSettingsPath();
//     const QString timestamp =
//     QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
//
//     // Resolve the Documents folder in a way that works on localized
//     // Windows installs. QStandardPaths::DocumentsLocation can return a
//     // display name that robocopy cannot resolve.
//     const QString documentsDir = resolveDocumentsDir();
//
//     if (m_upgradeBu) {
//         backupDir = documentsDir + "/Mixxx-Backups/UpgradeBUs";
//         archivePath = backupDir + "/MixxxSettings-Upgrade-" +
//                 currentMixxxVersion + "-" + timestamp;
//     } else {
//         backupDir = documentsDir + "/Mixxx-Backups";
//         archivePath = backupDir + "/MixxxSettings-" + timestamp;
//     }
//
//     archivePath7zExt = archivePath + ".7z";
//     archivePathZipExt = archivePath + ".zip";
//
//     qDebug() << "[Backup] -> [BackupWorker] -> documentsDir:" <<
//     documentsDir; qDebug() << "[Backup] -> [BackupWorker] -> backupDir:" <<
//     backupDir;
//
//     // Ensure the backup directory exists
//     if (!QDir().mkpath(backupDir)) {
//         qCritical() << "[Backup] -> [BackupWorker] -> could not create backup
//         dir:"
//                     << backupDir;
//         emit backupFinished(false,
//                 QStringLiteral("Backup failed: could not create
//                 %1").arg(backupDir));
//         return;
//     }
//
//     ///////////////////////////////////////////////////////////////////
//     // Phase 1: create the temp directory with a copy of the settings
//     ///////////////////////////////////////////////////////////////////
//     QString tempBackupDir = archivePath + "_temp";
//
// #if defined(Q_OS_MACOS)
//     // macOS uses the native /usr/bin/zip system tool
//     if (!QDir().mkpath(tempBackupDir)) {
//         qCritical() << "[Backup] -> [BackupWorker] -> could not create temp
//         dir:" << tempBackupDir; emit backupFinished(false,
//         QStringLiteral("Backup failed: could not create
//         %1").arg(tempBackupDir)); return;
//     }
//
//     if (!copySettingsToTempDir(settingsDir, tempBackupDir)) {
//         QDir(tempBackupDir).removeRecursively();
//         emit backupFinished(false, "Backup failed: could not copy settings");
//         return;
//     }
//
//     emit progressChanged(10);
//
//     const QString zipExecutable = QStringLiteral("/usr/bin/zip");
//     QStringList arguments = {"-r", archivePathZipExt, tempBackupDir, "-x",
//     tempBackupDir + "/analysis/*", "-x", tempBackupDir + "/lut/*"};
//
//     QProcess zipProcess;
//     zipProcess.start(zipExecutable, arguments);
//     if (zipProcess.waitForFinished() && zipProcess.exitCode() == 0) {
//         QDir(tempBackupDir).removeRecursively();
//         emit progressChanged(100);
//         emit backupFinished(true, archivePathZipExt);
//         return;
//     }
//
//     QDir(tempBackupDir).removeRecursively();
//     emit backupFinished(false, "Backup failed: MacOS zip backup failed");
//     return;
//
// #else
//     // Windows & Linux: Use bit7z with 7zip.dll / 7z.dll / 7z.so
//     const QString sevenZipLib = find7ZipLibrary();
//
//     if (sevenZipLib.isEmpty()) {
//         qCritical() << "[Backup] -> [BackupWorker] -> 7z library
//         (7zip.dll/7z.dll/7z.so) not found."; emit backupFinished(false,
//         "Backup failed: 7z library not found"); return;
//     }
//
//     qDebug() << "[Backup] -> [BackupWorker] -> Using 7z library:" <<
//     sevenZipLib;
//
//     if (!QDir().mkpath(tempBackupDir)) {
//         qCritical() << "[Backup] -> [BackupWorker] -> could not create temp
//         dir:" << tempBackupDir; emit backupFinished(false,
//         QStringLiteral("Backup failed: could not create
//         %1").arg(tempBackupDir)); return;
//     }
//
//     if (!copySettingsToTempDir(settingsDir, tempBackupDir)) {
//         QDir(tempBackupDir).removeRecursively();
//         emit backupFinished(false, "Backup failed: could not copy settings");
//         return;
//     }
//
//     emit progressChanged(10);
//
//     try {
//         bit7z::Bit7zLibrary lib{sevenZipLib.toStdString()};
//         //bit7z::Bit7zCompressor compressor{lib, bit7z::BitFormat::SevenZip};
//         try {
//             bit7z::Bit7zLibrary lib{sevenZipLib.toStdString()};
//             bit7z::BitFileCompressor compressor{lib,
//             bit7z::BitFormat::SevenZip};
//
//             // Set compression level to normal (level 6)
//             compressor.setCompressionLevel(bit7z::BitCompressionLevel::Normal);
//
//             // Perform compression on the prepared temp directory
//             compressor.compressDirectory(
//                     tempBackupDir.toStdString(),
//                     archivePath7zExt.toStdString());
//
//             qDebug() << "[Backup] -> [BackupWorker] -> Backup succeeded!
//             Archive:" << archivePath7zExt;
//             QDir(tempBackupDir).removeRecursively();
//             emit progressChanged(100);
//             emit backupFinished(true, archivePath7zExt);
//             return;
//         } catch (const bit7z::BitException& ex) {
//             qCritical() << "[Backup] -> [BackupWorker] -> bit7z exception:"
//             << ex.what(); QDir(tempBackupDir).removeRecursively(); emit
//             backupFinished(false, QStringLiteral("Backup failed:
//             %1").arg(ex.what())); return;
//         }
//
//         // Set compression level to normal (level 6)
//         //compressor.setCompressionLevel(bit7z::BitCompressionLevel::Normal);
//         compressor.setCompressionLevel(bit7z::BitCompression::Normal);
//
//         // Perform compression on the prepared temp directory
//         compressor.compressDirectory(
//                 tempBackupDir.toStdString(),
//                 archivePath7zExt.toStdString());
//
//         qDebug() << "[Backup] -> [BackupWorker] -> Backup succeeded!
//         Archive:" << archivePath7zExt;
//         QDir(tempBackupDir).removeRecursively();
//         emit progressChanged(100);
//         emit backupFinished(true, archivePath7zExt);
//         return;
//     } catch (const bit7z::BitException& ex) {
//         qCritical() << "[Backup] -> [BackupWorker] -> bit7z exception:" <<
//         ex.what(); QDir(tempBackupDir).removeRecursively(); emit
//         backupFinished(false, QStringLiteral("Backup failed:
//         %1").arg(ex.what())); return;
//     }
// #endif
// }
//
// void BackupWorker::deleteOldBackups() {
//     // Use the same Documents resolution as performBackUp() so cleanup
//     // actually targets the folder where the backups live.
//     const QString backupDir = resolveDocumentsDir() + "/Mixxx-Backups";
//     QDir dir(backupDir);
//     dir.setNameFilters({"MixxxSettings-*.7z", "MixxxSettings-*.zip"});
//     dir.setSorting(QDir::Time);
//
//     if (m_keepBackups == 0) {
//         qDebug() << "[Backup] -> [BackupWorker] -> keeping all backups
//         (m_keepBackups=0)"; return;
//     }
//
//     const auto backups = dir.entryInfoList();
//     for (int i = m_keepBackups; i < backups.size(); ++i) {
//         dir.remove(backups[i].fileName());
//         emit backupRemoved(backups[i].fileName());
//     }
//     // upgrade backups are created in Mixxx-Backups/UpgradeBUs/
//     // -> "MixxxSettings-Upgrade-..."
//     // -> they will not be removed
// }
//
////#include "backupworker.h"
////
////#include <QCoreApplication>
////#include <QDateTime>
////#include <QDebug>
////#include <QDir>
////#include <QDirIterator>
////#include <QFile>
////#include <QProcess>
////#include <QStandardPaths>
////#include <QTemporaryDir>
////#include <QTemporaryFile>
////
////#include "moc_backupworker.cpp"
////
////#if defined(Q_OS_WIN)
////#include <QSettings>
////#endif
////
////namespace {
////
////#if defined(Q_OS_WIN)
////// instead of searching for a 7-Zip installation directory
////// like C:\Program Files\7-Zip
////// -> look up HKLM\SOFTWARE\7-Zip\Path in the Windows registry.
////QString find7ZipFromRegistry() {
////    const QStringList registryKeys = {
////            QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\7-Zip"),
//// QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\7-Zip"), /    };
////    for (const QString& key : registryKeys) {
////        QSettings reg(key, QSettings::NativeFormat);
////        const QString dir = reg.value(QStringLiteral("Path")).toString();
////        if (dir.isEmpty()) {
////            continue;
////        }
////        const QString exe = QDir(dir).filePath(QStringLiteral("7z.exe"));
////        if (QFile::exists(exe)) {
////            return exe;
////        }
////    }
////    return QString();
////}
////#endif
////
////#if defined(Q_OS_MACOS)
////// not needed on macOS: use the built-in zip utility
////#else
////// Locate an external 7z executable,
////// -> if not found -> fallback to shipped binary next to application
////QString findExternal7z() {
////#if defined(Q_OS_WIN)
////    const QString exeName = QStringLiteral("7z.exe");
////    QString exe = QStandardPaths::findExecutable(QStringLiteral("7z"));
////    if (!exe.isEmpty()) {
////        return exe;
////    }
////    exe = find7ZipFromRegistry();
////    if (!exe.isEmpty()) {
////        return exe;
////    }
////    const QStringList winPaths = {
////            QDir::homePath() + "/scoop/apps/7zip/current/7z.exe",
////            QStringLiteral("C:\\Program Files\\7-Zip\\7z.exe"),
////            QStringLiteral("C:\\Program Files (x86)\\7-Zip\\7z.exe")};
////    for (const QString& path : winPaths) {
////        if (QFile::exists(path)) {
////            return path;
////        }
////    }
////#elif defined(Q_OS_LINUX)
////    const QString exeName = QStringLiteral("7z");
////    QString exe = QStandardPaths::findExecutable(QStringLiteral("7z"));
////    if (!exe.isEmpty()) {
////        return exe;
////    }
////    const QStringList linuxPaths = {
////            QStringLiteral("/usr/bin/7z"),
////            QStringLiteral("/usr/local/bin/7z"),
////            QStringLiteral("/bin/7z")};
////    for (const QString& path : linuxPaths) {
////        if (QFile::exists(path)) {
////            return path;
////        }
////    }
////#endif
////
////    // Fallback check: 7z executable shipped next to the Mixxx executable
////    const QDir appDir(QCoreApplication::applicationDirPath());
////    const QString shippedExe = QDir::cleanPath(appDir.filePath(exeName));
////    if (QFile::exists(shippedExe)) {
////        return shippedExe;
////    }
////
////    return QString();
////}
////#endif
////
////} // anonymous namespace
////
////BackupWorker::BackupWorker(
////        UserSettingsPointer config,
////        int keepBackups,
////        bool upgradeBu,
////        QObject* parent)
////        : QObject(parent),
////          m_pConfig(config),
////          m_keepBackups(keepBackups),
////          m_upgradeBu(upgradeBu) {
////    currentMixxxVersion = m_pConfig->getValue(ConfigKey("[Config]",
///"Version"));
////}
////
////// Helper for Windows: resolve the users Documents folder
////// -> localized Windows installs must return correct path
////// -> QStandardPaths::DocumentsLocation may return a localized
//////    display name like "Documenten" in NL
////// -> Robocopy fails on the translated name
////// -> If that happens -> fall back to the English folder name
//////    under the user's home directory.
////// A folder is only usable if we can actually write a file into it.
////// QDir::exists() returns true for localized shell aliases like
////// "Documenten" NL -> problem for Win32 tools (robocopy)
////// -> real file test
////QString BackupWorker::resolveDocumentsDir() {
////    auto isWritableDir = [](const QString& dir) -> bool {
////        if (dir.isEmpty() || !QDir(dir).exists()) {
////            return false;
////        }
////        QTemporaryFile
/// probe(QDir(dir).filePath(QStringLiteral("mixxx-write-test-XXXXXX"))); /
/// return probe.open(); /    };
////
////    // The standard Documents location.
////    // -> On English Windows this is normally "C:/Users/<user>/Documents".
////    const QString standard = QStandardPaths::writableLocation(
////            QStandardPaths::DocumentsLocation);
////    if (isWritableDir(standard)) {
////        return standard;
////    }
////    qWarning() << "[Backup] -> [BackupWorker] -> DocumentsLocation"
////               << standard << "not writable, trying fallbacks";
////
////    // "Documents" under the user's home.
////    //  This is what the real folder is named on disk even
////    const QString homeDocuments = QDir::homePath() + "/Documents";
////    if (isWritableDir(homeDocuments)) {
////        qWarning() << "[Backup] -> [BackupWorker] -> using" <<
/// homeDocuments; /        return homeDocuments; /    }
////
////    // Last resort: root of the system disk (C:\)
////    // Always writable for the current user via UAC-compatible locations
////    // independent of any localization.
////#if defined(Q_OS_WIN)
////    QString systemRoot = QDir::rootPath();
////    while (systemRoot.endsWith('/')) {
////        systemRoot.chop(1);
////    }
////    if (systemRoot.isEmpty()) {
////        systemRoot = QStringLiteral("C:");
////    }
////#else
////    QString systemRoot = QDir::rootPath(); // "/"
////#endif
////    qWarning() << "[Backup] -> [BackupWorker] -> falling back to system disk
/// root:" /               << systemRoot; /    return systemRoot;
////}
////
////bool BackupWorker::copySettingsToTempDir(const QString& settingsDir, const
/// QString& tempDirPath) {
////#if defined(Q_OS_WIN)
////    // Robocopy parses its own command line
////    // -> split any argument that contains spaces
////    const QString robocopyLog = QDir::toNativeSeparators(
////            QDir::tempPath() + "/mixxx-backup-robocopy.log");
////
////    qDebug() << "[BackUp] -> [BackUpWorker] -> start creation tempdir
///(robocopy)";
////
////    QProcess robocopy;
////    robocopy.setProcessChannelMode(QProcess::MergedChannels);
////
////    // Check for robocopy.exe
////    QObject::connect(&robocopy, &QProcess::errorOccurred,
///[](QProcess::ProcessError e) { /        qWarning() << "[Backup] ->
///[BackupWorker] -> robocopy process error:" << e; /    });
////
////    robocopy.start(QStringLiteral("robocopy"),
////            {QDir::toNativeSeparators(settingsDir),
////                    QDir::toNativeSeparators(tempDirPath),
////                    "/E", // copy subdirectories, including empty ones
////                    "/XD",
////                    "analysis", // exclude analysis
////                    "/XD",
////                    "lut", // exclude timecode lut
////                    "/XD",
////                    "samples", // exclude samples
////                    "/XD",
////                    "bpmcurve", // exclude bpmcurve
////                    "/XD",
////                    "keycurve", // exclude keycurve
////                    "/XD",
////                    "fingerprints", // exclude keycurve
////                    "/R:3",         // retry 3 times if file is locked
////                    "/W:2",         // wait 2 seconds between retries
////                    "/NP",          // progress display off
////                    "/NFL",         // no file list (keep log small)
////                    "/NDL",         // no dir list
////                    "/LOG+:" + robocopyLog});
////
////    if (!robocopy.waitForFinished(300000)) {
////        qCritical() << "[Backup] -> [BackupWorker] -> robocopy timed out! "
////                       "See log:"
////                    << robocopyLog;
////        return false;
////    }
////
////    // Robocopy exit codes are bitfields, NOT standard exit codes:
////    //    0 = no files copied, no failures
////    //    1 = files copied successfully
////    //    2 = extra files/dirs detected
////    //    4 = mismatched files/dirs
////    //    8 = some files/dirs could not be copied (copy errors)
////    //   16 = serious error, no copy performed
////    // Anything with the 8 or 16 bit set means a real failure.
////    const int rc = robocopy.exitCode();
////    if (rc >= 8) {
////        qCritical() << "[Backup] -> [BackupWorker] -> robocopy failed with
/// exit code" /                    << rc << "- see log:" << robocopyLog; / //
/// Dump the log to the Mixxx log /        QFile log(robocopyLog); /        if
///(log.open(QIODevice::ReadOnly | QIODevice::Text)) { / qCritical().noquote()
///<< log.readAll(); /        } /        return false; /    } /    return true;
////
////#elif defined(Q_OS_LINUX)
////    qDebug() << "[Backup] -> [BackupWorker] -> start creation tempdir
///(rsync)";
////
////    QProcess rsync;
////    rsync.setProgram(QStringLiteral("rsync"));
////    rsync.setArguments({"-a",
////            "--exclude=analysis/", // exclude analysis
////            "--exclude=lut/",      // exclude timecode lut
////            settingsDir + "/",
////            tempDirPath + "/"});
////    rsync.start();
////    rsync.waitForFinished();
////
////    qDebug() << "stdout:" << rsync.readAllStandardOutput();
////    qDebug() << "stderr:" << rsync.readAllStandardError();
////
////    if (rsync.exitCode() != 0) {
////        qCritical() << "[Backup] -> [BackupWorker] -> rsync failed! Exit
/// code:" << rsync.exitCode(); /        return false; /    } /    return true;
////
////#else
////    // macos ea: use Qts file copy
////    QDirIterator it(settingsDir,
////            QDir::Files | QDir::NoDotAndDotDot,
////            QDirIterator::Subdirectories);
////
////    while (it.hasNext()) {
////        QString srcPath = it.next();
////        QString relativePath = QDir(settingsDir).relativeFilePath(srcPath);
////
////        // Skip analysis folders at any level.
////        if (relativePath.contains("analysis/") ||
////                relativePath.startsWith("analysis/") ||
////                relativePath.endsWith("/analysis")) {
////            continue;
////        }
////        // Skip lut folders at any level.
////        if (relativePath.contains("lut/") ||
////                relativePath.startsWith("lut/") ||
////                relativePath.endsWith("/lut")) {
////            continue;
////        }
////        // Skip samples folders at any level.
////        if (relativePath.contains("samples/") ||
////                relativePath.startsWith("samples/") ||
////                relativePath.endsWith("/samples")) {
////            continue;
////        }
////        // Skip bpmcurve folders at any level.
////        if (relativePath.contains("bpmcurve/") ||
////                relativePath.startsWith("bpmcurve/") ||
////                relativePath.endsWith("/bpmcurve")) {
////            continue;
////        }
////        // Skip keycurve folders at any level.
////        if (relativePath.contains("keycurve/") ||
////                relativePath.startsWith("keycurve/") ||
////                relativePath.endsWith("/keycurve")) {
////            continue;
////        }
////        // Skip fingerprints folders at any level.
////        if (relativePath.contains("fingerprints/") ||
////                relativePath.startsWith("fingerprints/") ||
////                relativePath.endsWith("/fingerprints")) {
////            continue;
////        }
////
////        QString destPath = tempDirPath + "/" + relativePath;
////        QFileInfo(destPath).dir().mkpath(".");
////        if (!QFile::copy(srcPath, destPath)) {
////            qCritical() << "Failed to copy file:" << srcPath << "to" <<
/// destPath; /            return false; /        } /    } /    return true;
////#endif
////}
////
////void BackupWorker::performBackup() {
////    QString backupDir, archivePath, archivePath7zExt, archivePathZipExt,
/// zipExecutable; /    const QString settingsDir = m_pConfig->getSettingsPath();
////    const QString timestamp =
/// QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
////
////    // Resolve the Documents folder in a way that works on localized
////    // Windows installs. QStandardPaths::DocumentsLocation can return a
////    // display name that robocopy cannot resolve.
////    const QString documentsDir = resolveDocumentsDir();
////
////    if (m_upgradeBu) {
////        backupDir = documentsDir + "/Mixxx-Backups/UpgradeBUs";
////        archivePath = backupDir + "/MixxxSettings-Upgrade-" +
////                currentMixxxVersion + "-" + timestamp;
////    } else {
////        backupDir = documentsDir + "/Mixxx-Backups";
////        archivePath = backupDir + "/MixxxSettings-" + timestamp;
////    }
////
////    archivePath7zExt = archivePath + ".7z";
////    archivePathZipExt = archivePath + ".zip";
////
////    qDebug() << "[Backup] -> [BackupWorker] -> documentsDir:" <<
/// documentsDir; /    qDebug() << "[Backup] -> [BackupWorker] -> backupDir:" <<
/// backupDir;
////
////    // Ensure the backup directory exists
////    // mkpath returns false on failure unresolvable parent
////    // a redirected/synced folder... which is the only way
////    // we'd know about a problem before robocopy fails with
////    // ERROR 2.
////    if (!QDir().mkpath(backupDir)) {
////        qCritical() << "[Backup] -> [BackupWorker] -> could not create
/// backup dir:" /                    << backupDir /                    << "-
/// check that the parent folder is writable and not a" /                       "
/// redirected/synced/network path."; /        emit backupFinished(false, /
/// QStringLiteral("Backup failed: could not create %1").arg(backupDir)); /
/// return; /    }
////
////#if defined(Q_OS_MACOS)
////    // macOS has no 7z dependency; we use the system zip directly.
////    zipExecutable = QStringLiteral("/usr/bin/zip");
////#else
////    // Windows & Linux: prefer an external 7z if available, or shipped
/// binary. /    zipExecutable = findExternal7z();
////#endif
////
////    ///////////////////////////////////////////////////////////////////
////    // Phase 1: create the temp directory with a copy of the settings
////    ///////////////////////////////////////////////////////////////////
////    QString tempBackupDir = archivePath + "_temp";
////
////    if (!zipExecutable.isEmpty()) {
////        qDebug() << "[Backup] -> [BackupWorker] -> 7z/Zip found in:" <<
/// zipExecutable; /        qDebug() << "[Backup] -> [BackupWorker] ->
/// tempBackupDir:" << tempBackupDir;
////
////        if (!QDir().mkpath(tempBackupDir)) {
////            qCritical() << "[Backup] -> [BackupWorker] -> could not create
/// temp dir:" /                        << tempBackupDir; /            emit
/// backupFinished(false, /                    QStringLiteral("Backup failed:
/// could not create %1").arg(tempBackupDir)); /            return; /        }
////
////        if (!copySettingsToTempDir(settingsDir, tempBackupDir)) {
////            QDir(tempBackupDir).removeRecursively();
////            qCritical() << "[Backup] -> [BackupWorker] -> error creating
/// tempdir"; /            emit backupFinished(false, "Backup failed: could not
/// copy settings"); /            return; /        }
////
////        emit progressChanged(10);
////
////#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
////        QProcess process;
////        process.setProcessChannelMode(QProcess::MergedChannels);
////        process.start(zipExecutable,
////                {"a",
////                        "-t7z",
////                        archivePath7zExt,
////                        tempBackupDir,
////                        "-xr!analysis",
////                        "-xr!lut",
////                        "-mx=6"});
////
////        if (!process.waitForStarted(10000)) {
////            qCritical() << "[Backup] -> [BackupWorker] -> 7z failed to
/// start:" /                        << process.errorString(); /
/// QDir(tempBackupDir).removeRecursively(); /            emit
/// backupFinished(false, "Backup failed: 7z process failed to start"); / return;
////        } else if (!process.waitForFinished(300000)) {
////            qCritical() << "[Backup] -> [BackupWorker] -> 7z compression
/// timed out!"; /            process.kill(); /
/// QDir(tempBackupDir).removeRecursively(); /            emit
/// backupFinished(false, "Backup failed: 7z process timed out"); / return; / }
/// else if (process.exitCode() != 0) { /            qCritical() << "[Backup] ->
///[BackupWorker] -> 7z failed:" /                        <<
/// process.readAllStandardOutput(); / QDir(tempBackupDir).removeRecursively();
////            emit backupFinished(false, "Backup failed: 7z process returned
/// error code"); /            return; /        } else { /            qDebug() <<
///"[Backup] -> [BackupWorker] -> Backup succeeded! " / "Archive:" / <<
/// archivePath7zExt; /            QDir(tempBackupDir).removeRecursively(); /
/// emit progressChanged(100); /            emit backupFinished(true,
/// archivePath7zExt); /            return; /        }
////
////#elif defined(Q_OS_MACOS)
////        QStringList arguments = {"-r",
////                archivePathZipExt,
////                settingsDir,
////                "-x",
////                settingsDir + "/analysis/*",
////                "-x",
////                settingsDir + "/lut/*"};
////        qDebug() << "[Backup] -> [BackupWorker] -> Executing:" <<
/// zipExecutable /                 << arguments.join(" "); /        bool started
///= QProcess::startDetached(zipExecutable, arguments); /        if (started) {
////            qDebug() << "[Backup] -> [BackupWorker] -> MacOS zip backup "
////                        "started to:"
////                     << archivePathZipExt;
////            QDir(tempBackupDir).removeRecursively();
////            emit progressChanged(100);
////            emit backupFinished(true, archivePathZipExt);
////            return;
////        }
////        qWarning() << "[Backup] -> [BackupWorker] -> MacOS zip backup
/// failed."; /        QDir(tempBackupDir).removeRecursively(); /        emit
/// backupFinished(false, "Backup failed: MacOS zip backup failed"); / return;
////#endif
////    } else {
////        qWarning() << "[Backup] -> [BackupWorker] -> 7z/Zip executable not
/// found."; /        emit backupFinished(false, / QStringLiteral("Backup failed:
/// no 7z/zip executable available " /                               "(no
/// external 7z, and shipped binary missing)")); /        return; /    }
////}
////
////void BackupWorker::deleteOldBackups() {
////    // Use the same Documents resolution as performBackUp() so cleanup
////    // actually targets the folder where the backups live.
////    const QString backupDir = resolveDocumentsDir() + "/Mixxx-Backups";
////    QDir dir(backupDir);
////    dir.setNameFilters({"MixxxSettings-*.7z", "MixxxSettings-*.zip"});
////    dir.setSorting(QDir::Time);
////
////    if (m_keepBackups == 0) {
////        qDebug() << "[Backup] -> [BackupWorker] -> keeping all backups
///(m_keepBackups=0)"; /        return; /    }
////
////    const auto backups = dir.entryInfoList();
////    for (int i = m_keepBackups; i < backups.size(); ++i) {
////        dir.remove(backups[i].fileName());
////        emit backupRemoved(backups[i].fileName());
////    }
////    // upgrade backups are created in Mixxx-Backups/UpgradeBUs/
////    // -> "MixxxSettings-Upgrade-..."
////    // -> they will not be removed
////}

#pragma once

#include <QString>

namespace mixxx {
namespace deezer {

/// A desktop browser user agent is expected by the Deezer web endpoints.
const QString kUserAgent = QStringLiteral(
        "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");

/// Formats requested from the media.deezer.com license endpoint, ordered from
/// highest to lowest quality. The index matches the quality combo box order.
const QString kQualityFormats[] = {
        QStringLiteral("FLAC"),
        QStringLiteral("MP3_320"),
        QStringLiteral("MP3_128"),
};

} // namespace deezer
} // namespace mixxx

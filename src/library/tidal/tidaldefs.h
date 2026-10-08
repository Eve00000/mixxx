#pragma once

#include <QString>

namespace mixxx {
namespace tidal {

/// TIDAL only offers encrypted (Widevine) or raw MPEG-DASH streams. The
/// segments of the MPD manifests used by the desktop clients are plain,
/// unencrypted AAC/FLAC fragments and can be concatenated losslessly.
/// See the reference implementation "traktor-streaming-proxy" which fakes
/// the Beatport API and serves the very same DASH segments as mp4a.

const QString kApiBaseUrl = QStringLiteral("https://api.tidal.com/v1/");
const QString kAuthBaseUrl = QStringLiteral("https://auth.tidal.com/v1/oauth2/");

/// The v1 playbackinfopostpaywall endpoint is capped to AAC (HIGH) for our
/// OAuth client. Lossless FLAC is only served by the v2 trackManifests
/// endpoint, which returns an MPEG-DASH MPD with FLAC representations.
const QString kManifestBaseUrl =
        QStringLiteral("https://openapi.tidal.com/v2/trackManifests/");

/// Android client user agent. Required to receive an unencrypted DASH
/// manifest from the manifest endpoint.
const QString kAndroidUserAgent = QStringLiteral("okhttp/5.3.2");

/// OAuth2 client used by the TIDAL Android app. Same credentials as
/// python-tidal, see src/library/tidal/tidaldefs.cpp for provenance.
const QString kClientId = QStringLiteral("fX2JxdmntZWK0ixT");
const QString kClientSecret =
        QStringLiteral("1Nn9AfDAjxrgJFJbKNWLeAyKGVGmINuXPPLHVXAvxAg=");
const QString kScope = QStringLiteral("r_usr w_usr w_sub");

/// The user agent is required to receive a DASH manifest. Without it TIDAL
/// rejects the playbackinfo request.
const QString kUserAgent = QStringLiteral(
        "Mozilla/5.0 (Linux; Android 12; wv) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Version/4.0 Chrome/91.0.4472.114 Safari/537.36");
const QString kClientVersion = QStringLiteral("2025.7.16");

/// Quality codes accepted by the playbackinfo endpoint.
enum class Quality {
    Low,     // AAC 96 kbps
    High,    // AAC 320 kbps
    Lossless, // FLAC 16 bit / 44.1 kHz
};

inline QString qualityToString(Quality quality) {
    switch (quality) {
    case Quality::Low:
        return QStringLiteral("LOW");
    case Quality::High:
        return QStringLiteral("HIGH");
    case Quality::Lossless:
        return QStringLiteral("LOSSLESS");
    }
    return QStringLiteral("HIGH");
}

} // namespace tidal
} // namespace mixxx

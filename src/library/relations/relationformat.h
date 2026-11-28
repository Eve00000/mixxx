#pragma once

#include <QColor>
#include <QLocale>
#include <QPixmap>
#include <QRegularExpression>
#include <QString>
#include <QVariant>

#include "library/basetracktablemodel.h"
#include "track/keyutils.h"

// Formatting of track values in the relation windows, the same way as in the
// track table
namespace relationformat {

// BPM with 2 decimals (in the locale of the user), empty when there is no BPM
inline QString bpm(const QString& bpmText) {
    bool ok = false;
    const double value = bpmText.toDouble(&ok);
    if (!ok || value <= 0.0) {
        return QString();
    }
    return QLocale().toString(value, 'f', 2);
}

// Key in the notation selected by the user, the same text as in the track table
inline QString key(const QString& keyText, const QString& keyIdText) {
    bool ok = false;
    const int keyId = keyIdText.toInt(&ok);
    return QVariant(KeyUtils::keyFromKeyTextAndIdFields(
                            QVariant(keyText),
                            ok ? QVariant(keyId) : QVariant()))
            .toString();
}

// Colour of the key, invalid when key colours are disabled or there is no key
inline QColor keyColor(const QString& keyIdText) {
    if (!BaseTrackTableModel::keyColorsEnabled() ||
            !BaseTrackTableModel::keyColorPalette().has_value()) {
        return QColor();
    }
    bool ok = false;
    const int keyId = keyIdText.toInt(&ok);
    if (!ok) {
        return QColor();
    }
    const auto chromaticKey = KeyUtils::keyFromNumericValue(keyId);
    if (chromaticKey == mixxx::track::io::key::INVALID) {
        return QColor();
    }
    return KeyUtils::keyToColor(
            chromaticKey, BaseTrackTableModel::keyColorPalette().value());
}

// Comments of generated relations can contain {key:NN}: the key with number
// NN, shown in the key notation chosen by the user
inline QString expandComment(const QString& comment) {
    if (!comment.contains(QLatin1String("{key:"))) {
        return comment;
    }
    static const QRegularExpression kKeyToken(QStringLiteral("\\{key:(\\d+)\\}"));
    QString result;
    qsizetype last = 0;
    QRegularExpressionMatchIterator it = kKeyToken.globalMatch(comment);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        result += comment.mid(last, match.capturedStart() - last);
        const QString keyText = key(QString(), match.captured(1));
        result += keyText.isEmpty() ? match.captured(0) : keyText;
        last = match.capturedEnd();
    }
    result += comment.mid(last);
    return result;
}

inline QPixmap colorSwatch(const QColor& color) {
    QPixmap pixmap(12, 12);
    pixmap.fill(color);
    return pixmap;
}

} // namespace relationformat

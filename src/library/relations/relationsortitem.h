#pragma once

#include <QCollator>
#include <QRegularExpression>
#include <QString>
#include <QTableWidgetItem>

// Table item for the relation tables. Sorts like the relation list:
// optionally on the leading number of the text first ("98" before "117",
// "-1.2%" before "+2%"), then on the text with numbers in numeric order
class RelationSortItem : public QTableWidgetItem {
  public:
    RelationSortItem(const QString& text, bool numberFirst)
            : QTableWidgetItem(text),
              m_numberFirst(numberFirst) {
    }

    bool operator<(const QTableWidgetItem& other) const override {
        if (m_numberFirst) {
            const double a = leadingNumber(text());
            const double b = leadingNumber(other.text());
            if (a < b) {
                return true;
            }
            if (a > b) {
                return false;
            }
        }
        static const QCollator kCollator = []() {
            QCollator collator;
            collator.setNumericMode(true);
            collator.setCaseSensitivity(Qt::CaseInsensitive);
            return collator;
        }();
        return kCollator.compare(text(), other.text()) < 0;
    }

  private:
    // Leading number of a text ("117", "-1.2%"), 0 when the text does not
    // start with a number
    static double leadingNumber(const QString& text) {
        static const QRegularExpression kNumber(
                QStringLiteral("^\\s*([-+]?\\d+(?:[.,]\\d+)?)"));
        const QRegularExpressionMatch match = kNumber.match(text);
        if (!match.hasMatch()) {
            return 0.0;
        }
        QString number = match.captured(1);
        number.replace(QLatin1Char(','), QLatin1Char('.'));
        return number.toDouble();
    }

    bool m_numberFirst;
};

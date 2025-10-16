#pragma once

#include <QObject>
#include <QString>

class OscNotifier : public QObject {
    Q_OBJECT

  public:
    explicit OscNotifier(QObject* parent = nullptr);
    ~OscNotifier() override;

    void observeControls();
};
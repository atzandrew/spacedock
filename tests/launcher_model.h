#pragma once

#include <QStandardItemModel>

class PinDropModel : public QStandardItemModel {
    Q_OBJECT
    Q_PROPERTY(QStringList launcherList MEMBER launchers)
public:
    QStringList launchers;
};


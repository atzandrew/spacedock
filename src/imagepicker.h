#pragma once

#include <QObject>
#include <QUrl>
#include <QVariantMap>

class ImagePicker : public QObject {
    Q_OBJECT
public:
    explicit ImagePicker(QObject *parent = nullptr) : QObject(parent) {}
    Q_INVOKABLE void open();
    Q_INVOKABLE void cancel();
signals:
    void selected(const QUrl &url);
    void failed();
private slots:
    void response(uint code, const QVariantMap &results);
private:
    QString m_request;
};

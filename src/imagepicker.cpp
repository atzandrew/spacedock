#include "imagepicker.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusMetaType>
#include <QUuid>

struct FilterRule { uint type; QString value; };
using FilterRules = QList<FilterRule>;
struct FileFilter { QString name; FilterRules rules; };
using FileFilters = QList<FileFilter>;
Q_DECLARE_METATYPE(FilterRule)
Q_DECLARE_METATYPE(FilterRules)
Q_DECLARE_METATYPE(FileFilter)
Q_DECLARE_METATYPE(FileFilters)
QDBusArgument &operator<<(QDBusArgument &arg, const FilterRule &rule) { arg.beginStructure(); arg << rule.type << rule.value; arg.endStructure(); return arg; }
const QDBusArgument &operator>>(const QDBusArgument &arg, FilterRule &rule) { arg.beginStructure(); arg >> rule.type >> rule.value; arg.endStructure(); return arg; }
QDBusArgument &operator<<(QDBusArgument &arg, const FileFilter &filter) { arg.beginStructure(); arg << filter.name << filter.rules; arg.endStructure(); return arg; }
const QDBusArgument &operator>>(const QDBusArgument &arg, FileFilter &filter) { arg.beginStructure(); arg >> filter.name >> filter.rules; arg.endStructure(); return arg; }

static const QString service = QStringLiteral("org.freedesktop.portal.Desktop");
static const QString requestInterface = QStringLiteral("org.freedesktop.portal.Request");

void ImagePicker::open()
{
    if (!m_request.isEmpty()) return;
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) { emit failed(); return; }
    const auto token = QStringLiteral("spacedock_") + QUuid::createUuid().toString(QUuid::Id128);
    auto sender = bus.baseService().mid(1);
    sender.replace('.', '_');
    m_request = QStringLiteral("/org/freedesktop/portal/desktop/request/") + sender + '/' + token;
    if (!bus.connect(service, m_request, requestInterface, QStringLiteral("Response"), this, SLOT(response(uint,QVariantMap)))) {
        m_request.clear(); emit failed(); return;
    }
    auto call = QDBusMessage::createMethodCall(service, QStringLiteral("/org/freedesktop/portal/desktop"),
        QStringLiteral("org.freedesktop.portal.FileChooser"), QStringLiteral("OpenFile"));
    qDBusRegisterMetaType<FilterRule>();
    qDBusRegisterMetaType<FilterRules>();
    qDBusRegisterMetaType<FileFilter>();
    qDBusRegisterMetaType<FileFilters>();
    const FileFilters filters{{tr("Images"), {{1, QStringLiteral("image/*")}}}};
    call << QString() << tr("Choose an image") << QVariantMap{{"handle_token", token}, {"multiple", false},
        {"filters", QVariant::fromValue(filters)}};
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, token] {
        QDBusPendingReply<QDBusObjectPath> reply = *watcher;
        watcher->deleteLater();
        // Ignore replies to requests that have already been cancelled.
        if (!m_request.endsWith('/' + token)) {
            if (!reply.isError()) {
                auto close = QDBusMessage::createMethodCall(service, reply.value().path(), requestInterface, QStringLiteral("Close"));
                QDBusConnection::sessionBus().asyncCall(close);
            }
            return;
        }
        if (reply.isError()) { cancel(); emit failed(); return; }
        const auto actual = reply.value().path();
        if (actual != m_request) {
            auto bus = QDBusConnection::sessionBus();
            bus.disconnect(service, m_request, requestInterface, QStringLiteral("Response"), this, SLOT(response(uint,QVariantMap)));
            m_request = actual;
            if (!bus.connect(service, m_request, requestInterface, QStringLiteral("Response"), this, SLOT(response(uint,QVariantMap)))) {
                m_request.clear(); emit failed();
            }
        }
    });
}

void ImagePicker::cancel()
{
    if (m_request.isEmpty()) return;
    auto bus = QDBusConnection::sessionBus();
    bus.disconnect(service, m_request, requestInterface, QStringLiteral("Response"), this, SLOT(response(uint,QVariantMap)));
    auto close = QDBusMessage::createMethodCall(service, m_request, requestInterface, QStringLiteral("Close"));
    bus.asyncCall(close);
    m_request.clear();
}

void ImagePicker::response(uint code, const QVariantMap &results)
{
    auto bus = QDBusConnection::sessionBus();
    bus.disconnect(service, m_request, requestInterface, QStringLiteral("Response"), this, SLOT(response(uint,QVariantMap)));
    m_request.clear();
    if (code == 1) return;
    const auto uris = results.value("uris").toStringList();
    if (code != 0 || uris.isEmpty()) { emit failed(); return; }
    emit selected(QUrl(uris.first()));
}

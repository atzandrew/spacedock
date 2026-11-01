#pragma once

#include <QApplication>
#include <QQuickItem>
#include <QTemporaryDir>
#include <QTest>

inline QList<QQuickItem *> taskItems(QQuickItem *parent) {
    QList<QQuickItem *> result;
    for (auto *child : parent->childItems()) {
        if (child->objectName() == "taskDelegate") result.append(child);
        result.append(taskItems(child));
    }
    return result;
}

// Establish isolated XDG roots before Qt or KDE can cache their locations.
// Keep them alive until the test object and application have been destroyed.
template<class Test>
int runDockUiTest(int argc, char **argv) {
    QTemporaryDir config;
    if (!config.isValid()) return 1;
    qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
    qputenv("XDG_CACHE_HOME", (config.path() + "/cache").toUtf8());
    qputenv("XDG_DATA_HOME", (config.path() + "/data").toUtf8());
    QApplication app(argc, argv);
    app.setApplicationName("spacedock");
    app.setOrganizationName("spacedock");
    Test test;
    return QTest::qExec(&test, argc, argv);
}

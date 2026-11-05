#pragma once

#include <QCoreApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

// Keep translators alive for as long as the application uses them.
class Translations {
public:
    void install(QCoreApplication &app, const QLocale &locale = QLocale()) {
        if (m_qt.load(locale, "qtbase", "_", QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
            app.installTranslator(&m_qt);
        if (m_english.load(QLocale("en"), "spacedock", "_", ":/translations"))
            app.installTranslator(&m_english);
        if (m_app.load(locale, "spacedock", "_", ":/translations"))
            app.installTranslator(&m_app);
    }
private:
    QTranslator m_qt;
    QTranslator m_english;
    QTranslator m_app;
};

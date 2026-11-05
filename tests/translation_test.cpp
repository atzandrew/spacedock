#include "translations.h"
#include <QQmlEngine>
#include <QQmlExpression>
#include <QtTest>

class TranslationTest : public QObject {
    Q_OBJECT
private slots:
    void catalogIsAvailableToCppAndQml() {
        Translations translations;
        translations.install(*QCoreApplication::instance(), QLocale("fr_FR"));
        QCOMPARE(QCoreApplication::translate("TranslationFixture", "Open"), QString("Ouvrir"));
        QQmlEngine engine;
        QQmlExpression expression(engine.rootContext(), nullptr, "qsTranslate('TranslationFixture', 'Open')");
        QCOMPARE(expression.evaluate().toString(), QString("Ouvrir"));
        QVERIFY(!expression.hasError());
    }
    void productionCatalogsAndPlurals() {
        const char *source = "Permanently delete %n item(s) from the Trash?";
        {
            Translations translations;
            translations.install(*QCoreApplication::instance(), QLocale("nl_NL"));
            QCOMPARE(QCoreApplication::translate("TrashController", source, nullptr, 1), QString("Permanently delete 1 item from the Trash?"));
            QCOMPARE(QCoreApplication::translate("TrashController", source, nullptr, 2), QString("Permanently delete 2 items from the Trash?"));
        }
        {
            Translations translations;
            translations.install(*QCoreApplication::instance(), QLocale("fr_CA"));
            QCOMPARE(QCoreApplication::translate("Main", "Trash"), QString::fromUtf8("Corbeille"));
            QCOMPARE(QCoreApplication::translate("TrashController", source, nullptr, 2), QString::fromUtf8("Supprimer définitivement 2 éléments de la corbeille ?"));
            QQmlEngine engine;
            QQmlExpression expression(engine.rootContext(), nullptr, "qsTranslate('FolderView', '%n More in %1', '', 2).arg('Dolphin')");
            QCOMPARE(expression.evaluate().toString(), QString::fromUtf8("2 éléments supplémentaires dans Dolphin"));
            QVERIFY(!expression.hasError());
        }
    }
    void shippedCatalogsLoad() {
        const struct { const char *locale; const char *trash; } cases[] = {
            {"de_DE", "Papierkorb"}, {"es_MX", "Papelera"},
            {"pt_BR", "Lixeira"}, {"it_IT", "Cestino"},
            {"ru_RU", "Корзина"}, {"uk_UA", "Кошик"},
            {"pl_PL", "Kosz"}, {"zh_CN", "回收站"},
            {"ja_JP", "ごみ箱"}
        };
        for (const auto &entry : cases) {
            Translations translations;
            translations.install(*QCoreApplication::instance(), QLocale(entry.locale));
            QCOMPARE(QCoreApplication::translate("Main", "Trash"), QString::fromUtf8(entry.trash));
            const char *source = "Permanently delete %n item(s) from the Trash?";
            for (int count : {1, 2, 5}) {
                const QString result = QCoreApplication::translate("TrashController", source, nullptr, count);
                QVERIFY2(!result.contains("item(s)"), entry.locale);
                QVERIFY2(result.contains(QString::number(count)), entry.locale);
            }
        }
    }
    void missingCatalogFallsBackToEnglish() {
        Translations translations;
        translations.install(*QCoreApplication::instance(), QLocale("nl_NL"));
        QCOMPARE(QCoreApplication::translate("TranslationFixture", "Open"), QString("Open"));
    }
};

QTEST_GUILESS_MAIN(TranslationTest)
#include "translation_test.moc"

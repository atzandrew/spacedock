#include "tuning.h"
#include <QSettings>
#include <QFile>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

class TuningTest : public QObject {
    Q_OBJECT
private slots:
    void windowClickPreferencePersists() {
        QTemporaryDir directory;
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
        QCoreApplication::setOrganizationName("SpacedockTuningTest");
        QCoreApplication::setApplicationName("window-click");
        { Tuning tuning(":/tuning.json", nullptr, true);
          QVERIFY(!tuning.bringAllWindows());
          tuning.setBringAllWindows(true); }
        { Tuning tuning(":/tuning.json", nullptr, true);
          QVERIFY(tuning.bringAllWindows());
          tuning.setBringAllWindows(false); }
        { Tuning tuning(":/tuning.json", nullptr, true);
          QVERIFY(!tuning.bringAllWindows()); }
    }
    void magnificationScalePersistsAndResizesSurface() {
        QTemporaryDir directory;
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
        QCoreApplication::setOrganizationName("SpacedockTuningTest");
        QCoreApplication::setApplicationName("magnification-scale");
        {
            Tuning tuning(":/tuning.json", nullptr, true);
            const auto original = tuning.magnification().value("scaleSamples").toList();
            QCOMPARE(tuning.magnificationScale(), original.first().toDouble());
            tuning.setMagnificationScale(4);
            const auto scales = tuning.magnification().value("scaleSamples").toList();
            QCOMPARE(scales.first().toDouble(), 4.0);
            QCOMPARE(scales.last().toDouble(), 1.0);
            QVERIFY(qAbs((scales[1].toDouble() - 1) / 3 - (original[1].toDouble() - 1) / (original[0].toDouble() - 1)) < 0.00001);
            QVERIFY(tuning.layout().value("surfaceHeight").toDouble() >= tuning.size() * 4 + 14 + 72 + 9 + 24);
        }
        {
            Tuning tuning(":/tuning.json", nullptr, true);
            QCOMPARE(tuning.magnificationScale(), 4.0);
            tuning.setMagnificationScale(1);
            for (const auto &scale : tuning.magnification().value("scaleSamples").toList())
                QCOMPARE(scale.toDouble(), 1.0);
        }
    }
    void sizeAndSettings() {
        Tuning tuning(":/tuning.json");
        tuning.prepareSizeAnimation(128);
        const auto preparedHeight = tuning.layout().value("surfaceHeight").toDouble();
        QVERIFY(preparedHeight > 224);
        tuning.setSize(50);
        QCOMPARE(tuning.layout().value("surfaceHeight").toDouble(), preparedHeight);
        tuning.finishSizeAnimation();
        for (double size : {18.0, 36.5, 48.0, 128.0}) {
            tuning.setSize(size);
            const auto layout = tuning.layout();
            QCOMPARE(layout.value("iconSize").toDouble(), size);
            QVERIFY(layout.value("shelfHeight").toDouble() >= size + layout.value("iconBaselineInset").toDouble());
            QVERIFY(layout.value("surfaceHeight").toDouble() >= size * 1.75 + layout.value("iconBaselineInset").toDouble() + 14 + 72 + 9 + 24);
        }
        tuning.setSize(200);
        QCOMPARE(tuning.size(), 128.0);
        tuning.prepareSizeAnimation(18);
        const auto animationHeight = tuning.layout().value("surfaceHeight").toDouble();
        tuning.setSize(80);
        QCOMPARE(tuning.artworkSize(), 128.0);
        QCOMPARE(tuning.layout().value("surfaceHeight").toDouble(), animationHeight);
        tuning.finishSizeAnimation();
        QCOMPARE(tuning.artworkSize(), 80.0);
        QVERIFY(tuning.layout().value("surfaceHeight").toDouble() < animationHeight);
        tuning.prepareSizeAnimation(128);
        const auto growingHeight = tuning.layout().value("surfaceHeight").toDouble();
        tuning.setSize(100);
        QCOMPARE(tuning.layout().value("surfaceHeight").toDouble(), growingHeight);
        tuning.setSize(128);
        tuning.finishSizeAnimation();
        QCOMPARE(tuning.layout().value("surfaceHeight").toDouble(), growingHeight);
        tuning.setMagnificationEnabled(false);
        QVERIFY(!tuning.magnification().value("enabled").toBool());
        tuning.setAutoHideEnabled(true);
        QVERIFY(tuning.autoHide().value("enabled").toBool());
    }
    void invalidEditsKeepLastGoodLayout() {
        QTemporaryDir directory;
        const auto path = directory.filePath("tuning.json");
        QFile defaults(":/tuning.json");
        QVERIFY(defaults.copy(path));
        QVERIFY(QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner));
        Tuning tuning(path);
        const auto original = tuning.layout();
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(R"({"schemaVersion":1,"layout":{"iconSize":500}})");
        file.close();
        QVERIFY(!tuning.reload());
        QCOMPARE(tuning.layout(), original);
    }
    void rejectsInvalidTuning_data() {
        QTest::addColumn<QString>("kind");
        QTest::newRow("invalid reveal strip") << "auto-hide-edge";
        QTest::newRow("invalid hide delay") << "auto-hide-delay";
        QTest::newRow("non-monotone spatial samples") << "scales";
        QTest::newRow("unfinished temporal samples") << "progress";
        QTest::newRow("icon larger than surface") << "surface";
        QTest::newRow("bounce clips surface") << "bounce-space";
        QTest::newRow("bounce does not land") << "bounce-samples";
        QTest::newRow("tooltip text cannot fit") << "tooltip-font";
        QTest::newRow("negative menu fade") << "motion";
    }
    void rejectsInvalidTuning() {
        QFETCH(QString, kind);
        QTemporaryDir directory;
        const auto path = directory.filePath("tuning.json");
        QFile defaults(":/tuning.json");
        QVERIFY(defaults.open(QIODevice::ReadOnly));
        auto root = QJsonDocument::fromJson(defaults.readAll()).object();
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(root).toJson());
        file.close();
        Tuning tuning(path);
        const auto lastAutoHide = tuning.autoHide();
        const auto lastGood = tuning.magnification();
        const auto lastFeedback = tuning.feedback();
        const auto lastTooltip = tuning.tooltip();
        const auto lastMotion = tuning.motion();
        auto magnification = root["magnification"].toObject();
        if (kind == "scales") magnification["scaleSamples"] = QJsonArray {1.75, 1.2, 1.5, 1.0};
        if (kind == "progress") magnification["enterProgressSamples"] = QJsonArray {0.0, 0.5, 0.9};
        if (kind == "surface") {
            auto layout = root["layout"].toObject();
            layout["surfaceHeight"] = 90;
            root["layout"] = layout;
        }
        if (kind == "bounce-space" || kind == "bounce-samples") {
            auto feedback = root["feedback"].toObject();
            if (kind == "bounce-space") feedback["bounceHeight"] = 64;
            else feedback["bounceSamples"] = QJsonArray {0.0, 1.0, 0.2};
            root["feedback"] = feedback;
        }
        if (kind == "tooltip-font") {
            auto tooltip = root["tooltip"].toObject();
            tooltip["fontPixelSize"] = 32;
            root["tooltip"] = tooltip;
        }
        if (kind.startsWith("auto-hide")) {
            auto settings = root["autoHide"].toObject();
            settings[kind == "auto-hide-edge" ? "edgeSize" : "hideDelayMs"] = -1;
            root["autoHide"] = settings;
        }
        if (kind == "motion") {
            auto motion = root["motion"].toObject();
            motion["menuHighlightFadeMs"] = -1;
            root["motion"] = motion;
        }
        root["magnification"] = magnification;
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(QJsonDocument(root).toJson());
        file.close();
        QVERIFY(!tuning.reload());
        QCOMPARE(tuning.autoHide(), lastAutoHide);
        QCOMPARE(tuning.magnification(), lastGood);
        QCOMPARE(tuning.feedback(), lastFeedback);
        QCOMPARE(tuning.tooltip(), lastTooltip);
        QCOMPARE(tuning.motion(), lastMotion);
    }
    void olderDocumentsInheritMotionDefaults() {
        QTemporaryDir directory;
        const auto path = directory.filePath("tuning.json");
        QFile defaults(":/tuning.json");
        QVERIFY(defaults.open(QIODevice::ReadOnly));
        auto root = QJsonDocument::fromJson(defaults.readAll()).object();
        const auto expected = root["motion"].toObject().toVariantMap();
        QVERIFY(!expected.isEmpty());
        root.remove("motion");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(root).toJson());
        file.close();
        Tuning tuning(path);
        QVERIFY(tuning.reload());
        QCOMPARE(tuning.motion(), expected);
    }
    void reloadsAfterAtomicReplacement() {
        QTemporaryDir directory;
        const auto path = directory.filePath("tuning.json");
        QFile defaults(":/tuning.json");
        QVERIFY(defaults.copy(path));
        QVERIFY(QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner));
        Tuning tuning(path);
        QVERIFY(defaults.open(QIODevice::ReadOnly));
        auto bytes = defaults.readAll();
        bytes.replace("\"iconSize\": 48", "\"iconSize\": 56");
        bytes.replace("\"shelfHeight\": 76", "\"shelfHeight\": 88");
        bytes.replace("\"surfaceHeight\": 224", "\"surfaceHeight\": 256");
        QSaveFile replacement(path);
        QVERIFY(replacement.open(QIODevice::WriteOnly));
        replacement.write(bytes);
        QVERIFY(replacement.commit());
        QTRY_COMPARE(tuning.layout().value("iconSize").toInt(), 56);
        bytes.replace("\"iconSize\": 56", "\"iconSize\": 40");
        QSaveFile second(path);
        QVERIFY(second.open(QIODevice::WriteOnly));
        second.write(bytes);
        QVERIFY(second.commit());
        QTRY_COMPARE(tuning.layout().value("iconSize").toInt(), 40);
    }
};
QTEST_GUILESS_MAIN(TuningTest)
#include "tuning_test.moc"

#include <QFile>
#include <QJSEngine>
#include <QTest>
#include <cmath>

class MagnificationTest : public QObject {
    Q_OBJECT
    QJSEngine engine;
    QJSValue geometry(int count, double width, double pointer, double amount) {
        return engine.globalObject().property("layout").call({count, 48, 10, width, 16, pointer, amount,
            engine.evaluate("[1.75,1.5,1.2,1.0]")});
    }
private slots:
    void initTestCase() {
        QFile file(":/qml/Magnification.js");
        QVERIFY(file.open(QIODevice::ReadOnly));
        auto script = QString::fromUtf8(file.readAll());
        script.remove(".pragma library");
        const auto result = engine.evaluate(script);
        QVERIFY2(!result.isError(), qPrintable(result.toString()));
    }
    void iconsNeverOverlapOrExceedScreen() {
        for (const auto width : {220, 400, 900}) {
            for (int pointer = 0; pointer <= width; pointer += 3) {
                const auto result = geometry(7, width, pointer, 1);
                const auto items = result.property("items");
                const bool overflow = 7 * 48 + 6 * 10 > width - 32;
                QVERIFY(overflow || result.property("width").toNumber() <= width - 32 + 0.001);
                double right = -10;
                for (int i = 0; i < 7; ++i) {
                    const auto item = items.property(i);
                    const double x = item.property("x").toNumber();
                    const double size = item.property("size").toNumber();
                    QVERIFY(x >= right + 10 - 0.001);
                    QVERIFY(size >= 48 && size <= 48 * 1.75);
                    if (overflow) QCOMPARE(size, 48);
                    right = x + size;
                }
            }
        }
    }
    void preferredSizeFitsOrFallsBackToScrolling() {
        const auto fit = engine.globalObject().property("fitIconSize");
        QCOMPARE(fit.call({0, 80, 16, 24, 900, 18}).toNumber(), 80.0);
        QCOMPARE(fit.call({5, 80, 16, 24, 900, 18}).toNumber(), 80.0);
        const double size = fit.call({13, 100, 20, 32, 900, 18}).toNumber();
        QVERIFY(size < 100 && size > 18);
        QVERIFY((13 * 100 + 12 * 20 + 64) * size / 100 <= 900);
        const double smaller = fit.call({14, 100, 20, 32, 900, 18}).toNumber();
        QVERIFY(smaller < size);
        QCOMPARE(fit.call({100, 100, 20, 32, 300, 18}).toNumber(), 18.0);
        QCOMPARE(fit.call({5, 80, 16, 24, 900, 18}).toNumber(), 80.0);
    }
    void magnificationRequiresFullEffectToFit() {
        const auto can = engine.globalObject().property("canMagnify");
        const auto scales = engine.evaluate("[1.75,1.5,1.2,1.0]");
        QVERIFY(can.call({13, 48, 10, 16, 900, scales}).toBool());
        QVERIFY(!can.call({13, 70, 14, 23, 1000, scales}).toBool());
        QVERIFY(can.call({0, 48, 10, 16, 100, scales}).toBool());
        QVERIFY(can.call({1, 48, 10, 16, 116, scales}).toBool());
        QVERIFY(!can.call({1, 48, 10, 16, 115, scales}).toBool());
    }
    void centerPeakAndSymmetry() {
        const auto result = geometry(7, 900, 450, 1);
        const auto items = result.property("items");
        QVERIFY(std::abs(items.property(3).property("size").toNumber() - 84) < 0.001);
        for (int i = 0; i < 3; ++i)
            QVERIFY(std::abs(items.property(i).property("size").toNumber()
                - items.property(6-i).property("size").toNumber()) < 0.001);
    }
    void pointerSweepIsContinuous() {
        auto previous = geometry(7, 900, 0, 1);
        for (int pointer = 1; pointer <= 900; ++pointer) {
            const auto current = geometry(7, 900, pointer, 1);
            for (int i = 0; i < 7; ++i)
                QVERIFY(std::abs(current.property("items").property(i).property("x").toNumber()
                    - previous.property("items").property(i).property("x").toNumber()) < 2);
            previous = current;
        }
    }
    void liftedSlotClosesWithoutChangingMagnification() {
        const auto layout = engine.globalObject().property("layout");
        const auto scales = engine.evaluate("[1.75,1.5,1.2,1.0]");
        for (int gap = 0; gap < 7; ++gap) {
            for (const double amount : {0.0, 0.5, 1.0}) {
                for (const double pointer : {150.0, 450.0, 700.0}) {
                    const auto closed = layout.call({7, 48, 10, 900, 16, pointer, amount, scales, gap, 0});
                    const auto compact = geometry(6, 900, pointer, amount);
                    QVERIFY(qAbs(closed.property("width").toNumber() - compact.property("width").toNumber()) < 0.001);
                    for (int i = 0; i < 7; ++i) {
                        if (i == gap) continue;
                        const auto a = closed.property("items").property(i);
                        const auto b = compact.property("items").property(i < gap ? i : i - 1);
                        QVERIFY(qAbs(a.property("x").toNumber() - b.property("x").toNumber()) < 0.001);
                        QVERIFY(qAbs(a.property("size").toNumber() - b.property("size").toNumber()) < 0.001);
                    }
                }
            }
        }
        const auto empty = layout.call({1, 48, 10, 900, 16, 450, 1, scales, 0, 0});
        QCOMPARE(empty.property("width").toNumber(), 0.0);
    }
    void insertionGapMovesEveryNeighborContinuously() {
        const auto insert = engine.globalObject().property("insertionLayout");
        const auto scales = engine.evaluate("[1.75,1.5,1.2,1.0]");
        const auto compact = geometry(6, 900, 450, 1);
        for (int source = 0; source < 7; ++source) {
            for (int gap = 0; gap < 7; ++gap) {
                const auto closed = insert.call({7, 48, 10, 900, 16, 450, 1, scales, gap, 0});
                for (int original = 0; original < 7; ++original) {
                    if (original == source) continue;
                    int slot = original;
                    if (source < gap && original > source && original <= gap) --slot;
                    if (source > gap && original >= gap && original < source) ++slot;
                    const int rank = original < source ? original : original - 1;
                    const auto a = closed.property("items").property(slot);
                    const auto b = compact.property("items").property(rank);
                    QVERIFY(qAbs(a.property("x").toNumber() - b.property("x").toNumber()) < 0.001);
                    QVERIFY(qAbs(a.property("size").toNumber() - b.property("size").toNumber()) < 0.001);
                }
                const auto half = insert.call({7, 48, 10, 900, 16, 450, 1, scales, gap, 0.5});
                const auto full = geometry(7, 900, 450, 1);
                for (int slot = 0; slot < 7; ++slot) {
                    const double expected = (closed.property("items").property(slot).property("x").toNumber()
                        + full.property("items").property(slot).property("x").toNumber()) / 2;
                    QVERIFY(qAbs(half.property("items").property(slot).property("x").toNumber() - expected) < 0.001);
                }
            }
        }
    }
    void emptyDock() {
        const auto result = geometry(0, 900, 450, 1);
        QCOMPARE(result.property("width").toNumber(), 0);
        QCOMPARE(result.property("items").property("length").toInt(), 0);
    }
    void animationUsesElapsedTime() {
        const auto transition = engine.globalObject().property("transition");
        const auto samples = engine.evaluate("[0,.2,.6,.85,1]");
        auto run = [&](int fps) {
            double elapsed = 0;
            double progress = 0;
            while (elapsed < 200) {
                elapsed += 1000.0 / fps;
                progress = transition.call({elapsed, 167, samples}).toNumber();
            }
            return progress;
        };
        QCOMPARE(run(60), 1);
        QCOMPARE(run(120), 1);
        QCOMPARE(transition.call({83.5, 167, samples}).toNumber(), 0.6);
    }
};
QTEST_GUILESS_MAIN(MagnificationTest)
#include "magnification_test.moc"

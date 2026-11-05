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
    void numericSearchMatchesExpandedGeometry() {
        const auto numeric = engine.globalObject().property("pointerAtFocus");
        const auto shape = engine.globalObject().property("shape");
        const auto anchor = engine.globalObject().property("anchor");
        const auto scales = engine.evaluate("[1.75,1.5,1.2,1.0]");
        for (const int count : {1, 7, 16}) {
            for (const double width : {220.0, 900.0, 1500.0}) {
                for (const double amount : {0.0, 0.01, 0.5, 1.0}) {
                    for (const int gap : {-1, 0, count / 2, count - 1}) {
                        for (const double gapAmount : {0.0, 0.3, 1.0}) {
                            for (double focus = -3; focus <= count + 2; focus += 0.37) {
                                const auto expanded = shape.call({count, 48, 10, width - 32,
                                    focus, amount, scales, gap, gapAmount});
                                const double expected = (width - expanded.property("width").toNumber()) / 2
                                    + anchor.call({expanded, focus, 58}).toNumber();
                                const double actual = numeric.call({count, 48, 10, width - 32,
                                    width, focus, amount, scales, gap, gapAmount}).toNumber();
                                QVERIFY2(std::isfinite(actual) && std::abs(actual - expected) < 1e-9,
                                    qPrintable(QString("count %1 width %2 amount %3 gap %4/%5 focus %6: %7 != %8")
                                        .arg(count).arg(width).arg(amount).arg(gap).arg(gapAmount)
                                        .arg(focus).arg(actual, 0, 'g', 16).arg(expected, 0, 'g', 16)));
                            }
                        }
                    }
                }
            }
        }
    }
    void adaptiveSearchMatchesBisectionGeometry() {
        // Reference the original 20-step search through full geometry,
        // preserving its midpoint and pointer-boundary rounding.
        const auto reference = engine.evaluate(R"JS((function(count, width, pointerX, amount, scales, gap, weight) {
            const available = Math.max(0, width - 32);
            const rest = shape(count, 48, 10, available, 0, 0, scales, gap, weight);
            if (amount <= 0 || rest.width >= available) return rest;
            let low = 1 - scales.length;
            let high = count - 1 - (gap >= 0 ? 1 - weight : 0) + scales.length - 1;
            for (let i = 0; i < 20; ++i) {
                const focus = (low + high) / 2;
                const current = shape(count, 48, 10, available, focus, amount, scales, gap, weight);
                if ((width - current.width) / 2 + anchor(current, focus, 58) < pointerX) low = focus;
                else high = focus;
            }
            return shape(count, 48, 10, available, (low + high) / 2, amount, scales, gap, weight);
        }))JS");
        QVERIFY(reference.isCallable());
        const auto solve = engine.globalObject().property("layout");
        const auto scales = engine.evaluate("[1.75,1.5,1.2,1.0]");
        for (const int count : {1, 7, 16}) {
            for (const int width : {220, 900, 1500}) {
                for (const double amount : {0.01, 0.5, 1.0}) {
                    for (const int gap : {-1, 0, count / 2, count - 1}) {
                        for (const double weight : {0.0, 0.3, 1.0}) {
                            for (int step = 0; step <= 17; ++step) {
                                const double pointer = width * step / 17.0;
                                const auto expected = reference.call({count, width, pointer, amount, scales, gap, weight});
                                const auto actual = solve.call({count, 48, 10, width, 16, pointer, amount, scales, gap, weight});
                                QVERIFY(qAbs(actual.property("width").toNumber() - expected.property("width").toNumber()) < 1e-8);
                                for (int i = 0; i < count; ++i) {
                                    const auto a = actual.property("items").property(i);
                                    const auto b = expected.property("items").property(i);
                                    QVERIFY(qAbs(a.property("x").toNumber() - b.property("x").toNumber()) < 1e-8);
                                    QVERIFY(qAbs(a.property("size").toNumber() - b.property("size").toNumber()) < 1e-8);
                                }
                            }
                        }
                    }
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

#include "dockbackend.h"
#include "tooltipshadow.h"
#include "tuning.h"
#include "ui_test_support.h"
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QFont>
#include <QScreen>
#include <QQuickWindow>
#include <QStyleHints>
#include <QStandardItemModel>

class TouchTaskModel : public QStandardItemModel {
    Q_OBJECT
public:
    QList<int> activations;
    TouchTaskModel() {
        setItemRoleNames({{Qt::DisplayRole, "display"}, {Qt::UserRole + 1, "HasLauncher"},
                          {Qt::UserRole + 2, "AppId"}});
        for (int i = 0; i < 3; ++i) {
            auto *item = new QStandardItem(QString("App %1").arg(i));
            item->setData(true, Qt::UserRole + 1);
            item->setData(QString("app%1").arg(i), Qt::UserRole + 2);
            appendRow(item);
        }
    }
    Q_INVOKABLE void requestActivate(const QModelIndex &index) { activations.append(index.row()); }
};

class TouchUiTest : public QObject {
    Q_OBJECT
private slots:
    void touchGestures() {
        Tuning tuning(":/tuning.json");
        QQmlApplicationEngine engine;
        DockBackend backend(&engine, false);
        TouchTaskModel model;
        backend.setSourceModel(&model);
        engine.addImageProvider("tasks", new TaskIcons(&backend));
        engine.addImageProvider("folderthumbnails", new FolderThumbnails);
        engine.addImageProvider("tooltipshadows", new ToolTipShadows);
        engine.rootContext()->setContextProperty("dockBackend", &backend);
        engine.rootContext()->setContextProperty("tuning", &tuning);
        engine.rootContext()->setContextProperty("previewMode", true);
        engine.load(QUrl("qrc:/qml/PreviewWindow.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *dock = window->findChild<QQuickItem *>("dockContent");
        auto *touch = window->findChild<QObject *>("dockTouchController");
        auto *label = window->findChild<QQuickItem *>("dockToolTip");
        QVERIFY(dock && touch && label);
        QTRY_VERIFY(!dock->property("startingUp").toBool());
        dock->setProperty("autoHideEnabled", false);
        auto items = taskItems(dock);
        QCOMPARE(items.size(), 3);
        const auto center = [&](int index) {
            auto *item = items[index];
            return item->mapToScene(QPointF(item->width()/2, item->height()/2)).toPoint();
        };
        const auto firstShownRow = [&](QObject *menu) -> QQuickItem * {
            for (int i = 0; i < menu->property("count").toInt(); ++i) {
                QQmlExpression expression(engine.rootContext(), menu, QString("itemAt(%1)").arg(i));
                auto *row = qobject_cast<QQuickItem *>(expression.evaluate().value<QObject *>());
                if (row && row->property("shown").toBool()) return row;
            }
            return nullptr;
        };
        auto *device = QTest::createTouchDevice();
        auto events = QTest::touchEvent(window, device, false);
        auto start = center(0);
        events.press(0, start, window).commit();
        QTRY_VERIFY(touch->property("active").toBool());
        QVERIFY(model.activations.isEmpty());
        QCOMPARE(dock->property("pressedTask").toInt(), -1);
        QVERIFY(!dock->property("magnifying").toBool());
        QVERIFY(!touch->property("labelsReady").toBool());
        events.release(0, start, window).commit();
        QTRY_COMPARE(model.activations, QList<int>{0});
        QTest::qWait(700);
        QVERIFY(!touch->property("labelsReady").toBool());
        QVERIFY(!dock->property("magnifying").toBool());

        // A hold reveals the label first, then magnifies. Sliding selects
        // another app without reordering and activates only on release.
        start = center(0);
        events.press(0, start, window).commit();
        QTest::qWait(75);
        QVERIFY(!touch->property("labelsReady").toBool());
        QVERIFY(!dock->property("magnifying").toBool());
        QTRY_VERIFY_WITH_TIMEOUT(touch->property("labelsReady").toBool(), 175);
        QTRY_VERIFY(label->isVisible());
        QCOMPARE(label->property("label").toString(), QString("App 0"));
        QVERIFY(!dock->property("magnifying").toBool());
        QTRY_VERIFY_WITH_TIMEOUT(dock->property("magnifying").toBool(), 250);
        QTest::qWait(200);
        const auto end = center(2);
        events.move(0, end, window).commit();
        QTRY_COMPARE(touch->property("targetIndex").toInt(), 2);
        QCOMPARE(label->property("label").toString(), QString("App 2"));
        QVERIFY(qAbs(dock->property("pointerX").toDouble() - end.x()) < 1.0);
        QVERIFY(!dock->property("dragging").toBool());
        QCOMPARE(model.activations.size(), 1);
        QTest::qWait(250);
        QCOMPARE(dock->property("openMenus").toInt(), 0);
        events.release(0, end, window).commit();
        QTRY_COMPARE(model.activations, (QList<int>{0, 2}));
        QTest::qWait(300);

        // A stationary hold opens the menu; its release must never activate.
        start = center(1);
        events.press(0, start, window).commit();
        QTest::qWait(600);
        events.move(0, start + QPoint(1, 1), window).commit();
        QCOMPARE(dock->property("openMenus").toInt(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(dock->property("openMenus").toInt(), 1, 400);
        events.release(0, start, window).commit();
        QCOMPARE(model.activations.size(), 2);
        auto *menu = items[1]->findChild<QObject *>("taskMenu");
        QVERIFY(menu);
        QCOMPARE(menu->property("menuScale").toDouble(), 2.0);
        auto *row = firstShownRow(menu);
        QVERIFY(row);
        QCOMPARE(row->height(), 38.0);
        QCOMPARE(row->property("font").value<QFont>().pixelSize(), 28);
        auto *options = items[1]->findChild<QObject *>("dockOptionsMenu");
        QVERIFY(options);
        QCOMPARE(options->property("menuScale").toDouble(), 2.0);
        auto *optionRow = firstShownRow(options);
        QVERIFY(optionRow);
        QCOMPARE(optionRow->height(), 38.0);
        QCOMPARE(optionRow->property("font").value<QFont>().pixelSize(), 28);
        QVERIFY(QMetaObject::invokeMethod(options, "open"));
        QTRY_VERIFY(options->property("visible").toBool());
        QCOMPARE(firstShownRow(options)->height(), 38.0);
        QVERIFY(QMetaObject::invokeMethod(options, "close"));
        QTRY_VERIFY(!options->property("visible").toBool());
        QCOMPARE(dock->property("openMenus").toInt(), 1);

        // Long labels and many actions stay within the screen; tall menus
        // retain full-size rows in a scrollable viewport.
        QVariantList actions;
        for (int i = 0; i < 30; ++i)
            actions.append(QVariantMap{{"section", "actions"}, {"text", QString(300, 'W')}, {"token", i}});
        QVERIFY(menu->setProperty("appEntries", actions));
        auto *menuContent = menu->property("contentItem").value<QQuickItem *>();
        QVERIFY(menuContent);
        QTRY_VERIFY(menuContent->property("contentHeight").toDouble() > menuContent->height());
        QVERIFY(menu->property("width").toDouble() <= window->screen()->geometry().width() - 8);
        QVERIFY(menu->property("height").toDouble() <= window->screen()->geometry().height() - 8);
        QVERIFY(QMetaObject::invokeMethod(menu, "close"));
        QTRY_COMPARE(dock->property("openMenus").toInt(), 0);
        QTest::qWait(300);

        // Reopening the same menu with a mouse restores ordinary metrics.
        QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, center(1));
        QTRY_VERIFY(menu->property("visible").toBool());
        QCOMPARE(menu->property("menuScale").toDouble(), 1.0);
        row = firstShownRow(menu);
        QVERIFY(row);
        QCOMPARE(row->height(), 19.0);
        QCOMPARE(row->property("font").value<QFont>().pixelSize(), 14);
        QVERIFY(QMetaObject::invokeMethod(menu, "close"));
        QTRY_COMPARE(dock->property("openMenus").toInt(), 0);
        QTest::qWait(300);

        // Continued browsing postpones the menu beyond the original hold
        // deadline. Settling over the current icon starts a fresh idle hold.
        start = center(0);
        events.press(0, start, window).commit();
        events.move(0, center(2), window).commit();
        const int wobble = QGuiApplication::styleHints()->startDragDistance() + 2;
        for (int i = 0; i < 3; ++i) {
            QTest::qWait(300);
            QCOMPARE(dock->property("openMenus").toInt(), 0);
            events.move(0, center(2) + QPoint(i % 2 ? -wobble : wobble, 0), window).commit();
            QTRY_COMPARE(touch->property("targetIndex").toInt(), 2);
            QVERIFY(!dock->property("dragging").toBool());
        }
        const auto resting = center(2);
        events.move(0, resting, window).commit();
        QTest::qWait(600);
        QCOMPARE(dock->property("openMenus").toInt(), 0);
        events.move(0, resting + QPoint(1, 1), window).commit();
        QTRY_COMPARE_WITH_TIMEOUT(dock->property("openMenus").toInt(), 1, 400);
        auto *idleMenu = items[2]->findChild<QObject *>("taskMenu");
        QVERIFY(idleMenu && idleMenu->property("visible").toBool());
        events.release(0, resting, window).commit();
        QCOMPARE(model.activations.size(), 2);
        QVERIFY(QMetaObject::invokeMethod(idleMenu, "close"));
        QTRY_COMPARE(dock->property("openMenus").toInt(), 0);
        QTest::qWait(300);

        // Releasing outside cancels. Multiple fingers and platform cancellation
        // consume the whole gesture as well.
        start = center(0);
        events.press(0, start, window).commit();
        events.move(0, QPoint(2, 2), window).commit();
        events.release(0, QPoint(2, 2), window).commit();
        QCOMPARE(model.activations.size(), 2);
        events.press(0, start, window).commit();
        events.stationary(0).press(1, center(1), window).commit();
        events.release(0, start, window).stationary(1).commit();
        events.release(1, center(1), window).commit();
        QCOMPARE(model.activations.size(), 2);
        events.press(0, start, window).commit();
        QTouchEvent cancel(QEvent::TouchCancel, device);
        QCoreApplication::sendEvent(window, &cancel);
        QTRY_VERIFY(!touch->property("active").toBool());
        QCOMPARE(model.activations.size(), 2);

        // Trash uses the same touch hold without leaking into task activation.
        auto *trash = window->findChild<QQuickItem *>("trashIcon");
        QVERIFY(trash);
        const auto trashCenter = trash->mapToScene(QPointF(trash->width()/2, trash->height()/2)).toPoint();
        events.press(0, trashCenter, window).commit();
        QTRY_COMPARE_WITH_TIMEOUT(dock->property("openMenus").toInt(), 1, 1000);
        events.release(0, trashCenter, window).commit();
        auto *trashMenu = window->findChild<QObject *>("trashMenu");
        QVERIFY(trashMenu);
        QCOMPARE(trashMenu->property("menuScale").toDouble(), 2.0);
        QVERIFY(firstShownRow(trashMenu));
        QCOMPARE(firstShownRow(trashMenu)->height(), 38.0);
        QVERIFY(QMetaObject::invokeMethod(trashMenu, "close"));
        QTRY_COMPARE(dock->property("openMenus").toInt(), 0);
        QCOMPARE(model.activations.size(), 2);

        // Real mouse input still activates normally after touch.
        QTest::mouseMove(window, QPoint(2, 2));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, center(1));
        QTRY_COMPARE(model.activations, (QList<int>{0, 2, 1}));
        // The reveal strip accepts touch without needing mouse hover. Keep
        // holding through the reveal, then slide onto an icon and lift.
        QTest::mouseMove(window, QPoint(2, 2));
        dock->setProperty("autoHideEnabled", true);
        dock->setProperty("hideRequested", true);
        QTRY_COMPARE(dock->property("hideOffset").toDouble(), dock->height());
        const QPoint edge(window->width()/2, window->height()-1);
        events.press(0, edge, window).commit();
        QTRY_VERIFY(!dock->property("hideRequested").toBool());
        QTRY_COMPARE(dock->property("hideOffset").toDouble(), 0.0);
        QVERIFY(touch->property("active").toBool());
        const auto revealed = center(0);
        events.move(0, revealed, window).commit();
        QTRY_COMPARE(touch->property("targetIndex").toInt(), 0);
        events.release(0, revealed, window).commit();
        QTRY_COMPARE(model.activations, (QList<int>{0, 2, 1, 0}));
        // A tap on the enlarged Open row hits the matching action and runs
        // once, on release, through the popup's own touch handling.
        dock->setProperty("autoHideEnabled", false);
        QTest::qWait(300);
        const auto last = center(2);
        events.press(0, last, window).commit();
        QTRY_COMPARE_WITH_TIMEOUT(dock->property("openMenus").toInt(), 1, 1100);
        events.release(0, last, window).commit();
        auto *lastMenu = items[2]->findChild<QObject *>("taskMenu");
        QVERIFY(lastMenu);
        auto *openRow = firstShownRow(lastMenu);
        QVERIFY(openRow && openRow->window());
        QCOMPARE(openRow->property("text").toString(), QString("Open"));
        auto *popup = openRow->window();
        const auto tap = openRow->mapToScene(QPointF(openRow->width()/2, openRow->height()/2)).toPoint();
        auto menuTouch = QTest::touchEvent(popup, device, false);
        menuTouch.press(0, tap, popup).commit();
        QCOMPARE(model.activations.size(), 4);
        menuTouch.release(0, tap, popup).commit();
        QTRY_COMPARE(model.activations, (QList<int>{0, 2, 1, 0, 2}));
        QTRY_COMPARE(dock->property("openMenus").toInt(), 0);
        delete window;
    }
};

int main(int argc, char **argv) { return runDockUiTest<TouchUiTest>(argc, argv); }
#include "touch_ui_test.moc"

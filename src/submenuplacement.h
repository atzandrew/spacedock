#pragma once

#include <QEvent>
#include <QPlatformSurfaceEvent>
#include <QQuickItem>
#include <QQuickWindow>

// On Wayland, Qt ignores Menu.x/y. A submenu is placed by the compositor from
// its row: by default it hangs down from the row's top-right corner, and with
// the dock below there is rarely room, so KWin slides it up to rest on the
// shelf. Where it lands relative to its row then depends on how many rows sit
// under it. Grow submenus upward from the row's bottom-right corner instead,
// moved down by the menu's submenuDrop (qml/DockMenu.qml).
//
// These window properties are read when Qt creates the native popup, which
// happens before Menu.aboutToShow, so they are set from window events. Show
// arrives before the native popup on every open, so the row rect stays current
// if rows above it appear or disappear between opens.
class SubmenuPlacement : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *object, QEvent *event) override {
        const bool surfaceCreated = event->type() == QEvent::PlatformSurface
            && static_cast<QPlatformSurfaceEvent *>(event)->surfaceEventType()
                == QPlatformSurfaceEvent::SurfaceCreated;
        if (surfaceCreated || event->type() == QEvent::Show) {
            auto *window = qobject_cast<QQuickWindow *>(object);
            // A popup window opened from another popup window is a submenu.
            if (window && window->inherits("QQuickPopupWindow") && window->transientParent()
                && window->transientParent()->inherits("QQuickPopupWindow"))
                place(window);
        }
        return false;
    }

private:
    static QQuickItem *findPanel(QQuickItem *item) {
        if (item->objectName() == QLatin1String("dockMenuPanel")) return item;
        for (QQuickItem *child : item->childItems())
            if (QQuickItem *found = findPanel(child)) return found;
        return nullptr;
    }

    static void place(QQuickWindow *window) {
        window->setProperty("_q_waylandPopupAnchor",
            QVariant::fromValue(Qt::Edges(Qt::BottomEdge | Qt::RightEdge)));
        window->setProperty("_q_waylandPopupGravity",
            QVariant::fromValue(Qt::Edges(Qt::TopEdge | Qt::RightEdge)));

        // The row is the menu's parent item and lives in the parent menu's
        // window, whose scene coordinates the anchor rect uses.
        QQuickItem *panel = findPanel(window->contentItem());
        QObject *menu = panel ? qvariant_cast<QObject *>(panel->property("dockMenu")) : nullptr;
        QQuickItem *row = menu ? qvariant_cast<QQuickItem *>(menu->property("parent")) : nullptr;
        if (!row) return;
        const QRectF rect = row->mapRectToScene(QRectF(QPointF(), row->size()))
            .translated(0, menu->property("submenuDrop").toReal());
        window->setProperty("_q_waylandPopupAnchorRect", rect.toAlignedRect());
    }
};

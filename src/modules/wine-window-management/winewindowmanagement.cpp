// Copyright (C) 2026 UnionTech Software Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0 OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "winewindowmanagement.h"

#include "common/treelandlogging.h"
#include "core/rootsurfacecontainer.h"
#include "core/waylandserver.h"
#include "output/output.h"
#include "qwayland-server-treeland-wine-window-management-unstable-v1.h"
#include "surface/surfacewrapper.h"

#include <wxdgtoplevelsurface.h>

#include <QList>
#include <QPointF>
#include <QSizeF>

WAYLIB_SERVER_USE_NAMESPACE

namespace {

// Fits the whole window into the work area (output geometry minus the exclusive
// zones of layer-shell surfaces such as the dock) of the output it is heading
// to. This mirrors what an X11 window manager does while placing a window: the
// client asks for a position that may overlap a panel, the compositor decides
// the final one. Windows larger than the work area are aligned to its origin
// instead of being resized, as their size is owned by the client (xdg_toplevel
// configure).
QPointF clampToWorkArea(const QPointF &pos, const QSizeF &size, QRectF &workArea)
{
    auto *server = WaylandServer::instance();
    auto *root = server ? server->rootSurfaceContainer() : nullptr;
    Output *output = root ? root->outputAt(pos) : nullptr;
    if (!output) {
        return pos;
    }

    workArea = output->validGeometry();
    if (!workArea.isValid()) {
        return pos;
    }

    QPointF clamped = pos;
    if (size.width() <= workArea.width()) {
        clamped.setX(qBound(workArea.left(), clamped.x(), workArea.right() - size.width()));
    } else {
        clamped.setX(workArea.left());
    }
    if (size.height() <= workArea.height()) {
        clamped.setY(qBound(workArea.top(), clamped.y(), workArea.bottom() - size.height()));
    } else {
        clamped.setY(workArea.top());
    }
    return clamped;
}

} // namespace

class WineWindowControl;

class WineWindowManagerPrivate : public QtWaylandServer::treeland_wine_window_manager_v1
{
public:
    explicit WineWindowManagerPrivate(WineWindowManager *q)
        : q(q)
    {
    }

    wl_global *globalHandle() const
    {
        return m_global;
    }

    uint32_t nextWindowId()
    {
        return ++m_nextId;
    }

    WineWindowControl *controlForId(uint32_t id) const
    {
        for (const auto &entry : m_controls)
            if (entry.id == id)
                return entry.control;
        return nullptr;
    }

    void registerControl(uint32_t id, WXdgToplevelSurface *toplevel, WineWindowControl *control)
    {
        m_controls.append({ id, toplevel, control });
    }

    void removeControl(WineWindowControl *control)
    {
        for (auto it = m_controls.begin(); it != m_controls.end(); ++it) {
            if (it->control == control) {
                m_controls.erase(it);
                return;
            }
        }
    }

protected:
    void destroy_global() override
    {
        qCDebug(lcTlProtocol) << "WineWindowManager global destroyed";
    }

    void destroy(Resource *resource) override
    {
        wl_resource_destroy(resource->handle);
    }

    void get_window_control(Resource *resource,
                            uint32_t id,
                            struct ::wl_resource *toplevelResource) override;

private:
    struct ControlEntry
    {
        uint32_t id;
        WXdgToplevelSurface *toplevel{ nullptr };
        WineWindowControl *control{ nullptr };
    };

    WineWindowManager *q{ nullptr };
    uint32_t m_nextId = 0;
    QList<ControlEntry> m_controls;
};

class WineWindowControl
    : public QObject
    , public QtWaylandServer::treeland_wine_window_control_v1
{
public:
    WineWindowControl(QObject *parent,
                      WineWindowManagerPrivate *manager,
                      uint32_t windowId,
                      SurfaceWrapper *wrapper,
                      wl_client *client,
                      int version,
                      uint32_t id)
        : QObject(parent)
        , QtWaylandServer::treeland_wine_window_control_v1(client, id, version)
        , m_manager(manager)
        , m_windowId(windowId)
        , m_wrapper(wrapper)
    {
        Q_ASSERT_X(m_wrapper, Q_FUNC_INFO, "wrapper must not be null");
        Q_ASSERT_X(m_wrapper->shellSurface(),
                   Q_FUNC_INFO,
                   "wrapper->shellSurface() must not be null");

        connect(m_wrapper, &SurfaceWrapper::aboutToBeInvalidated, this, [this] {
            if (m_manager) {
                m_manager->removeControl(this);
                m_manager = nullptr;
            }
            m_wrapper = nullptr;
        });
        connect(m_wrapper->surface(),
                &WSurface::mappedChanged,
                this,
                &WineWindowControl::handleMappedChanged);
        if (auto *surfaceItem = m_wrapper->surfaceItem()) {
            // The clamp needs the committed size, which is only available once
            // the surface item became ready.
            connect(surfaceItem, &WSurfaceItem::readyChanged, this, &WineWindowControl::clampOnMap);
        }
        connect(m_wrapper, &QQuickItem::xChanged, this, &WineWindowControl::handleItemMoved);
        connect(m_wrapper, &QQuickItem::yChanged, this, &WineWindowControl::handleItemMoved);
        connect(m_wrapper, &SurfaceWrapper::alwaysOnTopChanged, this, [this] {
            sendConfigureStacking();
        });

        // Initial event sequence: window_id → configure_position → configure_stacking
        send_window_id(m_windowId);
        sendConfigurePosition();
        sendConfigureStacking();
    }

    uint32_t windowId() const
    {
        return m_windowId;
    }

    SurfaceWrapper *wrapper() const
    {
        return m_wrapper;
    }

protected:
    void destroy(Resource *resource) override
    {
        wl_resource_destroy(resource->handle);
    }

    void destroy_resource([[maybe_unused]] Resource *resource) override
    {
        if (m_manager) {
            m_manager->removeControl(this);
            m_manager = nullptr;
        }
        delete this;
    }

    void set_position([[maybe_unused]] Resource *resource,
                      int32_t x,
                      int32_t y,
                      uint32_t serial) override
    {
        if (!m_wrapper) {
            return;
        }

        const QPointF requested(x, y);
        const QSizeF size(m_wrapper->width(), m_wrapper->height());
        QRectF workArea;
        const QPointF fitted = clampToWorkArea(requested, size, workArea);
        const FitRequest fit = updateFitRequest(requested, fitted);

        qCDebug(lcTlProtocol) << "set_position" << requested << "window size" << size << "work area"
                              << workArea << "fitted position" << fitted << fitRequestName(fit)
                              << "for window_id" << m_windowId;

        moveWrapperTo(fit == FitRequest::None ? requested : fitted);
        // Echo the client serial back in configure_position, even when adjusted.
        sendConfigurePosition(serial);
    }

    void set_z_order([[maybe_unused]] Resource *resource, uint32_t op, uint32_t sibling_id) override
    {
        if (!m_wrapper) {
            return;
        }

        const auto zOrderOp =
            static_cast<QtWaylandServer::treeland_wine_window_control_v1::z_order_op>(op);

        // Validate sibling_id: must be 0 for non-insert_after ops
        if (op != z_order_op_hwnd_insert_after && sibling_id != 0) {
            wl_resource_post_error(resource->handle,
                                   error_invalid_sibling,
                                   "sibling_id must be 0 for op %u",
                                   op);
            return;
        }

        switch (zOrderOp) {
        case z_order_op_hwnd_top:
            applyHwndTop();
            // hwnd_top does not change tier → no configure_stacking
            break;
        case z_order_op_hwnd_bottom:
            applyHwndBottom();
            break;
        case z_order_op_hwnd_topmost:
            m_wrapper->setAlwaysOnTop(true);
            applyHwndTop();
            break;
        case z_order_op_hwnd_notopmost:
            m_wrapper->setAlwaysOnTop(false);
            applyHwndTop();
            break;
        case z_order_op_hwnd_insert_after:
            applyHwndInsertAfter(sibling_id);
            // hwnd_insert_after does not change tier → no configure_stacking
            break;
        }
    }

private:
    // How a client position request is answered.
    enum class FitRequest
    {
        None,     // honoured verbatim
        First,    // fitted: first placement request after the surface was mapped
        Repeated, // fitted again: the client repeated an already fitted request
    };

    static const char *fitRequestName(FitRequest fit)
    {
        switch (fit) {
        case FitRequest::First:
            return "fitted";
        case FitRequest::Repeated:
            return "fitted again, repeated request";
        case FitRequest::None:
            return "not fitted";
        }
        return "not fitted";
    }

    // Decides whether a client position request is fitted into the work area and
    // remembers it for later repetitions.
    //
    // Only the first request after a map is fitted: Wine also parks windows
    // outside the screen to emulate minimizing, so every other position is a
    // decision of the client and is honoured as it is. The exception is a
    // request that was fitted before and is now repeated, because Wine re-sends
    // its original position after each configure and honouring it verbatim would
    // undo the correction.
    FitRequest updateFitRequest(const QPointF &requested, const QPointF &fitted)
    {
        // Fullscreen / maximized / tiled windows get their geometry from the
        // compositor, so their position must not be overridden here.
        const bool firstPlacement =
            !m_firstPositionDone && m_wrapper->surfaceState() == SurfaceWrapper::State::Normal;
        m_firstPositionDone = true;

        if (firstPlacement) {
            m_fittedRequest = requested;
            m_hasFittedRequest = fitted != requested;
            return FitRequest::First;
        }

        if (m_hasFittedRequest && requested == m_fittedRequest)
            return FitRequest::Repeated;
        return FitRequest::None;
    }

    void handleMappedChanged()
    {
        if (!m_wrapper)
            return;
        // A surface may be unmapped and mapped again (Wine hides windows that
        // way): fit the position again on every map and treat the first
        // set_position after a map as the first placement request.
        if (!m_wrapper->surface()->mapped()) {
            m_firstPositionDone = false;
            m_mapClampPending = false;
            m_hasFittedRequest = false;
            return;
        }

        m_mapClampPending = true;
        clampOnMap();
    }

    void handleItemMoved()
    {
        if (!m_suppressPositionEvents)
            sendConfigurePosition();
    }

    bool surfaceItemReady() const
    {
        auto *surfaceItem = m_wrapper->surfaceItem();
        return surfaceItem && surfaceItem->isReady();
    }

    void moveWrapperTo(const QPointF &pos)
    {
        m_wrapper->setPositionAutomatic(false);
        // Suppress only this class's xChanged/yChanged handlers to avoid
        // emitting a serial=0 event alongside the one sent by the caller.
        m_suppressPositionEvents = true;
        m_wrapper->setPosition(pos);
        m_suppressPositionEvents = false;
    }

    // Fits the current position into the work area once the surface is mapped
    // and its item reports the committed size.
    void clampOnMap()
    {
        if (!m_mapClampPending || !m_wrapper)
            return;
        if (m_wrapper->surfaceState() != SurfaceWrapper::State::Normal) {
            m_mapClampPending = false;
            return;
        }

        if (!surfaceItemReady()) {
            // readyChanged fires once the committed size is known, the clamp is
            // retried from there.
            qCDebug(lcTlProtocol) << "clamp on map deferred until the surface item is ready,"
                                  << "window_id" << m_windowId;
            return;
        }

        const QSizeF size(m_wrapper->width(), m_wrapper->height());
        if (size.width() <= 0 || size.height() <= 0) {
            qCDebug(lcTlProtocol) << "clamp on map skipped, window size" << size << "is not valid,"
                                  << "window_id" << m_windowId;
            return;
        }
        m_mapClampPending = false;

        const QPointF current(m_wrapper->x(), m_wrapper->y());
        QRectF workArea;
        const QPointF fitted = clampToWorkArea(current, size, workArea);
        qCDebug(lcTlProtocol) << "clamp on map" << current << "window size" << size << "work area"
                              << workArea << "fitted position" << fitted << "for window_id"
                              << m_windowId;
        if (fitted == current)
            return;

        moveWrapperTo(fitted);
        // Keep the fitted position authoritative for the original coordinate:
        // Wine repeats its request once it sees the new configure.
        m_fittedRequest = current;
        m_hasFittedRequest = true;
        // Compositor-initiated correction, not the answer to a set_position.
        sendConfigurePosition();
    }

    // serial=0 means compositor-initiated (not in response to a client set_position)
    void sendConfigurePosition(uint32_t serial = 0)
    {
        if (m_wrapper) {
            send_configure_position(static_cast<int32_t>(m_wrapper->x()),
                                    static_cast<int32_t>(m_wrapper->y()),
                                    serial);
        } else {
            send_configure_position(0, 0, serial);
        }
    }

    void sendConfigureStacking()
    {
        bool topmost = m_wrapper && m_wrapper->alwaysOnTop();
        send_configure_stacking(topmost ? 1u : 0u);
    }

    // Raise to top of current stacking tier (no tier change)
    void applyHwndTop()
    {
        if (!m_wrapper)
            return;
        m_wrapper->stackToLast();
    }

    // Lower to absolute bottom, clear topmost tier.
    void applyHwndBottom()
    {
        if (!m_wrapper)
            return;
        m_wrapper->setAlwaysOnTop(false);
        m_wrapper->stackToFirst();
    }

    // Place this window directly below the identified sibling.
    // Matches Windows SetWindowPos semantics: top-level windows may only be
    // ordered relative to other top-level peers, and child windows only
    // relative to same-parent siblings. Cross-group requests are ignored.
    void applyHwndInsertAfter(uint32_t siblingId)
    {
        if (!m_manager || !m_wrapper)
            return;

        WineWindowControl *sibling = m_manager->controlForId(siblingId);
        // If sibling is not found, the request has no effect (e.g. the
        // sibling window was destroyed before this message arrived).
        if (!sibling || !sibling->wrapper())
            return;
        if (sibling == this)
            return;

        SurfaceWrapper *siblingWrapper = sibling->wrapper();

        // Protocol set_z_order: child windows may only be ordered relative to
        // same-parent siblings. An independent top-level has no parentSurface,
        // so the check only fires when m_wrapper is a child and its top-level
        // ancestor differs from the sibling's.
        if (m_wrapper->parentSurface()
            && topLevelSurface(m_wrapper) != topLevelSurface(siblingWrapper)) {
            qCWarning(lcTlProtocol)
                << "hwnd_insert_after: child window" << m_windowId << "and sibling_id" << siblingId
                << "belong to different top-level groups; ignoring";
            return;
        }

        // Protocol set_z_order: hwnd_insert_after must not mix tiers.
        if (m_wrapper->effectiveAlwaysOnTop() != siblingWrapper->effectiveAlwaysOnTop()) {
            qCWarning(lcTlProtocol) << "hwnd_insert_after: window" << m_windowId << "and sibling_id"
                                    << siblingId << "belong to different tiers; ignoring";
            return;
        }

        // Safety check: both windows must share the same Qt parent container.
        if (!m_wrapper->parentItem() || siblingWrapper->parentItem() != m_wrapper->parentItem()) {
            qCWarning(lcTlProtocol) << "hwnd_insert_after: window" << m_windowId << "and sibling_id"
                                    << siblingId << "do not share a parent container; ignoring";
            return;
        }

        // Place this window before the sibling (visually below sibling).
        if (!m_wrapper->stackBefore(siblingWrapper)) {
            qCWarning(lcTlProtocol) << "hwnd_insert_after: stackBefore failed for window"
                                    << m_windowId << "(sibling_id =" << siblingId << ")";
        }
    }

    static SurfaceWrapper *topLevelSurface(SurfaceWrapper *wrapper)
    {
        while (wrapper && wrapper->parentSurface())
            wrapper = wrapper->parentSurface();
        return wrapper;
    }

private:
    WineWindowManagerPrivate *m_manager = nullptr;
    uint32_t m_windowId = 0;
    SurfaceWrapper *m_wrapper = nullptr;
    bool m_firstPositionDone = false;
    bool m_mapClampPending = false;
    bool m_suppressPositionEvents = false;
    QPointF m_fittedRequest;
    bool m_hasFittedRequest = false;
};

void WineWindowManagerPrivate::get_window_control(Resource *resource,
                                                  uint32_t id,
                                                  struct ::wl_resource *toplevelResource)
{
    auto *qXdgToplevel = wlr_xdg_toplevel_from_resource(toplevelResource);
    if (!qXdgToplevel) {
        wl_resource_post_error(resource->handle,
                               TREELAND_WINE_WINDOW_MANAGER_V1_ERROR_DEFUNCT_TOPLEVEL,
                               "invalid or defunct toplevel");
        return;
    }

    auto *xdgToplevel = WXdgToplevelSurface::fromHandle(qXdgToplevel);
    auto *server = WaylandServer::instance();
    auto *root = server ? server->rootSurfaceContainer() : nullptr;
    auto *wrapper = (root && xdgToplevel) ? root->getSurface(xdgToplevel) : nullptr;

    if (!xdgToplevel || !wrapper || !wrapper->shellSurface()) {
        wl_resource_post_error(resource->handle,
                               TREELAND_WINE_WINDOW_MANAGER_V1_ERROR_DEFUNCT_TOPLEVEL,
                               "invalid or defunct toplevel");
        return;
    }

    const bool alreadyControlled = [&] {
        for (const auto &e : m_controls)
            if (e.toplevel == xdgToplevel)
                return true;
        return false;
    }();
    if (alreadyControlled) {
        wl_resource_post_error(resource->handle,
                               TREELAND_WINE_WINDOW_MANAGER_V1_ERROR_TOPLEVEL_ALREADY_CONTROLLED,
                               "toplevel already controlled");
        return;
    }

    uint32_t windowId = nextWindowId();
    auto *control = new WineWindowControl(q,
                                          this,
                                          windowId,
                                          wrapper,
                                          resource->client(),
                                          resource->version(),
                                          id);
    registerControl(windowId, xdgToplevel, control);

    qCDebug(lcTlProtocol) << "Created WineWindowControl for toplevel, window_id =" << windowId;
}

// ---------------------------------------------------------------------------
// WineWindowManager public interface
// ---------------------------------------------------------------------------

WineWindowManager::WineWindowManager(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<WineWindowManagerPrivate>(this))
{
}

WineWindowManager::~WineWindowManager() = default;

void WineWindowManager::create(WServer *server)
{
    d->init(server->handle(), InterfaceVersion);
}

void WineWindowManager::destroy(WServer * /*server*/)
{
    d->globalRemove();
}

wl_global *WineWindowManager::global() const
{
    return d->globalHandle();
}

QByteArrayView WineWindowManager::interfaceName() const
{
    return QtWaylandServer::treeland_wine_window_manager_v1::interfaceName();
}

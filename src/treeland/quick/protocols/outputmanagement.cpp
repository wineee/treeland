// Copyright (C) 2023 rewine <luhongxu@deepin.org>.
// SPDX-License-Identifier: Apache-2.0 OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "outputmanagement.h"

#include "output-management-protocol.h"

#include <qwdisplay.h>
#include <sys/socket.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wayland-util.h>
#include <wserver.h>

#include <QDebug>

TreelandOutputManager::TreelandOutputManager(QObject *parent)
    : Waylib::Server::WQuickWaylandServerInterface(parent)
    , m_impl(new treeland_output_manager_v1)
{

}

treeland_output_manager_v1 *TreelandOutputManager::impl()
{
    return m_impl;
}

void TreelandOutputManager::create()
{
    m_impl->global = wl_global_create(
        server()->handle()->handle(), &treeland_output_manager_v1_interface,
        TREELAND_OUTPUT_MANAGER_V1_VERSION, m_impl, output_manager_bind);

    //wl_list_init(&m_impl->contexts);
    wl_signal_init(&m_impl->events.set_primary_output);
    wl_signal_init(&m_impl->events.destroy);

    m_impl->display_destroy.notify = output_manager_handle_display_destroy;
    wl_display_add_destroy_listener(server()->handle()->handle(),
                                    &m_impl->display_destroy);
}

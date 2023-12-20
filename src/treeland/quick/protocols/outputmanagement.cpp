// Copyright (C) 2023 rewine <luhongxu@deepin.org>.
// SPDX-License-Identifier: Apache-2.0 OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "outputmanagement.h"

#include "output-management-protocol.h"

#include <QtQml/qqmlinfo.h>
#include <qwdisplay.h>
#include <wayland-server-core.h>
#include <wserver.h>
#include <woutput.h>

#include <QDebug>

TreelandOutputManager::TreelandOutputManager(QObject *parent)
    : Waylib::Server::WQuickWaylandServerInterface(parent)
    , m_handle(new treeland_output_manager_v1)
{

}

void TreelandOutputManager::on_set_primary_output(void *data)
{
    const char *name = *(static_cast<const char **>(data));
    Q_EMIT requestSetPrimaryOutput(name);
}

const char *TreelandOutputManager::primaryOutput()
{
    return m_handle->primary_output_name;
}

bool TreelandOutputManager::setPrimaryOutput(const char *name)
{
    for (auto *output : m_outputs)
        if (strcmp(output->nativeHandle()->name, name) == 0) {
            treeland_output_manager_v1_set_primary_output(m_handle, output->nativeHandle()->name);
            return true;
        }
    qmlWarning(this) << "Try to Set Unkonw output PrimaryOutput" << name;
    return false;
}

void TreelandOutputManager::newOutput(WAYLIB_SERVER_NAMESPACE::WOutput *output)
{
    m_outputs.append(output);
    if (m_handle->primary_output_name == nullptr)
        m_handle->primary_output_name = output->nativeHandle()->name;
}

void TreelandOutputManager::removeOutput(WAYLIB_SERVER_NAMESPACE::WOutput *output)
{
    m_outputs.removeOne(output);

    if (m_handle->primary_output_name == output->nativeHandle()->name) {
        if (m_outputs.isEmpty()) {
            m_handle->primary_output_name = nullptr;
        } else {
            m_handle->primary_output_name = m_outputs.first()->nativeHandle()->name;
        }
        Q_EMIT primaryOutputChanged();
    }
}

void TreelandOutputManager::create()
{
    m_handle->global = wl_global_create(
        server()->handle()->handle(), &treeland_output_manager_v1_interface,
        TREELAND_OUTPUT_MANAGER_V1_VERSION, m_handle, output_manager_bind);

    wl_list_init(&m_handle->resources);
    wl_signal_init(&m_handle->events.set_primary_output);
    wl_signal_init(&m_handle->events.destroy);
    m_sc.connect(&m_handle->events.set_primary_output, this, &TreelandOutputManager::on_set_primary_output);

    m_handle->display_destroy.notify = output_manager_handle_display_destroy;
    wl_display_add_destroy_listener(server()->handle()->handle(),
                                    &m_handle->display_destroy);
}

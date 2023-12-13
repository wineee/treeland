// Copyright (C) 2023 rewine <luhongxu@deepin.org>.
// SPDX-License-Identifier: Apache-2.0 OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "output_manager_impl.h"
#include "utils.h"

#include <QDebug>
#include <wayland-server-core.h>

struct treeland_output_manager_v1 *output_manager_from_resource(
        struct wl_resource *resource);


// void socket_context_resource_destroy(struct wl_resource *resource) {
//     struct treeland_socket_context_v1 *security_context = socket_context_from_resource(resource);
//     context_destroy(security_context);
// }

void output_manager_handle_set_primary_output(struct wl_client *client,
                           struct wl_resource *resource,
                           const char *output){
    qDebug() << "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee";
    qDebug() << "output:" << output;
    treeland_output_manager_v1_send_primary_output(resource, output);
}

static const struct treeland_output_manager_v1_interface output_manager_impl {
    .set_primary_output = output_manager_handle_set_primary_output,
    .destroy = resource_handle_destroy,
};

struct treeland_output_manager_v1 *output_manager_from_resource(
        struct wl_resource *resource) {
    assert(wl_resource_instance_of(resource, &treeland_output_manager_v1_interface, &output_manager_impl));
    struct treeland_output_manager_v1 *manager = static_cast<treeland_output_manager_v1*>(wl_resource_get_user_data(resource));
    assert(manager != NULL);
    return manager;
}

void output_manager_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id) {
    struct treeland_output_manager_v1 *manager = static_cast<treeland_output_manager_v1*>(data);

    struct wl_resource *resource = wl_resource_create(client, &treeland_output_manager_v1_interface, version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &output_manager_impl, manager, NULL);
    qDebug() << "binding!!!!!!!!!!!!!!!!!!";
    treeland_output_manager_v1_send_primary_output(resource, "output init!");
}

void output_manager_handle_display_destroy(struct wl_listener *listener, [[maybe_unused]] void *data)
{
    struct treeland_output_manager_v1 *manager =
        wl_container_of(listener, manager, display_destroy);
    wl_signal_emit_mutable(&manager->events.destroy, manager);
    assert(wl_list_empty(&manager->events.destroy.listener_list));

    // struct treeland_socket_context_v1 *context;
    // struct treeland_socket_context_v1 *tmp;
    // wl_list_for_each_safe(context, tmp, &manager->contexts, link) {
    //     context_destroy(context);
    // }

    wl_global_destroy(manager->global);
    wl_list_remove(&manager->display_destroy.link);
    free(manager);
}

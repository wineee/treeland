// Copyright (C) 2023 rewine <luhongxu@deepin.org>.
// SPDX-License-Identifier: Apache-2.0 OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#pragma once

#include <cassert>
#include "output-management-protocol.h"

#define TREELAND_OUTPUT_MANAGER_V1_VERSION 1

struct treeland_output_manager_v1 {
  struct wl_global *global;

  struct {
    struct wl_signal set_primary_output;
    struct wl_signal destroy;
  } events;

  //

  void *data;

  struct wl_listener display_destroy;
};

struct treeland_output_manager_v1 *output_manager_from_resource(struct wl_resource *resource);

void output_manager_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id);
void output_manager_handle_display_destroy(struct wl_listener *listener, void *data);

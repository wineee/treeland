// Copyright (C) 2023 rewine <luhongxu@deepin.org>.
// SPDX-License-Identifier: Apache-2.0 OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#pragma once

#include <QObject>
#include <QQmlEngine>
#include <wquickwaylandserver.h>

#include "output_manager_impl.h"

class TreelandOutputManager : public Waylib::Server::WQuickWaylandServerInterface {
    Q_OBJECT

    QML_ELEMENT

public:
    explicit TreelandOutputManager(QObject *parent = nullptr);

    treeland_output_manager_v1 *impl();

//Q_SIGNALS:
    //void newSocket(const QString &username, int fd);

protected:
    void create() override;

private:
    treeland_output_manager_v1 *m_impl;
};

/****************************************************************************
**
** Copyright (C) 2017 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
****************************************************************************/
#pragma once

#include "androidconfigurations.h"
#include "androidsdkmanager.h"

#include <QWidget>
#include <QFutureWatcher>

namespace Utils { class OutputFormatter; }

namespace Android {
namespace Internal {

class AndroidSdkManager;
namespace Ui {
    class AndroidSdkManagerWidget;
}

class AndroidSdkModel;

class AndroidSdkManagerWidget : public QWidget
{
    Q_OBJECT

    enum View {
        PackageListing,
        Operations
    };

public:
    AndroidSdkManagerWidget(AndroidConfig &config, AndroidSdkManager *sdkManager,
                            QWidget *parent = nullptr);
    ~AndroidSdkManagerWidget();

    void setSdkManagerControlsEnabled(bool enable);

signals:
    void updatingSdk();
    void updatingSdkFinished();

private:
    void onApplyButton();
    void onUpdatePackages();
    void onCancel();
    void onNativeSdkManager();
    void onOperationResult(int index);
    void onSdkManagerOptions();
    void addPackageFuture(const QFuture<AndroidSdkManager::OperationOutput> &future);
    void notifyOperationFinished();
    void packageFutureFinished();
    void cancelPendingOperations();
    void switchView(View view);
    View currentView() const;

    AndroidConfig &m_androidConfig;
    AndroidSdkManager *m_sdkManager = nullptr;
    AndroidSdkModel *m_sdkModel = nullptr;
    Ui::AndroidSdkManagerWidget *m_ui = nullptr;
    Utils::OutputFormatter *m_formatter = nullptr;
    QFutureWatcher<AndroidSdkManager::OperationOutput> *m_currentOperation = nullptr;
};

} // namespace Internal
} // namespace Android

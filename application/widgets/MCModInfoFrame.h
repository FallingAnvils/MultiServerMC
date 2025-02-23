/* Copyright 2013-2021 MultiServerMC Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <QFrame>
#include "minecraft/mod/Mod.h"

namespace Ui
{
class MCModInfoFrame;
}

class MCModInfoFrame : public QFrame
{
    Q_OBJECT

public:
    explicit MCModInfoFrame(QWidget *parent = 0);
    ~MCModInfoFrame();

    void setModText(QString text);
    void setModDescription(QString text);

    void updateWithMod(Mod &m);
    void clear();

public slots:
    void modDescEllipsisHandler(const QString& link );
    void boxClosed(int result);

private:
    void updateHiddenState();

private:
    Ui::MCModInfoFrame *ui;
    QString desc;
    class QMessageBox * currentBox = nullptr;
};


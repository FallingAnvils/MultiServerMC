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

#include <memory>
#include <QDialog>

#include "java/JavaChecker.h"
#include "pages/BasePage.h"
#include <MultiServerMC.h>

class SettingsObject;

namespace Ui
{
class MinecraftPage;
}

class MinecraftPage : public QWidget, public BasePage
{
    Q_OBJECT

public:
    explicit MinecraftPage(QWidget *parent = 0);
    ~MinecraftPage();

    QString displayName() const override
    {
        return tr("Minecraft");
    }
    QIcon icon() const override
    {
        return MSMC->getThemedIcon("minecraft");
    }
    QString id() const override
    {
        return "minecraft-settings";
    }
    QString helpPage() const override
    {
        return "Minecraft-settings";
    }
    bool apply() override;

private:
    void updateCheckboxStuff();
    void applySettings();
    void loadSettings();

private
slots:
    void on_maximizedCheckBox_clicked(bool checked);

private:
    Ui::MinecraftPage *ui;

};

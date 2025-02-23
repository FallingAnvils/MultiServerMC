#pragma once

#include "tasks/Task.h"
#include "multiservermc_logic_export.h"
#include "net/NetJob.h"
#include <QUrl>
#include "settings/SettingsObject.h"
#include "BaseVersion.h"
#include "InstanceTask.h"

class MULTISERVERMC_LOGIC_EXPORT InstanceCreationTask : public InstanceTask
{
    Q_OBJECT
public:
    explicit InstanceCreationTask(BaseVersionPtr version);

protected:
    //! Entry point for tasks.
    virtual void executeTask() override;

private: /* data */
    BaseVersionPtr m_version;
};

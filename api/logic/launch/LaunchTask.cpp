/* Copyright 2013-2021 MultiServerMC Contributors
 *
 * Authors: Orochimarufan <orochimarufan.x3@gmail.com>
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

#include "launch/LaunchTask.h"
#include "MessageLevel.h"
#include "MSMCStrings.h"
#include "java/JavaChecker.h"
#include "tasks/Task.h"
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QRegularExpression>
#include <QCoreApplication>
#include <QStandardPaths>
#include <assert.h>

void LaunchTask::init()
{
    m_instance->setRunning(true);
}

shared_qobject_ptr<LaunchTask> LaunchTask::create(InstancePtr inst)
{
    shared_qobject_ptr<LaunchTask> proc(new LaunchTask(inst));
    proc->init();
    return proc;
}

LaunchTask::LaunchTask(InstancePtr instance): m_instance(instance)
{
}

void LaunchTask::appendStep(shared_qobject_ptr<LaunchStep> step)
{
    m_steps.append(step);
}

void LaunchTask::prependStep(shared_qobject_ptr<LaunchStep> step)
{
    m_steps.prepend(step);
}


void LaunchTask::writeToStdin(const QByteArray &data)
{
    emit m_steps[currentStep]->stdinWrittenTo(data);
}

void LaunchTask::executeTask()
{
    m_instance->setCrashed(false);
    if(!m_steps.size())
    {
        state = LaunchTask::Finished;
        emitSucceeded();
    }
    state = LaunchTask::Running;
    onStepFinished();
}

void LaunchTask::onReadyForLaunch()
{
    state = LaunchTask::Waiting;
    emit readyForLaunch();
}

void LaunchTask::onStepFinished()
{
    // initial -> just start the first step
    if(currentStep == -1)
    {
        currentStep ++;
        m_steps[currentStep]->start();
        return;
    }

    auto step = m_steps[currentStep];
    if(step->wasSuccessful())
    {
        // end?
        if(currentStep == m_steps.size() - 1)
        {
            finalizeSteps(true, QString());
        }
        else
        {
            currentStep ++;
            step = m_steps[currentStep];
            step->start();
        }
    }
    else
    {
        finalizeSteps(false, step->failReason());
    }
}

void LaunchTask::finalizeSteps(bool successful, const QString& error)
{
    for(auto step = currentStep; step >= 0; step--)
    {
        m_steps[step]->finalize();
    }
    if(successful)
    {
        emitSucceeded();
    }
    else
    {
        emitFailed(error);
    }
}

void LaunchTask::onProgressReportingRequested()
{
    state = LaunchTask::Waiting;
    emit requestProgress(m_steps[currentStep].get());
}

void LaunchTask::setCensorFilter(QMap<QString, QString> filter)
{
    m_censorFilter = filter;
}

QString LaunchTask::censorPrivateInfo(QString in)
{
    auto iter = m_censorFilter.begin();
    while (iter != m_censorFilter.end())
    {
        in.replace(iter.key(), iter.value());
        iter++;
    }
    return in;
}

void LaunchTask::proceed()
{
    if(state != LaunchTask::Waiting)
    {
        return;
    }
    m_steps[currentStep]->proceed();
}

bool LaunchTask::canAbort() const
{
    switch(state)
    {
        case LaunchTask::Aborted:
        case LaunchTask::Failed:
        case LaunchTask::Finished:
            return false;
        case LaunchTask::NotStarted:
            return true;
        case LaunchTask::Running:
        case LaunchTask::Waiting:
        {
            auto step = m_steps[currentStep];
            return step->canAbort();
        }
    }
    return false;
}

bool LaunchTask::abort()
{
    switch(state)
    {
        case LaunchTask::Aborted:
        case LaunchTask::Failed:
        case LaunchTask::Finished:
            return true;
        case LaunchTask::NotStarted:
        {
            state = LaunchTask::Aborted;
            emitFailed("Aborted");
            return true;
        }
        case LaunchTask::Running:
        case LaunchTask::Waiting:
        {
            auto step = m_steps[currentStep];
            if(!step->canAbort())
            {
                return false;
            }
            if(step->abort())
            {
                state = LaunchTask::Aborted;
                return true;
            }
        }
        default:
            break;
    }
    return false;
}

shared_qobject_ptr<LogModel> LaunchTask::getLogModel()
{
    if(!m_logModel)
    {
        m_logModel.reset(new LogModel());
        m_logModel->setMaxLines(m_instance->getConsoleMaxLines());
        m_logModel->setStopOnOverflow(m_instance->shouldStopOnConsoleOverflow());
        // FIXME: should this really be here?
        m_logModel->setOverflowMessage(tr("MultiServerMC stopped watching the game log because the log length surpassed %1 lines.\n"
            "You may have to fix your mods because the game is still logging to files and"
            " likely wasting harddrive space at an alarming rate!").arg(m_logModel->getMaxLines()));
    }
    return m_logModel;
}

void LaunchTask::onLogLines(const QStringList &lines, MessageLevel::Enum defaultLevel)
{
    for (auto & line: lines)
    {
        onLogLine(line, defaultLevel);
    }
}

void LaunchTask::onLogLine(QString line, MessageLevel::Enum level)
{
    // if the launcher part set a log level, use it
    auto innerLevel = MessageLevel::fromLine(line);
    if(innerLevel != MessageLevel::Unknown)
    {
        level = innerLevel;
    }

    // If the level is still undetermined, guess level
    if (level == MessageLevel::StdErr || level == MessageLevel::StdOut || level == MessageLevel::Unknown)
    {
        level = m_instance->guessLevel(line, level);
    }

    // censor private user info
    line = censorPrivateInfo(line);

    auto &model = *getLogModel();
    model.append(level, line);
}

void LaunchTask::emitSucceeded()
{
    m_instance->setRunning(false);
    Task::emitSucceeded();
}

void LaunchTask::emitFailed(QString reason)
{
    m_instance->setRunning(false);
    m_instance->setCrashed(true);
    Task::emitFailed(reason);
}

QString LaunchTask::substituteVariables(const QString &cmd) const
{
    QString out = cmd;
    auto variables = m_instance->getVariables();
    for (auto it = variables.begin(); it != variables.end(); ++it)
    {
        out.replace("$" + it.key(), it.value());
    }
    auto env = QProcessEnvironment::systemEnvironment();
    for (auto var : env.keys())
    {
        out.replace("$" + var, env.value(var));
    }
    return out;
}


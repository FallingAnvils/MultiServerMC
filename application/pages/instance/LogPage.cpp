#include "LogPage.h"
#include "ui_LogPage.h"

#include "MultiServerMC.h"

#include <QIcon>
#include <QScrollBar>
#include <QShortcut>

#include "launch/LaunchTask.h"
#include <settings/Setting.h>
#include "GuiUtil.h"
#include <ColorCache.h>

#include <minecraft/launch/LauncherPartLaunch.h>

class LogFormatProxyModel : public QIdentityProxyModel
{
public:
    LogFormatProxyModel(QObject* parent = nullptr) : QIdentityProxyModel(parent)
    {
    }
    QVariant data(const QModelIndex &index, int role) const override
    {
        switch(role)
        {
            case Qt::FontRole:
                return m_font;
            case Qt::TextColorRole:
            {
                MessageLevel::Enum level = (MessageLevel::Enum) QIdentityProxyModel::data(index, LogModel::LevelRole).toInt();
                return m_colors->getFront(level);
            }
            case Qt::BackgroundRole:
            {
                MessageLevel::Enum level = (MessageLevel::Enum) QIdentityProxyModel::data(index, LogModel::LevelRole).toInt();
                return m_colors->getBack(level);
            }
            default:
                return QIdentityProxyModel::data(index, role);
            }
    }

    void setFont(QFont font)
    {
        m_font = font;
    }

    void setColors(LogColorCache* colors)
    {
        m_colors.reset(colors);
    }

    QModelIndex find(const QModelIndex &start, const QString &value, bool reverse) const
    {
        QModelIndex parentIndex = parent(start);
        auto compare = [&](int r) -> QModelIndex
        {
            QModelIndex idx = index(r, start.column(), parentIndex);
            if (!idx.isValid() || idx == start)
            {
                return QModelIndex();
            }
            QVariant v = data(idx, Qt::DisplayRole);
            QString t = v.toString();
            if (t.contains(value, Qt::CaseInsensitive))
                return idx;
            return QModelIndex();
        };
        if(reverse)
        {
            int from = start.row();
            int to = 0;

            for (int i = 0; i < 2; ++i)
            {
                for (int r = from; (r >= to); --r)
                {
                    auto idx = compare(r);
                    if(idx.isValid())
                        return idx;
                }
                // prepare for the next iteration
                from = rowCount() - 1;
                to = start.row();
            }
        }
        else
        {
            int from = start.row();
            int to = rowCount(parentIndex);

            for (int i = 0; i < 2; ++i)
            {
                for (int r = from; (r < to); ++r)
                {
                    auto idx = compare(r);
                    if(idx.isValid())
                        return idx;
                }
                // prepare for the next iteration
                from = 0;
                to = start.row();
            }
        }
        return QModelIndex();
    }
private:
    QFont m_font;
    std::unique_ptr<LogColorCache> m_colors;
};

LogPage::LogPage(InstancePtr instance, QWidget *parent)
    : QWidget(parent), ui(new Ui::LogPage), m_instance(instance)
{
    ui->setupUi(this);
    ui->tabWidget->tabBar()->hide();

    m_proxy = new LogFormatProxyModel(this);
    // set up text colors in the log proxy and adapt them to the current theme foreground and background
    {
        auto origForeground = ui->text->palette().color(ui->text->foregroundRole());
        auto origBackground = ui->text->palette().color(ui->text->backgroundRole());
        m_proxy->setColors(new LogColorCache(origForeground, origBackground));
    }

    // set up fonts in the log proxy
    {
        QString fontFamily = MSMC->settings()->get("ConsoleFont").toString();
        bool conversionOk = false;
        int fontSize = MSMC->settings()->get("ConsoleFontSize").toInt(&conversionOk);
        if(!conversionOk)
        {
            fontSize = 11;
        }
        m_proxy->setFont(QFont(fontFamily, fontSize));
    }

    ui->text->setModel(m_proxy);

    // set up instance and launch process recognition
    {
        auto launchTask = m_instance->getLaunchTask();
        if(launchTask)
        {
            setInstanceLaunchTaskChanged(launchTask, true);
        }
        connect(m_instance.get(), &BaseInstance::launchTaskChanged, this, &LogPage::onInstanceLaunchTaskChanged);
    }

    auto findShortcut = new QShortcut(QKeySequence(QKeySequence::Find), this);
    connect(findShortcut, SIGNAL(activated()), SLOT(runCommandActivated()));
    auto findNextShortcut = new QShortcut(QKeySequence(QKeySequence::FindNext), this);
    connect(findNextShortcut, SIGNAL(activated()), SLOT(findNextActivated()));
    connect(ui->commandBar, SIGNAL(returnPressed()), SLOT(on_runCommandButton_clicked()));
    auto findPreviousShortcut = new QShortcut(QKeySequence(QKeySequence::FindPrevious), this);
    connect(findPreviousShortcut, SIGNAL(activated()), SLOT(findPreviousActivated()));
}

LogPage::~LogPage()
{
    delete ui;
}

void LogPage::modelStateToUI()
{
    if(m_model->wrapLines())
    {
        ui->text->setWordWrap(true);
        ui->wrapCheckbox->setCheckState(Qt::Checked);
    }
    else
    {
        ui->text->setWordWrap(false);
        ui->wrapCheckbox->setCheckState(Qt::Unchecked);
    }
    if(m_model->suspended())
    {
        ui->trackLogCheckbox->setCheckState(Qt::Unchecked);
    }
    else
    {
        ui->trackLogCheckbox->setCheckState(Qt::Checked);
    }
}

void LogPage::UIToModelState()
{
    if(!m_model)
    {
        return;
    }
    m_model->setLineWrap(ui->wrapCheckbox->checkState() == Qt::Checked);
    m_model->suspend(ui->trackLogCheckbox->checkState() != Qt::Checked);
}

void LogPage::setInstanceLaunchTaskChanged(shared_qobject_ptr<LaunchTask> proc, bool initial)
{
    m_process = proc;
    if(m_process)
    {
        m_model = proc->getLogModel();
        m_proxy->setSourceModel(m_model.get());
        if(initial)
        {
            modelStateToUI();
        }
        else
        {
            UIToModelState();
        }
    }
    else
    {
        m_proxy->setSourceModel(nullptr);
        m_model.reset();
    }
}

void LogPage::onInstanceLaunchTaskChanged(shared_qobject_ptr<LaunchTask> proc)
{
    setInstanceLaunchTaskChanged(proc, false);
}

bool LogPage::apply()
{
    return true;
}

bool LogPage::shouldDisplay() const
{
    return m_instance->isRunning() || m_proxy->rowCount() > 0;
}

void LogPage::on_btnPaste_clicked()
{
    if(!m_model)
        return;

    //FIXME: turn this into a proper task and move the upload logic out of GuiUtil!
    m_model->append(MessageLevel::MultiServerMC, QString("MultiServerMC: Log upload triggered at: %1").arg(QDateTime::currentDateTime().toString(Qt::RFC2822Date)));
    auto url = GuiUtil::uploadPaste(m_model->toPlainText(), this);
    if(!url.isEmpty())
    {
        m_model->append(MessageLevel::MultiServerMC, QString("MultiServerMC: Log uploaded to: %1").arg(url));
    }
    else
    {
        m_model->append(MessageLevel::Error, "MultiServerMC: Log upload failed!");
    }
}

void LogPage::on_btnCopy_clicked()
{
    if(!m_model)
        return;
    m_model->append(MessageLevel::MultiServerMC, QString("Clipboard copy at: %1").arg(QDateTime::currentDateTime().toString(Qt::RFC2822Date)));
    GuiUtil::setClipboardText(m_model->toPlainText());
}

void LogPage::on_btnClear_clicked()
{
    if(!m_model)
        return;
    m_model->clear();
    m_container->refreshContainer();
}

void LogPage::on_btnBottom_clicked()
{
    ui->text->scrollToBottom();
}

void LogPage::on_trackLogCheckbox_clicked(bool checked)
{
    if(!m_model)
        return;
    m_model->suspend(!checked);
}

void LogPage::on_wrapCheckbox_clicked(bool checked)
{
    ui->text->setWordWrap(checked);
    if(!m_model)
        return;
    m_model->setLineWrap(checked);
}

void LogPage::on_runCommandButton_clicked()
{
    m_process->writeToStdin(ui->commandBar->text().append("\n").toUtf8());
}

void LogPage::runCommandActivated()
{
    // focus the search bar if it doesn't have focus
    if (!ui->commandBar->hasFocus())
    {
        ui->commandBar->setFocus();
        ui->commandBar->selectAll();
    }
}

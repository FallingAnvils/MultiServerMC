// Licensed under the Apache-2.0 license. See README.md for details.

#pragma once

#include <QString>
#include <QDebug>
#include <exception>

#include "multiservermc_logic_export.h"

class MULTISERVERMC_LOGIC_EXPORT Exception : public std::exception
{
public:
    Exception(const QString &message) : std::exception(), m_message(message)
    {
        qCritical() << "Exception:" << message;
    }
    Exception(const Exception &other)
        : std::exception(), m_message(other.cause())
    {
    }
    virtual ~Exception() noexcept {}
    const char *what() const noexcept
    {
        return m_message.toLatin1().constData();
    }
    QString cause() const
    {
        return m_message;
    }

private:
    QString m_message;
};

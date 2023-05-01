#include "TimeLabel.h"
#include <sstream>
#include <iomanip>

using namespace std;
using namespace literals;

const QString TimeLabel::undefinedTimeLabel(tr("--:--"));

TimeLabel::TimeLabel(const SharedPtr<IValueCollection>& config, const QString& label)
    : QLabel(label)
    , m_config{ config }
    , m_currentSeconds{ 0 }
    , m_totalSeconds{ 0 }
{
}

QString TimeLabel::FormatTimeInfo(int t, const string prefix /*= {}*/)
{
    stringstream ss;

    if (!prefix.empty())
    {
        ss << prefix;
    }
    if (t >= 3600)
    {
        const int h = t / 3600;
        t -= (h * 3600);

        if (h < 100)
        {
            ss << setw(2) << setfill('0') << h;
        }
        else
        {
            ss << h;
        }
        ss << ":"s;
    }
    ss << setw(2) << setfill('0') << (t / 60);
    ss << ":"s;
    ss << setw(2) << setfill('0') << (t % 60);

    return QString(ss.str().c_str());
}

void TimeLabel::SetValue(int currentSeconds, int totalSeconds)
{
    // synchronization is not necessary
    // because the members are being accessed
    // by Qt's main thread, only
    m_currentSeconds = currentSeconds;
    m_totalSeconds = totalSeconds;

    SetLabel(currentSeconds, totalSeconds,
        VariantValue::Key("RemainTimeMode").TryGet<bool>(m_config).value_or(true));
}

void TimeLabel::SetLabel(int currentSeconds, int totalSeconds, bool remainingMode)
{
    if (totalSeconds > 0 && currentSeconds >= 0 && currentSeconds <= totalSeconds)
    {
        if (remainingMode)
        {
            setText(FormatTimeInfo(totalSeconds - currentSeconds, "-"s));
        }
        else
        {
            setText(FormatTimeInfo(totalSeconds));

        }
    }
    else
    {
        setText(undefinedTimeLabel);
    }
}

void TimeLabel::mousePressEvent(QMouseEvent* ev)
{
    if (m_totalSeconds > 0 && m_currentSeconds >= 0 && m_currentSeconds <= m_totalSeconds)
    {
        const bool newRemainingMode = !VariantValue::Key("RemainTimeMode").TryGet<bool>(m_config).value_or(true);
        VariantValue::Key("RemainTimeMode").Set(m_config, newRemainingMode);

        SetLabel(m_currentSeconds, m_totalSeconds, newRemainingMode);
    }
    QLabel::mousePressEvent(ev);
}

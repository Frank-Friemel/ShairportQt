#include <time.h>
#include <string>
#include <LayerCake.h>

#include <QLabel>
#include <QString>

class TimeLabel
    : public QLabel
{
     Q_OBJECT
     
public:
    static const QString undefinedTimeLabel;

    TimeLabel(const SharedPtr<IValueCollection>& config, const QString& label);

    static QString FormatTimeInfo(int t, const std::string prefix = {});
    void SetValue(int currentSeconds, int totalSeconds);

private:
    void SetLabel(int currentSeconds, int totalSeconds, bool remainingMode);

protected:
    void mousePressEvent(QMouseEvent* ev) override;

private:
    const SharedPtr<IValueCollection>  	m_config;

    // synchronization is not necessary
    // because the members are being accessed
    // by Qt's main thread, only
    int m_currentSeconds;
    int m_totalSeconds;
};

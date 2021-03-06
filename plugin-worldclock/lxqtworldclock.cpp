/* BEGIN_COMMON_COPYRIGHT_HEADER
 * (c)LGPL2+
 *
 * LXDE-Qt - a lightweight, Qt based, desktop toolset
 * http://razor-qt.org
 *
 * Copyright: 2012-2013 Razor team
 *            2014 LXQt team
 * Authors:
 *   Kuzma Shapran <kuzma.shapran@gmail.com>
 *
 * This program or library is free software; you can redistribute it
 * and/or modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA
 *
 * END_COMMON_COPYRIGHT_HEADER */

#include "lxqtworldclock.h"

#include <QLocale>
#include <QTimer>
#include <QScopedArrayPointer>
#include <QDate>
#include <QWheelEvent>
#include <QCalendarWidget>
#include <QDesktopWidget>
#include <QDialog>
#include <QHBoxLayout>

#include <time.h>


LxQtWorldClock::LxQtWorldClock(const ILxQtPanelPluginStartupInfo &startupInfo):
    QObject(),
    ILxQtPanelPlugin(startupInfo),
    mPopup(NULL),
    mTimer(new QTimer(this)),
    mFormatType(FORMAT__INVALID),
    mAutoRotate(true),
    mFormat(Qt::TextDate),
    mPopupContent(NULL)
{
    mMainWidget = new QWidget();
    mContent = new ActiveLabel();
    mRotatedWidget = new LxQt::RotatedWidget(*mContent, mMainWidget);

    mRotatedWidget->setTransferWheelEvent(true);

    QVBoxLayout *borderLayout = new QVBoxLayout(mMainWidget);
    borderLayout->setContentsMargins(0, 0, 0, 0);
    borderLayout->setSpacing(0);
    borderLayout->addWidget(mRotatedWidget, 0, Qt::AlignCenter);

    mContent->setObjectName("WorldClockContent");

    mContent->setAlignment(Qt::AlignCenter);

    settingsChanged();

    connect(mTimer, SIGNAL(timeout()), SLOT(timeout()));

    connect(mContent, SIGNAL(wheelScrolled(int)), SLOT(wheelScrolled(int)));
}

LxQtWorldClock::~LxQtWorldClock()
{
    delete mMainWidget;
}

void LxQtWorldClock::timeout()
{
        QDateTime now = QDateTime::currentDateTime();

        QTimeZone timeZone(mActiveTimeZone.toLatin1());
        QDateTime tzNow = now.toTimeZone(timeZone);
        QString str;
        if (mFormat == -1) // custom
            str = tzNow.toString(preformat(mCustomFormat, timeZone, tzNow));
        else
            str = tzNow.toString(mFormat);
        if (str != mLastShownText)
        {
            mContent->setText(str);
            mLastShownText = str;

            mRotatedWidget->adjustContentSize();
            mRotatedWidget->update();

            updatePopupContent();
        }
}

void LxQtWorldClock::updateFormat()
{
    int timerInterval = 0;

    switch (mFormatType)
    {
    case FORMAT_CUSTOM:
    {
        mFormat = static_cast<Qt::DateFormat>(-1); // custom

        QString format = mCustomFormat;
        format.replace(QRegExp(QLatin1String("'[^']*'")), QString());
        if (format.contains(QString("SSS")))
            timerInterval = 1;
        else if (format.contains(QString("SS")))
            timerInterval = 10;
        else if (format.contains(QString("S")))
            timerInterval = 100;
        else if (format.contains(QString("s")))
            timerInterval = 1000;
        else
            timerInterval = 60000;
    }
        break;

    case FORMAT_FULL:
        mFormat = Qt::RFC2822Date;
        timerInterval = 1000;
        break;

    case FORMAT_LONG:
        mFormat = Qt::DefaultLocaleLongDate;
        timerInterval = 1000;
        break;

    case FORMAT_MEDIUM:
        mFormat = Qt::ISODate;
        timerInterval = 1000;
        break;

    case FORMAT_SHORT:
        mFormat = Qt::DefaultLocaleShortDate;
        timerInterval = 60000;
        break;

    default:;
    }

    restartTimer(timerInterval);
}

void LxQtWorldClock::restartTimer(int timerInterval)
{
    mTimer->stop();
    mTimer->setInterval(timerInterval);

    if (timerInterval < 1000)
        mTimer->start();
    else
    {
        int delay = static_cast<int>((timerInterval + 100 - (static_cast<long long>(QTime::currentTime().msecsSinceStartOfDay()) % timerInterval)) % timerInterval);
        QTimer::singleShot(delay, this, SLOT(timeout()));
        QTimer::singleShot(delay, mTimer, SLOT(start()));
    }
}

void LxQtWorldClock::settingsChanged()
{
    QSettings *_settings = settings();

    FormatType oldFormatType = mFormatType;
    QString oldCustomFormat = mCustomFormat;

    mTimeZones.clear();

    int size = _settings->beginReadArray("timeZones");
    for (int i = 0; i < size; ++i)
    {
        _settings->setArrayIndex(i);
        mTimeZones.append(_settings->value("timeZone", QString()).toString());
    }
    _settings->endArray();
    if (mTimeZones.isEmpty())
        mTimeZones.append(QString::fromLatin1(QTimeZone::systemTimeZoneId()));

    mDefaultTimeZone = _settings->value("defaultTimeZone", QString()).toString();
    if (mDefaultTimeZone.isEmpty())
        mDefaultTimeZone = mTimeZones[0];
    mActiveTimeZone = mDefaultTimeZone;

    mCustomFormat = _settings->value("customFormat", QString("'<b>'HH:mm:ss'</b><br/><font size=\"-2\">'eee, d MMM yyyy'<br/>'VVVV'</font>'")).toString();

    QString formatType = _settings->value("formatType", QString()).toString();
    if (formatType == "custom")
        mFormatType = FORMAT_CUSTOM;
    else if (formatType == "full")
        mFormatType = FORMAT_FULL;
    else if (formatType == "long")
        mFormatType = FORMAT_LONG;
    else if (formatType == "medium")
        mFormatType = FORMAT_MEDIUM;
    else
        mFormatType = FORMAT_SHORT;

    if ((oldFormatType != mFormatType) || (oldCustomFormat != mCustomFormat))
        updateFormat();

    bool autoRotate = settings()->value("autoRotate", true).toBool();
    if (autoRotate != mAutoRotate)
    {
        mAutoRotate = autoRotate;
        realign();
    }

    if (mPopup)
    {
        updatePopupContent();
        mPopup->adjustSize();
        mPopup->setGeometry(calculatePopupWindowPos(mPopup->size()));
    }

    timeout();
}

QDialog *LxQtWorldClock::configureDialog()
{
    return new LxQtWorldClockConfiguration(settings());
}

void LxQtWorldClock::wheelScrolled(int delta)
{
    if (mTimeZones.count() > 1)
    {
        mActiveTimeZone = mTimeZones[(mTimeZones.indexOf(mActiveTimeZone) + ((delta > 0) ? -1 : 1) + mTimeZones.size()) % mTimeZones.size()];
        timeout();
    }
}

void LxQtWorldClock::activated(ActivationReason reason)
{
    switch (reason)
    {
    case ILxQtPanelPlugin::Trigger:
    case ILxQtPanelPlugin::MiddleClick:
        break;

    default:
        return;
    }

    if (!mPopup)
    {
        mPopup = new QDialog(mContent);
        mPopup->setWindowFlags(Qt::FramelessWindowHint | Qt::Dialog | Qt::X11BypassWindowManagerHint);
        mPopup->setLayout(new QHBoxLayout(mPopup));

        if (reason == ILxQtPanelPlugin::Trigger)
        {
            mPopup->setObjectName(QString());

            mPopup->layout()->setContentsMargins(0, 0, 0, 0);
            QCalendarWidget *calendarWidget = new QCalendarWidget(mPopup);
            mPopup->layout()->addWidget(calendarWidget);

            QTimeZone timeZone(mActiveTimeZone.toLatin1());
            calendarWidget->setFirstDayOfWeek(QLocale(QLocale::AnyLanguage, timeZone.country()).firstDayOfWeek());
            calendarWidget->setSelectedDate(QDateTime::currentDateTime().toTimeZone(timeZone).date());
        }
        else
        {
            mPopup->setObjectName("WorldClockPopup");

            mPopupContent = new QLabel(mPopup);
            mPopup->layout()->addWidget(mPopupContent);
            mPopupContent->setAlignment(mContent->alignment());

            updatePopupContent();
        }

        mPopup->adjustSize();
        mPopup->setGeometry(calculatePopupWindowPos(mPopup->size()));

        mPopup->show();
    }
    else
    {
        mPopupContent = NULL;
        mPopup->deleteLater();
        mPopup = NULL;
    }
}

void LxQtWorldClock::updatePopupContent()
{
    if (mPopupContent)
    {
        QDateTime now = QDateTime::currentDateTime();
        QStringList allTimeZones;

        foreach (QString qTimeZoneName, mTimeZones)
        {
            QTimeZone timeZone(qTimeZoneName.toLatin1());
            QDateTime tzNow = now.toTimeZone(timeZone);
            QString str;
            if (mFormat == -1) // custom
                str = tzNow.toString(preformat(mCustomFormat, timeZone, tzNow));
            else
                str = tzNow.toString(mFormat);

            allTimeZones.append(str);
        }

        mPopupContent->setText(allTimeZones.join("<hr/>"));

    }
}

QString LxQtWorldClock::preformat(const QString &format, const QTimeZone &timeZone, const QDateTime &dateTime)
{
    QString result = format;
    int from = 0;
    for (;;)
    {
        int apos = result.indexOf(QLatin1Char('\''), from);
        int tz = result.indexOf(QLatin1Char('T'), from);
        if ((apos != -1) && (tz != -1))
        {
            if (apos > tz)
                apos = -1;
            else
                tz = -1;
        }
        if (apos != -1)
        {
            from = apos + 1;
            apos = result.indexOf(QLatin1Char('\''), from);
            if (apos == -1) // misformat
                break;
            from = apos + 1;
        }
        else if (tz != -1)
        {
            int length = 1;
            for (; result[tz + length] == QLatin1Char('T'); ++length);
            if (length > 5)
                length = 5;
            QString replacement;
            switch (length)
            {
            case 1:
                replacement = timeZone.displayName(dateTime, QTimeZone::OffsetName);
                if (replacement.startsWith(QLatin1String("UTC")))
                    replacement = replacement.mid(3);
                break;

            case 2:
                replacement = QString::fromLatin1(timeZone.id());
                break;

            case 3:
                replacement = timeZone.abbreviation(dateTime);
                break;

            case 4:
                replacement = timeZone.displayName(dateTime, QTimeZone::ShortName);
                break;

            case 5:
                replacement = timeZone.displayName(dateTime, QTimeZone::LongName);
                break;
            }

            if ((tz > 0) && (result[tz - 1] == QLatin1Char('\'')))
            {
                --tz;
                ++length;
            }
            else
                replacement.prepend(QLatin1Char('\''));

            if (result[tz + length] == QLatin1Char('\''))
                ++length;
            else
                replacement.append(QLatin1Char('\''));

            result.replace(tz, length, replacement);
            from = tz + replacement.length();
        }
        else
            break;
    }
    return result;
}

void LxQtWorldClock::realign()
{
    if (mAutoRotate)
        switch (panel()->position())
        {
        case ILxQtPanel::PositionTop:
        case ILxQtPanel::PositionBottom:
            mRotatedWidget->setOrigin(Qt::TopLeftCorner);
            break;

        case ILxQtPanel::PositionLeft:
            mRotatedWidget->setOrigin(Qt::BottomLeftCorner);
            break;

        case ILxQtPanel::PositionRight:
            mRotatedWidget->setOrigin(Qt::TopRightCorner);
            break;
        }
    else
        mRotatedWidget->setOrigin(Qt::TopLeftCorner);
}

ActiveLabel::ActiveLabel(QWidget * parent) :
    QLabel(parent)
{
}

void ActiveLabel::wheelEvent(QWheelEvent *event)
{
    emit wheelScrolled(event->delta());

    QLabel::wheelEvent(event);
}

void ActiveLabel::mouseReleaseEvent(QMouseEvent* event)
{
    switch (event->button())
    {
    case Qt::LeftButton:
        emit leftMouseButtonClicked();
        break;

    case Qt::MidButton:
        emit middleMouseButtonClicked();
        break;

    default:;
    }

    QLabel::mouseReleaseEvent(event);
}

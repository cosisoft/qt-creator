/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
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

#include "qmlprofilermodelmanager.h"
#include "qmlprofilerconstants.h"
#include "qmlprofilerdatamodel.h"
#include "qmlprofilertracefile.h"
#include "qmlprofilernotesmodel.h"

#include <coreplugin/progressmanager/progressmanager.h>
#include <utils/runextensions.h>
#include <utils/qtcassert.h>

#include <QDebug>
#include <QFile>
#include <QMessageBox>

namespace QmlProfiler {
namespace Internal {

static const char *ProfileFeatureNames[] = {
    QT_TRANSLATE_NOOP("MainView", "JavaScript"),
    QT_TRANSLATE_NOOP("MainView", "Memory Usage"),
    QT_TRANSLATE_NOOP("MainView", "Pixmap Cache"),
    QT_TRANSLATE_NOOP("MainView", "Scene Graph"),
    QT_TRANSLATE_NOOP("MainView", "Animations"),
    QT_TRANSLATE_NOOP("MainView", "Painting"),
    QT_TRANSLATE_NOOP("MainView", "Compiling"),
    QT_TRANSLATE_NOOP("MainView", "Creating"),
    QT_TRANSLATE_NOOP("MainView", "Binding"),
    QT_TRANSLATE_NOOP("MainView", "Handling Signal"),
    QT_TRANSLATE_NOOP("MainView", "Input Events"),
    QT_TRANSLATE_NOOP("MainView", "Debug Messages")
};

Q_STATIC_ASSERT(sizeof(ProfileFeatureNames) == sizeof(char *) * MaximumProfileFeature);

/////////////////////////////////////////////////////////////////////
QmlProfilerTraceTime::QmlProfilerTraceTime(QObject *parent) :
    QObject(parent), m_startTime(-1), m_endTime(-1)
{
}

QmlProfilerTraceTime::~QmlProfilerTraceTime()
{
}

qint64 QmlProfilerTraceTime::startTime() const
{
    return m_startTime;
}

qint64 QmlProfilerTraceTime::endTime() const
{
    return m_endTime;
}

qint64 QmlProfilerTraceTime::duration() const
{
    return endTime() - startTime();
}

void QmlProfilerTraceTime::clear()
{
    setTime(-1, -1);
}

void QmlProfilerTraceTime::setTime(qint64 startTime, qint64 endTime)
{
    Q_ASSERT(startTime <= endTime);
    if (startTime != m_startTime || endTime != m_endTime) {
        m_startTime = startTime;
        m_endTime = endTime;
    }
}

void QmlProfilerTraceTime::decreaseStartTime(qint64 time)
{
    if (m_startTime > time || m_startTime == -1) {
        m_startTime = time;
        if (m_endTime == -1)
            m_endTime = m_startTime;
        else
            QTC_ASSERT(m_endTime >= m_startTime, m_endTime = m_startTime);
    }
}

void QmlProfilerTraceTime::increaseEndTime(qint64 time)
{
    if (m_endTime < time || m_endTime == -1) {
        m_endTime = time;
        if (m_startTime == -1)
            m_startTime = m_endTime;
        else
            QTC_ASSERT(m_endTime >= m_startTime, m_startTime = m_endTime);
    }
}


} // namespace Internal

/////////////////////////////////////////////////////////////////////

class QmlProfilerModelManager::QmlProfilerModelManagerPrivate
{
public:
    QmlProfilerDataModel *model;
    QmlProfilerNotesModel *notesModel;

    QmlProfilerModelManager::State state;
    QmlProfilerTraceTime *traceTime;

    int numRegisteredModels;
    quint64 availableFeatures;
    quint64 visibleFeatures;
    quint64 recordedFeatures;

    QHash<ProfileFeature, QVector<EventLoader> > eventLoaders;
    QVector<Finalizer> finalizers;
};


QmlProfilerModelManager::QmlProfilerModelManager(Utils::FileInProjectFinder *finder, QObject *parent) :
    QObject(parent), d(new QmlProfilerModelManagerPrivate)
{
    d->numRegisteredModels = 0;
    d->availableFeatures = 0;
    d->visibleFeatures = 0;
    d->recordedFeatures = 0;
    d->model = new QmlProfilerDataModel(finder, this);
    d->state = Empty;
    d->traceTime = new QmlProfilerTraceTime(this);
    d->notesModel = new QmlProfilerNotesModel(this);
}

QmlProfilerModelManager::~QmlProfilerModelManager()
{
    delete d;
}

QmlProfilerTraceTime *QmlProfilerModelManager::traceTime() const
{
    return d->traceTime;
}

QmlProfilerDataModel *QmlProfilerModelManager::qmlModel() const
{
    return d->model;
}

QmlProfilerNotesModel *QmlProfilerModelManager::notesModel() const
{
    return d->notesModel;
}

bool QmlProfilerModelManager::isEmpty() const
{
    return d->model->isEmpty();
}

int QmlProfilerModelManager::registerModelProxy()
{
    return d->numRegisteredModels++;
}

void QmlProfilerModelManager::dispatch(const QmlEvent &event, const QmlEventType &type)
{
    foreach (const EventLoader &loader, d->eventLoaders[type.feature()])
        loader(event, type);
}

void QmlProfilerModelManager::announceFeatures(quint64 features, EventLoader eventLoader,
                                               Finalizer finalizer)
{
    if ((features & d->availableFeatures) != features) {
        d->availableFeatures |= features;
        emit availableFeaturesChanged(d->availableFeatures);
    }
    if ((features & d->visibleFeatures) != features) {
        d->visibleFeatures |= features;
        emit visibleFeaturesChanged(d->visibleFeatures);
    }

    for (int feature = 0; feature != MaximumProfileFeature; ++feature) {
        if (features & (1 << feature))
            d->eventLoaders[static_cast<ProfileFeature>(feature)].append(eventLoader);
    }

    d->finalizers.append(finalizer);
}

quint64 QmlProfilerModelManager::availableFeatures() const
{
    return d->availableFeatures;
}

quint64 QmlProfilerModelManager::visibleFeatures() const
{
    return d->visibleFeatures;
}

void QmlProfilerModelManager::setVisibleFeatures(quint64 features)
{
    if (d->visibleFeatures != features) {
        d->visibleFeatures = features;
        emit visibleFeaturesChanged(d->visibleFeatures);
    }
}

quint64 QmlProfilerModelManager::recordedFeatures() const
{
    return d->recordedFeatures;
}

void QmlProfilerModelManager::setRecordedFeatures(quint64 features)
{
    if (d->recordedFeatures != features) {
        d->recordedFeatures = features;
        emit recordedFeaturesChanged(d->recordedFeatures);
    }
}

const char *QmlProfilerModelManager::featureName(ProfileFeature feature)
{
    return ProfileFeatureNames[feature];
}

void QmlProfilerModelManager::addQmlEvent(Message message, RangeType rangeType, int detailType,
                                          qint64 startTime, qint64 length, const QString &data,
                                          const QmlEventLocation &location, qint64 ndata1,
                                          qint64 ndata2, qint64 ndata3, qint64 ndata4,
                                          qint64 ndata5)
{
    // If trace start time was not explicitly set, use the first event
    if (d->traceTime->startTime() == -1)
        d->traceTime->setTime(startTime, startTime + d->traceTime->duration());

    QTC_ASSERT(state() == AcquiringData, /**/);
    d->model->addEvent(message, rangeType, detailType, startTime, length, data, location, ndata1,
                       ndata2, ndata3, ndata4, ndata5);
}

void QmlProfilerModelManager::addDebugMessage(qint64 timestamp, QtMsgType messageType,
                                              const QString &text, const QmlEventLocation &location)
{
    if (state() == AcquiringData)
        d->model->addEvent(DebugMessage, MaximumRangeType, messageType, timestamp, 0, text,
                           location, 0, 0, 0, 0, 0);
}

void QmlProfilerModelManager::acquiringDone()
{
    QTC_ASSERT(state() == AcquiringData, /**/);
    setState(ProcessingData);
    d->model->processData();
}

void QmlProfilerModelManager::processingDone()
{
    QTC_ASSERT(state() == ProcessingData, /**/);
    // Load notes after the timeline models have been initialized ...
    // which happens on stateChanged(Done).
    setState(Done);
    d->notesModel->loadData();
    emit loadFinished();
}

void QmlProfilerModelManager::save(const QString &filename)
{
    QFile *file = new QFile(filename);
    if (!file->open(QIODevice::WriteOnly)) {
        emit error(tr("Could not open %1 for writing.").arg(filename));
        delete file;
        emit saveFinished();
        return;
    }

    d->notesModel->saveData();

    QmlProfilerFileWriter *writer = new QmlProfilerFileWriter(this);
    writer->setTraceTime(traceTime()->startTime(), traceTime()->endTime(),
                        traceTime()->duration());
    writer->setData(d->model->eventTypes(), d->model->events());
    writer->setNotes(d->notesModel->notes());

    connect(writer, &QObject::destroyed, this, &QmlProfilerModelManager::saveFinished,
            Qt::QueuedConnection);

    QFuture<void> result = Utils::runAsync([file, writer] (QFutureInterface<void> &future) {
        writer->setFuture(&future);
        writer->save(file);
        delete writer;
        file->deleteLater();
    });

    Core::ProgressManager::addTask(result, tr("Saving Trace Data"), Constants::TASK_SAVE,
                                   Core::ProgressManager::ShowInApplicationIcon);
}

void QmlProfilerModelManager::load(const QString &filename)
{
    QFile *file = new QFile(filename, this);
    if (!file->open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit error(tr("Could not open %1 for reading.").arg(filename));
        delete file;
        emit loadFinished();
        return;
    }

    clear();
    setState(AcquiringData);
    QmlProfilerFileReader *reader = new QmlProfilerFileReader(this);

    connect(reader, &QmlProfilerFileReader::error, this, [this, reader](const QString &message) {
        delete reader;
        emit error(message);
    }, Qt::QueuedConnection);

    connect(reader, &QmlProfilerFileReader::success, this, [this, reader]() {
        d->model->setData(reader->traceStart(), qMax(reader->traceStart(), reader->traceEnd()),
                          reader->eventTypes(), reader->events());
        d->notesModel->setNotes(reader->notes());
        setRecordedFeatures(reader->loadedFeatures());
        d->traceTime->increaseEndTime(d->model->lastTimeMark());
        delete reader;
        acquiringDone();
    }, Qt::QueuedConnection);

    QFuture<void> result = Utils::runAsync([file, reader] (QFutureInterface<void> &future) {
        reader->setFuture(&future);
        reader->load(file);
        file->close();
        file->deleteLater();
    });

    Core::ProgressManager::addTask(result, tr("Loading Trace Data"), Constants::TASK_LOAD);
}

void QmlProfilerModelManager::setState(QmlProfilerModelManager::State state)
{
    // It's not an error, we are continuously calling "AcquiringData" for example
    if (d->state == state)
        return;

    switch (state) {
        case ClearingData:
            QTC_ASSERT(d->state == Done || d->state == Empty || d->state == AcquiringData, /**/);
        break;
        case Empty:
            // if it's not empty, complain but go on
            QTC_ASSERT(isEmpty(), /**/);
        break;
        case AcquiringData:
            // we're not supposed to receive new data while processing older data
            QTC_ASSERT(d->state != ProcessingData, return);
        break;
        case ProcessingData:
            QTC_ASSERT(d->state == AcquiringData, return);
        break;
        case Done:
            QTC_ASSERT(d->state == ProcessingData || d->state == Empty, return);
        break;
        default:
            emit error(tr("Trying to set unknown state in events list."));
        break;
    }

    d->state = state;
    emit stateChanged();
}

QmlProfilerModelManager::State QmlProfilerModelManager::state() const
{
    return d->state;
}

void QmlProfilerModelManager::clear()
{
    setState(ClearingData);
    d->model->clear();
    d->traceTime->clear();
    d->notesModel->clear();
    setVisibleFeatures(0);
    setRecordedFeatures(0);

    setState(Empty);
}

void QmlProfilerModelManager::startAcquiring()
{
    setState(AcquiringData);
}

} // namespace QmlProfiler

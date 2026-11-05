#pragma once

#include <QElapsedTimer>
#include <QDebug>
#include <QMutex>
#include <QMutexLocker>
#include <QQuickWindow>
#include <QTimer>
#include <QEvent>
#include <QMouseEvent>
#include <QCoreApplication>
#include <QFile>
#include <QTextStream>
#include <functional>
#include <algorithm>
#include <array>
#include <ctime>
#include <memory>
#include <vector>

// Optional raw timeline for diagnosing slow frames. Product code may call it
// at points of interest; it does nothing unless a trace is recording.
inline std::function<void(char)> &benchmarkTraceHook()
{
    static std::function<void(char)> hook;
    return hook;
}

// Buffer swap timestamps rather than printing on the render thread. These are
// Qt swap completions, not compositor presentation feedback.
inline void startHoverBenchmark(QQuickWindow *window, QObject *content, bool automatic = true)
{
    if (!window || !content) {
        qWarning() << "hover-benchmark: dock window unavailable";
        return;
    }
    struct Samples {
        QMutex mutex;
        QElapsedTimer clock;
        std::vector<qint64> times;
        bool recording = false;
        std::array<qint64, 5> stages{};
        std::array<std::vector<double>, 5> durations;
        std::clock_t cpuStart = 0;
        std::vector<qint64> inputTimes;
        QPointF inputMin, inputMax;
        bool pointerInside = false;
        bool previousFrameInside = false;
        std::vector<double> insideIntervals;
        // SPACEDOCK_BENCHMARK_TRACE: (type, ns, value) for every traced point.
        struct Mark { char type; qint64 ns; double value; };
        std::vector<Mark> trace;
        bool tracing = false;
        double lastPointerX = 0;
    };
    auto samples = std::make_shared<Samples>();
    samples->times.reserve(4096);
    samples->inputTimes.reserve(8192);
    samples->insideIntervals.reserve(4096);
    const QString tracePath = qEnvironmentVariable("SPACEDOCK_BENCHMARK_TRACE");
    samples->tracing = !tracePath.isEmpty();
    if (samples->tracing) samples->trace.reserve(1 << 18);
    auto mark = [samples](char type, double value = 0) {
        QMutexLocker lock(&samples->mutex);
        if (samples->recording && samples->tracing)
            samples->trace.push_back({type, samples->clock.nsecsElapsed(), value});
    };
    struct PointerProbe : QObject {
        std::function<void(QPointF)> observe;
        std::function<void(char, double)> mark;
        using QObject::QObject;
        bool eventFilter(QObject *, QEvent *event) override {
            if (event->type() == QEvent::MouseMove) {
                const auto position = static_cast<QMouseEvent *>(event)->position();
                if (mark) mark('M', position.x());
                if (observe) observe(position);
            } else if (event->type() == QEvent::UpdateRequest && mark) {
                mark('Q', 0);
            }
            return false;
        }
    };
    auto *probe = new PointerProbe(window);
    probe->observe = [samples](QPointF position) {
        QMutexLocker lock(&samples->mutex);
        if (samples->recording) {
            if (samples->inputTimes.empty()) samples->inputMin = samples->inputMax = position;
            samples->inputMin.setX(std::min(samples->inputMin.x(), position.x()));
            samples->inputMin.setY(std::min(samples->inputMin.y(), position.y()));
            samples->inputMax.setX(std::max(samples->inputMax.x(), position.x()));
            samples->inputMax.setY(std::max(samples->inputMax.y(), position.y()));
            samples->inputTimes.push_back(samples->clock.nsecsElapsed());
        }
    };
    if (samples->tracing) probe->mark = mark;
    if (!automatic || samples->tracing) window->installEventFilter(probe);
    if (automatic) probe->observe = nullptr;
    for (auto &values : samples->durations) values.reserve(4096);
    auto stage = [samples](int index) {
        QMutexLocker lock(&samples->mutex);
        if (!samples->recording) return;
        const auto now = samples->clock.nsecsElapsed();
        if (index == 0) samples->stages.fill(0);
        if (index > 0 && samples->stages[index - 1] > 0)
            samples->durations[index - 1].push_back((now - samples->stages[index - 1]) / 1e6);
        if (index < 5) samples->stages[index] = now;
    };
    auto connections = std::make_shared<std::vector<QMetaObject::Connection>>();
    // Read QML state on its GUI thread, then share only this scalar with the
    // render thread. Restrict interval statistics to consecutive inside frames.
    connections->push_back(QObject::connect(window, &QQuickWindow::afterAnimating, window, [samples, content] {
        const bool inside = content->property("pointerInside").toBool();
        const double x = content->property("pointerX").toDouble();
        QMutexLocker lock(&samples->mutex);
        samples->pointerInside = inside;
        if (samples->recording && samples->tracing) {
            samples->trace.push_back({'A', samples->clock.nsecsElapsed(), (inside ? 1.0 : 0.0) + (x != samples->lastPointerX ? 2.0 : 0.0)});
            samples->lastPointerX = x;
        }
    }, Qt::DirectConnection));
    connections->push_back(QObject::connect(window, &QQuickWindow::beforeFrameBegin, window, [stage] { stage(0); }, Qt::DirectConnection));
    connections->push_back(QObject::connect(window, &QQuickWindow::beforeSynchronizing, window, [stage, mark] { stage(1); mark('S'); }, Qt::DirectConnection));
    connections->push_back(QObject::connect(window, &QQuickWindow::afterSynchronizing, window, [stage, mark] { stage(2); mark('s'); }, Qt::DirectConnection));
    connections->push_back(QObject::connect(window, &QQuickWindow::beforeRendering, window, [stage] { stage(3); }, Qt::DirectConnection));
    connections->push_back(QObject::connect(window, &QQuickWindow::afterRendering, window, [stage] { stage(4); }, Qt::DirectConnection));
    connections->push_back(QObject::connect(window, &QQuickWindow::frameSwapped, window, [stage] { stage(5); }, Qt::DirectConnection));
    const auto connection = QObject::connect(window, &QQuickWindow::frameSwapped, window, [samples] {
        QMutexLocker lock(&samples->mutex);
        if (samples->recording) {
            const auto now = samples->clock.nsecsElapsed();
            if (!samples->times.empty() && samples->pointerInside && samples->previousFrameInside)
                samples->insideIntervals.push_back((now - samples->times.back()) / 1e6);
            samples->times.push_back(now);
            if (samples->tracing) samples->trace.push_back({'W', now, 0});
            samples->previousFrameInside = samples->pointerInside;
        }
    }, Qt::DirectConnection);
    auto *ready = new QTimer(content);
    ready->setInterval(100);
    QObject::connect(ready, &QTimer::timeout, content, [=] {
        if (content->property("startingUp").toBool()) return;
        ready->stop();
        if (automatic && (!content->property("magnificationFits").toBool()
            || !content->property("magnification").toMap().value("enabled").toBool())) {
            qWarning() << "hover-benchmark: magnification disabled or does not fit; reduce icon size or enable magnification and restart benchmark";
            QObject::disconnect(connection);
            for (auto c : *connections) QObject::disconnect(c);
            probe->deleteLater();
            return;
        }
        if (automatic) content->setProperty("profileHoverRunning", true);
        QTimer::singleShot(2000, content, [=] {
            {
                QMutexLocker lock(&samples->mutex);
                samples->clock.start();
                samples->cpuStart = std::clock();
                samples->recording = true;
            }
            if (samples->tracing) {
                benchmarkTraceHook() = [mark](char type) { mark(type); };
                auto *heartbeat = new QTimer(probe);
                heartbeat->setTimerType(Qt::PreciseTimer);
                QObject::connect(heartbeat, &QTimer::timeout, probe, [mark] { mark('T'); });
                heartbeat->start(4);
            }
            qInfo() << "hover-benchmark: recording 20 seconds after 2-second warmup; automatic sweep" << automatic;
            QTimer::singleShot(20000, content, [=] {
                std::vector<double> intervals;
                double seconds;
                {
                    QMutexLocker lock(&samples->mutex);
                    samples->recording = false;
                    seconds = samples->clock.nsecsElapsed() / 1e9;
                    for (size_t i = 1; i < samples->times.size(); ++i)
                        intervals.push_back((samples->times[i] - samples->times[i - 1]) / 1e6);
                }
                QObject::disconnect(connection);
                for (auto c : *connections) QObject::disconnect(c);
                benchmarkTraceHook() = nullptr;
                probe->deleteLater();
                if (samples->tracing) {
                    QFile file(tracePath);
                    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
                        QTextStream out(&file);
                        QMutexLocker lock(&samples->mutex);
                        for (const auto &m : samples->trace) out << m.type << ' ' << m.ns << ' ' << m.value << '\n';
                        qInfo() << "hover-benchmark: trace marks" << samples->trace.size() << "written to" << tracePath;
                    } else {
                        qWarning() << "hover-benchmark: cannot write trace" << tracePath;
                    }
                }
                if (automatic) {
                    content->setProperty("profileHoverRunning", false);
                    content->setProperty("pointerInside", false);
                }
                if (intervals.empty()) {
                    qWarning() << "hover-benchmark: insufficient swap samples";
                    return;
                }
                std::sort(intervals.begin(), intervals.end());
                qInfo() << "hover-benchmark: Qt swaps/sec" << samples->times.size() / seconds
                        << "interval ms median" << intervals[intervals.size() / 2]
                        << "p95" << intervals[size_t((intervals.size() - 1) * 0.95)]
                        << "max" << intervals.back()
                        << "intervals over 20ms" << std::count_if(intervals.begin(), intervals.end(), [](double v) { return v > 20; })
                        << "of" << intervals.size();
                qInfo() << "hover-benchmark: process CPU percent of one core"
                        << 100.0 * (std::clock() - samples->cpuStart) / CLOCKS_PER_SEC / seconds;
                auto &inside = samples->insideIntervals;
                if (!inside.empty()) {
                    std::sort(inside.begin(), inside.end());
                    qInfo() << "hover-benchmark: inside-hover intervals" << inside.size()
                            << "ms median" << inside[inside.size() / 2]
                            << "p95" << inside[size_t((inside.size() - 1) * .95)]
                            << "over20ms" << std::count_if(inside.begin(), inside.end(), [](double v) { return v > 20; });
                }
                if (!automatic && samples->inputTimes.size() > 1) {
                    std::vector<double> inputIntervals;
                    for (size_t i = 1; i < samples->inputTimes.size(); ++i)
                        inputIntervals.push_back((samples->inputTimes[i] - samples->inputTimes[i - 1]) / 1e6);
                    std::sort(inputIntervals.begin(), inputIntervals.end());
                    qInfo() << "hover-benchmark: native mouse moves" << samples->inputTimes.size()
                            << "per second" << samples->inputTimes.size() / seconds
                            << "interval ms median" << inputIntervals[inputIntervals.size() / 2]
                            << "p95" << inputIntervals[size_t((inputIntervals.size() - 1) * 0.95)];
                    qInfo() << "hover-benchmark: native pointer bounds" << samples->inputMin << samples->inputMax;
                }
                const char *labels[] = {"begin-to-sync", "sync", "sync-to-render", "render", "render-to-swap"};
                for (size_t i = 0; i < samples->durations.size(); ++i) {
                    auto &values = samples->durations[i];
                    if (values.empty()) continue;
                    std::sort(values.begin(), values.end());
                    qInfo() << "hover-benchmark: stage" << labels[i] << "ms median" << values[values.size() / 2]
                            << "p95" << values[size_t((values.size() - 1) * 0.95)];
                }
            });
        });
    });
    ready->start();
}

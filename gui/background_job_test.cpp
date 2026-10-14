#include "background_job.hpp"

#include <QCoreApplication>
#include <QEventLoop>
#include <QThread>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    using microspice::gui::BackgroundJob;
    try {
        BackgroundJob<int> job;
        QEventLoop loop;
        QTimer deadline;
        deadline.setSingleShot(true);
        QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
        std::optional<int> value;
        QString failure;
        bool mainThread = false;
        job.finished = [&](std::optional<int> result, const QString& error) {
            value = result;
            failure = error;
            mainThread = QThread::currentThread() == application.thread();
            loop.quit();
        };
        require(job.start([&](const microspice::ExecutionControl&) {
            require(QThread::currentThread() != application.thread(),
                    "work must run off the UI thread");
            return 42;
        }),
                "first job must start");
        require(!job.start([](const auto&) { return 0; }),
                "overlapping jobs must be rejected");
        deadline.start(5000);
        loop.exec();
        require(
            !job.running() && value == 42 && failure.isEmpty() && mainThread,
            "completion must return results on the UI thread");
        require(job.start([](const auto&) { return 99; }),
                "job must be reusable");
        job.cancel();
        deadline.start(5000);
        loop.exec();
        require(!job.running() && !value && failure.contains("cancelled"),
                "cancelled jobs must never publish results, even if "
                "computation already completed");
        job.start([](const auto&) -> int {
            throw std::runtime_error("bad circuit");
        });
        deadline.start(5000);
        loop.exec();
        require(!value && failure == "bad circuit",
                "worker exceptions must reach the UI");
        bool progressDelivered = false;
        job.progress = [&](std::size_t completed, std::size_t total) {
            if (completed == 3 && total == 10) {
                progressDelivered =
                    QThread::currentThread() == application.thread();
                job.cancel();
            }
        };
        job.start([](const microspice::ExecutionControl& execution) {
            execution.report(3, 10);
            for (;;) {
                execution.check();
            }
            return 0;
        });
        deadline.start(5000);
        loop.exec();
        require(!job.running() && progressDelivered && !value &&
                    failure.contains("cancelled"),
                "progress must reach the UI thread and allow cancellation "
                "during active work");
        {
            BackgroundJob<int> closing;
            closing.start([](const microspice::ExecutionControl& execution) {
                for (;;) {
                    execution.check();
                }
                return 0;
            });
        }
        std::cout << "Background job checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
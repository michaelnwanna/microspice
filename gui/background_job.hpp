#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <optional>

#include "microspice/execution.hpp"

namespace microspice::gui {

template <class Result>
class BackgroundJob final : public QObject {
public:
    explicit BackgroundJob(QObject* parent = nullptr) : QObject(parent) {
        timer_.setInterval(15);
        QObject::connect(&timer_, &QTimer::timeout, this, [this] { poll(); });
    }
    ~BackgroundJob() override {
        cancel();
        if (future_.valid()) {
            future_.wait();
        }
    }

    std::function<void(std::optional<Result>, const QString&)> finished;
    std::function<void(std::size_t, std::size_t)> progress;

    bool running() const { return future_.valid(); }
    void cancel() {
        if (state_) {
            state_->cancelled.store(true);
        }
    }

    template <class Work>
    bool start(Work work) {
        if (running()) {
            return false;
        }
        state_ = std::make_shared<State>();
        auto state = state_;
        future_ = std::async(
            std::launch::async, [state, work = std::move(work)]() mutable {
                const ExecutionControl execution{
                    [state] { return state->cancelled.load(); },
                    [state](std::size_t completed, std::size_t total) {
                        state->total.store(total);
                        state->completed.store(completed);
                    }};
                execution.check();
                auto result = work(execution);
                execution.check();
                return result;
            });
        timer_.start();
        return true;
    }

private:
    struct State {
        std::atomic<bool> cancelled{false};
        std::atomic<std::size_t> completed{0};
        std::atomic<std::size_t> total{0};
    };
    void poll() {
        if (!running()) {
            return;
        }
        if (progress) {
            progress(state_->completed.load(), state_->total.load());
        }
        if (future_.wait_for(std::chrono::seconds(0)) !=
            std::future_status::ready) {
            return;
        }
        timer_.stop();
        std::optional<Result> result;
        QString error;
        try {
            result = future_.get();
        } catch (const std::exception& exception) {
            error = QString::fromUtf8(exception.what());
        } catch (...) {
            error = "Unknown analysis failure";
        }
        if (state_->cancelled.load()) {
            result.reset();
            error = "Analysis cancelled";
        }
        state_.reset();
        if (finished) {
            finished(std::move(result), error);
        }
    }

    QTimer timer_;
    std::shared_ptr<State> state_;
    std::future<Result> future_;
};

}  // namespace microspice::gui
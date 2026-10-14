#pragma once

#include <QCloseEvent>
#include <QMainWindow>
#include <functional>

namespace microspice::gui {

class ProjectWindow final : public QMainWindow {
public:
    std::function<bool()> allowClose;

protected:
    void closeEvent(QCloseEvent* event) override {
        if (!allowClose || allowClose()) {
            event->accept();
        } else {
            event->ignore();
        }
    }
};

}  // namespace microspice::gui
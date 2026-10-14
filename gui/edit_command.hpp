#pragma once

#include <QUndoCommand>
#include <functional>
#include <utility>

#include "project.hpp"

namespace microspice::gui {

class EditCommand final : public QUndoCommand {
public:
    EditCommand(ProjectState before, ProjectState after, QString description,
                std::function<void(const ProjectState&)> apply)
        : QUndoCommand(std::move(description)),
          before_(std::move(before)),
          after_(std::move(after)),
          apply_(std::move(apply)) {}

    void undo() override { apply_(before_); }
    void redo() override {
        if (firstRedo_) {
            firstRedo_ = false;
        } else {
            apply_(after_);
        }
    }

private:
    ProjectState before_;
    ProjectState after_;
    std::function<void(const ProjectState&)> apply_;
    bool firstRedo_ = true;
};

}  // namespace microspice::gui
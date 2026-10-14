#pragma once

#include <QFileInfo>
#include <functional>

#include "editor_scene.hpp"

namespace microspice::gui {

enum class UnsavedChoice { Save, Discard, Cancel };

class ProjectDocument {
public:
    explicit ProjectDocument(EditorScene& scene) : scene_(scene) {}

    QString filename() const { return filename_; }
    QString title() const {
        return filename_.isEmpty() ? label_ : QFileInfo(filename_).fileName();
    }
    bool modified() const { return !scene_.history().isClean(); }

    bool allowReplace(const std::function<UnsavedChoice()>& decide,
                      const std::function<bool()>& saveCurrent) const {
        if (!modified()) {
            return true;
        }
        switch (decide()) {
            case UnsavedChoice::Save:
                return saveCurrent();
            case UnsavedChoice::Discard:
                return true;
            case UnsavedChoice::Cancel:
                return false;
        }
        return false;
    }

    void saveTo(const QString& filename) {
        if (filename.isEmpty()) {
            throw std::invalid_argument("A project filename is required");
        }
        writeProject(filename, scene_.project());
        filename_ = filename;
        scene_.history().setClean();
    }

    void open(const QString& filename) {
        const auto state = readProject(filename);
        scene_.setProject(state);
        filename_ = filename;
    }

    void newProject() {
        scene_.reset();
        filename_.clear();
        label_ = "Untitled schematic";
    }

    void importExample(const QString& filename, const QString& title) {
        const auto state = readProject(filename);
        scene_.setProject(state);
        filename_.clear();
        label_ = title;
        scene_.history().resetClean();
    }

private:
    EditorScene& scene_;
    QString filename_;
    QString label_ = "Voltage divider";
};

}  // namespace microspice::gui
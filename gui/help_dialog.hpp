#pragma once

#include <QDialog>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QSplitter>
#include <QTextBrowser>
#include <QTextDocument>
#include <QToolButton>
#include <QVBoxLayout>

#include "ui_style.hpp"

namespace microspice::gui {

class HelpDialog final : public QDialog {
public:
    explicit HelpDialog(QWidget* parent = nullptr,
                        const QString& resource = ":/help/user-guide.md",
                        const QString& windowTitle = "microspice User Guide")
        : QDialog(parent) {
        setWindowTitle(windowTitle);
        resize(900, 680);
        auto* layout = new QVBoxLayout(this);
        auto* searchLayout = new QHBoxLayout;
        search_ = new QLineEdit(this);
        search_->setPlaceholderText("Find in guide");
        search_->setAccessibleName("Find in user guide");
        auto* previous = new QToolButton(this);
        auto* next = new QToolButton(this);
        previous->setIcon(actionIcon("undo-2"));
        next->setIcon(actionIcon("redo-2"));
        previous->setToolTip("Previous match");
        next->setToolTip("Next match");
        previous->setAccessibleName("Previous match");
        next->setAccessibleName("Next match");
        searchLayout->addWidget(search_, 1);
        searchLayout->addWidget(previous);
        searchLayout->addWidget(next);
        layout->addLayout(searchLayout);
        auto* splitter = new QSplitter(this);
        topics_ = new QListWidget(splitter);
        topics_->setAccessibleName("User guide topics");
        topics_->setWordWrap(true);
        topics_->setTextElideMode(Qt::ElideNone);
        topics_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        topics_->setSpacing(3);
        browser_ = new QTextBrowser(splitter);
        browser_->setAccessibleName("User guide content");
        browser_->setOpenExternalLinks(false);
        QFile guide(resource);
        if (guide.open(QIODevice::ReadOnly)) {
            const auto markdown = QString::fromUtf8(guide.readAll());
            browser_->setMarkdown(markdown);
            for (const auto& line : markdown.split('\n')) {
                if (line.startsWith("## ")) {
                    const auto title = line.mid(3).trimmed();
                    topics_->addItem(title);
                    topics_->item(topics_->count() - 1)->setToolTip(title);
                }
            }
        } else {
            browser_->setPlainText(
                "The bundled user guide could not be loaded.");
        }
        splitter->setChildrenCollapsible(false);
        splitter->setStretchFactor(0, 0);
        splitter->setStretchFactor(1, 1);
        splitter->setSizes({220, 650});
        layout->addWidget(splitter, 1);
        status_ = new QLabel(this);
        status_->setAccessibleName("Guide search result");
        layout->addWidget(status_);
        QObject::connect(topics_, &QListWidget::currentTextChanged, this,
                         [this](const QString& title) {
                             auto cursor = browser_->textCursor();
                             cursor.movePosition(QTextCursor::Start);
                             browser_->setTextCursor(cursor);
                             browser_->find(title);
                             status_->clear();
                         });
        QObject::connect(search_, &QLineEdit::returnPressed, this,
                         [this] { findText(false); });
        QObject::connect(next, &QToolButton::clicked, this,
                         [this] { findText(false); });
        QObject::connect(previous, &QToolButton::clicked, this,
                         [this] { findText(true); });
    }

    bool findText(bool backwards) {
        const auto text = search_->text().trimmed();
        if (text.isEmpty()) {
            status_->clear();
            return false;
        }
        const auto flags = backwards ? QTextDocument::FindBackward
                                     : QTextDocument::FindFlags();
        bool found = browser_->find(text, flags);
        if (!found) {
            auto cursor = browser_->textCursor();
            cursor.movePosition(backwards ? QTextCursor::End
                                          : QTextCursor::Start);
            browser_->setTextCursor(cursor);
            found = browser_->find(text, flags);
        }
        status_->setText(found ? QString() : "No matches");
        return found;
    }

    QString content() const { return browser_->toPlainText(); }
    void setSearchText(const QString& text) { search_->setText(text); }
    int topicCount() const { return topics_->count(); }
    void selectTopic(int index) { topics_->setCurrentRow(index); }
    QString selectedText() const {
        return browser_->textCursor().selectedText();
    }

private:
    QLineEdit* search_;
    QTextBrowser* browser_;
    QLabel* status_;
    QListWidget* topics_;
};

}  // namespace microspice::gui
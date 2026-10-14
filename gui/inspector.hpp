#pragma once

#include <QComboBox>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include "diagnostics.hpp"

namespace microspice::gui {

class SolverInspector final : public QWidget {
public:
    explicit SolverInspector(QWidget* parent = nullptr) : QWidget(parent) {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(10, 10, 10, 10);
        layout->setSpacing(8);
        frames_ = new QComboBox(this);
        frames_->setAccessibleName("Solver frame");
        layout->addWidget(frames_);
        status_ = new QLabel(this);
        status_->setWordWrap(true);
        layout->addWidget(status_);
        auto* tabs = new QTabWidget(this);
        system_ = table(tabs);
        tabs->addTab(system_, "MNA system");
        stamps_ = new QPlainTextEdit(tabs);
        stamps_->setReadOnly(true);
        tabs->addTab(stamps_, "Stamps");
        capacitors_ = table(tabs);
        tabs->addTab(capacitors_, "Capacitors");
        auto* newton = new QSplitter(Qt::Vertical, tabs);
        iterations_ = table(newton);
        diodes_ = table(newton);
        newton->addWidget(iterations_);
        newton->addWidget(diodes_);
        tabs->addTab(newton, "Newton");
        layout->addWidget(tabs, 1);
        setMinimumSize(300, 220);
        QObject::connect(frames_,
                         qOverload<int>(&QComboBox::currentIndexChanged), this,
                         [this](int) { displayFrame(); });
        QObject::connect(
            iterations_, &QTableWidget::currentCellChanged, this,
            [this](int row, int, int, int) { displayDiodes(row); });
    }

    void setFrames(std::vector<DiagnosticFrame> frames) {
        data_ = std::move(frames);
        {
            const QSignalBlocker blocker(frames_);
            frames_->clear();
            for (const auto& frame : data_) {
                frames_->addItem(frame.label);
            }
        }
        displayFrame();
    }
    void clear() { setFrames({}); }
    void selectFrame(int index) { frames_->setCurrentIndex(index); }
    std::size_t frameCount() const { return data_.size(); }
    QTableWidget& systemTable() { return *system_; }
    QTableWidget& iterationTable() { return *iterations_; }
    QTableWidget& diodeTable() { return *diodes_; }
    QTableWidget& capacitorTable() { return *capacitors_; }
    QString stampsText() const { return stamps_->toPlainText(); }
    QString status() const { return status_->text(); }

private:
    static QTableWidget* table(QWidget* parent) {
        auto* widget = new QTableWidget(parent);
        widget->setEditTriggers(QAbstractItemView::NoEditTriggers);
        widget->setSelectionBehavior(QAbstractItemView::SelectRows);
        widget->horizontalHeader()->setSectionResizeMode(
            QHeaderView::ResizeToContents);
        widget->horizontalHeader()->setMinimumSectionSize(70);
        widget->verticalHeader()->setDefaultSectionSize(30);
        widget->setAlternatingRowColors(true);
        return widget;
    }
    static void setup(QTableWidget* widget, const QStringList& headers,
                      std::size_t rows) {
        widget->clear();
        widget->setColumnCount(static_cast<int>(headers.size()));
        widget->setHorizontalHeaderLabels(headers);
        widget->setRowCount(static_cast<int>(rows));
    }
    static void number(QTableWidget* widget, std::size_t row,
                       std::size_t column, double value) {
        auto* item = new QTableWidgetItem(QString::number(value, 'g', 10));
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        widget->setItem(static_cast<int>(row), static_cast<int>(column), item);
    }
    const DiagnosticFrame* selected() const {
        const int index = frames_->currentIndex();
        return index >= 0 && static_cast<std::size_t>(index) < data_.size()
                   ? &data_[static_cast<std::size_t>(index)]
                   : nullptr;
    }
    void displayFrame() {
        const QSignalBlocker iterationBlocker(iterations_);
        const auto* frame = selected();
        if (!frame) {
            for (auto* widget : {system_, capacitors_, iterations_, diodes_}) {
                setup(widget, {}, 0);
            }
            stamps_->clear();
            status_->clear();
            return;
        }
        status_->setText(
            frame->label +
            (frame->converged ? " | Converged" : " | Not converged") +
            (frame->iterations.empty() ? QString()
                                       : " | Final linearized MNA system"));
        auto headers = frame->unknowns;
        headers << "RHS z" << "Solution x";
        const auto size = frame->solution.size();
        setup(system_, headers, size);
        system_->setVerticalHeaderLabels(frame->unknowns);
        for (std::size_t row = 0; row < size; ++row) {
            for (std::size_t column = 0; column < size; ++column) {
                number(system_, row, column, frame->matrix(row, column));
            }
            number(system_, row, size, frame->rhs[row]);
            number(system_, row, size + 1, frame->solution[row]);
        }
        QString stamps;
        for (const auto& stamp : frame->stamps) {
            stamps += QString::fromStdString(stamp.component) + "\n";
            for (const auto& operation : stamp.operations) {
                stamps += "  " + QString::fromStdString(operation) + "\n";
            }
            stamps += '\n';
        }
        if (!frame->capacitors.empty()) {
            stamps +=
                "Static-device stamps; capacitor companion values are in the "
                "Capacitors tab.\n";
        }
        stamps_->setPlainText(stamps);
        setup(capacitors_,
              {"Capacitor", "Previous V", "G_eq (S)", "I_history (A)",
               "Solved V"},
              frame->capacitors.size());
        for (std::size_t row = 0; row < frame->capacitors.size(); ++row) {
            const auto& capacitor = frame->capacitors[row];
            capacitors_->setItem(
                static_cast<int>(row), 0,
                new QTableWidgetItem(QString::fromStdString(capacitor.name)));
            number(capacitors_, row, 1, capacitor.previousVoltage);
            number(capacitors_, row, 2, capacitor.equivalentConductance);
            number(capacitors_, row, 3, capacitor.historyCurrent);
            number(capacitors_, row, 4, capacitor.solvedVoltage);
        }
        setup(iterations_,
              {"Iteration", "Max delta V", "Max delta I",
               "Junction mismatch (V)"},
              frame->iterations.size());
        for (std::size_t row = 0; row < frame->iterations.size(); ++row) {
            const auto& iteration = frame->iterations[row];
            number(iterations_, row, 0, static_cast<double>(iteration.number));
            number(iterations_, row, 1, iteration.maximumVoltageChange);
            number(iterations_, row, 2, iteration.maximumCurrentChange);
            number(iterations_, row, 3, iteration.maximumJunctionMismatch);
        }
        if (!frame->iterations.empty()) {
            iterations_->setCurrentCell(0, 0);
        }
        displayDiodes(frame->iterations.empty() ? -1 : 0);
    }
    void displayDiodes(int row) {
        const auto* frame = selected();
        if (!frame || row < 0 ||
            static_cast<std::size_t>(row) >= frame->iterations.size()) {
            setup(diodes_, {}, 0);
            return;
        }
        const auto& diodes =
            frame->iterations[static_cast<std::size_t>(row)].diodes;
        setup(diodes_,
              {"Diode", "Linearization V", "Solved V", "Current (A)",
               "Conductance (S)", "I_eq (A)"},
              diodes.size());
        for (std::size_t index = 0; index < diodes.size(); ++index) {
            const auto& diode = diodes[index];
            diodes_->setItem(
                static_cast<int>(index), 0,
                new QTableWidgetItem(QString::fromStdString(diode.name)));
            number(diodes_, index, 1, diode.linearizationVoltage);
            number(diodes_, index, 2, diode.solvedVoltage);
            number(diodes_, index, 3, diode.currentAmps);
            number(diodes_, index, 4, diode.conductanceSiemens);
            number(diodes_, index, 5, diode.equivalentCurrentAmps);
        }
    }

    std::vector<DiagnosticFrame> data_;
    QComboBox* frames_;
    QLabel* status_;
    QTableWidget* system_;
    QPlainTextEdit* stamps_;
    QTableWidget* capacitors_;
    QTableWidget* iterations_;
    QTableWidget* diodes_;
};

}  // namespace microspice::gui
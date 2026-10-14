#include <QApplication>
#include <QTemporaryDir>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

#include "editor_scene.hpp"
#include "microspice/dc_solver.hpp"
#include "project_document.hpp"
#include "project_window.hpp"

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    try {
        using namespace microspice::gui;
        EditorScene scene;
        scene.loadDivider();
        auto* diode = scene.place(ComponentKind::Diode, {480, 0});
        diode->setValue(2e-12);
        diode->setIdeality(1.7);
        diode->setRotation(270);
        scene.place(ComponentKind::Capacitor, {480, 120})->setValue(4.7e-6);
        scene.place(ComponentKind::Current, {480, 240})->setValue(-0.002);
        const auto original = scene.project();
        QTemporaryDir directory;
        require(directory.isValid(), "temporary project directory must exist");
        const auto filename = directory.filePath("roundtrip.microspice");
        writeProject(filename, original);
        EditorScene reopened;
        reopened.setProject(readProject(filename));
        require(
            serializeProject(reopened.project()) == serializeProject(original),
            "project roundtrip must preserve every value, position, rotation, "
            "reference, and wire");
        const auto before = serializeProject(reopened.project());
        auto invalid = original;
        invalid.components[0].position = {1, 0};
        bool rejected = false;
        try {
            reopened.setProject(invalid);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        require(rejected && serializeProject(reopened.project()) == before,
                "invalid project must leave current scene intact");
        for (const auto& bad :
             {QByteArray("{"), QByteArray("{}"), QByteArray("[]")}) {
            rejected = false;
            try {
                parseProject(bad);
            } catch (const std::invalid_argument&) {
                rejected = true;
            }
            require(rejected, "malformed project must be rejected");
        }
        const auto originalJson =
            QJsonDocument::fromJson(serializeProject(original)).object();
        std::vector<QJsonObject> malformed;
        auto badVersion = originalJson;
        badVersion["version"] = 2;
        malformed.push_back(badVersion);
        auto missingFields = originalJson;
        missingFields.remove("wires");
        malformed.push_back(missingFields);
        for (const auto& field :
             {QString("kind"), QString("value"), QString("rotation")}) {
            auto badNumbers = originalJson;
            auto entries = badNumbers["components"].toArray();
            auto component = entries[0].toObject();
            component[field] =
                field == "kind" ? QJsonValue(0.5) : QJsonValue("not a number");
            entries[0] = component;
            badNumbers["components"] = entries;
            malformed.push_back(badNumbers);
        }
        auto duplicate = originalJson;
        auto entries = duplicate["components"].toArray();
        entries.append(entries[0]);
        duplicate["components"] = entries;
        malformed.push_back(duplicate);
        auto diagonal = originalJson;
        diagonal["wires"] = QJsonArray{QJsonObject{
            {"start", QJsonArray{0, 0}}, {"end", QJsonArray{20, 20}}}};
        malformed.push_back(diagonal);
        for (const auto& bad : malformed) {
            rejected = false;
            try {
                parseProject(QJsonDocument(bad).toJson());
            } catch (const std::invalid_argument&) {
                rejected = true;
            }
            require(rejected,
                    "unsupported versions, missing fields, bad numbers, "
                    "duplicates, and diagonal wires must be rejected");
        }
        rejected = false;
        try {
            writeProject(filename, invalid);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        require(rejected && serializeProject(readProject(filename)) ==
                                serializeProject(original),
                "invalid save data must not overwrite a valid project file");
        scene.loadDivider();
        writeProject(filename, scene.project());
        reopened.setProject(readProject(filename));
        const auto extracted = reopened.extract();
        const auto result = microspice::solveDc(extracted.circuit);
        require(std::abs(result.voltage(extracted.terminals.at("R1")[1]) - 5) <
                    1e-9,
                "reopened divider must still solve to 5 V");
        require(
            reopened.place(ComponentKind::Resistor, {600, 0})->name() == "R3",
            "new references must not collide with loaded components");
        reopened.history().undo();
        require(reopened.findComponent("R3") == nullptr &&
                    reopened.history().isClean(),
                "undo placement must return to the loaded clean state");
        reopened.history().redo();
        require(reopened.findComponent("R3") != nullptr,
                "redo must restore placement");
        reopened.findComponent("R3")->setValue(2200);
        reopened.history().undo();
        require(reopened.findComponent("R3")->value() == 1000,
                "undo must restore component value");
        reopened.history().redo();
        require(reopened.findComponent("R3")->value() == 2200,
                "redo must restore component value");
        reopened.findComponent("R3")->setSelected(true);
        reopened.rotateSelected();
        reopened.history().undo();
        require(reopened.findComponent("R3")->rotation() == 0,
                "rotation must be undoable");
        reopened.history().redo();
        require(reopened.findComponent("R3")->rotation() == 90,
                "rotation must be redoable");
        reopened.findComponent("R3")->setSelected(true);
        reopened.removeSelected();
        reopened.history().undo();
        require(reopened.findComponent("R3") != nullptr,
                "deletion must be undoable");
        reopened.history().redo();
        require(reopened.findComponent("R3") == nullptr,
                "deletion must be redoable");
        const auto beforeWire = reopened.project();
        reopened.addWire({600, 0}, {600, 120});
        reopened.history().undo();
        require(reopened.project() == beforeWire,
                "wire creation must be undoable");
        reopened.history().redo();
        require(reopened.project().wires.size() == beforeWire.wires.size() + 1,
                "wire creation must be redoable");
        reopened.history().setClean();
        reopened.findComponent("R1")->setValue(4700);
        require(!reopened.history().isClean(),
                "editing must mark the document dirty");
        reopened.history().undo();
        require(reopened.history().isClean(),
                "undo back to a save point must mark the document clean");
        reopened.setProject(readProject(filename));
        const auto beforeMove = reopened.project();
        reopened.findComponent("R1")->setPos(0, -200);
        auto movedCircuit = reopened.extract();
        require(
            std::abs(microspice::solveDc(movedCircuit.circuit)
                         .voltage(movedCircuit.terminals.at("R1")[1]) -
                     5) < 1e-9,
            "moving a wired resistor must preserve its electrical connections");
        validateProject(reopened.project());
        reopened.history().undo();
        require(reopened.project() == beforeMove,
                "undo move must restore components and wire routing together");
        reopened.history().redo();
        reopened.findComponent("R1")->setSelected(true);
        reopened.rotateSelected();
        movedCircuit = reopened.extract();
        require(std::abs(microspice::solveDc(movedCircuit.circuit)
                             .voltage(movedCircuit.terminals.at("R1")[1]) -
                         5) < 1e-9,
                "rotating a wired resistor must preserve its electrical "
                "connections");
        validateProject(reopened.project());
        ProjectDocument document(reopened);
        int prompts = 0;
        const auto cancel = [&prompts] {
            ++prompts;
            return UnsavedChoice::Cancel;
        };
        const auto failedSave = [] { return false; };
        require(!document.allowReplace(cancel, failedSave),
                "cancel must prevent replacement of a dirty project");
        require(!document.allowReplace([] { return UnsavedChoice::Save; },
                                       failedSave),
                "cancelled or failed save must prevent replacement");
        require(document.allowReplace([] { return UnsavedChoice::Discard; },
                                      failedSave),
                "discard must allow replacement");
        document.saveTo(filename);
        require(!document.modified() && document.filename() == filename,
                "successful save must set filename and clean state");
        const int beforePrompt = prompts;
        require(document.allowReplace(cancel, failedSave) &&
                    prompts == beforePrompt,
                "clean documents must not prompt");
        reopened.findComponent("R1")->setValue(3300);
        const auto dirty = reopened.project();
        rejected = false;
        try {
            document.saveTo(directory.filePath("missing/failed.microspice"));
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(
            rejected && document.modified() && document.filename() == filename,
            "failed save must preserve filename and dirty state");
        rejected = false;
        try {
            document.open(directory.filePath("absent.microspice"));
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected && reopened.project() == dirty,
                "failed open must preserve the current document");
        require(document.allowReplace([] { return UnsavedChoice::Save; },
                                      [&document, &filename] {
                                          document.saveTo(filename);
                                          return true;
                                      }),
                "successful save must allow replacement");
        document.newProject();
        require(!document.modified() && document.filename().isEmpty() &&
                    reopened.components().empty(),
                "new project must start empty and clean");
        document.open(filename);
        require(!document.modified() && document.filename() == filename,
                "open must establish a clean project");
        ProjectWindow window;
        window.allowClose = [] { return false; };
        window.show();
        application.processEvents();
        require(!window.close() && window.isVisible(),
                "cancel must keep the window open");
        window.allowClose = [] { return true; };
        require(window.close(), "approved close must close the window");
        EditorScene bounded;
        bounded.loadDivider();
        for (int index = 1; index <= 105; ++index) {
            bounded.findComponent("R1")->setValue(1000 + index);
        }
        require(bounded.history().count() == 100,
                "undo history must be bounded to 100 edits");
        auto distant = bounded.project();
        distant.components[0].position = {20000, 20000};
        bounded.setProject(distant);
        require(bounded.sceneRect().contains(
                    bounded.findComponent(distant.components[0].reference)
                        ->sceneBoundingRect()),
                "loaded distant components must remain navigable");
        if (application.arguments().size() > 1) {
            EditorScene sample;
            sample.setProject(readProject(application.arguments()[1]));
            const auto sampleCircuit = sample.extract();
            require(std::abs(microspice::solveDc(sampleCircuit.circuit)
                                 .voltage(sampleCircuit.terminals.at("R1")[1]) -
                             5) < 1e-9,
                    "checked-in project sample must solve to 5 V");
        }
        std::cout << "Project checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
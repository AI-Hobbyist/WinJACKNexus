/* This file is part of WinJACKNexus. SPDX-License-Identifier: AGPL-3.0-only */
#include <WinJACKNexus/Node/NodeApi.h>
#include <WinJACKNexus/NativeFx/deesser/Ui.hpp>

#include <QApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLibrary>
#include <QLineEdit>
#include <QMouseEvent>

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
bool check(bool condition, const char* message) {
    std::printf("%s %s\n", condition ? "PASS" : "FAIL", message);
    return condition;
}

void click(QWidget& widget, const QPointF& position) {
    QMouseEvent press(QEvent::MouseButtonPress, position, Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, position, Qt::LeftButton,
                        Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &release);
}
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    if (argc < 3) return 1;
    QLibrary module(QString::fromLocal8Bit(argv[1]));
    bool passed = check(module.load(), "Native Fx module loads");
    if (!passed) return 1;
    using GetCount = std::uint32_t (*)();
    using GetText = const char* (*)(std::uint32_t);
    using CreateNode = WinJACKNexus::Node::INode* (*)(std::uint32_t);
    using DestroyNode = void (*)(WinJACKNexus::Node::INode*);
    using GetJson = const char* (*)(WinJACKNexus::Node::INode*);
    using SetJson = int (*)(WinJACKNexus::Node::INode*, const char*);
    auto getCount = reinterpret_cast<GetCount>(module.resolve("wjn_get_node_count"));
    auto getName = reinterpret_cast<GetText>(module.resolve("wjn_get_node_name"));
    auto getCategory = reinterpret_cast<GetText>(module.resolve("wjn_get_node_category"));
    auto createNode = reinterpret_cast<CreateNode>(module.resolve("wjn_create_node"));
    auto destroyNode = reinterpret_cast<DestroyNode>(module.resolve("wjn_destroy_node"));
    auto getJson = reinterpret_cast<GetJson>(module.resolve("wjn_get_node_settings_json"));
    auto setJson = reinterpret_cast<SetJson>(module.resolve("wjn_set_node_settings_json"));
    passed &= check(getCount && getName && getCategory && createNode && destroyNode && getJson && setJson,
                    "Native Fx entry points resolve");
    if (!passed) return 1;
    passed &= check(getCount() == 1 && std::strcmp(getName(0), "De-Esser") == 0
                    && std::strcmp(getCategory(0), "Native Fx") == 0,
                    "De-Esser is exported as Native Fx");
    auto* node = createNode(0);
    passed &= check(node != nullptr, "De-Esser node is created");
    if (node) {
        const auto window = node->createWindow(nullptr);
        passed &= check(window != nullptr, "De-Esser standalone window opens without JACK connections");
        if (window) node->destroyWindow(window);
        auto document = QJsonDocument::fromJson(getJson(node));
        auto root = document.object();
        auto channels = root.value(QStringLiteral("channels")).toArray();
        passed &= check(channels.size() == 8, "Eight channel settings are serialized");
        if (channels.size() == 8) {
            auto channel = channels.at(3).toObject();
            channel.insert(QStringLiteral("thresholdDb"), -19.0);
            channels[3] = channel;
            root.insert(QStringLiteral("channels"), channels);
            const auto saved = QJsonDocument(root).toJson(QJsonDocument::Compact);
            passed &= check(setJson(node, saved.constData()) == 1,
                            "Valid per-channel settings restore");
            document = QJsonDocument::fromJson(getJson(node));
            passed &= check(document.object().value(QStringLiteral("channels")).toArray().at(3)
                                .toObject().value(QStringLiteral("thresholdDb")).toDouble() == -19.0,
                            "Restored threshold survives serialization");
        }
        destroyNode(node);
    }

    WinJACKNexus::UI::DeEsserWidget widget;
    passed &= check(widget.isWindow() && widget.parentWidget() == nullptr && widget.size() == QSize(768, 650),
                    "Effect UI is a separate top-level window");
    std::array<WinJACKNexus::UI::DeEsserWidget::Parameters, 8> changed {};
    int notifications = 0;
    widget.setChangeHandler([&](std::size_t channel, const auto& parameters) {
        changed[channel] = parameters; ++notifications;
    });
    click(widget, QPointF(137, 88)); // L
    click(widget, QPointF(215, 568)); // Split mode
    passed &= check(notifications == 1 && changed[0].mode == WinJACKNexus::Common::DeEsserMode::Split,
                    "L selector edits one channel");
    click(widget, QPointF(59, 88)); // All
    click(widget, QPointF(302, 568)); // Dynamic EQ
    passed &= check(notifications == 9 && changed[0].mode == WinJACKNexus::Common::DeEsserMode::DynamicEq
                    && changed[7].mode == WinJACKNexus::Common::DeEsserMode::DynamicEq,
                    "All selector edits all eight channels");
    WinJACKNexus::UI::DeEsserWidget gestureWidget;
    int knobNotifications = 0;
    float changedFrequency = 0.0F;
    gestureWidget.setChangeHandler([&](std::size_t, const auto& parameters) {
        ++knobNotifications; changedFrequency = parameters.centerFrequencyHz;
    });
    click(gestureWidget, QPointF(137, 88)); // L
    const QPointF knobCenter(480, 287);
    QMouseEvent knobPress(QEvent::MouseButtonPress, knobCenter, Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&gestureWidget, &knobPress);
    passed &= check(knobNotifications == 0, "Pressing a knob does not jump its value");
    QMouseEvent knobMove(QEvent::MouseMove, QPointF(480, 257), Qt::NoButton,
                         Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&gestureWidget, &knobMove);
    QMouseEvent knobRelease(QEvent::MouseButtonRelease, QPointF(480, 257), Qt::LeftButton,
                            Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&gestureWidget, &knobRelease);
    passed &= check(knobNotifications == 1 && changedFrequency > 6500.0F,
                    "Dragging a knob changes the selected channel frequency");
    click(gestureWidget,QPointF(57,88)); // All
    knobNotifications=0;
    click(gestureWidget,QPointF(480,324)); // Center frequency readout in kHz
    auto* editor=gestureWidget.findChild<QLineEdit*>(QStringLiteral("numericValueEditor"));
    passed &= check(editor!=nullptr,"De-Esser readout opens a precise value editor");
    if(editor){
        editor->setText(QStringLiteral("7.3"));
        QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);
        QApplication::sendEvent(editor,&enter);
    }
    passed &= check(knobNotifications==8&&std::abs(changedFrequency-7300.0F)<0.01F,
                    "De-Esser All numeric edit applies kHz value to eight channels");
    knobNotifications=0;
    QMouseEvent reset(QEvent::MouseButtonDblClick,QPointF(480,287),Qt::LeftButton,
                      Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(&gestureWidget,&reset);
    passed &= check(knobNotifications==8&&std::abs(changedFrequency-6500.0F)<0.01F,
                    "De-Esser double click restores all center frequencies");
    QImage image(widget.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    widget.render(&image);
    passed &= check(image.pixelColor(250, 445) == image.pixelColor(150, 445),
                    "Frequency curve has no fill below its outline");
    passed &= check(image.pixelColor(220, 433) == image.pixelColor(160, 433),
                    "Spectrum path does not erase the grid below the curve");
    passed &= check(image.save(QString::fromLocal8Bit(argv[2])), "UI review image is saved");
    return passed ? 0 : 1;
}

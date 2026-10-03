/* Actual Native Fx DLLs, offline instances and Windows UI. SPDX-License-Identifier: AGPL-3.0-only */
#include <WinJACKNexus/NativeFx/NativeFxApi.h>
#include <WinJACKNexus/Node/NodeApi.h>
#include <WinJACKNexus/UI/HostI18n.hpp>
#include <WinJACKNexus/UI/LocalizedText.hpp>
#include <WinJACKNexus/UI/Controls.hpp>
#include <WinJACKNexus/UI/ThemeCore.hpp>
#include <WinJACKNexus/UI/ThemeManager.hpp>
#include <WinJACKNexus/UI/FxSettingsWindow.hpp>
#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QFontDatabase>
#include <QGlyphRun>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QLabel>
#include <QLibrary>
#include <QListWidget>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QPointer>
#include <QPainter>
#include <QHelpEvent>
#include <QToolTip>
#include <QMenu>
#include <QAction>
#include <QCursor>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextLayout>
#include <QTimer>
#include <QWindow>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace WinJACKNexus;
using namespace WinJACKNexus::UI;
namespace {
int checks = 0;
void check(bool condition, const char* message) {
    ++checks;
    if (!condition) { std::fprintf(stderr, "S5 FAILED: %s\n", message); std::exit(1); }
}
void settle(int milliseconds = 100) {
    QEventLoop loop; QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit); loop.exec();
}
QStringList presetKeys(const QString& effect) {
    if (effect=="gate") return {"preset.default","preset.tight","preset.gentle","preset.custom"};
    if (effect=="graphic_eq") return {"preset.default","preset.vocalBright","preset.bassBoost","preset.classical","preset.custom"};
    if (effect=="deesser") return {"preset.default","preset.brightVocal","preset.softVoice","preset.custom"};
    if (effect=="compressor") return {"preset.default","preset.punchyDrums","preset.gentleLevel","preset.custom"};
    if (effect=="limiter") return {"preset.default","preset.broadcast","preset.maxLoudness","preset.custom"};
    if (effect=="delay") return {"preset.default","preset.vocalEcho","preset.shortSlap","preset.rhythmicEcho","preset.ambientEcho","preset.tapeEcho","preset.custom"};
    if (effect=="parametric_eq") return {"preset.default","preset.vocalClear","preset.bassBoost","preset.warmth","preset.presence","preset.custom"};
    return {"preset.default","preset.smallRoom","preset.brightPlate","preset.cathedral","preset.studioBooth","preset.vintageSpring","preset.custom"};
}
template<class T> T resolve(QLibrary& library, const char* name) {
    auto function = reinterpret_cast<T>(library.resolve(name));
    if (!function) std::fprintf(stderr, "Missing export: %s (%s)\n", name, library.errorString().toUtf8().constData());
    check(function != nullptr, "actual module export exists"); return function;
}
QJsonObject catalog(const QString& path) {
    QFile file(path); check(file.open(QIODevice::ReadOnly), "actual language catalog opens");
    const auto object = QJsonDocument::fromJson(file.readAll()).object();
    check(!object.isEmpty(), "language catalog parses"); return object;
}
void bindings(QObject& root) {
    auto objects = root.findChildren<QObject*>(); objects.prepend(&root);
    for (auto* object : objects) for (const auto& name : object->dynamicPropertyNames()) {
        if (!name.startsWith("wjn.i18n.")) continue;
        const auto property = name.mid(9);
        const auto domain = object->property((QByteArray("wjn.i18nDomain.")+property).constData()).toByteArray();
        const auto key = object->property(name).toByteArray();
        check(object->property(property).toString() == localizedText(domain.constData(),key.constData(),"MISSING"),
              "actual bound property has current catalog text");
    }
}
void capture(QWidget& widget, const QString& output, const QString& name) {
    widget.show(); widget.raise(); widget.activateWindow(); settle(150);
    check(widget.isVisible() && widget.windowHandle() && widget.windowHandle()->isExposed(), "actual visible exposed Windows window");
    check(widget.font().family() == QApplication::font().family(), "system font inherited");
    QTextLayout layout(QStringLiteral("English 简体中文 日本語 한국어 −∞"),widget.font());
    layout.beginLayout(); auto line = layout.createLine(); line.setLineWidth(2000); layout.endLayout();
    for (const auto& run : layout.glyphRuns()) for (auto glyph : run.glyphIndexes()) check(glyph != 0, "system glyph fallback covers four languages");
    auto* window = widget.window();
    const auto position = window->pos();
    const auto available = window->screen()->availableGeometry();
    const QSize step(std::min(window->width(),std::max(100,available.width()-100)),
                     std::min(window->height(),std::max(100,available.height()-100)));
    for (int y=0; y<window->height(); y+=step.height()) for (int x=0; x<window->width(); x+=step.width()) {
        const QRect tile(QPoint(x,y),QSize(std::min(step.width(),window->width()-x),std::min(step.height(),window->height()-y)));
        window->move(available.topLeft()+QPoint(30,30)-tile.topLeft()); settle();
        const QRect actual(window->mapToGlobal(tile.topLeft()),tile.size());
        auto* screen = QGuiApplication::screenAt(actual.center());
        check(screen && screen->availableGeometry().contains(actual), "entire capture region on visible desktop");
        const auto pixels = screen->grabWindow(window->winId(),x,y,tile.width(),tile.height());
        check(!pixels.isNull() && pixels.save(output+'/'+name+QStringLiteral("-%1-%2.png").arg(y).arg(x)), "capture native screen pixels");
    }
    window->move(position); bindings(widget);
}
void clientName(HostI18n& host, const QString& output) {
    int writes = 0;
    Controls::ClientNameWidget widget(QStringLiteral("Original"),[&](const QString& name) { ++writes; return name != QStringLiteral("reject"); });
    widget.resize(440,150);
    auto* editor = widget.findChild<QLineEdit*>(QStringLiteral("gate.clientName"));
    auto* apply = widget.findChild<QPushButton*>(QStringLiteral("gate.applyClientName"));
    check(editor && apply,"actual shared client name controls");
    const auto status = [&] {
        for (auto* label : widget.findChildren<QLabel*>())
            if (!label->property("wjn.i18n.text").isValid()) return label->text();
        return QStringLiteral("MISSING");
    };
    QString error;
    for (const auto& language : I18nManager::supportedLanguages()) {
        editor->setText(QStringLiteral("用户_日本語_한국어 draft")); editor->setSelection(1,4);
        const auto prior = writes;
        check(host.manager().setLanguage(language,&error),"switch with client name draft"); settle();
        check(editor->text()==QStringLiteral("用户_日本語_한국어 draft") && editor->selectionStart()==1 && editor->selectedText().size()==4 && writes==prior,"draft, selection and handler unchanged");
        editor->clear(); apply->click();
        check(writes==prior && status()==localizedText("ui","clientName.empty","MISSING"),"empty validation localized without applying");
        editor->setText(QStringLiteral("reject")); apply->click();
        check(status()==localizedText("ui","clientName.rejected","MISSING"),"failed apply localized");
        editor->setText(QStringLiteral(" Saved ")); apply->click();
        check(editor->text()==QStringLiteral("Saved") && status()==localizedText("ui","clientName.applied","MISSING"),"successful trimmed apply retains status");
        const auto after = writes;
        check(host.manager().setLanguage(language==QStringLiteral("en-US")?QStringLiteral("ja-JP"):QStringLiteral("en-US"),&error),"switch with successful status");
        check(writes==after && status()==localizedText("ui","clientName.applied","MISSING"),"status hot refresh does not invoke apply");
        check(host.manager().setLanguage(language,&error),"restore client name capture language");
        capture(widget,output,QStringLiteral("client-name-")+language);
    }
}
class ElisionWindow final : public QWidget {
public:
    QString fullText() const { return localizedText("ui","clientName.label","Client name").repeated(12); }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this); painter.translate(30,30);
        Controls::drawElidedText(painter,QRectF(0,0,65,35),fullText(),font(),palette().color(QPalette::Text));
    }
};
void elision(HostI18n& host,const QString& output) {
    ElisionWindow widget; widget.resize(200,120); widget.move(50,50);
    widget.show(); widget.raise(); widget.activateWindow(); settle(); QString error;
    check(widget.windowHandle() && widget.windowHandle()->isExposed(),"tooltip fixture is a real exposed Windows window");
    for (const auto& language : I18nManager::supportedLanguages()) {
        check(host.manager().setLanguage(language,&error),"select elision language"); widget.repaint();
        QCursor::setPos(widget.mapToGlobal(QPoint(35,35))); settle();
        QHelpEvent help(QEvent::ToolTip,QPoint(35,35),widget.mapToGlobal(QPoint(35,35)));
        QCoreApplication::sendEvent(&widget,&help); settle();
        if (!QToolTip::isVisible() || QToolTip::text()!=Qt::convertFromPlainText(widget.fullText()))
            std::fprintf(stderr,"Tooltip diagnostic: visible=%d actual=[%s] expected=[%s]\n",int(QToolTip::isVisible()),QToolTip::text().toUtf8().constData(),Qt::convertFromPlainText(widget.fullText()).toUtf8().constData());
        check(QToolTip::isVisible() && QToolTip::text()==Qt::convertFromPlainText(widget.fullText()),"transformed text hit rectangle exposes complete unshrunk caption");
        check(host.manager().setLanguage(language==QStringLiteral("en-US")?QStringLiteral("ko-KR"):QStringLiteral("en-US"),&error),"change language while full tooltip is visible"); settle();
        check(QToolTip::isVisible() && QToolTip::text()==Qt::convertFromPlainText(widget.fullText()),"already visible full tooltip refreshes immediately with language");
        QToolTip::hideText(); capture(widget,output,QStringLiteral("shared-elision-")+language);
    }
}
void module(HostI18n& host, const QString& runtime, const QString& output, const QString& effect, const QStringList& languages) {
    const auto path = QDir(runtime).filePath(QStringLiteral("nodes/native_fx/%1/mod_%1.dll").arg(effect));
    QLibrary library(path); check(library.load(),"actual Native Fx DLL loads");
    auto meta = resolve<const WjnFxMeta*(*)(uint32_t)>(library,"wjn_fx_meta");
    auto descriptor = resolve<const char*(*)(uint32_t)>(library,"wjn_fx_i18n_descriptor_json");
    auto create = resolve<WjnFxInstance*(*)(uint32_t)>(library,"wjn_fx_create_mixer");
    auto destroy = resolve<void(*)(WjnFxInstance*)>(library,"wjn_fx_destroy_mixer");
    auto editor = resolve<QWidget*(*)(WjnFxInstance*,QWidget*)>(library,"wjn_fx_create_mixer_editor");
    auto size = resolve<uint32_t(*)()>(library,"wjn_fx_parameter_snapshot_size");
    auto exportState = resolve<int(*)(const WjnFxInstance*,void*,uint32_t)>(library,"wjn_fx_export_parameter_snapshot");
    auto importState = resolve<int(*)(WjnFxInstance*,const void*,uint32_t)>(library,"wjn_fx_import_parameter_snapshot");
    auto presetCount = resolve<uint32_t(*)()>(library,"wjn_fx_preset_count");
    auto presetName = resolve<const char*(*)(uint32_t)>(library,"wjn_fx_preset_name");
    auto selectPreset = resolve<void(*)(WjnFxInstance*,int)>(library,"wjn_fx_select_preset");
    auto currentPreset = resolve<int(*)(const WjnFxInstance*)>(library,"wjn_fx_current_preset");
    auto setChannel = resolve<void(*)(WjnFxInstance*,int)>(library,"wjn_fx_set_channel");
    const auto id = QString::fromUtf8(meta(0)->id), originalName = QString::fromUtf8(meta(0)->name);
    const auto domain = QStringLiteral("nativefx.")+id;
    const auto description = QJsonDocument::fromJson(descriptor(0)).object();
    check(description.value("moduleId").toString()==id && descriptor(1)==nullptr,"optional descriptor matches stable module identity");
    QString error;
    auto fxLease = host.manager().registerLibrary(path,I18nManager::Extension::nativeFx,0,&error);
    auto nodeLease = host.manager().registerLibrary(path,I18nManager::Extension::node,0,&error);
    check(bool(fxLease) && bool(nodeLease),"both placements register actual shared language domain");
    check(host.manager().setLanguage(languages.front(),&error),"select initial language before creating module windows");
    auto* instance = create(0); auto* second = create(0); check(instance && second,"independent offline mixer instances");
    QWidget* panel = editor(instance,nullptr); QWidget* secondPanel=editor(second,nullptr);
    check(panel && secondPanel && panel!=secondPanel,"actual two editor instances");
    auto snapshot = [&](const WjnFxInstance* value) { QByteArray bytes(size(),'\0'); check(exportState(value,bytes.data(),bytes.size())==1,"export actual eight-channel snapshot"); return bytes; };
    // Import different factory states into selected channels; do not collapse to an All preset.
    for (uint32_t channel=0; channel<8; ++channel) {
        selectPreset(second,channel%(presetCount()-1)); auto bytes=snapshot(second);
        WjnFxParameterSnapshotHeader header{}; std::memcpy(&header,bytes.constData(),sizeof(header));
        header.channelMask=1u<<channel; std::memcpy(bytes.data(),&header,sizeof(header));
        check(importState(instance,bytes.constData(),bytes.size())==1,"seed independently masked channel parameters");
    }
    const auto before = snapshot(instance), secondBefore = snapshot(second);
    auto createNode=resolve<Node::INode*(*)(uint32_t)>(library,"wjn_create_node");
    auto destroyNode=resolve<void(*)(Node::INode*)>(library,"wjn_destroy_node");
    auto setName=resolve<int(*)(void*,const char*)>(library,"wjn_fx_node_set_client_name");
    auto nodeSettings=resolve<const char*(*)(Node::INode*)>(library,"wjn_get_node_settings_json");
    auto* node=createNode(0); check(node && setName(node,"S5_User_Name")==1,"node exists without JACK init");
    const QByteArray nodeBefore(nodeSettings(node));
    auto hwnd=node->createWindow(nullptr); check(hwnd!=nullptr,"actual node standalone native window");
    auto* nodePanel=QWidget::find(reinterpret_cast<WId>(hwnd)); check(nodePanel,"actual Qt node widget found by HWND");
    const auto locate=QFileInfo(path).absolutePath()+QStringLiteral("/locate/");
    const auto initial=catalog(locate+languages.front()+QStringLiteral(".json")).value("domains").toObject().value(domain).toObject();
    check(panel->windowTitle()==initial.value("module.name").toString()
          && nodePanel->windowTitle()==initial.value("window.title").toString().arg(QStringLiteral("S5_User_Name")),"first frame uses selected language without a post-construction switch");
    for (const auto& language : languages) {
        const auto presetBeforeLanguage=currentPreset(instance);
        check(host.manager().setLanguage(language,&error),"switch actual module language"); settle();
        check(currentPreset(instance)==presetBeforeLanguage,"language preserves the current channel's preset identity");
        const auto values=catalog(locate+language+QStringLiteral(".json")).value("domains").toObject().value(domain).toObject();
        check(panel->windowTitle()==values.value("module.name").toString(),"mixer title relocalizes");
        check(nodePanel->windowTitle()==values.value("window.title").toString().arg(QStringLiteral("S5_User_Name")),"node title keeps user identity");
        check(panel->accessibleDescription()==values.value("module.description").toString(),"complete module description relocalizes");
        for (uint32_t index=0; index<presetCount(); ++index) {
            const auto keys=presetKeys(effect);
            check(presetCount()==keys.size() && QString::fromUtf8(presetName(index))==values.value(keys[index]).toString(),"each stable preset index has its specified current-language caption");
        }
        check(presetName(presetCount())==nullptr,"invalid preset index rejected");
        for (int channel=-1; channel<8; ++channel) {
            setChannel(instance,channel);
            check(snapshot(instance)==before,"channel selection and language preserve all parameter bytes");
        }
        check(snapshot(second)==secondBefore && QByteArray(nodeSettings(node))==nodeBefore
              && QString::fromUtf8(meta(0)->name)==originalName,"other instance, node settings and raw SDK metadata stable");
        capture(*panel,output,effect+QStringLiteral("-mixer-")+language);
        capture(*nodePanel,output,effect+QStringLiteral("-node-")+language);
        // Open each module's real numeric editor through its existing readout hit target.
        QPoint readout;
        if (effect=="gate") readout={440,335};
        else if (effect=="graphic_eq") readout={119,624};
        else if (effect=="deesser") readout={480,332};
        else if (effect=="compressor") readout={454,336};
        else if (effect=="limiter") readout={591,347};
        else if (effect=="delay") readout={170,409};
        else if (effect=="parametric_eq") readout={qRound(91.0*nodePanel->width()/1536),qRound(883.0*nodePanel->height()/1136)};
        else readout={185,473};
        nodePanel->move(nodePanel->screen()->availableGeometry().center()-readout); settle();
        const QPointF global=nodePanel->mapToGlobal(readout);
        QMouseEvent press(QEvent::MouseButtonPress,QPointF(readout),global,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QCoreApplication::sendEvent(nodePanel,&press);
        QMouseEvent release(QEvent::MouseButtonRelease,QPointF(readout),global,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
        QCoreApplication::sendEvent(nodePanel,&release); settle();
        QPointer<QLineEdit> draft=nodePanel->findChild<QLineEdit*>(QStringLiteral("numericValueEditor"));
        check(draft && draft->isVisible(),"actual module readout opens shared numeric editor");
        draft->setText(QStringLiteral("1.")); draft->setSelection(1,1);
        const auto draftLocale=draft->locale();
        const auto alternate=language==QStringLiteral("en-US")?QStringLiteral("ja-JP"):QStringLiteral("en-US");
        check(host.manager().setLanguage(alternate,&error),"language changes with actual pending numeric input"); settle();
        check(draft && draft->isVisible() && draft->text()==QStringLiteral("1.") && draft->selectionStart()==1
              && draft->selectedText()==QStringLiteral(".") && draft->locale()==draftLocale,"actual numeric draft, selection, editor and number locale preserved");
        check(snapshot(instance)==before && snapshot(second)==secondBefore && QByteArray(nodeSettings(node))==nodeBefore,"pending input and language switch never commit either placement parameters");
        const QRect draftRect(draft->mapToGlobal(QPoint(0,0)),draft->size());
        auto* draftScreen=QGuiApplication::screenAt(draftRect.center());
        check(draftScreen && draftScreen->availableGeometry().contains(draftRect),"numeric draft entirely visible on real desktop");
        const auto draftPixels=draftScreen->grabWindow(nodePanel->winId(),draft->x(),draft->y(),draft->width(),draft->height());
        check(!draftPixels.isNull() && draftPixels.save(output+'/'+effect+QStringLiteral("-draft-")+language+QStringLiteral(".png")),"capture actual current-language pending input");
        QKeyEvent escape(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier); QCoreApplication::sendEvent(draft,&escape); settle();
        check(!draft || !draft->isVisible(),"Escape cancels pending numeric editor");
        // Enter the real preset menu, then change language while its native window is open.
        QPoint menuPoint(530,188);
        if (effect=="compressor") menuPoint={540,195};
        else if (effect=="limiter") menuPoint={520,195};
        else if (effect=="delay") menuPoint={530,185};
        else if (effect=="parametric_eq") menuPoint={qRound(969.0*nodePanel->width()/1536),qRound(171.0*nodePanel->height()/1136)};
        bool menuChecked=false;
        QTimer::singleShot(180,nodePanel,[&] {
            auto* menu=qobject_cast<QMenu*>(QApplication::activePopupWidget());
            check(menu && menu->isVisible() && menu->windowHandle() && menu->windowHandle()->isExposed(),"actual preset menu exposed on Windows");
            const auto actions=menu->actions(); const auto keys=presetKeys(effect);
            check(!actions.isEmpty() && actions.size()<=keys.size(),"factory menu preserves stable preset order");
            QList<QVariant> data;
            for (int i=0;i<actions.size();++i) {
                data.append(actions[i]->data());
                check(actions[i]->text()==host.manager().text(domain,keys[i],"MISSING"),"open menu uses current-language preset caption by index");
            }
            check(host.manager().setLanguage(language,&error),"hot switch while module preset menu is open"); settle();
            check(menu->isVisible() && menu->actions()==actions,"language preserves actual open menu and action identities");
            for (int i=0;i<actions.size();++i) check(actions[i]->data()==data[i]
                && actions[i]->text()==host.manager().text(domain,keys[i],"MISSING"),"existing menu action relocalizes with stable data and index");
            capture(*menu,output,effect+QStringLiteral("-preset-menu-")+language);
            menuChecked=true; menu->close();
        });
        QMouseEvent openMenu(QEvent::MouseButtonPress,QPointF(menuPoint),QPointF(nodePanel->mapToGlobal(menuPoint)),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QCoreApplication::sendEvent(nodePanel,&openMenu); settle(250);
        check(menuChecked && QByteArray(nodeSettings(node))==nodeBefore,"opening, translating and dismissing preset menu does not select or mutate state");
        if (effect!="graphic_eq" && effect!="reverb") {
            QPoint operationPoint(723,188);
            QStringList operationKeys={"action.restoreDefaults"};
            if (effect=="compressor") operationPoint={727,195};
            else if (effect=="limiter") { operationPoint={705,195}; operationKeys={"action.resetCurrent","action.resetAll","","action.copyToAll"}; }
            else if (effect=="delay") { operationPoint={705,185}; operationKeys={"action.resetCurrent","action.resetAll"}; }
            else if (effect=="parametric_eq") { operationPoint={qRound(1315.0*nodePanel->width()/1536),qRound(171.0*nodePanel->height()/1136)}; operationKeys={"action.resetDefault"}; }
            bool operationsChecked=false;
            QTimer::singleShot(180,nodePanel,[&] {
                auto* menu=qobject_cast<QMenu*>(QApplication::activePopupWidget()); check(menu && menu->isVisible(),"actual module operations menu open");
                const auto actions=menu->actions(); check(actions.size()==operationKeys.size(),"actual reset/copy menu operations retain order");
                check(host.manager().setLanguage(alternate,&error),"language switches while reset/copy menu remains open"); settle();
                check(menu->isVisible() && menu->actions()==actions,"actual operations actions retain object identity");
                for (int i=0;i<actions.size();++i) if (!operationKeys[i].isEmpty())
                    check(actions[i]->text()==host.manager().text(domain,operationKeys[i],"MISSING"),"open reset/copy caption relocalizes by semantic key");
                capture(*menu,output,effect+QStringLiteral("-operations-menu-")+language); operationsChecked=true; menu->close();
            });
            QMouseEvent openOperations(QEvent::MouseButtonPress,QPointF(operationPoint),QPointF(nodePanel->mapToGlobal(operationPoint)),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
            QCoreApplication::sendEvent(nodePanel,&openOperations); settle(250);
            check(operationsChecked && QByteArray(nodeSettings(node))==nodeBefore,"translation and dismissal never trigger reset/copy operations");
        }
        if (effect=="graphic_eq") for (bool save : {false,true}) {
            bool dialogChecked=false;
            QTimer dialogProbe; dialogProbe.setInterval(100); int dialogAttempts=0;
            QObject::connect(&dialogProbe,&QTimer::timeout,nodePanel,[&] {
                check(++dialogAttempts<50,"actual EQ preset dialog appears within bounded observation");
                auto* dialog=qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
                if (!dialog || !dialog->isVisible() || !dialog->windowHandle() || !dialog->windowHandle()->isExposed()) return;
                dialogProbe.stop();
                check(dialog && dialog->objectName()==QStringLiteral("nativeFxPresetFileDialog")
                      && dialog->isVisible() && dialog->windowHandle()->isExposed(),"actual EQ preset file dialog is visible on Windows");
                auto* filename=dialog->findChild<QLineEdit*>(QStringLiteral("fileNameEdit"));
                auto* types=dialog->findChild<QComboBox*>(QStringLiteral("fileTypeCombo"));
                check(filename && types && types->count()==1,"actual file dialog pending filename and filter controls");
                filename->setFocus(); filename->setText(QStringLiteral("Draft_日本語_한국어.json")); filename->setSelection(1,4);
                const auto directory=dialog->directory().absolutePath();
                check(host.manager().setLanguage(alternate,&error),"change language with actual preset filename draft"); settle();
                check(filename->text()==QStringLiteral("Draft_日本語_한국어.json") && filename->selectionStart()==1
                      && filename->selectedText().size()==4 && dialog->directory().absolutePath()==directory,"file dialog draft, selection and directory preserved");
                check(dialog->windowTitle()==host.manager().text(domain,save?"dialog.savePreset":"dialog.loadPreset","MISSING")
                      && types->itemText(0)==host.manager().text(domain,"filter.presetFiles","MISSING"),"existing file dialog title and JSON filter relocalize");
                capture(*dialog,output,effect+(save?QStringLiteral("-save-dialog-"):QStringLiteral("-load-dialog-"))+language);
                dialogChecked=true; dialog->reject();
            });
            dialogProbe.start();
            const QPoint filePoint(690,save?477:423);
            QMouseEvent openFile(QEvent::MouseButtonPress,QPointF(filePoint),QPointF(nodePanel->mapToGlobal(filePoint)),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
            QCoreApplication::sendEvent(nodePanel,&openFile); settle();
            check(dialogChecked && QByteArray(nodeSettings(node))==nodeBefore,"cancelling translated preset dialog preserves node settings");
        }
        fxLease.reset();
        check(host.manager().setLanguage(language==QStringLiteral("en-US")?QStringLiteral("ko-KR"):QStringLiteral("en-US"),&error),"remaining node lease keeps translation active");
        check(panel->windowTitle()==host.manager().text(domain,QStringLiteral("module.name"),QStringLiteral("MISSING")),"live mixer remains translated through node lease");
        fxLease=host.manager().registerLibrary(path,I18nManager::Extension::nativeFx,0,&error); check(bool(fxLease),"FX lease reacquires shared domain");
    }
    check(host.manager().setLanguage(languages.back(),&error),"reopen node under active language");
    node->destroyWindow(hwnd); hwnd=node->createWindow(nullptr);
    nodePanel=QWidget::find(reinterpret_cast<WId>(hwnd)); check(nodePanel,"actual node editor reopens");
    check(nodePanel->windowTitle()==host.manager().text(domain,"window.title","MISSING").arg(QStringLiteral("S5_User_Name"))
          && QByteArray(nodeSettings(node))==nodeBefore,"reopened node first frame is localized and preserves settings");
    capture(*nodePanel,output,effect+QStringLiteral("-reopened-")+languages.back());
    nodeLease.reset(); fxLease.reset();
    const auto english=catalog(locate+QStringLiteral("en-US.json")).value("domains").toObject().value(domain).toObject();
    check(panel->windowTitle()==english.value("module.name").toString()
          && nodePanel->windowTitle()==english.value("window.title").toString().arg(QStringLiteral("S5_User_Name")),"detached live displays return to complete local English");
    check(snapshot(instance)==before && snapshot(second)==secondBefore,"detach does not mutate either instance");
    node->destroyWindow(hwnd); destroyNode(node); destroy(second); destroy(instance);
    check(library.unload(),"actual DLL unloads after widgets, instances and last lease");
    check(host.manager().setLanguage(QStringLiteral("en-US"),&error),"switch after unload has no stale callback");
    // Use the actual DLL with an isolated package to exercise per-entry fallback.
    QTemporaryDir fallback; check(fallback.isValid(),"isolated actual module fallback fixture");
    const auto copyPath=fallback.filePath(QStringLiteral("mod_")+effect+QStringLiteral(".dll"));
    check(QFile::copy(path,copyPath) && QDir().mkpath(fallback.filePath("locate")),"copy real module without modifying its deployed package");
    check(QFile::copy(locate+"manifest.json",fallback.filePath("locate/manifest.json"))
          && QFile::copy(locate+"en-US.json",fallback.filePath("locate/en-US.json")),"fixture has full mandatory English and manifest");
    auto partial=catalog(locate+"zh-CN.json"); auto domains=partial.value("domains").toObject();
    auto translated=domains.value(domain).toObject(); translated.remove("module.name");
    domains[domain]=translated; partial["domains"]=domains;
    { QFile file(fallback.filePath("locate/zh-CN.json")); check(file.open(QIODevice::WriteOnly),"partial target fixture opens"); check(file.write(QJsonDocument(partial).toJson())>0,"partial target fixture saved"); }
    QLibrary copied(copyPath); check(copied.load(),"actual fallback fixture DLL loads");
    auto fallbackCreate=resolve<WjnFxInstance*(*)(uint32_t)>(copied,"wjn_fx_create_mixer");
    auto fallbackDestroy=resolve<void(*)(WjnFxInstance*)>(copied,"wjn_fx_destroy_mixer");
    auto fallbackEditor=resolve<QWidget*(*)(WjnFxInstance*,QWidget*)>(copied,"wjn_fx_create_mixer_editor");
    auto lease=host.manager().registerLibrary(copyPath,I18nManager::Extension::nativeFx,0,&error);
    check(bool(lease) && host.manager().setLanguage("zh-CN",&error),"actual module accepts incomplete target with full English");
    auto* fallbackInstance=fallbackCreate(0); auto* fallbackPanel=fallbackEditor(fallbackInstance,nullptr);
    check(fallbackPanel && fallbackPanel->windowTitle()==english.value("module.name").toString()
          && fallbackPanel->accessibleDescription()==translated.value("module.description").toString(),"missing title falls back per key while translated description remains localized");
    capture(*fallbackPanel,output,effect+QStringLiteral("-partial-fallback"));
    check(host.manager().setLanguage("ko-KR",&error),"actual module selects absent target package");
    check(fallbackPanel->windowTitle()==english.value("module.name").toString()
          && fallbackPanel->accessibleDescription()==english.value("module.description").toString(),"absent target falls back to same-module full English");
    fallbackDestroy(fallbackInstance); lease.reset(); check(copied.unload(),"fallback fixture unloads after last lease");
}
void fxHost(HostI18n& host,const QString& output,const QStringList& languages) {
    QString error;
    check(host.manager().setLanguage(languages.front(),&error),"initial FX host language before creation");
    const QStringList ids={"gate","graphic-eq","deesser","compressor","limiter","delay","parametric-eq","reverb"};
    std::vector<Common::MixerEffectState> chain;
    for (const auto& id : ids) chain.push_back({id,true,1,{}});
    FxSettingsWindow window(QStringLiteral("User_Name"),{"L","R","C","LFE","Ls","Rs","Lrb","Rrb"});
    window.setMeterReadHandler([](bool) { return 0.0F; });
    window.loadChain(chain,true);
    auto* list=window.findChild<QListWidget*>(QStringLiteral("fxChainList"));
    auto* preset=window.findChild<QComboBox*>(QStringLiteral("fxModulePreset"));
    auto* area=window.findChild<QScrollArea*>(QStringLiteral("fxEditorArea"));
    check(list && preset && area && list->count()==8,"actual FX host discovers and creates all eight modules");
    int writes=0; QByteArray published;
    window.setChainCommitHandler([&](const std::vector<Common::MixerEffectState>& state,bool enabled) {
        ++writes; QJsonArray rows;
        for (const auto& effect : state) rows.append(QJsonObject{{"id",effect.moduleId},{"enabled",effect.enabled},{"preset",effect.preset},{"parameters",effect.parameters}});
        published=QJsonDocument(QJsonObject{{"enabled",enabled},{"effects",rows}}).toJson(QJsonDocument::Compact); return true;
    });
    // Capture real host snapshots through the public command handler; do not infer state from text.
    window.setChainEnabled(false); window.setChainEnabled(true);
    const auto before=published;
    check(!before.isEmpty(),"actual host serializes chain through commit handler");
    for (int row=0; row<list->count(); ++row) {
        list->setCurrentRow(row); settle();
        auto* editor=area->widget(); check(editor,"actual selected module editor");
        auto* item=list->item(row); const auto identity=item->data(Qt::UserRole);
        const auto selectedPreset=preset->currentIndex();
        for (const auto& language : languages) {
            const auto count=writes;
            check(host.manager().setLanguage(language,&error),"hot switch through actual FX host"); settle();
            check(writes==count && list->currentItem()==item && item->data(Qt::UserRole)==identity
                  && area->widget()==editor && preset->currentIndex()==selectedPreset,"language preserves editor, row, identity, preset and makes no chain write");
            for (int i=0; i<list->count(); ++i) {
                const auto domain=QStringLiteral("nativefx.")+ids[i];
                check(list->item(i)->text()==host.manager().text(domain,"module.name","MISSING"),"existing actual FX cards display localized module names");
            }
            const auto domain=QStringLiteral("nativefx.")+ids[row];
            check(editor->windowTitle()==host.manager().text(domain,"module.name","MISSING"),"actual embedded editor has current module language");
            capture(window,output,QStringLiteral("fx-host-%1-%2").arg(ids[row],language));
            window.setChainEnabled(false); window.setChainEnabled(true);
            check(published==before,"actual serialized FX chain and all parameter snapshots unchanged by translation");
        }
    }
}
void community(HostI18n& host,const QString& runtime,const QString& output,const QStringList& languages) {
    const auto path=QDir(runtime).filePath(QStringLiteral("examples/native_fx/wjn_example_native_fx.dll"));
    QLibrary library(path); check(library.load(),"real community Native Fx example loads");
    auto create=resolve<WjnFxInstance*(*)(uint32_t)>(library,"wjn_fx_create_mixer");
    auto destroy=resolve<void(*)(WjnFxInstance*)>(library,"wjn_fx_destroy_mixer");
    auto editor=resolve<QWidget*(*)(WjnFxInstance*,QWidget*)>(library,"wjn_fx_create_mixer_editor");
    auto createNode=resolve<Node::INode*(*)(uint32_t)>(library,"wjn_create_node");
    auto destroyNode=resolve<void(*)(Node::INode*)>(library,"wjn_destroy_node");
    auto setName=resolve<int(*)(void*,const char*)>(library,"wjn_fx_node_set_client_name");
    auto settings=resolve<const char*(*)(Node::INode*)>(library,"wjn_get_node_settings_json");
    QString error;
    auto fxLease=host.manager().registerLibrary(path,I18nManager::Extension::nativeFx,0,&error);
    auto nodeLease=host.manager().registerLibrary(path,I18nManager::Extension::node,0,&error);
    check(bool(fxLease) && bool(nodeLease) && host.manager().setLanguage(languages.front(),&error),"example dual placement leases and initial language");
    auto* instance=create(0); auto* view=editor(instance,nullptr); auto* node=createNode(0);
    check(instance && view && node && setName(node,"Example_User_Name")==1,"example actual offline instances");
    auto hwnd=node->createWindow(nullptr); auto* window=QWidget::find(reinterpret_cast<WId>(hwnd));
    check(window,"actual community example node window"); const QByteArray before(settings(node));
    for (const auto& language : languages) {
        check(host.manager().setLanguage(language,&error),"example actual windows hot switch"); settle();
        const auto name=localizedText("example.nativefx","name","MISSING");
        check(view->windowTitle()==name && window->windowTitle()==name+QStringLiteral(" — Example_User_Name"),"both real example window titles localized with stable client name");
        check(QByteArray(settings(node))==before,"example serialized node gain and client identity stable");
        capture(*view,output,QStringLiteral("community-mixer-")+language);
        capture(*window,output,QStringLiteral("community-node-")+language);
        auto* spin=window->findChild<QDoubleSpinBox*>(); check(spin,"actual community gain control");
        auto* draft=spin->findChild<QLineEdit*>(); check(draft,"actual community numeric line editor");
        draft->setFocus(); draft->setText(QStringLiteral("1.")); draft->setSelection(1,1);
        const QByteArray draftBefore(settings(node)); const auto value=spin->value();
        check(host.manager().setLanguage(language==QStringLiteral("en-US")?QStringLiteral("ja-JP"):QStringLiteral("en-US"),&error),"example changes language with actual numeric draft"); settle();
        check(draft->text()==QStringLiteral("1.") && draft->selectionStart()==1 && draft->selectedText()==QStringLiteral(".")
              && spin->value()==value && QByteArray(settings(node))==draftBefore,"example numeric draft, selection and state preserved during language switch");
    }
    nodeLease.reset(); fxLease.reset();
    check(view->windowTitle()==QStringLiteral("Example Gain") && window->windowTitle()==QStringLiteral("Example Gain — Example_User_Name"),"real detached community windows fall back to local English");
    node->destroyWindow(hwnd); destroyNode(node); destroy(instance); check(library.unload(),"actual community DLL unloads after UI destruction");
}
}
int main(int argc,char** argv) {
    QApplication app(argc,argv); app.setQuitOnLastWindowClosed(false);
    check(app.platformName()==QStringLiteral("windows"),"offscreen forbidden; actual platform windows");
    app.setFont(QFontDatabase::systemFont(QFontDatabase::GeneralFont));
    const auto args=app.arguments(); check(args.size()>=3,"runtime root and output path supplied");
    check(QDir().mkpath(args[2]),"artifact output directory");
    QTemporaryDir settings; check(settings.isValid(),"isolated language settings");
    QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    QStringList languages=I18nManager::supportedLanguages();
    const auto settingsFile=settings.filePath(QStringLiteral("language.ini"));
    const auto cold=args.indexOf(QStringLiteral("--cold"));
    if (cold>=0) { check(args.size()>cold+1,"cold locale supplied"); languages={args[cold+1]}; QSettings saved(settingsFile,QSettings::IniFormat); saved.setValue(QStringLiteral("i18n/language"),languages[0]); saved.sync(); }
    HostI18n host({settingsFile,{QStringLiteral("en-US")},false}); QString error;
    check(host.initialize(args[1],&error),"host initialized before UI");
    if (cold>=0) check(host.manager().selectedLanguage()==languages[0],"fresh process restores exact language");
    ThemeCore core; check(core.setOverride(QStringLiteral("--wjn-theme-global-font-source"),QStringLiteral("system"),{},&error),"system font selected through existing Theme Token");
    ThemeManager::installThemeCore(&core); ThemeManager themes; themes.watchApplication(&app);
    for (const auto* effect : {"gate","graphic_eq","deesser","compressor","limiter","delay","parametric_eq","reverb"}) module(host,args[1],args[2],QString::fromLatin1(effect),languages);
    fxHost(host,args[2],languages);
    community(host,args[1],args[2],languages);
    check(host.manager().setLanguage(QStringLiteral("en-US"),&error),"language change after actual FX host destruction has no dangling binding");
    if (cold<0) { clientName(host,args[2]); elision(host,args[2]); }
    std::printf("S5 module Windows checks PASS: %d; platform=%s font=%s scale=%s\n",checks,app.platformName().toUtf8().constData(),app.font().family().toUtf8().constData(),qgetenv("QT_SCALE_FACTOR").constData());
    return 0;
}

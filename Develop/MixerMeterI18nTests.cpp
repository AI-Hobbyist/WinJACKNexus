/* SPDX-License-Identifier: AGPL-3.0-only */
#include <WinJACKNexus/UI/HostI18n.hpp>
#include <WinJACKNexus/UI/LocalizedText.hpp>
#include <WinJACKNexus/UI/MixerWindow.hpp>
#include <WinJACKNexus/UI/AuxSettingsWindow.hpp>
#include <WinJACKNexus/UI/FxSettingsWindow.hpp>
#include <WinJACKNexus/UI/ThemeCore.hpp>
#include <WinJACKNexus/UI/ThemeManager.hpp>
#include <WinJACKNexus/UI/HistoryChartWidget.hpp>
#include <WinJACKNexus/UI/HistoryLegendWidget.hpp>
#include <WinJACKNexus/UI/MetricStatisticsWidget.hpp>
#include <WinJACKNexus/UI/MetricReadout.hpp>
#include <WinJACKNexus/Meter/Mod/MeterbridgeWindow.hpp>
#include <WinJACKNexus/Meter/Mod/MeterHistoryWindow.hpp>
#include <WinJACKNexus/Meter/Mod/MeterCard.hpp>
#include <WinJACKNexus/Meter/Mod/MeterSettingsWidget.hpp>
#include <WinJACKNexus/Meter/MeterCsvWriter.hpp>
#include "../src/ui/src/LocalizedMixerErrors.hpp"
#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSettings>
#include <QSlider>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QWindow>
#include <QTextLayout>
#include <QGlyphRun>
#include <QCheckBox>
#include <QSignalBlocker>
#include <QImage>
#include <algorithm>
#include <cstdio>
#include <cstdlib>

using namespace WinJACKNexus;
using namespace WinJACKNexus::UI;
using namespace WinJACKNexus::Meter;
using namespace WinJACKNexus::Meter::Mod;
namespace {
int checks = 0;
void check(bool value, const char* reason) {
    ++checks;
    if (!value) { std::fprintf(stderr, "S4 check failed: %s\n", reason); std::fflush(stderr); std::exit(1); }
}
void settle(int milliseconds = 70) {
    QEventLoop loop; QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit); loop.exec();
}
void expose(QWidget& widget) {
    widget.show(); widget.raise(); widget.activateWindow(); settle(160);
    check(widget.isVisible() && widget.window()->windowHandle() && widget.window()->windowHandle()->isExposed(), "visible exposed Windows window");
}
void bound(QObject& root) {
    auto objects = root.findChildren<QObject*>(); objects.prepend(&root);
    for (auto* object : objects) for (const auto& property : object->dynamicPropertyNames()) {
        if (!property.startsWith("wjn.i18n.")) continue;
        const auto target = property.mid(9);
        const auto domain = object->property((QByteArray("wjn.i18nDomain.") + target).constData()).toByteArray();
        const auto key = object->property(property).toByteArray();
        check(object->property(target).toString() == localizedText(domain.constData(), key.constData(), "missing"), "bound display property uses the active domain catalog");
    }
}
void capture(QWidget& widget, const QString& directory, const QString& file) {
    expose(widget); bound(widget);
    check(widget.font().family() == QApplication::font().family(), "window uses default system font family");
    QTextLayout layout(QStringLiteral("English 简体中文 日本語 한국어 −∞"), widget.font());
    layout.beginLayout(); auto line = layout.createLine(); line.setLineWidth(2000); layout.endLayout();
    for (const auto& run : layout.glyphRuns())
        for (auto glyph : run.glyphIndexes()) check(glyph != 0, "system font and Qt font fallback cover four-language glyphs");
    const auto image = widget.window()->screen()->grabWindow(widget.window()->winId());
    check(!image.isNull() && image.save(directory + '/' + file + QStringLiteral(".png")), "capture actual visible native window");
}
struct Provider final : MeterViewDataProvider {
    std::vector<MeterViewSnapshot> rows;
    int commands = 0;
    std::vector<MeterViewSnapshot> snapshots() const override { return rows; }
    bool dispatch(const MeterViewCommand&) override { ++commands; return true; }
};
Provider fixture() {
    Provider provider;
    for (int index = 0; index < 2; ++index) {
        MeterViewSnapshot snapshot;
        snapshot.instanceId = QStringLiteral("stable-probe-%1").arg(index);
        snapshot.clientName = index == 0 ? QStringLiteral("用户_日本語_한국어_LongName") : QStringLiteral("Custom probe");
        snapshot.nameRevision = 17;
        snapshot.layoutLabel = index == 0 ? QStringLiteral("立体声") : QStringLiteral("自定义 8");
        snapshot.presetId = QStringLiteral("ebu-r128");
        snapshot.state = MeterProbeState::active;
        snapshot.recordingEnabled = index == 1;
        snapshot.channelLayoutEditable = index == 1;
        snapshot.channels = {{QStringLiteral("Left user"),QStringLiteral("M+030"),false}, {QStringLiteral("Right user"),QStringLiteral("M-030"),false}};
        if (index == 1) for (int channel = 2; channel < 8; ++channel)
            snapshot.channels.push_back({QStringLiteral("User %1").arg(channel), QStringLiteral("LFE"), channel == 3});
        snapshot.currentMetrics = {-12.0F,-20.0F,-11.5F,-23.0F,-24.0F,-23.0F,7.5F};
        snapshot.historyView.mode = MeterHistoryViewMode::scanning;
        snapshot.historyView.paused = true;
        snapshot.historyView.timeWindowMilliseconds = 30000;
        snapshot.historyView.visibleSeriesIds = {QStringLiteral("peak"),QStringLiteral("integrated"),QStringLiteral("lra")};
        for (int point = 0; point < 40; ++point) {
            MeterHistoryPoint data;
            data.elapsedMilliseconds = point * 1000; data.systemTimeMilliseconds = 1700000000000LL + point * 1000;
            data.metrics = snapshot.currentMetrics; data.lfePeakDbfs = -32.0F;
            snapshot.history.push_back(data);
        }
        snapshot.latestHistoryElapsedMilliseconds = 39000;
        snapshot.csvPreview.activeFilePath = QStringLiteral("D:/用户/record.csv");
        snapshot.csvPreview.statusKind = index == 0 ? MeterCsvStatusKind::stopped : MeterCsvStatusKind::queueFull;
        snapshot.csvPreview.droppedRecordCount = 23;
        snapshot.globalSettings.logRootDirectory = QStringLiteral("D:/用户/日志");
        provider.rows.push_back(snapshot);
    }
    return provider;
}
}
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    check(app.platformName() == QStringLiteral("windows"), "offscreen forbidden; actual platform must be windows");
    app.setFont(QFontDatabase::systemFont(QFontDatabase::GeneralFont));
    app.setQuitOnLastWindowClosed(false);
    const auto arguments = app.arguments();
    check(arguments.size() >= 3, "runtime root and screenshot destination supplied");
    const auto output = arguments[2]; check(QDir().mkpath(output), "screenshot artifact directory");
    QTemporaryDir isolated; check(isolated.isValid(), "isolated settings and file fixtures");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, isolated.path());
    const bool cold = arguments.contains(QStringLiteral("--cold"));
    const auto settingsPath = cold ? arguments[4] : isolated.filePath(QStringLiteral("language.ini"));
    HostI18n host({settingsPath, {QStringLiteral("en-US")}, false});
    QString error; check(host.initialize(arguments[1], &error), "host catalog initializes before windows");
    if (cold) check(host.manager().selectedLanguage() == arguments[5], "cold process restores exact language selection");
    ThemeCore theme;
    check(theme.setOverride(QStringLiteral("--wjn-theme-global-font-source"), QStringLiteral("system"), {}, &error), "existing Theme Token selects system font");
    ThemeManager::installThemeCore(&theme); ThemeManager themes; themes.watchApplication(&app);
    auto provider = fixture();
    Common::DigitalMixerState model;
    const auto input = model.add(Common::MixerStripKind::input, Common::MixerLayout::stereo);
    model.add(Common::MixerStripKind::aux, Common::MixerLayout::stereo);
    model.add(Common::MixerStripKind::subMix, Common::MixerLayout::surround71);
    model.strip(input)->name = QStringLiteral("用户_日本語_한국어_LongName");
    model.strip(input)->comment = QStringLiteral("Keep comment 123.45");
    const auto modelBytes = model.toJson(); int commits = 0;
    MixerWindow mixer(&model, [&](const Common::DigitalMixerState&, QString*) { ++commits; return true; }, [](std::uint64_t,bool) { return 0.0F; });
    auto state = std::make_shared<Common::AuxMixerState>();
    AuxSettingsWindow aux(state); aux.setSource(QStringLiteral("stable-source"),QStringLiteral("User name"));
    aux.setMeterReadCallback([](bool) { return 0.0F; });
    AuxSettingsWindow sub(state); sub.setSubMixMode(true); sub.setSource(QStringLiteral("stable-sub"),QStringLiteral("Sub user"));
    sub.setMeterReadCallback([](bool) { return 0.0F; });
    FxSettingsWindow fx(QStringLiteral("User source"), {QStringLiteral("L"),QStringLiteral("R")});
    int fxCommits = 0; fx.setChainCommitHandler([&](const std::vector<Common::MixerEffectState>&,bool) { ++fxCommits; return true; });
    fx.setMeterReadHandler([](bool) { return 0.0F; });
    MeterbridgeWindow bridge(provider);
    MeterHistoryWindow history(provider, [] {}, [] {}); history.setSnapshots(provider.rows, provider.rows[1].instanceId);
    MeterCard card(provider, [] {}, [](const QString&) {}); card.setSnapshot(provider.rows[0]);
    Provider empty; MeterbridgeWindow emptyBridge(empty);
    if (cold) {
        expose(history); bound(history);
        check(history.statistics()->entries().size() == 8, "cold history renders all eight metrics");
        check(history.chart()->verticalAxisLabel() == localizedText("meterbridge","history.levelAxis","missing"), "cold chart first frame uses restored language");
        capture(fx, output, QStringLiteral("cold-fx-") + arguments[5]);
        std::printf("S4 cold %s: %d checks PASS\n",arguments[5].toUtf8().constData(),checks);
        ThemeManager::installThemeCore(nullptr); return 0;
    }
    const auto initialFxCommits = fxCommits;
    const auto selectedAux = aux.selectedBusId();
    const auto* editorArea = fx.findChild<QScrollArea*>(QStringLiteral("fxEditorArea"));
    check(editorArea && editorArea->widget(), "actual FX chain editor is present");
    const auto* editor = editorArea->widget();
    const auto cursor = history.chart()->timeCursorMilliseconds();
    const auto points = history.chart()->series();
    const QStringList languages {QStringLiteral("en-US"),QStringLiteral("zh-CN"),QStringLiteral("ja-JP"),QStringLiteral("ko-KR")};
    for (const auto& language : languages) {
        expose(history);
        auto* draft = history.settingsWidget()->findChild<QLineEdit*>(QStringLiteral("meterSettings.logRoot"));
        check(draft, "real log-root edit exists"); draft->setFocus(); settle();
        draft->setText(QStringLiteral("D:/未提交_日本語_한국어")); draft->setSelection(3,4);
        const auto initialCommands = provider.commands;
        const auto mainCommits = commits;
        check(host.manager().setLanguage(language,&error), "hot language selection succeeds"); settle();
        check(draft->text() == QStringLiteral("D:/未提交_日本語_한국어") && draft->selectionStart() == 3 && draft->selectedText().size() == 4, "pending edit and selection survive language switch");
        check(model.toJson() == modelBytes && commits == mainCommits, "model bytes and topology callbacks stay unchanged");
        check(fxCommits == initialFxCommits && editorArea->widget() == editor, "FX editor identity and commit callbacks stay unchanged");
        check(aux.selectedBusId() == selectedAux && aux.property("wjn.sourceName").toString() == QStringLiteral("User name"), "AUX selection and source identity stay unchanged");
        check(provider.commands == initialCommands, "language refresh dispatches no meter commands");
        check(history.chart()->timeCursorMilliseconds() == cursor, "paused scan cursor remains unchanged");
        check(history.chart()->series().size() == points.size(), "rendered series count remains unchanged");
        for (std::size_t i = 0; i < points.size(); ++i) {
            check(history.chart()->series()[i].id == points[i].id && history.chart()->series()[i].points.size() == points[i].points.size(), "series IDs and point counts remain unchanged");
            for (std::size_t point = 0; point < points[i].points.size(); ++point)
                check(history.chart()->series()[i].points[point].timestampMilliseconds == points[i].points[point].timestampMilliseconds
                    && history.chart()->series()[i].points[point].value == points[i].points[point].value, "rendered measured points remain unchanged");
        }
        check(card.findChild<MetricReadout*>(QStringLiteral("meterCard.readout.Integrated"))->label() == localizedText("meterbridge","metric.integrated","missing"), "card metric cache uses current language");
        check(history.chart()->verticalAxisLabel() == localizedText("meterbridge","history.levelAxis","missing"), "history axis cache uses current language");
        capture(history,output,QStringLiteral("history-")+language);
        capture(mixer,output,QStringLiteral("mixer-")+language);
        auto* details = mixer.openView(MixerWindow::View::channelDetails); check(details,"actual SEL view");
        capture(*details,output,QStringLiteral("sel-")+language); details->hide();
        auto* overview = mixer.openView(MixerWindow::View::channelOverview); check(overview,"actual overview view");
        capture(*overview,output,QStringLiteral("overview-")+language); overview->hide();
        capture(aux,output,QStringLiteral("aux-")+language); aux.hide();
        capture(sub,output,QStringLiteral("submix-")+language); sub.hide();
        capture(fx,output,QStringLiteral("fx-")+language); fx.hide();
        capture(card,output,QStringLiteral("card-")+language); card.hide();
        capture(bridge,output,QStringLiteral("bridge-")+language); bridge.hide();
        capture(emptyBridge,output,QStringLiteral("bridge-empty-")+language); emptyBridge.hide();
        const auto alternate = language == QStringLiteral("en-US") ? QStringLiteral("ja-JP") : QStringLiteral("en-US");
        expose(history);
        auto* numeric = history.settingsWidget()->findChild<QDoubleSpinBox*>(QStringLiteral("meterSettings.silenceThreshold"));
        check(numeric,"real numeric editor"); numeric->setKeyboardTracking(false);
        auto* numericEdit = numeric->findChild<QLineEdit*>(); check(numericEdit,"spin box's actual editor");
        numericEdit->setFocus(); settle();
        const auto oldNumber = numeric->value(); const auto oldNumberText = numericEdit->text();
        numericEdit->setText(QStringLiteral("-3. dBFS")); numericEdit->setSelection(0,3);
        const auto beforeNumeric = provider.commands;
        check(host.manager().setLanguage(alternate,&error),"switch with numeric draft"); settle();
        check(numericEdit->text() == QStringLiteral("-3. dBFS") && numericEdit->selectionStart() == 0 && numericEdit->selectedText().size() == 3,"unsubmitted numeric text and selection preserved");
        check(numeric->value() == oldNumber && provider.commands == beforeNumeric,"numeric value and provider commands unchanged during switch");
        numericEdit->setText(oldNumberText);
        check(host.manager().setLanguage(language,&error),"restore language after draft test"); settle();
        auto* table = history.settingsWidget()->findChild<QTableWidget*>(QStringLiteral("meterSettings.channelTable"));
        check(table && table->rowCount() == 8,"eight-channel actual custom table");
        auto* channelDraft = qobject_cast<QLineEdit*>(table->cellWidget(0,0)); check(channelDraft,"actual channel-name editor");
        const auto oldChannelName = channelDraft->text(); channelDraft->setFocus(); settle();
        channelDraft->setText(QStringLiteral("未提交_Channel_日本語")); channelDraft->setSelection(1,5);
        const auto beforeTable = provider.commands;
        check(host.manager().setLanguage(alternate,&error),"switch with custom channel draft"); settle();
        check(table->cellWidget(0,0) == channelDraft && channelDraft->text() == QStringLiteral("未提交_Channel_日本語") && channelDraft->selectionStart() == 1,"channel editor identity/draft/selection preserved");
        check(provider.commands == beforeTable,"table language update emits no model command");
        channelDraft->setText(oldChannelName);
        check(host.manager().setLanguage(language,&error),"restore language after table test");
        expose(*details);
        auto* channelMenu = details->findChild<QMenu*>(QStringLiteral("selChannelSelectorMenu")); check(channelMenu,"actual SEL channel menu");
        channelMenu->popup(details->mapToGlobal(QPoint(40,80))); settle(); bound(*channelMenu);
        check(channelMenu->actions().size() == 4,"all stable channel groups remain available");
        QAction* chooseInput = nullptr;
        for (auto* category : channelMenu->actions()) if (category->menu())
            for (auto* action : category->menu()->actions())
                if (action->data().toULongLong() == input) chooseInput = action;
        check(chooseInput,"stable model ID identifies the input action"); channelMenu->hide(); chooseInput->trigger(); settle();
        auto* nameDraft = details->findChild<QLineEdit*>(QStringLiteral("selChannelName")); check(nameDraft,"actual SEL name editor");
        nameDraft->setFocus(); settle(); const auto oldName = nameDraft->text();
        nameDraft->setText(QStringLiteral("SEL_未提交_日本語")); nameDraft->setSelection(4,3);
        const auto beforeSel = commits;
        check(host.manager().setLanguage(alternate,&error),"switch with SEL draft"); settle();
        check(nameDraft->text() == QStringLiteral("SEL_未提交_日本語") && nameDraft->selectionStart() == 4 && commits == beforeSel,"SEL draft, selection and commit count preserved");
        nameDraft->setText(oldName);
        check(host.manager().setLanguage(language,&error),"restore language after SEL test"); details->hide();
        expose(fx);
        QTimer::singleShot(100, &app, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget()); check(dialog,"actual FX preset modal");
            auto* edit = dialog->findChild<QLineEdit*>(QStringLiteral("fxChainPresetName")); check(edit,"actual FX preset name editor");
            edit->setText(QStringLiteral("用户_FX_Preset")); edit->setSelection(0,2);
            check(host.manager().setLanguage(alternate,&error),"switch inside FX preset dialog"); settle();
            check(edit->text() == QStringLiteral("用户_FX_Preset") && edit->selectionStart() == 0,"FX preset draft preserved");
            capture(*dialog,output,QStringLiteral("fx-preset-hot-")+language); dialog->reject();
        });
        auto* saveAs = fx.findChild<QPushButton*>(QStringLiteral("fxChainPresetSaveAs")); check(saveAs,"FX save-as action"); saveAs->click();
        check(host.manager().setLanguage(language,&error),"restore language after FX modal"); fx.hide();
        expose(card);
        auto* preset = card.findChild<QComboBox*>(QStringLiteral("meterCard.preset")); check(preset,"actual reference preset selector");
        const auto customIndex = preset->findData(QStringLiteral("custom")); check(customIndex >= 0,"custom entry stable ID");
        QTimer::singleShot(100,&app,[&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget()); check(dialog,"actual custom-reference modal");
            auto* edit = dialog->findChild<QLineEdit*>(QStringLiteral("lufsPreset.name")); check(edit,"custom reference name editor");
            edit->setText(QStringLiteral("Reference_日本語")); edit->setSelection(0,4);
            check(host.manager().setLanguage(alternate,&error),"switch inside reference dialog"); settle();
            check(edit->text() == QStringLiteral("Reference_日本語") && edit->selectionStart() == 0,"reference draft preserved");
            capture(*dialog,output,QStringLiteral("reference-hot-")+language); dialog->reject();
        });
        check(QMetaObject::invokeMethod(preset,"activated",Qt::DirectConnection,Q_ARG(int,customIndex)),"activate actual custom-reference entry");
        check(host.manager().setLanguage(language,&error),"restore language after reference modal"); card.hide();
        expose(history);
        QTimer::singleShot(100,&app,[&] {
            auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()); check(dialog,"actual clear-history confirmation");
            check(host.manager().setLanguage(alternate,&error),"switch inside clear confirmation"); settle(); bound(*dialog);
            check(dialog->text() == localizedText("meterbridge","history.clearConfirm","missing"),"visible clear confirmation body uses active language");
            capture(*dialog,output,QStringLiteral("clear-hot-")+language); dialog->done(QMessageBox::No);
        });
        history.findChild<QAction*>(QStringLiteral("meterHistory.clearAction"))->trigger();
        check(host.manager().setLanguage(language,&error),"restore language after clear confirmation");
        const auto exportPath = isolated.filePath(QStringLiteral("chart-")+language+QStringLiteral(".png"));
        QTimer::singleShot(100,&app,[&] {
            auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget()); check(dialog,"actual PNG QFileDialog");
            dialog->setDirectory(isolated.path()); dialog->selectFile(QFileInfo(exportPath).fileName());
            auto* edit = dialog->findChild<QLineEdit*>(QStringLiteral("fileNameEdit")); check(edit,"actual file name editor"); edit->setSelection(0,3);
            const auto filename = edit->text();
            check(host.manager().setLanguage(alternate,&error),"switch inside PNG chooser"); settle();
            check(edit->text() == filename && edit->selectionStart() == 0,"PNG filename and selection preserved");
            check(dialog->nameFilters().value(0) == localizedText("meterbridge","history.pngFilter","missing"),"PNG filter changes language without changing extension");
            capture(*dialog,output,QStringLiteral("png-chooser-hot-")+language);
            check(QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection),"accept actual PNG chooser");
        });
        history.findChild<QAction*>(QStringLiteral("meterHistory.exportAction"))->trigger();
        check(!QImage(exportPath).isNull(),"actual PNG export writes a readable image");
        check(host.manager().setLanguage(language,&error),"restore language after PNG export"); settle();
        auto* exportLabel = history.findChild<QLabel*>(QStringLiteral("meterHistory.exportStatus"));
        check(exportLabel && exportLabel->text() == localizedText("meterbridge","history.exportSaved","missing").arg(exportPath),"saved PNG path and state relocalize");
        QTimer::singleShot(100,&app,[&] {
            auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()); check(dialog,"actual mixer failure dialog");
            check(host.manager().setLanguage(alternate,&error),"switch inside mixer failure"); settle();
            check(dialog->text() == localizedText("mixer","error.routeCycle","missing"),"legacy canonical model error rendered in active language");
            capture(*dialog,output,QStringLiteral("mixer-error-hot-")+language); dialog->accept();
        });
        MixerErrors::warning(&mixer,"error.topologyTitle","Topology update failed",QStringLiteral("总线路由存在环"));
        check(host.manager().setLanguage(language,&error),"restore language before cold start");
        history.hide(); mixer.hide();
        QProcess child; child.setProcessChannelMode(QProcess::ForwardedChannels);
        child.start(QCoreApplication::applicationFilePath(), {arguments[1], output, QStringLiteral("--cold"),settingsPath,language});
        check(child.waitForFinished(30000) && child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0, "actual cold process succeeds");
    }
    {
        MeterCsvWriter writer;
        MeterCsvWriter::Record record;
        record.instanceId = QStringLiteral("offline-csv"); record.clientName = QStringLiteral("User_Client");
        record.logRootDirectory = isolated.filePath(QStringLiteral("csv")); record.point = provider.rows[0].history[4];
        writer.enqueue(record); check(writer.waitForIdle(5000),"real offline CSV writer drained");
        const auto status = writer.status(record.instanceId);
        check(status.statusKind == MeterCsvStatusKind::recording && !status.activeFilePath.isEmpty(),"writer publishes stable recording status and original file path");
        QFile file(status.activeFilePath); check(file.open(QIODevice::ReadOnly),"actual CSV file readable");
        const auto csvBytes = file.readAll();
        check(csvBytes.startsWith("localTime,elapsedMilliseconds,peakDbfs,rmsDbfs,truePeakDbtp,lfePeakDbfs,momentaryLufs,shortTermLufs,integratedLufs,lraLu\n"),"CSV machine header remains stable English");
        check(csvBytes.contains(",-12.000000,-20.000000,"),"CSV decimal data remains locale-independent");
        writer.closeSession(record.instanceId); check(writer.waitForIdle(5000),"real CSV session closes");
        check(writer.status(record.instanceId).statusKind == MeterCsvStatusKind::sessionClosed,"closed session status uses stable enum");
    }
    std::printf("S4 suite: %d checks; system font=%s; scale=%s PASS\n",checks,QApplication::font().family().toUtf8().constData(),qgetenv("QT_SCALE_FACTOR").constData());
    ThemeManager::installThemeCore(nullptr);
    return 0;
}

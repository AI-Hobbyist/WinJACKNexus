// SPDX-License-Identifier: AGPL-3.0-only
namespace
{
int meterHistoryThemeChecks(ThemeCore &theme, HostI18n &host, Provider &provider,
                            const QString &output)
{
    WindowShowCounter shows;
    qApp->installEventFilter(&shows);
    QString error;
    check(host.manager().setLanguage(QStringLiteral("zh-CN"), &error), "Chinese history labels");
    const auto overrideColor = [&](const char *role, const QColor &color) {
        check(theme.setOverride(QStringLiteral("--wjn-theme-") + QString::fromLatin1(role),
                                color.name(), {}, &error),
              "controlled theme token override");
    };
    overrideColor("global-surface-raised", QColor("#50315f"));
    overrideColor("meter-bridge-surface-page", QColor("#102516"));
    overrideColor("meter-bridge-control-tab-surface-disabled", QColor("#193620"));
    overrideColor("meter-bridge-control-tab-surface-normal", QColor("#24482e"));
    const auto configure = [&](bool second) {
        const QColor panel(second ? "#245744" : "#315424");
        const QColor text("#eff5dd"), border("#89af68");
        for (const auto *role : {"control-preset-surface-normal", "control-action-surface-normal",
                                 "control-number-surface"})
            overrideColor((QStringLiteral("meter-bridge-") + role).toLatin1().constData(), panel);
        for (const auto *role :
             {"control-preset-text-normal", "control-action-text-normal", "control-number-text"})
            overrideColor((QStringLiteral("meter-bridge-") + role).toLatin1().constData(), text);
        for (const auto *role : {"control-preset-border-normal", "control-action-border-normal",
                                 "control-number-border"})
            overrideColor((QStringLiteral("meter-bridge-") + role).toLatin1().constData(), border);
        return panel;
    };
    auto expected = configure(false);
    MeterHistoryWindow history(provider, [] {}, [] {});
    history.setSnapshots(provider.rows);
    history.resize(1280, 820);
    expose(history);
    check(history.devicePixelRatioF() == 1.0, "theme regression uses 100 percent scale");
    const auto verify = [&] {
        for (const auto *name : {"meterHistory.channelSelector", "meterHistory.viewMode",
                                 "meterSettings.silenceThreshold", "meterSettings.silenceDuration",
                                 "meterHistory.timeWindow", "meterHistory.returnButton"})
        {
            auto *widget = history.findChild<QWidget *>(QString::fromLatin1(name));
            check(widget, "affected history control found");
            const auto actual = widget->property("surfaceColor").value<QColor>();
            std::printf("THEME CONTROL %s surface=%s expected=%s\n", name,
                        actual.name().toUtf8().constData(), expected.name().toUtf8().constData());
            check(actual == expected, "painted control follows module surface after polish");
            check(widget->property("borderColor").value<QColor>() == QColor("#89af68"),
                  "painted control follows module border");
        }
        auto *number = history.findChild<QDoubleSpinBox *>("meterSettings.silenceThreshold");
        check(number && number->findChild<QLineEdit *>()->palette().color(QPalette::Text) ==
                            QColor("#eff5dd"),
              "numeric editor follows module text color");
    };
    capture(history, output, QStringLiteral("history-theme-initial"));
    verify();
    expected = configure(true);
    overrideColor("global-surface-raised", QColor("#683756"));
    settle(200);
    verify();
    capture(history, output, QStringLiteral("history-theme-switched"));
    history.hide();
    expose(history);
    verify();
    history.close();
    check(shows.shows <= 100, "bounded real-window theme validation batch");
    qApp->removeEventFilter(&shows);
    std::printf("METER HISTORY THEME PASS checks=%d realWindows=%u platform=windows DPR=1\n",
                checks, shows.shows);
    return 0;
}
}

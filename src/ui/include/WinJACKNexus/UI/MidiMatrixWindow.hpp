// Composition only: public controls/theme; no private drawing or styles.
// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <QGridLayout>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <WinJACKNexus/Common/MidiToolsMatrix.hpp>
#include <WinJACKNexus/UI/Controls.hpp>
#include <functional>
namespace WinJACKNexus::UI
{
class MidiMatrixWindow final : public QWidget
{
    using Routes = Common::MidiTools::MatrixRoutes;
    std::array<Controls::ActionButton *, 256> buttons_{};
    Controls::TextLabel *help_{};
    Controls::TextLabel *input_{};
    Controls::TextLabel *output_{};

  public:
    MidiMatrixWindow(Routes routes, std::function<void(unsigned, unsigned, bool)> edit)
    {
        auto *layout = new QVBoxLayout(this);
        help_ = new Controls::TextLabel(this);
        layout->addWidget(help_);
        auto *grid = new QGridLayout;
        layout->addLayout(grid);
        input_ = new Controls::TextLabel(this);
        output_ = new Controls::TextLabel(this);
        grid->addWidget(input_, 1, 0);
        grid->addWidget(output_, 0, 1, 1, 16);
        for (unsigned c = 0; c < 16; ++c)
        {
            grid->addWidget(new Controls::TextLabel(QString::number(c + 1), this), 1, int(c + 1));
        }
        for (unsigned r = 0; r < 16; ++r)
        {
            grid->addWidget(new Controls::TextLabel(QString::number(r + 1), this), int(r + 2), 0);
            for (unsigned c = 0; c < 16; ++c)
            {
                auto *b =
                    new Controls::ActionButton(QStringLiteral("%1→%2").arg(r + 1).arg(c + 1), this);
                b->setObjectName(QStringLiteral("route-%1-%2").arg(r).arg(c));
                b->setCheckable(true);
                b->setChecked(routes[r] & (1u << c));
                b->setAccessibleName(QStringLiteral("%1→%2").arg(r + 1).arg(c + 1));
                buttons_[r * 16 + c] = b;
                grid->addWidget(b, int(r + 2), int(c + 1));
                connect(b, &QPushButton::toggled, this, [edit, r, c](bool on) { edit(r, c, on); });
            }
        }
        resize(sizeHint());
    }
    void language(const QString &title, const QString &help, const QString &input,
                  const QString &output)
    {
        setWindowTitle(title);
        help_->setText(help);
        input_->setText(input);
        output_->setText(output);
        input_->setMinimumWidth(input_->fontMetrics().horizontalAdvance(input) +
                                input_->contentsMargins().left() +
                                input_->contentsMargins().right());
    }
    void routes(const Routes &value)
    {
        for (unsigned r = 0; r < 16; ++r)
        {
            for (unsigned c = 0; c < 16; ++c)
            {
                QSignalBlocker block(buttons_[r * 16 + c]);
                buttons_[r * 16 + c]->setChecked(value[r] & (1u << c));
            }
        }
    }
};
} // namespace WinJACKNexus::UI

#include <WinJACKNexus/Node/NodeApi.h>

#include <mod_patchbay/PatchbayCanvas.hpp>

#include <QActionGroup>
#include <QMainWindow>
#include <QPointer>
#include <QToolBar>

namespace {

class PatchbayNode final : public WinJACKNexus::Node::INode {
public:
	WinJACKNexus::Node::NodeInfo info() const noexcept override
	{
		return {
			"Patchbay",
			"Patchbay",
			"0.1.0",
			"WinJACKNexus",
			"JACK multi-client patchbay module."
		};
	}

	bool init() noexcept override
	{
		initialized_ = true;
		return true;
	}

	void shutdown() noexcept override
	{
		destroyWindow(nullptr);
		initialized_ = false;
	}

	HWND createWindow(HWND) noexcept override
	{
		if (!initialized_)
			return nullptr;

		if (window_ == nullptr) {
			window_ = new QMainWindow;
			window_->setAttribute(Qt::WA_DeleteOnClose, false);
			window_->setWindowTitle(QStringLiteral("WinJACKNexus - 接线盘"));
			window_->resize(1180, 760);
			PatchbayCanvas::applyTheme(false);

			auto* canvas = new PatchbayCanvas(window_);
			canvas->loadDemoGraph();
			window_->setCentralWidget(canvas);

			auto* toolbar = window_->addToolBar(QStringLiteral("接线盘"));
			toolbar->setMovable(false);
			auto* themes = new QActionGroup(window_);
			themes->setExclusive(true);
			auto* darkAction = toolbar->addAction(QStringLiteral("深色"));
			darkAction->setCheckable(true);
			darkAction->setChecked(true);
			themes->addAction(darkAction);
			auto* lightAction = toolbar->addAction(QStringLiteral("浅色"));
			lightAction->setCheckable(true);
			themes->addAction(lightAction);
			QObject::connect(darkAction, &QAction::triggered, window_, [] {
				PatchbayCanvas::applyTheme(false);
			});
			QObject::connect(lightAction, &QAction::triggered, window_, [] {
				PatchbayCanvas::applyTheme(true);
			});
		}

		window_->showNormal();
		window_->raise();
		window_->activateWindow();
		return reinterpret_cast<HWND>(static_cast<quintptr>(window_->winId()));
	}

	void destroyWindow(HWND) noexcept override
	{
		if (window_ != nullptr) {
			delete window_;
			window_ = nullptr;
		}
	}

private:
	bool initialized_ = false;
	QPointer<QMainWindow> window_;
};

}

const char* wjn_get_node_api_version(void)
{
	return "1";
}

const char* wjn_get_node_name(void)
{
	return "Patchbay";
}

const char* wjn_get_node_category(void)
{
	return "Patchbay";
}

WinJACKNexus::Node::INode* wjn_create_node(void)
{
	return new PatchbayNode;
}

void wjn_destroy_node(WinJACKNexus::Node::INode* node)
{
	delete node;
}
#include "MoreLessIntegerAction.h"

#include <QAction>
#include <QHBoxLayout>
#include <QLabel>
#include <QWidget>

MoreLessIntegerAction::MoreLessIntegerAction(QObject* parent, const QString& title) :
    mv::gui::IntegralAction(parent, title, 0, 0, 0)
{
    setConfigurationFlag(mv::gui::WidgetAction::ConfigurationFlag::NoLabelInGroup);
    setDefaultWidgetFlags(mv::gui::IntegralAction::Slider);
}

QWidget* MoreLessIntegerAction::getWidget(QWidget* parent, const std::int32_t&)
{
    auto* widget = new QWidget(parent);
    auto* layout = new QHBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* moreLabel = new QLabel(tr("More"), widget);
    auto* lessLabel = new QLabel(tr("Less"), widget);
    layout->addWidget(moreLabel);
    layout->addWidget(mv::gui::IntegralAction::getWidget(widget, mv::gui::IntegralAction::Slider), 1);
    layout->addWidget(lessLabel);
    const auto updateLabelEnabledState = [this, moreLabel, lessLabel] {
        moreLabel->setEnabled(isEnabled());
        lessLabel->setEnabled(isEnabled());
    };
    connect(this, &QAction::changed, widget, updateLabelEnabledState);
    updateLabelEnabledState();
    return widget;
}

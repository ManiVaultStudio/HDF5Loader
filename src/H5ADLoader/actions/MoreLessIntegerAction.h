#pragma once

#include <actions/IntegralAction.h>

/** @brief Integer slider action with More/Less endpoint labels. */
class MoreLessIntegerAction final : public mv::gui::IntegralAction
{
public:
    explicit MoreLessIntegerAction(QObject* parent, const QString& title);

protected:
    QWidget* getWidget(QWidget* parent, const std::int32_t& widgetFlags) override;
};

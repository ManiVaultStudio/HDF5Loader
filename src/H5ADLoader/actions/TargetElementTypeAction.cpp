#include "TargetElementTypeAction.h"

TargetElementTypeAction::TargetElementTypeAction(QObject* parent, const QString& title) :
    mv::gui::HorizontalGroupAction(parent, title),
    _typeAction(this, "Type"),
    _estimateAction(this, "Estimate")
{
    setShowLabels(false);
    _typeAction.setDefaultWidgetFlags(mv::gui::OptionAction::ComboBox);
    _estimateAction.setIconByName("wand-magic-sparkles");
    _estimateAction.setToolTip(tr("Estimate a compact target type from a bounded sample of the matrix"));
    addAction(&_typeAction);
    addAction(&_estimateAction, mv::gui::TriggerAction::Text);
    connect(&_estimateAction, &mv::gui::TriggerAction::triggered, this, &TargetElementTypeAction::estimateRequested);
}

mv::gui::OptionAction& TargetElementTypeAction::getTypeAction()
{
    return _typeAction;
}

const mv::gui::OptionAction& TargetElementTypeAction::getTypeAction() const
{
    return _typeAction;
}

void TargetElementTypeAction::setAnalysisRunning(const bool running, const bool estimating)
{
    _typeAction.setEnabled(!running);
    _estimateAction.setEnabled(!running);
    _estimateAction.setText(running && estimating ? tr("Estimating...") : tr("Estimate"));
}

void TargetElementTypeAction::setRecommendedTypeIndex(const int index)
{
    if (index >= 0 && index < static_cast<int>(_typeAction.getNumberOfOptions()))
        _typeAction.setCurrentIndex(index);
}

#pragma once

#include <actions/HorizontalGroupAction.h>
#include <actions/OptionAction.h>
#include <actions/TriggerAction.h>

/**
 * @brief Combines element-type selection with an estimate request.
 *
 * Estimation is performed by the import dialog's workflow; this action only
 * exposes the controls and emits a request signal.
 */
class TargetElementTypeAction : public mv::gui::HorizontalGroupAction
{
    Q_OBJECT

public:
    TargetElementTypeAction(QObject* parent, const QString& title);

    mv::gui::OptionAction& getTypeAction();
    const mv::gui::OptionAction& getTypeAction() const;
    /** Disables the controls during analysis and updates the estimate-button label. */
    void setAnalysisRunning(bool running, bool estimating = false);
    /** Selects the recommended option when index is within the available choices. */
    void setRecommendedTypeIndex(int index);

signals:
    /** Emitted when the user requests a bounded estimate; the dialog owns the scan workflow. */
    void estimateRequested();

private:
    mv::gui::OptionAction  _typeAction;      /**< Target element-type selector. */
    mv::gui::TriggerAction _estimateAction;  /**< Requests analysis of the matrix values. */
};

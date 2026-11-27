#pragma once

#include <actions/WidgetAction.h>

#include <cstdint>
#include <memory>
#include <vector>

class H5ADDimensionTableModel;

/** @brief Provides an HDF5-loader-owned, table-based dimension selector. */
class DimensionSelectionAction : public mv::gui::WidgetAction
{
    Q_OBJECT

public:
    explicit DimensionSelectionAction(QObject* parent);
    ~DimensionSelectionAction() override;

    /** Sets HDF5 dimension names and restores selected source-column indices. */
    void setDimensions(std::uint32_t dimensionCount, const std::vector<QString>& dimensionNames, const std::vector<std::int32_t>& selectedDimensions = {});

    /** Returns selected source-column indices in their original matrix order. */
    std::vector<std::int32_t> selectedDimensions() const;

    /** Restores selected source-column indices. Invalid indices are ignored. */
    void selectDimensions(const std::vector<std::int32_t>& selectedDimensions);

signals:
    /** Emitted when an included dimension changes. */
    void selectedDimensionsChanged();

protected:
    /** Creates the HDF5 dimension table widget. */
    QWidget* getWidget(QWidget* parent, const std::int32_t& widgetFlags) override;

private:
    std::unique_ptr<H5ADDimensionTableModel> _model; /**< Standalone Qt model for HDF5 dimension rows. */
};

#pragma once

#include "../H5ADDimensionStatistics.h"

#include <actions/WidgetAction.h>

#include <QPointer>

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

    /** Updates the statistics cells; an unavailable entry is displayed as a dash. */
    void setStatistics(const std::vector<H5ADDimensionStatistics>& statistics);

    /** Returns the sorted, distinct standard deviations available for filtering. */
    std::vector<double> distinctStandardDeviations() const;

    /** Sets the lower standard-deviation threshold in every active table view. */
    void setMinimumStandardDeviation(double minimumStandardDeviation);

signals:
    /** Emitted when an included dimension changes. */
    void selectedDimensionsChanged();

    /** Emitted when full statistics become available for slider filtering. */
    void statisticsChanged();

protected:
    /** Creates the HDF5 dimension table widget. */
    QWidget* getWidget(QWidget* parent, const std::int32_t& widgetFlags) override;

private:
    std::unique_ptr<H5ADDimensionTableModel> _model; /**< Standalone Qt model for HDF5 dimension rows. */
    std::vector<QPointer<QObject>> _filterModels; /**< Proxy models hosted by dimension table views. */
};

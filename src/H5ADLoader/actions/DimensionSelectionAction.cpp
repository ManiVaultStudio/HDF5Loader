#include "DimensionSelectionAction.h"

#include <actions/WidgetActionWidget.h>

#include <QAbstractTableModel>
#include <QAction>
#include <QMenu>
#include <QHeaderView>
#include <QPointer>
#include <QSortFilterProxyModel>
#include <QSizePolicy>
#include <QTableView>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

class H5ADDimensionTableModel final : public QAbstractTableModel
{
public:
    struct Row
    {
        std::int32_t sourceIndex = -1;
        QString name;
        bool selected = true;
        H5ADDimensionStatistics statistics;
    };

    explicit H5ADDimensionTableModel(QObject* parent = nullptr) : QAbstractTableModel(parent) {}

    void setDimensions(const std::uint32_t dimensionCount, const std::vector<QString>& names, const std::vector<std::int32_t>& selectedDimensions)
    {
        const std::unordered_set<std::int32_t> selected(selectedDimensions.cbegin(), selectedDimensions.cend());
        beginResetModel();
        _rows.clear();
        _rows.reserve(dimensionCount);
        for (std::uint32_t index = 0; index < dimensionCount; ++index)
        {
            const auto sourceIndex = static_cast<std::int32_t>(index);
            const auto hasStoredSelection = !selectedDimensions.empty();
            const auto name = index < names.size() ? names[index] : QStringLiteral("Dimension %1").arg(index);
            _rows.push_back({ sourceIndex, name, !hasStoredSelection || selected.count(sourceIndex) != 0, {} });
        }
        endResetModel();
    }

    std::vector<std::int32_t> selectedDimensions() const
    {
        std::vector<std::int32_t> selected;
        selected.reserve(_rows.size());
        for (const auto& row : _rows)
            if (row.selected)
                selected.push_back(row.sourceIndex);
        return selected;
    }

    void selectDimensions(const std::vector<std::int32_t>& selectedDimensions)
    {
        const std::unordered_set<std::int32_t> selected(selectedDimensions.cbegin(), selectedDimensions.cend());
        bool changed = false;
        for (auto& row : _rows)
        {
            const auto selectedNow = selected.count(row.sourceIndex) != 0;
            changed = changed || row.selected != selectedNow;
            row.selected = selectedNow;
        }
        if (changed && !_rows.empty())
            emit dataChanged(index(0, 0), index(static_cast<int>(_rows.size() - 1), 0), { Qt::CheckStateRole });
    }

    void setStatistics(const std::vector<H5ADDimensionStatistics>& statistics)
    {
        const auto count = std::min(_rows.size(), statistics.size());
        for (std::size_t row = 0; row < count; ++row)
            _rows[row].statistics = statistics[row];
        if (count > 0)
        {
            emit dataChanged(index(0, 3), index(static_cast<int>(count - 1), 8), { Qt::DisplayRole, Qt::UserRole + 1 });
            emit headerDataChanged(Qt::Horizontal, 3, 8);
        }
    }

    std::vector<double> distinctStandardDeviations() const
    {
        std::vector<double> values;
        values.reserve(_rows.size());
        for (const auto& row : _rows)
            if (row.statistics.available && row.statistics.finiteValueCount > 0 && !std::isnan(row.statistics.standardDeviation))
                values.push_back(row.statistics.standardDeviation);
        std::sort(values.begin(), values.end());
        values.erase(std::unique(values.begin(), values.end()), values.end());
        return values;
    }

    int rowCount(const QModelIndex& parent = {}) const override
    {
        return parent.isValid() ? 0 : static_cast<int>(_rows.size());
    }

    int columnCount(const QModelIndex& parent = {}) const override
    {
        return parent.isValid() ? 0 : 9;
    }

    QVariant data(const QModelIndex& index, const int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || index.row() < 0 || static_cast<std::size_t>(index.row()) >= _rows.size())
            return {};

        const auto& row = _rows[static_cast<std::size_t>(index.row())];
        if (role == Qt::CheckStateRole && index.column() == 0)
            return row.selected ? Qt::Checked : Qt::Unchecked;
        if (role == Qt::DisplayRole)
        {
            if (index.column() == 1)
                return row.sourceIndex;
            if (index.column() == 2)
                return row.name;
            if (index.column() >= 3)
            {
                if (!row.statistics.available)
                    return QStringLiteral("—");
                if (index.column() != 5 && index.column() != 8 && row.statistics.finiteValueCount == 0)
                    return QStringLiteral("—");
                QString value;
                switch (index.column())
                {
                    case 3: value = QString::number(row.statistics.mean, 'g', 6); break;
                    case 4: value = QString::number(row.statistics.standardDeviation, 'g', 6); break;
                    case 5: value = QStringLiteral("%1%").arg(row.statistics.zeroFraction * 100.0, 0, 'f', 1); break;
                    case 6: value = QString::number(row.statistics.minimum, 'g', 6); break;
                    case 7: value = QString::number(row.statistics.maximum, 'g', 6); break;
                    case 8: value = QString::number(row.statistics.nanCount); break;
                }
                if (!row.statistics.exact)
                    value.prepend(QLatin1Char('~'));
                return value;
            }
        }
        if (role == Qt::UserRole + 1)
        {
            if (!row.statistics.available)
                return {};
            if (index.column() >= 3 && index.column() != 5 && index.column() != 8 && row.statistics.finiteValueCount == 0)
                return {};
            switch (index.column())
            {
                case 1: return row.sourceIndex;
                case 2: return row.name;
                case 3: return row.statistics.mean;
                case 4: return row.statistics.standardDeviation;
                case 5: return row.statistics.zeroFraction;
                case 6: return row.statistics.minimum;
                case 7: return row.statistics.maximum;
                case 8: return QVariant::fromValue<qulonglong>(row.statistics.nanCount);
                default: return {};
            }
        }
        if (role == Qt::TextAlignmentRole && index.column() >= 1 && index.column() != 2)
            return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
        return {};
    }

    QVariant headerData(const int section, const Qt::Orientation orientation, const int role = Qt::DisplayRole) const override
    {
        if (orientation != Qt::Horizontal)
            return {};
        if (role == Qt::ToolTipRole)
        {
            switch (section)
            {
                case 0: return tr("Include dimension in the imported dataset.");
                case 3: return tr("Mean across finite matrix values in this dimension. Values prefixed with ~ are estimates from a bounded sample.");
                case 4: return tr("Population standard deviation across finite matrix values. Values prefixed with ~ are estimates from a bounded sample.");
                case 5: return tr("Percentage of observations whose value is zero; implicit sparse zeros are included. Values prefixed with ~ are estimates from a bounded sample.");
                case 6: return tr("Minimum finite matrix value. Values prefixed with ~ are estimates from a bounded sample.");
                case 7: return tr("Maximum finite matrix value. Values prefixed with ~ are estimates from a bounded sample.");
                case 8: return tr("Number of NaN values. Infinite values are not counted as NaN. Values prefixed with ~ are estimates from a bounded sample.");
                default: return {};
            }
        }
        if (role != Qt::DisplayRole)
            return {};
        switch (section)
        {
            case 0: return QString();
            case 1: return tr("ID");
            case 2: return tr("Dimension");
            case 3: return tr("Mean");
            case 4: return tr("Std. dev.");
            case 5: return tr("Zero %");
            case 6: return tr("Min");
            case 7: return tr("Max");
            case 8: return tr("NaN");
        }
        return {};
    }

    Qt::ItemFlags flags(const QModelIndex& index) const override
    {
        auto itemFlags = QAbstractTableModel::flags(index);
        if (index.isValid() && index.column() == 0)
            itemFlags |= Qt::ItemIsUserCheckable;
        return itemFlags;
    }

    bool setData(const QModelIndex& index, const QVariant& value, const int role = Qt::EditRole) override
    {
        if (role != Qt::CheckStateRole || !index.isValid() || index.column() != 0 || index.row() < 0 || static_cast<std::size_t>(index.row()) >= _rows.size())
            return false;
        auto& row = _rows[static_cast<std::size_t>(index.row())];
        const auto selected = value.toInt() == Qt::Checked;
        if (row.selected == selected)
            return true;
        row.selected = selected;
        emit dataChanged(index, index, { Qt::CheckStateRole });
        return true;
    }

private:
    std::vector<Row> _rows;
};

class H5ADDimensionFilterProxyModel final : public QSortFilterProxyModel
{
public:
    explicit H5ADDimensionFilterProxyModel(QObject* parent = nullptr) : QSortFilterProxyModel(parent) {}

    void setMinimumStandardDeviation(const double minimumStandardDeviation)
    {
        if (_minimumStandardDeviation == minimumStandardDeviation)
            return;
        _minimumStandardDeviation = minimumStandardDeviation;
        invalidateFilter();
    }

protected:
    bool filterAcceptsRow(const int sourceRow, const QModelIndex& sourceParent) const override
    {
        if (_minimumStandardDeviation == -std::numeric_limits<double>::infinity())
            return true;

        const auto standardDeviationIndex = sourceModel()->index(sourceRow, 4, sourceParent);
        const auto standardDeviation = standardDeviationIndex.data(Qt::UserRole + 1);
        return standardDeviation.isValid() && standardDeviation.toDouble() >= _minimumStandardDeviation;
    }

private:
    double _minimumStandardDeviation = -std::numeric_limits<double>::infinity();
};

DimensionSelectionAction::DimensionSelectionAction(QObject* parent) :
    mv::gui::WidgetAction(parent, "HDF5 dimension table"),
    _model(std::make_unique<H5ADDimensionTableModel>())
{
    setConfigurationFlag(mv::gui::WidgetAction::ConfigurationFlag::NoLabelInGroup);
    connect(_model.get(), &QAbstractItemModel::dataChanged, this, [this](const QModelIndex& topLeft, const QModelIndex& bottomRight, const QList<int>&) {
        if (topLeft.column() == 0 && bottomRight.column() == 0)
            emit selectedDimensionsChanged();
    });
}

DimensionSelectionAction::~DimensionSelectionAction() = default;

void DimensionSelectionAction::setDimensions(const std::uint32_t dimensionCount, const std::vector<QString>& dimensionNames, const std::vector<std::int32_t>& selectedDimensions)
{
    _model->setDimensions(dimensionCount, dimensionNames, selectedDimensions);
}

std::vector<std::int32_t> DimensionSelectionAction::selectedDimensions() const
{
    return _model->selectedDimensions();
}

void DimensionSelectionAction::setStatistics(const std::vector<H5ADDimensionStatistics>& statistics)
{
    _model->setStatistics(statistics);
    emit statisticsChanged();
}

std::vector<double> DimensionSelectionAction::distinctStandardDeviations() const
{
	return _model->distinctStandardDeviations();
}

void DimensionSelectionAction::setMinimumStandardDeviation(const double minimumStandardDeviation)
{
	for (auto iterator = _filterModels.begin(); iterator != _filterModels.end();)
	{
		if (auto* filterModel = dynamic_cast<H5ADDimensionFilterProxyModel*>(iterator->data()))
		{
			filterModel->setMinimumStandardDeviation(minimumStandardDeviation);
			++iterator;
		}
		else
			iterator = _filterModels.erase(iterator);
	}
}

void DimensionSelectionAction::selectDimensions(const std::vector<std::int32_t>& selectedDimensions)
{
    _model->selectDimensions(selectedDimensions);
}

QWidget* DimensionSelectionAction::getWidget(QWidget* parent, const std::int32_t& widgetFlags)
{
    auto* widget = new mv::gui::WidgetActionWidget(parent, this, widgetFlags);

    auto* table = new QTableView(widget);
    table->setObjectName(QStringLiteral("HDF5DimensionTableView"));
    auto* sortModel = new H5ADDimensionFilterProxyModel(table);
	_filterModels.emplace_back(sortModel);
    sortModel->setSourceModel(_model.get());
    sortModel->setSortRole(Qt::UserRole + 1);
    sortModel->setDynamicSortFilter(true);
    table->setModel(sortModel);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setAlternatingRowColors(true);
    table->setCornerButtonEnabled(false);
    table->setSortingEnabled(true);
    table->verticalHeader()->setVisible(false);
    table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    table->verticalHeader()->setDefaultSectionSize(22);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    table->setColumnWidth(0, 28);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    for (int column = 3; column < 9; ++column)
        table->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    table->sortByColumn(4, Qt::DescendingOrder);
    table->setColumnHidden(6, true);
    table->setColumnHidden(7, true);
    table->setColumnHidden(8, true);

    auto* header = table->horizontalHeader();
    header->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(header, &QWidget::customContextMenuRequested, table, [table, header](const QPoint& position) {
        const QStringList columnNames = {
            tr("Include"), tr("ID"), tr("Dimension"), tr("Mean"), tr("Std. dev."),
            tr("Zero %"), tr("Min"), tr("Max"), tr("NaN")
        };
        QMenu menu(table);
        for (int column = 0; column < columnNames.size(); ++column)
        {
            auto* action = menu.addAction(columnNames[column]);
            action->setCheckable(true);
            action->setChecked(!table->isColumnHidden(column));
            connect(action, &QAction::toggled, table, [table, column](const bool visible) {
                table->setColumnHidden(column, !visible);
            });
        }
        menu.exec(header->mapToGlobal(position));
    });
    table->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	widget->setLayout(new QVBoxLayout());
	widget->layout()->setContentsMargins(0, 0, 0, 0);
	widget->layout()->addWidget(table);
    return widget;
}

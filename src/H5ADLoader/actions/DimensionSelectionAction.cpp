#include "DimensionSelectionAction.h"

#include <actions/WidgetActionWidget.h>

#include <QAbstractTableModel>
#include <QHeaderView>
#include <QSizePolicy>
#include <QTableView>
#include <QVBoxLayout>

#include <unordered_set>

class H5ADDimensionTableModel final : public QAbstractTableModel
{
public:
    struct Row
    {
        std::int32_t sourceIndex = -1;
        QString name;
        bool selected = true;
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
            _rows.push_back({ sourceIndex, name, !hasStoredSelection || selected.count(sourceIndex) != 0 });
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

    int rowCount(const QModelIndex& parent = {}) const override
    {
        return parent.isValid() ? 0 : static_cast<int>(_rows.size());
    }

    int columnCount(const QModelIndex& parent = {}) const override
    {
        return parent.isValid() ? 0 : 2;
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
            if (index.column() == 0)
                return row.sourceIndex;
            if (index.column() == 1)
                return row.name;
        }
        if (role == Qt::UserRole && index.column() == 0)
            return row.sourceIndex;
        return {};
    }

    QVariant headerData(const int section, const Qt::Orientation orientation, const int role = Qt::DisplayRole) const override
    {
        if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
            return {};
        if (section == 0)
            return tr("Source index");
        if (section == 1)
            return tr("Dimension");
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

DimensionSelectionAction::DimensionSelectionAction(QObject* parent) :
    mv::gui::WidgetAction(parent, "HDF5 dimension table"),
    _model(std::make_unique<H5ADDimensionTableModel>())
{
    setConfigurationFlag(mv::gui::WidgetAction::ConfigurationFlag::NoLabelInGroup);
    connect(_model.get(), &QAbstractItemModel::dataChanged, this, [this] { emit selectedDimensionsChanged(); });
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

void DimensionSelectionAction::selectDimensions(const std::vector<std::int32_t>& selectedDimensions)
{
    _model->selectDimensions(selectedDimensions);
}

QWidget* DimensionSelectionAction::getWidget(QWidget* parent, const std::int32_t& widgetFlags)
{
    auto* widget = new mv::gui::WidgetActionWidget(parent, this, widgetFlags);
    auto* layout = new QVBoxLayout();
    layout->setContentsMargins(0, 0, 0, 0);

    auto* table = new QTableView(widget);
    table->setObjectName(QStringLiteral("HDF5DimensionTableView"));
    table->setModel(_model.get());
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setAlternatingRowColors(true);
    table->setCornerButtonEnabled(false);
    table->setSortingEnabled(false);
    table->verticalHeader()->setVisible(false);
    table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    table->verticalHeader()->setDefaultSectionSize(22);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    layout->addWidget(table);
    widget->setLayout(layout);
    return widget;
}

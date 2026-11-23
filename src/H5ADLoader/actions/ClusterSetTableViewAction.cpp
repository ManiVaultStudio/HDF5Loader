#include "ClusterSetTableViewAction.h"

#include <actions/WidgetActionWidget.h>

#include <QAbstractTableModel>
#include <QHeaderView>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QVBoxLayout>

#include <vector>

class ClusterSetSelectionModel final : public QAbstractTableModel
{
public:
    struct Row
    {
        QString name;
        H5ADClusterSetInfo info;
        bool selected = false;
    };

    explicit ClusterSetSelectionModel(QObject* parent = nullptr) :
        QAbstractTableModel(parent)
    {
    }

    void setClusterSets(const QMap<QString, H5ADClusterSetInfo>& clusterSets, const QStringList& selectedClusterSets)
    {
        beginResetModel();
        _rows.clear();
        _rows.reserve(static_cast<std::size_t>(clusterSets.size()));
        for (auto it = clusterSets.cbegin(); it != clusterSets.cend(); ++it)
            _rows.push_back({ it.key(), it.value(), selectedClusterSets.contains(it.key()) });
        endResetModel();
    }

    QStringList selectedClusterSets() const
    {
        QStringList selected;
        for (const auto& row : _rows)
            if (row.selected)
                selected.append(row.name);
        return selected;
    }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : static_cast<int>(_rows.size());
    }

    int columnCount(const QModelIndex& parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : 3;
    }

    QVariant data(const QModelIndex& index, const int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || index.row() < 0 || static_cast<std::size_t>(index.row()) >= _rows.size())
            return {};

        const auto& row = _rows[static_cast<std::size_t>(index.row())];
        if (role == Qt::UserRole + 1)
        {
            if (index.column() == 1)
                return row.name;
            if (index.column() == 2 && row.info.clusterCountAvailable)
                return QVariant::fromValue<qulonglong>(row.info.clusterCount);
            return {};
        }

        if (role == Qt::CheckStateRole && index.column() == 0)
            return row.selected ? Qt::Checked : Qt::Unchecked;

        if (role == Qt::DisplayRole)
        {
            if (index.column() == 1)
                return row.name;
            if (index.column() == 2)
                return row.info.clusterCountAvailable ? QString::number(row.info.clusterCount) : QStringLiteral("—");
        }

        if (role == Qt::ToolTipRole)
            return row.info.description;

        if (role == Qt::TextAlignmentRole && index.column() == 2)
            return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);

        return {};
    }

    QVariant headerData(const int section, const Qt::Orientation orientation, const int role = Qt::DisplayRole) const override
    {
        if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
            return {};

        switch (section)
        {
            case 0: return tr("Import");
            case 1: return tr("Cluster set");
            case 2: return tr("Clusters");
            default: return {};
        }
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

    void setAllSelected(const bool selected)
    {
        updateSelection([selected](const Row&) { return selected; });
    }

    void invertSelection()
    {
        updateSelection([](const Row& row) { return !row.selected; });
    }

private:
    template<typename SelectionFunction>
    void updateSelection(SelectionFunction selectionFunction)
    {
        bool changed = false;
        for (auto& row : _rows)
        {
            const auto selected = selectionFunction(row);
            changed = changed || row.selected != selected;
            row.selected = selected;
        }

        if (!changed || _rows.empty())
            return;

        const auto lastRow = static_cast<int>(_rows.size() - 1);
        emit dataChanged(index(0, 0), index(lastRow, 0), { Qt::CheckStateRole });
    }

    std::vector<Row> _rows;
};

ClusterSetTableViewAction::ClusterSetTableViewAction(QObject* parent) :
    mv::gui::WidgetAction(parent, "Cluster set table"),
    _model(std::make_unique<ClusterSetSelectionModel>())
{
    setConfigurationFlag(mv::gui::WidgetAction::ConfigurationFlag::NoLabelInGroup);
    connect(_model.get(), &QAbstractItemModel::dataChanged, this, [this] { emit selectionChanged(); });
}

ClusterSetTableViewAction::~ClusterSetTableViewAction() = default;

void ClusterSetTableViewAction::setClusterSets(const QMap<QString, H5ADClusterSetInfo>& clusterSets, const QStringList& selectedClusterSets)
{
    _model->setClusterSets(clusterSets, selectedClusterSets);
}

QStringList ClusterSetTableViewAction::selectedClusterSets() const
{
    return _model->selectedClusterSets();
}

void ClusterSetTableViewAction::selectAll()
{
    _model->setAllSelected(true);
}

void ClusterSetTableViewAction::selectNone()
{
    _model->setAllSelected(false);
}

void ClusterSetTableViewAction::invertSelection()
{
    _model->invertSelection();
}

void ClusterSetTableViewAction::setSearchText(const QString& searchText)
{
    _searchText = searchText;
    if (_filterModel)
        _filterModel->setFilterFixedString(_searchText);
}

QWidget* ClusterSetTableViewAction::getWidget(QWidget* parent, const std::int32_t& widgetFlags)
{
    auto* widget = new mv::gui::WidgetActionWidget(parent, this, widgetFlags);
    auto* layout = new QVBoxLayout();
    layout->setContentsMargins(0, 0, 0, 0);

    auto* table = new QTableView(widget);
    table->setObjectName(QStringLiteral("ClusterSetTableView"));
    auto* filterModel = new QSortFilterProxyModel(table);
    filterModel->setSourceModel(_model.get());
    filterModel->setFilterKeyColumn(1);
    filterModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
    filterModel->setSortRole(Qt::UserRole + 1);
    filterModel->setFilterFixedString(_searchText);
    _filterModel = filterModel;
    table->setModel(filterModel);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setAlternatingRowColors(true);
    table->setCornerButtonEnabled(false);
    table->setSortingEnabled(true);
    table->verticalHeader()->setVisible(false);
    table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    table->verticalHeader()->setDefaultSectionSize(22);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table->setColumnWidth(0, 55);
    table->setFixedHeight(280);
    table->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    table->sortByColumn(1, Qt::AscendingOrder);

    layout->addWidget(table);
    widget->setLayout(layout);
    return widget;
}

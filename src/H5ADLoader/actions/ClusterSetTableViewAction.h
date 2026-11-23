#pragma once

#include "../H5ADClusterSetInfo.h"

#include <actions/WidgetAction.h>

#include <QMap>
#include <QPointer>
#include <QSortFilterProxyModel>
#include <QStringList>

#include <memory>

class ClusterSetSelectionModel;

/** @brief Displays selectable observation cluster sets in a sortable table. */
class ClusterSetTableViewAction : public mv::gui::WidgetAction
{
    Q_OBJECT

public:
    explicit ClusterSetTableViewAction(QObject* parent);
    ~ClusterSetTableViewAction() override;

    /** Rebuilds the rows and restores the current selection. */
    void setClusterSets(const QMap<QString, H5ADClusterSetInfo>& clusterSets, const QStringList& selectedClusterSets);

    /** Returns names of cluster sets enabled for import. */
    QStringList selectedClusterSets() const;

    /** Selects every cluster set, including rows hidden by search. */
    void selectAll();

    /** Clears every cluster-set selection, including rows hidden by search. */
    void selectNone();

    /** Inverts every cluster-set selection, including rows hidden by search. */
    void invertSelection();

    /** Sets the case-insensitive name filter and applies it to any live table. */
    void setSearchText(const QString& searchText);

signals:
    /** Emitted when a cluster set is enabled or disabled. */
    void selectionChanged();

protected:
    /** Creates the sortable table view used by this action. */
    QWidget* getWidget(QWidget* parent, const std::int32_t& widgetFlags) override;

private:
    std::unique_ptr<ClusterSetSelectionModel> _model; /**< Source model backing the table. */
    QPointer<QSortFilterProxyModel> _filterModel; /**< Transient search and sort model attached to the current view. */
    QString _searchText; /**< Search filter retained while the dialog's table widget is recreated. */
};

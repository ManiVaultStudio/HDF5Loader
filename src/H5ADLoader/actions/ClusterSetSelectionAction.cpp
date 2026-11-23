#include "ClusterSetSelectionAction.h"

#include <util/StyledIcon.h>

ClusterSetSelectionAction::ClusterSetSelectionAction(QObject* parent) :
    mv::gui::VerticalGroupAction(parent, "Cluster set selection", true),
    _toolbarAction(this, "Cluster set selection controls"),
    _searchAction(&_toolbarAction, "Search cluster sets"),
    _selectAllAction(&_toolbarAction, "Select all"),
    _selectNoneAction(&_toolbarAction, "Select none"),
    _invertSelectionAction(&_toolbarAction, "Invert selection"),
    _tableViewAction(this)
{
    setShowLabels(false);
    _toolbarAction.setShowLabels(false);
    _toolbarAction.setDefaultWidgetFlag(mv::gui::GroupAction::NoMargins);
    _searchAction.setDefaultWidgetFlags(mv::gui::StringAction::LineEdit);
    _searchAction.setStretch(1);
    _searchAction.setPlaceHolderString(tr("Search cluster sets…"));
    _searchAction.getLeadingAction().setIcon(mv::util::StyledIcon("magnifying-glass"));
    _searchAction.setSearchMode(true);
    _selectAllAction.setToolTip(tr("Select every cluster set, including rows hidden by search"));
    _selectNoneAction.setToolTip(tr("Clear every cluster-set selection, including rows hidden by search"));
    _invertSelectionAction.setToolTip(tr("Invert every cluster-set selection, including rows hidden by search"));

    _toolbarAction.addAction(&_searchAction, mv::gui::StringAction::LineEdit);
    _toolbarAction.addAction(&_selectAllAction, mv::gui::TriggerAction::Text);
    _toolbarAction.addAction(&_selectNoneAction, mv::gui::TriggerAction::Text);
    _toolbarAction.addAction(&_invertSelectionAction, mv::gui::TriggerAction::Text);

    addAction(&_toolbarAction);
    addAction(&_tableViewAction);

    connect(&_searchAction, &mv::gui::StringAction::stringChanged, this, [this] {
        _tableViewAction.setSearchText(_searchAction.getString());
    });
    connect(&_tableViewAction, &ClusterSetTableViewAction::selectionChanged, this, &ClusterSetSelectionAction::selectionChanged);
    connect(&_selectAllAction, &mv::gui::TriggerAction::triggered, this, &ClusterSetSelectionAction::selectAll);
    connect(&_selectNoneAction, &mv::gui::TriggerAction::triggered, this, &ClusterSetSelectionAction::selectNone);
    connect(&_invertSelectionAction, &mv::gui::TriggerAction::triggered, this, &ClusterSetSelectionAction::invertSelection);
}

void ClusterSetSelectionAction::setClusterSets(const QMap<QString, H5ADClusterSetInfo>& clusterSets, const QStringList& selectedClusterSets)
{
    _tableViewAction.setClusterSets(clusterSets, selectedClusterSets);
}

QStringList ClusterSetSelectionAction::selectedClusterSets() const
{
    return _tableViewAction.selectedClusterSets();
}

void ClusterSetSelectionAction::selectAll()
{
    _tableViewAction.selectAll();
}

void ClusterSetSelectionAction::selectNone()
{
    _tableViewAction.selectNone();
}

void ClusterSetSelectionAction::invertSelection()
{
    _tableViewAction.invertSelection();
}

#pragma once

#include <QSize>

namespace ads
{
class CDockManager;
class CDockWidget;
}

namespace DockLayoutPolicy
{
// Keep the primary editor as a stable workbench part. Side panels may resize
// around it, but they must not make the chart interaction surface unusable or
// accept the workspace into a tab/vertical tool stack.
QSize primaryWorkspaceMinimumSize();
void applyPrimaryWorkspaceDockPolicy(ads::CDockWidget *dock);

// Keep small tool sections content-sized while allowing normal editor panels
// in the same vertical stack to absorb space released by a closed section.
// Floating tools deliberately regain unconstrained sizing.
void applyCompactToolDockPolicy(ads::CDockWidget *dock);
void refreshCompactToolDockPolicies(ads::CDockManager *manager);
}

#pragma once

namespace ads
{
class CDockWidget;
}

namespace DockLayoutPolicy
{
// Keep small tool sections content-sized while allowing normal editor panels
// in the same vertical stack to absorb space released by a closed section.
void applyCompactToolDockPolicy(ads::CDockWidget *dock);
}

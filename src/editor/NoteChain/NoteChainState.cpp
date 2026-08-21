// NoteChainState.cpp - runtime state (based on Python state.py)
#include "NoteChainState.h"
#include <algorithm>
namespace NoteChain {

NoteChainState::NoteChainState()
{
    m_activeShape = QStringLiteral("curve");
    m_style.name = QStringLiteral("balanced");
    m_style.denominators = {4, 8, 12, 16};
}

int NoteChainState::appendAnchor(double lx,double bt){Anchor a;a.id=m_nextAnchorId++;a.laneX=ncClamp(lx,0.0,Const::kLaneWidth);a.beat=qMax(0.0,bt);m_anchors.append(a);m_cacheValid=false;return m_anchors.size()-1;}

void NoteChainState::removeAnchorById(int id){int idx=anchorIndexById(id);if(idx<0)return;QVector<LinkKey> dead;for(auto&l:m_links)if(l.from==id||l.to==id)dead.append(l.key());for(auto&k:dead){m_segDen.remove(k);m_segShape.remove(k);m_densMode.remove(k);}m_links.erase(std::remove_if(m_links.begin(),m_links.end(),[id](Link&l){return l.from==id||l.to==id;}),m_links.end());m_anchors.removeAt(idx);m_selAnchorIds.remove(id);m_selLinkKeys.clear();m_cacheValid=false;}

int NoteChainState::anchorIndexById(int id)const{for(int i=0;i<m_anchors.size();++i)if(m_anchors[i].id==id)return i;return -1;}

Anchor& NoteChainState::anchorAt(int idx){return m_anchors[idx];}
const Anchor& NoteChainState::anchorAt(int idx)const{return m_anchors[idx];}

void NoteChainState::setAnchorInAbsChart(int idx,double lx,double bt,bool mir){auto&a=m_anchors[idx];a.inDx=lx-a.laneX;a.inDy=bt-a.beat;if(mir&&a.smooth){a.outDx=-a.inDx;a.outDy=-a.inDy;}m_cacheValid=false;}

void NoteChainState::setAnchorOutAbsChart(int idx,double lx,double bt,bool mir){auto&a=m_anchors[idx];a.outDx=lx-a.laneX;a.outDy=bt-a.beat;if(mir&&a.smooth){a.inDx=-a.outDx;a.inDy=-a.outDy;}m_cacheValid=false;}

QMap<int,int> NoteChainState::anchorIndexMap()const{QMap<int,int>m;for(int i=0;i<m_anchors.size();++i)m[m_anchors[i].id]=i;return m;}

void NoteChainState::addLink(int f,int t){if(f==t||anchorIndexById(f)<0||anchorIndexById(t)<0)return;if(hasLink(f,t))return;m_links.append({f,t});setSegmentDen(f,t,Const::kDefaultSegmentDen);setSegmentShape(f,t,m_activeShape);m_cacheValid=false;}

void NoteChainState::removeLink(int f,int t){removeLinkInternal(f,t);}

bool NoteChainState::hasLink(int f,int t)const{LinkKey k=makeLinkKey(f,t);for(auto&l:m_links)if(l.key()==k)return true;return false;}

void NoteChainState::clearLinks(){m_links.clear();m_segDen.clear();m_segShape.clear();m_densMode.clear();m_cacheValid=false;}

void NoteChainState::removeLinkInternal(int f,int t){LinkKey k=makeLinkKey(f,t);m_links.erase(std::remove_if(m_links.begin(),m_links.end(),[k](Link&l){return l.key()==k;}),m_links.end());m_segDen.remove(k);m_segShape.remove(k);m_densMode.remove(k);m_cacheValid=false;}

void NoteChainState::setSegmentDen(int f,int t,int d){LinkKey k=makeLinkKey(f,t);m_segDen[k]=qMax(1,d);m_densMode[k]=d;}

int NoteChainState::segmentDen(int f,int t)const{return m_segDen.value(makeLinkKey(f,t),Const::kDefaultSegmentDen);}

int NoteChainState::segmentDensityMode(int f,int t)const{return m_densMode.value(makeLinkKey(f,t),Const::kDefaultSegmentDen);}

void NoteChainState::setSegmentShape(int f,int t,const QString&s){QString ns=ncNormalizeShape(s);LinkKey k=makeLinkKey(f,t);if(ns==QStringLiteral("curve"))m_segShape.remove(k);else m_segShape[k]=ns;m_cacheValid=false;}

QString NoteChainState::segmentShape(int f,int t)const{return ncNormalizeShape(m_segShape.value(makeLinkKey(f,t)));}

void NoteChainState::setDensityMode(int f,int t,int m){m_densMode[makeLinkKey(f,t)]=m;}

void NoteChainState::seedMissingSegmentDenominators(){for(auto&l:m_links){LinkKey k=l.key();if(!m_segDen.contains(k))m_segDen[k]=Const::kDefaultSegmentDen;}}

QVector<SegmentInfo> NoteChainState::connectedAnchorSegments()const{QVector<SegmentInfo> segs;for(auto&l:m_links){int i0=anchorIndexById(l.from),i1=anchorIndexById(l.to);if(i0<0||i1<0)continue;SegmentInfo s;s.i0=i0;s.i1=i1;s.id0=l.from;s.id1=l.to;s.a0=m_anchors[i0];s.a1=m_anchors[i1];s.shape=segmentShape(l.from,l.to);int mode=m_densMode.value(l.key(),0);s.denominator=(mode==0)?Const::kDefaultSegmentDen:qMax(1,m_segDen.value(l.key(),Const::kDefaultSegmentDen));segs.append(s);}std::sort(segs.begin(),segs.end(),[](SegmentInfo&a,SegmentInfo&b){return a.a0.beat<b.a0.beat;});return segs;}

void NoteChainState::selectAnchor(int id){if(anchorIndexById(id)>=0)m_selAnchorIds.insert(id);}
void NoteChainState::deselectAnchor(int id){m_selAnchorIds.remove(id);}
void NoteChainState::clearAnchorSelection(){m_selAnchorIds.clear();}
void NoteChainState::toggleAnchorSelection(int id){if(m_selAnchorIds.contains(id))m_selAnchorIds.remove(id);else selectAnchor(id);}
void NoteChainState::setSingleSelectedAnchor(int id){m_selAnchorIds.clear();selectAnchor(id);}
bool NoteChainState::isAnchorSelected(int id)const{return m_selAnchorIds.contains(id);}
void NoteChainState::selectLink(LinkKey k){m_selLinkKeys.insert(k);}
void NoteChainState::deselectLink(LinkKey k){m_selLinkKeys.remove(k);}
void NoteChainState::clearLinkSelection(){m_selLinkKeys.clear();}
void NoteChainState::toggleLinkSelection(LinkKey k){if(m_selLinkKeys.contains(k))m_selLinkKeys.remove(k);else m_selLinkKeys.insert(k);}
bool NoteChainState::isLinkSelected(LinkKey k)const{return m_selLinkKeys.contains(k);}

bool NoteChainState::selectionEnabled(const QString&k)const{if(k==QStringLiteral("anchors"))return m_noteSnap?false:m_selTargets.anchors;if(k==QStringLiteral("segments"))return m_noteSnap?false:m_selTargets.segments;if(k==QStringLiteral("notes"))return m_selTargets.notes;return false;}
void NoteChainState::setSelectionEnabled(const QString&k,bool v){if(k==QStringLiteral("anchors"))m_selTargets.anchors=v;else if(k==QStringLiteral("segments")){m_selTargets.segments=v;if(!v)m_selLinkKeys.clear();}else if(k==QStringLiteral("notes"))m_selTargets.notes=v;}

void NoteChainState::enforceHandleTimeConstraints(int idx){if(idx<0||idx>=m_anchors.size())return;auto&a=m_anchors[idx];for(auto&l:m_links){if(l.to==a.id){int pi=anchorIndexById(l.from);if(pi>=0){double mb=m_anchors[pi].beat;if(a.beat+a.inDy<mb)a.inDy=mb-a.beat;}}if(l.from==a.id){int ni=anchorIndexById(l.to);if(ni>=0){double mx=m_anchors[ni].beat;if(a.beat+a.outDy>mx)a.outDy=mx-a.beat;}}}}
void NoteChainState::enforceAnchorAndConnectedHandleConstraints(int idx){enforceHandleTimeConstraints(idx);}

void NoteChainState::cleanupLinksAndSelection(){QVector<LinkKey> dead;QSet<int> vids;for(auto&a:m_anchors)vids.insert(a.id);for(auto&l:m_links)if(!vids.contains(l.from)||!vids.contains(l.to))dead.append(l.key());for(auto&k:dead){m_segDen.remove(k);m_segShape.remove(k);m_densMode.remove(k);}m_links.erase(std::remove_if(m_links.begin(),m_links.end(),[&](Link&l){return dead.contains(l.key());}),m_links.end());QSet<int> vs;for(int id:m_selAnchorIds)if(vids.contains(id))vs.insert(id);m_selAnchorIds=vs;}

StateSnapshot NoteChainState::captureSnapshot()const{StateSnapshot s;s.anchors=m_anchors;s.links=m_links;s.segmentDenominators=m_segDen;s.segmentShapes=m_segShape;s.densityModes=m_densMode;s.selectedAnchorIds=m_selAnchorIds;s.selectedLinkKeys=m_selLinkKeys;s.style=m_style;s.nextAnchorId=m_nextAnchorId;s.selectionTargets=m_selTargets;s.curveVisible=m_curveVisible;s.anchorPlacementEnabled=m_anchorPlaceEnabled;s.noteCurveSnapEnabled=m_noteSnap;s.activeLinkShape=m_activeShape;return s;}
void NoteChainState::restoreSnapshot(const StateSnapshot&snap){m_anchors=snap.anchors;m_links=snap.links;m_segDen=snap.segmentDenominators;m_segShape=snap.segmentShapes;m_densMode=snap.densityModes;m_selAnchorIds=snap.selectedAnchorIds;m_selLinkKeys=snap.selectedLinkKeys;m_style=snap.style;m_nextAnchorId=snap.nextAnchorId;m_selTargets=snap.selectionTargets;m_curveVisible=snap.curveVisible;m_anchorPlaceEnabled=snap.anchorPlacementEnabled;m_noteSnap=snap.noteCurveSnapEnabled;m_activeShape=ncNormalizeShape(snap.activeLinkShape);m_drag=DragState{};m_linkDrag=LinkDrag{};m_boxSelect=BoxSelect{};m_pendingConnect=-1;m_shiftDown=false;m_cacheValid=false;}

} // namespace NoteChain

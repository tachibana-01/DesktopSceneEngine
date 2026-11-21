export type HoverRect = {left:number;top:number;right:number;bottom:number};
export type HoverPlacement = {
  left:number;top:number;width:number;height:number;
  contentWidth:number;contentHeight:number;
};

// Accepted visual size, independent of where the card can be presented.
// Native rendering consumes the existing composed frame at this logical size.
export function canonicalTimelineHoverSize(aspect:readonly[number,number]) {
  const ratio=aspect[0]/aspect[1];
  if(!Number.isFinite(ratio)||ratio<=0)return null;
  const contentWidth=Math.min(200,140*ratio),contentHeight=contentWidth/ratio;
  return {contentWidth,contentHeight,width:Math.max(112,contentWidth+16),height:contentHeight+40};
}

// CSS client coordinates throughout. Native Host alone converts to physical
// screen coordinates. A native popup may overlay Preview without suspending it.
export function timelineHoverPlacement(
  bounds:HoverRect,anchor:HoverRect,viewport:{width:number;height:number},
  pointerX:number,aspect:readonly[number,number],nativeLane?:HoverRect,
):HoverPlacement|null {
  const canonical=canonicalTimelineHoverSize(aspect),gap=8;
  if(!canonical||!Number.isFinite(pointerX))return null;
  const left=Math.max(0,bounds.left,nativeLane?.left??bounds.left)+gap;
  const right=Math.min(viewport.width,bounds.right,nativeLane?.right??bounds.right)-gap;
  const top=Math.max(0,bounds.top)+gap;
  const bottom=Math.min(anchor.top-gap,viewport.height-gap,bounds.bottom-gap);
  const availableWidth=right-left,availableHeight=bottom-top;
  if(availableWidth<=16||availableHeight<=40)return null;
  if(nativeLane){
    // Normal native presentation keeps the canonical card. An impossible
    // viewport/lane is unavailable, never a smaller card or a sidebar slot.
    if(canonical.width>availableWidth||canonical.height>availableHeight)return null;
    return {...canonical,left:Math.max(left,Math.min(pointerX-canonical.width/2,right-canonical.width)),top:bottom-canonical.height};
  }
  // Existing DOM-only prototype keeps its viewport-fit presentation policy.
  const ratio=aspect[0]/aspect[1];
  const contentWidth=Math.min(200,availableWidth-16,Math.min(140,availableHeight-40)*ratio);
  const contentHeight=contentWidth/ratio,width=Math.max(Math.min(112,availableWidth),contentWidth+16),height=contentHeight+40;
  return {left:Math.max(left,Math.min(pointerX-width/2,right-width)),top:bottom-height,width,height,contentWidth,contentHeight};
}

type Rect={x:number;y:number;width:number;height:number};
export type PlaybackModePaint={left:number;right:number;top:number;bottom:number};
export type PlaybackModePlacement={left:number;top:number;arrowX:number;paint:PlaybackModePaint};

// CSS blur has no useful hard getBoundingClientRect extent. Bound its paint by
// an explicit clip derived from the configured shadow lengths, not an English
// menu width or a corrective X offset. The arrow is included in the same clip.
export function playbackModePaint(shadow:string,arrowDepth:number):PlaybackModePaint|null{
  const paint={left:0,right:0,top:0,bottom:arrowDepth};
  if(shadow==='none')return paint;
  for(const layer of shadow.replace(/(?:rgba?|color|oklab|oklch|lab|lch)\([^)]*\)/g,'').split(',')){
    if(layer.includes('inset'))continue;
    const values=[...layer.matchAll(/(-?\d*\.?\d+)px/g)].map(value=>Number(value[1]));
    if(values.length<2||values.length>4)return null;
    const [x,y,blur=0,spread=0]=values,extent=Math.max(0,blur+spread);
    paint.left=Math.max(paint.left,extent-x);paint.right=Math.max(paint.right,extent+x);
    paint.top=Math.max(paint.top,extent-y);paint.bottom=Math.max(paint.bottom,extent+y);
  }
  return paint;
}

export function placePlaybackModePopup(trigger:Rect,sidebar:Rect,menu:{width:number;height:number},
  viewport:{width:number;height:number},paint:PlaybackModePaint,arrowWidth:number,arrowDepth:number,gap:number):PlaybackModePlacement|null{
  const values=[trigger.x,trigger.y,trigger.width,trigger.height,sidebar.x,sidebar.y,sidebar.width,sidebar.height,
    menu.width,menu.height,viewport.width,viewport.height,...Object.values(paint),arrowWidth,arrowDepth,gap];
  if(!values.every(Number.isFinite)||trigger.width<=0||trigger.height<=0||sidebar.width<=0||sidebar.height<=0||menu.width<=0||menu.height<=0)return null;
  const safeLeft=Math.max(0,sidebar.x),safeRight=Math.min(viewport.width,sidebar.x+sidebar.width);
  const minimum=safeLeft+paint.left,maximum=safeRight-paint.right-menu.width;
  const center=trigger.x+trigger.width/2,top=trigger.y-gap;
  // No canonical-size shrink, workspace spill or misleading displaced arrow.
  // A layout that cannot contain the measured card explicitly dismisses it.
  if(minimum>maximum||top-menu.height<0||top+arrowDepth>viewport.height)return null;
  const left=Math.max(minimum,Math.min(maximum,center-menu.width/2)),arrowX=center-left;
  if(arrowX<arrowWidth/2||arrowX>menu.width-arrowWidth/2)return null;
  return{left,top,arrowX,paint:{...paint,top:Math.min(paint.top,top-menu.height),bottom:Math.min(paint.bottom,viewport.height-top)}};
}

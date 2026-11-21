// Accepted Timeline slider geometry. No runtime, sequence, decoder or clock.
export const TIMELINE_MAXIMUM=10000;
function validDuration(duration:number){return Number.isFinite(Math.fround(duration))&&Math.fround(duration)>0;}
export function timelineValueForTime(time:number,duration:number):number {
  if(!validDuration(duration))return 0;
  const total=Math.fround(duration),position=Math.fround(time);
  const clamped=Number.isFinite(position)?Math.min(total,Math.max(0,position)):position>0?total:0;
  return Math.min(TIMELINE_MAXIMUM,Math.max(0,Math.round(Math.fround(clamped/total)*TIMELINE_MAXIMUM)));
}
export function timeForValue(value:number,duration:number):number {
  if(!validDuration(duration)||!Number.isFinite(value))return 0;
  const clamped=Math.min(TIMELINE_MAXIMUM,Math.max(0,Math.trunc(value)));
  return Math.fround(Math.fround(Math.fround(duration)*clamped)/TIMELINE_MAXIMUM);
}
export function valueForGroovePosition(x:number,left:number,width:number,upsideDown=false):number {
  if(![x,left,width].every(Number.isFinite)||Math.round(width)<=1)return 0;
  const span=Math.round(width)-1,position=Math.min(span,Math.max(0,Math.round(x)-Math.round(left)));
  const value=Math.floor((2*TIMELINE_MAXIMUM*position+span)/(2*span));
  return upsideDown?TIMELINE_MAXIMUM-value:value;
}

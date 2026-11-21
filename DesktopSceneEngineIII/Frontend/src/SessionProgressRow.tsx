import type { HTMLAttributes, Ref } from 'react';
import { formatRuntimeTime } from './sessionTime.ts';
import { useSessionText } from './sessionLanguage';
import './sessionTimeline.css';

// The accepted Adjustment 005 row. Interaction/thumbnail ownership belongs to
// the caller; B1 uses this view without a seek or mock thumbnail driver.
export function SessionProgressRow({ value, time, duration, enabled, preview = false, readOnly = false, controlRef, grooveRef, controlProps }: {
  value:number; time:number; duration:number; enabled:boolean; preview?:boolean; readOnly?:boolean;
  controlRef?:Ref<HTMLDivElement>; grooveRef?:Ref<HTMLSpanElement>; controlProps?:HTMLAttributes<HTMLDivElement>;
}) {
  const t=useSessionText();
  return <><div className="timeline-line">
    <div ref={controlRef} className="timeline-control" role="slider" aria-label={t('Playback position')} aria-disabled={!enabled || readOnly}
      aria-valuemin={0} aria-valuemax={10000} aria-valuenow={value} aria-valuetext={t('%1 of %2').replace('%1',()=>formatRuntimeTime(time)).replace('%2',()=>formatRuntimeTime(duration))}
      aria-describedby="timeline-instructions" tabIndex={enabled && !readOnly ? 0 : -1} {...controlProps}>
      <span ref={grooveRef} className="timeline-groove"><span className="timeline-progress" style={{width:value/100+'%'}}/><span className="timeline-thumb" style={{left:value/100+'%'}}/></span>
    </div>
    <span className={'timeline-time'+(preview?' is-preview':'')}>{formatRuntimeTime(time)}<span> / </span>{formatRuntimeTime(duration)}</span>
  </div><span id="timeline-instructions" className="timeline-sr">{t(readOnly ? 'Read-only native runtime position. Seeking is not integrated in this milestone.' : 'Drag to preview time; release to seek. The exact right edge advances playback. Keyboard adjusts the thumb only, matching the current production slider.')}</span></>;
}

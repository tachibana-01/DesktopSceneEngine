import { useState, type ReactNode } from 'react';
import {ProductTitleBar} from './ProductTitleBar.tsx';

type WindowAction = 'hide' | 'minimize' | 'maximize' | 'close';

function HideGlyph() {
  return <path d="m3.5 5.75 4.5 4.5 4.5-4.5" />;
}

export function FrozenProductHide({text, onHide}: {text: (value: string) => string; onHide?: () => void}) {
  return <div className="window-controls" role="group" aria-label={text('Hide to tray')}>
    <button type="button" className="window-control window-control--hide" aria-label={text('Hide to tray')}
      aria-describedby="tooltip-hide" disabled={!onHide} onClick={onHide}>
      <svg width="14" height="14" viewBox="0 0 16 16" fill="none" aria-hidden="true"><HideGlyph /></svg>
      <span className="control-tooltip" role="tooltip" id="tooltip-hide">{text(onHide ? 'Hide to tray' : 'Hide is temporarily unavailable')}</span>
    </button>
  </div>;
}

export function ProductSessionBar({context,leftActions,text,onHide}:{
  context:ReactNode;leftActions:ReactNode;text:(value:string)=>string;onHide?:()=>void;
}) {
  return <ProductTitleBar text={text}
    beforeDrag={<>{leftActions}<div className="session-title-context">{context}</div></>}
    beforeCaption={<FrozenProductHide text={text} onHide={onHide} />} />;
}

function WindowIcon({ action, restored = false }: { action: WindowAction; restored?: boolean }) {
  return (
    <svg width="14" height="14" viewBox="0 0 16 16" fill="none" aria-hidden="true">
      {action === 'hide' && <HideGlyph />}
      {action === 'minimize' && <path d="M3 8.5h10" />}
      {action === 'maximize' && (restored
        ? <><path d="M5.5 5.5v-3H13v7.5h-3" /><path d="M3 6h7v7H3z" /></>
        : <path d="M3.5 3.5h9v9h-9z" />)}
      {action === 'close' && <path d="m3.5 3.5 9 9m0-9-9 9" />}
    </svg>
  );
}

// Optional Launcher controls leave the accepted Session / Music shell unchanged.
export function TitleBar({ onFeedback, context, leftActions, hideEligible = true, hideReason, onClose, onHide, hideState, text = value=>value }: {
  onFeedback: (message: string) => void; context?: ReactNode; leftActions?: ReactNode;
  hideEligible?: boolean; hideReason?: string;
  onClose?: () => void;
  onHide?: () => void; text?: (value:string)=>string;
  hideState?: {hidden:boolean;lastResult:string};
}) {
  const [maximized, setMaximized] = useState(false);
  const labels: Record<WindowAction, string> = {
    hide: 'Hide to tray', minimize: 'Minimize', maximize: maximized ? 'Restore' : 'Maximize', close: 'Close',
  };
  function handleAction(action: WindowAction) {
    if(action==='hide'&&onHide){onHide();return;}
    if (action === 'close' && onClose) { onClose(); return; }
    onFeedback(labels[action] + ' — visual prototype only');
    if (action === 'maximize') setMaximized(value => !value);
  }
  function control(action: WindowAction) {
    return (
      <button type="button" className={'window-control window-control--' + action}
        aria-label={text(labels[action])} aria-describedby={'tooltip-' + action}
        data-hidden={action==='hide'?hideState?.hidden:undefined} data-hide-result={action==='hide'?hideState?.lastResult:undefined} data-hide-eligible={action==='hide'&&hideState?hideEligible:undefined}
        disabled={action === 'hide' && !hideEligible}
        onClick={() => handleAction(action)}>
        <WindowIcon action={action} restored={maximized} />
        <span className="control-tooltip" role="tooltip" id={'tooltip-' + action}>{action === 'hide' && !hideEligible
          ? text(hideReason ?? 'Hide is temporarily unavailable') : text(labels[action])}</span>
      </button>
    );
  }
  return (
    <header className="title-bar">
      <span className="application-identity">DESKTOPSCENEENGINE</span>
      {leftActions}
      {context && <div className="session-title-context">{context}</div>}
      <div className="title-bar-space" aria-hidden="true" />
      <div className="window-controls" role="group" aria-label="Mock window controls">
        {control('hide')}
        <div className="caption-controls">
          {control('minimize')}{control('maximize')}{control('close')}
        </div>
      </div>
    </header>
  );
}

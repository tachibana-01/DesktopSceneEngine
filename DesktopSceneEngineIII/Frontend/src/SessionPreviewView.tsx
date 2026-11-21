import { useLayoutEffect, useRef, useState } from 'react';
import type { CSSProperties, ReactNode, Ref } from 'react';
import { findItem, type SessionReadState } from './sessionReadProjection.ts';
import type { displayFacts } from './sessionDisplayModel.ts';
import { previewGeometry } from './sessionPreviewGeometry.ts';
import './sessionPreview.css';
import {useSessionText,displayText} from './sessionLanguage';
import type { NativePreviewState } from './ProductionPreviewGeometry.ts';

export function SessionPreviewView({ state, facts, highlighted, background, language, resetEpoch, readOnly = false, onResetLayout, onHighlight, onBackground, onBackgroundReselect, onIdentify, onDeploy, deploymentPending, actionFeedback, children, viewportOverride, identifyStatus, diagnostics, nativePreviewSurfaceRef, nativePreviewState, onNativePreviewLayout }: {
  state: SessionReadState; facts: ReturnType<typeof displayFacts>; highlighted:boolean; background:'cover'|'reveal'; language:'en'|'zh_CN'; resetEpoch:unknown;
  readOnly?:boolean; onResetLayout?:()=>void; onHighlight?:(value:boolean)=>void; onBackground?:(value:'cover'|'reveal')=>void;
  onBackgroundReselect?:(value:'cover'|'reveal')=>void;
  onIdentify?:()=>void; onDeploy?:()=>void; children?:ReactNode; viewportOverride?:{width:number;height:number}; identifyStatus?:string;
  deploymentPending?:'deploy'|'undeploy'; actionFeedback?:string;
  diagnostics?:Record<string,string|number|boolean>;
  nativePreviewSurfaceRef?:Ref<HTMLDivElement>; nativePreviewState?:NativePreviewState;
  onNativePreviewLayout?:()=>void;
}) {
  const t=useSessionText();
  const aspect = state.project.canvasAspect;
  const viewport = useRef<HTMLDivElement>(null), viewTrigger = useRef<HTMLButtonElement>(null);
  const viewOptions = useRef<HTMLDivElement>(null);
  const [size, setSize] = useState({ width: 0, height: 0 });
  const [viewOpen, setViewOpen] = useState(false);
  useLayoutEffect(() => { setViewOpen(false); }, [resetEpoch]);
  useLayoutEffect(() => {
    if (!viewOpen) return;
    const close = () => setViewOpen(false);
    const outside = (event: Event) => {
      if (event.target instanceof Node && !viewTrigger.current?.contains(event.target) && !viewOptions.current?.contains(event.target)) close();
    };
    const escape = (event: KeyboardEvent) => {
      if (event.key !== 'Escape') return;
      event.preventDefault();
      event.stopPropagation();
      close();
      viewTrigger.current?.focus();
    };
    document.addEventListener('pointerdown', outside, true);
    document.addEventListener('focusin', outside, true);
    window.addEventListener('blur', close);
    window.addEventListener('keydown', escape, true);
    return () => {
      document.removeEventListener('pointerdown', outside, true);
      document.removeEventListener('focusin', outside, true);
      window.removeEventListener('blur', close);
      window.removeEventListener('keydown', escape, true);
    };
  }, [viewOpen]);
  useLayoutEffect(() => {
    const element = viewport.current!;
    const measure = () => { const rect = element.getBoundingClientRect(); setSize(old => old.width === Math.floor(rect.width) && old.height === Math.floor(rect.height) ? old : { width: Math.floor(rect.width), height: Math.floor(rect.height) }); };
    const observer = new ResizeObserver(measure); observer.observe(element); measure(); return () => observer.disconnect();
  }, []);
  const geometry = previewGeometry(size, aspect);
  // Inner surface size can remain constant while its fitted parent moves.
  // Notify after React has committed that canonical layout, then read the DOM.
  useLayoutEffect(()=>{onNativePreviewLayout?.();},[onNativePreviewLayout,size.width,size.height,geometry.native.x,geometry.native.y,geometry.native.width,geometry.native.height]);
  const current = state.runtime ? findItem(state, state.activeItemId)?.item : undefined;
  const frameStyle: CSSProperties & Record<`--canvas-${'left' | 'top' | 'width' | 'height'}`, string> = {
    left: geometry.frame.x, top: geometry.frame.y, width: geometry.frame.width, height: geometry.frame.height,
    '--canvas-left': geometry.surfaceInFrame.x + 'px', '--canvas-top': geometry.surfaceInFrame.y + 'px',
    '--canvas-width': geometry.surfaceInFrame.width + 'px', '--canvas-height': geometry.surfaceInFrame.height + 'px',
  };
  const nativeStyle: CSSProperties = { left: geometry.surfaceInFrame.x, top: geometry.surfaceInFrame.y, width: geometry.surfaceInFrame.width, height: geometry.surfaceInFrame.height };
  const viewportStyle:CSSProperties|undefined=viewportOverride
    ? {flex:'none',width:viewportOverride.width,height:viewportOverride.height}
    : undefined;
  const presentation=nativePreviewState?.currentPresentation;
  const presentationFacts={
    'data-model-c-accepted':presentation?JSON.stringify(presentation.accepted):undefined,
    'data-model-c-accepted-through':presentation?.acceptedThrough,
    'data-model-c-current-reason':presentation?.reason,
    'data-model-c-lifecycle-valid':presentation?.lifecycleValid,
    'data-model-c-desired-pending':nativePreviewState?.desired?.pending,
    'data-model-c-desired-outcome':nativePreviewState?.desired?.outcome,
    'data-model-c-desired-request':nativePreviewState?.desired?.requestId,
    'data-model-c-aperture-owner':presentation?.apertureOwner,
  };
  const deploymentLabel=deploymentPending==='deploy'?'Deploy':deploymentPending==='undeploy'?'Undeploy':facts.deploymentLabel;
  const presentationLabel=deploymentPending==='deploy'?'Deploying…':deploymentPending==='undeploy'?'Undeploying…'
    :facts.presentation==='preview-only'?'PREVIEW ONLY':facts.presentation==='deployed'?'DEPLOYED':'CLEANUP REQUIRED';
  return <section className="preview-workspace preview-phase-e" aria-labelledby="preview-heading" data-bound-id={facts.boundId}
    data-display-availability={facts.availability} data-presentation={facts.presentation} data-retained={facts.retained}
    data-highlight={highlighted} data-background={background} {...diagnostics}>
    <header className="preview-context preview-context-e">
      <div className="preview-facts-row"><h1 id="preview-heading">{t('PREVIEW')}</h1><span className="preview-canvas-fact" data-canvas-aspect={aspect.join(':')}>{t('CANVAS')} <strong>{aspect.join(':')}</strong></span>
        <span className="preview-presentation-state" role="status">{t(presentationLabel)}</span></div>
      <div className="preview-display-row"><span className="bound-display" tabIndex={0} title={displayText(language,facts.tooltip)} aria-label={displayText(language,facts.label)}>{displayText(language,facts.label)}</span>
        <div className="preview-actions"><button type="button" aria-label={t('Identify bound Display')} disabled={readOnly || !facts.canIdentify} onClick={onIdentify}>{t('Identify')}</button>
          <button type="button" className="deployment-action" aria-label={t(deploymentLabel)} disabled={readOnly || Boolean(deploymentPending) || !facts.deploymentEnabled} onClick={onDeploy}>{t(deploymentLabel)}</button>
          <button type="button" ref={viewTrigger} aria-label={t('Preview View preferences')} aria-expanded={viewOpen} aria-controls="preview-view-options" disabled={false} onClick={() => setViewOpen(!viewOpen)}>{t('View')} <span aria-hidden="true">⌄</span></button></div></div>
      {viewOpen && <div ref={viewOptions} id="preview-view-options" className="preview-view-options" aria-label={t('Preview View preferences')} role="group">
        <label className="preview-highlight-option"><input type="checkbox" aria-label={t('Highlight Canvas Boundary')} checked={highlighted} disabled={readOnly} onChange={event => onHighlight?.(event.target.checked)} />{t('Highlight Canvas Boundary')}</label>
        <fieldset><legend>{t('Desktop Background')} <span>{t('DESKTOP OUTPUT')}</span></legend>
          <label><input type="radio" name="desktop-background" aria-label={t('Cover Entire')} checked={background === 'cover'} disabled={readOnly}
            onClick={() => { if (background === 'cover') onBackgroundReselect?.('cover'); }} onChange={() => onBackground?.('cover')} />{t('Cover Entire')}</label>
          <label><input type="radio" name="desktop-background" aria-label={t('Reveal Wallpaper')} checked={background === 'reveal'} disabled={readOnly}
            onClick={() => { if (background === 'reveal') onBackgroundReselect?.('reveal'); }} onChange={() => onBackground?.('reveal')} />{t('Reveal Wallpaper')}</label>
        </fieldset>
        <button type="button" data-reset-layout onClick={onResetLayout}>{t('Reset Layout')}</button>
      </div>}
    </header>
    <div className="preview-stage preview-stage-e">
      <div ref={viewport} className="preview-viewport" data-native-region="future-child-hwnd" style={viewportStyle}>
        <div className="preview-canvas preview-chrome-frame" data-preview-visible={geometry.visible} data-native-bounds={JSON.stringify(geometry.native)} hidden={!geometry.visible} style={frameStyle}>
          <div ref={nativePreviewSurfaceRef} className="preview-placeholder preview-native-surface" data-preview-fit-width={size.width} data-preview-fit-height={size.height} data-native-preview-state={nativePreviewState?.status} {...presentationFacts} style={nativeStyle} role="img" aria-label={t(nativePreviewState ? 'Production Engine native Preview target' : readOnly ? 'Native Preview integration pending' : 'Native Preview surface placeholder, Canvas %1, mock only').replace('%1',aspect.join(' by '))} />
        </div>
      </div>
      <div className="preview-caption"><span className="preview-source">{current?.alias ?? t('No media loaded')}</span><span data-native-preview-status={nativePreviewState?.status} role={nativePreviewState?.status==='failure'?'alert':undefined}>{nativePreviewState ? nativePreviewState.status==='failure' ? 'NATIVE PREVIEW · '+nativePreviewState.message : 'NATIVE PREVIEW · '+nativePreviewState.status.toUpperCase() : t(geometry.visible ? 'NATIVE SURFACE · MOCK' : 'PREVIEW SURFACE UNAVAILABLE')}</span></div>
      <output className="preview-identify-result" aria-live="polite">{actionFeedback ? t(actionFeedback) : identifyStatus ? t('Identify request')+' · '+identifyStatus+' · '+t('mock') : ''}</output>
    </div>
    {children}
  </section>;
}

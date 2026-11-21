import { useCallback, useEffect, useLayoutEffect, useRef, useState, type ReactNode } from 'react';
import { findItem, findPlaylist } from './sessionReadProjection.ts';
import { normalizedAspect } from './sessionPresentationValues.ts';
import { MediaIcon, type EntityMenuTarget } from './SessionLibrary';
import { SessionChoice as Choice } from './SessionChoice';
import type { SessionItem, SessionPlaylist } from './sessionTypes';
import type { useSessionPrototype } from './useSessionPrototype';
import type {AuthoredIntent,PresentationPatch} from './ProductionAuthoredTypes.ts';
import { useSessionText } from './sessionLanguage';
import { useNativeRangeDrag } from './useNativeRangeDrag.ts';
import './sessionInspector.css';

type Editable = ReturnType<typeof useSessionPrototype>;
export interface SessionInspectorAdapter {
  state: import('./sessionReadProjection.ts').SessionReadState;
  inspectorEpoch: string | number;
  language: { effective: 'en' | 'zh_CN'; retranslation: string | number };
  readOnly?: boolean;
  runtimeRateAvailable?:boolean;
  inspector?: Editable['inspector'];
  present?: Editable['present'];
  authoredIntent?: (intent:AuthoredIntent)=>Promise<void>;
}
type Mock = SessionInspectorAdapter;
function Row({ label, children, source = false }: { label: string; children: ReactNode; source?: boolean }) {
  const t = useSessionText();
  return <div className={'identity-property inspector-property' + (source ? ' identity-property--source' : '')}>
    <span className="inspector-property-label">{t(label)}</span>{children}
  </div>;
}
function Section({ title, children }: { title: string; children: ReactNode }) {
  const t = useSessionText();
  return <section className="inspector-section"><h3>{t(title)}</h3>{children}</section>;
}

// Spinbox drafts retain partial text. The prototype keeps accepted immediate
// edits; native scalar intents commit on editing-finished, and Rate uses Apply.
function NumberInput({ label, value, min, max, step, decimals = 2, onValue, title, inGroup = false, exact = false, commitOnFinish = false, stepOnGrid = false }: {
  label: string; value: number; min: number; max: number; step: number; decimals?: number;
  onValue: (value: number) => void; title?: string; inGroup?: boolean;
  exact?:boolean;commitOnFinish?:boolean;stepOnGrid?:boolean;
}) {
  const t = useSessionText();
  const format = (number: number) => exact ? String(number) : number.toFixed(decimals);
  const [draft, setDraft] = useState(() => format(value));
  const input = useRef<HTMLInputElement>(null);
  // A formatted display is never an edit candidate. Native fields keep the raw
  // value until explicit input, so focus/Enter/blur cannot quantize authored truth.
  const last = useRef(commitOnFinish ? value : Number(format(value)));
  const submitted = useRef(value);
  useEffect(() => {
    if (document.activeElement !== input.current) {
      last.current = commitOnFinish ? value : Number(format(value)); submitted.current=value; setDraft(format(value));
    }
  }, [value]);
  function accept(text: string) {
    setDraft(text);
    if (text.trim() === '') return;
    const parsed = Number(text);
    if (!Number.isFinite(parsed) || parsed < min || parsed > max) return;
    const rounded = Number(parsed.toFixed(decimals));
    if (rounded !== last.current) { last.current = rounded; if(!commitOnFinish)onValue(rounded); }
  }
  function finish() {
    setDraft(format(last.current));
    if(commitOnFinish&&last.current!==submitted.current){submitted.current=last.current;onValue(last.current);}
  }
  return <input ref={input} className={(inGroup ? '' : 'property-value ') + 'inspector-number'} type="number" aria-label={t(label)} title={title ? t(title) : undefined}
    min={min} max={max} step={step} value={draft} onChange={event => accept(event.target.value)} onBlur={finish}
    onKeyDown={event => {
      if (event.key === 'Enter') { event.preventDefault(); finish(); }
      if (event.key === 'ArrowUp' || event.key === 'ArrowDown') {
        event.preventDefault();
        const up = event.key === 'ArrowUp', units = last.current / step;
        const stepped = stepOnGrid ? Number(((up ? Math.floor(units + 1e-9) + 1 : Math.ceil(units - 1e-9) - 1) * step).toFixed(decimals))
          : last.current + (up ? step : -step);
        accept(format(Math.min(max, Math.max(min, stepped))));
      }
    }} />;
}
function NameInput({ alias, commit }: { alias: string; commit: (draft: string,expectedAlias:string) => string|Promise<void> }) {
  const t = useSessionText();
  const [draft, setDraft] = useState(alias);
  const input=useRef<HTMLInputElement>(null),baseline=useRef(alias),latest=useRef(alias),finishing=useRef(false),submitted=useRef(false);latest.current=alias;
  useEffect(() => {if(submitted.current||document.activeElement!==input.current){setDraft(alias);baseline.current=alias;}}, [alias]);
  function finish() {
    if(finishing.current)return;
    const result=commit(draft,baseline.current);
    if(typeof result==='string'){setDraft(result);baseline.current=result;}
    else{submitted.current=true;finishing.current=true;void result.finally(()=>{finishing.current=false;setDraft(latest.current);baseline.current=latest.current;});}
  }
  return <input ref={input} type="text" className="property-value inspector-name" aria-label={t('Name')} data-inspector-name
    value={draft} title={draft} onChange={event => {submitted.current=false;setDraft(event.target.value);}} onBlur={finish}
    onFocus={()=>{baseline.current=alias;}}
    onKeyDown={event => { if (event.key === 'Enter') { event.preventDefault(); finish(); } }} />;
}
function Percent({ label, value, onValue, commit = 'prototype', readOnly = false }: {
  label: string; value: number; onValue: (value: number) => void | Promise<void>; commit?: 'prototype' | 'release' | 'live'; readOnly?: boolean;
}) {
  const t = useSessionText();
  const percent = Math.round(value * 100);
  const trackDrag = commit !== 'prototype';
  const drag = useNativeRangeDrag();
  const dragValue = useRef<number | null>(null);
  const submission = useRef(0);
  const [dragDraft, setDragDraft] = useState<number | null>(null);
  const [submittedDraft, setSubmittedDraft] = useState<number | null>(null);
  const shown = dragDraft ?? submittedDraft ?? percent;
  const clearDraft = useCallback(() => {
    const next = dragValue.current;
    dragValue.current = null;
    setDragDraft(null);
    return next;
  }, []);
  const cancelDrag = useCallback(() => {
    if (drag.cancel()) clearDraft();
  }, [drag, clearDraft]);
  useLayoutEffect(() => {
    if (readOnly) {
      cancelDrag();
      ++submission.current;
      setSubmittedDraft(null);
    }
  }, [readOnly, cancelDrag]);
  useLayoutEffect(() => {
    if (!trackDrag) return;
    const onVisibility = () => { if (document.hidden) cancelDrag(); };
    window.addEventListener('blur', cancelDrag);
    window.addEventListener('pagehide', cancelDrag);
    document.addEventListener('visibilitychange', onVisibility);
    return () => {
      ++submission.current;
      window.removeEventListener('blur', cancelDrag);
      window.removeEventListener('pagehide', cancelDrag);
      document.removeEventListener('visibilitychange', onVisibility);
    };
  }, [trackDrag, cancelDrag]);
  function apply(next: number) {
    if (readOnly) return;
    if (!trackDrag) {
      void onValue(next / 100);
      return;
    }
    // Keep the thumb local until the matching native request settles. The
    // selected entity and the authored revision continue to come from native.
    const request = ++submission.current;
    setSubmittedDraft(next);
    const settle = () => {
      if (request === submission.current) setSubmittedDraft(null);
    };
    void Promise.resolve(onValue(next / 100)).then(settle, settle);
  }
  return <div className="property-value range-property"><input type="range" aria-label={t(label)} min={0} max={100} step={1} value={shown}
    onChange={event => {
      if (drag.ignoresInput()) return;
      const next = Number(event.currentTarget.value);
      if (drag.active()) {
        setDragDraft(next);
        if (commit === 'release') {
          dragValue.current = next;
          return;
        }
      }
      apply(next);
    }}
    onPointerDown={event => { if (trackDrag && !readOnly) drag.start(event); }}
    onPointerMove={event => {
      if (event.pointerType === 'mouse' && !(event.buttons & 1) && drag.owns(event)) cancelDrag();
    }}
    onPointerUp={event => {
      if (!drag.owns(event)) return;
      drag.end();
      const next = clearDraft();
      if (commit === 'release' && next !== null) apply(next);
    }}
    onPointerCancel={event => { if (drag.owns(event)) cancelDrag(); }}
    onLostPointerCapture={event => { if (drag.owns(event)) cancelDrag(); }}
    onBlur={cancelDrag}
    onKeyDown={drag.onKeyDown} />
    <output className="property-output" aria-label={t('%1 value').replace('%1', t(label))}>{shown}%</output></div>;
}
function PlaylistFields({ playlist, mock }: { playlist: SessionPlaylist; mock: Mock }) {
  return <><Section title="PLAYLIST">
    <Row label="Name"><NameInput alias={playlist.alias} commit={(alias,expectedAlias) => mock.authoredIntent
      ?mock.authoredIntent({type:'RenamePlaylist',payload:{playlistId:playlist.id,alias,expectedAlias}})
      :findPlaylist(mock.inspector?.({ type: 'rename-playlist', id: playlist.id, alias }) ?? mock.state, playlist.id)?.alias ?? playlist.alias} /></Row>
    <Row label="Items"><output className="property-value inspector-readout" data-item-count>{playlist.items.length}</output></Row>
  </Section><Section title="PLAYBACK"><Row label="Master Volume"><Percent label="Playlist Master Volume" value={playlist.masterVolume} commit={mock.authoredIntent ? 'release' : 'prototype'} readOnly={mock.readOnly}
    onValue={value => {
      if (mock.authoredIntent) return mock.authoredIntent({type:'SetPlaylistMaster',payload:{playlistId:playlist.id,value}});
      mock.inspector?.({ type: 'playlist-master', id: playlist.id, value });
    }} /></Row></Section></>;
}
const presets = [[16, 9], [16, 10], [4, 3], [1, 1]] as const;
function VisualFields({ item, playlistId, mock }: { item: SessionItem; playlistId:string; mock: Mock }) {
  const t = useSessionText();
  const visual = item.presentation;
  const [ratio, setRatio] = useState(() => [visual.mediaPresentation.customAspect.numerator, visual.mediaPresentation.customAspect.denominator]);
  const ratioRef = useRef(ratio);
  const [preset, setPreset] = useState(() => String(presets.findIndex(([n, d]) => n === ratio[0] && d === ratio[1]) + 1 || 'custom'));
  const position = useRef([Number(visual.layout.positionX.toFixed(2)), Number(visual.layout.positionY.toFixed(2))]);
  const production=Boolean(mock.authoredIntent);
  function present(patch:PresentationPatch){void mock.authoredIntent?.({type:'SetItemPresentation',payload:{playlistId,itemId:item.id,patch}});}
  useEffect(()=>{
    if(!production)return;
    const next=[visual.mediaPresentation.customAspect.numerator,visual.mediaPresentation.customAspect.denominator];
    ratioRef.current=next;setRatio(next);setPreset(String(presets.findIndex(([n,d])=>n===next[0]&&d===next[1])+1||'custom'));
  },[production,visual.mediaPresentation.customAspect.numerator,visual.mediaPresentation.customAspect.denominator]);
  function aspect(next: number[]) {
    ratioRef.current = next; setRatio(next);
    const normalized = normalizedAspect(next[0], next[1]);
    if (normalized) {if(production)present({customAspect:normalized});else mock.present?.(item.id, { mediaPresentation: { customAspect: normalized } });}
  }
  return <Section title="VISUAL">
    <Row label="Policy"><Choice label="Visual policy" value={visual.mediaPresentation.mode}
      options={[[ 'native', 'Native' ], [ 'auto-fill', 'Auto Fill' ], [ 'custom', 'Custom Aspect' ]]}
      onValue={value => {const mode=value as typeof visual.mediaPresentation.mode;if(production)present({mode});else mock.present?.(item.id, { mediaPresentation: { mode } });}} /></Row>
    <div className="inspector-aspect" hidden={visual.mediaPresentation.mode !== 'custom'}>
      <Row label="Aspect"><Choice label="Aspect preset" value={preset} options={[...presets.map(([n, d], index) => [String(index + 1), n + ':' + d] as const), ['custom', 'Custom']]}
        onValue={chosen => {
        setPreset(chosen);
        if (chosen !== 'custom') aspect([...presets[Number(chosen) - 1]]);
      }} /></Row>
      <div className="inspector-ratio-pair"><span>{t('Ratio')}</span><div>
        <NumberInput label="Aspect numerator" value={ratio[0]} min={1} max={10000} step={1} decimals={0} exact={production} commitOnFinish={production} onValue={value => aspect([value, ratioRef.current[1]])} />
        <span aria-hidden="true">:</span>
        <NumberInput label="Aspect denominator" value={ratio[1]} min={1} max={10000} step={1} decimals={0} exact={production} commitOnFinish={production} onValue={value => aspect([ratioRef.current[0], value])} />
      </div></div>
    </div>
    <Row label="Scale"><NumberInput label="Uniform scale" value={visual.layout.scaleX} min={0.1} max={3} step={0.1}
      decimals={2} commitOnFinish={production} onValue={value => {if(production)present({scale:value});else mock.present?.(item.id, { layout: { scaleX: Math.fround(value), scaleY: Math.fround(value) } });}} /></Row>
    <Row label="Position X"><NumberInput label="Position X normalized" title="Positive X moves right" value={visual.layout.positionX} min={-1} max={1} step={0.05}
      decimals={2} commitOnFinish={production} onValue={value => {if(production)present({positionX:value});else{position.current[0] = value; mock.present?.(item.id, { layout: { positionX: Math.fround(value), positionY: Math.fround(position.current[1]) } });}}} /></Row>
    <Row label="Position Y"><NumberInput label="Position Y normalized" title="Positive Y moves up" value={visual.layout.positionY} min={-1} max={1} step={0.05}
      decimals={2} commitOnFinish={production} onValue={value => {if(production)present({positionY:value});else{position.current[1] = value; mock.present?.(item.id, { layout: { positionX: Math.fround(position.current[0]), positionY: Math.fround(value) } });}}} /></Row>
  </Section>;
}
function ItemFields({ item, playlistId, mock }: { item: SessionItem; playlistId:string; mock: Mock }) {
  const t = useSessionText();
  const [rateDraft, setRateDraft] = useState(item.rate);
  const currentRateDraft = useRef(rateDraft);
  const production=Boolean(mock.authoredIntent);
  useEffect(()=>{if(production){currentRateDraft.current=item.rate;setRateDraft(item.rate);}},[production,item.rate]);
  return <><section className="inspector-section" aria-label={t('ITEM')}>
    <Row label="Name"><NameInput alias={item.alias} commit={(alias,expectedAlias) => mock.authoredIntent
      ?mock.authoredIntent({type:'RenameItem',payload:{playlistId,itemId:item.id,alias,expectedAlias}})
      :findItem(mock.inspector?.({ type: 'rename-item', id: item.id, alias }) ?? mock.state, item.id)?.item.alias ?? item.alias} /></Row>
    <Row label="Source" source><span className="property-value inspector-source" tabIndex={0} title={item.source} aria-label={t('Source')}>{item.source}</span></Row>
  </section><Section title="PLAYBACK">
    {item.kind === 'image' ? <Row label="Duration · s"><NumberInput label="Image Duration" value={item.duration} min={0.1} max={86400} step={0.1}
      exact={production} commitOnFinish={production} onValue={value => {if(mock.authoredIntent)void mock.authoredIntent({type:'SetImageDuration',payload:{playlistId,itemId:item.id,value}});else mock.inspector?.({ type: 'image-duration', id: item.id, value });}} /></Row> : <>
      <Row label="Configured Rate"><div className="property-value inspector-rate-entry">
        <NumberInput label="Configured Rate" value={rateDraft} min={0.5} max={2} step={0.1} decimals={production?2:1} exact={production} stepOnGrid={production} inGroup
          onValue={value => { currentRateDraft.current = value; setRateDraft(value); }} />
        <button type="button" className="inspector-apply" aria-label={t('Apply Rate')} onClick={() => {
          if(mock.authoredIntent){void mock.authoredIntent({type:'ApplyVideoRate',payload:{playlistId,itemId:item.id,value:currentRateDraft.current}});return;}
          const next = mock.inspector?.({ type: 'video-rate', id: item.id, value: currentRateDraft.current });
          const value = findItem(next ?? mock.state, item.id)?.item.rate ?? item.rate;
          currentRateDraft.current = value; setRateDraft(value);
        }}>{t('Apply')}</button>
      </div></Row>
      <Row label="Actual Rate"><output className="property-value inspector-readout" aria-label={t('Actual Rate')}>{mock.runtimeRateAvailable===false?'—':(mock.state.runtime?.rate ?? 1).toFixed(2)+'×'}</output></Row>
      <Row label="Volume"><Percent label="Volume" value={item.mediaGain} commit={production ? 'live' : 'prototype'} readOnly={mock.readOnly}
        onValue={value => {
          if (mock.authoredIntent) return mock.authoredIntent({type:'SetItemGain',payload:{playlistId,itemId:item.id,value}});
          mock.inspector?.({ type: 'media-gain', id: item.id, value });
        }} /></Row>
    </>}
  </Section><VisualFields item={item} playlistId={playlistId} mock={mock} /></>;
}

export function SessionInspector({ mock, onMenu }: { mock: Mock; onMenu: (target: EntityMenuTarget) => void }) {
  const t = useSessionText();
  const state = mock.state;
  const selectedItem = findItem(state, state.selectedItemId);
  const selectedPlaylist = findPlaylist(state, state.selectedPlaylistId);
  const target = selectedItem?.item ?? selectedPlaylist;
  const entity = selectedItem ? 'item' : 'playlist';
  const active = target && (selectedItem ? state.activeItemId : state.activePlaylistId) === target.id;
  return <aside className="session-inspector inspector-phase-b" aria-labelledby="inspector-heading" data-inspector-context={target ? entity : 'none'}
    data-owner-id={target?.id ?? ''} data-authored-rate={selectedItem?.item.rate} data-actual-rate={mock.runtimeRateAvailable===false?'':state.runtime?.rate ?? 1}
    data-authored-duration={selectedItem?.item.duration} data-runtime-volume={mock.runtimeRateAvailable!==undefined?undefined:state.runtime?.volume ?? 1}
    data-authored-presentation={selectedItem ? JSON.stringify(selectedItem.item.presentation) : undefined}
    data-runtime-item={state.runtime ? JSON.stringify(state.runtime.items?.find(item => item.id === state.activeItemId)) : undefined}>
    <div className="panel-heading"><h2 id="inspector-heading">{t('INSPECTOR')}</h2></div>
    <fieldset disabled={mock.readOnly} className="inspector-content session-inspector-fields">
      {!target ? <p className="inspector-empty">{t('No selection')}</p> : <>
        <div className="inspector-context-heading"><span>{t(entity.toUpperCase())}</span>
          <button type="button" className="panel-icon-button" aria-label={t('Selected entity commands')} aria-haspopup="menu" onClick={event => {
            const r = event.currentTarget.getBoundingClientRect(); onMenu({ entity, id: target.id, x: r.right, y: r.bottom });
          }}>···</button>
        </div>
        <div className="inspector-selection">{selectedItem && <MediaIcon kind={selectedItem.item.kind} />}
          <div><p title={target.alias}>{target.alias}</p><div className="inspector-selection-metadata">
            <span>{t(active ? 'ACTIVE' : 'SELECTED')}{!target.enabled ? ' · ' + t('DISABLED') : ''}</span>
            {selectedItem && <output className="inspector-item-type" aria-label={t('Type')} data-item-type>{t(selectedItem.item.kind === 'image' ? 'Image' : 'Video')}</output>}
          </div></div>
        </div>
        {selectedItem ? <ItemFields key={mock.inspectorEpoch + ':' + mock.language.retranslation + ':' + target.id} item={selectedItem.item} playlistId={selectedItem.playlist.id} mock={mock} />
          : <PlaylistFields key={mock.inspectorEpoch + ':' + mock.language.retranslation + ':' + target.id} playlist={selectedPlaylist!} mock={mock} />}
      </>}
    </fieldset>
  </aside>;
}

import type {DocumentChoice} from './ProductionSessionTypes.ts';
import {SessionModal} from './SessionModal';
import {useSessionText} from './sessionLanguage';

// Shared accepted presentation only: it never owns a document, path, dirty bit
// or pending intent. Unmount/reload does not produce a business decision.
export function SessionDocumentDecision({name,onDecision,disabled=false}:{name:string;onDecision:(choice:DocumentChoice)=>void;disabled?:boolean}){
  const t=useSessionText();
  return <SessionModal title={t('Unsaved Changes')} onClose={()=>{if(!disabled)onDecision('cancel');}} initialFocus="[data-decision-save]">
    <p>{t('Save changes to “%1”?').replace('%1',()=>name)}</p>
    <div className="session-dialog-actions">
      <button type="button" data-decision-discard disabled={disabled} onClick={()=>onDecision('discard')}>{t('Discard')}</button>
      <button type="button" data-decision-cancel disabled={disabled} onClick={()=>onDecision('cancel')}>{t('Cancel')}</button>
      <button type="button" className="session-dialog-primary" data-decision-save disabled={disabled} onClick={()=>onDecision('save')}>{t('Save')}</button>
    </div>
  </SessionModal>;
}

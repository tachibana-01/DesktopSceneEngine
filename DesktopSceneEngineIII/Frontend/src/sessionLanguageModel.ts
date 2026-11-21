export interface SessionLanguageState { desired: 'en'|'zh_CN'; effective: 'en'|'zh_CN'; committed: string; draft: string; pending: boolean; readFails: boolean; catalogAvailable: boolean; installSucceeds: boolean; qtbaseAvailable: boolean; retranslation: number }
export type LanguageAction = {type:'draft'|'external-write';value:string}|{type:'invalidate'|'flush'}|{type:'faults';value:Partial<Pick<SessionLanguageState,'readFails'|'catalogAvailable'|'installSucceeds'|'qtbaseAvailable'>>};
export function createSessionLanguage():SessionLanguageState{return {desired:'en',effective:'en',committed:'en',draft:'en',pending:false,readFails:false,catalogAvailable:true,installSucceeds:true,qtbaseAvailable:true,retranslation:0};}
// Delivered invalidation queues a reload of latest saved truth, never its draft
// or an event payload. This is value-only fixture state, not a storage design.
export function languageTransition(state:SessionLanguageState,action:LanguageAction):SessionLanguageState{
  switch(action.type){
    case 'draft': return {...state,draft:action.value};
    case 'external-write': return {...state,committed:action.value};
    case 'invalidate': return {...state,pending:true};
    case 'faults': return {...state,...action.value};
    case 'flush': {
      if(!state.pending)return state;
      if(state.readFails)return {...state,pending:false};
      const desired=state.committed==='zh_CN'?'zh_CN':'en';
      // setLanguage keeps an already installed same-language translator. A
      // missing qtbase catalog alone does not disable the application catalog.
      const effective=desired===state.effective?state.effective:desired==='zh_CN'&&state.catalogAvailable&&state.installSucceeds?'zh_CN':'en';
      // MainWindow::applyCommittedLanguage skips rebuilding only when both
      // desired and installed language already match. A failed catalog can
      // therefore cause another real retranslation on a later delivered reload.
      const rebuild=state.desired!==desired||state.effective!==desired;
      return {...state,pending:false,desired,effective,retranslation:state.retranslation+(rebuild?1:0)};
    }
  }
}

export type ProjectCommand='new'|'open'|'save'|'save-as'|'close';
/** Four standard WindowShortcut bindings, not browser editing heuristics. */
export function documentShortcut(event:{ctrlKey:boolean;altKey:boolean;metaKey:boolean;shiftKey:boolean;key:string}):ProjectCommand|null{
 if(!event.ctrlKey||event.altKey||event.metaKey)return null;
 const key=event.key.toLowerCase();
 if(key==='s')return event.shiftKey?'save-as':'save';
 if(event.shiftKey)return null;
 return key==='n'?'new':key==='o'?'open':null;
}

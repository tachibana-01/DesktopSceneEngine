import type {AuthoredResult} from './ProductionAuthoredTypes.ts';

// Native imports include per-source warnings and an aggregate summary. Render
// each source occurrence in order, with shared runtime/restore warnings once.
export function authoredOutcomeMessages(result:AuthoredResult,t:(message:string)=>string):string[]{
  const messages:string[]=[],represented=new Set<string>(),shared=new Set<string>();
  const key=(code:string,message:string)=>JSON.stringify([code,message]);
  for(const source of result.sources){
    for(const warning of source.warnings){
      const identity=key(warning.code,warning.message),isShared=/^(runtime-|position-)/.test(warning.code);
      if(isShared&&shared.has(identity))continue;
      if(isShared)shared.add(identity);
      represented.add(identity);messages.push(source.source+': '+(warning.message||warning.code));
    }
    if(!source.warnings.length&&source.authored!=='committed')messages.push(source.source+': '+source.authored);
  }
  for(const warning of result.warnings){const identity=key(warning.code,warning.message);if(!represented.has(identity)){represented.add(identity);messages.push(warning.message||warning.code);}}
  if(!['accepted','applied','no-change','picker-cancelled'].includes(result.status)&&!messages.length)messages.push(t('Authored command')+': '+result.status);
  if(['warning','failed'].includes(result.runtime)&&!messages.length)messages.push(t('Authored changes were kept; runtime synchronization needs attention.')+' ('+result.runtime+')');
  if(['rebound-at-start','failed'].includes(result.positionRestore)&&!messages.length)messages.push(t('Authored changes were kept; playback position could not be restored.'));
  return messages;
}

import {sessionUuid} from './ProductionSessionIdentity.ts';

// The native Session's exact page route scopes Model C paint and startup.
// Native transport and page identity still authorize every command.
export function isModelCSessionPage(search:string):boolean {
  const query=new URLSearchParams(search);
  const generation=query.get('generation'),session=query.get('session');
  return query.size===5
    &&query.get('mode')==='production'
    &&query.get('view')==='session'
    &&query.get('presentation')==='model-c-production'
    &&sessionUuid(generation)&&sessionUuid(session)
    &&generation!=='00000000-0000-0000-0000-000000000000'
    &&session!=='00000000-0000-0000-0000-000000000000';
}

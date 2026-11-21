import type {RuntimePlaybackSample} from './ProductionRuntimeFacade.ts';
import type {MainWindowSnapshot} from './ProductionSessionTypes.ts';
import type {SessionReadState} from './sessionReadProjection.ts';

// Authored project and editor selection remain the snapshot's facts. Runtime
// samples replace presentation facts only; never write back into that snapshot.
export function runtimeSessionProjection(snapshot:MainWindowSnapshot,sample:RuntimePlaybackSample|null):SessionReadState {
  return {project:snapshot.project,selectedPlaylistId:snapshot.selection.playlistId,selectedItemId:snapshot.selection.itemId,
    activePlaylistId:sample?.active.playlistId??null,activeItemId:sample?.active.itemId??null,
    outputMuted:sample?.outputMuted??false,
    runtime:sample?.hasMedia?{time:sample.position,duration:sample.duration,paused:sample.state!=='playing',rate:sample.actualRate,
      sceneIndex:sample.sceneIndex,sceneCount:sample.sceneCount,mediaKind:sample.mediaKind??'unknown'}:null};
}

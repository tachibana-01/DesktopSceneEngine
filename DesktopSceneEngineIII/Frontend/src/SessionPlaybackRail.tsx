import type { useSessionPrototype } from './useSessionPrototype';
import { sessionDisplay } from './sessionPresentationModel.ts';
import { displayFacts } from './sessionDisplayModel.ts';
import { SessionPlaybackRailView } from './SessionPlaybackRailView';

export function SessionPlaybackRail({ mock, notice }: { mock: ReturnType<typeof useSessionPrototype>; notice: string }) {
  return <SessionPlaybackRailView state={mock.state} notice={notice} display={displayFacts(sessionDisplay(mock.state), mock.lifecycle)} onPlayback={mock.playback} />;
}

import type { ReactNode } from 'react';
import type { useSessionPrototype } from './useSessionPrototype';
import { sessionDisplay, sessionView } from './sessionPresentationModel.ts';
import { displayFacts } from './sessionDisplayModel.ts';
import { SessionPreviewView } from './SessionPreviewView';

export function SessionPreview({ mock,onResetLayout,children }: { mock: ReturnType<typeof useSessionPrototype>;onResetLayout?:()=>void;children?:ReactNode }) {
  const {state}=mock, display=sessionDisplay(state), view=sessionView(state);
  return <SessionPreviewView state={state} facts={displayFacts(display,mock.lifecycle)} highlighted={view.highlighted} background={view.background}
    language={mock.language.effective} resetEpoch={mock.scenario+':'+mock.timelineEpoch} onResetLayout={onResetLayout} viewportOverride={state.previewMock?.viewport}
    onHighlight={value=>mock.presentation({scope:'view',action:{type:'highlight',value}})}
    onBackground={mode=>mock.presentation({scope:'view',action:{type:'background',mode}})}
    onIdentify={()=>mock.presentation({scope:'display',action:{type:'identify',status:state.previewMock?.identifyStatus,refresh:state.previewMock?.identifyRefresh}})}
    onDeploy={()=>mock.presentation({scope:'display',action:{type:'toggle-deployment'}})} identifyStatus={display.identifyStatus??undefined}
    diagnostics={{'data-host-owned':display.hostOwned,'data-target-registered':display.targetRegistered,'data-desired':display.desired,
      'data-background-persisted':view.persisted.backgrounds[display.boundId]??'missing','data-background-writes':view.backgroundWrites,
      'data-identify-requests':display.counters.identifyRequests,'data-identify-status':display.identifyStatus??'','data-last-deployment-result':display.lastResult??''}}>{children}</SessionPreviewView>;
}

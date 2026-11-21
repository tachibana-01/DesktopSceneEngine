import type { MainWindowSnapshot } from './ProductionSessionTypes.ts';
import type { displayFacts } from './sessionDisplayModel.ts';

export function snapshotDisplayFacts(snapshot:MainWindowSnapshot):ReturnType<typeof displayFacts> {
  const descriptor=snapshot.display.descriptor;
  const availability=snapshot.display.status!=='success'?'enumeration-failed':descriptor?'available':'unavailable';
  const label=availability==='enumeration-failed'?'Unable to enumerate displays':!descriptor?'Selected display unavailable'
    :`Display ${descriptor.displayNumber}${descriptor.friendlyName?' — '+descriptor.friendlyName:''} — ${descriptor.pixelWidth}×${descriptor.pixelHeight}${descriptor.primary?' (Primary)':''}`;
  const d=snapshot.desktop,retained=d.desired||d.targetRegistered||d.hostOwned,healthy=d.desired&&d.targetRegistered&&d.hostOwned&&d.hostHealthy;
  const canDeploy=d.canDeploy&&!d.busy,canUndeploy=d.canUndeploy&&!d.busy;
  return {boundId:snapshot.session.boundDisplayId,descriptor,availability,label,tooltip:label,retained,healthy,
    presentation:!retained?'preview-only':healthy?'deployed':'cleanup-required',deploymentLabel:retained?'Undeploy':'Deploy',
    deploymentEnabled:retained?canUndeploy:canDeploy,canIdentify:availability==='available',canDeploy,canUndeploy};
}

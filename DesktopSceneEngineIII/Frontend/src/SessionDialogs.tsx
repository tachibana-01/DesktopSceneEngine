import { useState, type ReactNode } from 'react';
import { documentName } from './sessionDocumentMock.ts';
import { findItem, findPlaylist } from './sessionEntities.ts';
import { EDITORIAL_PATH, INVALID_PATH, mockSources, SAVE_AS_PATH, STUDIO_PATH, WRONG_OWNER_PATH } from './sessionFixtures';
import type { SessionProjection } from './sessionTypes';
import type { useSessionPrototype } from './useSessionPrototype';
import { useSessionText } from './sessionLanguage';

import { SessionModal } from './SessionModal';
import { SessionDocumentDecision } from './SessionDocumentDecision';
export { SessionModal } from './SessionModal';

type SessionMock = ReturnType<typeof useSessionPrototype>;
export function SessionDecisionDialog({ mock }: { mock: SessionMock }) {
  const t = useSessionText();
  const dialog = mock.dialog;
  const [text, setText] = useState(() => {
    if (dialog?.kind !== 'rename') return '';
    return (dialog.entity === 'playlist' ? findPlaylist(mock.state, dialog.id)?.alias : findItem(mock.state, dialog.id)?.item.alias) ?? '';
  });
  const [path, setPath] = useState(() => dialog?.kind === 'open'
    ? mock.scenario === 'open-failure' ? INVALID_PATH : mock.scenario === 'wrong-owner' ? WRONG_OWNER_PATH : STUDIO_PATH
    : mock.state.document.path ?? SAVE_AS_PATH);
  const [sources, setSources] = useState<string[]>([mockSources[0].source]);
  if (!dialog) return null;
  const cancel = mock.dismiss;
  const actions = (children: ReactNode) => <div className="session-dialog-actions">{children}</div>;
  const cancelButton = <button type="button" data-dialog-cancel onClick={cancel}>{t('Cancel')}</button>;
  if (dialog.kind === 'configure-background-music') {
    const playlist = findPlaylist(mock.state, dialog.playlistId);
    return <SessionModal title={t('Configure Background Music')} onClose={cancel} initialFocus="[data-message-ok]">
      <p className="session-message" data-level="info" data-configure-playlist-id={dialog.playlistId}>{t('Playlist')} · {playlist?.alias}</p>
      <p className="session-dialog-note">Entry mock only. Background Music internals are outside this trial.</p>
      {actions(<button type="button" data-message-ok onClick={cancel}>{t('OK')}</button>)}
    </SessionModal>;
  }
  if (dialog.kind === 'background-warning') return <SessionModal title={t('Desktop Background')} onClose={cancel} dismissible={false} initialFocus="[data-message-ok]">
    <p className="session-message" data-level="warning">{t(mock.state.view?.warning ?? '')}</p>
    {actions(<button type="button" data-message-ok className="session-dialog-primary" onClick={mock.acknowledgeBackground}>{t('OK')}</button>)}
  </SessionModal>;
  if (dialog.kind === 'decision') return <SessionDocumentDecision name={mock.state.document.path ? documentName(mock.state) : t('Untitled')}
    onDecision={choice=>choice==='cancel'?cancel():mock.decide(choice,dialog.pending)}/>;
  if (dialog.kind === 'open' || dialog.kind === 'save-as') {
    const open = dialog.kind === 'open';
    const paths = [...new Set([open ? STUDIO_PATH : SAVE_AS_PATH, mock.state.document.path ?? SAVE_AS_PATH,
      EDITORIAL_PATH, STUDIO_PATH, INVALID_PATH, WRONG_OWNER_PATH, ...Object.keys(mock.state.files), 'C:/MockProjects/missing-parent/project.json'])];
    return <SessionModal title={t(open ? 'Open Project' : 'Save Project As')} onClose={cancel} initialFocus="[data-picker-value]">
      <p className="session-mock-picker-label">{t('MOCK FILE SELECTION')}</p>
      <label className="session-dialog-label" htmlFor="session-mock-path">{t('Project path')}</label>
      <select id="session-mock-path" data-picker-value value={path} onChange={event => setPath(event.target.value)}>
        {paths.map(value => <option key={value} value={value}>{value}</option>)}
      </select>
      <p className="session-dialog-note">{t('Predefined paths only. No files are read or written.')}</p>
      {actions(<>{cancelButton}<button type="button" className="session-dialog-primary" data-dialog-confirm
        onClick={() => open ? mock.open(path) : mock.saveAs(path, dialog.pending)}>{t(open ? 'Open' : mock.state.files[path] ? 'Save / Replace' : 'Save')}</button></>)}
    </SessionModal>;
  }
  if (dialog.kind === 'rename') return <SessionModal title={t(dialog.entity === 'playlist' ? 'Rename Playlist' : 'Rename Item')} onClose={cancel} initialFocus="[data-rename-value]">
    <form onSubmit={event => { event.preventDefault(); mock.setDialog(null); mock.entity({ type: dialog.entity === 'playlist' ? 'rename-playlist' : 'rename-item', id: dialog.id, alias: text }); }}>
      <label className="session-dialog-label" htmlFor="session-alias">{t('Name')}</label>
      <input type="text" id="session-alias" data-rename-value value={text} onChange={event => setText(event.target.value)} />
      {actions(<>{cancelButton}<button type="submit" className="session-dialog-primary" data-dialog-confirm>{t('OK')}</button></>)}
    </form>
  </SessionModal>;
  if (dialog.kind === 'delete') {
    const alias = dialog.entity === 'playlist' ? findPlaylist(mock.state, dialog.id)?.alias : findItem(mock.state, dialog.id)?.item.alias;
    return <SessionModal title={t(dialog.entity === 'playlist' ? 'Delete Playlist' : 'Remove Item')} onClose={cancel} initialFocus="[data-dialog-cancel]">
      <p>{t(dialog.entity === 'playlist' ? 'Delete playlist “%1”?' : 'Remove “%1” from the playlist?').replace('%1', () => String(alias))}</p>
      {dialog.entity === 'item' && <p className="session-dialog-note">{t('The source file will not be deleted.')}</p>}
      {actions(<><button type="button" data-dialog-confirm onClick={() => { mock.setDialog(null); mock.entity({ type: dialog.entity === 'playlist' ? 'delete-playlist' : 'delete-item', id: dialog.id }); }}>{t('Yes')}</button>{cancelButton}</>)}
    </SessionModal>;
  }
  if (dialog.kind === 'add-media') return <SessionModal title={t('Add Media')} onClose={cancel} initialFocus="input">
    <p className="session-mock-picker-label">{t('MOCK FILE SELECTION')}</p>
    <p className="session-dialog-note">{t('Selected Playlist')} · {findPlaylist(mock.state, mock.state.selectedPlaylistId)?.alias ?? t('None')}</p>
    <div className="session-source-options">{mockSources.map(source => <label key={source.source}>
      <input type="checkbox" value={source.source} checked={sources.includes(source.source)} onChange={event => {
        const checked = event.currentTarget.checked;
        setSources(previous => checked ? [...previous, source.source] : previous.filter(value => value !== source.source));
      }} />
      <span>{source.source.split('/').at(-1)}</span>
    </label>)}</div>
    {actions(<>{cancelButton}<button type="button" className="session-dialog-primary" data-dialog-confirm onClick={() => {
      mock.setDialog(null); mock.entity({ type: 'add-media', sources: mockSources.filter(source => sources.includes(source.source)) });
    }}>{t('Add')}</button></>)}
  </SessionModal>;
  return <SessionModal title={t(dialog.feedback.title)} onClose={cancel} initialFocus="[data-message-ok]">
    <p className="session-message" data-level={dialog.feedback.level}>{t(dialog.feedback.message)}</p>
    {actions(<button type="button" data-message-ok className="session-dialog-primary" onClick={cancel}>{t('OK')}</button>)}
  </SessionModal>;
}

export function projectionAttributes(state: SessionProjection) {
  return { 'data-selected-playlist-id': state.selectedPlaylistId ?? '', 'data-selected-item-id': state.selectedItemId ?? '',
    'data-active-playlist-id': state.activePlaylistId ?? '', 'data-active-item-id': state.activeItemId ?? '',
    'data-project-path': state.document.path ?? '', 'data-revision': state.document.revision,
    'data-clean-revision': state.document.cleanRevision, 'data-session-closed': state.closed };
}

import type { LauncherLanguage } from './ProductionLauncherFacade.ts';
import type { LauncherLanguageFeedback, LauncherSessionActionFeedback } from './ProductionLauncherController.ts';

export type LauncherLocale = 'en' | 'zh-CN';
export const launcherLocale = (language: LauncherLanguage): LauncherLocale => language === 'zh_CN' ? 'zh-CN' : 'en';
export const canonicalLauncherLanguage = (locale: LauncherLocale): LauncherLanguage => locale === 'zh-CN' ? 'zh_CN' : 'en';
export const launcherLanguageName = (language: LauncherLanguage): string => language === 'zh_CN' ? '简体中文' : 'English';

const en = {
  displays: 'Displays', display: 'Display', connected: 'connected',
  deck: 'Display deck', carousel: 'carousel', select: 'Select', navigation: 'Display navigation',
  previous: 'Previous display', next: 'Next display', choose: 'Choose a display', goTo: 'Go to', of: 'of',
  primary: 'Primary', secondary: 'Secondary', registered: 'Workspace open', unknown: 'Status unavailable',
  active: 'Open', disconnected: 'Display disconnected', closing: 'Closing', noRegisteredSession: 'Not open',
  workspace: 'Workspace', registeredSessions: 'Other workspaces', controlUnavailable: 'Can’t open this workspace right now.',
  closeUncertain: 'Check whether the workspace has closed.',
  resolutionUnknown: 'Display information is temporarily unavailable.', boundDisplayUnresolved: 'This workspace’s display is disconnected.',
  sessionRegistered: 'Return to this display’s workspace.',
  sessionUnknown: 'Open a workspace for this display.',
  launcherActions: 'Launcher actions', refresh: 'Refresh displays', refreshIncomplete: 'Refresh incomplete',
  enumerationFailed: 'Displays unavailable', refreshFailed: 'Couldn’t refresh displays', noDisplays: 'No displays found',
  enumerateFailed: 'Couldn’t find displays', refreshInvalid: 'The display list couldn’t be updated. Please try again.',
  apiFailure: 'Windows couldn’t read the display list.', metadataFailure: 'Windows hasn’t finished identifying the displays.',
  duplicateFailure: 'Some displays couldn’t be told apart.', catalogUnavailable: 'The display list is temporarily unavailable.',
  retry: 'RETRY', refreshing: 'Refreshing displays…',
  identify: 'Identify display', identifyPending: 'Identifying display…',
  identifyUnresolved: 'This display cannot currently be located.', identifyInvalid: 'The Identify request is invalid.',
  identifyFailure: 'Unable to show the Identify marker.', identifyTransport: 'The Identify request did not complete. Please retry.',
  create: 'Get started', open: 'Open workspace', starting: 'Starting',
  sendingCreate: 'Opening a workspace…', sendingOpen: 'Switching to the workspace…',
  createFailed: 'Couldn’t open a workspace.', createExited: 'The workspace closed before it opened.',
  createUncertain: 'Check whether a workspace opened before trying again.',
  createTransport: 'Opening the workspace didn’t finish. Check its status before trying again.',
  alreadyRegistered: 'A workspace is already open for this display.',
  notOwnerCapable: 'Workspaces can’t be opened on this display right now.',
  createUnresolved: 'This display is no longer connected.', createCapacity: 'Too many workspaces are opening. Try again in a moment.',
  sessionUnavailable: 'This workspace is no longer available.', openFailure: 'Couldn’t switch to the workspace.',
  openTransport: 'Couldn’t switch to the workspace. Please try again.',
  sessionClosing: 'This workspace is closing.', commandUnavailable: 'This action is temporarily unavailable.',
  languageReadFailure: 'Your saved language couldn’t be loaded. The current language is still in use.',
  effective: 'Effective language', committed: 'Committed', settings: 'SETTINGS', closeSettings: 'Close settings', language: 'LANGUAGE',
  cancel: 'Cancel', apply: 'Apply', ok: 'OK', saving: 'Saving…', changesOnApply: 'Changes take effect when applied.',
  committedUnavailable: 'Committed language unavailable',
  unchanged: 'The language preference is already saved.', saved: 'Language preference saved.',
  readFailure: 'Unable to read the saved language. Nothing was committed. Please retry.',
  saveFailure: 'Unable to save the language. The preference was not committed. Please retry.',
  commitTransport: 'The language request did not complete. Reopen Settings to read the current preference before retrying.',
  translationFallback: 'Preference saved. The application is using English because the translation catalog is unavailable.',
  notificationFailure: 'Preference saved. Other windows may not have received the language update.',
  bootstrapFailure: 'Couldn’t load the Launcher', bootstrapExplanation: 'Please try again.',
  connecting: 'Opening Launcher…', hostRequired: 'Open DesktopSceneEngine', hostExplanation: 'Start DesktopSceneEngine to see your displays.',
};
const zh: Record<keyof typeof en, string> = {
  displays: '显示器', display: '显示器', connected: '已连接',
  deck: '显示器卡片', carousel: '轮播', select: '选择', navigation: '显示器导航',
  previous: '上一台显示器', next: '下一台显示器', choose: '选择显示器', goTo: '转到', of: '/',
  primary: '主屏', secondary: '次屏', registered: '工作区已打开', unknown: '暂时无法查看状态',
  active: '已打开', disconnected: '显示器未连接', closing: '正在关闭', noRegisteredSession: '尚未打开',
  workspace: '工作区', registeredSessions: '其他工作区', controlUnavailable: '暂时无法打开此工作区。',
  closeUncertain: '请确认工作区是否已关闭。',
  resolutionUnknown: '暂时无法获取显示器信息。', boundDisplayUnresolved: '此工作区的显示器未连接。',
  sessionRegistered: '返回这块屏幕的工作区。',
  sessionUnknown: '在这块屏幕上打开一个工作区。',
  launcherActions: '启动器操作', refresh: '刷新显示器', refreshIncomplete: '刷新未完成',
  enumerationFailed: '显示器信息不可用', refreshFailed: '无法刷新显示器', noDisplays: '未检测到显示器',
  enumerateFailed: '无法获取显示器信息', refreshInvalid: '无法更新显示器列表，请重试。',
  apiFailure: '暂时无法读取 Windows 的显示器列表。', metadataFailure: 'Windows 还未完成显示器识别。',
  duplicateFailure: '暂时无法区分部分显示器。', catalogUnavailable: '显示器列表暂不可用。',
  retry: '重试', refreshing: '正在刷新显示器…',
  identify: '识别显示器', identifyPending: '正在识别显示器…',
  identifyUnresolved: '当前无法定位该显示器。', identifyInvalid: '识别请求无效。',
  identifyFailure: '无法显示识别标记。', identifyTransport: '识别请求未完成，请重试。',
  create: '开始使用', open: '打开工作区', starting: '正在启动',
  sendingCreate: '正在打开工作区…', sendingOpen: '正在切换到工作区…',
  createFailed: '无法打开工作区。', createExited: '工作区尚未打开便已关闭。',
  createUncertain: '请确认工作区是否已打开，再尝试一次。',
  createTransport: '打开工作区未完成，请先确认当前状态。',
  alreadyRegistered: '这块屏幕已有打开的工作区。',
  notOwnerCapable: '暂时无法在这块屏幕上打开工作区。',
  createUnresolved: '这块屏幕已断开连接。', createCapacity: '正在打开的工作区过多，请稍后再试。',
  sessionUnavailable: '此工作区已不可用。', openFailure: '无法切换到工作区。',
  openTransport: '无法切换到工作区，请重试。', sessionClosing: '此工作区正在关闭。',
  commandUnavailable: '此操作暂不可用。',
  languageReadFailure: '无法加载保存的语言，已保留当前语言。',
  effective: '当前语言', committed: '已提交', settings: '设置', closeSettings: '关闭设置', language: '语言',
  cancel: '取消', apply: '应用', ok: '确定', saving: '正在保存…', changesOnApply: '应用后更改才会生效。',
  committedUnavailable: '已提交语言不可用',
  unchanged: '语言偏好已保存，无需更改。', saved: '语言偏好已保存。',
  readFailure: '无法读取已保存的语言，未提交任何更改，请重试。',
  saveFailure: '无法保存语言，偏好尚未提交，请重试。',
  commitTransport: '语言请求未完成，请重新打开设置读取当前偏好后再重试。',
  translationFallback: '偏好已保存。由于翻译目录不可用，应用正在使用英语。',
  notificationFailure: '偏好已保存，其他窗口可能未收到语言更新。',
  bootstrapFailure: '无法加载启动器', bootstrapExplanation: '请重试。',
  connecting: '正在打开启动器…', hostRequired: '请打开 DesktopSceneEngine', hostExplanation: '启动 DesktopSceneEngine 后即可查看显示器。',
};
export function launcherCopy(language: LauncherLanguage): Record<keyof typeof en, string> {
  return launcherLocale(language) === 'zh-CN' ? zh : en;
}
export function launcherLanguageFeedback(feedback: LauncherLanguageFeedback | null, effective: LauncherLanguage): string {
  if (!feedback) return '';
  const copy = launcherCopy(effective);
  if (feedback.status === 'TransportFailure') return copy.commitTransport;
  if (feedback.status === 'READ_FAILURE') return copy.readFailure;
  if (feedback.status === 'SAVE_FAILURE') return copy.saveFailure;
  if (feedback.status === 'UNCHANGED') return copy.unchanged;
  return feedback.warnings.length ? feedback.warnings.map(warning => warning === 'TranslationFallback'
    ? copy.translationFallback : copy.notificationFailure).join(' ') : copy.saved;
}

export function launcherSessionActionMessage(feedback: LauncherSessionActionFeedback | null, effective: LauncherLanguage): string | null {
  if (!feedback) return null;
  const copy = launcherCopy(effective), status = feedback.result.status;
  if (feedback.kind === 'Create') {
    if (status === 'Accepted' || status === 'AlreadyPending') return null;
    if (status === 'AlreadyRegistered') return copy.alreadyRegistered;
    if (status === 'NotOwnerCapable') return copy.notOwnerCapable;
    if (status === 'Unresolved') return copy.createUnresolved;
    if (status === 'CatalogUnavailable') return copy.catalogUnavailable;
    if (status === 'CapacityExceeded') return copy.createCapacity;
    if (status === 'LaunchFailed') return copy.createFailed;
    if (status === 'TransportFailure') return copy.createTransport;
    return copy.commandUnavailable;
  }
  if (status === 'ActivationRequested') return null;
  if (status === 'RoleGone') return copy.sessionUnavailable;
  if (status === 'ControlUnavailable') return copy.controlUnavailable;
  if (status === 'Closing') return copy.sessionClosing;
  if (status === 'TransportFailure') return copy.openTransport;
  return copy.openFailure;
}

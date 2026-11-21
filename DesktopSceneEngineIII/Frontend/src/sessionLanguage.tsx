import {createContext,useContext,type ReactNode} from 'react';
import {productionChinese} from './sessionZh.ts';
import {sessionCopyZh} from './sessionCopyZh.ts';

// Accepted React shell copy absent from Qt's catalog uses the same product
// terminology. These labels do not define new product behavior.
const shellChinese:Record<string,string>={
  'Background Music':'背景音乐','Playlist Global BGM':'播放列表全局背景音乐','Item BGM':'项目背景音乐','Edit Item BGM':'编辑项目背景音乐',
  'Queue Mode':'队列模式','Queue Volume':'队列音量','Track Volume':'曲目音量','Queue Volume × Track Volume':'队列音量 × 曲目音量',
  'Current Single Loop':'当前曲目循环','List Loop':'列表循环','Shuffle':'随机播放','Enabled':'启用','Current':'当前曲目',
  'Starting':'正在启动','Playing':'正在播放','Paused':'已暂停','Waiting':'等待中','Not running':'未运行','Empty queue':'空队列',
  'No playable track':'无可播放曲目','No Current track':'没有当前曲目','No Item selected':'未选择项目','Loading runtime…':'正在读取运行状态…',
  'No music added':'尚未添加音乐','No track selected':'未选择曲目','Current track':'当前曲目','Selection does not start playback':'选择不会开始播放',
  'Use now':'立即使用','Remove':'移除','Add Music':'添加音乐','Failed attempt':'最近失败尝试','Native picker pending…':'原生文件选择进行中…',
  'Saved':'已保存','tracks':'曲目','Background music configuration':'背景音乐配置','Native BGM editor unavailable':'原生背景音乐编辑器不可用',
  'Picker cancelled':'已取消文件选择','Some sources were not added':'部分来源未添加','Native edit rejected':'原生编辑被拒绝','Native Use rejected':'原生使用请求被拒绝',
  'Authored change saved; runtime synchronization failed':'配置更改已保存，但运行同步失败',
  'Unavailable until a production Session is connected':'连接生产会话后才可使用',
  // Adjustment 003 presentation labels; policy IDs and Qt catalogs stay intact.
  'Random':'随机播放','Repeat Current':'单曲循环',
  'New':'新建','Open…':'打开…','Save':'保存','Save As…':'另存为…','Close Session':'关闭会话','Project commands':'项目命令',
  'Another operation is already in progress.':'当前操作尚未完成，请稍后再试。',
  'The volume change could not be confirmed. Try again.':'暂时无法确认音量更改，请重试。',
  'The View controls are currently unavailable.':'视图设置暂不可用。',
  'Deploying…':'正在部署…','Undeploying…':'正在取消部署…',
  'The desktop could not be deployed.':'部署失败，请重试。',
  'The desktop could not be undeployed.':'取消部署失败，请重试。',
  'The canvas highlight could not be changed.':'无法更改画布边界高亮，请重试。',
  'The desktop background could not be changed.':'无法更改桌面背景，请重试。',
  'Untitled':'未命名','Unsaved changes':'未保存的更改','INSPECTOR':'检查器','ITEM':'项目','PLAYLIST':'播放列表','PROJECT':'工程',
  'CANVAS':'画布','PREVIEW ONLY':'仅预览','DEPLOYED':'已部署','CLEANUP REQUIRED':'需要清理','VIEW':'视图',
  'No selection.':'未选择。','No playlists':'没有播放列表','No items':'没有项目','No playlist selected':'未选择播放列表',
  'No selection':'未选择','commands':'命令','playlist commands':'播放列表命令','item commands':'项目命令','Configure Background Music…':'配置背景音乐…',
  'Resize Library and Preview':'调整素材库与预览宽度','Resize Preview and Inspector':'调整预览与检查器宽度',
  'Native Preview surface placeholder, Canvas %1, mock only':'原生预览区域占位，画布 %1，仅模拟','Identify request':'识别请求','mock':'模拟',
  'SELECTED':'已选择','ACTIVE':'活动','NOT ACTIVE':'非活动','Disabled':'已禁用','Selected':'已选择','Active':'活动',
  'Items':'项目','Type':'类型','Source':'来源','Audio':'Audio','Configured Rate':'配置速率','Actual Rate':'实际速率',
  'Playlist Master Volume':'播放列表主音量','Image Duration':'图像显示时长',
  'Duration · s':'时长 · 秒','Apply Rate':'确定速率','Visual policy':'视觉策略','Policy':'策略','Aspect':'比例','Aspect preset':'比例预设',
  'Native':'原始比例','Auto Fill':'自动填充','Custom Aspect':'自定义比例','Custom':'自定义','Ratio':'比例',
  'Aspect numerator':'比例分子','Aspect denominator':'比例分母','Uniform scale':'等比缩放','Position X normalized':'归一化 X 位置',
  'Position Y normalized':'归一化 Y 位置','Positive X moves right':'正 X 向右移动','Positive Y moves up':'正 Y 向上移动',
  'POSITION':'位置','Playback position':'播放位置','Active runtime timeline':'活动播放时间线','Timeline hover preview':'时间线悬停预览',
  'PLAYING':'正在播放','PAUSED':'已暂停','NO MEDIA':'无媒体','MASTER':'主音量','Active Playlist Master':'活动播放列表主音量',
  'Session playback controls':'会话播放控制','Mock playback':'模拟播放','Mute output':'输出静音','Unmute output':'取消输出静音',
  'UNAVAILABLE':'不可用','ENUMERATION FAILED':'枚举失败','DISPLAY':'显示器','View':'视图','Cover Entire':'覆盖全部',
  'Reveal Wallpaper':'显示壁纸','DESKTOP OUTPUT':'桌面输出','Preview View preferences':'预览视图偏好','Identify bound Display':'识别绑定显示器',
  'NATIVE SURFACE · MOCK':'原生区域 · 模拟','PREVIEW SURFACE UNAVAILABLE':'预览区域不可用','Dismiss':'知道了',
  'Dismiss Desktop Background warning':'关闭桌面背景警告','Cancel':'取消','Discard':'放弃更改','Yes':'是','OK':'确定','Add':'添加',
  'Unsaved Changes':'未保存的更改','MOCK FILE SELECTION':'模拟文件选择','Project path':'项目路径',
  'Predefined paths only. No files are read or written.':'仅预设路径，不读取或写入文件。',
  'The source file will not be deleted.':'不会删除源文件。','Save / Replace':'保存 / 替换','None':'无',
  'Session closed.':'会话已关闭。','Mock close result. This browser remains open.':'模拟关闭结果，浏览器保持打开。',
  'Hide to tray':'隐藏到系统托盘','Window controls':'窗口控制','Minimize':'最小化','Maximize':'最大化','Restore':'还原','Close':'关闭',
  'Hide is temporarily unavailable':'暂时无法隐藏','Hidden Session · mock':'会话已隐藏 · 模拟','Restore Session · mock':'还原会话 · 模拟',
  'No restore eligibility':'没有可用的恢复路径','Modal interaction active':'模态交互进行中','Close pending':'正在等待关闭',
  'Session ending':'会话正在结束','Window unavailable':'窗口不可用',
  'Playlists and Items':'播放列表和项目','Project Playlists':'工程播放列表','Selected Playlist Items':'所选播放列表项目',
  'Project sequence policy':'工程播放策略','Playlist Item sequence policy':'播放列表项目播放策略',
  'Drag to preview time; release to seek. The exact right edge advances playback. Keyboard adjusts the thumb only, matching the current production slider.':'拖动预览时间，松开后定位。最右端前进到下一项。键盘只调整滑块，与当前产品一致。',
};
const chinese={...productionChinese,...shellChinese,...sessionCopyZh};
const patterns=Object.entries(chinese).filter(([key])=>/%\d/.test(key)).map(([source,translation])=>{
 const escaped=source.replace(/[.*+?^${}()|[\]\\]/g,'\\$&');
 return {expression:new RegExp('^'+escaped.replace(/%\d/g,'([\\s\\S]*?)')+'$'),translation,ordinals:[...source.matchAll(/%(\d)/g)].map(m=>m[1])};
});
export function translateSession(language:'en'|'zh_CN',text:string):string{
 if(language!=='zh_CN'||!text)return text;
 if(chinese[text])return chinese[text];
 for(const pattern of patterns){const match=text.match(pattern.expression);if(match)return pattern.translation.replace(/%(\d)/g,(_,n)=>match[pattern.ordinals.indexOf(n)+1]??'%'+n);}
 return text;
}
const LanguageContext=createContext<'en'|'zh_CN'>('en');
export function SessionLanguageProvider({language,children}:{language:'en'|'zh_CN';children:ReactNode}){return <LanguageContext.Provider value={language}>{children}</LanguageContext.Provider>;}
export function useSessionText(){const language=useContext(LanguageContext);return (text:string)=>translateSession(language,text);}
export function displayText(language:'en'|'zh_CN',label:string){return language==='en'?label:translateSession(language,label).replace(/^Display (\d+)/,'显示器 $1').replace(/\(Primary\)$/,'（主显示器）');}

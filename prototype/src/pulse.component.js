class Component extends DCLogic {
  static RAISED = 'inset -1px -1px #0A0A0A, inset 1px 1px #FFFFFF, inset -2px -2px #808080, inset 2px 2px #DFDFDF';
  static LIGHT = {
    '--page':'#EDF1EF','--page-ink':'#0E1E19','--page-ink2':'#4B635B','--page-track':'rgba(14,30,25,0.07)','--page-card':'rgba(255,255,255,0.72)','--page-line':'rgba(14,30,25,0.08)','--page-do':'#047857','--page-dont':'#BE123C',
    '--wall':'radial-gradient(42% 46% at 78% 30%, oklch(0.86 0.08 165 / 0.95), transparent 70%), radial-gradient(40% 40% at 88% 82%, oklch(0.9 0.08 75 / 0.95), transparent 70%), radial-gradient(45% 45% at 14% 80%, oklch(0.88 0.05 225 / 0.85), transparent 70%), radial-gradient(35% 35% at 30% 18%, oklch(0.92 0.04 20 / 0.8), transparent 70%), #E6ECE9',
    '--fly-bg':'rgba(245,248,246,0.82)','--fly-solid':'#F5F8F6','--fly-border':'rgba(14,30,25,0.08)',
    '--fly-shadow':'inset 0 0 0 1px rgba(255,255,255,0.85), 0 30px 70px -12px rgba(14,30,25,0.24), 0 4px 14px rgba(14,30,25,0.06)',
    '--card':'rgba(255,255,255,0.78)','--card-hover':'rgba(255,255,255,0.96)','--card-b':'rgba(14,30,25,0.06)','--card-sh':'0 2px 10px rgba(14,30,25,0.04)','--card-sh-hover':'0 8px 22px rgba(14,30,25,0.09)',
    '--ink':'#0E1E19','--ink2':'#4B635B','--ink3':'#5E746C','--track':'rgba(14,30,25,0.07)','--track-strong':'rgba(14,30,25,0.2)','--hover':'rgba(14,30,25,0.045)','--hair':'rgba(14,30,25,0.1)',
    '--menubar':'rgba(248,250,249,0.62)','--taskbar':'rgba(240,244,242,0.8)','--pod':'rgba(255,255,255,0.6)','--press':'rgba(14,30,25,0.1)','--ph':'rgba(14,30,25,0.13)',
    '--tip-bg':'rgba(255,255,255,0.94)','--tip-b':'rgba(14,30,25,0.06)','--tip-sh':'0 14px 36px rgba(14,30,25,0.16)','--seg-pill':'#FFFFFF','--seg-sh':'0 1px 3px rgba(14,30,25,0.14)',
    '--badge-bg':'linear-gradient(150deg,#FFFFFF,#E2F2EB)','--btn-sh':'none','--field-sh':'none','--chart-bg':'transparent','--sp-top':'0.28','--sp-bot':'0',
    '--cpu':'#10B981','--mem':'#0EA5E9','--nrg':'#F59E0B','--thm':'#F43F5E','--gpu':'#14B8A6','--warn':'#F97316','--crit':'#EF4444',
    '--cpu-ink':'#047857','--mem-ink':'#0369A1','--nrg-ink':'#B45309','--thm-ink':'#BE123C','--gpu-ink':'#0F766E','--warn-ink':'#C2410C','--crit-ink':'#B91C1C',
    '--thm-tint-solid':'#FFF0F2','--danger':'#DC2626','--danger-on':'#FFFFFF'
  };
  static DARK = {
    '--page':'#08100D','--page-ink':'#ECFDF5','--page-ink2':'rgba(236,253,245,0.68)','--page-track':'rgba(255,255,255,0.08)','--page-card':'rgba(255,255,255,0.05)','--page-line':'rgba(255,255,255,0.09)','--page-do':'#34D399','--page-dont':'#FB7185',
    '--wall':'radial-gradient(42% 46% at 78% 30%, oklch(0.36 0.08 165 / 0.9), transparent 70%), radial-gradient(40% 40% at 88% 82%, oklch(0.33 0.06 85 / 0.8), transparent 70%), radial-gradient(45% 45% at 14% 80%, oklch(0.32 0.06 225 / 0.8), transparent 70%), radial-gradient(35% 35% at 30% 18%, oklch(0.28 0.04 20 / 0.7), transparent 70%), #0A1310',
    '--fly-bg':'rgba(12,21,18,0.80)','--fly-solid':'#0F1A16','--fly-border':'rgba(167,243,208,0.12)',
    '--fly-shadow':'inset 0 1px 0 rgba(255,255,255,0.06), 0 30px 80px -10px rgba(0,0,0,0.6)',
    '--card':'rgba(255,255,255,0.055)','--card-hover':'rgba(255,255,255,0.09)','--card-b':'rgba(255,255,255,0.08)','--card-sh':'none','--card-sh-hover':'0 8px 22px rgba(0,0,0,0.35)',
    '--ink':'#ECFDF5','--ink2':'rgba(236,253,245,0.68)','--ink3':'rgba(236,253,245,0.52)','--track':'rgba(255,255,255,0.08)','--track-strong':'rgba(255,255,255,0.22)','--hover':'rgba(255,255,255,0.05)','--hair':'rgba(255,255,255,0.1)',
    '--menubar':'rgba(12,21,18,0.55)','--taskbar':'rgba(14,24,20,0.82)','--pod':'rgba(255,255,255,0.08)','--press':'rgba(255,255,255,0.14)','--ph':'rgba(255,255,255,0.14)',
    '--tip-bg':'rgba(20,32,28,0.94)','--tip-b':'rgba(255,255,255,0.08)','--tip-sh':'0 14px 36px rgba(0,0,0,0.5)','--seg-pill':'rgba(255,255,255,0.16)','--seg-sh':'none',
    '--badge-bg':'linear-gradient(150deg, rgba(255,255,255,0.12), rgba(255,255,255,0.03))','--btn-sh':'none','--field-sh':'none','--chart-bg':'transparent','--sp-top':'0.28','--sp-bot':'0',
    '--cpu':'#34D399','--mem':'#38BDF8','--nrg':'#FBBF24','--thm':'#FB7185','--gpu':'#2DD4BF','--warn':'#FB923C','--crit':'#F87171',
    '--cpu-ink':'#34D399','--mem-ink':'#38BDF8','--nrg-ink':'#FBBF24','--thm-ink':'#FB7185','--gpu-ink':'#2DD4BF','--warn-ink':'#FDBA74','--crit-ink':'#FCA5A5',
    '--thm-tint-solid':'rgba(251,113,133,0.16)','--danger':'#F87171','--danger-on':'#2A0E00'
  };
  static CLASSIC = {
    xp: {
      '--wall':'linear-gradient(180deg, #3A78D4 0%, #79A9E6 52%, #9CC57A 53%, #5E9A35 76%, #41782A 100%)',
      '--fly-bg':'#ECE9D8','--fly-solid':'#ECE9D8','--fly-border':'#0831D9','--fly-shadow':'2px 3px 10px rgba(0,0,0,0.35)',
      '--card':'#FFFFFF','--card-hover':'#F3F7FF','--card-b':'#ACA899','--card-sh':'none','--card-sh-hover':'0 2px 6px rgba(0,0,0,0.12)',
      '--ink':'#000000','--ink2':'#3F3F3F','--ink3':'#5A5A5A','--track':'#E5E2D3','--track-strong':'#ACA899','--hover':'#E8EFFC','--hair':'#ACA899',
      '--tip-bg':'#FFFFE1','--tip-b':'#000000','--tip-sh':'2px 2px 4px rgba(0,0,0,0.3)','--seg-pill':'#FFFFFF','--seg-sh':'0 0 0 1px #7F9DB9',
      '--badge-bg':'#FFFFFF','--btn-sh':'none','--field-sh':'none','--chart-bg':'transparent','--press':'rgba(255,255,255,0.2)',
      '--cpu':'#1FA055','--mem':'#2B7BD6','--nrg':'#E8A317','--thm':'#D9344F','--gpu':'#1D9A8E','--warn':'#F27B1E','--crit':'#D93A2B',
      '--cpu-ink':'#13703A','--mem-ink':'#1A4F96','--nrg-ink':'#8A5A00','--thm-ink':'#A21C33','--gpu-ink':'#106B62','--warn-ink':'#A84A00','--crit-ink':'#A3261A',
      '--thm-tint-solid':'#FDECEE','--danger':'#D93A2B','--danger-on':'#FFFFFF'
    },
    w98: {
      '--wall':'#008080',
      '--fly-bg':'#C0C0C0','--fly-solid':'#C0C0C0','--fly-border':'#DFDFDF','--fly-shadow':'inset -1px -1px #0A0A0A, inset 1px 1px #FFFFFF, inset -2px -2px #808080, inset 2px 2px #DFDFDF',
      '--card':'#C0C0C0','--card-hover':'#CBCBCB','--card-b':'transparent','--card-sh':'inset -1px -1px #0A0A0A, inset 1px 1px #FFFFFF, inset -2px -2px #808080, inset 2px 2px #DFDFDF','--card-sh-hover':'inset -1px -1px #0A0A0A, inset 1px 1px #FFFFFF, inset -2px -2px #808080, inset 2px 2px #DFDFDF',
      '--ink':'#000000','--ink2':'#202020','--ink3':'#404040','--track':'#FFFFFF','--track-strong':'#808080','--hover':'rgba(0,0,128,0.1)','--hair':'#808080',
      '--tip-bg':'#FFFFE1','--tip-b':'#000000','--tip-sh':'none','--seg-pill':'#DFDFDF','--seg-sh':'inset -1px -1px #0A0A0A, inset 1px 1px #FFFFFF, inset -2px -2px #808080',
      '--badge-bg':'#C0C0C0','--btn-sh':'inset -1px -1px #0A0A0A, inset 1px 1px #FFFFFF, inset -2px -2px #808080, inset 2px 2px #DFDFDF',
      '--field-sh':'inset 1px 1px #808080, inset -1px -1px #FFFFFF, inset 2px 2px #0A0A0A, inset -2px -2px #DFDFDF','--chart-bg':'#FFFFFF','--press':'rgba(0,0,0,0.08)','--sp-top':'0.16','--sp-bot':'0.16',
      '--cpu':'#008A45','--mem':'#0050C8','--nrg':'#C88A00','--thm':'#C8102E','--gpu':'#008080','--warn':'#F27B1E','--crit':'#D00000',
      '--cpu-ink':'#005A2B','--mem-ink':'#000080','--nrg-ink':'#6B4500','--thm-ink':'#8B0020','--gpu-ink':'#005555','--warn-ink':'#7A2E00','--crit-ink':'#8B0000',
      '--thm-tint-solid':'#C0C0C0','--danger':'#C0C0C0','--danger-on':'#000000'
    }
  };
  static PLAT = {
    mac: { rFly:'22px', rCard:'14px', rBtn:'8px', rBadge:'8px', rRow:'9px', rPill:'999px', rMono:'7px', rTip:'12px', rLogo:'10px', hero:'ui-rounded, "SF Pro Rounded", "Nunito", "Plus Jakarta Sans", system-ui, sans-serif', ui:'-apple-system, BlinkMacSystemFont, "SF Pro Text", "Plus Jakarta Sans", system-ui, sans-serif', top:'36px', bottom:'auto', end:'10px', origin:'top', reserve:'42px' },
    win: { rFly:'12px', rCard:'8px', rBtn:'6px', rBadge:'6px', rRow:'6px', rPill:'999px', rMono:'6px', rTip:'8px', rLogo:'8px', hero:'"Segoe UI Variable Display", "Segoe UI Variable", "Plus Jakarta Sans", "Segoe UI", sans-serif', ui:'"Segoe UI Variable Text", "Segoe UI Variable", "Plus Jakarta Sans", "Segoe UI", sans-serif', top:'auto', bottom:'54px', end:'12px', origin:'bottom', reserve:'60px' },
    chrome: { rFly:'24px', rCard:'20px', rBtn:'999px', rBadge:'10px', rRow:'12px', rPill:'999px', rMono:'8px', rTip:'16px', rLogo:'12px', hero:'"Google Sans", "Google Sans Flex", "Plus Jakarta Sans", sans-serif', ui:'"Google Sans Text", "Google Sans", "Plus Jakarta Sans", sans-serif', top:'auto', bottom:'60px', end:'8px', origin:'bottom', reserve:'66px' },
    linux: { rFly:'18px', rCard:'12px', rBtn:'999px', rBadge:'8px', rRow:'9px', rPill:'999px', rMono:'7px', rTip:'12px', rLogo:'10px', hero:'"Ubuntu", "Cantarell", "Nunito", "Plus Jakarta Sans", sans-serif', ui:'"Cantarell", "Ubuntu", "Noto Sans", "Plus Jakarta Sans", sans-serif', top:'36px', bottom:'auto', end:'8px', origin:'top', reserve:'42px' },
    xp: { rFly:'8px 8px 0 0', rCard:'3px', rBtn:'3px', rBadge:'3px', rRow:'2px', rPill:'3px', rMono:'3px', rTip:'0', rLogo:'4px', hero:'Tahoma, Verdana, sans-serif', ui:'Tahoma, Verdana, sans-serif', top:'auto', bottom:'34px', end:'6px', origin:'bottom', reserve:'40px' },
    w98: { rFly:'0', rCard:'0', rBtn:'0', rBadge:'0', rRow:'0', rPill:'0', rMono:'0', rTip:'0', rLogo:'0', hero:'Tahoma, "MS Sans Serif", Verdana, sans-serif', ui:'Tahoma, "MS Sans Serif", Verdana, sans-serif', top:'auto', bottom:'32px', end:'4px', origin:'bottom', reserve:'38px' }
  };
  // Localized labels are { k: '<hardware key>', p: { params } } and resolve
  // through the active locale file. Plain strings are technical identifiers
  // that stay as written in every language (model names, units, interfaces).
  static PROF = (() => {
    const H = (k, p) => ({ k, p });
    return {
      mac: { coreA: H('coreP'), coreB: H('coreE'), share: ['P', 'E'], cores: { c: 12 },
        mem: { total: 16, u: 'GB', d: 1, seg: [6.1, 2.0, 1.2], l2: H('wired'), l3: H('compressed'), x1: [H('swapUsed'), '0.4 GB'], x2: [H('pressure'), H('normal')] },
        disk: { name: 'Macintosh HD', used: '240 GB', free: '248 GB', fmt: 'APFS', k: 1 },
        net: { name: 'Wi-Fi', iface: 'Wi-Fi 6E', sub: H('netSub', { link: 'Wi-Fi 6E', detail: '5 GHz', state: H('strongSignal') }), sig: [H('signal'), '−52 dBm'], lat: '18 ms', today: '↓ 1.4 GB · ↑ 212 MB', mode: 'mb' },
        gpu: { name: 'M3 Pro · 18-core GPU', t: 31, vram: H('vramUnified', { size: '1.6 GB' }), clock: '1.38 GHz', power: '4.1 W' },
        fans: H('fansOff', { rpm: '0 RPM' }), monitor: H('activityMonitor'), bar: H('menuBar') },
      win: { coreA: H('coreP'), coreB: H('coreE'), share: ['P', 'E'], cores: { c: 14, t: 20 },
        mem: { total: 16, u: 'GB', d: 1, seg: [6.1, 2.0, 1.2], l2: H('system'), l3: H('compressed'), x1: [H('committed'), '11.2 / 24 GB'], x2: [H('pressure'), H('normal')] },
        disk: { name: H('localDisk', { drive: 'C:' }), used: '240 GB', free: '248 GB', fmt: 'NTFS', k: 1 },
        net: { name: 'Wi-Fi', iface: 'Wi-Fi 6E', sub: H('netSub', { link: 'Wi-Fi 6E', detail: '5 GHz', state: H('strongSignal') }), sig: [H('signal'), '−54 dBm'], lat: '21 ms', today: '↓ 1.4 GB · ↑ 212 MB', mode: 'mb' },
        gpu: { name: 'Intel Arc Graphics', t: 44, vram: H('vramShared', { size: '2.1 / 8 GB' }), clock: '2.25 GHz', power: '11 W' },
        fans: '1,180 RPM', monitor: H('taskManager'), bar: H('taskbar') },
      chrome: { coreA: H('coreP'), coreB: H('coreE'), share: ['P', 'E'], cores: { c: 8 },
        mem: { total: 16, u: 'GB', d: 1, seg: [6.1, 2.0, 1.2], l2: H('system'), l3: 'ZRAM', x1: null, x2: null },
        disk: { name: H('internalStorage'), used: '240 GB', free: '248 GB', fmt: 'ext4', k: 1 },
        net: { name: 'Wi-Fi', iface: 'Wi-Fi 6', sub: H('netSub', { link: 'Wi-Fi 6', detail: '5 GHz', state: H('strongSignal') }), sig: [H('signal'), '−55 dBm'], lat: '19 ms', today: '↓ 1.4 GB · ↑ 212 MB', mode: 'mb' },
        gpu: { name: 'Mali-G57 MC2', t: 36, vram: H('vramShared', { size: '0.6 GB' }), clock: '950 MHz', power: '2.0 W' },
        fans: H('fanless'), monitor: H('diagnostics'), bar: H('shelf') },
      linux: { coreA: H('coreZen4'), coreB: H('coreZen4c'), share: ['Zen 4', 'Zen 4c'], cores: { c: 8, t: 16 },
        mem: { total: 16, u: 'GB', d: 1, seg: [6.1, 2.0, 1.2], l2: H('buffers'), l3: 'zswap', x1: [H('swapUsed'), '0.5 / 8 GB'], x2: [H('psiPressure'), H('pctNormal', { pct: '0.4%' })] },
        disk: { name: H('fsRoot', { path: '/' }), used: '240 GB', free: '248 GB', fmt: 'btrfs', k: 1 },
        net: { name: 'Wi-Fi · wlp1s0', iface: 'wlp1s0 · Wi-Fi 6E', sub: H('netSub', { link: 'wlp1s0', detail: 'Wi-Fi 6E', state: H('strongSignal') }), sig: [H('signal'), '−53 dBm'], lat: '17 ms', today: '↓ 1.4 GB · ↑ 212 MB', mode: 'mb' },
        gpu: { name: 'AMD Radeon 780M', t: 42, vram: '1.2 / 4 GB', clock: '2.7 GHz', power: '9.6 W' },
        fans: '1,050 RPM', monitor: H('systemMonitor'), bar: H('topBar') },
      xp: { coreA: H('coreN', { n: 0 }), coreB: H('coreN', { n: 1 }), share: ['C0', 'C1'], cores: { c: 2, model: 'Core 2 Duo' },
        mem: { total: 2, u: 'GB', d: 2, seg: [0.74, 0.28, 0.14], l2: H('kernel'), l3: H('cache'), x1: [H('pageFile'), '0.6 GB'], x2: [H('commitCharge'), '1.4 / 3.9 GB'] },
        disk: { name: H('localDisk', { drive: 'C:' }), used: '39 GB', free: '41 GB', fmt: 'NTFS', k: 0.45 },
        net: { name: H('lanConnection'), iface: 'Ethernet', sub: H('netSub', { link: 'Ethernet', detail: '100 Mbps', state: H('connected') }), sig: [H('linkSpeed'), '100 Mbps'], lat: '32 ms', today: '↓ 640 MB · ↑ 58 MB', mode: 'lan' },
        gpu: { name: 'GeForce 6600 GT', t: 58, vram: '128 MB', clock: '500 MHz', power: '48 W' },
        fans: '2,400 RPM', monitor: H('taskManager'), bar: H('notificationArea') },
      w98: { single: true, coreA: H('user'), coreB: H('kernel'), share: null, cores: { c: 1, model: 'Pentium II 400' },
        mem: { total: 128, u: 'MB', d: 0, seg: [46, 18, 10], l2: H('kernel'), l3: H('diskCache'), x1: [H('swapFile'), '24 MB'], x2: [H('sysResources'), H('pctFree', { pct: '71%' })] },
        disk: { name: H('drive', { drive: 'C:' }), used: '4.9 GB', free: '5.1 GB', fmt: 'FAT32', k: 0.07 },
        net: { name: H('dialUp'), iface: '56K modem', sub: H('netSub', { link: H('modem', { speed: '56K' }), detail: '53.3 kbps', state: H('connected') }), sig: [H('lineSpeed'), '53.3 kbps'], lat: '180 ms', today: '↓ 4.2 MB · ↑ 0.6 MB', mode: 'dial' },
        gpu: { name: 'Voodoo3 3000', t: 47, vram: '16 MB', clock: '166 MHz', power: '15 W' },
        fans: '2,800 RPM', monitor: H('systemMonitor'), bar: H('notificationArea') }
    };
  })();

  // App names are the English originals; locales translate them under "apps".
  static APPS = {
    mac: [
      {id:'xcode',name:'Xcode',p:9,cpu:52.4,mem:3.12,gpu:4,h:250,hog:true,pid:4182,th:84},
      {id:'safari',name:'Safari',p:14,cpu:5.3,mem:1.84,gpu:12,h:220,pid:812,th:142},
      {id:'figma',name:'Figma',p:6,cpu:3.6,mem:1.21,gpu:22,h:25,pid:2210,th:61},
      {id:'ws',name:'WindowServer',p:1,cpu:2.4,mem:0.62,gpu:18,h:165,bg:true,pid:398,th:24,mono:'W'},
      {id:'spot',name:'Spotlight',p:4,cpu:1.1,mem:0.38,gpu:0,h:290,bg:true,pid:521,th:18},
      {id:'slack',name:'Slack',p:5,cpu:0.7,mem:0.74,gpu:1,h:330,pid:1904,th:40}
    ],
    win: [
      {id:'msmpeng',name:'Antimalware Service',p:1,cpu:50.8,mem:0.41,gpu:0,h:200,bg:true,hog:true,pid:5124,th:52},
      {id:'edge',name:'Microsoft Edge',p:14,cpu:5.6,mem:1.92,gpu:9,h:215,pid:7740,th:156},
      {id:'figmaw',name:'Figma',p:6,cpu:3.3,mem:1.18,gpu:24,h:25,pid:9032,th:58},
      {id:'dwm',name:'Desktop Window Manager',p:1,cpu:2.2,mem:0.33,gpu:21,h:165,bg:true,pid:1288,th:22},
      {id:'teams',name:'Microsoft Teams',p:8,cpu:1.6,mem:1.06,gpu:3,h:265,pid:6610,th:77},
      {id:'spotify',name:'Spotify',p:5,cpu:0.6,mem:0.41,gpu:1,h:150,pid:3348,th:36}
    ],
    chrome: [
      {id:'crostini',name:'Linux (Crostini)',p:22,cpu:51.6,mem:1.42,gpu:2,h:45,hog:true,pid:2901,th:96,mono:'L'},
      {id:'chromeb',name:'Chrome',p:14,cpu:6.1,mem:1.88,gpu:11,h:220,pid:1402,th:131},
      {id:'arcvm',name:'Android (ARCVM)',p:31,cpu:3.0,mem:1.12,gpu:6,h:140,pid:3318,th:118,mono:'A'},
      {id:'ash',name:'System UI',p:3,cpu:1.9,mem:0.44,gpu:14,h:165,bg:true,pid:611,th:29},
      {id:'files',name:'Files',p:1,cpu:0.8,mem:0.21,gpu:0,h:250,pid:2077,th:12},
      {id:'meet',name:'Google Meet',p:2,cpu:0.6,mem:0.36,gpu:5,h:100,pid:2490,th:21}
    ],
    linux: [
      {id:'tracker',name:'tracker-miner-fs-3',p:3,cpu:53.2,mem:0.48,gpu:0,h:260,bg:true,hog:true,pid:2316,th:14,mono:'T'},
      {id:'firefox',name:'Firefox',p:18,cpu:5.8,mem:2.04,gpu:10,h:30,pid:3480,th:211},
      {id:'code',name:'Visual Studio Code',p:11,cpu:3.1,mem:1.36,gpu:5,h:230,pid:4102,th:96},
      {id:'gshell',name:'gnome-shell',p:1,cpu:2.3,mem:0.52,gpu:23,h:165,bg:true,pid:1720,th:28,mono:'G'},
      {id:'docker',name:'dockerd',p:6,cpu:1.2,mem:0.61,gpu:0,h:215,bg:true,pid:980,th:42,mono:'D'},
      {id:'spotifyl',name:'Spotify (Flatpak)',p:5,cpu:0.6,mem:0.39,gpu:1,h:150,pid:5512,th:36}
    ],
    xp: [
      {id:'svchost',name:'svchost.exe',p:12,cpu:54.1,mem:0.092,gpu:0,h:210,bg:true,hog:true,pid:1048,th:71,mono:'S'},
      {id:'iexplore',name:'Internet Explorer',p:6,cpu:5.9,mem:0.142,gpu:3,h:215,pid:3120,th:38},
      {id:'wmp',name:'Windows Media Player',p:1,cpu:3.1,mem:0.061,gpu:12,h:30,pid:2204,th:19},
      {id:'explorer',name:'explorer.exe',p:1,cpu:1.4,mem:0.034,gpu:4,h:60,bg:true,pid:1544,th:16,mono:'E'},
      {id:'msn',name:'MSN Messenger',p:2,cpu:0.8,mem:0.024,gpu:1,h:140,pid:2780,th:12},
      {id:'winamp',name:'Winamp',p:1,cpu:0.6,mem:0.012,gpu:0,h:80,pid:2916,th:8}
    ],
    w98: [
      {id:'nav',name:'Norton AntiVirus',p:3,cpu:56.3,mem:0.0112,gpu:0,h:55,bg:true,hog:true,pid:'FFF4A2B1',th:9},
      {id:'ie5',name:'Internet Explorer 5',p:1,cpu:4.8,mem:0.0181,gpu:6,h:215,pid:'FFF51C2D',th:7},
      {id:'winamp98',name:'Winamp',p:1,cpu:2.9,mem:0.0042,gpu:2,h:80,pid:'FFF5E0A9',th:3},
      {id:'expl98',name:'Explorer',p:1,cpu:1.3,mem:0.0061,gpu:3,h:60,bg:true,pid:'FFF7B3C1',th:5},
      {id:'icq',name:'ICQ',p:1,cpu:0.9,mem:0.0052,gpu:0,h:130,pid:'FFF6D411',th:4},
      {id:'mspaint',name:'Paint',p:1,cpu:0.4,mem:0.0031,gpu:1,h:300,pid:'FFF4F7E0',th:2}
    ]
  };

  state = this.init();

  rng(seed) { let a = seed >>> 0; return () => { a |= 0; a = a + 0x6D2B79F5 | 0; let t = Math.imul(a ^ a >>> 15, 1 | a); t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t; return ((t ^ t >>> 14) >>> 0) / 4294967296; }; }
  hash(s) { s = String(s); let h = 2166136261; for (let i = 0; i < s.length; i++) { h ^= s.charCodeAt(i); h = Math.imul(h, 16777619); } return h >>> 0; }
  walk(n, base, vol, lo, hi, r) { let v = base; const a = []; for (let i = 0; i < n; i++) { v += (r() - 0.5) * vol + (base - v) * 0.12; v = Math.max(lo, Math.min(hi, v)); a.push(v); } return a; }
  bump(a, at, h, w) { return a.map((v, i) => v + h * Math.exp(-Math.pow((i - at) / w, 2))); }

  init() {
    const r = this.rng(11);
    const hog = ((this.props || {}).hogAlert ?? true) !== false;
    const cpuB = hog ? 66 : 16;
    return {
      platform: 'mac', theme: 'light', lang: 'en', langSys: false, langMenu: false, open: true, quit: false, view: null, sort: 'cpu', hoverRow: -1, ended: {}, hogDismissed: false, chartHover: null,
      sim: {}, unitF: false, confirm: null, toast: null, jit: {},
      live: { cpu: cpuB, ram: 58, watt: hog ? 14.8 : 9.4, temp: hog ? 52 : 33, down: 1.8, up: 140, read: 42.1, write: 8.6, gpu: 14, gpuD: 0 },
      spark: { cpu: this.walk(60, cpuB, 7, 3, 92, r), mem: this.walk(60, 58, 1.4, 54, 62, r), nrg: this.walk(60, hog ? 14.8 : 9.4, 1.8, 5, 22, r), thm: this.walk(60, hog ? 52 : 33, 0.9, 29, 60, r), gpu: this.walk(60, 14, 7, 2, 45, r) }
    };
  }

  componentDidMount() { this.iv = setInterval(() => this.tick(), 1500); }
  componentWillUnmount() { clearInterval(this.iv); clearTimeout(this.tt); }

  cfg(s, k, def) { return s.sim[k] !== undefined ? s.sim[k] : (this.props[k] ?? def); }
  hogActive(s) {
    if (this.cfg(s, 'hogAlert', true) === false) return false;
    const h = Component.APPS[s.platform].find(a => a.hog);
    return !!h && !s.ended[h.id];
  }

  tick() {
    this.setState(s => {
      if (this.cfg(s, 'liveUpdates', true) === false) return null;
      const L = { ...s.live }, hog = this.hogActive(s);
      const j = (v, b, vol, lo, hi) => Math.max(lo, Math.min(hi, v + (Math.random() - 0.5) * vol + (b - v) * 0.3));
      L.cpu = j(L.cpu, hog ? 66 : 16, 5, 3, 95); L.ram = j(L.ram, 58, 0.6, 52, 64); L.watt = j(L.watt, hog ? 14.8 : 9.4, 1.0, 4, 24);
      L.temp = j(L.temp, hog ? 52 : 33, 0.6, 28, 70); L.down = j(L.down, 1.8, 0.8, 0.1, 6); L.up = j(L.up, 140, 50, 20, 600);
      L.read = j(L.read, 42, 18, 0, 160); L.write = j(L.write, 8.6, 5, 0, 60); L.gpu = j(L.gpu, 14, 7, 2, 60); L.gpuD = j(L.gpuD, 0, 0.8, -3, 4);
      const jit = {};
      Component.APPS[s.platform].forEach(a => { jit[a.id] = Math.max(-0.14, Math.min(0.14, (s.jit[a.id] || 0) * 0.6 + (Math.random() - 0.5) * 0.14)); });
      const push = (a, v) => [...a.slice(1), v];
      return { live: L, jit, spark: { cpu: push(s.spark.cpu, L.cpu), mem: push(s.spark.mem, L.ram), nrg: push(s.spark.nrg, L.watt), thm: push(s.spark.thm, L.temp), gpu: push(s.spark.gpu, L.gpu) } };
    });
  }

  smooth(p) {
    const f = x => x.toFixed(1);
    let d = `M${f(p[0][0])},${f(p[0][1])}`;
    for (let i = 0; i < p.length - 1; i++) {
      const p0 = p[i - 1] || p[i], p1 = p[i], p2 = p[i + 1], p3 = p[i + 2] || p2;
      d += ` C${f(p1[0] + (p2[0] - p0[0]) / 6)},${f(p1[1] + (p2[1] - p0[1]) / 6)} ${f(p2[0] - (p3[0] - p1[0]) / 6)},${f(p2[1] - (p3[1] - p1[1]) / 6)} ${f(p2[0])},${f(p2[1])}`;
    }
    return d;
  }

  spark(vals, W = 172, H = 28) {
    const n = vals.length, mn = Math.min(...vals), mx = Math.max(...vals), rg = Math.max(mx - mn, 0.5);
    const lo = mn - rg * 0.25, hi = mx + rg * 0.35;
    const pts = vals.map((v, i) => [i / (n - 1) * W, H - 2 - (v - lo) / (hi - lo) * (H - 4)]);
    const line = this.smooth(pts);
    return { line, area: `${line} L${W},${H} L0,${H} Z` };
  }

  series(key, base, hog) {
    this._ser = this._ser || {};
    if (this._ser[key]) return this._ser[key];
    const r = this.rng(this.hash(key)), N = 121;
    const ramp = (o, add) => o.map((v, i) => i > N - 38 ? v + add * Math.min(1, (i - (N - 38)) / 4) : v);
    let o;
    if (key === 'cpu:1') o = ramp(this.bump(this.walk(N, 16, 6, 4, 40, r), 50, 22, 3), 48);
    else if (key === 'cpu:0') o = this.bump(this.walk(N, 16, 6, 4, 40, r), 50, 22, 3);
    else if (key === 'mem') o = this.bump(this.walk(N, 57, 1.2, 52, 62, r), 80, 2.5, 6);
    else if (key === 'nrg:1') o = ramp(this.walk(N, 9.4, 1.6, 4, 16, r), 5.4);
    else if (key === 'nrg:0') o = this.walk(N, 9.4, 1.6, 4, 16, r);
    else if (key === 'thm:1') o = ramp(this.walk(N, 33, 0.7, 29, 38, r), 19);
    else if (key === 'thm:0') o = this.walk(N, 33, 0.7, 29, 38, r);
    else if (key === 'gpu') o = this.bump(this.walk(N, 13, 7, 2, 40, r), 66, 38, 4);
    else if (key === 'ssd') o = { a: this.bump(this.walk(N, 28, 30, 0, 120, r), 62, 90, 2.5), b: this.bump(this.walk(N, 7, 7, 0, 40, r), 88, 30, 3) };
    else if (key === 'net') o = { a: this.bump(this.walk(N, 1.6, 1.2, 0.05, 5, r), 70, 3, 4), b: this.walk(N, 140, 90, 20, 480, r) };
    else { o = this.walk(N, hog ? 3 : base, Math.max(0.6, base * 0.3), 0, 100, r); if (hog) o = ramp(o, base); }
    this._ser[key] = o;
    return o;
  }

  withLast(a, v) { const b = [...a]; b[b.length - 1] = v; return b; }

  apps(s) {
    const hogOn = this.cfg(s, 'hogAlert', true);
    return Component.APPS[s.platform].filter(a => !s.ended[a.id]).map(a => {
      const j = s.jit[a.id] || 0;
      const cpu = (a.hog && !hogOn) ? 2.8 * (1 + j) : a.hog ? Math.max(51, a.cpu * (1 + j * 0.4)) : a.cpu * (1 + j);
      return { ...a, base: a.cpu, cpu: Math.max(0.1, cpu), gpu: Math.max(0, Math.round(a.gpu * (1 + j))) };
    });
  }

  syncLive(st, patch) {
    const ns = { ...st, ...patch };
    if (this.cfg(ns, 'liveUpdates', true) !== false) return patch;
    const hog = this.hogActive(ns);
    const live = { ...st.live, cpu: hog ? 66 : 16, watt: hog ? 14.8 : 9.4, temp: hog ? 52 : 33 };
    const push = (a, v) => [...a.slice(1), v];
    return { ...patch, live, spark: { ...st.spark, cpu: push(st.spark.cpu, live.cpu), nrg: push(st.spark.nrg, live.watt), thm: push(st.spark.thm, live.temp) } };
  }
  key(fn) { return (e) => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); fn(); } }; }
  openView(v) { if (v.type !== 'settings') this.lastView = v; this.setState({ view: v, chartHover: null, hoverRow: -1 }); }
  showToast(toast) { clearTimeout(this.tt); this.setState({ toast }); this.tt = setTimeout(() => this.setState({ toast: null }), 2800); }
  requestEnd(id) { this.setState({ confirm: id, hoverRow: -1 }); }
  confirmEnd() {
    const id = this.state.confirm; if (!id) return;
    this.setState(s => this.syncLive(s, { ended: { ...s.ended, [id]: true }, confirm: null, hoverRow: -1, view: s.view && s.view.id === id ? null : s.view }));
    this.showToast({ kind: 'ended', id });
  }
  // --- Localization -------------------------------------------------------
  // All copy comes from locales/<code>.json through window.PulseI18n. Nothing
  // below branches on a language code: direction, fonts, plurals and number
  // shapes come from the active locale.
  locale(code) {
    const I = window.PulseI18n;
    if (!Component.localesLoaded) { I.loadFromDocument(); Component.localesLoaded = true; }
    return I.get(code);
  }
  // Resolves a PROF label ({ k, p }) through the locale's "hardware" section.
  R(lc, v) {
    if (v == null || typeof v !== 'object') return v;
    const p = {};
    if (v.p) Object.keys(v.p).forEach(k => { p[k] = this.R(lc, v.p[k]); });
    return lc.hw(v.k, p);
  }
  coresLabel(lc, c) {
    const parts = [lc.plural('core', c.c)];
    if (c.t) parts.push(lc.plural('thread', c.t));
    if (c.model) parts.push(c.model);
    return lc.join(parts);
  }
  // Puts locale fallbacks after the platform stack without letting a generic
  // family (system-ui, sans-serif) claim the glyphs first.
  stack(first, second) {
    if (!second) return first;
    const strip = s => s.split(',').map(x => x.trim()).filter(x => !/^(system-ui|sans-serif|serif|ui-sans-serif)$/.test(x)).join(', ');
    return `${strip(first)}, ${second}`;
  }

  fmts(pf, unitF, lc) {
    const N = (x, d = 0) => lc.num(x, d);
    const m = pf.net.mode, k = pf.disk.k;
    const down = m === 'dial' ? v => N(v * 2.9, 1) + ' KB/s' : m === 'lan' ? v => N(v * 0.55, 2) + ' MB/s' : v => N(v, 1) + ' MB/s';
    const up = m === 'dial' ? v => N(v / 127, 1) + ' KB/s' : m === 'lan' ? v => N(Math.round(v * 0.7)) + ' KB/s' : v => N(Math.round(v)) + ' KB/s';
    const io = v => N(v * k, 1) + ' MB/s';
    const memF = gb => pf.mem.u === 'MB' || gb < 0.25 ? N(gb * 1024, gb * 1024 < 10 ? 1 : 0) + ' MB' : N(gb, 2) + ' GB';
    const T = (c, d = 0) => unitF ? N(c * 9 / 5 + 32, d) + '°F' : N(c, d) + '°C';
    const pct = (x, d = 0) => N(d ? x : Math.round(x), d) + '%';
    const W = x => N(x, 1) + ' W';
    return { N, down, up, io, memF, T, pct, W };
  }
  // Locale menu keyboard support: arrows move between items, Escape closes.
  focusLangTrigger() { setTimeout(() => { const b = document.querySelector('[data-lang-trigger]'); if (b) b.focus(); }, 0); }
  focusLangItem(delta) {
    setTimeout(() => {
      const items = [...document.querySelectorAll('[role=menuitemradio]')];
      if (!items.length) return;
      let i = items.indexOf(document.activeElement);
      if (i < 0) i = Math.max(0, items.findIndex(x => x.getAttribute('aria-checked') === 'true'));
      else i = (i + delta + items.length) % items.length;
      items[i].focus();
    }, 0);
  }
  langKey(e) {
    if (e.key === 'Escape' && this.state.langMenu) { e.preventDefault(); this.setState({ langMenu: false }); this.focusLangTrigger(); }
    else if (e.key === 'ArrowDown' || e.key === 'ArrowUp') {
      e.preventDefault();
      if (!this.state.langMenu) this.setState({ langMenu: true });
      this.focusLangItem(e.key === 'ArrowDown' ? 1 : -1);
    } else if ((e.key === 'Home' || e.key === 'End') && this.state.langMenu) {
      e.preventDefault();
      const items = [...document.querySelectorAll('[role=menuitemradio]')];
      if (items.length) items[e.key === 'Home' ? 0 : items.length - 1].focus();
    }
  }
  thermalLevel(c) { return c < 48 ? 0 : c < 70 ? 1 : c < 90 ? 2 : 3; }

  buildDetail(s, lc, L, apps, charging, pf, F, gpuT, memC, inDetail) {
    const v = (s.view && s.view.type !== 'settings' ? s.view : null) || this.lastView || { type: 'cpu' };
    const tt = (k, p) => lc.t(k, p), R = x => this.R(lc, x), pct = F.pct;
    const plat = s.platform, M = pf.mem, hogA = this.hogActive(s) ? 1 : 0;
    const T = (k, val) => ({ k, v: val });
    const uS = this.cpuSplit(L.cpu, pf);
    const dur = (h, m) => h ? tt('durHM', { h, m }) : tt('durM', { m });
    const left = hogA ? dur(2, 40) : dur(4, 15), full = dur(0, 52);
    const mu = x => x + ' ' + M.u;
    let d;
    if (v.type === 'cpu') {
      d = { acc: 'cpu', title: tt('cpu'), hero: pct(L.cpu), sub: tt('cpuDetailSub', { user: pct(uS.u), sys: pct(uS.sy), cores: this.coresLabel(lc, pf.cores) }), a: this.withLast(this.series('cpu:' + hogA), L.cpu), zero: true, fv: x => pct(x), unit: tt('unitCpu'),
        tiles: [T(tt('tUser'), pct(uS.u)), T(tt('tSystem'), pct(uS.sy)), T(tt('tIdle'), pct(Math.max(0, 100 - Math.round(L.cpu)))), T(R(pf.coreA), pct(uS.a)), T(R(pf.coreB), pct(uS.b)), T(tt('tLoad'), F.N(L.cpu / 11.2, 2))] };
    } else if (v.type === 'mem') {
      const tiles = plat === 'chrome'
        ? [T(tt('segApp'), mu(memC.app)), T(R(M.l2), mu(memC.s2)), T(tt('tZramStored'), `${F.N(3.7, 1)} → ${F.N(1.2, 1)} GB`), T(tt('tZramRatio'), F.N(3.1, 1) + '×'), T('Crostini (Linux)', `${F.N(1.42, 2)} GB · ${pct(9)}`), T('ARCVM (Android)', `${F.N(1.12, 2)} GB · ${pct(7)}`)]
        : [T(tt('segApp'), mu(memC.app)), T(R(M.l2), mu(memC.s2)), T(R(M.l3), mu(memC.s3)), T(tt('segFree'), mu(memC.free)), T(R(M.x1[0]), R(M.x1[1])), T(R(M.x2[0]), R(M.x2[1]))];
      d = { acc: 'mem', title: tt('mem'), hero: pct(L.ram), sub: plat === 'chrome' ? tt('zramSub', { orig: F.N(3.7, 1) + ' GB', comp: F.N(1.2, 1) + ' GB', pct: pct(16) }) : tt('memInUse', { used: mu(memC.used), total: mu(memC.total) }), a: this.withLast(this.series('mem'), L.ram), fv: x => pct(x), unit: tt('unitRam'), tiles };
    } else if (v.type === 'nrg') {
      d = { acc: 'nrg', title: tt('nrg'), hero: pct(84), sub: charging ? tt('chargingFullIn', { time: full }) : tt('onBattLeft', { time: left }), a: this.withLast(this.series('nrg:' + hogA), L.watt), zero: true, fv: F.W, unit: '',
        tiles: [T(tt('tDraw'), charging ? '+' + F.N(48) + ' W' : '−' + F.W(L.watt)), T(tt('tTimeLeft'), charging ? tt('fullIn', { time: full }) : left), T(tt('tHealth'), pct(95)), T(tt('tCycles'), F.N(118)), T(tt('tCapacity'), `${F.N(69, 1)} / ${F.N(72.6, 1)} Wh`), T(tt('tSource'), charging ? 'USB-C 96 W' : tt('tBattery'))] };
    } else if (v.type === 'thm') {
      const lv = this.thermalLevel(L.temp), names = [tt('cool'), tt('warm'), tt('hot'), tt('throttled')];
      d = { acc: ['mem', 'nrg', 'thm', 'crit'][lv], title: tt('thm'), hero: F.T(L.temp), sub: lc.join([names[lv], lv < 3 ? tt('zero') : tt('throttling')]), a: this.withLast(this.series('thm:' + hogA), L.temp), fv: x => F.T(x, 1), unit: '',
        tiles: [T(tt('tCpuDie'), F.T(L.temp, 1)), T(tt('gpu'), F.T(gpuT, 1)), T(tt('tStorage'), F.T(29)), T(tt('tBattery'), F.T(28)), T(tt('tFans'), R(pf.fans)), T(tt('tThrottling'), pct(0))] };
    } else if (v.type === 'gpu') {
      d = { acc: 'gpu', title: tt('gpu'), hero: pct(L.gpu), sub: lc.join([pf.gpu.name, F.T(gpuT)]), a: this.withLast(this.series('gpu'), L.gpu), zero: true, fv: x => pct(x), unit: tt('unitGpu'),
        tiles: [T(tt('util'), pct(L.gpu)), T(tt('tTemperature'), F.T(gpuT, 1)), T(tt('tVideoMem'), R(pf.gpu.vram)), T(tt('tCoreClock'), pf.gpu.clock), T(tt('tPower'), pf.gpu.power), T(tt('tHotspot'), F.T(gpuT + 6.5, 1))] };
    } else if (v.type === 'ssd') {
      const sr = this.series('ssd');
      d = { acc: 'cpu', acc2: 'nrg', split: true, title: tt('storage'), hero: pct(49), sub: lc.join([R(pf.disk.name), tt('free', { value: pf.disk.free })]), a: this.withLast(sr.a, L.read), b: this.withLast(sr.b, L.write), aLabel: tt('read'), bLabel: tt('write'), fa: F.io, fb: F.io,
        tiles: [T(tt('tUsed'), pf.disk.used), T(tt('tFree'), pf.disk.free), T(tt('read'), F.io(L.read)), T(tt('write'), F.io(L.write)), T(tt('tHealth'), tt('tVerified')), T(tt('tFormat'), pf.disk.fmt)] };
    } else if (v.type === 'net') {
      const sr = this.series('net');
      d = { acc: 'mem', acc2: 'cpu', split: true, title: tt('network'), hero: F.down(L.down), sub: R(pf.net.sub), a: this.withLast(sr.a, L.down), b: this.withLast(sr.b, L.up), aLabel: tt('down'), bLabel: tt('up'), fa: x => '↓ ' + F.down(x), fb: x => '↑ ' + F.up(x),
        tiles: [T(tt('down'), F.down(L.down)), T(tt('up'), F.up(L.up)), T(tt('tInterface'), pf.net.iface), T(R(pf.net.sig[0]), pf.net.sig[1]), T(tt('tLatency'), pf.net.lat), T(tt('tToday'), pf.net.today)] };
    } else {
      const app = apps.find(a => a.id === v.id) || { ...(Component.APPS[plat].find(a => a.id === v.id) || Component.APPS[plat][0]) };
      const hogOn = this.cfg(s, 'hogAlert', true) && app.hog;
      const sub = lc.join([lc.plural('process', app.p), lc.plural('thread', app.th), 'PID ' + app.pid]);
      d = { acc: hogOn ? 'warn' : 'cpu', isApp: true, title: lc.app(app.name), hero: pct(app.cpu, 1), sub, a: this.withLast(this.series('app:' + app.id + ':' + (hogOn ? 1 : 0), app.base || app.cpu, hogOn), app.cpu), zero: true, fv: x => pct(x, 1), unit: tt('unitOfCpu'),
        tiles: [T(tt('cpu'), pct(app.cpu, 1)), T(tt('mem'), F.memF(app.mem)), T(tt('gpu'), pct(app.gpu)), T(tt('tipProc'), F.N(app.p)), T(tt('tThreads'), F.N(app.th)), T('PID', String(app.pid))] };
    }
    const W = 372, H = 140, n = d.a.length;
    let lineA, areaA, lineB = '', areaB = '', pa, pb = null, stat;
    if (!d.split) {
      const mx = Math.max(...d.a), mn = Math.min(...d.a);
      const lo = d.zero ? 0 : Math.max(0, mn - (mx - mn) * 0.8), hi = mx + (mx - lo) * 0.2;
      pa = d.a.map((v, i) => [i / (n - 1) * W, H - 6 - (v - lo) / (hi - lo) * (H - 34)]);
      lineA = this.smooth(pa); areaA = `${lineA} L${W},${H} L0,${H} Z`;
      const avg = d.a.reduce((x, y) => x + y, 0) / n;
      stat = tt('peakAvg', { peak: d.fv(mx), avg: d.fv(avg) });
    } else {
      const c = H / 2, ma = Math.max(...d.a) * 1.12, mb = Math.max(...d.b) * 1.12;
      pa = d.a.map((v, i) => [i / (n - 1) * W, c - 1 - (v / ma) * (c - 22)]);
      pb = d.b.map((v, i) => [i / (n - 1) * W, c + 1 + (v / mb) * (c - 22)]);
      lineA = this.smooth(pa); areaA = `${lineA} L${W},${c} L0,${c} Z`;
      lineB = this.smooth(pb); areaB = `${lineB} L${W},${c} L0,${c} Z`;
      stat = tt('peakSplit', { a: d.fa(Math.max(...d.a)), b: d.fb(Math.max(...d.b)) });
    }
    let ch = { show: false, left: 0, yA: 0, yB: 0, hasB: false, label: '', pillLeft: 50 };
    if (s.chartHover != null && inDetail) {
      const i = Math.max(0, Math.min(n - 1, s.chartHover));
      const secs = (n - 1 - i) * 5, m = Math.floor(secs / 60), sc = secs % 60;
      const ago = secs === 0 ? tt('now') : !m ? tt('agoS', { s: sc }) : sc ? tt('agoMS', { m, s: sc }) : tt('agoM', { m });
      const val = d.split ? lc.join([d.fa(d.a[i]), d.fb(d.b[i])]) : `${d.fv(d.a[i])}${d.unit ? ' ' + d.unit : ''}`;
      const left = i / (n - 1) * 100;
      // Split charts keep their legend in the card header, so the pill
      // tracks across [16%, 84%] like single-series charts. The pill shifts
      // by the same percentage of its own width, so it never leaves the chart.
      const lim = 16;
      ch = { show: true, left: left.toFixed(2), yA: pa[i][1].toFixed(1), yB: pb ? pb[i][1].toFixed(1) : 0, hasB: !!pb, label: lc.join([ago, val]), pillLeft: Math.max(lim, Math.min(100 - lim, left)).toFixed(2) };
    }
    return { det: { title: d.title, hero: d.hero, sub: d.sub, stat, tiles: d.tiles, split: !!d.split, isApp: !!d.isApp && inDetail, appId: v.id, aLabel: d.aLabel || '', bLabel: d.bLabel || '', lineA, areaA, lineB, areaB, midDash: d.split ? '0' : '2 4' }, ch, acc: d.acc, acc2: d.acc2 || d.acc };
  }

  cpuSplit(cpu, pf) {
    const u = Math.round(cpu * 0.66), sy = Math.round(cpu) - u;
    if (pf.single) return { u, sy, a: u, b: sy };
    return { u, sy, a: Math.min(100, Math.round(cpu * 1.22)), b: Math.round(cpu * 0.58) };
  }

  renderVals() {
    const s = this.state, plat = s.platform, P = Component.PLAT[plat], pf = Component.PROF[plat];
    const lc = this.locale(s.lang), rtl = lc.rtl, tt = (k, p) => lc.t(k, p), R = x => this.R(lc, x);
    const classic = plat === 'xp' || plat === 'w98', w98 = plat === 'w98';
    const dark = !classic && s.theme === 'dark';
    const t = lc.strings(), L = s.live;
    const hogOn = this.cfg(s, 'hogAlert', true), charging = this.cfg(s, 'charging', false), live = this.cfg(s, 'liveUpdates', true);
    const apps = this.apps(s);
    const hogApp = hogOn ? apps.find(a => a.hog) : null;
    const base = classic ? { ...Component.LIGHT, ...Component.CLASSIC[plat] } : dark ? Component.DARK : Component.LIGHT;
    const pageBase = s.theme === 'dark' ? Component.DARK : Component.LIGHT;
    const mix = (c, p) => `color-mix(in srgb, ${base[c]} ${p}%, transparent)`;
    const F = this.fmts(pf, s.unitF, lc), pct = F.pct;
    const gpuT = pf.gpu.t + L.gpuD;
    const M = pf.mem;
    const usedV = L.ram * M.total / 100;
    const memC = { used: F.N(usedV, M.d), total: F.N(M.total), app: F.N(Math.max(0, usedV - M.seg[1] - M.seg[2]), M.d), free: F.N(Math.max(0, M.total - usedV), M.d), s2: F.N(M.seg[1], M.d), s3: F.N(M.seg[2], M.d) };
    const appV = Math.max(0, usedV - M.seg[1] - M.seg[2]);
    const inSettings = !!s.view && s.view.type === 'settings';
    const inDetail = !!s.view && !inSettings;
    const lf = lc.fonts(plat);

    const { det, ch, acc, acc2 } = this.buildDetail(s, lc, L, apps, charging, pf, F, gpuT, memC, inDetail);
    const lvl = this.thermalLevel(L.temp), tsKey = ['mem', 'nrg', 'thm', 'crit'][lvl];
    const gl = gpuT < 60 ? 'mem' : gpuT < 80 ? 'nrg' : 'thm';
    const gradDir = rtl ? '270deg' : '90deg';
    const vars = {
      ...base,
      '--page': pageBase['--page'], '--page-ink': pageBase['--page-ink'], '--page-ink2': pageBase['--page-ink2'], '--page-track': pageBase['--page-track'], '--page-card': pageBase['--page-card'], '--page-line': pageBase['--page-line'], '--page-do': pageBase['--page-do'], '--page-dont': pageBase['--page-dont'],
      '--cpu-tint': mix('--cpu', 12), '--mem-tint': mix('--mem', 12), '--nrg-tint': mix('--nrg', 13), '--thm-tint': mix('--thm', 12), '--gpu-tint': mix('--gpu', 13), '--warn-tint': mix('--warn', 16),
      '--mem-tint2': mix('--mem', 30), '--cpu-soft': mix('--cpu', 55), '--mem-soft': mix('--mem', 55),
      '--cpu-line': mix('--cpu', 35), '--mem-line': mix('--mem', 35), '--nrg-line': mix('--nrg', 30), '--thm-line': mix('--thm', 32), '--gpu-line': mix('--gpu', 35), '--warn-line': mix('--warn', 34), '--danger-line': mix('--crit', 40),
      '--cpu-glow': mix('--cpu', 60), '--mem-glow': mix('--mem', 60),
      '--ts': base['--' + tsKey], '--ts-ink': base['--' + tsKey + '-ink'], '--ts-tint': mix('--' + tsKey, 13), '--ts-line': mix('--' + tsKey, 35),
      '--gt-ink': base['--' + gl + '-ink'], '--gt-tint': mix('--' + gl, 13),
      '--focus': base['--mem'],
      '--hog-bg': w98 ? '#E8D2BC' : `linear-gradient(${gradDir}, ${mix('--warn', dark ? 18 : 13)}, ${mix('--crit', dark ? 12 : 8)})`,
      '--ssd-fill': w98 ? '#000080' : `linear-gradient(${gradDir}, ${base['--cpu']}, ${base['--nrg']})`,
      '--acc': base['--' + acc], '--acc-ink': base['--' + acc + '-ink'], '--acc-tint': mix('--' + acc, 13), '--acc2': base['--' + acc2], '--acc2-ink': base['--' + acc2 + '-ink'],
      '--r-fly': P.rFly, '--r-card': P.rCard, '--r-btn': P.rBtn, '--r-badge': P.rBadge, '--r-row': P.rRow, '--r-pill': P.rPill, '--r-mono': P.rMono, '--r-tip': P.rTip, '--r-logo': P.rLogo,
      '--f-hero': this.stack(P.hero, lf.hero), '--f-ui-latin': P.ui, '--f-text': lf.ui ? this.stack(lf.ui, P.ui) : P.ui,
      '--flip': rtl ? '-1' : '1', '--grad-dir': gradDir,
      '--fly-reserve': P.reserve, '--fly-top': P.top, '--fly-bottom': P.bottom, '--fly-end': P.end, '--fly-origin': `${P.origin} ${rtl ? 'left' : 'right'}`
    };
    if (w98) Object.keys(vars).forEach(k => { if (/-tint$/.test(k)) vars[k] = 'transparent'; });

    const ROW = 34;
    const key = s.sort;
    const sorted = [...apps].sort((a, b) => b[key] - a[key]).slice(0, 5);
    const mx = (sorted[0] && sorted[0][key]) || 1;
    const rows = sorted.map((a, i) => {
      const hov = s.hoverRow === i, hogRow = a.hog && hogOn && key === 'cpu';
      const h = a.h, nm = lc.app(a.name);
      const monoBg = w98 ? '#FFFFFF' : dark ? `oklch(0.4 0.07 ${h} / ${a.bg ? 0.45 : 0.75})` : (a.bg ? `oklch(0.95 0.035 ${h})` : `oklch(0.89 0.075 ${h})`);
      const monoInk = w98 ? '#000080' : dark ? `oklch(0.9 0.08 ${h})` : `oklch(${a.bg ? 0.45 : 0.4} 0.11 ${h})`;
      return {
        name: nm, mono: a.mono || a.name[0], monoBg, monoInk, procs: F.N(a.p), showProcs: a.p > 1,
        barW: (Math.max(0.03, a[key] / mx) * 100).toFixed(1) + '%',
        barColor: hogRow ? `linear-gradient(${gradDir}, var(--warn), var(--crit))` : key === 'cpu' ? 'var(--cpu)' : key === 'mem' ? 'var(--mem)' : 'var(--gpu)',
        metric: key === 'mem' ? F.memF(a.mem) : key === 'cpu' ? pct(a.cpu, 1) : pct(a.gpu),
        metricInk: hogRow ? 'var(--warn-ink)' : 'var(--ink)',
        bg: hov ? 'var(--hover)' : 'transparent', endOp: hov ? 1 : 0, endPe: hov ? 'auto' : 'none', metricOp: hov ? 0 : 1, endTab: hov ? 0 : -1,
        endLabel: tt('endNamed', { app: nm }),
        onEnter: () => this.setState({ hoverRow: i }),
        onLeave: () => this.setState(st => st.hoverRow === i ? { hoverRow: -1 } : null),
        onClick: () => this.openView({ type: 'app', id: a.id }),
        onKey: this.key(() => this.openView({ type: 'app', id: a.id })),
        onEnd: (e) => { e.stopPropagation(); this.requestEnd(a.id); }
      };
    });
    const hr = s.hoverRow >= 0 ? sorted[s.hoverRow] : null;
    const share = hr ? (pf.single ? 66 : hr.bg ? 34 : 58 + this.hash(hr.id) % 28) : 0;
    const totalGb = M.u === 'MB' ? M.total / 1024 : M.total;
    const tip = hr ? { name: lc.app(hr.name), rows: [
      { k: pf.single ? tt('tipUserKernel') : tt('tipCore'), v: pf.single ? `${pct(share)} · ${pct(100 - share)}` : `${pf.share[0]} ${pct(share)} · ${pf.share[1]} ${pct(100 - share)}` },
      { k: tt('tipRam'), v: pct(hr.mem / totalGb * 100, 1) }, { k: tt('tipGpu'), v: pct(hr.gpu) }, { k: tt('tipProc'), v: F.N(hr.p) }] } : { name: '', rows: [] };
    const tipUp = s.hoverRow >= 3 || (rows.length >= 3 && s.hoverRow === rows.length - 1);
    const tipTop = tipUp ? s.hoverRow * ROW - 142 : (s.hoverRow + 1) * ROW + 2;

    const sortIdx = { cpu: 0, mem: 1, gpu: 2 }[key];
    const sorts = [['cpu', tt('cpu')], ['mem', tt('mem')], ['gpu', tt('gpu')]].map(([k, label]) => ({ label, on: k === key, ink: k === key ? 'var(--ink)' : 'var(--ink2)', onClick: () => this.setState({ sort: k, hoverRow: -1 }) }));

    const platforms = [['mac', 'macOS'], ['win', 'Windows 11'], ['chrome', 'ChromeOS'], ['linux', 'Linux'], ['xp', 'Windows XP'], ['w98', 'Windows 98 SE']].map(([k, label]) => {
      const on = plat === k;
      return { label, on, bg: on ? pageBase['--seg-pill'] : 'transparent', ink: on ? pageBase['--page-ink'] : pageBase['--page-ink2'], sh: on ? pageBase['--seg-sh'] : 'none', onClick: () => { this.lastView = null; this.setState({ platform: k, view: null, hoverRow: -1, chartHover: null, open: true, quit: false, confirm: null, jit: {} }); } };
    });

    // Language switcher: one segment per registered locale file.
    const langList = window.PulseI18n.list();
    const langIdx = Math.max(0, langList.findIndex(x => x.code === lc.code));
    const langs = langList.map(x => {
      const on = x.code === lc.code, xf = window.PulseI18n.get(x.code).fonts(plat);
      return { label: x.label, name: x.name, code: x.code, dir: x.dir, on, ink: on ? 'var(--ink)' : 'var(--ink2)', bg: on ? 'var(--hover)' : 'transparent', check: on ? 'visible' : 'hidden',
        font: xf.ui || 'var(--f-ui-latin)', onClick: () => { this.setState({ lang: x.code, langSys: false, langMenu: false }); this.focusLangTrigger(); } };
    });
    // Two locales get the fast pill toggle; three or more get a menu.
    const langPill = langs.length <= 2;

    const uS = this.cpuSplit(L.cpu, pf), gp = Math.round(L.gpu);
    const cpuHigh = L.cpu >= 50;
    const wave = s.spark.cpu.slice(-6).map(v => ({ h: Math.max(3, Math.min(14, 3 + v / 70 * 11)).toFixed(1), h98: Math.max(1, Math.min(10, v / 70 * 10)).toFixed(0) }));
    const shift = rtl ? -16 : 16;
    const vis = on => ({ op: on ? 1 : 0, tf: on ? 'none' : `translateX(${shift}px)`, vis: on ? 'visible' : 'hidden', mh: on ? '3000px' : '0px', ov: on ? 'visible' : 'hidden' });
    const mainV = vis(!s.view); if (s.view) mainV.tf = `translateX(${-shift}px)`;
    const detV = vis(inDetail), setV = vis(inSettings);
    const isBottom = P.origin === 'bottom';
    const open = (k) => () => this.openView({ type: k });
    const opens = { cpu: open('cpu'), mem: open('mem'), nrg: open('nrg'), thm: open('thm'), gpu: open('gpu'), ssd: open('ssd'), net: open('net') };
    const k = {}; Object.keys(opens).forEach(x => { k[x] = this.key(opens[x]); });

    const confirmApp = s.confirm ? Component.APPS[plat].find(a => a.id === s.confirm) : null;
    const cName = confirmApp ? lc.app(confirmApp.name) : '';
    let toastText = '';
    if (s.toast && s.toast.kind === 'ended') { const a = Component.APPS[plat].find(x => x.id === s.toast.id); toastText = tt('toastEnded', { app: a ? lc.app(a.name) : '' }); }
    if (s.toast && s.toast.kind === 'monitor') toastText = tt('toastMonitor', { monitor: R(pf.monitor) });
    if (s.toast && s.toast.kind === 'restored') toastText = tt('toastRestored');
    const confirmShow = !!confirmApp, toastShow = !confirmShow && !!toastText;
    const hogShow = !!hogApp && !s.hogDismissed && !s.view && !confirmShow && !toastShow;
    const endedCount = Object.keys(s.ended).filter(id => Component.APPS[plat].some(a => a.id === id)).length;

    const sw = (label, sub, on, k2) => ({ label, sub, on, track: on ? 'var(--cpu)' : 'var(--track-strong)', knob: on ? 19 : 3, onClick: () => this.setState(st => this.syncLive(st, { sim: { ...st.sim, [k2]: !on }, hogDismissed: k2 === 'hogAlert' ? false : st.hogDismissed })) });
    const settingsRows = [
      sw(tt('simHog'), tt('simHogSub', { pct: pct(50) }), hogOn, 'hogAlert'),
      sw(tt('simCharging'), tt('simChargingSub', { adapter: 'USB-C 96 W' }), charging, 'charging'),
      sw(tt('simLive'), tt('simLiveSub', { secs: tt('seconds', { n: F.N(1.5, 1) }) }), live, 'liveUpdates')
    ];
    const units = [['°C', false], ['°F', true]].map(([label, f]) => { const on = s.unitF === f; return { label, on, bg: on ? 'var(--seg-pill)' : 'transparent', sh: on ? 'var(--seg-sh)' : 'none', ink: on ? 'var(--ink)' : 'var(--ink2)', onClick: () => this.setState({ unitF: f }) }; });

    const closed = { show: !s.open,
      text: s.quit ? tt('closedQuit') : tt('closedRunning', { bar: R(pf.bar) }),
      btn: s.quit ? tt('relaunch') : tt('openPulse'),
      action: () => this.setState({ open: true, quit: false }) };

    const thermalNames = [tt('cool'), tt('warm'), tt('hot'), tt('throttled')];
    const thermal = {
      label: thermalNames[lvl], now: lvl + 1, note: lvl < 3 ? tt('zero') : tt('throttling'), noteInk: lvl < 3 ? 'var(--ink3)' : 'var(--crit-ink)',
      segs: [0, 1, 2, 3].map(i => ({ bg: i === lvl ? 'var(--ts)' : 'var(--track)' })),
      labels: thermalNames.map((label, i) => ({ label, ink: i === lvl ? 'var(--ts-ink)' : 'var(--ink3)', w: i === lvl ? 700 : 500 }))
    };
    const hogVal = hogApp ? Math.round(hogApp.cpu) : 0;
    const dur = (h, m) => h ? tt('durHM', { h, m }) : tt('durM', { m });
    const wattShort = F.N(L.watt, 1) + 'W';

    return {
      vars, t, dir: lc.dir, lang: lc.code, isMac: plat === 'mac', isWin: plat === 'win', isChrome: plat === 'chrome', isLinux: plat === 'linux', isXp: plat === 'xp', is98: w98, isModern: !classic,
      isDark: dark, isLight: !dark, platforms, menuItems: ['Finder', tt('menuFile'), tt('menuEdit'), tt('menuView'), tt('menuGo')].map((label, i) => ({ label, w: i === 0 ? 700 : 400 })),
      running: !s.quit, closed,
      cpuPct: pct(L.cpu), ramPct: pct(L.ram), wattShort, battPct: pct(84),
      barCpuText: `${t.barCpu} ${pct(L.cpu)}`, barRamText: `${t.barRam} ${pct(L.ram)}`,
      netDown: F.down(L.down), netUp: F.up(L.up),
      tempLine: tt('tempLine', { cpu: F.T(L.temp), gpu: F.T(gpuT) }),
      thermal, gpuName: pf.gpu.name, gpuTempC: F.T(gpuT),
      userSys: tt('userSys', { user: pct(uS.u), sys: pct(uS.sy) }),
      coreA: R(pf.coreA), coreB: R(pf.coreB),
      pcW: Math.max(2, uS.a) + '%', ecW: Math.max(2, uS.b) + '%', gpuW: Math.max(2, gp) + '%', pcVal: pct(uS.a), ecVal: pct(uS.b), gpuVal: pct(gp), gpuNow: gp,
      cpuCard: { high: cpuHigh, ink: cpuHigh ? 'var(--warn-ink)' : 'var(--ink)', border: cpuHigh ? 'var(--warn-line)' : 'var(--card-b)' },
      memOf: tt('memOf', { used: memC.used + ' ' + M.u, total: memC.total + ' ' + M.u }),
      mem: { w1: (appV / M.total * 100).toFixed(1) + '%', w2: (M.seg[1] / M.total * 100).toFixed(1) + '%', w3: (M.seg[2] / M.total * 100).toFixed(1) + '%', v1: memC.app + ' ' + M.u, v2: memC.s2 + ' ' + M.u, v3: memC.s3 + ' ' + M.u, v4: memC.free + ' ' + M.u, l2: R(M.l2), l3: R(M.l3) },
      disk: { name: R(pf.disk.name), usedTxt: tt('used', { pct: pct(49) }), freeTxt: tt('free', { value: pf.disk.free }) },
      netName: R(pf.net.name),
      battSub: charging ? tt('fullIn', { time: dur(0, 52) }) : tt('timeLeft', { time: this.hogActive(s) ? dur(2, 40) : dur(4, 15) }),
      flowW: charging ? '+' + F.N(48) + ' W' : '−' + F.W(L.watt), flowLabel: charging ? tt('charging') : tt('onBatt'),
      healthLine: tt('health', { pct: pct(95), cycles: lc.plural('cycle', 118) }),
      sp: { cpu: this.spark(s.spark.cpu), mem: this.spark(s.spark.mem), nrg: this.spark(s.spark.nrg), thm: this.spark(s.spark.thm) },
      wave,
      status: hogApp && !s.hogDismissed
        ? { text: tt('pillValue', { label: tt('hogPill'), value: pct(hogVal) }), bg: 'var(--warn-tint)', ink: 'var(--warn-ink)', dot: 'var(--warn)' }
        : { text: tt('pillValue', { label: tt('calm'), value: wattShort }), bg: 'var(--cpu-tint)', ink: 'var(--cpu-ink)', dot: 'var(--cpu)' },
      langs, langIdx, langCount: langs.length, langW: Math.max(2, langs.length) * 33,
      langPill, langMenuMode: !langPill, langMenuOpen: !langPill && s.langMenu, langCur: langs[langIdx] || langs[0],
      langMenuLabel: `${tt('aLang')}: ${lc.name}`,
      toggleLang: () => this.setState({ lang: langList[(langIdx + 1) % langList.length].code, langSys: false }),
      langSel: s.langSys ? 'system' : lc.code,
      langOpts: [{ value: 'system', name: tt('langSystem') }].concat(langList.map(x => ({ value: x.code, name: x.name }))),
      pickLang: e => { const v = e.target.value; if (v === 'system') { const sys = window.PulseI18n.resolve((navigator.languages && navigator.languages[0]) || navigator.language || 'en'); this.setState({ langSys: true, lang: sys ? sys.code || sys : 'en' }); } else this.setState({ langSys: false, lang: v }); },
      toggleLangMenu: () => { this.setState(st => ({ langMenu: !st.langMenu })); this.focusLangItem(0); },
      langBlur: (e) => { if (!e.currentTarget.contains(e.relatedTarget)) this.setState({ langMenu: false }); },
      langKey: (e) => this.langKey(e),
      themeLabel: dark ? tt('aLight') : tt('aDark'),
      toggleTheme: () => this.setState(st => ({ theme: st.theme === 'dark' ? 'light' : 'dark' })),
      toggleOpen: () => this.setState(st => ({ open: !st.open, hoverRow: -1, confirm: null, langMenu: false })),
      quitApp: () => this.setState({ open: false, quit: true, hoverRow: -1, confirm: null }),
      openSettings: () => inSettings ? this.setState({ view: null }) : this.openView({ type: 'settings' }),
      inSettings, gearBg: inSettings ? 'var(--card-hover)' : 'var(--card)', gearBorder: inSettings ? 'var(--mem-line)' : 'var(--card-b)', gearInk: inSettings ? 'var(--mem-ink)' : 'var(--ink2)',
      barPress: s.open ? 'var(--press)' : 'transparent', podBg: s.open ? 'var(--press)' : 'var(--pod)', linuxPress: s.open ? 'rgba(255,255,255,0.18)' : 'transparent',
      flyOp: s.open ? 1 : 0, flyPe: s.open ? 'auto' : 'none', flyVis: s.open ? 'visible' : 'hidden', flyTf: s.open ? 'none' : `translateY(${isBottom ? 10 : -10}px) scale(0.97)`,
      mainV, detV, setV, k, ...{ openCpu: opens.cpu, openMem: opens.mem, openNrg: opens.nrg, openThm: opens.thm, openGpu: opens.gpu, openSsd: opens.ssd, openNet: opens.net },
      back: () => this.setState({ view: null, chartHover: null }),
      sorts, segIdx: sortIdx, rows,
      tipShow: !!hr, tip, tipTop,
      hogShow, hogTitle: hogApp ? tt('hogTitle', { app: lc.app(hogApp.name), pct: pct(hogVal) }) : '', hogSub: tt('hogSub', { pct: pct(50) }),
      endHog: () => hogApp && this.requestEnd(hogApp.id),
      dismissHog: () => this.setState({ hogDismissed: true }),
      confirm: { show: confirmShow, title: tt('confirmTitle', { app: cName }), btn: tt('confirmBtn') },
      cancelEnd: () => this.setState({ confirm: null }),
      confirmEnd: () => this.confirmEnd(),
      endCurrent: () => det.appId && this.requestEnd(det.appId),
      toast: { show: toastShow, text: toastText },
      monitorLabel: tt('openMonitor', { monitor: R(pf.monitor) }),
      openMonitor: () => this.showToast({ kind: 'monitor' }),
      settingsRows, units,
      restoreSub: lc.plural('ended', endedCount),
      restoreOp: endedCount ? 1 : 0.5,
      restoreApps: () => { if (!endedCount) return; this.setState(st => this.syncLive(st, { ended: {}, hogDismissed: false })); this.showToast({ kind: 'restored' }); },
      det, ch,
      chartMove: (e) => { const r = e.currentTarget.getBoundingClientRect(); const fr = Math.max(0, Math.min(1, (e.clientX - r.left) / r.width)); const i = Math.round(fr * 120); if (i !== this.state.chartHover) this.setState({ chartHover: i }); },
      chartLeave: () => this.setState({ chartHover: null })
    };
  }
}

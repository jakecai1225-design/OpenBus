// ===== 菜单下拉 =====
function toggleMenu(el) { const wasOpen = el.classList.contains('open'); closeMenus(); if (!wasOpen) el.classList.add('open'); }
function closeMenus() { document.querySelectorAll('.menu-item.open').forEach(m => m.classList.remove('open')); }
document.addEventListener('click', e => { if (!e.target.closest('.menu-item')) closeMenus(); });
function toggleCheck(el) { if (el.classList.contains('checked')) { el.classList.remove('checked'); el.classList.add('unchecked'); } else { el.classList.remove('unchecked'); el.classList.add('checked'); } }

// ===== ActivityBar 面板切换 =====
function switchPanel(name) {
  document.querySelectorAll('.act-icon').forEach(i => i.classList.remove('active'));
  document.querySelector('.act-icon[data-panel="' + name + '"]').classList.add('active');
  document.querySelectorAll('.sb-panel').forEach(p => p.classList.remove('active'));
  const panel = document.getElementById('panel-' + name);
  if (panel) panel.classList.add('active');
}

// ===== 中央标签页切换 =====
function switchCenterTab(name) {
  document.querySelectorAll('#center-tabs .ctab').forEach(t => t.classList.remove('active'));
  const tab = document.querySelector('#center-tabs .ctab[data-tab="' + name + '"]');
  if (tab) tab.classList.add('active');
  document.querySelectorAll('#center-area > .tab-pane').forEach(p => p.classList.remove('active'));
  const pane = document.getElementById('pane-' + name);
  if (pane) pane.classList.add('active');
  // 标签页-侧边栏联动：根据标签页名称切换左侧栏面板
  const tabPanelMap = {
    'trace':'trace','trace2':'trace',
    'graphic1':'graphic','graphic2':'graphic','graphic3':'graphic',
    'dbc':'dbc','dbc2':'dbc',
    'send':'send','record':'record'
  };
  const panel = tabPanelMap[name];
  if (panel) switchPanel(panel);
}

// ===== 右侧栏标签页切换 =====
function switchRightTab(name) {
  document.querySelectorAll('.right-dock .dtab').forEach(t => t.classList.remove('active'));
  document.querySelector('.right-dock .dtab[data-tab="' + name + '"]').classList.add('active');
  ['ai','quick'].forEach(n => document.getElementById('right-' + n).classList.remove('active'));
  document.getElementById('right-' + name).classList.add('active');
}

// ===== AI 对话发送 =====
function sendChat() {
  const input = document.getElementById('ai-input');
  const msg = input.value.trim();
  if (!msg) return;
  const messages = document.getElementById('ai-messages');
  const userDiv = document.createElement('div');
  userDiv.className = 'ai-msg user';
  userDiv.innerHTML = '<div class="role">我</div>' + msg;
  messages.appendChild(userDiv);
  input.value = '';
  messages.scrollTop = messages.scrollHeight;
  setTimeout(() => {
    const aiDiv = document.createElement('div');
    aiDiv.className = 'ai-msg ai';
    aiDiv.innerHTML = '<div class="role">AI 助手</div>已收到你的消息: "' + msg + '"。这是一个原型演示，实际 AI 功能正在开发中。';
    messages.appendChild(aiDiv);
    messages.scrollTop = messages.scrollHeight;
  }, 500);
}

// ===== 底部标签页切换 =====
function switchBottomTab(name) {
  document.querySelectorAll('.bottom-dock .dtab').forEach(t => t.classList.remove('active'));
  document.querySelector('.bottom-dock .dtab[data-tab="' + name + '"]').classList.add('active');
  ['terminal','output','problems'].forEach(n => document.getElementById('bottom-' + n).classList.remove('active'));
  document.getElementById('bottom-' + name).classList.add('active');
}

// ===== 行选择 =====
function selectRow(tr) {
  document.querySelectorAll('#trace-tbody tr').forEach(r => r.classList.remove('selected'));
  tr.classList.add('selected');
  const cells = tr.querySelectorAll('td');
  const frameInfo = 'Time:       ' + cells[0].textContent + '\nChannel:    ' + cells[1].textContent +
    '\nDirection:  ' + cells[2].textContent + '\nID:         ' + cells[3].textContent +
    '\nDLC:        ' + cells[4].textContent + '\nData:       ' + cells[5].textContent +
    '\nFlags:      ' + cells[6].textContent + '\n\nHex:  ' + cells[5].textContent;
  document.getElementById('frame-info').textContent = frameInfo;
  const sel = document.getElementById('status-selected');
  if (sel) sel.textContent = '选中1行';
}

// ===== 右键菜单（标签页） — 需要 DOM 加载完成后调用 =====
function initPage() {
  document.querySelectorAll('#center-tabs .ctab').forEach(tab => {
    tab.addEventListener('contextmenu', e => {
      e.preventDefault();
      const menu = document.getElementById('tab-ctx-menu');
      menu.style.left = e.clientX + 'px';
      menu.style.top = e.clientY + 'px';
      menu.classList.add('show');
    });
  });
  document.addEventListener('click', hideCtx);
}
function hideCtx() { document.getElementById('tab-ctx-menu').classList.remove('show'); }

// ===== 拆分视图 =====
function showSplitView(direction) {
  const center = document.getElementById('center-area');
  if (center.querySelector('.split-container')) return;
  const tabs = document.getElementById('center-tabs');
  const allPanes = [];
  ['trace','trace2','graphic1','graphic2','graphic3','dbc','dbc2','send','record'].forEach(n => {
    const p = document.getElementById('pane-' + n);
    if (p) allPanes.push(p);
  });
  const container = document.createElement('div');
  container.className = 'split-container ' + (direction === 'horizontal' ? 'split-horizontal' : 'split-vertical');
  container.style.flex = '1';
  const pane1 = document.createElement('div');
  pane1.className = 'split-pane';
  pane1.appendChild(tabs);
  allPanes.forEach(p => pane1.appendChild(p));
  const divider = document.createElement('div');
  divider.className = 'split-divider ' + (direction === 'horizontal' ? 'horizontal' : 'vertical');
  const pane2 = document.getElementById('split-template').querySelector('.split-pane').cloneNode(true);
  pane2.id = 'split-pane-2';
  pane2.style.display = 'flex';
  container.appendChild(pane1);
  container.appendChild(divider);
  container.appendChild(pane2);
  center.appendChild(container);
}

// ===== 场景切换 =====
function setScenario(name, btn) {
  document.querySelectorAll('.sbtn').forEach(b => b.classList.remove('active'));
  if (btn) btn.classList.add('active');
  switchRightTab('ai');
  switchBottomTab('output');
  switch (name) {
    case 'trace': switchPanel('trace'); switchCenterTab('trace'); break;
    case 'graphic': switchPanel('graphic'); switchCenterTab('graphic1'); break;
    case 'dbc': switchPanel('dbc'); switchCenterTab('dbc'); break;
    case 'send': switchPanel('send'); switchCenterTab('send'); break;
    case 'record': switchPanel('record'); switchCenterTab('record'); break;
    case 'project': switchPanel('project'); switchCenterTab('trace'); break;
    case 'trace-cfg': switchPanel('trace'); switchCenterTab('trace'); break;
    case 'dbc-cfg': switchPanel('dbc'); switchCenterTab('dbc'); break;
    case 'device': switchPanel('device'); switchCenterTab('trace'); break;
    case 'settings': switchPanel('settings'); switchCenterTab('trace'); break;
    case 'ai': switchPanel('trace'); switchCenterTab('trace'); switchRightTab('ai'); break;
    case 'quick': switchPanel('trace'); switchCenterTab('trace'); switchRightTab('quick'); break;
    case 'terminal': switchPanel('trace'); switchCenterTab('trace'); switchBottomTab('terminal'); break;
    case 'output': switchPanel('trace'); switchCenterTab('trace'); switchBottomTab('output'); break;
    case 'problems': switchPanel('trace'); switchCenterTab('trace'); switchBottomTab('problems'); break;
    case 'split': switchPanel('trace'); switchCenterTab('trace'); showSplitView('horizontal'); break;
    case 'about': document.getElementById('about-dialog').classList.add('show'); break;
  }
}

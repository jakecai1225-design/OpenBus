// ===== 动态加载 HTML partials =====
(function () {
  const partials = [
    { id: 'partial-scenario-bar',  url: 'partials/scenario-bar.html' },
    { id: 'partial-menubar',       url: 'partials/menubar.html' },
    { id: 'partial-left-dock',     url: 'partials/left-dock.html' },
    { id: 'partial-center-area',   url: 'partials/center-area.html' },
    { id: 'partial-right-dock',    url: 'partials/right-dock.html' },
    { id: 'partial-bottom-dock',   url: 'partials/bottom-dock.html' },
    { id: 'partial-dialogs',       url: 'partials/dialogs.html' },
    { id: 'partial-overlays',      url: 'partials/overlays.html' }
  ];

  Promise.all(partials.map(p =>
    fetch(p.url).then(r => r.text()).then(html => {
      document.getElementById(p.id).innerHTML = html;
    })
  )).then(() => {
    // 所有 partial 加载完成后初始化页面交互
    if (typeof initPage === 'function') initPage();
  }).catch(err => {
    console.error('加载 partial 失败:', err);
  });
})();

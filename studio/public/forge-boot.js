(() => {
  const get = (id) => document.getElementById(id);
  const show = (message) => {
    const panel = get('forge-boot');
    const detail = get('forge-boot-error');
    const retry = get('forge-boot-reload');
    if (panel) panel.hidden = false;
    if (detail) { detail.hidden = false; detail.textContent = String(message || 'Errore di avvio non specificato'); }
    if (retry) retry.hidden = false;
  };
  window.__FORGE_UI_READY__ = () => { const panel = get('forge-boot'); if (panel) panel.hidden = true; };
  window.__FORGE_UI_FAIL__ = show;
  window.addEventListener('error', (event) => {
    const source = event.filename ? `\nFile: ${event.filename}:${event.lineno || 0}:${event.colno || 0}` : '';
    show(`${event.message || 'Errore JavaScript'}${source}${event.error?.stack ? `\n${event.error.stack}` : ''}`);
  });
  window.addEventListener('unhandledrejection', (event) => {
    const reason = event.reason;
    show(`Errore asincrono non gestito: ${reason?.stack || reason?.message || String(reason)}`);
  });
  window.setTimeout(() => {
    if (!window.__FORGE_UI_MOUNTED__) show('L’interfaccia non ha completato il rendering entro 15 secondi. Controllare WebView2, bundle e console frontend.');
  }, 15000);
})();

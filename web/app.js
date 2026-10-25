'use strict';
(() => {
  const $ = id => document.getElementById(id);
  let key = sessionStorage.getItem('marauder-key') || '';
  let firstSetup = false, accessPending = null, accessSaving = false;
  try {
    const saved = JSON.parse(sessionStorage.getItem('marauder-access-pending'));
    if (saved && typeof saved.next === 'string' && typeof saved.previous === 'string' && saved.next.length <= 63 && saved.previous.length <= 63) accessPending = saved;
  } catch {}
  let cursor = 0, bootId = null, pollTimer = 0, polling = false, commandPending = false;
  let activeTab = 'tasks', failures = 0, consoleText = '', history = [], historyIndex = 0;
  const headings = {tasks:['Choose your next task','Wi-Fi, Bluetooth, network and device tools. Guided from start to result.'],advanced:['Advanced tools','The firmware’s manual controls and command reference.'],overview:['Device status','Everything on your board, within reach.'],commands:['Command library','The program’s controls, ready when you are.'],console:['Live console','Direct access to the Marauder command interface.'],files:['Device files','Capture and log files, stored locally.'],settings:['Program settings','Configure the firmware and manage your session.']};
  const modes = window.MARAUDER_MODES || {0:'Idle'};
  const bytes = n => n >= 1048576 ? (n / 1048576).toFixed(1) + ' MB' : (n / 1024).toFixed(1) + ' KB';
  function message(text, error = false) {
    $('message').textContent = text;
    $('message').classList.toggle('error', error);
    $('message').hidden = !text;
  }
  function connection(text, state) {
    $('connection').textContent = text;
    $('connection').className = 'connection ' + state;
  }
  function append(text) {
    consoleText = (consoleText + text).slice(-32768);
    $('console').textContent = consoleText;
    if ($('autoscroll').checked) $('console').scrollTop = $('console').scrollHeight;
  }
  async function request(path, options = {}, format = 'json') {
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), 4500);
    try {
      const response = await fetch(path, {...options, cache:'no-store', signal:controller.signal,
        headers:{'X-Device-Key':key, ...(options.body ? {'Content-Type':'application/json'} : {}), ...options.headers}});
      if (!response.ok) {
        let body;
        try { body = await response.json(); } catch { body = {}; }
        const error = new Error(body.error || 'Device returned HTTP ' + response.status);
        error.status = response.status;
        throw error;
      }
      return await (format === 'blob' ? response.blob() : response.json());
    } finally { clearTimeout(timeout); }
  }
  async function json(path, options) { return request(path, options); }
  function update(status) {
    if (bootId !== null && bootId !== status.bootId) {
      cursor = 0;
      append('\n[Device restarted — reconnecting to the new session]\n');
    }
    bootId = status.bootId;
    $('mode').textContent = modes[status.mode] || 'Mode ' + status.mode;
    $('mode-note').textContent = status.remaining ? 'Auto-stop in ' + status.remaining + ' seconds' : (status.mode ? 'Running until stopped' : 'Ready for your next command');
    $('heap').textContent = bytes(status.heap);
    $('psram').textContent = bytes(status.psram);
    const seconds = status.uptime;
    $('uptime').textContent = Math.floor(seconds / 3600) + 'h ' + Math.floor(seconds % 3600 / 60) + 'm';
    $('board').textContent = status.board;
    $('firmware').textContent = 'Marauder ' + status.version;
    $('sidebar-version').textContent = status.version + ' / N16R8';
    $('ssid').textContent = status.ssid;
    $('channel').textContent = status.channel;
    $('flash').textContent = bytes(status.flash);
    $('clients').textContent = status.clients;
    $('stop').disabled = false;
    window.MarauderFlow.update(status);
  }
  function showAccess() {
    if (accessPending) return message('Reconnect Wi-Fi to finish the password change.', true);
    $('access-title').textContent = firstSetup ? 'Choose your device password' : 'Wi-Fi access';
    $('access-current').hidden = firstSetup;
    $('access-password').value = firstSetup ? '' : key;
    $('access-new').value = ''; $('access-confirm').value = '';
    $('access-show').checked = false;
    for (const id of ['access-password','access-new','access-confirm']) $(id).type = 'password';
    $('access-error').textContent = ''; $('access-close').hidden = firstSetup;
    if (!$('access-dialog').open) $('access-dialog').showModal();
  }
  function forgetPending() { accessPending = null; sessionStorage.removeItem('marauder-access-pending'); }
  async function readStatus() {
    let status;
    try { status = await json('/api/status', accessPending ? {headers:{'X-Device-Key':accessPending.next}} : undefined); if (accessPending) key = accessPending.next; }
    catch (error) {
      if (error.status !== 401 || !accessPending) throw error;
      status = await json('/api/status', {headers:{'X-Device-Key':accessPending.previous}});
      if (status.accessChanging) return status;
      key = accessPending.previous;
      message('Change was not applied. Your previous password still works.', true);
    }
    if (accessPending) {
      if (key === accessPending.next) message('Connected with your new password.');
      sessionStorage.setItem('marauder-key', key); forgetPending(); failures = 0;
    }
    firstSetup = !!status.setupRequired;
    if (firstSetup && !$('access-dialog').open) showAccess();
    return status;
  }
  async function saveAccess(event) {
    event.preventDefault();
    if (accessSaving || accessPending) return;
    const next = $('access-new').value;
    const error = text => { $('access-error').textContent = text; };
    if (!/^[ -~]{8,63}$/.test(next) || next !== next.trim()) return error('Use 8 to 63 ASCII characters with no leading or trailing spaces.');
    if (next !== $('access-confirm').value) return error('Passwords do not match.');
    if (next === key || next === 'marauder-setup') return error('Choose a different password; the setup password is reserved.');
    accessSaving = true; $('access-save').disabled = true; $('access-close').disabled = true; clearTimeout(pollTimer);
    while (polling) await new Promise(resolve => setTimeout(resolve, 25));
    let uncertain = false;
    try { await json('/api/access', {method:'POST', body:JSON.stringify({password:next})}); }
    catch (failure) {
      if (failure.status) { error(failure.message); accessSaving = false; $('access-save').disabled = false; $('access-close').disabled = false; schedule(); return; }
      uncertain = true;
    }
    accessPending = {previous:key,next};
    sessionStorage.setItem('marauder-access-pending', JSON.stringify(accessPending));
    key = next; sessionStorage.setItem('marauder-key', key);
    $('access-dialog').close(); $('access-password').value = ''; $('access-new').value = ''; $('access-confirm').value = '';
    accessSaving = false; $('access-save').disabled = false; $('access-close').disabled = false; showTab('settings'); failures = 0;
    message((uncertain ? 'Save receipt is uncertain. ' : 'Password saved. ') + 'Reconnect to ' + $('ssid').textContent + ' using your new Wi-Fi password. If it fails, try the previous password. This page checks access without resending the change.');
    connection('Reconnect Wi-Fi', 'disconnected'); pollTimer = setTimeout(poll, 2000);
  }
  function schedule() {
    clearTimeout(pollTimer);
    if (key && !accessSaving && !document.hidden) pollTimer = setTimeout(poll, Math.min(15000, 2000 * Math.max(1, failures)));
  }
  async function poll() {
    if (!key || polling || accessSaving || document.hidden) return;
    polling = true;
    try {
      const status = await readStatus();
      if (accessSaving) return;
      update(status);
      if (firstSetup || accessPending) return;
      const log = await json('/api/log?cursor=' + cursor);
      cursor = log.cursor;
      if (log.lost) append('\n[Older output was overwritten in the device’s fixed-size log]\n');
      append(log.text);
      if (failures) message('Connection restored.');
      failures = 0;
      connection('Connected', 'connected');
    } catch (error) {
      if (accessSaving) return;
      failures++;
      if (error.status === 401) { lock(); $('login-error').textContent = error.message; }
      else {
        connection('Disconnected', 'disconnected');
        message(accessPending ? 'Reconnect your phone to the device Wi-Fi using the new password. If it fails, try the previous password. This page never repeats password changes.' : 'Connection lost. A radio operation may have changed Wi-Fi. Wait for its timer, reconnect to the device network, and this page will reconnect automatically. Commands are never resent.', true);
      }
    } finally { polling = false; schedule(); }
  }
  function showTab(name) {
    if (!headings[name]) name = 'tasks';
    activeTab = name;
    document.querySelectorAll('[data-tab]').forEach(button => {
      const current = button.dataset.tab === name || (button.dataset.tab === 'advanced' && ['console','commands'].includes(name));
      button.classList.toggle('active', current);
      button.setAttribute('aria-current', current ? 'page' : 'false');
    });
    document.querySelectorAll('.tab').forEach(tab => { tab.hidden = tab.id !== 'tab-' + name; });
    $('page-name').textContent = name === 'overview' ? 'Device status' : name[0].toUpperCase() + name.slice(1);
    [$('heading').textContent, $('subheading').textContent] = headings[name];
    if (name === 'tasks') window.MarauderFlow.activate();
    if (name === 'files' && key) loadFiles();
  }
  async function run(text, seconds = Number($('run-seconds').value)) {
    text = text.trim();
    if (commandPending) return;
    if (!text || new TextEncoder().encode(text).length > 256 || /[\x00-\x1f\x7f]/.test(text)) {
      message('Use one command, 1 to 256 bytes, with no control characters.', true); return;
    }
    if (!key) { $('device-password').focus(); return; }
    commandPending = true;
    const button = $('command-form').querySelector('button[type=submit]');
    button.disabled = true;
    try {
      await json('/api/command', {method:'POST', body:JSON.stringify({command:text, seconds})});
      append('\n› ' + text + '\n');
      history = [...history, text].slice(-30); historyIndex = history.length;
      if ($('command-input').value.trim() === text) $('command-input').value = '';
      message('Queued for execution. Read the console for results.' + (seconds ? ' Radio runs auto-stop after ' + seconds + ' seconds.' : ' No recovery timer is set. Use Stop operation or USB to recover Wi-Fi.'));
    } catch (error) {
      const network = !error.status;
      message(network ? 'Connection lost before receipt could be confirmed. The command may have run. Check device output before sending it again.' : error.message, true);
    } finally { commandPending = false; button.disabled = false; }
  }
  async function loadFiles() {
    try {
      const result = await json('/api/files?storage=spiffs');
      $('file-list').replaceChildren();
      if (!result.files.length) $('file-list').textContent = 'No capture or log files in internal storage.';
      for (const file of result.files) {
        const row = document.createElement('div'); row.className = 'file-row';
        const label = document.createElement('span'); label.textContent = file.path;
        const size = document.createElement('small'); size.textContent = bytes(file.size); label.append(size);
        const button = document.createElement('button'); button.className = 'subtle'; button.textContent = 'Download';
        button.onclick = async () => {
          button.disabled = true;
          try {
            const blob = await request('/api/file?storage=spiffs&path=' + encodeURIComponent(file.path), {}, 'blob');
            save(blob, file.path.split('/').pop());
          } catch (error) { message(error.message, true); }
          finally { button.disabled = false; }
        };
        const remove = document.createElement('button'); remove.className = 'danger-outline'; remove.textContent = 'Delete';
        remove.onclick = async () => {
          if (!confirm('Delete ' + file.path + '? This cannot be undone.')) return;
          remove.disabled = true;
          try {
            await json('/api/file/delete', {method:'POST', body:JSON.stringify({path:file.path})});
            message('File deleted.'); await loadFiles();
          } catch (error) { message(error.message, true); }
          finally { remove.disabled = false; }
        };
        const actions = document.createElement('div'); actions.className = 'button-group'; actions.append(button, remove);
        row.append(label, actions); $('file-list').append(row);
      }
    } catch (error) { $('file-list').textContent = error.message; }
  }
  function save(blob, name) {
    const url = URL.createObjectURL(blob), link = document.createElement('a');
    link.href = url; link.download = name; link.click();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
  }
  function catalog() {
    const query = $('command-search').value.toLowerCase();
    const commands = (window.MARAUDER_COMMANDS || []).filter(item => (item.syntax + item.group).toLowerCase().includes(query));
    $('catalog').replaceChildren();
    $('command-count').textContent = commands.length + ' ENTRIES';
    for (const item of commands) {
      const card = document.createElement('article'); card.className = 'command-card';
      const button = document.createElement('button'); button.textContent = item.command;
      button.setAttribute('aria-label', 'Use ' + item.syntax);
      button.onclick = () => {
        showTab('console');
        $('command-input').value = item.syntax.includes('<') || item.syntax.includes('[') || item.syntax.includes('|') ? item.command + ' ' : item.syntax;
        $('command-input').focus(); message(item.syntax);
      };
      const syntax = document.createElement('p'); syntax.textContent = item.syntax;
      const tag = document.createElement('span'); tag.className = 'tag'; tag.textContent = item.group;
      card.append(button, syntax, tag); $('catalog').append(card);
    }
    if (!commands.length) $('catalog').textContent = 'No matching commands. You can still enter commands directly in the console.';
  }
  function lock() {
    key = ''; sessionStorage.removeItem('marauder-key'); forgetPending(); clearTimeout(pollTimer);
    $('access-dialog').close(); $('access-password').value = ''; $('access-new').value = ''; $('access-confirm').value = '';
    $('dashboard').hidden = true; $('unlock').hidden = false; $('stop').disabled = true;
    $('device-password').value = ''; cursor = 0; bootId = null; consoleText = ''; $('console').textContent = '';
    connection('Locked', '');
  }
  async function unlock(candidate) {
    key = candidate;
    const button = $('login-form').querySelector('button'); button.disabled = true;
    $('login-error').textContent = '';
    try {
      const status = await readStatus();
      sessionStorage.setItem('marauder-key', key);
      $('device-password').value = '';
      $('unlock').hidden = true; $('dashboard').hidden = false;
      update(status); connection('Connected', 'connected'); showTab(firstSetup || accessPending ? 'settings' : activeTab);
      await poll();
    } catch (error) { lock(); $('login-error').textContent = error.status ? error.message : 'Device unavailable. Connect to the board’s Wi-Fi and try again.'; }
    finally { button.disabled = false; }
  }
  $('first-setup').onclick = () => unlock('marauder-setup');
  $('access-open').onclick = showAccess;
  $('access-form').onsubmit = saveAccess;
  $('access-close').onclick = () => { $('access-dialog').close(); $('access-password').value = ''; $('access-new').value = ''; $('access-confirm').value = ''; };
  $('access-dialog').oncancel = event => { if (firstSetup || accessSaving) event.preventDefault(); };
  $('access-show').onchange = () => { for (const id of ['access-password','access-new','access-confirm']) $(id).type = $('access-show').checked ? 'text' : 'password'; };
  $('login-form').onsubmit = event => { event.preventDefault(); unlock($('device-password').value); };
  $('command-form').onsubmit = event => { event.preventDefault(); run($('command-input').value); };
  $('settings-form').onsubmit = event => { event.preventDefault(); showTab('console'); run('settings -s ' + $('setting-name').value + ' ' + $('setting-value').value); };
  document.querySelectorAll('[data-tab]').forEach(button => { button.onclick = () => showTab(button.dataset.tab); });
  document.querySelectorAll('[data-command]').forEach(button => { button.onclick = () => { showTab('console'); run(button.dataset.command); }; });
  $('stop').onclick = () => window.MarauderFlow.active() ? window.MarauderFlow.cancel() : run('stopscan', 0);
  $('lock').onclick = lock;
  $('reboot').onclick = () => { if (confirm('Restart this device? The dashboard will disconnect briefly.')) run('reboot', 0); };
  $('refresh-files').onclick = loadFiles;
  $('clear-console').onclick = () => { consoleText = ''; $('console').textContent = ''; };
  $('save-console').onclick = () => save(new Blob([consoleText], {type:'text/plain'}), 'marauder-console.txt');
  $('command-search').oninput = catalog;
  $('command-input').onkeydown = event => {
    if (event.key !== 'ArrowUp' && event.key !== 'ArrowDown') return;
    event.preventDefault(); historyIndex = Math.max(0, Math.min(history.length, historyIndex + (event.key === 'ArrowUp' ? -1 : 1)));
    $('command-input').value = history[historyIndex] || '';
  };
  document.addEventListener('visibilitychange', () => { clearTimeout(pollTimer); if (!document.hidden) poll(); });
  window.addEventListener('online', () => { if (!polling) poll(); });
  window.addEventListener('pagehide', () => clearTimeout(pollTimer));
  if (location.hostname === '127.0.0.1' || location.hostname === 'localhost') $('preview-banner').hidden = false;
  window.MarauderFlow.mount({request, json, showTab, message, save});
  document.querySelector('.brand').onclick = event => { event.preventDefault(); showTab('tasks'); };
  catalog();
  if (key) unlock(key);
})();

'use strict';
window.MarauderFlow = (() => {
  const tasks = {
    wifi_discover:['Discover Wi-Fi networks','Find nearby networks, channels and signal strength.','Wi-Fi'],
    wifi_inspect:['Inspect a Wi-Fi network','Choose a network and read its discovery snapshot.','Wi-Fi'],
    wifi_signal:['Track a Wi-Fi signal','Follow the signal strength of a chosen network.','Wi-Fi'],
    ble_discover:['Discover Bluetooth devices','Find nearby Bluetooth Low Energy devices.','Bluetooth'],
    ble_inspect:['Inspect a Bluetooth device','Choose a device and read its discovery snapshot.','Bluetooth'],
    ble_signal:['Track a Bluetooth signal','Follow the signal strength of a chosen device.','Bluetooth'],
    wifi_capture:['Record Wi-Fi traffic','Save a timed recording of one radio channel.','Captures']
  };
  const catalog = window.MARAUDER_TOOLS || [];
  const toolById = id => catalog.find(item => item.id === id);
  let tool = null, inputs = {}, selectedTargets = {}, activeField = null, toolResume = null, query = '';
  let output = '', outputCursor = 0, outputId = 0, outputBusy = false, outputLost = false, outputLimited = false, outputMore = false;
  const draftStorage = 'marauder-draft';
  const running = job => job && ['queued','running','recovering'].includes(job.state);
  const storage = 'marauder-flow';
  let hooks, root, content, latest = null, boot = null, tracked = null;
  let kind = '', target = null, seconds = 30, channel = 0, view = 'home', resume = '';
  let busy = false, cancelPending = false, pickerVersion = 0, page = 0, total = 0, shownId = 0, pendingShown = false;
  let targets = [], notice = '', noticeError = false;
  function el(tag, cls, text) {
    const node = document.createElement(tag);
    if (cls) node.className = cls;
    if (text !== undefined) node.textContent = text;
    return node;
  }
  function button(text, action, cls = 'subtle', test = '') {
    const node = el('button', cls, text); node.type = 'button'; node.onclick = action;
    if (test) node.dataset.testid = test;
    return node;
  }
  function title(value, id) { const row = toolById(id || (value === 'tool' && tool ? tool.id : '')); return row ? row.title : tasks[value] ? tasks[value][0] : 'Device task'; }
  function wifi() { return kind.startsWith('wifi_'); }
  function targeted() { return /_(inspect|signal)$/.test(kind) || (kind === 'wifi_capture' && target); }
  function persist() {
    try {
      if (tracked) sessionStorage.setItem(storage, JSON.stringify(tracked));
      else sessionStorage.removeItem(storage);
    } catch { /* Device status remains authoritative. */ }
    saveDraft();
  }
  function publicInputs(row, values) { return Object.fromEntries(Object.entries(values).filter(([name]) => !row || !(row.fields || []).some(field => field.name === name && field.type === 'password'))); }
  function saveDraft() {
    try {
      if (kind) sessionStorage.setItem(draftStorage, JSON.stringify({kind,tool:tool && tool.id,inputs:publicInputs(tool,inputs),selectedTargets,seconds,channel,target,view,resume,toolResume,secretsOmitted:!!(tool && (tool.fields || []).some(field => field.type === 'password' && inputs[field.name])),activeField:activeField && activeField.name}));
      else sessionStorage.removeItem(draftStorage);
    } catch { /* Storage may be disabled. */ }
  }
  function alert(text, error = false) {
    notice = text; noticeError = error;
    const node = document.getElementById('flow-notice');
    if (node) { node.textContent = text; node.className = 'notice' + (error ? ' error' : ''); node.hidden = !text; }
  }
  function activate() {
    if (!root || root.hidden) return;
    document.getElementById('heading').textContent = view === 'home' ? 'Choose your next task' : title(kind);
    document.getElementById('subheading').textContent = view === 'home' ? 'Wi-Fi, Bluetooth, network and device tools. Guided from start to result.' : 'A few simple choices. Your device handles the operation.';
  }
  function shell(step) {
    root.replaceChildren();
    const top = el('div', 'flow-toolbar');
    top.append(button('All tasks', home, 'subtle', 'flow-home'), el('span', 'eyebrow', title(kind)));
    root.append(top);
    const steps = el('ol', 'flow-steps');
    ['Choose','Set up','Review','Run'].forEach((text, index) => {
      const node = el('li', index <= step ? 'reached' : '', text);
      if (index === step) node.setAttribute('aria-current', 'step');
      steps.append(node);
    });
    const note = el('p', 'notice' + (noticeError ? ' error' : ''), notice);
    note.id = 'flow-notice'; note.hidden = !notice; note.setAttribute('role', 'status');
    content = el('article', 'panel flow-panel'); root.append(steps, note, content);
    activate();
  }
  function home() {
    pickerVersion++; view = 'home'; notice = ''; noticeError = false; kind = ''; tool = null; saveDraft();
    root.replaceChildren();
    if (latest && latest.id) {
      const recent = el('div', 'flow-recent');
      recent.append(el('div', '', title(latest.kind, latest.tool) + ' · ' + latest.state),
        button(running(latest) ? 'View running task' : 'View latest result', () => showJob(), 'subtle', 'flow-latest'));
      root.append(recent);
    }
    const search = el('input'); search.type = 'search'; search.placeholder = 'Search tasks, features or devices…';
    search.setAttribute('aria-label', 'Search tasks'); search.id = 'task-search'; search.value = query;
    const count = el('p', 'muted small'); count.id = 'task-count'; count.setAttribute('role','status');
    const groups = el('div', 'task-groups');
    const category = el('select'); category.setAttribute('aria-label','Task category'); category.id = 'task-category';
    const all = el('option','','All categories'); all.value = ''; category.append(all);
    [...new Set(catalog.map(row => row.group))].forEach(name => { const option = el('option','',name); option.value = name; category.append(option); });
    const filters = el('div','task-filters'); filters.append(search,category); root.append(filters,count,groups);
    function render() {
      groups.replaceChildren();
      const rows = catalog.length ? catalog : Object.entries(tasks).map(([id,t]) => ({id,legacy:id,title:t[0],description:t[1],group:t[2],available:true}));
      const match = rows.filter(row => (!category.value || row.group === category.value) && [row.title,row.description,row.group,row.reason,...(row.commands || [])].join(' ').toLowerCase().includes(query.toLowerCase()));
      count.textContent = match.length + ' tasks · ' + match.filter(row => row.available).length + ' available on this board';
      const names = [...new Set(['Wi-Fi','Bluetooth','Network','Captures','Targets','Device','Hardware',...match.map(row => row.group)])];
      names.forEach((group, index) => {
        const found = match.filter(row => row.group === group);
        if (!found.length && !(group === 'Captures' && !query)) return;
        const section = el('section', 'task-group');
        const heading = el('div', 'task-group-heading');
        heading.append(el('span', 'task-number', String(index + 1).padStart(2,'0')),el('h2','',group));
        const cards = el('div','task-cards');
        found.forEach(row => {
          const card = button('', () => row.available && row.legacy ? choose(row.legacy) : chooseTool(row.id), 'task-card' + (!row.available ? ' unavailable' : ''));
          card.dataset.flow = row.legacy || row.id; card.dataset.tool = row.id; card.setAttribute('aria-label',row.title);
          card.append(el('span','task-arrow',row.available ? '↗' : '—'),el('strong','',row.title),el('small','',row.description));
          if (!row.available) card.append(el('small','tool-reason',row.reason || 'Unavailable on this board.'));
          cards.append(card);
        });
        if (group === 'Captures' && !query && !catalog.some(row => row.view === 'files')) {
          const card = button('',() => hooks.showTab('files'),'task-card'); card.dataset.flow = 'files'; card.setAttribute('aria-label','Browse saved captures');
          card.append(el('strong','','Browse saved captures'),el('small','','Download recordings and logs from your device.')); cards.append(card);
        }
        section.append(heading,cards); groups.append(section);
      });
      if (!match.length) groups.append(el('p','muted','No matching tasks. Try another feature or device name.'));
    }
    search.oninput = () => { query = search.value; render(); }; category.onchange = render; render();
    root.append(el('p','flow-footnote','One task at a time. Timed radio tasks stop on the device and restore its Wi-Fi. Reconnect to follow progress.'));
    activate();
  }
  function needsJoin() { return kind === 'tool' && tool && tool.requires === 'joined'; }
  function offerJoin() { content.append(button('Connect to Wi-Fi first', connectFirst, 'subtle', 'flow-connect')); }
  function connectFirst() {
    if (busy || running(latest) || (tracked && tracked.awaiting)) return;
    const join = toolById('network_join');
    if (!join || !join.available) { alert(join && join.reason || 'Wi-Fi connection setup is unavailable.',true); return; }
    const original = toolResume && toolById(toolResume.tool) && toolById(toolResume.tool).requires === 'joined' ? {...toolResume} : {
      tool:tool.id,inputs:publicInputs(tool,inputs),selectedTargets,field:view === 'targets' && activeField ? activeField.name : null,seconds,channel,resume:toolResume
    };
    original.prerequisite = 'joined'; toolResume = original;
    tracked = null; tool = join; kind = 'tool'; target = null; activeField = null; inputs = {}; selectedTargets = {}; seconds = join.timed ? 30 : 1; channel = 0;
    for (const field of join.fields || []) if (field.default !== undefined) inputs[field.name] = field.default;
    notice = 'Connect the device to a Wi-Fi network. Your original task will resume after connection succeeds.'; noticeError = false; persist(); setup();
  }
  function chooseTool(id) {
    const next = toolById(id); if (!next) return;
    if (next.view === 'files') { hooks.showTab('files'); return; }
    if (busy || (running(latest) && !canControl(next)) || (tracked && tracked.awaiting)) { showJob(); return; }
    if (!running(latest)) tracked = null; tool = next; kind = 'tool'; target = null; inputs = {}; selectedTargets = {}; activeField = null;
    toolResume = null; resume = ''; seconds = next.timed ? 30 : 1; channel = 0; notice = ''; noticeError = false;
    for (const field of next.fields || []) if (field.default !== undefined) inputs[field.name] = field.default;
    persist(); setup();
  }
  async function uploadTemplate(file, field, node, status, form, submit) {
    const path = file.name.replace(/\.html$/i,'.html');
    if (!file.size || file.size > 8192) throw new Error('Choose an HTML file from 1 to 8192 bytes.');
    if (path.length > 26 || !/^[A-Za-z0-9_-]+\.html$/i.test(path)) throw new Error('Use an HTML filename up to 26 characters, with letters, numbers, dashes or underscores before .html.');
    const text = new TextDecoder('utf-8',{fatal:true,ignoreBOM:true}).decode(await file.arrayBuffer());
    const encoder = new TextEncoder(), total = encoder.encode(text).length;
    if (!total || total > 8192) throw new Error('Choose an HTML file from 1 to 8192 bytes.');
    const chunks = []; let data = '', offset = 0;
    for (const character of text) {
      const candidate = data + character;
      const request = {path,offset,data:candidate,final:false,total};
      if (encoder.encode(candidate).length > 256 || encoder.encode(JSON.stringify(request)).length > 512) {
        chunks.push(data); offset += encoder.encode(data).length; data = character;
      } else data = candidate;
    }
    if (data) chunks.push(data);
    offset = 0;
    for (let index = 0; index < chunks.length; index++) {
      if (!root.contains(form) || view !== 'setup') throw new Error('Upload interrupted because you left this form. The uploaded page was not selected.');
      const chunk = chunks[index], final = index === chunks.length - 1;
      status.textContent = 'Uploading ' + path + '… ' + offset + ' of ' + total + ' bytes';
      let response;
      try { response = await hooks.json('/api/template',{method:'POST',body:JSON.stringify({path,offset,data:chunk,final,total})}); }
      catch (error) { throw new Error(error.status ? 'Upload failed: ' + error.message + '. The previous page remains selected.' : 'Upload not confirmed. The file may have been saved. Nothing was resent; the filename selection was not changed. Select the file again to start a new upload.'); }
      offset += encoder.encode(chunk).length;
      if (String(response.path).replace(/^\//,'') !== path || response.offset !== offset || response.complete !== final) throw new Error('Upload acknowledgement did not match the file. Nothing was resent; the previous page remains selected.');
    }
    if (!root.contains(form) || view !== 'setup') throw new Error('Upload finished after you left this form. Return and choose its filename to use it.');
    node.value = path; inputs[field.name] = path; saveDraft(); status.textContent = path + ' is ready. ' + total + ' bytes uploaded.';
  }
  function toolSetup() {
    content.append(el('h2','','Set up your task'),el('p','muted',tool.description));
    if (!tool.available) { content.append(el('p','notice error',tool.reason || 'Unavailable on this board.')); return; }
    if (needsJoin()) { content.append(el('p','muted','This task uses the device’s connection to a Wi-Fi network. Continue if it is already connected, or connect first.')); offerJoin(); }
    const form = el('form','flow-form'); form.noValidate = true;
    const controls = new Map(); let uploading = false;
    for (const field of tool.fields || []) {
      if (field.type === 'target') {
        const box = el('div','flow-field');
        const chosen = selectedTargets[field.name];
        box.append(el('span','',field.label + (field.required ? ' (required)' : ' (optional)')));
        box.append(button('Choose ' + field.label,() => { collect(false); activeField = field; loadTargets(); },'subtle','tool-target-' + field.name));
        if (inputs[field.name]) box.append(el('p','flow-selection',chosen ? (Array.isArray(chosen) ? chosen.map(row => row.name || row.id).join(', ') : chosen.name || chosen.id) : String(inputs[field.name])));
        if (inputs[field.name] && !field.required) box.append(button('Clear ' + field.label,() => { delete inputs[field.name]; delete selectedTargets[field.name]; setup(); }));
        form.append(box); continue;
      }
      if (field.format === 'filter') {
        const match = el('select'); match.setAttribute('aria-label','Match');
        [['contains','Contains'],['equals','Exact']].forEach(([value,label]) => { const option = el('option','',label); option.value = value; match.append(option); });
        const node = el('input'); node.type = 'text'; node.setAttribute('aria-label','Network name'); node.id = 'tool-field-' + field.name; node.required = !!field.required;
        const saved = /^(contains|equals) (.*)$/.exec(inputs[field.name] || ''); match.value = saved ? saved[1] : 'contains'; node.value = saved ? saved[2] : '';
        node.maxLength = (field.maxLength || 80) - match.value.length - 1; node.filterMatch = match; controls.set(field.name,node);
        node.oninput = () => { collect(false); saveDraft(); }; match.onchange = () => { node.maxLength = (field.maxLength || 80) - match.value.length - 1; collect(false); saveDraft(); };
        form.append(labeled('Match',match),labeled('Network name',node)); continue;
      }
      const node = el(field.type === 'select' ? 'select' : 'input');
      node.setAttribute('aria-label',field.label); node.id = 'tool-field-' + field.name; node.name = field.name; node.dataset.testid = node.id;
      if (field.type === 'select') {
        if (!field.required) { const blank = el('option','','Choose an option'); blank.value = ''; node.append(blank); }
        (field.options || []).forEach(option => { const item = el('option','',option.label); item.value = option.value; node.append(item); });
      } else node.type = field.type === 'boolean' ? 'checkbox' : ['integer','number'].includes(field.type) ? 'number' : field.type === 'password' ? 'password' : 'text';
      node.required = !!field.required;
      if (field.min !== undefined) node.min = field.min;
      if (field.max !== undefined) node.max = field.max;
      if (field.maxLength !== undefined) node.maxLength = field.maxLength;
      if (['integer','number'].includes(field.type)) node.step = field.type === 'integer' ? '1' : 'any';
      if (field.type === 'password') node.autocomplete = 'new-password';
      if (field.type === 'boolean') node.checked = !!inputs[field.name]; else node.value = inputs[field.name] === undefined ? '' : String(inputs[field.name]);
      controls.set(field.name,node);
      node.oninput = () => { collect(false); saveDraft(); };
      form.append(labeled(field.label,node));
      if (field.format === 'public_html') {
        const upload = el('input'); upload.type = 'file'; upload.accept = '.html,text/html'; upload.setAttribute('aria-label','Upload HTML page');
        const status = el('p','muted small','Use the built-in index.html page, an existing filename, or upload an HTML page up to 8 KiB.'); status.setAttribute('role','status'); status.dataset.testid = 'template-upload-status';
        form.append(labeled('Upload HTML page',upload),status);
        upload.onchange = async () => {
          const file = upload.files[0]; if (!file || uploading) return;
          uploading = true; status.className = 'muted small'; submit.disabled = true; node.disabled = true; upload.disabled = true;
          try { await uploadTemplate(file,field,node,status,form,submit); }
          catch (error) { status.textContent = error.message; status.className = 'notice error'; if (!root.contains(form)) hooks.message(error.message,true); }
          finally { uploading = false; submit.disabled = false; node.disabled = false; upload.disabled = false; upload.value = ''; }
        };
      }
    }
    if (tool.channel) {
      const select = el('select'); select.setAttribute('aria-label','Radio channel'); select.dataset.testid = 'flow-channel';
      for (let number = 0; number <= 14; number++) { const option = el('option','',number ? 'Channel ' + number : 'All supported channels'); option.value = number; select.append(option); }
      select.value = channel; select.onchange = () => { channel = Number(select.value); saveDraft(); }; form.append(labeled('Radio channel',select));
    }
    let duration;
    if (tool.timed) {
      duration = el('input'); duration.type = 'number'; duration.min = '1'; duration.max = '300'; duration.step = '1'; duration.value = seconds; duration.dataset.testid = 'flow-seconds';
      duration.oninput = () => { seconds = Number(duration.value); saveDraft(); };
      form.append(labeled('Duration in seconds',duration),el('p','muted small','1–300 seconds. The device stops automatically, even if this page disconnects.'));
    } else form.append(el('p','muted','The device runs the command sequence once and reports its output.'));
    function collect(validate) {
      if (validate && uploading) { alert('Wait for the HTML upload to finish before reviewing.',true); return false; }
      for (const field of tool.fields || []) {
        if (field.type === 'target') {
          if (validate && field.required && (!inputs[field.name] || (Array.isArray(inputs[field.name]) && !inputs[field.name].length))) { alert('Choose ' + field.label + ' before continuing.',true); return false; }
          continue;
        }
        const node = controls.get(field.name);
        if (validate && !node.checkValidity()) { alert('Check ' + field.label + '. ' + node.validationMessage,true); node.focus(); return false; }
        let value = field.type === 'boolean' ? node.checked : node.value;
        if (field.type !== 'boolean' && value === '') { delete inputs[field.name]; continue; }
        if (['integer','number'].includes(field.type)) value = Number(value);
        if (validate && typeof value === 'string' && /[\x00-\x1f\x7f]/.test(value)) { alert('Use a single line for ' + field.label + '.',true); node.focus(); return false; }
        if (field.format === 'filter') value = node.filterMatch.value + ' ' + value;
        inputs[field.name] = value;
      }
      if (duration) {
        const value = Number(duration.value);
        if (validate && (!duration.value || !Number.isInteger(value) || value < 1 || value > 300)) { alert('Choose a whole-number duration from 1 to 300 seconds.',true); duration.focus(); return false; }
        seconds = value;
      }
      return true;
    }
    const submit = el('button','primary','Review task'); submit.type = 'submit'; submit.dataset.testid = 'flow-review'; form.append(submit); content.append(form);
    form.onsubmit = event => { event.preventDefault(); if (collect(true)) { notice = ''; noticeError = false; review(); } };
  }
  function toolPicker() {
    content.append(el('h2','','Choose ' + activeField.label),el('p','muted','Choose a discovered identity. The device checks that it is still available before running.'));
    const list = el('div','target-list'); list.id = 'flow-targets';
    const selection = activeField.multiple ? [...(Array.isArray(inputs[activeField.name]) ? inputs[activeField.name] : [])] : [];
    const items = activeField.multiple ? [...(selectedTargets[activeField.name] || [])] : [];
    targets.forEach(item => {
      const name = item.name || item.id;
      const text = [item.id,item.rssi ? item.rssi + ' dBm' : '',item.channel ? 'Channel ' + item.channel : '',item.security || ''].filter(Boolean).join(' · ');
      if (activeField.multiple) {
        const check = el('input'); check.type = 'checkbox'; check.value = item.id; check.checked = selection.includes(item.id);
        check.onchange = () => {
          const index = selection.indexOf(item.id);
          if (check.checked && index < 0) { if (selection.length >= (activeField.max || 8)) { check.checked = false; alert('Choose no more than ' + (activeField.max || 8) + ' targets.',true); return; } selection.push(item.id); items.push(item); }
          else if (!check.checked && index >= 0) { selection.splice(index,1); items.splice(index,1); }
          inputs[activeField.name] = [...selection]; selectedTargets[activeField.name] = [...items]; saveDraft();
        };
        const wrap = labeled(name,check); wrap.append(el('small','target-meta',text)); list.append(wrap);
      } else {
        const row = button('',() => { inputs[activeField.name] = item.id; selectedTargets[activeField.name] = item; notice = ''; setup(); },'target-choice','flow-target');
        row.dataset.target = item.id; row.setAttribute('aria-label',name); row.append(el('strong','',name),el('span','target-meta',text)); list.append(row);
      }
    });
    if (!targets.length) list.append(el('p','muted','No devices found yet.'));
    content.append(list);
    const actions = el('div','button-group flow-actions');
    if (activeField.multiple) actions.append(button('Use selected targets',() => { inputs[activeField.name] = selection; selectedTargets[activeField.name] = items; setup(); },'primary'));
    if (activeField.discover) actions.append(button('Discover devices first',() => {
      toolResume = {tool:tool.id,inputs:publicInputs(tool,inputs),selectedTargets,field:activeField.name,seconds,channel,resume:toolResume};
      const discover = toolById(activeField.discover);
      if (discover && !discover.available) { alert(discover.reason || 'Discovery is unavailable on this board.',true); return; }
      target = null; activeField = null; inputs = {}; selectedTargets = {}; channel = 0;
      if (discover && !discover.legacy) { tool = discover; kind = 'tool'; seconds = discover.timed ? 30 : 1; for (const field of discover.fields || []) if (field.default !== undefined) inputs[field.name] = field.default; }
      else { kind = discover ? discover.legacy : toolResume && toolById(toolResume.tool).fields.find(field => field.name === toolResume.field).discover; tool = null; seconds = 30; }
      notice = ''; setup();
    },targets.length ? 'subtle' : 'primary','flow-discover'));
    else if (!targets.length) content.append(el('p','notice','This list has no entries yet. Add or discover entries using the matching task, then return here.'));
    actions.append(button('Refresh targets',() => loadTargets(),'subtle','flow-refresh'));
    if (targets.length < total) actions.append(button('Load more targets',() => loadTargets(true),'subtle','flow-more'));
    actions.append(button('Back to setup',setup)); content.append(actions);
  }
  function toolReview() {
    content.append(el('h2','','Ready to run?'));
    const summary = el('dl','details flow-summary'); detail(summary,'Task',tool.title);
    for (const field of tool.fields || []) {
      if (inputs[field.name] === undefined) continue;
      const chosen = selectedTargets[field.name];
      let value = inputs[field.name];
      if (field.format === 'filter') { const match = /^(contains|equals) (.*)$/.exec(value); detail(summary,'Match',match && match[1] === 'equals' ? 'Exact' : 'Contains'); detail(summary,'Network name',match ? match[2] : ''); continue; }
      if (field.type === 'password') value = 'Provided (hidden)';
      else if (field.type === 'boolean') value = value ? 'Enabled' : 'Disabled';
      else if (field.type === 'select') value = (field.options.find(option => String(option.value) === String(value)) || {}).label || value;
      else if (chosen) value = (Array.isArray(chosen) ? chosen : [chosen]).map(item => (item.name ? item.name + ' · ' : '') + item.id).join(', ');
      detail(summary,field.label,String(value));
    }
    if (tool.timed) detail(summary,'Duration',seconds + ' seconds');
    if (tool.channel) detail(summary,'Radio channel',channel ? 'Channel ' + channel : 'All supported channels');
    content.append(summary,el('p','muted',tool.timed ? 'The device runs and stops this task automatically. Reconnect to the board if Wi-Fi disconnects.' : 'The device executes this task once. Its reported output appears in the result.'));
    const actions = el('div','button-group flow-actions'); actions.append(button('Run task',start,'primary','flow-run'),button('Back to setup',setup)); content.append(actions);
  }
  let outputDone = false;
  function outputNote() {
    if (outputLost) return 'Earlier output was overwritten in the device log. Showing available output.';
    if (outputLimited) return 'Earlier output was omitted from this view. Showing the most recent output.';
    if (outputMore) return 'More output remains. Reading continues with the next status update.';
    return 'Device output confirms what the program did. Command completion does not guarantee an external effect.';
  }
  async function readOutput(job) {
    if (job.logStart === undefined || outputBusy || (outputId === job.id && outputDone)) return;
    if (outputId !== job.id) { outputId = job.id; outputCursor = job.logStart; output = ''; outputLost = false; outputLimited = false; outputMore = false; outputDone = false; }
    outputBusy = true;
    const terminal = !running(job);
    try {
      for (let chunk = 0; chunk < (terminal ? 9 : 1); chunk++) {
        const before = outputCursor;
        const log = await hooks.json('/api/log?cursor=' + before);
        if (!latest || latest.id !== job.id) return;
        outputCursor = log.cursor; outputLost = outputLost || !!log.lost;
        const combined = output + (log.text || ''); outputLimited = outputLimited || combined.length > 8192; output = combined.slice(-8192);
        const end = outputCursor === before;
        outputDone = terminal && end; outputMore = terminal && !end;
        if (view === 'job') {
          const node = document.getElementById('flow-output'); if (node) node.textContent = output;
          document.getElementById('flow-output-note').textContent = outputNote();
        }
        if (end) break;
      }
    } catch { if (view === 'job') document.getElementById('flow-output-note').textContent = 'Output unavailable. Reconnect to the device; status polling will try reading it again.'; }
    finally { outputBusy = false; }
  }
  function choose(id) {
    if (busy || running(latest) || (tracked && tracked.awaiting)) { showJob(); return; }
    tracked = null; persist(); resume = ''; notice = ''; noticeError = false;
    tool = null; inputs = {}; selectedTargets = {}; toolResume = null; activeField = null; kind = id; target = null; seconds = 30; channel = kind === 'wifi_capture' ? 1 : 0;
    saveDraft(); if (targeted()) loadTargets(); else setup();
  }
  function picker() {
    view = 'targets'; saveDraft(); shell(0);
    if (kind === 'tool') { toolPicker(); return; }
    content.append(el('h2', '', wifi() ? 'Choose a network' : 'Choose a device'),
      el('p', 'muted', 'Select a result from the latest discovery. Targets are identified by their device address.'));
    const list = el('div', 'target-list'); list.id = 'flow-targets';
    targets.forEach(item => {
      const row = button('', () => { target = item; notice = ''; setup(); }, 'target-choice', 'flow-target');
      row.dataset.target = item.id; row.setAttribute('aria-label', item.name || item.id);
      const name = el('span'); name.append(el('strong', '', item.name || 'Unnamed device'), el('small', '', item.id));
      const detail = [item.rssi + ' dBm'];
      if (wifi()) { detail.push('Channel ' + item.channel); if (item.security) detail.push(item.security); }
      row.append(name, el('span', 'target-meta', detail.join(' · '))); list.append(row);
    });
    if (!targets.length) list.append(el('p', 'muted', 'No devices found yet.'));
    content.append(list);
    const actions = el('div', 'button-group flow-actions');
    actions.append(button('Discover devices first', () => {
      resume = kind; kind = wifi() ? 'wifi_discover' : 'ble_discover'; target = null; channel = 0; notice = ''; setup();
    }, targets.length ? 'subtle' : 'primary', 'flow-discover'));
    actions.append(button('Refresh targets', () => loadTargets(), 'subtle', 'flow-refresh'));
    if (targets.length < total) actions.append(button('Load more targets', () => loadTargets(true), 'subtle', 'flow-more'));
    if (kind === 'wifi_capture') actions.append(button('Choose a channel instead', () => { target = null; setup(); }));
    content.append(actions);
  }
  async function loadTargets(more = false) {
    view = 'targets'; const version = ++pickerVersion;
    if (!more) { page = 0; targets = []; total = 0; }
    shell(0); content.append(el('h2', '', 'Loading discovered devices…'));
    try {
      const nextPage = more ? page + 1 : 0;
      const result = await hooks.json('/api/targets?kind=' + encodeURIComponent(kind === 'tool' && activeField ? activeField.kind : (wifi() ? 'wifi' : 'ble')) + '&page=' + nextPage);
      if (version !== pickerVersion || view !== 'targets') return;
      page = result.page; total = result.total;
      targets = more ? [...targets, ...result.targets] : result.targets;
      picker();
    } catch (error) {
      if (version !== pickerVersion || view !== 'targets') return;
      notice = error.status === 409 && !needsJoin() ? 'The radio is busy. Wait for the current operation to finish, then refresh targets.' : error.message;
      noticeError = true; picker();
      if (error.status === 409 && needsJoin()) offerJoin();
    }
  }
  function labeled(label, input) {
    const wrap = el('label', 'flow-field'); wrap.append(el('span', '', label), input); return wrap;
  }
  function setup() {
    pickerVersion++; view = 'setup'; saveDraft(); shell(1);
    if (kind === 'tool') { toolSetup(); return; }
    content.append(el('h2', '', 'Set up your task'), el('p', 'muted', tasks[kind][1]));
    if (target) content.append(el('div', 'flow-selection', (target.name || 'Unnamed device') + ' · ' + target.id));
    const form = el('form', 'flow-form'); form.noValidate = true;
    const inspection = kind.endsWith('_inspect');
    if (inspection) seconds = 1;
    const duration = el('input'); duration.type = 'number'; duration.min = '1'; duration.max = '300'; duration.step = '1';
    duration.required = true; duration.value = seconds; duration.oninput = () => { seconds = Number(duration.value); saveDraft(); }; duration.dataset.testid = 'flow-seconds';
    if (inspection) form.append(el('p', 'muted', 'Inspection reads the latest discovery snapshot. No timed radio operation is needed.'));
    else form.append(labeled('Duration in seconds', duration), el('p', 'muted small', '1–300 seconds. The device stops automatically, even if this page disconnects.'));
    if (wifi() && !target && (kind.endsWith('_discover') || kind === 'wifi_capture')) {
      const select = el('select'); select.setAttribute('aria-label','Radio channel'); select.dataset.testid = 'flow-channel';
      if (kind.endsWith('_discover')) { const all = el('option', '', 'All supported channels'); all.value = '0'; select.append(all); }
      for (let number = 1; number <= 14; number++) {
        const option = el('option', '', 'Channel ' + number); option.value = number; select.append(option);
      }
      select.value = channel;
      select.onchange = () => { channel = Number(select.value); };
      form.append(labeled('Radio channel', select));
    }
    if (kind === 'wifi_capture') {
      form.append(el('p', 'notice', 'This records the entire selected radio channel, including traffic from other networks. Recording must be enabled in device settings and storage must have room.'));
      form.append(button(target ? 'Choose another target' : 'Choose a network instead', () => { seconds = Number(duration.value) || 30; loadTargets(); }, 'subtle', 'flow-choose-target'));
    } else if (target) form.append(button('Choose another target', () => loadTargets(), 'subtle', 'flow-choose-target'));
    const submit = el('button', 'primary', 'Review task'); submit.type = 'submit'; submit.dataset.testid = 'flow-review';
    form.append(submit); content.append(form);
    form.onsubmit = event => {
      event.preventDefault(); const value = inspection ? 1 : Number(duration.value);
      if (!inspection && (!duration.value || !Number.isInteger(value) || value < 1 || value > 300)) {
        alert('Choose a whole-number duration from 1 to 300 seconds.', true); duration.focus(); return;
      }
      if (target && !/^([0-9a-f]{2}:){5}[0-9a-f]{2}$/i.test(target.id)) { alert('This target has no valid device address. Refresh targets.', true); return; }
      seconds = value; notice = ''; noticeError = false; review();
    };
  }
  function detail(list, name, value) {
    const row = el('div'); row.append(el('dt', '', name), el('dd', '', value)); list.append(row);
  }
  function review() {
    view = 'review'; saveDraft(); shell(2);
    if (kind === 'tool') { toolReview(); return; } content.append(el('h2', '', 'Ready to run?'));
    const summary = el('dl', 'details flow-summary');
    detail(summary, 'Task', title(kind));
    detail(summary, kind.endsWith('_inspect') ? 'Result' : 'Duration', kind.endsWith('_inspect') ? 'Latest discovery snapshot' : seconds + ' seconds');
    if (target) {
      detail(summary, 'Target', target.name || 'Unnamed device'); detail(summary, 'Address', target.id);
      detail(summary, 'Last signal', target.rssi + ' dBm');
      if (wifi()) { detail(summary, 'Channel', target.channel); detail(summary, 'Security', target.security || 'Unknown'); }
    } else if (wifi()) detail(summary, 'Channel', channel ? channel : 'All supported channels');
    content.append(summary);
    content.append(el('p', 'muted', kind === 'wifi_capture' ? 'Records the entire selected radio channel. The device restores its Wi-Fi when the task ends.' : 'The device will run and stop this task automatically. If Wi-Fi disconnects, reconnect to the board to see the result.'));
    const actions = el('div', 'button-group flow-actions');
    actions.append(button('Run task', start, 'primary', 'flow-run'), button('Back to setup', setup));
    content.append(actions);
  }
  function canControl(row) { return row && (row.stop || row.during === 'read' || (row.during === 'csi' && latest && latest.tool === 'wifi_csi')); }
  async function startControl(payload, selected) {
    busy = true; showJob(); alert('Sending ' + selected.title + '…');
    try {
      const response = await hooks.json('/api/workflow',{method:'POST',body:JSON.stringify(payload)});
      const job = response.workflow || response; if (job.id) latest = job;
      if (response.side && response.logStart !== undefined) { outputId = latest.id; outputCursor = response.logStart; output = ''; outputLost = false; outputLimited = false; outputMore = false; outputDone = false; }
      showJob(); alert(response.side ? selected.title + ' command accepted. Read the device output; the original task is still running.' : 'Stop requested. Waiting for the device to confirm recovery.');
    } catch (error) { alert(error.status ? error.message : 'Control request receipt was not confirmed. It may have run. The original task timer continues; nothing will be resent.',true); }
    finally { busy = false; paintJob(); }
  }
  async function start() {
    if (busy || (tracked && tracked.awaiting)) return;
    const control = running(latest) && kind === 'tool' && canControl(tool);
    if (running(latest) && !control) return;
    busy = true;
    const payload = kind === 'tool' ? {action:'start',kind:'tool',tool:tool.id,inputs,seconds,channel} : {action:'start', kind, seconds, channel:target && wifi() ? target.channel : channel};
    if (new TextEncoder().encode(JSON.stringify(payload)).length > 512) { busy = false; alert('These choices exceed the device request limit. Shorten text values.',true); return; }
    if (target) payload.target = target.id;
    if (control) { await startControl(payload,tool); return; }
    tracked = {boot, id:0, before:latest ? latest.id : 0, awaiting:true, kind, tool:tool && tool.id, resume, toolResume}; persist();
    notice = ''; noticeError = false; showJob();
    try {
      const result = await hooks.json('/api/workflow', {method:'POST', body:JSON.stringify(payload)});
      const accepted = result.workflow || result;
      if (accepted.id) { tracked.id = accepted.id; tracked.awaiting = false; latest = accepted; persist(); }
      notice = ''; showJob();
    } catch (error) {
      if (error.status) {
        tracked = null; persist(); notice = error.message; noticeError = true;
        review();
        if (error.status === 409 && needsJoin()) offerJoin();
        if (error.status === 404 && (target || (tool && tool.fields.some(field => field.type === 'target')))) content.append(button('Choose another target', () => { notice = ''; if (kind === 'tool') activeField = tool.fields.find(field => field.type === 'target'); loadTargets(); }, 'subtle', 'flow-choose-target'));
      } else {
        alert('Receipt not confirmed. This task may have started. Reconnect to the device’s Wi-Fi; checking status will resume monitoring. It will not be sent again.', true);
      }
    } finally { busy = false; paintJob(); }
  }
  function showJob() {
    if (tracked && tracked.awaiting) pendingShown = true;
    if (latest && latest.id && !(tracked && tracked.awaiting)) { shownId = latest.id; kind = latest.kind; tool = toolById(latest.tool) || null; }
    pickerVersion++; view = 'job'; shell(3); content.id = 'flow-job';
    const heading = el('h2'); heading.id = 'flow-job-heading';
    const description = el('p', 'muted'); description.id = 'flow-job-description';
    const progress = el('progress'); progress.id = 'flow-progress'; progress.max = 300;
    progress.setAttribute('aria-label', 'Task progress');
    const summary = el('dl', 'details flow-summary'); summary.id = 'flow-job-summary';
    const actions = el('div', 'button-group flow-actions'); actions.id = 'flow-job-actions';
    const out = el('pre','task-output'); out.id = 'flow-output'; out.setAttribute('aria-label','Task output'); out.tabIndex = 0;
    const outputNote = el('p','muted small',''); outputNote.id = 'flow-output-note';
    content.append(heading, description, progress, summary, outputNote, out, actions); paintJob();
  }
  function paintJob() {
    if (view !== 'job') return;
    const job = latest;
    const pending = tracked && tracked.awaiting;
    const heading = document.getElementById('flow-job-heading');
    const description = document.getElementById('flow-job-description');
    const progress = document.getElementById('flow-progress');
    const summary = document.getElementById('flow-job-summary');
    const actions = document.getElementById('flow-job-actions');
    if (pending) {
      heading.textContent = 'Checking task receipt';
      description.textContent = 'Waiting for the device to confirm. A start request is sent only once. Reconnect to the device network if needed.';
      progress.hidden = true; summary.replaceChildren(); actions.replaceChildren();
      actions.append(button('Check device status', async () => {
        try { update(await hooks.json('/api/status')); }
        catch (error) { alert('Unable to check status. Reconnect to the device network and try checking again.', true); }
      }, 'subtle', 'flow-status'));
      if (!busy) actions.append(button('Return to tasks', () => {
        tracked = null; persist(); home();
        hooks.message('The previous request was unconfirmed and may have run. Check the latest device task before starting another.');
      }));
      return;
    }
    if (!job || !job.id) {
      heading.textContent = 'No task reported'; description.textContent = 'The device has no current task. Nothing was resubmitted.';
      progress.hidden = true; summary.replaceChildren(); actions.replaceChildren(); return;
    }
    if (!running(job) && /^Stop (requested\.|could not be confirmed\.)/.test(notice)) alert('');
    heading.textContent = ({completed:'Task complete',cancelled:'Task stopped',failed:'Task failed'})[job.state] || 'Task in progress';
    description.textContent = job.error || ({queued:'The device is preparing your task.',running:'The task runs on the device. You can reconnect to this page without interrupting it.',recovering:'The device is restoring dashboard Wi-Fi. Reconnect to its network if needed.',completed:job.kind === 'tool' ? 'The device finished its command sequence. Read the output below for the reported result.' : 'The device finished the task and restored dashboard Wi-Fi.',cancelled:'The device confirmed this task was stopped and restored dashboard Wi-Fi.'})[job.state] || 'Waiting for device status.';
    progress.hidden = !running(job); progress.max = job.seconds || 1; progress.value = Math.min(job.elapsed || 0, job.seconds || 1);
    summary.replaceChildren(); detail(summary, 'Task', title(job.kind, job.tool));
    if (job.kind === 'tool') {
      const out = document.getElementById('flow-output'); out.hidden = false; out.textContent = outputId === job.id ? output : '';
      document.getElementById('flow-output-note').textContent = outputNote();
      readOutput(job);
    } else { document.getElementById('flow-output').hidden = true; document.getElementById('flow-output-note').textContent = ''; }
    if (running(job)) detail(summary, 'Time remaining', (job.remaining || 0) + ' seconds');
    else detail(summary, 'Elapsed', (job.elapsed || 0) + ' seconds');
    if (job.target) { detail(summary, 'Target', job.targetName || 'Unnamed device'); detail(summary, 'Address', job.target); }
    if (job.channel) detail(summary, 'Channel', job.channel);
    if (Number.isFinite(job.rssi) && job.rssi > -128 && job.rssi < 0) detail(summary, 'Signal', job.rssi + ' dBm');
    if (job.kind !== 'tool' && (job.state === 'completed' || job.count)) detail(summary, /_discover$/.test(job.kind) ? 'Devices found' : 'Packets observed', job.count || 0);
    const actionKey = job.id + ':' + job.state + ':' + (job.file || '') + ':' + cancelPending;
    if (actions.dataset.key === actionKey) return;
    actions.dataset.key = actionKey; actions.replaceChildren();
    if (running(job)) {
      const controls = catalog.filter(row => row.available && !row.legacy && row.during && canControl(row));
      if (controls.length) {
        const menu = el('details','task-controls'); menu.append(el('summary','','Task controls'));
        controls.forEach(row => menu.append(button(row.title,() => chooseTool(row.id),'subtle'))); actions.append(menu);
      }
      const stop = button(cancelPending ? 'Stopping…' : 'Stop task', cancel, 'danger-outline', 'flow-stop'); stop.disabled = cancelPending; actions.append(stop);
    } else {
      if (job.state === 'completed' && /_discover$/.test(job.kind)) actions.append(button('View discovered devices', () => {
        kind = job.kind === 'wifi_discover' ? 'wifi_inspect' : 'ble_inspect'; target = null; notice = ''; loadTargets();
      }, 'primary', 'flow-results'));
      if (job.state === 'completed' && job.file) actions.append(button('Download capture', async event => {
        const node = event.currentTarget; node.disabled = true;
        try {
          const blob = await hooks.request('/api/file?storage=spiffs&path=' + encodeURIComponent(job.file), {}, 'blob');
          hooks.save(blob, job.file.split('/').pop());
        } catch (error) { alert('Download failed: ' + error.message, true); }
        finally { node.disabled = false; }
      }, 'primary', 'flow-download'));
      actions.append(button('Choose another task', home, 'subtle', 'flow-another'));
    }
  }
  async function cancel() {
    if (!running(latest) || cancelPending) return;
    const id = latest.id; cancelPending = true; paintJob();
    try {
      const result = await hooks.json('/api/workflow', {method:'POST', body:JSON.stringify({action:'cancel', id})});
      const acknowledged = result.workflow || result;
      alert('Stop requested. Waiting for the device to confirm recovery.');
      if (acknowledged.id === id && acknowledged.state) { latest = acknowledged; paintJob(); }
    } catch (error) {
      alert(error.status ? error.message : 'Stop could not be confirmed. The device’s automatic timer is still responsible for ending the task. Reconnect to its Wi-Fi to check progress; nothing was resent.', true);
    } finally { cancelPending = false; paintJob(); }
  }
  function update(status) {
    const previous = latest;
    const changedBoot = tracked && tracked.boot !== null && tracked.boot !== status.bootId;
    boot = status.bootId; latest = status.workflow || null;
    if (changedBoot) {
      tracked = null; resume = ''; toolResume = null; outputId = 0; persist(); home();
      const note = el('p', 'notice error', 'Device restarted. The previous task was cleared from device memory. Nothing was resubmitted.'); note.setAttribute('role', 'status'); root.prepend(note);
      return;
    }
    if (tracked && tracked.awaiting && (!latest || latest.id === tracked.before)) {
      if (!pendingShown) { kind = tracked.kind; hooks.showTab('tasks'); showJob(); }
      return;
    }
    if (!latest || !latest.id) return;
    if (tracked && tracked.awaiting && latest.id !== tracked.before) {
      tracked.id = latest.id; tracked.awaiting = false;
      if (latest.kind !== tracked.kind || (latest.kind === 'tool' && latest.tool !== tracked.tool)) { tracked.resume = ''; tracked.toolResume = null; }
      persist();
    }
    if (tracked && tracked.id && latest.id !== tracked.id) {
      tracked = running(latest) ? {boot, id:latest.id, kind:latest.kind} : null; resume = ''; persist();
      alert('The previous task is no longer the device’s latest task. Showing the current device task; nothing was resubmitted.', true);
      if (running(latest) || view === 'job') showJob();
      return;
    }
    if (tracked && tracked.id === latest.id && latest.state === 'completed' && tracked.toolResume) {
      const original = tracked.toolResume; tool = toolById(original.tool); kind = 'tool'; inputs = original.inputs; selectedTargets = original.selectedTargets || {}; seconds = original.seconds; channel = original.channel; activeField = tool && tool.fields.find(field => field.name === original.field);
      toolResume = original.resume || null; tracked = null; persist(); notice = original.prerequisite === 'joined' ? 'Wi-Fi connected. Continue your original task.' : 'Discovery complete. Choose a device to continue your original task.'; noticeError = false;
      if (tool && activeField) loadTargets(); else if (tool) setup(); else home(); return;
    }
    if (tracked && tracked.id === latest.id && latest.state === 'completed' && tracked.resume) {
      kind = tracked.resume; target = null; resume = ''; tracked = null; persist();
      notice = 'Discovery complete. Choose a device to continue your original task.'; noticeError = false;
      loadTargets(); return;
    }
    if (view === 'job') { paintJob(); return; }
    if ((running(latest) || (tracked && tracked.id === latest.id)) && shownId !== latest.id) {
      if (!tracked) { tracked = {boot, id:latest.id, kind:latest.kind}; persist(); }
      kind = latest.kind; hooks.showTab('tasks'); showJob();
    } else if (view === 'home' && (!previous || previous.id !== latest.id || previous.state !== latest.state)) home();
  }
  function mount(api) {
    hooks = api; root = document.getElementById('tab-tasks');
    try { tracked = JSON.parse(sessionStorage.getItem(storage) || 'null'); } catch { tracked = null; }
    let saved = null; try { saved = JSON.parse(sessionStorage.getItem(draftStorage) || 'null'); } catch {}
    home();
    try {
      if (!tracked && saved && saved.kind) {
        kind = saved.kind; tool = toolById(saved.tool) || null; inputs = saved.inputs || {}; selectedTargets = saved.selectedTargets || {}; target = saved.target; seconds = saved.seconds || 30; channel = saved.channel || 0; resume = saved.resume || ''; toolResume = saved.toolResume || null;
        activeField = tool && tool.fields.find(field => field.name === saved.activeField);
        if (saved.secretsOmitted) { notice = 'Your choices were restored. Enter passwords again before running.'; noticeError = false; }
        if (saved.view === 'targets' && (kind !== 'tool' || activeField)) loadTargets(); else if (saved.view === 'review' && !saved.secretsOmitted) review(); else setup();
      }
    } catch { /* Invalid drafts never execute. */ }
  }
  return {mount, update, activate, cancel, active:() => running(latest)};
})();

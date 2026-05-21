// ── toast ────────────────────────────────────

function toast(msg, type) {
  type = type || "info";
  var c = document.getElementById("toast-container");
  if (!c) {
    c = document.createElement("div");
    c.id = "toast-container";
    c.className = "toast-container";
    document.body.appendChild(c);
  }
  var el = document.createElement("div");
  el.className = "toast toast-" + type;
  el.textContent = msg;
  c.appendChild(el);
  setTimeout(function () { if (el.parentNode) el.remove(); }, 5000);
}

// ── drop zone / file upload ──────────────────

var dropZone = document.getElementById("drop-zone");
var fileInput = document.getElementById("file-input");
var selectedFileDiv = document.getElementById("selected-file");
var selectedName = document.getElementById("selected-name");
var selectedSize = document.getElementById("selected-size");
var binarySelect = document.getElementById("binary-select");
var uploadedFileName = null;
var pendingFile = null;

function formatSize(bytes) {
  if (bytes < 1024) return bytes + " B";
  if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + " KB";
  return (bytes / (1024 * 1024)).toFixed(1) + " MB";
}

function showSelected(name, size) {
  selectedName.textContent = name;
  selectedSize.textContent = formatSize(size);
  selectedFileDiv.style.display = "";
  if (dropZone) dropZone.style.display = "none";
}

function clearFile() {
  uploadedFileName = null;
  pendingFile = null;
  if (selectedFileDiv) selectedFileDiv.style.display = "none";
  if (dropZone) dropZone.style.display = "";
  if (fileInput) fileInput.value = "";
}

async function uploadFile(file) {
  var form = new FormData();
  form.append("file", file);
  var resp = await fetch("/api/upload", { method: "POST", body: form });
  if (!resp.ok) {
    var err = await resp.json();
    throw new Error(err.error || "Upload failed");
  }
  return await resp.json();
}

if (dropZone && fileInput) {
  dropZone.addEventListener("click", function () { fileInput.click(); });

  dropZone.addEventListener("dragover", function (e) {
    e.preventDefault();
    dropZone.classList.add("drag-over");
  });
  dropZone.addEventListener("dragleave", function () { dropZone.classList.remove("drag-over"); });
  async function handleFileDrop(file) {
    if (!file) return;
    pendingFile = file;
    uploadedFileName = file.name;
    showSelected(file.name, file.size);
    // auto-upload on files page
    if (document.getElementById("file-mgmt-list")) {
      try {
        var uploaded = await uploadFile(file);
        clearFile();
        toast(uploaded.name + " " + _("uploaded"), "success");
        renderFileMgmtList();
      } catch (e) {
        toast(_("upload_failed") + ": " + e.message, "error");
      }
    }
  }

  dropZone.addEventListener("drop", function (e) {
    e.preventDefault();
    dropZone.classList.remove("drag-over");
    handleFileDrop(e.dataTransfer.files[0]);
  });

  fileInput.addEventListener("change", function () {
    handleFileDrop(fileInput.files[0]);
  });

  var btnRemove = document.getElementById("btn-remove-file");
  if (btnRemove) btnRemove.addEventListener("click", clearFile);
}

if (binarySelect) {
  binarySelect.addEventListener("change", function () {
    if (binarySelect.value) clearFile();
  });
}

// ── plugin selection ─────────────────────────

var PLUGIN_STORAGE_KEY = "armcave-selected-plugins";

function getSelectedPlugins() {
  var raw = localStorage.getItem(PLUGIN_STORAGE_KEY);
  if (raw) {
    try { return JSON.parse(raw); } catch (e) { /* ignore */ }
  }
  return null; // null = all selected
}

function setSelectedPlugins(names) {
  localStorage.setItem(PLUGIN_STORAGE_KEY, JSON.stringify(names));
}

function isPluginSelected(name) {
  var sel = getSelectedPlugins();
  if (sel === null) return true; // no filter = all selected
  return sel.indexOf(name) !== -1;
}

// select-all checkbox on plugins page
var selectAllBox = document.getElementById("select-all-plugins");
if (selectAllBox) {
  selectAllBox.addEventListener("change", function () {
    var checked = selectAllBox.checked;
    document.querySelectorAll(".plugin-select").forEach(function (cb) {
      cb.checked = checked;
    });
    persistPluginCheckboxes();
  });
}

// individual plugin checkboxes on plugins page
document.querySelectorAll(".plugin-select").forEach(function (cb) {
  // init from localStorage
  cb.checked = isPluginSelected(cb.dataset.name);
  cb.addEventListener("change", function () { persistPluginCheckboxes(); });
});

updatePluginCountBadge();

function persistPluginCheckboxes() {
  var names = [];
  document.querySelectorAll(".plugin-select:checked").forEach(function (cb) {
    names.push(cb.dataset.name);
  });
  // only persist if not all-selected (save space)
  var all = document.querySelectorAll(".plugin-select");
  if (names.length === all.length && all.length > 0) {
    localStorage.removeItem(PLUGIN_STORAGE_KEY);
  } else {
    setSelectedPlugins(names);
  }
  // update select-all state
  var sa = document.getElementById("select-all-plugins");
  if (sa) sa.checked = names.length === all.length;
  updatePluginCountBadge();
}

function updatePluginCountBadge() {
  var badge = document.getElementById("plugin-count-badge");
  if (!badge) return;
  var sel = getSelectedPlugins();
  if (sel === null) {
    // all enabled — count from DOM or pipeline list
    var cbs = document.querySelectorAll(".plugin-select");
    if (cbs.length > 0) { badge.textContent = cbs.length; return; }
    var pcbs = document.querySelectorAll(".pipeline-plugin-select");
    if (pcbs.length > 0) { badge.textContent = pcbs.length; return; }
    return;
  }
  badge.textContent = sel.length;
}

// render plugin checkboxes on pipeline page
function renderPipelinePluginList() {
  var list = document.getElementById("plugin-select-list");
  var count = document.getElementById("selected-plugin-count");
  if (!list) return;

  var cbs = document.querySelectorAll(".plugin-select");
  if (cbs.length === 0) {
    // plugins not on this page, fetch from API
    fetch("/api/plugins")
      .then(function (r) { return r.json(); })
      .then(function (plugins) {
        renderPluginListFromData(list, count, plugins);
      });
  } else {
    renderPluginListFromData(list, count);
  }
}

function renderPluginListFromData(list, count, plugins) {
  if (!plugins) {
    // use DOM checkboxes from plugins page (user is on plugins page)
    var nodes = document.querySelectorAll(".plugin-select");
    plugins = [];
    nodes.forEach(function (cb) {
      plugins.push({ name: cb.dataset.name, checked: cb.checked });
    });
  }

  var html = "";
  plugins.forEach(function (p) {
    var name = p.name || p;
    var checked = p.checked !== undefined ? p.checked : isPluginSelected(name);
    html += '<label style="display:inline-flex;align-items:center;gap:6px;margin-right:16px;margin-bottom:6px;font-size:14px;cursor:pointer">' +
      '<input type="checkbox" class="pipeline-plugin-select" data-name="' + name + '" ' + (checked ? "checked" : "") + '>' +
      '<code>' + name + '</code></label>';
  });
  list.innerHTML = html || '<span class="muted">—</span>';

  var checkedCount = list.querySelectorAll("input:checked").length;
  if (count) count.textContent = "(" + checkedCount + ")";

  updatePluginCountBadge();

  // bind change events
  list.querySelectorAll(".pipeline-plugin-select").forEach(function (cb) {
    cb.addEventListener("change", function () {
      var sel = [];
      list.querySelectorAll(".pipeline-plugin-select:checked").forEach(function (c) {
        sel.push(c.dataset.name);
      });
      var total = list.querySelectorAll(".pipeline-plugin-select").length;
      if (sel.length === total) {
        localStorage.removeItem(PLUGIN_STORAGE_KEY);
      } else {
        setSelectedPlugins(sel);
      }
      if (count) count.textContent = "(" + sel.length + ")";
      updatePluginCountBadge();
    });
  });
}

// auto-render on pipeline page
if (document.getElementById("plugin-select-list")) {
  renderPipelinePluginList();
}

// ── pipeline ─────────────────────────────────

var outputLog = document.getElementById("output-log");
var btnRun = document.getElementById("btn-run");
var abortController = null;

function cancelPipeline() {
  if (abortController) {
    abortController.abort();
    abortController = null;
  }
}

function logColor(line) {
  if (/^\[done\]|^\[cave\]/.test(line)) return "log-ok";
  if (/^ERROR:|^Traceback|^\[error\]/.test(line)) return "log-err";
  if (/^\[copy\]|^\[dry-run\]/.test(line)) return "log-warn";
  return "log-info";
}

function appendLog(line) {
  var cls = logColor(line);
  outputLog.innerHTML += '<span class="' + cls + '">' + line.replace(/&/g,"&amp;").replace(/</g,"&lt;").replace(/>/g,"&gt;") + '</span>\n';
  outputLog.scrollTop = outputLog.scrollHeight;
}

function setButtons(disabled) {
  if (btnRun) btnRun.disabled = disabled;
}

async function runPipeline() {
  var binaryName = (binarySelect && binarySelect.value) || uploadedFileName || null;

  if (pendingFile) {
    appendLog(_("uploading") + " " + pendingFile.name + " ...");
    try {
      var uploaded = await uploadFile(pendingFile);
      binaryName = uploaded.name;
      appendLog(_("uploaded") + ": " + uploaded.name + " (" + formatSize(uploaded.size) + ")");
      appendLog("");
    } catch (e) {
      appendLog(_("upload_failed") + ": " + e.message);
      toast(_("upload_failed") + ": " + e.message, "error");
      return;
    }
  }

  if (!binaryName) {
    toast(_("binary_required"), "error");
    return;
  }

  cancelPipeline();
  outputLog.innerHTML = "";
  setButtons(true);
  abortController = new AbortController();
  var outName = (document.getElementById("output-name") || {}).value || "";
  if (!outName) {
    var patched = binaryName.replace(/(\.[^.]+)$/, ".patched$1");
    outName = patched !== binaryName ? patched : binaryName + ".patched";
  }

  appendLog(_("pipeline_start"));
  appendLog("");

  var selectedPlugins = getOrderedPluginNames();

  var resp = await fetch("/api/pipeline/run", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ binary: binaryName, output: outName, plugins: selectedPlugins }),
    signal: abortController.signal,
  });

  var reader = resp.body.getReader();
  var decoder = new TextDecoder();
  var buf = "";

  while (true) {
    var result = await reader.read();
    if (result.done) break;
    buf += decoder.decode(result.value, { stream: true });
    var idx;
    while ((idx = buf.indexOf("\n\n")) !== -1) {
      var chunk = buf.slice(0, idx);
      buf = buf.slice(idx + 2);
      if (chunk.startsWith("data: ")) {
        var msg = chunk.slice(6);
        if (msg === "__DONE__") {
          abortController = null;
          setButtons(false);
          showDownload(outName);
          toast(_("injection_complete_toast"), "success");
          return;
        }
        appendLog(msg);
      }
    }
  }
  abortController = null;
  setButtons(false);
}

if (btnRun) btnRun.addEventListener("click", function () { runPipeline(); });

// ── file management ─────────────────────────

function renderFileMgmtList() {
  var list = document.getElementById("file-mgmt-list");
  if (!list) return;
  fetch("/api/binaries")
    .then(function (r) { return r.json(); })
    .then(function (files) {
      if (files.length === 0) {
        list.innerHTML = '<span class="muted">' + _("no_binaries") + '</span>';
        return;
      }
      var html = '<table class="table"><thead><tr><th>File</th><th>Size</th><th style="width:60px"></th></tr></thead><tbody>';
      files.forEach(function (f) {
        html += '<tr id="filerow-' + f.name + '">' +
          '<td><code>' + f.name + '</code></td>' +
          '<td style="color:var(--muted)">' + formatSize(f.size) + '</td>' +
          '<td><button class="btn-sm btn-delete-file" data-name="' + f.name + '">' + _("delete_file") + '</button></td>' +
          '</tr>';
      });
      html += '</tbody></table>';
      list.innerHTML = html;

      list.querySelectorAll(".btn-delete-file").forEach(function (btn) {
        btn.addEventListener("click", function () {
          var name = btn.dataset.name;
          if (!confirm(_("confirm_delete_file"))) return;
          fetch("/api/binaries/" + encodeURIComponent(name), { method: "DELETE" })
            .then(function (r) { return r.json(); })
            .then(function (data) {
              if (data.ok) {
                var row = document.getElementById("filerow-" + name);
                if (row) row.remove();
                toast(_("deleted"), "success");
                // refresh binary select dropdown
                if (binarySelect) {
                  var opt = binarySelect.querySelector('option[value="' + name + '"]');
                  if (opt) opt.remove();
                }
              }
            });
        });
      });
    });
}

if (document.getElementById("file-mgmt-list")) {
  renderFileMgmtList();
}

// ── plugin editor (modal) ─────────────────────

var editingPlugin = null;
var editorModal = document.getElementById("editor-modal");

function openModal(title) {
  document.getElementById("modal-title").textContent = title;
  editorModal.style.display = "";
  document.body.style.overflow = "hidden";
  setTimeout(function () { document.getElementById("plugin-content").focus(); }, 150);
}

function closeModal() {
  editorModal.style.display = "none";
  document.body.style.overflow = "";
  editingPlugin = null;
}

document.getElementById("btn-modal-close").addEventListener("click", closeModal);
document.getElementById("btn-cancel-edit").addEventListener("click", closeModal);
editorModal.addEventListener("click", function (e) {
  if (e.target === editorModal) closeModal();
});

async function editPlugin(name) {
  var resp = await fetch("/api/plugins/" + name);
  if (!resp.ok) { toast(_("failed_load_plugin"), "error"); return; }
  var p = await resp.json();

  editingPlugin = name;
  document.getElementById("plugin-filename").value = p.name;
  document.getElementById("plugin-content").value = p.content;
  document.getElementById("plugin-filename").disabled = true;
  document.getElementById("plugin-errors").style.display = "none";
  var cr = document.getElementById("compile-result");
  if (cr) { cr.style.display = "none"; cr.textContent = ""; }
  openModal(_("edit_plugin"));
  updateLineNumbers();
  highlightSyntax();
}

async function deletePlugin(name) {
  if (!confirm(_("confirm_delete_msg") + ' "' + name + '"?')) return;
  var resp = await fetch("/api/plugins/" + name, { method: "DELETE" });
  if (!resp.ok) { toast(_("failed_delete"), "error"); return; }
  toast(_("deleted"), "success");
  setTimeout(function () { location.reload(); }, 600);
}

// bind table buttons
document.querySelectorAll(".btn-edit-plugin").forEach(function (btn) {
  btn.addEventListener("click", function () { editPlugin(btn.dataset.name); });
});
document.querySelectorAll(".btn-delete-plugin").forEach(function (btn) {
  btn.addEventListener("click", function () { deletePlugin(btn.dataset.name); });
});

// new plugin
var btnNew = document.getElementById("btn-new-plugin");
if (btnNew) {
  btnNew.addEventListener("click", function () {
    editingPlugin = null;
    document.getElementById("plugin-filename").value = "";
    document.getElementById("plugin-content").value = "";
    document.getElementById("plugin-filename").disabled = false;
    document.getElementById("plugin-errors").style.display = "none";
    var cr = document.getElementById("compile-result");
    if (cr) { cr.style.display = "none"; cr.textContent = ""; }
    openModal(_("new_plugin"));
    updateLineNumbers();
    highlightSyntax();
  });
}

// save
var btnSave = document.getElementById("btn-save-plugin");
if (btnSave) {
  btnSave.addEventListener("click", async function () {
    var name = document.getElementById("plugin-filename").value.trim();
    var content = document.getElementById("plugin-content").value;
    var errDiv = document.getElementById("plugin-errors");

    if (!name) { errDiv.textContent = _("filename_required"); errDiv.style.display = ""; return; }

    var url = editingPlugin ? "/api/plugins/" + editingPlugin : "/api/plugins";
    var method = editingPlugin ? "PUT" : "POST";

    var resp = await fetch(url, {
      method: method,
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ name: name, content: content }),
    });

    var data = await resp.json();
    if (!resp.ok) {
      errDiv.textContent = data.error || _("unknown_error");
      errDiv.style.display = "";
      return;
    }
    toast(_("saved"), "success");
    setTimeout(function () { location.reload(); }, 500);
  });
}

// ── priority ordering on plugins page ──

var PRIORITY_KEY = "armcave-plugin-order";

function getPluginOrder() {
  var raw = localStorage.getItem(PRIORITY_KEY);
  if (raw) {
    try { return JSON.parse(raw); } catch (e) { /* ignore */ }
  }
  var names = [];
  document.querySelectorAll(".plugin-priority").forEach(function (td) {
    names.push(td.dataset.name);
  });
  return names;
}

function setPluginOrder(names) {
  localStorage.setItem(PRIORITY_KEY, JSON.stringify(names));
}

function renderPriorities() {
  var order = getPluginOrder();
  var allNames = [];
  document.querySelectorAll(".plugin-priority").forEach(function (td) {
    allNames.push(td.dataset.name);
  });
  allNames.forEach(function (n) {
    if (order.indexOf(n) === -1) order.push(n);
  });
  order = order.filter(function (n) { return allNames.indexOf(n) !== -1; });
  setPluginOrder(order);

  document.querySelectorAll(".plugin-priority").forEach(function (td) {
    var idx = order.indexOf(td.dataset.name);
    td.innerHTML = '<span class="prio-num">' + (idx + 1) + '</span>';
  });
}

// drag-and-drop reorder
var dragSrc = null;

function initDragDrop() {
  document.querySelectorAll("#plugin-table-body tr.plugin-row").forEach(function (row) {
    row.setAttribute("draggable", "true");
    row.addEventListener("dragstart", function (e) {
      dragSrc = row;
      row.classList.add("drag-row");
      e.dataTransfer.effectAllowed = "move";
    });
    row.addEventListener("dragend", function () {
      row.classList.remove("drag-row");
      document.querySelectorAll(".drag-over-row").forEach(function (r) { r.classList.remove("drag-over-row"); });
      dragSrc = null;
    });
    row.addEventListener("dragover", function (e) {
      e.preventDefault();
      e.dataTransfer.dropEffect = "move";
      if (row !== dragSrc) row.classList.add("drag-over-row");
    });
    row.addEventListener("dragleave", function () {
      row.classList.remove("drag-over-row");
    });
    row.addEventListener("drop", function (e) {
      e.preventDefault();
      row.classList.remove("drag-over-row");
      if (row === dragSrc) return;
      var tbody = document.getElementById("plugin-table-body");
      var rows = Array.from(tbody.querySelectorAll("tr.plugin-row"));
      var srcIdx = rows.indexOf(dragSrc);
      var dstIdx = rows.indexOf(row);
      if (srcIdx < dstIdx) {
        tbody.insertBefore(dragSrc, row.nextSibling);
      } else {
        tbody.insertBefore(dragSrc, row);
      }
      // update localStorage order from new DOM order
      var newOrder = [];
      tbody.querySelectorAll(".plugin-select").forEach(function (cb) {
        newOrder.push(cb.dataset.name);
      });
      setPluginOrder(newOrder);
      renderPriorities();
    });
  });
}

if (document.querySelector(".plugin-priority")) {
  renderPriorities();
  initDragDrop();
}

// expose sorted list for pipeline
function getOrderedPluginNames() {
  var order = getPluginOrder();
  var sel = getSelectedPlugins();
  // if no priority order set, build from all known plugin names
  if (order.length === 0) {
    var allCbs = document.querySelectorAll(".plugin-select");
    if (allCbs.length === 0) {
      // neither order nor DOM available — let server decide
      return sel !== null ? sel : null;
    }
    allCbs.forEach(function (cb) { order.push(cb.dataset.name); });
  }
  // filter to selected, preserve priority order
  if (sel === null) return order;
  var result = order.filter(function (n) { return sel.indexOf(n) !== -1; });
  // append any selected plugins not yet in priority order
  sel.forEach(function (n) {
    if (result.indexOf(n) === -1) result.push(n);
  });
  return result;
}

// ── segment conflict detection ─────────────

function checkSegmentConflicts() {
  var segments = {};
  var cells = document.querySelectorAll(".plugin-seg");
  // first pass: count
  cells.forEach(function (c) {
    var seg = c.textContent.trim();
    if (!seg || seg === _("parse_error") || seg === "parse error") return;
    segments[seg] = (segments[seg] || 0) + 1;
  });
  // second pass: mark duplicates
  cells.forEach(function (c) {
    var seg = c.textContent.trim();
    if (segments[seg] > 1) {
      c.classList.add("seg-conflict");
      c.title = "Segment '" + seg + "' is shared by " + segments[seg] + " plugins";
    } else {
      c.classList.remove("seg-conflict");
      c.title = "";
    }
  });
}

if (document.querySelector(".plugin-seg")) {
  checkSegmentConflicts();
}

// ── compile check ────────────────────────────

var btnCheck = document.getElementById("btn-compile-check");
if (btnCheck) {
  btnCheck.addEventListener("click", async function () {
    var content = document.getElementById("plugin-content").value;
    if (!content.trim()) { toast(_("no_code"), "error"); return; }

    btnCheck.disabled = true;
    btnCheck.textContent = _("compiling");
    var resultDiv = document.getElementById("compile-result");
    resultDiv.style.display = "";
    resultDiv.textContent = "";
    resultDiv.className = "";

    try {
      var body = { content: content };
      if (editingPlugin) body.current_name = editingPlugin;
      var resp = await fetch("/api/plugins/compile-check", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(body),
      });
      var data = await resp.json();
      if (data.ok) {
        resultDiv.className = data.segment_conflict ? "result-warn" : "result-ok";
        resultDiv.textContent = data.segment_conflict || _("compile_ok");
        toast(data.segment_conflict || _("compile_success_msg"), data.segment_conflict ? "error" : "success");
      } else {
        resultDiv.className = "result-err";
        resultDiv.textContent = data.error || _("compile_error");
        toast(_("compile_failed"), "error");
      }
    } catch (e) {
      resultDiv.className = "result-err";
      resultDiv.textContent = _("network_error");
    }
    btnCheck.disabled = false;
    btnCheck.textContent = _("compile_check_btn");
  });
}

// ── download ─────────────────────────────────

function showDownload(filename) {
  var panel = document.getElementById("download-panel");
  var btn = document.getElementById("btn-download");
  if (panel && btn) {
    panel.style.display = "";
    btn.href = "/api/download/" + encodeURIComponent(filename);
    btn.innerHTML =
      '<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">' +
      '<path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="7 10 12 15 17 10"/><line x1="12" y1="15" x2="12" y2="3"/>' +
      '</svg> ' + _("download_btn") + ' ' + filename;
  }
}

// ── binary info + hex ────────────────────────

var currentBinaryName = null;

async function fetchBinaryInfo(name) {
  currentBinaryName = name;
  var panel = document.getElementById("binary-info-panel");
  if (!panel) return;
  panel.style.display = "";

  try {
    var resp = await fetch("/api/binary/info", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ name: name }),
    });
    if (!resp.ok) { panel.style.display = "none"; return; }
    var info = await resp.json();
    if (info.error) { panel.style.display = "none"; return; }

    document.getElementById("meta-format").textContent = info.format;
    document.getElementById("meta-arch").textContent = info.arch;
    document.getElementById("meta-entry").textContent = info.entrypoint;

    // auto-fill hex preview
    var hexAddr = document.getElementById("hex-addr");
    if (hexAddr && info.entrypoint !== "none") hexAddr.value = info.entrypoint;

    // render segments table
    var segDiv = document.getElementById("segments-table");
    if (segDiv && info.segments) {
      var html = '<div class="seg-row" style="font-weight:600;color:var(--muted);font-size:11px">' +
        '<span>Name</span><span>VA</span><span>VSize</span><span>FSize</span></div>';
      info.segments.forEach(function (s) {
        html += '<div class="seg-row">' +
          '<span class="seg-name">' + s.name + '</span>' +
          '<span class="seg-va">' + s.va + '</span>' +
          '<span class="seg-size">' + s.vsize + '</span>' +
          '<span class="seg-size">' + s.fsize + '</span></div>';
      });
      segDiv.innerHTML = html;
    }
  } catch (e) { /* ignore */ }
}

async function fetchHex() {
  if (!currentBinaryName) return;
  var addr = document.getElementById("hex-addr").value.trim();
  if (!addr) return;

  try {
    var resp = await fetch("/api/binary/hex", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ name: currentBinaryName, address: addr, length: 32 }),
    });
    if (!resp.ok) return;
    var data = await resp.json();
    if (data.error) return;
    var out = document.getElementById("hex-output");
    out.style.display = "";
    out.textContent =
      data.address + "  " + data.hex + "  |" + data.ascii + "|";
  } catch (e) { /* ignore */ }
}

var btnHex = document.getElementById("btn-hex-preview");
if (btnHex) btnHex.addEventListener("click", fetchHex);

// auto-trigger binary info when file selected/dropped
var origShowSelected = showSelected;
showSelected = function (name, size) {
  origShowSelected(name, size);
  fetchBinaryInfo(name);
};

// also trigger on dropdown change
if (binarySelect) {
  binarySelect.addEventListener("change", function () {
    if (binarySelect.value) fetchBinaryInfo(binarySelect.value);
  });
}

// ── code editor (line numbers, tab, auto-indent, syntax highlight) ──

var codeTextarea = document.getElementById("plugin-content");
var lineGutter = document.getElementById("line-gutter");
var codeHighlight = document.getElementById("code-highlight");

function updateLineNumbers() {
  if (!codeTextarea || !lineGutter) return;
  var lines = codeTextarea.value.split("\n");
  var html = "";
  for (var i = 0; i < lines.length; i++) {
    html += '<span>' + (i + 1) + '</span>';
  }
  lineGutter.innerHTML = html;
}

var C_KEYWORDS = /\b(auto|break|case|const|continue|default|do|else|enum|extern|for|goto|if|register|return|signed|sizeof|static|struct|switch|typedef|union|unsigned|volatile|while)\b/g;
var C_TYPES = /\b(void|char|short|int|long|float|double|size_t|uint8_t|uint16_t|uint32_t|uint64_t|int8_t|int16_t|int32_t|int64_t)\b/g;

function highlightSyntax() {
  if (!codeTextarea || !codeHighlight) return;
  var text = codeTextarea.value;
  var html = text
    .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
  html = html.replace(/(\/\*[\s\S]*?\*\/)/g, '<span class="syn-cmt">$1</span>');
  html = html.replace(/(\/\/.*)/g, '<span class="syn-cmt">$1</span>');
  html = html.replace(/("(?:[^"\\]|\\.)*")/g, '<span class="syn-str">$1</span>');
  html = html.replace(/^(#.*)$/gm, '<span class="syn-pp">$1</span>');
  html = html.replace(/(__attribute__)/g, '<span class="syn-attr">$1</span>');
  html = html.replace(/\b(0x[0-9a-fA-F]+|\d+)\b/g, '<span class="syn-num">$1</span>');
  html = html.replace(C_TYPES, '<span class="syn-ty">$1</span>');
  html = html.replace(C_KEYWORDS, '<span class="syn-kw">$1</span>');
  codeHighlight.innerHTML = html;
}

function syncEditorScroll() {
  if (!codeTextarea) return;
  if (lineGutter) lineGutter.scrollTop = codeTextarea.scrollTop;
  if (codeHighlight) {
    codeHighlight.scrollTop = codeTextarea.scrollTop;
    codeHighlight.scrollLeft = codeTextarea.scrollLeft;
  }
}

function editorRefresh() { updateLineNumbers(); highlightSyntax(); syncEditorScroll(); }

if (codeTextarea && lineGutter) {
  codeTextarea.addEventListener("input", editorRefresh);
  codeTextarea.addEventListener("scroll", syncEditorScroll);
}

// tab → 4 spaces, Enter → auto-indent, } → de-indent
document.addEventListener("keydown", function (e) {
  if (document.activeElement !== codeTextarea) return;
  var ta = codeTextarea;
  var start = ta.selectionStart;
  var end = ta.selectionEnd;
  var before = ta.value.substring(0, start);
  var after = ta.value.substring(end);

  if (e.key === "Tab") {
    e.preventDefault();
    ta.value = before + "    " + after;
    ta.selectionStart = ta.selectionEnd = start + 4;
    editorRefresh();
  }

  if (e.key === "Enter") {
    e.preventDefault();
    var lineStart = before.lastIndexOf("\n") + 1;
    var curLine = before.substring(lineStart);
    var indent = curLine.match(/^[ \t]*/)[0];
    var extra = curLine.trimEnd().endsWith("{") ? "    " : "";
    var insertion = "\n" + indent + extra;
    ta.value = before + insertion + after;
    ta.selectionStart = ta.selectionEnd = start + insertion.length;
    editorRefresh();
  }

  if (e.key === "}" && start === end) {
    var lineStart = before.lastIndexOf("\n") + 1;
    var beforeKey = before.substring(lineStart);
    if (/^[ \t]*$/.test(beforeKey) && beforeKey.length >= 4) {
      e.preventDefault();
      var deindented = beforeKey.substring(0, beforeKey.length - 4) + "}";
      ta.value = before.substring(0, lineStart) + deindented + after;
      ta.selectionStart = ta.selectionEnd = lineStart + deindented.length;
      editorRefresh();
    }
  }
});

// ── keyboard shortcut ────────────────────────

document.addEventListener("keydown", function (e) {
  if ((e.ctrlKey || e.metaKey) && e.key === "s") {
    var saveBtn = document.getElementById("btn-save-plugin");
    if (saveBtn && saveBtn.offsetParent !== null) {
      e.preventDefault();
      saveBtn.click();
    }
  }
});

// ── lang cookie ──────────────────────────────

(function () {
  var lang = new URLSearchParams(location.search).get("lang");
  if (lang) document.cookie = "lang=" + lang + ";path=/;max-age=31536000";
})();

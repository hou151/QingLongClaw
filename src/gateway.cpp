#include "QingLongClaw/gateway.h"

#include <atomic>
#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "httplib.h"
#include "json.hpp"
#include "QingLongClaw/agent.h"
#include "QingLongClaw/cron_service.h"
#include "QingLongClaw/http_client.h"
#include "QingLongClaw/model_preset.h"
#include "QingLongClaw/util.h"
#include "QingLongClaw/version.h"

namespace QingLongClaw {

namespace {

std::atomic_bool g_stop_requested{false};

void signal_handler(int) { g_stop_requested.store(true); }

nlohmann::json parse_json_or_error(const std::string& text, std::string* error);
std::string dump_json_lossy(const nlohmann::json& value, int indent = -1);

const char* kFrontendHtml = R"HTML(<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>QingLongClaw Chat Console</title>
  <style>
    :root {
      --bg1: #f4f6fb;
      --bg2: #eef2f8;
      --panel: #ffffff;
      --line: #dbe3f1;
      --text: #334155;
      --muted: #64748b;
      --brand: #4f83cc;
      --brand-soft: #e9f2ff;
      --ok: #2fa37a;
      --err: #d15b5b;
      --shadow: 0 10px 26px rgba(78, 105, 153, 0.12);
    }
    * { box-sizing: border-box; }
    html, body { height: 100%; }
    body {
      margin: 0;
      background:
        radial-gradient(circle at 20% -20%, #dde8fb 0%, transparent 55%),
        radial-gradient(circle at 90% 0%, #e8eefb 0%, transparent 45%),
        linear-gradient(180deg, var(--bg1), var(--bg2));
      color: var(--text);
      font-family: "PingFang SC", "Hiragino Sans GB", "Microsoft YaHei", "Segoe UI", sans-serif;
    }
    .shell {
      max-width: 1000px;
      height: 100%;
      margin: 0 auto;
      padding: 14px;
      display: flex;
      flex-direction: column;
      gap: 12px;
    }
    .topbar {
      background: rgba(255, 255, 255, 0.86);
      border: 1px solid var(--line);
      border-radius: 14px;
      box-shadow: var(--shadow);
      padding: 10px 12px;
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 10px;
      backdrop-filter: blur(8px);
    }
    .left-tools, .right-tools {
      display: flex;
      align-items: center;
      gap: 8px;
      flex-wrap: wrap;
    }
    .title {
      font-size: 15px;
      color: var(--muted);
      white-space: nowrap;
    }
    .btn {
      border: 1px solid var(--line);
      background: #fff;
      color: var(--text);
      border-radius: 10px;
      padding: 7px 12px;
      cursor: pointer;
      transition: all .15s ease;
    }
    .btn:hover {
      border-color: #b9c9e5;
      background: #f8fbff;
    }
    .btn.primary {
      background: var(--brand);
      color: #fff;
      border-color: var(--brand);
    }
    .btn.primary:hover {
      filter: brightness(1.03);
    }
    .btn.busy {
      background: #94a3b8;
      border-color: #94a3b8;
      color: #fff;
    }
    .btn.busy:hover {
      filter: none;
      cursor: pointer;
    }
    input, select, textarea {
      border: 1px solid var(--line);
      background: #fff;
      color: var(--text);
      border-radius: 10px;
      padding: 8px 10px;
      outline: none;
      font: inherit;
    }
    input:focus, select:focus, textarea:focus {
      border-color: #9db9e6;
      box-shadow: 0 0 0 3px #e8f1ff;
    }
    #sessionKey { min-width: 160px; }
    #chatModel { min-width: 140px; }
    #langSelect { min-width: 96px; }
    .chat-panel {
      flex: 1;
      min-height: 0;
      background: rgba(255, 255, 255, 0.9);
      border: 1px solid var(--line);
      border-radius: 14px;
      box-shadow: var(--shadow);
      display: flex;
      flex-direction: column;
      overflow: hidden;
      backdrop-filter: blur(8px);
    }
    .chat-list {
      flex: 1;
      min-height: 0;
      overflow-y: auto;
      padding: 14px;
      display: flex;
      flex-direction: column;
      gap: 10px;
    }
    .msg-row {
      max-width: 100%;
      display: flex;
      align-items: flex-end;
      gap: 8px;
    }
    .msg-row.user {
      align-self: flex-end;
    }
    .msg-row.assistant {
      align-self: flex-start;
    }
    .msg-row.system {
      align-self: center;
    }
    .msg {
      max-width: min(86%, 760px);
      padding: 10px 12px;
      border-radius: 12px;
      border: 1px solid transparent;
      word-break: break-word;
    }
    .msg-text {
      white-space: pre-wrap;
      line-height: 1.55;
    }
    .msg-copy {
      position: static;
      align-self: flex-end;
      margin-bottom: 2px;
      border: 1px solid #cbd5e1;
      background: #f8fafc;
      color: #475569;
      border-radius: 8px;
      padding: 2px 8px;
      font-size: 12px;
      cursor: pointer;
      transition: all .15s ease;
      flex-shrink: 0;
    }
    .msg-copy:hover {
      border-color: #94a3b8;
      background: #f1f5f9;
    }
    .msg.user {
      background: var(--brand);
      color: #fff;
    }
    .msg-row.user .msg-copy {
      border-color: rgba(255, 255, 255, 0.45);
      background: rgba(255, 255, 255, 0.14);
      color: #fff;
    }
    .msg.assistant {
      background: #fff;
      border-color: var(--line);
      color: var(--text);
    }
    .msg.system {
      background: var(--brand-soft);
      color: var(--muted);
      border-color: #cfe1ff;
      font-size: 13px;
    }
    .composer {
      border-top: 1px solid var(--line);
      background: rgba(255, 255, 255, 0.96);
      padding: 10px;
      display: grid;
      grid-template-columns: 1fr auto;
      gap: 10px;
      align-items: end;
    }
    #chatInput {
      width: 100%;
      min-height: 72px;
      max-height: 180px;
      resize: vertical;
    }
    .status {
      font-size: 12px;
      color: var(--muted);
      min-height: 18px;
      padding: 0 14px 10px 14px;
    }
    .status.ok { color: var(--ok); }
    .status.err { color: var(--err); }
    .overlay {
      position: fixed;
      inset: 0;
      background: rgba(15, 23, 42, 0.22);
      backdrop-filter: blur(1px);
      opacity: 0;
      pointer-events: none;
      transition: opacity .2s ease;
    }
    .overlay.show {
      opacity: 1;
      pointer-events: auto;
    }
    .drawer {
      position: fixed;
      left: 0;
      top: 0;
      height: 100%;
      width: min(420px, 92vw);
      background: #fff;
      border-right: 1px solid var(--line);
      box-shadow: var(--shadow);
      transform: translateX(-100%);
      transition: transform .22s ease;
      z-index: 20;
      display: flex;
      flex-direction: column;
    }
    .drawer.show { transform: translateX(0); }
    .drawer-hd {
      display: flex;
      align-items: center;
      justify-content: space-between;
      padding: 14px 14px 10px 14px;
      border-bottom: 1px solid var(--line);
    }
    .drawer-body {
      flex: 1;
      overflow-y: auto;
      padding: 12px 14px;
      display: flex;
      flex-direction: column;
      gap: 10px;
    }
    .field label {
      font-size: 12px;
      color: var(--muted);
      display: block;
      margin-bottom: 5px;
    }
    .group {
      border: 1px solid var(--line);
      border-radius: 10px;
      padding: 10px;
      background: #fafcff;
    }
    .group h4 {
      margin: 0 0 8px 0;
      font-size: 13px;
      color: #3d5476;
    }
    .drawer-ft {
      border-top: 1px solid var(--line);
      padding: 10px 14px 14px 14px;
      display: flex;
      gap: 8px;
      align-items: center;
      flex-wrap: wrap;
    }
    .cfg-msg {
      font-size: 12px;
      min-height: 16px;
      color: var(--muted);
      width: 100%;
    }
    .cfg-msg.ok { color: var(--ok); }
    .cfg-msg.err { color: var(--err); }
    @media (max-width: 720px) {
      .shell { padding: 10px; }
      .title { display: none; }
      .composer { grid-template-columns: 1fr; }
    }
  </style>
</head>
<body>
  <div class="shell">
    <div class="topbar">
      <div class="left-tools">
        <button class="btn" id="openCfgBtn">Config</button>
        <div class="title">QingLongClaw Chat Console</div>
      </div>
      <div class="right-tools">
        <input id="sessionKey" value="web:default" placeholder="Session ID">
        <select id="langSelect">
          <option value="zh">ZH</option>
          <option value="en">English</option>
        </select>
        <select id="chatModel">
          <option value="">Default model</option>
          <option value="glm-4.7">glm-4.7</option>
          <option value="deepseek-chat">deepseek-chat</option>
          <option value="qwen-plus">qwen-plus</option>
          <option value="minimax-m2.1">minimax-m2.1</option>
        </select>
      </div>
    </div>

    <div class="chat-panel">
      <div id="chatList" class="chat-list"></div>
      <div id="chatStatus" class="status"></div>
      <div class="composer">
        <textarea id="chatInput" placeholder="Type a message. Enter to send, Shift+Enter for newline"></textarea>
        <button class="btn primary" id="sendBtn">Send</button>
      </div>
    </div>
  </div>

  <div id="overlay" class="overlay"></div>
  <aside id="cfgDrawer" class="drawer">
    <div class="drawer-hd">
      <strong data-i18n="apiSettings">API Settings</strong>
      <button class="btn" id="closeCfgBtn">Close</button>
    </div>
    <div class="drawer-body">
      <div class="field">
        <label data-i18n="adminTokenLabel">Admin token (required only when qinglongclaw_WEB_TOKEN is set on server)</label>
        <input id="adminToken" type="password" data-i18n-placeholder="adminTokenPlaceholder" placeholder="Optional">
      </div>
      <div class="field">
        <label data-i18n="defaultModelLabel">Default model</label>
        <select id="defaultModel">
          <option value="glm-4.7">glm-4.7</option>
          <option value="deepseek-chat">deepseek-chat</option>
          <option value="qwen-plus">qwen-plus</option>
          <option value="minimax-m2.1">minimax-m2.1</option>
        </select>
      </div>
      <div class="field">
        <label data-i18n="maxToolIterationsLabel">Max tool iterations (0 = unlimited)</label>
        <input id="maxToolIterations" type="number" min="0" max="100000" step="1" value="0">
      </div>
      <div class="field">
        <label data-i18n="maxHistoryMessagesLabel">Context history messages</label>
        <input id="maxHistoryMessages" type="number" min="4" max="800" step="1" value="24">
      </div>
      <div class="field">
        <label data-i18n="workspacePathLabel">Workspace Path</label>
        <input id="workspacePath" data-i18n-placeholder="workspacePathPlaceholder" placeholder="/userdata/QingLongClaw">
      </div>
      <div class="field">
        <label style="display:flex;align-items:center;gap:8px">
          <input id="restrictWorkspace" type="checkbox" checked style="width:auto">
          <span data-i18n="restrictWorkspaceLabel">Only allow tools to access workspace</span>
        </label>
      </div>
      <div class="field" style="display:none">
        <label style="display:flex;align-items:center;gap:8px">
          <input id="codexPrimary" type="checkbox" checked style="width:auto">
          <span data-i18n="codexPrimaryLabel">Codex priority when model is not manually selected</span>
        </label>
      </div>
      <div class="field" style="display:none">
        <label style="display:flex;align-items:center;gap:8px">
          <input id="codexFallbackOnError" type="checkbox" checked style="width:auto">
          <span data-i18n="codexFallbackOnErrorLabel">Fallback to non-codex model if Codex request fails</span>
        </label>
      </div>
      <div class="group">
        <h4 data-i18n="groupGlm">GLM (Zhipu)</h4>
        <div class="field">
          <label data-i18n="apiKeyLabel">API Key</label>
          <input id="glmKey" type="password" data-i18n-placeholder="glmKeyPlaceholder" placeholder="Enter GLM API key">
        </div>
        <div class="field">
          <label data-i18n="apiBaseLabel">API Base</label>
          <input id="glmBase" data-i18n-placeholder="glmBasePlaceholder" placeholder="https://open.bigmodel.cn/api/paas/v4">
        </div>
        <div class="field">
          <label data-i18n="proxyOptionalLabel">Proxy (optional)</label>
          <input id="glmProxy" data-i18n-placeholder="proxyPlaceholder" placeholder="http://127.0.0.1:7890">
        </div>
        <div class="field">
          <button class="btn" id="loadGlmModelsBtn" type="button" data-i18n="loadModels">Load models</button>
        </div>
        <div class="field">
          <label data-i18n="loadedModelsLabel">Loaded models</label>
          <select id="glmModelSelect"></select>
        </div>
        <div class="field">
          <label data-i18n="customModelNameLabel">Custom model name</label>
          <input id="glmCustomModelInput" data-i18n-placeholder="glmCustomModelPlaceholder" placeholder="e.g. glm-4-plus">
        </div>
        <div class="field">
          <button class="btn" id="addGlmModelBtn" type="button" data-i18n="addCustomModel">Add custom model</button>
        </div>
      </div>
      <div class="group">
        <h4 data-i18n="groupDeepseek">DeepSeek</h4>
        <div class="field">
          <label data-i18n="apiKeyLabel">API Key</label>
          <input id="deepseekKey" type="password" data-i18n-placeholder="deepseekKeyPlaceholder" placeholder="Enter DeepSeek API key">
        </div>
        <div class="field">
          <label data-i18n="apiBaseLabel">API Base</label>
          <input id="deepseekBase" data-i18n-placeholder="deepseekBasePlaceholder" placeholder="https://api.deepseek.com/v1">
        </div>
        <div class="field">
          <label data-i18n="proxyOptionalLabel">Proxy (optional)</label>
          <input id="deepseekProxy" data-i18n-placeholder="proxyPlaceholder" placeholder="http://127.0.0.1:7890">
        </div>
        <div class="field">
          <button class="btn" id="loadDeepseekModelsBtn" type="button" data-i18n="loadModels">Load models</button>
        </div>
        <div class="field">
          <label data-i18n="loadedModelsLabel">Loaded models</label>
          <select id="deepseekModelSelect"></select>
        </div>
        <div class="field">
          <label data-i18n="customModelNameLabel">Custom model name</label>
          <input id="deepseekCustomModelInput" data-i18n-placeholder="deepseekCustomModelPlaceholder" placeholder="e.g. deepseek-reasoner">
        </div>
        <div class="field">
          <button class="btn" id="addDeepseekModelBtn" type="button" data-i18n="addCustomModel">Add custom model</button>
        </div>
      </div>
      <div class="group">
        <h4 data-i18n="groupQwen">Qwen</h4>
        <div class="field">
          <label data-i18n="apiKeyLabel">API Key</label>
          <input id="qwenKey" type="password" data-i18n-placeholder="qwenKeyPlaceholder" placeholder="Enter Qwen API key">
        </div>
        <div class="field">
          <label data-i18n="apiBaseLabel">API Base</label>
          <input id="qwenBase" data-i18n-placeholder="qwenBasePlaceholder" placeholder="https://dashscope.aliyuncs.com/compatible-mode/v1">
        </div>
        <div class="field">
          <label data-i18n="proxyOptionalLabel">Proxy (optional)</label>
          <input id="qwenProxy" data-i18n-placeholder="proxyPlaceholder" placeholder="http://127.0.0.1:7890">
        </div>
        <div class="field">
          <button class="btn" id="loadQwenModelsBtn" type="button" data-i18n="loadModels">Load models</button>
        </div>
        <div class="field">
          <label data-i18n="loadedModelsLabel">Loaded models</label>
          <select id="qwenModelSelect"></select>
        </div>
        <div class="field">
          <label data-i18n="customModelNameLabel">Custom model name</label>
          <input id="qwenCustomModelInput" data-i18n-placeholder="qwenCustomModelPlaceholder" placeholder="e.g. qwen-max">
        </div>
        <div class="field">
          <button class="btn" id="addQwenModelBtn" type="button" data-i18n="addCustomModel">Add custom model</button>
        </div>
      </div>
      <div class="group">
        <h4 data-i18n="groupMinmax">MinMax</h4>
        <div class="field">
          <label data-i18n="apiKeyLabel">API Key</label>
          <input id="minmaxKey" type="password" data-i18n-placeholder="minmaxKeyPlaceholder" placeholder="Enter MinMax API key">
        </div>
        <div class="field">
          <label data-i18n="apiBaseLabel">API Base</label>
          <input id="minmaxBase" data-i18n-placeholder="minmaxBasePlaceholder" placeholder="https://api.minimaxi.com/v1">
        </div>
        <div class="field">
          <label data-i18n="proxyOptionalLabel">Proxy (optional)</label>
          <input id="minmaxProxy" data-i18n-placeholder="proxyPlaceholder" placeholder="http://127.0.0.1:7890">
        </div>
        <div class="field">
          <button class="btn" id="loadMinmaxModelsBtn" type="button" data-i18n="loadModels">Load models</button>
        </div>
        <div class="field">
          <label data-i18n="loadedModelsLabel">Loaded models</label>
          <select id="minmaxModelSelect"></select>
        </div>
        <div class="field">
          <label data-i18n="customModelNameLabel">Custom model name</label>
          <input id="minmaxCustomModelInput" data-i18n-placeholder="minmaxCustomModelPlaceholder" placeholder="e.g. MiniMax-M2.1">
        </div>
        <div class="field">
          <button class="btn" id="addMinmaxModelBtn" type="button" data-i18n="addCustomModel">Add custom model</button>
        </div>
      </div>
      <div class="group" style="display:none">
        <h4 data-i18n="groupCodex">Removed Provider</h4>
        <div class="field">
          <label data-i18n="apiKeyLabel">API Key</label>
          <input id="codexKey" type="password" data-i18n-placeholder="codexKeyPlaceholder" placeholder="Enter Codex API key">
        </div>
        <div class="field">
          <label data-i18n="apiBaseLabel">API Base</label>
          <input id="codexBase" data-i18n-placeholder="codexBasePlaceholder" placeholder="https://api.invalid/removed-provider">
        </div>
        <div class="field">
          <label data-i18n="proxyOptionalLabel">Proxy (optional)</label>
          <input id="codexProxy" data-i18n-placeholder="proxyPlaceholder" placeholder="http://127.0.0.1:7890">
        </div>
        <div class="field">
          <button class="btn" id="loadCodexModelsBtn" type="button" data-i18n="loadModels">Load models</button>
        </div>
        <div class="field">
          <label data-i18n="loadedModelsLabel">Loaded models</label>
          <select id="codexModelSelect"></select>
        </div>
        <div class="field">
          <label data-i18n="customModelNameLabel">Custom model name</label>
          <input id="codexCustomModelInput" data-i18n-placeholder="codexCustomModelPlaceholder" placeholder="e.g. gpt-5.1-codex">
        </div>
        <div class="field">
          <button class="btn" id="addCodexModelBtn" type="button" data-i18n="addCustomModel">Add custom model</button>
        </div>
      </div>
      <div class="group">
        <h4 data-i18n="groupFeishu">Feishu</h4>
        <div class="field" style="display:none">
          <label data-i18n="feishuAppIdLabel">Feishu App ID</label>
          <input id="feishuAppId" data-i18n-placeholder="feishuAppIdPlaceholder" placeholder="cli_xxx">
        </div>
        <div class="field" style="display:none">
          <label data-i18n="feishuAppSecretLabel">Feishu App Secret</label>
          <input id="feishuAppSecret" type="password" data-i18n-placeholder="feishuAppSecretPlaceholder" placeholder="Enter Feishu App Secret">
        </div>
        <div class="field">
          <label data-i18n="feishuProxyLabel">Feishu proxy (optional)</label>
          <input id="feishuProxy" data-i18n-placeholder="proxyPlaceholder" placeholder="http://127.0.0.1:7890">
        </div>
        <div class="field">
          <label data-i18n="feishuDefaultChatIdLabel">Feishu default Chat ID (used for web-to-Feishu sync)</label>
          <input id="feishuDefaultChatId" data-i18n-placeholder="feishuDefaultChatIdPlaceholder" placeholder="oc_xxx / chat_id">
        </div>
        <div class="field">
          <label data-i18n="feishuEnabledHint">Feishu App ID/App Secret are stripped in this open-source build</label>
        </div>
        <div class="field">
          <label data-i18n="feishuModeHint">Using long-connection mode; callback URL is not required</label>
        </div>
      </div>
    </div>
    <div class="drawer-ft">
      <button class="btn" id="reloadCfgBtn">Reload</button>
      <button class="btn" id="probeCfgBtn">Connectivity Test</button>
      <button class="btn primary" id="saveCfgBtn">Save Config</button>
      <div id="cfgMsg" class="cfg-msg"></div>
    </div>
  </aside>

  <script>
    const el = (id) => document.getElementById(id);
    const baseModels = ["glm-4.7", "deepseek-chat", "qwen-plus", "minimax-m2.1"];
    let glmModelsCatalog = [];
    let deepseekModelsCatalog = [];
    let qwenModelsCatalog = [];
    let minmaxModelsCatalog = [];
    let codexModelsCatalog = [];
    let lastEventId = 0;
    let eventsInstanceId = "";
    let pollingEvents = false;
    let eventsTimer = null;
    let chatAbortController = null;
    let localChatInFlight = false;
    const runningSessions = new Set();
    const sessionStartTimes = new Map();
    const seenEventIds = new Set();
    const builtInMinmaxModels = ["MiniMax-M2.1", "MiniMax-M2.1-lightning", "MiniMax-M2.5", "MiniMax-M2.5-Lightning"];
    const builtInCodexModels = [];

    const I18N = {
      zh: {
        title: "QingLongClaw \u804a\u5929\u63a7\u5236\u53f0",
        config: "\u914d\u7f6e",
        close: "\u5173\u95ed",
        reload: "\u5237\u65b0",
        probe: "\u8fde\u901a\u6d4b\u8bd5",
        save: "\u4fdd\u5b58\u914d\u7f6e",
        send: "\u53d1\u9001",
        stop: "\u505c\u6b62",
        langZh: "\u4e2d\u6587",
        langEn: "\u82f1\u6587",
        sessionPlaceholder: "\u4f1a\u8bdd ID",
        inputPlaceholder: "\u8bf7\u8f93\u5165\u6d88\u606f\uff0c\u56de\u8f66\u53d1\u9001\uff0cShift+\u56de\u8f66\u6362\u884c",
        defaultModelOption: "\u9ed8\u8ba4\u6a21\u578b",
        notLoaded: "\u5c1a\u672a\u52a0\u8f7d",
        copy: "\u590d\u5236",
        copied: "\u5df2\u590d\u5236",
        copyFailed: "\u5931\u8d25",
        generating: "\u6b63\u5728\u751f\u6210\u56de\u590d...",
        done: "\u5b8c\u6210\u3002",
        cancelled: "\u5df2\u53d6\u6d88\u3002",
        requestFailed: "\u8bf7\u6c42\u5931\u8d25\u3002",
        pleaseInput: "\u8bf7\u8f93\u5165\u6d88\u606f\u3002",
        cfgLoaded: "\u914d\u7f6e\u5df2\u52a0\u8f7d\u3002",
        cfgSaved: "\u914d\u7f6e\u4fdd\u5b58\u6210\u529f\u3002",
        statusStopRequested: "\u5df2\u53d1\u9001\u505c\u6b62\u6307\u4ee4\uff0c\u7b49\u5f85\u5f53\u524d\u4efb\u52a1\u7ed3\u675f\u3002",
        feishuGenerating: "\u98de\u4e66\u4efb\u52a1\u6b63\u5728\u751f\u6210\u56de\u590d...",
        feishuDone: "\u98de\u4e66\u4efb\u52a1\u5df2\u5b8c\u6210",
        feishuDoneWithTime: "\u98de\u4e66\u4efb\u52a1\u5df2\u5b8c\u6210\uff0c\u7528\u65f6 {elapsed}",
        feishuPrefix: "\u98de\u4e66: ",
        failedSyncRuntimeModel: "\u540c\u6b65\u8fd0\u884c\u65f6\u6a21\u578b\u5931\u8d25: {error}",
        failedLoadConfig: "\u52a0\u8f7d\u914d\u7f6e\u5931\u8d25: {error}",
        failedSaveConfig: "\u4fdd\u5b58\u914d\u7f6e\u5931\u8d25: {error}",
        unsupportedProvider: "\u4e0d\u652f\u6301\u7684\u63d0\u4f9b\u5546: {provider}",
        enterModelName: "\u8bf7\u8f93\u5165\u6a21\u578b\u540d\u79f0\u3002",
        modelExists: "{provider} \u6a21\u578b\u5df2\u5b58\u5728\u6216\u65e0\u6548\u3002",
        customModelAdded: "\u5df2\u6dfb\u52a0 {provider} \u81ea\u5b9a\u4e49\u6a21\u578b: {model}",
        modelsLoadedSaved: "{provider} \u6a21\u578b\u52a0\u8f7d\u5e76\u4fdd\u5b58\u6210\u529f: {count}",
        modelsLoadFailed: "{provider} \u6a21\u578b\u52a0\u8f7d\u5931\u8d25: {error}",
        connectivityOk: "\u8fde\u901a\u6210\u529f: {status} ({apiBase})",
        connectivityFailed: "\u8fde\u901a\u5931\u8d25: {error}",
        httpStatusUnknown: "\u65e0 HTTP \u72b6\u6001\u7801",
        welcomeBanner: "\u6b22\u8fce\u4f7f\u7528 {displayName}\u3002\u5f53\u524d\u7248\u672c {version}\uff0c\u5de5\u5177\u534f\u8bae v{toolApi}\u3002\u70b9\u51fb\u5de6\u4e0a\u89d2\u201c\u914d\u7f6e\u201d\u586b\u5199 API \u540e\u5373\u53ef\u4f7f\u7528\u3002",
        requestCancelledLine: "\u8bf7\u6c42\u5df2\u53d6\u6d88\u3002",
        requestFailedLine: "\u8bf7\u6c42\u5931\u8d25: {error}\u3002\u53ef\u5728\u5de6\u4e0a\u89d2\u201c\u914d\u7f6e\u201d\u91cc\u70b9\u201c\u8fde\u901a\u6d4b\u8bd5\u201d\u6392\u67e5\u7f51\u7edc/API\u8bbe\u7f6e\u3002",
        apiSettings: "\u63a5\u53e3\u914d\u7f6e",
        adminTokenLabel: "\u7ba1\u7406\u4ee4\u724c\uff08\u4ec5\u5f53\u670d\u52a1\u7aef\u8bbe\u7f6e qinglongclaw_WEB_TOKEN \u65f6\u9700\u8981\uff09",
        adminTokenPlaceholder: "\u53ef\u9009",
        defaultModelLabel: "\u9ed8\u8ba4\u6a21\u578b",
        maxToolIterationsLabel: "\u6700\u5927\u5de5\u5177\u8fed\u4ee3\u6b21\u6570\uff080=\u4e0d\u9650\u5236\uff09",
        maxHistoryMessagesLabel: "\u4e0a\u4e0b\u6587\u5386\u53f2\u6d88\u606f\u6761\u6570",
        workspacePathLabel: "\u5de5\u4f5c\u7a7a\u95f4\u8def\u5f84",
        workspacePathPlaceholder: "/userdata/QingLongClaw",
        restrictWorkspaceLabel: "\u4ec5\u5141\u8bb8\u5de5\u5177\u8bbf\u95ee\u5de5\u4f5c\u7a7a\u95f4",
        codexPrimaryLabel: "\u672a\u624b\u52a8\u6307\u5b9a\u6a21\u578b\u65f6\u4f18\u5148\u4f7f\u7528 Codex",
        codexFallbackOnErrorLabel: "Codex \u8bf7\u6c42\u5931\u8d25\u65f6\u56de\u9000\u5230\u975e Codex \u6a21\u578b",
        groupGlm: "GLM (Zhipu)",
        groupDeepseek: "DeepSeek",
        groupQwen: "Qwen",
        groupMinmax: "MinMax",
        groupCodex: "Removed Provider",
        groupFeishu: "\u98de\u4e66",
        apiKeyLabel: "API Key",
        apiBaseLabel: "API Base",
        proxyOptionalLabel: "\u4ee3\u7406\uff08\u53ef\u9009\uff09",
        proxyPlaceholder: "http://127.0.0.1:7890",
        loadModels: "\u52a0\u8f7d\u6a21\u578b",
        loadedModelsLabel: "\u5df2\u52a0\u8f7d\u6a21\u578b",
        customModelNameLabel: "\u81ea\u5b9a\u4e49\u6a21\u578b\u540d",
        addCustomModel: "\u6dfb\u52a0\u81ea\u5b9a\u4e49\u6a21\u578b",
        glmKeyPlaceholder: "\u8f93\u5165 GLM API Key",
        glmBasePlaceholder: "https://open.bigmodel.cn/api/paas/v4",
        glmCustomModelPlaceholder: "\u4f8b\u5982\uff1aglm-4-plus",
        deepseekKeyPlaceholder: "\u8f93\u5165 DeepSeek API Key",
        deepseekBasePlaceholder: "https://api.deepseek.com/v1",
        deepseekCustomModelPlaceholder: "\u4f8b\u5982\uff1adeepseek-reasoner",
        qwenKeyPlaceholder: "\u8f93\u5165 Qwen API Key",
        qwenBasePlaceholder: "https://dashscope.aliyuncs.com/compatible-mode/v1",
        qwenCustomModelPlaceholder: "\u4f8b\u5982\uff1aqwen-max",
        minmaxKeyPlaceholder: "\u8f93\u5165 MinMax API Key",
        minmaxBasePlaceholder: "https://api.minimaxi.com/v1",
        minmaxCustomModelPlaceholder: "\u4f8b\u5982\uff1aMiniMax-M2.1",
        codexKeyPlaceholder: "\u8f93\u5165 Codex API Key",
        codexBasePlaceholder: "https://api.invalid/removed-provider",
        codexCustomModelPlaceholder: "\u4f8b\u5982\uff1agpt-5.1-codex",
        feishuAppIdLabel: "\u98de\u4e66 App ID",
        feishuAppIdPlaceholder: "cli_xxx",
        feishuAppSecretLabel: "\u98de\u4e66 App Secret",
        feishuAppSecretPlaceholder: "\u8f93\u5165\u98de\u4e66 App Secret",
        feishuProxyLabel: "\u98de\u4e66\u4ee3\u7406\uff08\u53ef\u9009\uff09",
        feishuDefaultChatIdLabel: "\u98de\u4e66\u9ed8\u8ba4 Chat ID\uff08Web \u5411\u98de\u4e66\u540c\u6b65\u65f6\u4f7f\u7528\uff09",
        feishuDefaultChatIdPlaceholder: "oc_xxx / chat_id",
        feishuEnabledHint: "\u5f53 App ID \u4e0e App Secret \u90fd\u5df2\u8bbe\u7f6e\u65f6\uff0c\u98de\u4e66\u901a\u9053\u81ea\u52a8\u542f\u7528",
        feishuModeHint: "\u5f53\u524d\u4f7f\u7528\u957f\u8fde\u63a5\u6a21\u5f0f\uff0c\u65e0\u9700\u56de\u8c03 URL"
      },
      en: {
        title: "QingLongClaw Chat Console",
        config: "Config",
        close: "Close",
        reload: "Reload",
        probe: "Connectivity Test",
        save: "Save Config",
        send: "Send",
        stop: "Stop",
        langZh: "Chinese",
        langEn: "English",
        sessionPlaceholder: "Session ID",
        inputPlaceholder: "Type a message. Enter to send, Shift+Enter for newline",
        defaultModelOption: "Default model",
        notLoaded: "Not loaded",
        copy: "Copy",
        copied: "Copied",
        copyFailed: "Failed",
        generating: "Generating reply...",
        done: "Done.",
        cancelled: "Cancelled.",
        requestFailed: "Request failed.",
        pleaseInput: "Please enter a message.",
        cfgLoaded: "Configuration loaded.",
        cfgSaved: "Configuration saved successfully.",
        statusStopRequested: "Stop requested. Waiting for current task to finish.",
        feishuGenerating: "Feishu task is generating a reply...",
        feishuDone: "Feishu task completed",
        feishuDoneWithTime: "Feishu task completed in {elapsed}",
        feishuPrefix: "Feishu: ",
        failedSyncRuntimeModel: "Failed to sync runtime model: {error}",
        failedLoadConfig: "Failed to load config: {error}",
        failedSaveConfig: "Failed to save config: {error}",
        unsupportedProvider: "Unsupported provider: {provider}",
        enterModelName: "Please enter a model name.",
        modelExists: "{provider} model already exists or is invalid.",
        customModelAdded: "{provider} custom model added: {model}",
        modelsLoadedSaved: "{provider} models loaded and saved: {count}",
        modelsLoadFailed: "{provider} model loading failed: {error}",
        connectivityOk: "Connectivity OK: {status} ({apiBase})",
        connectivityFailed: "Connectivity failed: {error}",
        httpStatusUnknown: "No HTTP status code",
        welcomeBanner: "Welcome to {displayName}. Current version {version}, Tool API v{toolApi}. Click Config (top-left) to set API keys and start chatting.",
        requestCancelledLine: "Request cancelled.",
        requestFailedLine: "Request failed: {error}. Use Config -> Connectivity Test to verify API/network settings.",
        apiSettings: "API Settings",
        adminTokenLabel: "Admin token (required only when qinglongclaw_WEB_TOKEN is set on server)",
        adminTokenPlaceholder: "Optional",
        defaultModelLabel: "Default model",
        maxToolIterationsLabel: "Max tool iterations (0 = unlimited)",
        maxHistoryMessagesLabel: "Context history messages",
        workspacePathLabel: "Workspace Path",
        workspacePathPlaceholder: "/userdata/QingLongClaw",
        restrictWorkspaceLabel: "Only allow tools to access workspace",
        codexPrimaryLabel: "Codex priority when model is not manually selected",
        codexFallbackOnErrorLabel: "Fallback to non-codex model if Codex request fails",
        groupGlm: "GLM (Zhipu)",
        groupDeepseek: "DeepSeek",
        groupQwen: "Qwen",
        groupMinmax: "MinMax",
        groupCodex: "Removed Provider",
        groupFeishu: "Feishu",
        apiKeyLabel: "API Key",
        apiBaseLabel: "API Base",
        proxyOptionalLabel: "Proxy (optional)",
        proxyPlaceholder: "http://127.0.0.1:7890",
        loadModels: "Load models",
        loadedModelsLabel: "Loaded models",
        customModelNameLabel: "Custom model name",
        addCustomModel: "Add custom model",
        glmKeyPlaceholder: "Enter GLM API key",
        glmBasePlaceholder: "https://open.bigmodel.cn/api/paas/v4",
        glmCustomModelPlaceholder: "e.g. glm-4-plus",
        deepseekKeyPlaceholder: "Enter DeepSeek API key",
        deepseekBasePlaceholder: "https://api.deepseek.com/v1",
        deepseekCustomModelPlaceholder: "e.g. deepseek-reasoner",
        qwenKeyPlaceholder: "Enter Qwen API key",
        qwenBasePlaceholder: "https://dashscope.aliyuncs.com/compatible-mode/v1",
        qwenCustomModelPlaceholder: "e.g. qwen-max",
        minmaxKeyPlaceholder: "Enter MinMax API key",
        minmaxBasePlaceholder: "https://api.minimaxi.com/v1",
        minmaxCustomModelPlaceholder: "e.g. MiniMax-M2.1",
        codexKeyPlaceholder: "Enter Codex API key",
        codexBasePlaceholder: "https://api.invalid/removed-provider",
        codexCustomModelPlaceholder: "e.g. gpt-5.1-codex",
        feishuAppIdLabel: "Feishu App ID",
        feishuAppIdPlaceholder: "cli_xxx",
        feishuAppSecretLabel: "Feishu App Secret",
        feishuAppSecretPlaceholder: "Enter Feishu App Secret",
        feishuProxyLabel: "Feishu proxy (optional)",
        feishuDefaultChatIdLabel: "Feishu default Chat ID (used for web-to-Feishu sync)",
        feishuDefaultChatIdPlaceholder: "oc_xxx / chat_id",
        feishuEnabledHint: "Feishu is enabled when App ID and App Secret are both set",
        feishuModeHint: "Using long-connection mode; callback URL is not required"
      }
    };

    function currentLang() {
      const stored = (localStorage.getItem("qinglongclaw.lang") || "").toLowerCase();
      if (stored === "en" || stored === "zh") {
        return stored;
      }
      const nav = (navigator.language || "").toLowerCase();
      return nav.startsWith("zh") ? "zh" : "en";
    }

    function tr(key, vars = {}) {
      const lang = currentLang();
      const dict = I18N[lang] || I18N.zh;
      let out = dict[key] || I18N.en[key] || key;
      for (const [k, v] of Object.entries(vars)) {
        out = out.replace(new RegExp("\\\\{" + k + "\\\\}", "g"), String(v));
      }
      return out;
    }

    function applyI18n() {
      const lang = currentLang();
      document.documentElement.lang = lang === "zh" ? "zh-CN" : "en";
      document.title = tr("title");
      const langSelect = el("langSelect");
      if (langSelect) {
        if (langSelect.options.length >= 2) {
          langSelect.options[0].textContent = tr("langZh");
          langSelect.options[1].textContent = tr("langEn");
        }
        langSelect.value = lang;
      }
      if (el("openCfgBtn")) el("openCfgBtn").textContent = tr("config");
      if (el("closeCfgBtn")) el("closeCfgBtn").textContent = tr("close");
      if (el("reloadCfgBtn")) el("reloadCfgBtn").textContent = tr("reload");
      if (el("probeCfgBtn")) el("probeCfgBtn").textContent = tr("probe");
      if (el("saveCfgBtn")) el("saveCfgBtn").textContent = tr("save");
      if (el("sessionKey")) el("sessionKey").placeholder = tr("sessionPlaceholder");
      if (el("chatInput")) el("chatInput").placeholder = tr("inputPlaceholder");
      const topTitle = document.querySelector('.title');
      if (topTitle) topTitle.textContent = tr("title");
      document.querySelectorAll("[data-i18n]").forEach((node) => {
        const key = node.getAttribute("data-i18n");
        if (!key) {
          return;
        }
        node.textContent = tr(key);
      });
      document.querySelectorAll("[data-i18n-placeholder]").forEach((node) => {
        const key = node.getAttribute("data-i18n-placeholder");
        if (!key || !("placeholder" in node)) {
          return;
        }
        node.placeholder = tr(key);
      });
      document.querySelectorAll(".msg-copy").forEach((btn) => {
        btn.textContent = tr("copy");
      });
      const sendBtn = el("sendBtn");
      if (sendBtn) {
        sendBtn.textContent = isAnyChatRunning() ? tr("stop") : tr("send");
      }
    }

    function formatElapsedDuration(ms) {
      if (!Number.isFinite(ms) || ms <= 0) {
        return "0s";
      }
      const totalSeconds = Math.max(1, Math.round(ms / 1000));
      const minutes = Math.floor(totalSeconds / 60);
      const seconds = totalSeconds % 60;
      if (minutes <= 0) {
        return totalSeconds + "s";
      }
      if (minutes < 60) {
        return minutes + "m" + (seconds > 0 ? (" " + seconds + "s") : "");
      }
      const hours = Math.floor(minutes / 60);
      const remainMinutes = minutes % 60;
      return hours + "h" + (remainMinutes > 0 ? (" " + remainMinutes + "m") : "");
    }

    function rebuildChatModelOptions() {
      const select = el("chatModel");
      const current = select.value;
      const seen = new Set();
      select.innerHTML = "";
      const defaultOpt = document.createElement("option");
      defaultOpt.value = "";
      defaultOpt.textContent = tr("defaultModelOption");
      select.appendChild(defaultOpt);
      seen.add("");

      for (const model of baseModels) {
        if (seen.has(model)) {
          continue;
        }
        seen.add(model);
        const opt = document.createElement("option");
        opt.value = model;
        opt.textContent = model;
        select.appendChild(opt);
      }
      for (const model of glmModelsCatalog) {
        const value = "zhipu/" + model;
        if (seen.has(value)) {
          continue;
        }
        seen.add(value);
        const opt = document.createElement("option");
        opt.value = value;
        opt.textContent = model + " (glm)";
        select.appendChild(opt);
      }
      for (const model of deepseekModelsCatalog) {
        const value = "deepseek/" + model;
        if (seen.has(value)) {
          continue;
        }
        seen.add(value);
        const opt = document.createElement("option");
        opt.value = value;
        opt.textContent = model + " (deepseek)";
        select.appendChild(opt);
      }
      for (const model of qwenModelsCatalog) {
        const value = "qwen/" + model;
        if (seen.has(value)) {
          continue;
        }
        seen.add(value);
        const opt = document.createElement("option");
        opt.value = value;
        opt.textContent = model + " (qwen)";
        select.appendChild(opt);
      }
      for (const model of minmaxModelsCatalog) {
        const value = "minmax/" + model;
        if (seen.has(value)) {
          continue;
        }
        seen.add(value);
        const opt = document.createElement("option");
        opt.value = value;
        opt.textContent = model + " (minmax)";
        select.appendChild(opt);
      }
      for (const model of codexModelsCatalog) {
        const value = "codex/" + model;
        if (seen.has(value)) {
          continue;
        }
        seen.add(value);
        const opt = document.createElement("option");
        opt.value = value;
        opt.textContent = model + " (codex)";
        select.appendChild(opt);
      }
      if ([...select.options].some((opt) => opt.value === current)) {
        select.value = current;
      }
    }

    function rebuildModelSelect(selectId, models) {
      const select = el(selectId);
      const current = select.value;
      select.innerHTML = "";
      if (!models.length) {
        const opt = document.createElement("option");
        opt.value = "";
        opt.textContent = tr("notLoaded");
        select.appendChild(opt);
        return;
      }
      for (const model of models) {
        const opt = document.createElement("option");
        opt.value = model;
        opt.textContent = model;
        select.appendChild(opt);
      }
      if ([...select.options].some((opt) => opt.value === current)) {
        select.value = current;
      }
    }

    function rebuildProviderModelSelects() {
      rebuildModelSelect("glmModelSelect", glmModelsCatalog);
      rebuildModelSelect("deepseekModelSelect", deepseekModelsCatalog);
      rebuildModelSelect("qwenModelSelect", qwenModelsCatalog);
      rebuildModelSelect("minmaxModelSelect", minmaxModelsCatalog);
      rebuildModelSelect("codexModelSelect", codexModelsCatalog);
    }

    function addModelToCatalog(catalog, modelName) {
      const value = (modelName || "").trim();
      if (!value) {
        return false;
      }
      if (catalog.some((m) => m === value)) {
        return false;
      }
      catalog.push(value);
      return true;
    }

    function mergeCatalogModels(existing, incoming) {
      const out = Array.isArray(existing) ? existing.slice() : [];
      for (const model of (Array.isArray(incoming) ? incoming : [])) {
        const value = (typeof model === "string") ? model.trim() : "";
        if (!value || out.some((m) => m === value)) {
          continue;
        }
        out.push(value);
      }
      return out;
    }

    async function addCustomProviderModel(provider) {
      const providerName = (provider || "").trim().toLowerCase();
      let inputId = "";
      let catalog = null;
      let label = providerName;
      if (providerName === "zhipu" || providerName === "glm") {
        inputId = "glmCustomModelInput";
        catalog = glmModelsCatalog;
        label = "GLM";
      } else if (providerName === "deepseek") {
        inputId = "deepseekCustomModelInput";
        catalog = deepseekModelsCatalog;
        label = "DeepSeek";
      } else if (providerName === "qwen") {
        inputId = "qwenCustomModelInput";
        catalog = qwenModelsCatalog;
        label = "Qwen";
      } else if (providerName === "minmax" || providerName === "minimax") {
        inputId = "minmaxCustomModelInput";
        catalog = minmaxModelsCatalog;
        label = "MinMax";
      } else if (providerName === "codex") {
        inputId = "codexCustomModelInput";
        catalog = codexModelsCatalog;
        label = "Codex";
      } else {
        setCfgMsg(tr("unsupportedProvider", { provider: providerName }), false);
        return;
      }

      const input = el(inputId);
      const modelName = (input.value || "").trim();
      if (!modelName) {
        setCfgMsg(tr("enterModelName"), false);
        return;
      }

      const added = addModelToCatalog(catalog, modelName);
      if (!added) {
        setCfgMsg(tr("modelExists", { provider: label }), false);
        return;
      }

      input.value = "";
      rebuildProviderModelSelects();
      rebuildChatModelOptions();
      await syncRuntimeModel(true);
      await saveConfig();
      setCfgMsg(tr("customModelAdded", { provider: label, model: modelName }), true);
    }

    function setStatus(text, ok = true) {
      const node = el("chatStatus");
      node.textContent = text || "";
      node.className = "status " + (ok ? "ok" : "err");
    }

    function setCfgMsg(text, ok = true) {
      const node = el("cfgMsg");
      node.textContent = text || "";
      node.className = "cfg-msg " + (ok ? "ok" : "err");
    }

    function isAnyChatRunning() {
      return localChatInFlight || runningSessions.size > 0;
    }

    function refreshChatRunningState() {
      const btn = el("sendBtn");
      if (isAnyChatRunning()) {
        btn.textContent = tr("stop");
        btn.classList.remove("primary");
        btn.classList.add("busy");
        btn.disabled = false;
      } else {
        btn.textContent = tr("send");
        btn.classList.remove("busy");
        btn.classList.add("primary");
        btn.disabled = false;
      }
    }

    function setLocalChatRunning(running) {
      localChatInFlight = !!running;
      refreshChatRunningState();
    }

    function updateRunningSessionsFromEvent(event) {
      if (!event || typeof event !== "object") {
        return;
      }
      const kind = (typeof event.kind === "string") ? event.kind : "message";
      const sessionKey = (typeof event.session_key === "string") ? event.session_key.trim() : "";
      if (!sessionKey) {
        return;
      }
      if (kind === "task_start") {
        runningSessions.add(sessionKey);
      } else if (kind === "task_end") {
        runningSessions.delete(sessionKey);
      }
    }

    function syncRunningSessions(snapshot) {
      runningSessions.clear();
      for (const item of (Array.isArray(snapshot) ? snapshot : [])) {
        const key = (typeof item === "string") ? item.trim() : "";
        if (!key) {
          continue;
        }
        runningSessions.add(key);
      }
      for (const key of [...sessionStartTimes.keys()]) {
        if (!runningSessions.has(key)) {
          sessionStartTimes.delete(key);
        }
      }
      refreshChatRunningState();
    }

    async function stopCurrentChat() {
      if (chatAbortController) {
        chatAbortController.abort();
        chatAbortController = null;
      }
      setLocalChatRunning(false);

      const targets = new Set();
      const currentSessionKey = (el("sessionKey").value || "web:default").trim();
      if (currentSessionKey) {
        targets.add(currentSessionKey);
      }
      for (const key of runningSessions) {
        targets.add(key);
      }
      if (!targets.size) {
        targets.add("web:default");
      }

      const stopPromises = [];
      for (const session_key of targets) {
        stopPromises.push(fetch("/api/chat/stop", {
          method: "POST",
          headers: tokenHeaders(true),
          body: JSON.stringify({ session_key })
        }).catch(() => {}));
      }
      await Promise.all(stopPromises);
      setStatus(tr("statusStopRequested"), true);
      await pollGatewayEvents();
      refreshChatRunningState();
    }

    function tokenHeaders(json = true) {
      const headers = {};
      if (json) headers["Content-Type"] = "application/json";
      const token = el("adminToken").value.trim();
      if (token) headers["X-Admin-Token"] = token;
      return headers;
    }

    function openConfig() {
      el("cfgDrawer").classList.add("show");
      el("overlay").classList.add("show");
    }

    function closeConfig() {
      el("cfgDrawer").classList.remove("show");
      el("overlay").classList.remove("show");
    }

    async function copyText(text) {
      const value = String(text || "");
      if (!value) {
        return false;
      }
      if (navigator.clipboard && window.isSecureContext) {
        await navigator.clipboard.writeText(value);
        return true;
      }
      const area = document.createElement("textarea");
      area.value = value;
      area.style.position = "fixed";
      area.style.opacity = "0";
      document.body.appendChild(area);
      area.focus();
      area.select();
      let ok = false;
      try {
        ok = document.execCommand("copy");
      } catch (e) {
        ok = false;
      }
      document.body.removeChild(area);
      return ok;
    }

    function appendMessage(role, text) {
      const wrap = el("chatList");
      const row = document.createElement("div");
      row.className = "msg-row " + role;

      const item = document.createElement("div");
      item.className = "msg " + role;

      const body = document.createElement("div");
      body.className = "msg-text";
      body.textContent = text;
      item.appendChild(body);
      row.appendChild(item);

      const copyBtn = document.createElement("button");
      copyBtn.type = "button";
      copyBtn.className = "msg-copy";
      copyBtn.textContent = tr("copy");
      copyBtn.addEventListener("click", async () => {
        try {
          const ok = await copyText(text);
          copyBtn.textContent = ok ? tr("copied") : tr("copyFailed");
          setTimeout(() => {
            copyBtn.textContent = tr("copy");
          }, 1200);
        } catch (e) {
          copyBtn.textContent = tr("copyFailed");
          setTimeout(() => {
            copyBtn.textContent = tr("copy");
          }, 1200);
        }
      });
      row.appendChild(copyBtn);

      wrap.appendChild(row);
      wrap.scrollTop = wrap.scrollHeight;
    }

    function renderGatewayEvent(event) {
      const kind = (event && typeof event.kind === "string") ? event.kind : "message";
      const sessionKey = (event && typeof event.session_key === "string") ? event.session_key.trim() : "";
      const isFeishuSession = sessionKey.startsWith("feishu:");
      const isFeishuEvent = !!(event && event.source === "feishu");
      const eventTimeMs = (event && typeof event.time_ms === "number" && Number.isFinite(event.time_ms)) ?
        event.time_ms : Date.now();
      if (kind === "task_start") {
        if (sessionKey) {
          sessionStartTimes.set(sessionKey, eventTimeMs);
        }
        if (isFeishuSession && isFeishuEvent) {
          appendMessage("system", tr("feishuGenerating"));
          setStatus(tr("generating"), true);
        }
        return;
      }
      if (kind === "task_end") {
        let elapsed = "";
        if (sessionKey && sessionStartTimes.has(sessionKey)) {
          const startedAt = sessionStartTimes.get(sessionKey);
          if (typeof startedAt === "number" && Number.isFinite(startedAt)) {
            elapsed = formatElapsedDuration(Math.max(0, eventTimeMs - startedAt));
          }
          sessionStartTimes.delete(sessionKey);
        }
        if (isFeishuSession && isFeishuEvent) {
          let doneText = elapsed ? tr("feishuDoneWithTime", { elapsed }) : tr("feishuDone");
          if (elapsed) {
            doneText = doneText.replace("{elapsed}", elapsed);
          }
          appendMessage("system", doneText);
          setStatus(doneText, true);
        }
        return;
      }
      if (kind !== "message") {
        return;
      }
      const role = (event && (event.role === "user" || event.role === "assistant")) ? event.role : "system";
      let text = (event && typeof event.content === "string") ? event.content : "";
      if (!text.trim()) {
        return;
      }
      if (event && event.source === "feishu" && role === "user") {
        text = tr("feishuPrefix") + text;
      } else if (event && event.source === "gateway" && role === "assistant" && event.model_name) {
        text = "[" + event.model_name + "] " + text;
      }
      appendMessage(role, text);
    }

    async function syncRuntimeModel(silent = true) {
      const model_name = (el("chatModel").value || el("defaultModel").value || "").trim();
      if (!model_name) {
        return;
      }
      try {
        const res = await fetch("/api/runtime/model", {
          method: "POST",
          headers: tokenHeaders(true),
          body: JSON.stringify({ model_name })
        });
        const data = await res.json();
        if (!res.ok || !data.ok) throw new Error(data.error || ("HTTP " + res.status));
      } catch (e) {
        if (!silent) {
          setStatus(tr("failedSyncRuntimeModel", { error: e.message }), false);
        }
      }
    }

    async function loadRuntimeModel() {
      try {
        const res = await fetch("/api/runtime/model", {
          headers: tokenHeaders(false)
        });
        const data = await res.json();
        if (!res.ok || !data.ok) {
          throw new Error(data.error || ("HTTP " + res.status));
        }

        const model_name = (data.model_name || "").trim();
        if (!model_name) {
          return false;
        }

        const select = el("chatModel");
        if (![...select.options].some((opt) => opt.value === model_name)) {
          const opt = document.createElement("option");
          opt.value = model_name;
          opt.textContent = model_name;
          select.appendChild(opt);
        }
        select.value = model_name;
        return true;
      } catch (e) {
        return false;
      }
    }

    async function pollGatewayEvents() {
      if (pollingEvents) {
        return;
      }
      pollingEvents = true;
      try {
        const res = await fetch("/api/events?since_id=" + encodeURIComponent(String(lastEventId)), {
          headers: tokenHeaders(false)
        });
        const data = await res.json();
        if (!res.ok || !data.ok) {
          throw new Error(data.error || ("HTTP " + res.status));
        }
        const instanceId = (data && typeof data.instance_id === "string") ? data.instance_id : "";
        if (instanceId) {
          if (eventsInstanceId && eventsInstanceId !== instanceId) {
            // Gateway restarted; reset cursor so cross-channel events can be consumed again.
            eventsInstanceId = instanceId;
            lastEventId = 0;
            runningSessions.clear();
            sessionStartTimes.clear();
            seenEventIds.clear();
            return;
          }
          eventsInstanceId = instanceId;
        }

        const events = Array.isArray(data.events) ? data.events : [];
        for (const event of events) {
          if (event && typeof event.id === "number") {
            if (seenEventIds.has(event.id)) {
              continue;
            }
            seenEventIds.add(event.id);
            if (seenEventIds.size > 12000) {
              const oldest = seenEventIds.values().next();
              if (!oldest.done) {
                seenEventIds.delete(oldest.value);
              }
            }
            if (event.id > lastEventId) {
              lastEventId = event.id;
            }
          }
          updateRunningSessionsFromEvent(event);
          renderGatewayEvent(event);
        }
        if (typeof data.next_since_id === "number" && data.next_since_id > lastEventId) {
          lastEventId = data.next_since_id;
        }
        if (Array.isArray(data.running_sessions)) {
          syncRunningSessions(data.running_sessions);
        } else {
          refreshChatRunningState();
        }
      } catch (e) {
      } finally {
        pollingEvents = false;
      }
    }

    function startGatewayEventPolling() {
      if (eventsTimer !== null) {
        clearInterval(eventsTimer);
      }
      eventsTimer = setInterval(() => {
        pollGatewayEvents();
      }, 1000);
      pollGatewayEvents();
    }

    function collectConfigPayload() {
      const maxToolIterationsRaw = Number(el("maxToolIterations").value);
      const max_tool_iterations = Number.isFinite(maxToolIterationsRaw)
        ? Math.max(0, Math.min(100000, Math.round(maxToolIterationsRaw)))
        : 0;
      const maxHistoryMessagesRaw = Number(el("maxHistoryMessages").value);
      const max_history_messages = Number.isFinite(maxHistoryMessagesRaw)
        ? Math.max(4, Math.min(800, Math.round(maxHistoryMessagesRaw)))
        : 24;
      return {
        default_model: el("defaultModel").value,
        max_tool_iterations,
        max_history_messages,
        workspace_path: el("workspacePath").value || "",
        restrict_to_workspace: !!el("restrictWorkspace").checked,
        models: [
          {
            model_name: "glm-4.7",
            api_key: el("glmKey").value || "",
            api_base: el("glmBase").value || "",
            proxy: el("glmProxy").value || ""
          },
          {
            model_name: "deepseek-chat",
            api_key: el("deepseekKey").value || "",
            api_base: el("deepseekBase").value || "",
            proxy: el("deepseekProxy").value || ""
          },
          {
            model_name: "qwen-plus",
            api_key: el("qwenKey").value || "",
            api_base: el("qwenBase").value || "",
            proxy: el("qwenProxy").value || ""
          },
          {
            model_name: "minimax-m2.1",
            api_key: el("minmaxKey").value || "",
            api_base: el("minmaxBase").value || "",
            proxy: el("minmaxProxy").value || ""
          }
        ],
        feishu: {
          app_id: el("feishuAppId").value || "",
          app_secret: el("feishuAppSecret").value || "",
          proxy: el("feishuProxy").value || "",
          default_chat_id: el("feishuDefaultChatId").value || ""
        },
        provider_models: {
          zhipu: glmModelsCatalog,
          deepseek: deepseekModelsCatalog,
          qwen: qwenModelsCatalog,
          minmax: minmaxModelsCatalog
        }
      };
    }

    async function loadConfig() {
      try {
        const res = await fetch("/api/config", { headers: tokenHeaders(false) });
        const data = await res.json();
        if (!res.ok || !data.ok) throw new Error(data.error || ("HTTP " + res.status));
        el("defaultModel").value = data.default_model || "glm-4.7";
        el("maxToolIterations").value = String(
          Math.max(0, Math.min(100000, Number(data.max_tool_iterations ?? 0) || 0))
        );
        el("maxHistoryMessages").value = String(
          Math.max(4, Math.min(800, Number(data.max_history_messages ?? 24) || 24))
        );
        el("workspacePath").value = data.workspace_path || "";
        el("restrictWorkspace").checked = data.restrict_to_workspace !== false;
        for (const m of data.models || []) {
          if (m.model_name === "glm-4.7") {
            el("glmKey").value = m.api_key || "";
            el("glmBase").value = m.api_base || "";
            el("glmProxy").value = m.proxy || "";
          }
          if (m.model_name === "deepseek-chat") {
            el("deepseekKey").value = m.api_key || "";
            el("deepseekBase").value = m.api_base || "";
            el("deepseekProxy").value = m.proxy || "";
          }
          if (m.model_name === "qwen-plus") {
            el("qwenKey").value = m.api_key || "";
            el("qwenBase").value = m.api_base || "";
            el("qwenProxy").value = m.proxy || "";
          }
          if (m.model_name === "minimax-m2.1") {
            el("minmaxKey").value = m.api_key || "";
            el("minmaxBase").value = m.api_base || "";
            el("minmaxProxy").value = m.proxy || "";
          }
        }
        const feishu = data.feishu || {};
        el("feishuAppId").value = feishu.app_id || "";
        el("feishuAppSecret").value = feishu.app_secret || "";
        el("feishuProxy").value = feishu.proxy || "";
        el("feishuDefaultChatId").value = feishu.default_chat_id || "";
        const providerModels = data.provider_models || {};
        glmModelsCatalog = Array.isArray(providerModels.zhipu) ?
          providerModels.zhipu.filter((m) => typeof m === "string" && m.trim()) : [];
        deepseekModelsCatalog = Array.isArray(providerModels.deepseek) ?
          providerModels.deepseek.filter((m) => typeof m === "string" && m.trim()) : [];
        qwenModelsCatalog = Array.isArray(providerModels.qwen) ?
          providerModels.qwen.filter((m) => typeof m === "string" && m.trim()) : [];
        minmaxModelsCatalog = Array.isArray(providerModels.minmax) ?
          providerModels.minmax.filter((m) => typeof m === "string" && m.trim()) : [];
        minmaxModelsCatalog = mergeCatalogModels(minmaxModelsCatalog, builtInMinmaxModels);
        rebuildProviderModelSelects();
        rebuildChatModelOptions();
        if (!glmModelsCatalog.length && (el("glmKey").value || "").trim()) {
          await loadProviderModels("zhipu", true);
        }
        if (!deepseekModelsCatalog.length && (el("deepseekKey").value || "").trim()) {
          await loadProviderModels("deepseek", true);
        }
        if (!qwenModelsCatalog.length && (el("qwenKey").value || "").trim()) {
          await loadProviderModels("qwen", true);
        }
        if (!minmaxModelsCatalog.length && (el("minmaxKey").value || "").trim()) {
          await loadProviderModels("minmax", true);
        }
        const runtimeLoaded = await loadRuntimeModel();
        if (!runtimeLoaded) {
          await syncRuntimeModel(true);
        }
        setCfgMsg(tr("cfgLoaded"), true);
      } catch (e) {
        setCfgMsg(tr("failedLoadConfig", { error: e.message }), false);
      }
    }

    async function saveConfig() {
      try {
        const res = await fetch("/api/config", {
          method: "POST",
          headers: tokenHeaders(true),
          body: JSON.stringify(collectConfigPayload())
        });
        const data = await res.json();
        if (!res.ok || !data.ok) throw new Error(data.error || ("HTTP " + res.status));
        await syncRuntimeModel(true);
        setCfgMsg(tr("cfgSaved"), true);
      } catch (e) {
        setCfgMsg(tr("failedSaveConfig", { error: e.message }), false);
      }
    }

    async function loadProviderModels(provider, silent = false) {
      const providerName = (provider || "").trim().toLowerCase();
      if (!providerName) {
        return;
      }

      let api_key = "";
      let api_base = "";
      let proxy = "";
      let uiLabel = providerName;
      if (providerName === "zhipu" || providerName === "glm") {
        api_key = el("glmKey").value || "";
        api_base = el("glmBase").value || "";
        proxy = el("glmProxy").value || "";
        uiLabel = "GLM";
      } else if (providerName === "deepseek") {
        api_key = el("deepseekKey").value || "";
        api_base = el("deepseekBase").value || "";
        proxy = el("deepseekProxy").value || "";
        uiLabel = "DeepSeek";
      } else if (providerName === "qwen") {
        api_key = el("qwenKey").value || "";
        api_base = el("qwenBase").value || "";
        proxy = el("qwenProxy").value || "";
        uiLabel = "Qwen";
      } else if (providerName === "minmax" || providerName === "minimax") {
        api_key = el("minmaxKey").value || "";
        api_base = el("minmaxBase").value || "";
        proxy = el("minmaxProxy").value || "";
        uiLabel = "MinMax";
      } else if (providerName === "codex") {
        api_key = el("codexKey").value || "";
        api_base = el("codexBase").value || "";
        proxy = el("codexProxy").value || "";
        uiLabel = "Codex";
      } else {
        if (!silent) {
          setCfgMsg(tr("unsupportedProvider", { provider: providerName }), false);
        }
        return;
      }

      try {
        const res = await fetch("/api/provider/models", {
          method: "POST",
          headers: tokenHeaders(true),
          body: JSON.stringify({
            provider: providerName,
            api_key,
            api_base,
            proxy
          })
        });
        const data = await res.json();
        if (!res.ok || !data.ok) throw new Error(data.error || ("HTTP " + res.status));

        const models = Array.isArray(data.models) ? data.models.filter((m) => typeof m === "string" && m.trim()) : [];
        if (providerName === "zhipu" || providerName === "glm") {
          glmModelsCatalog = mergeCatalogModels(glmModelsCatalog, models);
        } else if (providerName === "deepseek") {
          deepseekModelsCatalog = mergeCatalogModels(deepseekModelsCatalog, models);
        } else if (providerName === "qwen") {
          qwenModelsCatalog = mergeCatalogModels(qwenModelsCatalog, models);
        } else if (providerName === "minmax" || providerName === "minimax") {
          minmaxModelsCatalog = mergeCatalogModels(minmaxModelsCatalog, models);
          minmaxModelsCatalog = mergeCatalogModels(minmaxModelsCatalog, builtInMinmaxModels);
        } else if (providerName === "codex") {
          codexModelsCatalog = mergeCatalogModels(codexModelsCatalog, models);
          codexModelsCatalog = mergeCatalogModels(codexModelsCatalog, builtInCodexModels);
        }
        rebuildProviderModelSelects();
        rebuildChatModelOptions();
        await syncRuntimeModel(true);
        if (!silent) {
          await saveConfig();
          setCfgMsg(tr("modelsLoadedSaved", { provider: uiLabel, count: models.length }), true);
        }
      } catch (e) {
        if (!silent) {
          setCfgMsg(tr("modelsLoadFailed", { provider: uiLabel, error: e.message }), false);
        }
      }
    }

    async function loadCodexModels(silent = false) {
      return loadProviderModels("codex", silent);
    }

    async function probeConfig() {
      const model_name = el("chatModel").value || el("defaultModel").value || "";
      try {
        const res = await fetch("/api/probe-model", {
          method: "POST",
          headers: tokenHeaders(true),
          body: JSON.stringify({ model_name })
        });
        const data = await res.json();
        if (!res.ok || !data.ok) throw new Error(data.error || ("HTTP " + res.status));
        const statusText = (data.http_status && data.http_status > 0) ? ("HTTP " + data.http_status) : tr("httpStatusUnknown");
        setCfgMsg(tr("connectivityOk", { status: statusText, apiBase: (data.api_base || "") }), true);
      } catch (e) {
        setCfgMsg(tr("connectivityFailed", { error: e.message }), false);
      }
    }

    async function loadVersionBanner() {
      try {
        const res = await fetch("/api/version", { headers: tokenHeaders(false) });
        const data = await res.json();
        if (res.ok && data.ok) {
          const toolApi = (data.tools && data.tools.api_version) ? data.tools.api_version : "unknown";
          const displayName = data.project_display_name || "QingLongClaw";
          appendMessage("system", tr("welcomeBanner", { displayName, version: (data.qinglongclaw || "unknown"), toolApi }));
          return;
        }
      } catch (e) {
      }
      appendMessage("system", tr("welcomeBanner", { displayName: "QingLongClaw", version: "unknown", toolApi: "unknown" }));
    }

    async function sendChat() {
      if (isAnyChatRunning()) {
        await stopCurrentChat();
        return;
      }

      const input = el("chatInput");
      const message = input.value.trim();
      if (!message) {
        setStatus(tr("pleaseInput"), false);
        return;
      }
      const sessionKey = (el("sessionKey").value || "web:default").trim() || "web:default";
      input.value = "";
      setStatus(tr("generating"), true);
      chatAbortController = new AbortController();
      setLocalChatRunning(true);
      pollGatewayEvents();
      try {
        const res = await fetch("/api/chat", {
          method: "POST",
          headers: tokenHeaders(true),
          signal: chatAbortController.signal,
          body: JSON.stringify({
            message,
            session_key: sessionKey,
            model_name: el("chatModel").value || ""
          })
        });
        const data = await res.json();
        if (!res.ok || !data.ok) throw new Error(data.error || ("HTTP " + res.status));
        await pollGatewayEvents();
        setStatus(tr("done"), true);
      } catch (e) {
        if (e && e.name === "AbortError") {
          await pollGatewayEvents();
          appendMessage("system", tr("requestCancelledLine"));
          setStatus(tr("cancelled"), true);
        } else {
          await pollGatewayEvents();
          appendMessage("system", tr("requestFailedLine", { error: e.message }));
          setStatus(tr("requestFailed"), false);
        }
      } finally {
        chatAbortController = null;
        setLocalChatRunning(false);
      }
    }

    el("openCfgBtn").addEventListener("click", openConfig);
    el("closeCfgBtn").addEventListener("click", closeConfig);
    el("overlay").addEventListener("click", closeConfig);
    el("reloadCfgBtn").addEventListener("click", loadConfig);
    el("probeCfgBtn").addEventListener("click", probeConfig);
    el("saveCfgBtn").addEventListener("click", saveConfig);
    el("loadGlmModelsBtn").addEventListener("click", () => loadProviderModels("zhipu", false));
    el("loadDeepseekModelsBtn").addEventListener("click", () => loadProviderModels("deepseek", false));
    el("loadQwenModelsBtn").addEventListener("click", () => loadProviderModels("qwen", false));
    el("loadMinmaxModelsBtn").addEventListener("click", () => loadProviderModels("minmax", false));
    el("loadCodexModelsBtn").addEventListener("click", () => loadCodexModels(false));
    el("addGlmModelBtn").addEventListener("click", () => addCustomProviderModel("zhipu"));
    el("addDeepseekModelBtn").addEventListener("click", () => addCustomProviderModel("deepseek"));
    el("addQwenModelBtn").addEventListener("click", () => addCustomProviderModel("qwen"));
    el("addMinmaxModelBtn").addEventListener("click", () => addCustomProviderModel("minmax"));
    el("addCodexModelBtn").addEventListener("click", () => addCustomProviderModel("codex"));
    el("sendBtn").addEventListener("click", sendChat);
    el("langSelect").addEventListener("change", (e) => {
      const nextLang = (e && e.target && e.target.value === "en") ? "en" : "zh";
      localStorage.setItem("qinglongclaw.lang", nextLang);
      applyI18n();
      rebuildProviderModelSelects();
      rebuildChatModelOptions();
    });
    el("chatModel").addEventListener("change", () => syncRuntimeModel(false));
    el("defaultModel").addEventListener("change", () => syncRuntimeModel(false));

    el("chatInput").addEventListener("keydown", (e) => {
      if (e.key === "Enter" && !e.shiftKey) {
        e.preventDefault();
        sendChat();
      }
    });

    applyI18n();
    rebuildProviderModelSelects();
    rebuildChatModelOptions();
    refreshChatRunningState();
    loadVersionBanner();
    loadConfig();
    startGatewayEventPolling();
  </script>
</body>
</html>)HTML";

std::string gateway_host(const Config& config) {
  const auto gateway = config.raw.value("gateway", nlohmann::json::object());
  return gateway.value("host", std::string("0.0.0.0"));
}

int gateway_port(const Config& config) {
  const auto gateway = config.raw.value("gateway", nlohmann::json::object());
  return gateway.value("port", 18790);
}

std::vector<std::string> enabled_channels(const Config& config) {
  std::vector<std::string> channels;
  const auto channels_json = config.raw.value("channels", nlohmann::json::object());
  if (!channels_json.is_object()) {
    return channels;
  }
  for (auto it = channels_json.begin(); it != channels_json.end(); ++it) {
    if (!it.value().is_object()) {
      continue;
    }
    if (it.value().value("enabled", false)) {
      channels.push_back(it.key());
    }
  }
  return channels;
}

bool is_authorized(const httplib::Request& req) {
  const char* token_env = std::getenv("qinglongclaw_WEB_TOKEN");
  if (token_env == nullptr || token_env[0] == '\0') {
    return true;
  }
  const std::string expected = token_env;
  const std::string header_token = req.get_header_value("X-Admin-Token");
  if (header_token == expected) {
    return true;
  }
  if (req.has_param("token") && req.get_param_value("token") == expected) {
    return true;
  }
  return false;
}

struct FeishuRuntimeConfig {
  bool enabled = false;
  std::string app_id;
  std::string app_secret;
  std::string default_chat_id;
  std::unordered_set<std::string> allow_from;
  std::string mode = "long_connection";
  std::string api_base = "https://open.feishu.cn";
  std::string proxy;
  int reconnect_count = -1;
  int reconnect_interval_seconds = 120;
  int reconnect_nonce_seconds = 30;
  int ping_interval_seconds = 120;
};

struct FeishuWsHeader {
  std::string key;
  std::string value;
};

struct FeishuWsFrame {
  std::uint64_t seq_id = 0;
  std::uint64_t log_id = 0;
  std::int32_t service = 0;
  std::int32_t method = 0;
  std::vector<FeishuWsHeader> headers;
  std::string payload_encoding;
  std::string payload_type;
  std::string payload;
  std::string log_id_new;
};

struct FeishuWsEndpointData {
  std::string url;
  int reconnect_count = -1;
  int reconnect_interval_seconds = 120;
  int reconnect_nonce_seconds = 30;
  int ping_interval_seconds = 120;
};

struct CodexRuntimeConfig {
  std::string api_key;
  std::string api_base = "https://api.invalid/removed-provider";
  std::string proxy;
  std::vector<std::string> models;
};

struct GatewayChatEvent {
  std::int64_t id = 0;
  std::int64_t time_ms = 0;
  std::string kind = "message";
  std::string role;
  std::string source;
  std::string session_key;
  std::string model_name;
  std::string content;
};

std::string json_string_or_empty(const nlohmann::json& item, const std::string& key) {
  if (!item.is_object() || !item.contains(key) || !item[key].is_string()) {
    return "";
  }
  return item[key].get<std::string>();
}

std::string normalize_api_base(std::string api_base) {
  api_base = trim(api_base);
  while (!api_base.empty() && api_base.back() == '/') {
    api_base.pop_back();
  }
  return api_base;
}

CodexRuntimeConfig read_codex_config(const Config& config) {
  CodexRuntimeConfig codex;
  const auto providers = config.raw.value("providers", nlohmann::json::object());
  if (!providers.is_object() || !providers.contains("codex") || !providers["codex"].is_object()) {
    return codex;
  }
  const auto section = providers["codex"];
  codex.api_key = json_string_or_empty(section, "api_key");
  const auto api_base = normalize_api_base(json_string_or_empty(section, "api_base"));
  if (!api_base.empty()) {
    codex.api_base = api_base;
  }
  codex.proxy = json_string_or_empty(section, "proxy");
  if (section.contains("models") && section["models"].is_array()) {
    std::unordered_set<std::string> seen;
    for (const auto& item : section["models"]) {
      if (!item.is_string()) {
        continue;
      }
      const auto model = trim(item.get<std::string>());
      if (model.empty() || seen.find(model) != seen.end()) {
        continue;
      }
      seen.insert(model);
      codex.models.push_back(model);
    }
  }
  return codex;
}

std::vector<std::string> read_provider_model_catalog(const Config& config, std::string provider) {
  std::vector<std::string> models;
  provider = to_lower(trim(provider));
  if (provider == "glm") {
    provider = "zhipu";
  } else if (provider == "minimax") {
    provider = "minmax";
  }
  if (provider.empty()) {
    return models;
  }
  const auto providers = config.raw.value("providers", nlohmann::json::object());
  if (!providers.is_object() || !providers.contains(provider) || !providers[provider].is_object()) {
    return models;
  }
  const auto section = providers[provider];
  if (!section.contains("models") || !section["models"].is_array()) {
    return models;
  }
  std::unordered_set<std::string> seen;
  for (const auto& item : section["models"]) {
    if (!item.is_string()) {
      continue;
    }
    const auto model = trim(item.get<std::string>());
    if (model.empty() || seen.find(model) != seen.end()) {
      continue;
    }
    seen.insert(model);
    models.push_back(model);
  }
  return models;
}

void add_unique_model_name(const std::string& raw_name,
                           std::vector<std::string>* out,
                           std::unordered_set<std::string>* seen) {
  if (out == nullptr || seen == nullptr) {
    return;
  }
  const auto name = trim(raw_name);
  if (name.empty() || seen->find(name) != seen->end()) {
    return;
  }
  seen->insert(name);
  out->push_back(name);
}

void collect_model_names_from_json(const nlohmann::json& node,
                                   std::vector<std::string>* out,
                                   std::unordered_set<std::string>* seen,
                                   int depth = 0) {
  if (depth > 6 || out == nullptr || seen == nullptr) {
    return;
  }

  if (node.is_array()) {
    for (const auto& item : node) {
      if (item.is_string()) {
        add_unique_model_name(item.get<std::string>(), out, seen);
      } else {
        collect_model_names_from_json(item, out, seen, depth + 1);
      }
    }
    return;
  }

  if (!node.is_object()) {
    return;
  }

  for (const auto* key : {"id", "model", "name"}) {
    if (node.contains(key) && node[key].is_string()) {
      add_unique_model_name(node[key].get<std::string>(), out, seen);
    }
  }

  for (const auto* key : {"data", "models", "result", "items", "list"}) {
    if (node.contains(key)) {
      collect_model_names_from_json(node[key], out, seen, depth + 1);
    }
  }
}

std::vector<std::string> extract_remote_model_names(const nlohmann::json& payload) {
  std::vector<std::string> models;
  std::unordered_set<std::string> seen;
  collect_model_names_from_json(payload, &models, &seen, 0);
  return models;
}

bool is_likely_codex_base(const std::string& api_base);
bool is_likely_minmax_base(const std::string& api_base);
void enrich_minmax_models(std::vector<std::string>* models);
void append_known_codex_models(std::vector<std::string>* models);
void enrich_codex_models_by_probe(const std::string& api_base,
                                  const std::string& api_key,
                                  const std::string& proxy,
                                  std::vector<std::string>* models);

std::vector<std::string> build_model_catalog_urls(const std::string& raw_api_base) {
  std::vector<std::string> urls;
  std::unordered_set<std::string> seen;

  const auto push_unique = [&](const std::string& url) {
    if (!url.empty() && seen.find(url) == seen.end()) {
      seen.insert(url);
      urls.push_back(url);
    }
  };

  std::string base = normalize_api_base(raw_api_base);
  if (base.empty()) {
    return urls;
  }

  auto lower_base = to_lower(base);
  const auto ends_with = [](const std::string& value, const std::string& suffix) -> bool {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
  };
  const auto strip_suffix = [&](const std::string& suffix) {
    if (ends_with(lower_base, suffix)) {
      base.resize(base.size() - suffix.size());
      while (!base.empty() && base.back() == '/') {
        base.pop_back();
      }
      lower_base = to_lower(base);
    }
  };

  if (ends_with(lower_base, "/v1/models") || ends_with(lower_base, "/models")) {
    push_unique(base);
    return urls;
  }

  // Allow users to paste runtime endpoints and still resolve catalog correctly.
  strip_suffix("/v1/chat/completions");
  strip_suffix("/chat/completions");
  strip_suffix("/v1/responses");
  strip_suffix("/responses");

  if (base.empty()) {
    return urls;
  }

  const bool likely_codex = lower_base.find("/api/codex/codex") != std::string::npos;
  if (likely_codex) {
    if (ends_with(lower_base, "/v1")) {
      push_unique(base + "/models");
    } else {
      push_unique(base + "/v1/models");
      push_unique(base + "/models");
    }
    return urls;
  }

  if (ends_with(lower_base, "/v1")) {
    push_unique(base + "/models");
    const std::string base_without_v1 = base.substr(0, base.size() - 3);
    push_unique(base_without_v1 + "/models");
  } else {
    push_unique(base + "/models");
    push_unique(base + "/v1/models");
  }

  return urls;
}

bool fetch_remote_model_catalog(const std::string& api_base,
                                const std::string& api_key,
                                const std::string& proxy,
                                std::vector<std::string>* models,
                                std::string* used_url,
                                std::string* error) {
  if (models == nullptr) {
    if (error != nullptr) {
      *error = "invalid output pointer";
    }
    return false;
  }

  const auto urls = build_model_catalog_urls(api_base);
  if (urls.empty()) {
    if (error != nullptr) {
      *error = "api_base is empty";
    }
    return false;
  }
  std::ostringstream tried_urls;
  for (std::size_t i = 0; i < urls.size(); ++i) {
    if (i != 0) {
      tried_urls << ", ";
    }
    tried_urls << urls[i];
  }

  std::string last_error;
  std::map<std::string, std::string> headers;
  if (!trim(api_key).empty()) {
    headers["Authorization"] = "Bearer " + trim(api_key);
  }
  headers["Accept"] = "application/json";

  for (const auto& url : urls) {
    const auto response = HttpClient::get(url, headers, trim(proxy), 30);
    if (!response.error.empty()) {
      last_error = "request failed for " + url + ": " + response.error;
      continue;
    }
    if (response.status < 200 || response.status >= 300) {
      if (response.status == 404) {
        continue;
      }
      std::string body = response.body;
      if (body.size() > 300) {
        body = body.substr(0, 300) + "...";
      }
      last_error = "request failed for " + url + " with status " + std::to_string(response.status) + ": " + body;
      continue;
    }

    std::string parse_error;
    const auto payload = parse_json_or_error(response.body, &parse_error);
    if (!parse_error.empty() || payload.is_null()) {
      last_error = "invalid json from " + url + ": " + parse_error;
      continue;
    }

    auto names = extract_remote_model_names(payload);
    if (names.empty()) {
      last_error = "no models found in response from " + url;
      continue;
    }
    if (is_likely_minmax_base(api_base)) {
      enrich_minmax_models(&names);
    }
    if (is_likely_codex_base(api_base) && names.size() <= 2) {
      enrich_codex_models_by_probe(api_base, api_key, proxy, &names);
    }
    if (used_url != nullptr) {
      *used_url = url;
    }
    *models = std::move(names);
    return true;
  }

  if (error != nullptr) {
    if (last_error.empty()) {
      *error = "failed to fetch model catalog";
    } else {
      *error = last_error + " (tried: " + tried_urls.str() + ")";
    }
  }
  return false;
}

bool is_likely_codex_base(const std::string& api_base) {
  const auto lower = to_lower(normalize_api_base(api_base));
  return lower.find("/api/codex/codex") != std::string::npos;
}

bool is_likely_minmax_base(const std::string& api_base) {
  const auto lower = to_lower(normalize_api_base(api_base));
  return lower.find("api.minimax.io") != std::string::npos || lower.find("api.minimaxi.com") != std::string::npos;
}

void enrich_minmax_models(std::vector<std::string>* models) {
  if (models == nullptr) {
    return;
  }
  std::unordered_set<std::string> seen;
  for (const auto& model : *models) {
    seen.insert(model);
  }
  static const std::vector<std::string> kCandidates = {
      "MiniMax-M2.1",
      "MiniMax-M2.1-lightning",
      "MiniMax-M2.5",
      "MiniMax-M2.5-Lightning",
      "MiniMax-VL-01",
  };
  for (const auto& name : kCandidates) {
    if (seen.find(name) != seen.end()) {
      continue;
    }
    models->push_back(name);
    seen.insert(name);
  }
}

void append_known_codex_models(std::vector<std::string>* models) {
  if (models == nullptr) {
    return;
  }
  std::unordered_set<std::string> seen;
  for (const auto& model : *models) {
    seen.insert(model);
  }
  static const std::vector<std::string> kKnownCodexModels = {
      "gpt-5.3-codex",
      "gpt-5.1-codex",
      "gpt-5-codex",
      "gpt-5-mini-codex",
      "o4-mini",
      "o4-mini-high",
      "o3",
      "gpt-4o-mini",
  };
  for (const auto& name : kKnownCodexModels) {
    if (seen.find(name) != seen.end()) {
      continue;
    }
    models->push_back(name);
    seen.insert(name);
  }
}

std::string build_codex_responses_endpoint(std::string api_base) {
  api_base = normalize_api_base(api_base);
  auto lower = to_lower(api_base);
  const auto ends_with = [](const std::string& value, const std::string& suffix) -> bool {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
  };
  const auto trim_suffix = [&](const std::string& suffix) {
    if (ends_with(lower, suffix)) {
      api_base.resize(api_base.size() - suffix.size());
      while (!api_base.empty() && api_base.back() == '/') {
        api_base.pop_back();
      }
      lower = to_lower(api_base);
    }
  };

  trim_suffix("/v1/responses");
  trim_suffix("/responses");
  trim_suffix("/v1/chat/completions");
  trim_suffix("/chat/completions");

  if (lower.find("/api/codex/codex") != std::string::npos &&
      !(ends_with(lower, "/v1") || lower.find("/v1/") != std::string::npos)) {
    api_base += "/v1";
  }

  if (!ends_with(to_lower(api_base), "/responses")) {
    api_base += "/responses";
  }
  return api_base;
}

bool probe_responses_model(const std::string& endpoint,
                           const std::string& api_key,
                           const std::string& proxy,
                           const std::string& model_name) {
  if (trim(endpoint).empty() || trim(api_key).empty() || trim(model_name).empty()) {
    return false;
  }

  nlohmann::json body = nlohmann::json::object();
  body["model"] = model_name;
  body["input"] = nlohmann::json::array(
      {nlohmann::json{{"role", "user"},
                      {"content", nlohmann::json::array({nlohmann::json{{"type", "input_text"}, {"text", "ping"}}})}}});
  body["stream"] = false;
  body["store"] = false;
  body["max_output_tokens"] = 1;
  std::map<std::string, std::string> headers;
  headers["Authorization"] = "Bearer " + trim(api_key);
  headers["Content-Type"] = "application/json";

  const auto response = HttpClient::post_json(endpoint, body.dump(), headers, trim(proxy), 12);
  if (!response.error.empty()) {
    return false;
  }
  if (response.status < 200 || response.status >= 300) {
    return false;
  }

  std::string parse_error;
  const auto payload = parse_json_or_error(response.body, &parse_error);
  if (payload.is_null() || !parse_error.empty()) {
    return false;
  }
  if (payload.contains("error") && payload["error"].is_object() && !payload["error"].empty()) {
    return false;
  }
  return true;
}

void enrich_codex_models_by_probe(const std::string& api_base,
                                  const std::string& api_key,
                                  const std::string& proxy,
                                  std::vector<std::string>* models) {
  if (models == nullptr || trim(api_key).empty()) {
    return;
  }
  const std::string endpoint = build_codex_responses_endpoint(api_base);
  if (trim(endpoint).empty()) {
    return;
  }

  std::unordered_set<std::string> seen;
  for (const auto& model : *models) {
    seen.insert(model);
  }

  static const std::vector<std::string> candidates = {
      "gpt-5.3-codex",
      "gpt-5.1-codex",
      "gpt-5-codex",
      "gpt-5-mini-codex",
      "o4-mini-high",
      "o4-mini",
      "o3",
      "o1",
      "gpt-4o-mini",
      "claude-3-5-sonnet",
  };

  for (const auto& candidate : candidates) {
    if (seen.find(candidate) != seen.end()) {
      continue;
    }
    if (probe_responses_model(endpoint, api_key, proxy, candidate)) {
      models->push_back(candidate);
      seen.insert(candidate);
    }
  }
}

void push_gateway_chat_event(std::vector<GatewayChatEvent>* events,
                             std::mutex* events_mutex,
                             std::atomic_int64_t* next_event_id,
                             const std::string& kind,
                             const std::string& role,
                             const std::string& source,
                             const std::string& session_key,
                             const std::string& model_name,
                             const std::string& content) {
  if (events == nullptr || events_mutex == nullptr || next_event_id == nullptr) {
    return;
  }
  GatewayChatEvent event;
  event.id = next_event_id->fetch_add(1);
  event.time_ms = unix_ms_now();
  event.kind = trim(kind).empty() ? "message" : trim(kind);
  event.role = role;
  event.source = source;
  event.session_key = session_key;
  event.model_name = model_name;
  event.content = content;

  constexpr std::size_t kMaxEvents = 2000;
  std::lock_guard<std::mutex> lock(*events_mutex);
  events->push_back(std::move(event));
  if (events->size() > kMaxEvents) {
    events->erase(events->begin(),
                  events->begin() + static_cast<std::ptrdiff_t>(events->size() - kMaxEvents));
  }
}

std::vector<GatewayChatEvent> collect_gateway_chat_events_since(const std::vector<GatewayChatEvent>* events,
                                                                std::mutex* events_mutex,
                                                                std::int64_t since_id) {
  std::vector<GatewayChatEvent> out;
  if (events == nullptr || events_mutex == nullptr) {
    return out;
  }
  std::lock_guard<std::mutex> lock(*events_mutex);
  out.reserve(events->size());
  for (const auto& event : *events) {
    if (event.id > since_id) {
      out.push_back(event);
    }
  }
  return out;
}

void set_gateway_session_running(std::unordered_set<std::string>* running_sessions,
                                 std::mutex* running_sessions_mutex,
                                 const std::string& session_key,
                                 const bool running) {
  if (running_sessions == nullptr || running_sessions_mutex == nullptr) {
    return;
  }
  const auto normalized = trim(session_key);
  if (normalized.empty()) {
    return;
  }
  std::lock_guard<std::mutex> lock(*running_sessions_mutex);
  if (running) {
    running_sessions->insert(normalized);
  } else {
    running_sessions->erase(normalized);
  }
}

std::vector<std::string> collect_running_gateway_sessions(const std::unordered_set<std::string>* running_sessions,
                                                          std::mutex* running_sessions_mutex) {
  std::vector<std::string> out;
  if (running_sessions == nullptr || running_sessions_mutex == nullptr) {
    return out;
  }
  std::lock_guard<std::mutex> lock(*running_sessions_mutex);
  out.reserve(running_sessions->size());
  for (const auto& key : *running_sessions) {
    out.push_back(key);
  }
  std::sort(out.begin(), out.end());
  return out;
}

FeishuRuntimeConfig read_feishu_config(const Config& config) {
  FeishuRuntimeConfig feishu;
  const auto channels = config.raw.value("channels", nlohmann::json::object());
  if (!channels.is_object() || !channels.contains("feishu") || !channels["feishu"].is_object()) {
    return feishu;
  }
  const auto section = channels["feishu"];
  feishu.enabled = false;
  feishu.app_id.clear();
  feishu.app_secret.clear();
  feishu.default_chat_id = trim(json_string_or_empty(section, "default_chat_id"));
  if (section.contains("allow_from") && section["allow_from"].is_array()) {
    for (const auto& item : section["allow_from"]) {
      if (!item.is_string()) {
        continue;
      }
      const auto value = trim(item.get<std::string>());
      if (!value.empty()) {
        feishu.allow_from.insert(value);
      }
    }
  }
  const std::string mode = trim(json_string_or_empty(section, "mode"));
  if (!mode.empty()) {
    feishu.mode = mode;
  }
  const std::string api_base = trim(json_string_or_empty(section, "api_base"));
  if (!api_base.empty()) {
    feishu.api_base = api_base;
  }
  feishu.proxy = trim(json_string_or_empty(section, "proxy"));
  if (section.contains("reconnect_count") && section["reconnect_count"].is_number_integer()) {
    feishu.reconnect_count = section["reconnect_count"].get<int>();
  }
  if (section.contains("reconnect_interval_seconds") && section["reconnect_interval_seconds"].is_number_integer()) {
    feishu.reconnect_interval_seconds = std::max(5, section["reconnect_interval_seconds"].get<int>());
  }
  if (section.contains("reconnect_nonce_seconds") && section["reconnect_nonce_seconds"].is_number_integer()) {
    feishu.reconnect_nonce_seconds = std::max(0, section["reconnect_nonce_seconds"].get<int>());
  }
  if (section.contains("ping_interval_seconds") && section["ping_interval_seconds"].is_number_integer()) {
    feishu.ping_interval_seconds = std::max(10, section["ping_interval_seconds"].get<int>());
  }
  return feishu;
}

std::string feishu_extract_sender_id(const nlohmann::json& event_sender) {
  if (!event_sender.is_object()) {
    return "";
  }
  const auto sender_id = event_sender.value("sender_id", nlohmann::json::object());
  for (const auto* key : {"user_id", "open_id", "union_id"}) {
    if (sender_id.contains(key) && sender_id[key].is_string() && !sender_id[key].get<std::string>().empty()) {
      return sender_id[key].get<std::string>();
    }
  }
  return "";
}

std::string feishu_extract_sender_type(const nlohmann::json& event_sender) {
  if (!event_sender.is_object()) {
    return "";
  }
  if (event_sender.contains("sender_type") && event_sender["sender_type"].is_string()) {
    return to_lower(trim(event_sender["sender_type"].get<std::string>()));
  }
  return "";
}

std::string feishu_session_key_from_chat_id(const std::string& chat_id) {
  const auto normalized = trim(chat_id);
  if (normalized.empty()) {
    return "feishu:unknown";
  }
  return "feishu:" + normalized;
}

std::string feishu_chat_id_from_session_key(const std::string& session_key) {
  const auto normalized = trim(session_key);
  const std::string prefix = "feishu:";
  if (normalized.size() <= prefix.size() || normalized.rfind(prefix, 0) != 0) {
    return "";
  }
  const auto remain = normalized.substr(prefix.size());
  const auto pos = remain.find(':');
  if (pos == std::string::npos) {
    return trim(remain);
  }
  return trim(remain.substr(0, pos));
}

std::string feishu_extract_text_content(const std::string& raw_content) {
  if (raw_content.empty()) {
    return "";
  }
  std::string parse_error;
  const auto content_json = parse_json_or_error(raw_content, &parse_error);
  if (!parse_error.empty() || !content_json.is_object()) {
    return raw_content;
  }
  if (content_json.contains("text") && content_json["text"].is_string()) {
    return content_json["text"].get<std::string>();
  }
  return raw_content;
}

bool feishu_is_duplicate_event(std::unordered_map<std::string, std::int64_t>* seen_events,
                               std::mutex* seen_events_mutex,
                               const std::string& event_id) {
  if (event_id.empty() || seen_events == nullptr || seen_events_mutex == nullptr) {
    return false;
  }
  const std::int64_t now = unix_ms_now();
  constexpr std::int64_t keep_ms = 15 * 60 * 1000;
  std::lock_guard<std::mutex> lock(*seen_events_mutex);
  for (auto it = seen_events->begin(); it != seen_events->end();) {
    if (now - it->second > keep_ms) {
      it = seen_events->erase(it);
    } else {
      ++it;
    }
  }
  if (seen_events->find(event_id) != seen_events->end()) {
    return true;
  }
  (*seen_events)[event_id] = now;
  return false;
}

std::string feishu_header_value(const std::vector<FeishuWsHeader>& headers, const std::string& key) {
  for (const auto& header : headers) {
    if (header.key == key) {
      return header.value;
    }
  }
  return "";
}

int feishu_header_int(const std::vector<FeishuWsHeader>& headers, const std::string& key, int fallback = 0) {
  const auto value = feishu_header_value(headers, key);
  if (value.empty()) {
    return fallback;
  }
  try {
    return std::stoi(value);
  } catch (const std::exception&) {
    return fallback;
  }
}

void feishu_set_header(std::vector<FeishuWsHeader>* headers, const std::string& key, const std::string& value) {
  if (headers == nullptr) {
    return;
  }
  for (auto& header : *headers) {
    if (header.key == key) {
      header.value = value;
      return;
    }
  }
  headers->push_back(FeishuWsHeader{key, value});
}

bool protobuf_read_varint(const std::string& input, std::size_t* pos, std::uint64_t* out) {
  if (pos == nullptr || out == nullptr) {
    return false;
  }
  std::uint64_t value = 0;
  int shift = 0;
  while (*pos < input.size() && shift <= 63) {
    const auto byte = static_cast<std::uint8_t>(input[*pos]);
    ++(*pos);
    value |= static_cast<std::uint64_t>(byte & 0x7f) << shift;
    if ((byte & 0x80) == 0) {
      *out = value;
      return true;
    }
    shift += 7;
  }
  return false;
}

bool protobuf_skip_field(const std::string& input, std::size_t* pos, std::uint64_t wire_type) {
  if (pos == nullptr) {
    return false;
  }
  switch (wire_type) {
    case 0: {
      std::uint64_t ignored = 0;
      return protobuf_read_varint(input, pos, &ignored);
    }
    case 1: {
      if (*pos + 8 > input.size()) {
        return false;
      }
      *pos += 8;
      return true;
    }
    case 2: {
      std::uint64_t length = 0;
      if (!protobuf_read_varint(input, pos, &length)) {
        return false;
      }
      if (*pos + length > input.size()) {
        return false;
      }
      *pos += static_cast<std::size_t>(length);
      return true;
    }
    case 5: {
      if (*pos + 4 > input.size()) {
        return false;
      }
      *pos += 4;
      return true;
    }
    default:
      return false;
  }
}

void protobuf_write_varint(std::string* out, std::uint64_t value) {
  if (out == nullptr) {
    return;
  }
  while (value >= 0x80) {
    out->push_back(static_cast<char>((value & 0x7f) | 0x80));
    value >>= 7;
  }
  out->push_back(static_cast<char>(value & 0x7f));
}

void protobuf_write_key(std::string* out, std::uint32_t field_number, std::uint8_t wire_type) {
  const std::uint64_t key = (static_cast<std::uint64_t>(field_number) << 3) | wire_type;
  protobuf_write_varint(out, key);
}

void protobuf_write_varint_field(std::string* out, std::uint32_t field_number, std::uint64_t value) {
  protobuf_write_key(out, field_number, 0);
  protobuf_write_varint(out, value);
}

void protobuf_write_bytes_field(std::string* out, std::uint32_t field_number, const std::string& value) {
  protobuf_write_key(out, field_number, 2);
  protobuf_write_varint(out, static_cast<std::uint64_t>(value.size()));
  out->append(value);
}

bool decode_feishu_ws_header(const std::string& input, FeishuWsHeader* header) {
  if (header == nullptr) {
    return false;
  }
  FeishuWsHeader parsed;
  std::size_t pos = 0;
  while (pos < input.size()) {
    std::uint64_t key = 0;
    if (!protobuf_read_varint(input, &pos, &key)) {
      return false;
    }
    const auto field = static_cast<std::uint32_t>(key >> 3);
    const auto wire_type = key & 0x7;
    if (field == 1 && wire_type == 2) {
      std::uint64_t length = 0;
      if (!protobuf_read_varint(input, &pos, &length) || pos + length > input.size()) {
        return false;
      }
      parsed.key = input.substr(pos, static_cast<std::size_t>(length));
      pos += static_cast<std::size_t>(length);
      continue;
    }
    if (field == 2 && wire_type == 2) {
      std::uint64_t length = 0;
      if (!protobuf_read_varint(input, &pos, &length) || pos + length > input.size()) {
        return false;
      }
      parsed.value = input.substr(pos, static_cast<std::size_t>(length));
      pos += static_cast<std::size_t>(length);
      continue;
    }
    if (!protobuf_skip_field(input, &pos, wire_type)) {
      return false;
    }
  }
  *header = parsed;
  return true;
}

std::string encode_feishu_ws_header(const FeishuWsHeader& header) {
  std::string out;
  protobuf_write_bytes_field(&out, 1, header.key);
  protobuf_write_bytes_field(&out, 2, header.value);
  return out;
}

bool decode_feishu_ws_frame(const std::string& input, FeishuWsFrame* frame) {
  if (frame == nullptr) {
    return false;
  }
  FeishuWsFrame parsed;
  std::size_t pos = 0;
  while (pos < input.size()) {
    std::uint64_t key = 0;
    if (!protobuf_read_varint(input, &pos, &key)) {
      return false;
    }
    const auto field = static_cast<std::uint32_t>(key >> 3);
    const auto wire_type = key & 0x7;

    if ((field == 1 || field == 2 || field == 3 || field == 4) && wire_type == 0) {
      std::uint64_t value = 0;
      if (!protobuf_read_varint(input, &pos, &value)) {
        return false;
      }
      if (field == 1) {
        parsed.seq_id = value;
      } else if (field == 2) {
        parsed.log_id = value;
      } else if (field == 3) {
        parsed.service = static_cast<std::int32_t>(value);
      } else {
        parsed.method = static_cast<std::int32_t>(value);
      }
      continue;
    }

    if (wire_type == 2) {
      std::uint64_t length = 0;
      if (!protobuf_read_varint(input, &pos, &length) || pos + length > input.size()) {
        return false;
      }
      const auto bytes = input.substr(pos, static_cast<std::size_t>(length));
      pos += static_cast<std::size_t>(length);
      if (field == 5) {
        FeishuWsHeader header;
        if (!decode_feishu_ws_header(bytes, &header)) {
          return false;
        }
        parsed.headers.push_back(std::move(header));
      } else if (field == 6) {
        parsed.payload_encoding = bytes;
      } else if (field == 7) {
        parsed.payload_type = bytes;
      } else if (field == 8) {
        parsed.payload = bytes;
      } else if (field == 9) {
        parsed.log_id_new = bytes;
      }
      continue;
    }

    if (!protobuf_skip_field(input, &pos, wire_type)) {
      return false;
    }
  }

  *frame = std::move(parsed);
  return true;
}

std::string encode_feishu_ws_frame(const FeishuWsFrame& frame) {
  std::string out;
  protobuf_write_varint_field(&out, 1, frame.seq_id);
  protobuf_write_varint_field(&out, 2, frame.log_id);
  protobuf_write_varint_field(&out, 3, static_cast<std::uint64_t>(static_cast<std::uint32_t>(frame.service)));
  protobuf_write_varint_field(&out, 4, static_cast<std::uint64_t>(static_cast<std::uint32_t>(frame.method)));
  for (const auto& header : frame.headers) {
    const auto header_bytes = encode_feishu_ws_header(header);
    protobuf_write_bytes_field(&out, 5, header_bytes);
  }
  if (!frame.payload_encoding.empty()) {
    protobuf_write_bytes_field(&out, 6, frame.payload_encoding);
  }
  if (!frame.payload_type.empty()) {
    protobuf_write_bytes_field(&out, 7, frame.payload_type);
  }
  if (!frame.payload.empty()) {
    protobuf_write_bytes_field(&out, 8, frame.payload);
  }
  if (!frame.log_id_new.empty()) {
    protobuf_write_bytes_field(&out, 9, frame.log_id_new);
  }
  return out;
}

struct FeishuPayloadParts {
  std::vector<std::string> parts;
  std::int64_t updated_at_ms = 0;
};

std::optional<std::string> feishu_merge_payload_parts(std::unordered_map<std::string, FeishuPayloadParts>* cache,
                                                      std::mutex* cache_mutex,
                                                      const std::string& message_id,
                                                      int sum,
                                                      int seq,
                                                      const std::string& payload) {
  if (cache == nullptr || cache_mutex == nullptr) {
    return std::nullopt;
  }
  if (sum <= 1) {
    return payload;
  }
  if (message_id.empty() || seq < 0 || seq >= sum) {
    return std::nullopt;
  }

  constexpr std::int64_t keep_ms = 5 * 1000;
  const auto now = unix_ms_now();
  std::lock_guard<std::mutex> lock(*cache_mutex);
  for (auto it = cache->begin(); it != cache->end();) {
    if (now - it->second.updated_at_ms > keep_ms) {
      it = cache->erase(it);
    } else {
      ++it;
    }
  }

  auto& slot = (*cache)[message_id];
  if (slot.parts.empty() || static_cast<int>(slot.parts.size()) != sum) {
    slot.parts.assign(static_cast<std::size_t>(sum), "");
  }
  slot.updated_at_ms = now;
  slot.parts[static_cast<std::size_t>(seq)] = payload;

  std::size_t total_size = 0;
  for (const auto& part : slot.parts) {
    if (part.empty()) {
      return std::nullopt;
    }
    total_size += part.size();
  }

  std::string merged;
  merged.reserve(total_size);
  for (const auto& part : slot.parts) {
    merged += part;
  }
  cache->erase(message_id);
  return merged;
}

std::optional<std::string> parse_url_query_param(const std::string& url, const std::string& key) {
  const auto q_pos = url.find('?');
  if (q_pos == std::string::npos) {
    return std::nullopt;
  }
  const auto query = url.substr(q_pos + 1);
  std::size_t pos = 0;
  while (pos < query.size()) {
    auto amp = query.find('&', pos);
    if (amp == std::string::npos) {
      amp = query.size();
    }
    const auto pair = query.substr(pos, amp - pos);
    const auto eq = pair.find('=');
    const auto name = pair.substr(0, eq);
    const auto value = eq == std::string::npos ? std::string("") : pair.substr(eq + 1);
    if (name == key) {
      return value;
    }
    pos = amp + 1;
  }
  return std::nullopt;
}

std::string feishu_response_payload_json(int code) {
  return nlohmann::json{
      {"code", code},
      {"headers", nlohmann::json::object()},
      {"data", nullptr},
  }
      .dump();
}

std::optional<std::string> feishu_fetch_ws_url(const FeishuRuntimeConfig& feishu,
                                               FeishuWsEndpointData* endpoint_data,
                                               std::string* error) {
  const std::string endpoint = feishu.api_base + "/callback/ws/endpoint";
  const nlohmann::json request{
      {"AppID", feishu.app_id},
      {"AppSecret", feishu.app_secret},
  };

  const auto response = HttpClient::post_json(endpoint, dump_json_lossy(request), {{"locale", "zh"}}, trim(feishu.proxy), 20);
  if (!response.error.empty()) {
    if (error != nullptr) {
      *error = "request ws endpoint failed: " + response.error;
    }
    return std::nullopt;
  }
  if (response.status < 200 || response.status >= 300) {
    if (error != nullptr) {
      *error = "request ws endpoint failed, status " + std::to_string(response.status) + ": " + response.body;
    }
    return std::nullopt;
  }

  std::string parse_error;
  const auto body = parse_json_or_error(response.body, &parse_error);
  if (body.is_null() || !parse_error.empty()) {
    if (error != nullptr) {
      *error = "parse ws endpoint response failed: " + parse_error;
    }
    return std::nullopt;
  }

  const int code = body.value("code", -1);
  if (code != 0) {
    if (error != nullptr) {
      *error = "ws endpoint api error: code=" + std::to_string(code) + " msg=" + body.value("msg", std::string(""));
    }
    return std::nullopt;
  }
  const auto data = body.value("data", nlohmann::json::object());
  const std::string url = data.value("URL", std::string(""));
  if (url.empty()) {
    if (error != nullptr) {
      *error = "ws endpoint response missing URL";
    }
    return std::nullopt;
  }

  if (endpoint_data != nullptr && data.contains("ClientConfig") && data["ClientConfig"].is_object()) {
    const auto cfg = data["ClientConfig"];
    endpoint_data->url = url;
    endpoint_data->reconnect_count = cfg.value("ReconnectCount", feishu.reconnect_count);
    endpoint_data->reconnect_interval_seconds =
        std::max(5, cfg.value("ReconnectInterval", feishu.reconnect_interval_seconds));
    endpoint_data->reconnect_nonce_seconds =
        std::max(0, cfg.value("ReconnectNonce", feishu.reconnect_nonce_seconds));
    endpoint_data->ping_interval_seconds = std::max(10, cfg.value("PingInterval", feishu.ping_interval_seconds));
  } else if (endpoint_data != nullptr) {
    endpoint_data->url = url;
    endpoint_data->reconnect_count = feishu.reconnect_count;
    endpoint_data->reconnect_interval_seconds = feishu.reconnect_interval_seconds;
    endpoint_data->reconnect_nonce_seconds = feishu.reconnect_nonce_seconds;
    endpoint_data->ping_interval_seconds = feishu.ping_interval_seconds;
  }
  return url;
}

std::string feishu_truncate_text(std::string text) {
  constexpr std::size_t kMaxChars = 3800;
  if (text.size() <= kMaxChars) {
    return text;
  }
  text.resize(kMaxChars);
  text += "\n...(truncated)";
  return text;
}

bool feishu_fetch_tenant_access_token(const FeishuRuntimeConfig& feishu,
                                      std::string* token,
                                      std::string* error) {
  if (token == nullptr) {
    if (error != nullptr) {
      *error = "invalid token output pointer";
    }
    return false;
  }
  if (trim(feishu.app_id).empty() || trim(feishu.app_secret).empty()) {
    if (error != nullptr) {
      *error = "feishu app_id or app_secret is empty";
    }
    return false;
  }

  const std::string endpoint = feishu.api_base + "/open-apis/auth/v3/tenant_access_token/internal";
  const nlohmann::json request{
      {"app_id", feishu.app_id},
      {"app_secret", feishu.app_secret},
  };
  const auto response = HttpClient::post_json(endpoint, dump_json_lossy(request), {}, trim(feishu.proxy), 20);
  if (!response.error.empty()) {
    if (error != nullptr) {
      *error = "request tenant_access_token failed: " + response.error;
    }
    return false;
  }
  if (response.status < 200 || response.status >= 300) {
    if (error != nullptr) {
      *error = "request tenant_access_token failed, status " + std::to_string(response.status) + ": " + response.body;
    }
    return false;
  }

  std::string parse_error;
  const auto payload = parse_json_or_error(response.body, &parse_error);
  if (payload.is_null() || !parse_error.empty()) {
    if (error != nullptr) {
      *error = "parse tenant_access_token response failed: " + parse_error;
    }
    return false;
  }
  if (payload.value("code", -1) != 0) {
    if (error != nullptr) {
      *error = "tenant_access_token api error: code=" + std::to_string(payload.value("code", -1)) + " msg=" +
               payload.value("msg", std::string(""));
    }
    return false;
  }
  if (!payload.contains("tenant_access_token") || !payload["tenant_access_token"].is_string()) {
    if (error != nullptr) {
      *error = "tenant_access_token missing in response";
    }
    return false;
  }
  *token = payload["tenant_access_token"].get<std::string>();
  return true;
}

bool feishu_send_text_message(const FeishuRuntimeConfig& feishu,
                              const std::string& chat_id,
                              const std::string& text,
                              std::string* error) {
  if (trim(chat_id).empty()) {
    if (error != nullptr) {
      *error = "chat_id is empty";
    }
    return false;
  }

  std::string token;
  if (!feishu_fetch_tenant_access_token(feishu, &token, error)) {
    return false;
  }

  const std::string endpoint = feishu.api_base + "/open-apis/im/v1/messages?receive_id_type=chat_id";
  const nlohmann::json content_json{{"text", feishu_truncate_text(sanitize_utf8_lossy(text))}};
  const nlohmann::json request{
      {"receive_id", chat_id},
      {"msg_type", "text"},
      {"content", dump_json_lossy(content_json)},
  };
  std::map<std::string, std::string> headers{
      {"Authorization", "Bearer " + token},
  };
  auto response = HttpClient::post_json(endpoint, dump_json_lossy(request), headers, trim(feishu.proxy), 30);
  if (!response.error.empty()) {
    const auto lowered = to_lower(response.error);
    if (lowered.find("timeout") != std::string::npos || lowered.find("timed out") != std::string::npos) {
      response = HttpClient::post_json(endpoint, dump_json_lossy(request), headers, trim(feishu.proxy), 45);
    }
  }
  if (!response.error.empty()) {
    if (error != nullptr) {
      *error = "send feishu message failed: " + response.error;
    }
    return false;
  }
  if (response.status < 200 || response.status >= 300) {
    if (error != nullptr) {
      *error = "send feishu message failed, status " + std::to_string(response.status) + ": " + response.body;
    }
    return false;
  }

  std::string parse_error;
  const auto payload = parse_json_or_error(response.body, &parse_error);
  if (payload.is_null() || !parse_error.empty()) {
    if (error != nullptr) {
      *error = "parse feishu send response failed: " + parse_error;
    }
    return false;
  }
  if (payload.value("code", -1) != 0) {
    if (error != nullptr) {
      *error = "feishu send api error: code=" + std::to_string(payload.value("code", -1)) + " msg=" +
               payload.value("msg", std::string(""));
    }
    return false;
  }
  return true;
}

void handle_feishu_event_payload(const FeishuRuntimeConfig& feishu_config,
                                 AgentRuntime* agent,
                                 std::mutex* agent_mutex,
                                 std::string* active_console_model,
                                 std::mutex* active_console_model_mutex,
                                 std::vector<GatewayChatEvent>* chat_events,
                                 std::mutex* chat_events_mutex,
                                 std::atomic_int64_t* next_chat_event_id,
                                 std::unordered_map<std::string, std::int64_t>* seen_events,
                                 std::mutex* seen_events_mutex,
                                 std::unordered_map<std::string, bool>* cancelled_sessions,
                                 std::mutex* cancelled_sessions_mutex,
                                 std::unordered_set<std::string>* running_sessions,
                                 std::mutex* running_sessions_mutex,
                                 std::string* latest_feishu_chat_id,
                                 std::mutex* latest_feishu_chat_id_mutex,
                                 const std::string& payload) {
  if (agent == nullptr || agent_mutex == nullptr || active_console_model == nullptr || active_console_model_mutex == nullptr ||
      chat_events == nullptr || chat_events_mutex == nullptr || next_chat_event_id == nullptr || cancelled_sessions == nullptr ||
      cancelled_sessions_mutex == nullptr || running_sessions == nullptr || running_sessions_mutex == nullptr ||
      latest_feishu_chat_id == nullptr || latest_feishu_chat_id_mutex == nullptr) {
    return;
  }

  std::string parse_error;
  const auto body = parse_json_or_error(payload, &parse_error);
  if (body.is_null() || !parse_error.empty()) {
    std::cerr << "[feishu] invalid event payload: " << parse_error << "\n";
    return;
  }

  const auto header = body.value("header", nlohmann::json::object());
  const std::string header_app_id = header.value("app_id", std::string(""));
  if (!header_app_id.empty() && !feishu_config.app_id.empty() && header_app_id != feishu_config.app_id) {
    std::cerr << "[feishu] warning: event app_id mismatch (header=" << header_app_id
              << ", config=" << feishu_config.app_id << "), continue processing\n";
  }

  const std::string event_type = header.value("event_type", std::string(""));
  if (event_type != "im.message.receive_v1") {
    return;
  }

  const auto event = body.value("event", nlohmann::json::object());
  const auto message = event.value("message", nlohmann::json::object());
  const auto sender = event.value("sender", nlohmann::json::object());

  const std::string chat_id = message.value("chat_id", std::string(""));
  const std::string message_type = message.value("message_type", std::string(""));
  const std::string message_content_raw = message.value("content", std::string(""));
  if (chat_id.empty() || message_type != "text") {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(*latest_feishu_chat_id_mutex);
    *latest_feishu_chat_id = chat_id;
  }

  const std::string content = trim(sanitize_utf8_lossy(feishu_extract_text_content(message_content_raw)));
  if (content.empty()) {
    return;
  }
  if (content.rfind("Web user:", 0) == 0) {
    std::cout << "[feishu] ignore mirrored web message to avoid echo loop: chat_id=" << chat_id << "\n";
    return;
  }

  const std::string sender_id = feishu_extract_sender_id(sender);
  const std::string sender_type = feishu_extract_sender_type(sender);
  if (sender_type == "app" || sender_type == "bot") {
    std::cout << "[feishu] ignore bot/app message: chat_id=" << chat_id << "\n";
    return;
  }
  if (!feishu_config.allow_from.empty() && feishu_config.allow_from.find(sender_id) == feishu_config.allow_from.end()) {
    std::cout << "[feishu] ignore message from sender not in allow_from: chat_id=" << chat_id
              << " sender=" << (sender_id.empty() ? "unknown" : sender_id) << "\n";
    return;
  }
  const std::string event_id = message.value("message_id", header.value("event_id", std::string("")));
  if (feishu_is_duplicate_event(seen_events, seen_events_mutex, event_id)) {
    return;
  }
  std::cout << "[feishu] inbound text message: chat_id=" << chat_id
            << " sender=" << (sender_id.empty() ? "unknown" : sender_id) << "\n";

  const std::string session_key = feishu_session_key_from_chat_id(chat_id);
  std::string runtime_model_name;
  {
    std::lock_guard<std::mutex> lock(*active_console_model_mutex);
    runtime_model_name = trim(*active_console_model);
  }
  const auto model_override =
      runtime_model_name.empty() ? std::nullopt : std::optional<std::string>(runtime_model_name);

  {
    std::lock_guard<std::mutex> lock(*cancelled_sessions_mutex);
    (*cancelled_sessions)[session_key] = false;
  }
  set_gateway_session_running(running_sessions, running_sessions_mutex, session_key, true);

  push_gateway_chat_event(chat_events,
                          chat_events_mutex,
                          next_chat_event_id,
                          "message",
                          "user",
                          "feishu",
                          session_key,
                          model_override.value_or(""),
                          content);

  push_gateway_chat_event(chat_events,
                          chat_events_mutex,
                          next_chat_event_id,
                          "task_start",
                          "system",
                          "feishu",
                          session_key,
                          model_override.value_or(""),
                          "");

  std::thread([agent,
               agent_mutex,
               feishu_config,
               chat_id,
               chat_events,
               chat_events_mutex,
               next_chat_event_id,
               content,
               session_key,
               model_override,
               cancelled_sessions,
               cancelled_sessions_mutex,
               running_sessions,
               running_sessions_mutex]() {
    AgentResponse response;
    try {
      std::lock_guard<std::mutex> lock(*agent_mutex);
      const auto is_cancelled = [cancelled_sessions, cancelled_sessions_mutex, session_key]() -> bool {
        std::lock_guard<std::mutex> lock(*cancelled_sessions_mutex);
        const auto it = cancelled_sessions->find(session_key);
        if (it == cancelled_sessions->end()) {
          return false;
        }
        return it->second;
      };
      response = agent->process_direct(content, session_key, model_override, false, is_cancelled);
    } catch (const std::exception& ex) {
      response.error = std::string("internal error: ") + ex.what();
    } catch (...) {
      response.error = "internal error: unknown exception";
    }
    std::string reply = response.ok() ? response.content : ("Error: " + response.error);
    reply = trim(reply);
    const std::string usage_summary = trim(response.usage_summary);
    if (reply.empty()) {
      reply = "Done.";
    }
    std::string feishu_reply = reply;
    if (!usage_summary.empty()) {
      feishu_reply += "\n\n" + usage_summary;
    }

    push_gateway_chat_event(chat_events,
                            chat_events_mutex,
                            next_chat_event_id,
                            "message",
                            "assistant",
                            "gateway",
                            session_key,
                            model_override.value_or(""),
                            reply);
    if (!usage_summary.empty()) {
      push_gateway_chat_event(chat_events,
                              chat_events_mutex,
                              next_chat_event_id,
                              "message",
                              "system",
                              "runtime",
                              session_key,
                              model_override.value_or(""),
                              usage_summary);
    }

    std::string send_error;
    if (!feishu_send_text_message(feishu_config, chat_id, feishu_reply, &send_error)) {
      std::cerr << "[feishu] send reply failed: " << send_error << "\n";
    }
    set_gateway_session_running(running_sessions, running_sessions_mutex, session_key, false);
    push_gateway_chat_event(chat_events,
                            chat_events_mutex,
                            next_chat_event_id,
                            "task_end",
                            "system",
                            "feishu",
                            session_key,
                            model_override.value_or(""),
                            "");
  }).detach();
}

bool feishu_run_long_connection_session(const std::string& ws_url,
                                        const FeishuRuntimeConfig& feishu_config,
                                        const FeishuWsEndpointData& endpoint_data,
                                        AgentRuntime* agent,
                                        std::mutex* agent_mutex,
                                        std::string* active_console_model,
                                        std::mutex* active_console_model_mutex,
                                        std::vector<GatewayChatEvent>* chat_events,
                                        std::mutex* chat_events_mutex,
                                        std::atomic_int64_t* next_chat_event_id,
                                        std::unordered_map<std::string, std::int64_t>* seen_events,
                                        std::mutex* seen_events_mutex,
                                        std::unordered_map<std::string, bool>* cancelled_sessions,
                                        std::mutex* cancelled_sessions_mutex,
                                        std::unordered_set<std::string>* running_sessions,
                                        std::mutex* running_sessions_mutex,
                                        std::string* latest_feishu_chat_id,
                                        std::mutex* latest_feishu_chat_id_mutex,
                                        std::string* error) {
  httplib::ws::WebSocketClient client(ws_url);
  if (!client.is_valid()) {
    if (error != nullptr) {
      *error = "invalid websocket url from feishu endpoint";
    }
    return false;
  }

  // Keep a long timeout: cpp-httplib treats websocket read timeout as connection failure.
  // If timeout is too short (e.g. 1s), connection will be closed repeatedly.
  client.set_read_timeout(300, 0);
  client.set_write_timeout(10, 0);
  if (!client.connect()) {
    if (error != nullptr) {
      *error = "failed to connect feishu websocket endpoint";
    }
    return false;
  }
  std::cout << "[feishu] websocket connected\n";

  int service_id = 0;
  if (const auto parsed = parse_url_query_param(ws_url, "service_id"); parsed.has_value()) {
    try {
      service_id = std::stoi(parsed.value());
    } catch (const std::exception&) {
      service_id = 0;
    }
  }

  std::atomic_int ping_interval_seconds(std::max(10, endpoint_data.ping_interval_seconds));
  std::mutex send_mutex;
  auto send_binary_frame = [&](const FeishuWsFrame& frame) -> bool {
    const auto binary = encode_feishu_ws_frame(frame);
    std::lock_guard<std::mutex> lock(send_mutex);
    return client.send(binary.data(), binary.size());
  };

  std::atomic_bool session_stop{false};
  std::thread stop_watcher_thread([&]() {
    while (!session_stop.load()) {
      if (g_stop_requested.load()) {
        std::lock_guard<std::mutex> lock(send_mutex);
        client.close();
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  });

  std::thread ping_thread([&]() {
    while (!session_stop.load() && !g_stop_requested.load()) {
      const int delay_seconds = std::max(10, ping_interval_seconds.load());
      for (int i = 0; i < delay_seconds; ++i) {
        if (session_stop.load() || g_stop_requested.load()) {
          break;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
      }
      if (session_stop.load() || g_stop_requested.load()) {
        break;
      }
      FeishuWsFrame ping;
      ping.service = service_id;
      ping.method = 0;
      ping.headers.push_back({"type", "ping"});
      if (!send_binary_frame(ping)) {
        break;
      }
    }
  });

  std::unordered_map<std::string, FeishuPayloadParts> payload_parts_cache;
  std::mutex payload_parts_cache_mutex;

  while (!g_stop_requested.load()) {
    std::string payload_binary;
    const auto read_result = client.read(payload_binary);
    if (read_result == httplib::ws::Fail) {
      if (g_stop_requested.load()) {
        break;
      }
      if (client.is_open()) {
        continue;
      }
      if (error != nullptr) {
        *error = "feishu websocket connection closed";
      }
      break;
    }
    if (read_result != httplib::ws::Binary) {
      continue;
    }

    FeishuWsFrame frame;
    if (!decode_feishu_ws_frame(payload_binary, &frame)) {
      continue;
    }

    if (frame.method == 0) {
      if (feishu_header_value(frame.headers, "type") == "pong" && !frame.payload.empty()) {
        std::string parse_error;
        const auto conf = parse_json_or_error(frame.payload, &parse_error);
        if (parse_error.empty() && conf.is_object()) {
          const int ping_interval = conf.value("PingInterval", ping_interval_seconds.load());
          ping_interval_seconds.store(std::max(10, ping_interval));
        }
      }
      continue;
    }
    if (frame.method != 1) {
      continue;
    }

    const auto frame_type = feishu_header_value(frame.headers, "type");
    const auto message_id = feishu_header_value(frame.headers, "message_id");
    const int sum = feishu_header_int(frame.headers, "sum", 1);
    const int seq = feishu_header_int(frame.headers, "seq", 0);
    const auto merged_payload =
        feishu_merge_payload_parts(&payload_parts_cache, &payload_parts_cache_mutex, message_id, sum, seq, frame.payload);
    if (!merged_payload.has_value()) {
      continue;
    }

    const auto start_ms = unix_ms_now();
    int response_code = 200;
    if (frame_type == "event") {
      handle_feishu_event_payload(feishu_config,
                                  agent,
                                  agent_mutex,
                                  active_console_model,
                                  active_console_model_mutex,
                                  chat_events,
                                  chat_events_mutex,
                                  next_chat_event_id,
                                  seen_events,
                                  seen_events_mutex,
                                  cancelled_sessions,
                                  cancelled_sessions_mutex,
                                  running_sessions,
                                  running_sessions_mutex,
                                  latest_feishu_chat_id,
                                  latest_feishu_chat_id_mutex,
                                  merged_payload.value());
    }
    const auto end_ms = unix_ms_now();
    feishu_set_header(&frame.headers, "biz_rt", std::to_string(std::max<std::int64_t>(0, end_ms - start_ms)));
    frame.payload = feishu_response_payload_json(response_code);
    if (!send_binary_frame(frame)) {
      if (error != nullptr) {
        *error = "failed to send response frame to feishu";
      }
      break;
    }
  }

  session_stop.store(true);
  {
    std::lock_guard<std::mutex> lock(send_mutex);
    client.close();
  }
  if (ping_thread.joinable()) {
    ping_thread.join();
  }
  if (stop_watcher_thread.joinable()) {
    stop_watcher_thread.join();
  }
  return false;
}

void feishu_long_connection_loop(Config* config,
                                 AgentRuntime* agent,
                                 std::mutex* agent_mutex,
                                 std::string* active_console_model,
                                 std::mutex* active_console_model_mutex,
                                 std::vector<GatewayChatEvent>* chat_events,
                                 std::mutex* chat_events_mutex,
                                 std::atomic_int64_t* next_chat_event_id,
                                 std::unordered_map<std::string, std::int64_t>* seen_events,
                                 std::mutex* seen_events_mutex,
                                 std::unordered_map<std::string, bool>* cancelled_sessions,
                                 std::mutex* cancelled_sessions_mutex,
                                 std::unordered_set<std::string>* running_sessions,
                                 std::mutex* running_sessions_mutex,
                                 std::string* latest_feishu_chat_id,
                                 std::mutex* latest_feishu_chat_id_mutex) {
  if (config == nullptr || agent == nullptr || agent_mutex == nullptr || active_console_model == nullptr ||
      active_console_model_mutex == nullptr || chat_events == nullptr || chat_events_mutex == nullptr ||
      next_chat_event_id == nullptr || seen_events == nullptr || seen_events_mutex == nullptr ||
      cancelled_sessions == nullptr || cancelled_sessions_mutex == nullptr || running_sessions == nullptr ||
      running_sessions_mutex == nullptr || latest_feishu_chat_id == nullptr || latest_feishu_chat_id_mutex == nullptr) {
    return;
  }

  std::mt19937 rng(std::random_device{}());
  bool first_retry = true;
  while (!g_stop_requested.load()) {
    FeishuRuntimeConfig feishu;
    {
      std::lock_guard<std::mutex> lock(*agent_mutex);
      feishu = read_feishu_config(*config);
    }

    if (!feishu.enabled || trim(feishu.mode) != "long_connection") {
      std::this_thread::sleep_for(std::chrono::seconds(1));
      first_retry = true;
      continue;
    }

    if (feishu.app_id.empty() || feishu.app_secret.empty()) {
      std::this_thread::sleep_for(std::chrono::seconds(3));
      continue;
    }

    FeishuWsEndpointData endpoint_data;
    std::string fetch_error;
    const auto ws_url = feishu_fetch_ws_url(feishu, &endpoint_data, &fetch_error);
    if (!ws_url.has_value()) {
      std::cerr << "[feishu] " << fetch_error << "\n";
      const int wait_seconds = std::max(5, feishu.reconnect_interval_seconds);
      for (int i = 0; i < wait_seconds && !g_stop_requested.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
      }
      continue;
    }
    std::cout << "[feishu] ws endpoint ready\n";

    if (first_retry && endpoint_data.reconnect_nonce_seconds > 0) {
      std::uniform_int_distribution<int> dist(0, endpoint_data.reconnect_nonce_seconds * 1000);
      std::this_thread::sleep_for(std::chrono::milliseconds(dist(rng)));
    }
    first_retry = false;

    std::string session_error;
    feishu_run_long_connection_session(ws_url.value(),
                                       feishu,
                                       endpoint_data,
                                       agent,
                                       agent_mutex,
                                       active_console_model,
                                       active_console_model_mutex,
                                       chat_events,
                                       chat_events_mutex,
                                       next_chat_event_id,
                                       seen_events,
                                       seen_events_mutex,
                                       cancelled_sessions,
                                       cancelled_sessions_mutex,
                                       running_sessions,
                                       running_sessions_mutex,
                                       latest_feishu_chat_id,
                                       latest_feishu_chat_id_mutex,
                                       &session_error);
    if (!session_error.empty()) {
      std::cerr << "[feishu] " << session_error << "\n";
    }

    const int wait_seconds = std::max(5, endpoint_data.reconnect_interval_seconds);
    for (int i = 0; i < wait_seconds && !g_stop_requested.load(); ++i) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  }
}

nlohmann::json current_config_payload(Config* config) {
  apply_supported_model_preset(config, true);
  nlohmann::json payload;
  payload["ok"] = true;
  payload["default_model"] = config->default_model_name();
  payload["max_tool_iterations"] = config->agents_defaults.max_tool_iterations;
  payload["max_history_messages"] = config->agents_defaults.max_history_messages;
  payload["workspace_path"] = config->agents_defaults.workspace;
  payload["restrict_to_workspace"] = config->agents_defaults.restrict_to_workspace;
  payload["models"] = nlohmann::json::array();
  for (const auto& preset : supported_models()) {
    auto model = find_model_config(*config, preset.model_name);
    payload["models"].push_back({
        {"model_name", preset.model_name},
        {"model", preset.model},
        {"api_key", model.has_value() ? model->api_key : ""},
        {"api_base", model.has_value() && !model->api_base.empty() ? model->api_base : preset.api_base},
        {"proxy", model.has_value() ? model->proxy : ""},
    });
  }
  const auto feishu = read_feishu_config(*config);
  payload["feishu"] = {
      {"enabled", feishu.enabled},
      {"app_id", ""},
      {"app_secret", ""},
      {"default_chat_id", feishu.default_chat_id},
      {"mode", feishu.mode},
      {"api_base", feishu.api_base},
      {"proxy", feishu.proxy},
      {"reconnect_interval_seconds", feishu.reconnect_interval_seconds},
      {"ping_interval_seconds", feishu.ping_interval_seconds},
  };
  payload["provider_models"] = {
      {"zhipu", read_provider_model_catalog(*config, "zhipu")},
      {"deepseek", read_provider_model_catalog(*config, "deepseek")},
      {"qwen", read_provider_model_catalog(*config, "qwen")},
      {"minmax", read_provider_model_catalog(*config, "minmax")},
  };
  return payload;
}

nlohmann::json parse_json_or_error(const std::string& text, std::string* error) {
  try {
    return nlohmann::json::parse(text);
  } catch (const std::exception& ex_first) {
    try {
      return nlohmann::json::parse(sanitize_utf8_lossy(text));
    } catch (const std::exception& ex_second) {
      if (error != nullptr) {
        *error = std::string(ex_first.what()) + " | utf8 retry failed: " + ex_second.what();
      }
      return nlohmann::json();
    }
  } catch (...) {
    if (error != nullptr) {
      *error = "unknown json parse error";
    }
    return nlohmann::json();
  }
}

std::string dump_json_lossy(const nlohmann::json& value, const int indent) {
  return value.dump(indent, ' ', false, nlohmann::json::error_handler_t::replace);
}

}  // namespace

int run_gateway(Config* config, AuthStore* auth_store, const bool debug_mode) {
  if (config == nullptr) {
    std::cerr << "invalid config\n";
    return 1;
  }

  if (debug_mode) {
    std::cout << "Debug mode enabled\n";
  }

  apply_supported_model_preset(config, true);
  config->save(default_config_path());

  AgentRuntime agent(config, auth_store);
  std::mutex agent_mutex;

  const auto workspace = std::filesystem::path(config->workspace_path());
  const auto cron_path = workspace / "cron" / "jobs.json";
  CronService cron_service(cron_path, [&](const CronJob& job) {
    std::lock_guard<std::mutex> lock(agent_mutex);
    const auto response = agent.process_direct(job.payload.message, "cron:" + job.id);
    if (!response.ok()) {
      throw std::runtime_error(response.error);
    }
    std::cout << "[cron] " << job.name << " -> " << response.content << "\n";
    return response.content;
  });
  cron_service.start();

  const auto channels = enabled_channels(*config);
  if (channels.empty()) {
    std::cout << "Warning: no channels enabled. Gateway will only expose health and cron.\n";
  } else {
    std::cout << "Enabled channels: ";
    for (std::size_t i = 0; i < channels.size(); ++i) {
      if (i != 0) {
        std::cout << ", ";
      }
      std::cout << channels[i];
    }
    std::cout << "\n";
  }

  const std::string host = gateway_host(*config);
  const int port = gateway_port(*config);

  httplib::Server server;
  std::string active_console_model = config->default_model_name();
  std::mutex active_console_model_mutex;
  std::vector<GatewayChatEvent> gateway_chat_events;
  std::mutex gateway_chat_events_mutex;
  std::atomic_int64_t next_gateway_chat_event_id{1};
  std::mutex config_payload_cache_mutex;
  std::string config_payload_cache_json;
  std::unordered_map<std::string, bool> cancelled_sessions;
  std::mutex cancelled_sessions_mutex;
  std::unordered_set<std::string> running_sessions;
  std::mutex running_sessions_mutex;
  const std::string events_instance_id =
      std::to_string(unix_ms_now()) + "-" + std::to_string(static_cast<unsigned long long>(std::random_device{}()));
  std::string latest_feishu_chat_id;
  std::mutex latest_feishu_chat_id_mutex;
  std::unordered_map<std::string, std::int64_t> feishu_seen_events;
  std::mutex feishu_seen_events_mutex;

  auto refresh_config_payload_cache = [&]() {
    const auto payload = current_config_payload(config);
    const auto dumped = payload.dump();
    std::lock_guard<std::mutex> lock(config_payload_cache_mutex);
    config_payload_cache_json = dumped;
  };
  {
    std::lock_guard<std::mutex> lock(agent_mutex);
    refresh_config_payload_cache();
  }

  server.Get("/", [](const httplib::Request&, httplib::Response& res) {
    res.set_content(kFrontendHtml, "text/html; charset=utf-8");
    res.status = 200;
  });

  server.Get("/health", [](const httplib::Request&, httplib::Response& res) {
    nlohmann::json payload{
        {"status", "ok"},
        {"service", "QingLongClaw"},
        {"time_ms", unix_ms_now()},
    };
    res.set_content(payload.dump(), "application/json");
    res.status = 200;
  });
  server.Get("/ready", [](const httplib::Request&, httplib::Response& res) {
    nlohmann::json payload{
        {"status", "ready"},
        {"service", "QingLongClaw"},
    };
    res.set_content(payload.dump(), "application/json");
    res.status = 200;
  });

  server.Get("/api/version", [&](const httplib::Request& req, httplib::Response& res) {
    if (!is_authorized(req)) {
      res.status = 401;
      res.set_content(nlohmann::json{{"ok", false}, {"error", "unauthorized"}}.dump(), "application/json");
      return;
    }
    auto payload = version_payload();
    payload["ok"] = true;
    res.status = 200;
    res.set_content(payload.dump(), "application/json");
  });

  server.Post("/api/probe-model", [&](const httplib::Request& req, httplib::Response& res) {
    if (!is_authorized(req)) {
      res.status = 401;
      res.set_content(nlohmann::json{{"ok", false}, {"error", "unauthorized"}}.dump(), "application/json");
      return;
    }

    std::string parse_error;
    const auto body = parse_json_or_error(req.body, &parse_error);
    if (body.is_null() || !parse_error.empty()) {
      res.status = 400;
      res.set_content(
          nlohmann::json{{"ok", false}, {"error", "invalid json: " + parse_error}}.dump(),
          "application/json");
      return;
    }

    const std::string requested_model = trim(body.value("model_name", config->default_model_name()));
    const auto model_override =
        requested_model.empty() ? std::nullopt : std::optional<std::string>(requested_model);

    std::lock_guard<std::mutex> lock(agent_mutex);
    const auto resolved = config->resolve_model(model_override);
    if (resolved.api_base.empty()) {
      res.status = 400;
      res.set_content(nlohmann::json{{"ok", false}, {"error", "api_base is empty for model " + resolved.model_name}}.dump(),
                      "application/json");
      return;
    }

    const auto probe = HttpClient::get(resolved.api_base, {}, resolved.proxy, 15);
    if (!probe.error.empty()) {
      res.status = 502;
      res.set_content(
          nlohmann::json{{"ok", false},
                         {"error", "network probe failed: " + probe.error},
                         {"api_base", resolved.api_base},
                         {"proxy", resolved.proxy}}
              .dump(),
          "application/json");
      return;
    }

    res.status = 200;
    res.set_content(
        nlohmann::json{{"ok", true},
                       {"model_name", resolved.model_name},
                       {"api_base", resolved.api_base},
                       {"proxy", resolved.proxy},
                       {"http_status", probe.status},
                       {"note", "Connectivity is OK. Non-2xx status can still be normal for base URL probe."}}
            .dump(),
        "application/json");
  });

  server.Post("/api/codex/models", [&](const httplib::Request& req, httplib::Response& res) {
    if (!is_authorized(req)) {
      res.status = 401;
      res.set_content(nlohmann::json{{"ok", false}, {"error", "unauthorized"}}.dump(), "application/json");
      return;
    }
    res.status = 410;
    res.set_content(nlohmann::json{{"ok", false},
                                   {"error", "codex endpoint removed in this open-source build"}}.dump(),
                    "application/json");
  });

  server.Post("/api/provider/models", [&](const httplib::Request& req, httplib::Response& res) {
    if (!is_authorized(req)) {
      res.status = 401;
      res.set_content(nlohmann::json{{"ok", false}, {"error", "unauthorized"}}.dump(), "application/json");
      return;
    }

    std::string parse_error;
    const auto body = parse_json_or_error(req.body, &parse_error);
    if (body.is_null() || !parse_error.empty()) {
      res.status = 400;
      res.set_content(
          nlohmann::json{{"ok", false}, {"error", "invalid json: " + parse_error}}.dump(),
          "application/json");
      return;
    }

    std::string provider = to_lower(trim(body.value("provider", std::string(""))));
    if (provider == "glm") {
      provider = "zhipu";
    } else if (provider == "minimax") {
      provider = "minmax";
    }
    if (provider != "zhipu" && provider != "deepseek" && provider != "qwen" && provider != "minmax") {
      res.status = 400;
      res.set_content(nlohmann::json{{"ok", false}, {"error", "unsupported provider"}}.dump(), "application/json");
      return;
    }

    std::string api_base;
    std::string api_key;
    std::string proxy;
    if (body.contains("api_base") && body["api_base"].is_string()) {
      api_base = normalize_api_base(body["api_base"].get<std::string>());
    }
    if (body.contains("api_key") && body["api_key"].is_string()) {
      api_key = trim(body["api_key"].get<std::string>());
    }
    if (body.contains("proxy") && body["proxy"].is_string()) {
      proxy = trim(body["proxy"].get<std::string>());
    }

    const std::string model_name = provider == "zhipu"
                                       ? "glm-4.7"
                                       : (provider == "deepseek"
                                              ? "deepseek-chat"
                                              : (provider == "qwen" ? "qwen-plus" : "minimax-m2.1"));
    {
      std::lock_guard<std::mutex> lock(agent_mutex);
      const auto model = find_model_config(*config, model_name);
      if (model.has_value()) {
        if (api_base.empty()) {
          api_base = normalize_api_base(model->api_base);
        }
        if (api_key.empty()) {
          api_key = trim(model->api_key);
        }
        if (proxy.empty()) {
          proxy = trim(model->proxy);
        }
      }
    }
    if (api_base.empty()) {
      if (provider == "zhipu") {
        api_base = "https://open.bigmodel.cn/api/paas/v4";
      } else if (provider == "deepseek") {
        api_base = "https://api.deepseek.com/v1";
      } else if (provider == "qwen") {
        api_base = "https://dashscope.aliyuncs.com/compatible-mode/v1";
      } else if (provider == "minmax") {
        api_base = "https://api.minimaxi.com/v1";
      }
    }

    if (api_base.empty()) {
      res.status = 400;
      res.set_content(nlohmann::json{{"ok", false}, {"error", "api_base is empty"}}.dump(), "application/json");
      return;
    }
    if (api_key.empty()) {
      res.status = 400;
      res.set_content(nlohmann::json{{"ok", false}, {"error", "api_key is empty"}}.dump(), "application/json");
      return;
    }

    std::vector<std::string> models;
    std::string used_url;
    std::string fetch_error;
    if (!fetch_remote_model_catalog(api_base, api_key, proxy, &models, &used_url, &fetch_error)) {
      if (provider == "minmax") {
        enrich_minmax_models(&models);
        res.status = 200;
        res.set_content(nlohmann::json{{"ok", true},
                                       {"provider", provider},
                                       {"api_base", api_base},
                                       {"catalog_url", used_url},
                                       {"models", models},
                                       {"count", static_cast<int>(models.size())},
                                       {"warning", "remote model catalog unavailable; returned built-in minimax set"}}
                            .dump(),
                        "application/json");
        return;
      }
      res.status = 502;
      res.set_content(nlohmann::json{{"ok", false},
                                     {"provider", provider},
                                     {"error", fetch_error},
                                     {"api_base", api_base}}
                          .dump(),
                      "application/json");
      return;
    }
    if (provider == "minmax") {
      enrich_minmax_models(&models);
    }

    res.status = 200;
    res.set_content(nlohmann::json{{"ok", true},
                                   {"provider", provider},
                                   {"api_base", api_base},
                                   {"catalog_url", used_url},
                                   {"models", models},
                                   {"count", static_cast<int>(models.size())}}
                        .dump(),
                    "application/json");
  });

  server.Get("/api/runtime/model", [&](const httplib::Request& req, httplib::Response& res) {
    if (!is_authorized(req)) {
      res.status = 401;
      res.set_content(nlohmann::json{{"ok", false}, {"error", "unauthorized"}}.dump(), "application/json");
      return;
    }

    std::string model_name;
    {
      std::lock_guard<std::mutex> lock(active_console_model_mutex);
      model_name = active_console_model;
    }
    if (trim(model_name).empty()) {
      std::lock_guard<std::mutex> lock(agent_mutex);
      model_name = config->default_model_name();
    }

    res.status = 200;
    res.set_content(nlohmann::json{{"ok", true}, {"model_name", model_name}}.dump(), "application/json");
  });

  server.Post("/api/runtime/model", [&](const httplib::Request& req, httplib::Response& res) {
    if (!is_authorized(req)) {
      res.status = 401;
      res.set_content(nlohmann::json{{"ok", false}, {"error", "unauthorized"}}.dump(), "application/json");
      return;
    }

    std::string parse_error;
    const auto body = parse_json_or_error(req.body, &parse_error);
    if (body.is_null() || !parse_error.empty()) {
      res.status = 400;
      res.set_content(
          nlohmann::json{{"ok", false}, {"error", "invalid json: " + parse_error}}.dump(),
          "application/json");
      return;
    }

    std::string model_name = trim(body.value("model_name", std::string("")));
    if (model_name.empty()) {
      std::lock_guard<std::mutex> lock(agent_mutex);
      model_name = config->default_model_name();
    }
    if (model_name.empty()) {
      res.status = 400;
      res.set_content(nlohmann::json{{"ok", false}, {"error", "model_name is empty"}}.dump(), "application/json");
      return;
    }

    {
      std::lock_guard<std::mutex> lock(active_console_model_mutex);
      active_console_model = model_name;
    }

    res.status = 200;
    res.set_content(nlohmann::json{{"ok", true}, {"model_name", model_name}}.dump(), "application/json");
  });

  server.Get("/api/events", [&](const httplib::Request& req, httplib::Response& res) {
    if (!is_authorized(req)) {
      res.status = 401;
      res.set_content(nlohmann::json{{"ok", false}, {"error", "unauthorized"}}.dump(), "application/json");
      return;
    }

    std::int64_t since_id = 0;
    if (req.has_param("since_id")) {
      try {
        since_id = std::stoll(req.get_param_value("since_id"));
      } catch (const std::exception&) {
        res.status = 400;
        res.set_content(nlohmann::json{{"ok", false}, {"error", "invalid since_id"}}.dump(), "application/json");
        return;
      }
    }

    const auto events = collect_gateway_chat_events_since(&gateway_chat_events, &gateway_chat_events_mutex, since_id);
    nlohmann::json items = nlohmann::json::array();
    std::int64_t next_since_id = since_id;
    for (const auto& event : events) {
      next_since_id = std::max(next_since_id, event.id);
      items.push_back({
          {"id", event.id},
          {"time_ms", event.time_ms},
          {"kind", event.kind},
          {"role", event.role},
          {"source", event.source},
          {"session_key", event.session_key},
          {"model_name", event.model_name},
          {"content", event.content},
      });
    }
    const auto running = collect_running_gateway_sessions(&running_sessions, &running_sessions_mutex);

    res.status = 200;
    res.set_content(nlohmann::json{{"ok", true},
                                   {"instance_id", events_instance_id},
                                   {"events", items},
                                   {"next_since_id", next_since_id},
                                   {"running_sessions", running}}
                        .dump(),
                    "application/json");
  });

  server.Get("/api/config", [&](const httplib::Request& req, httplib::Response& res) {
    if (!is_authorized(req)) {
      res.status = 401;
      res.set_content(nlohmann::json{{"ok", false}, {"error", "unauthorized"}}.dump(), "application/json");
      return;
    }
    {
      std::lock_guard<std::mutex> lock(config_payload_cache_mutex);
      if (!config_payload_cache_json.empty()) {
        res.status = 200;
        res.set_content(config_payload_cache_json, "application/json");
        return;
      }
    }

    std::lock_guard<std::mutex> lock(agent_mutex);
    const auto payload = current_config_payload(config);
    const auto dumped = payload.dump();
    {
      std::lock_guard<std::mutex> cache_lock(config_payload_cache_mutex);
      config_payload_cache_json = dumped;
    }
    res.status = 200;
    res.set_content(dumped, "application/json");
  });

  server.Post("/api/config", [&](const httplib::Request& req, httplib::Response& res) {
    if (!is_authorized(req)) {
      res.status = 401;
      res.set_content(nlohmann::json{{"ok", false}, {"error", "unauthorized"}}.dump(), "application/json");
      return;
    }

    std::string parse_error;
    const auto body = parse_json_or_error(req.body, &parse_error);
    if (body.is_null() || !parse_error.empty()) {
      res.status = 400;
      res.set_content(
          nlohmann::json{{"ok", false}, {"error", "invalid json: " + parse_error}}.dump(),
          "application/json");
      return;
    }

    std::lock_guard<std::mutex> lock(agent_mutex);
    apply_supported_model_preset(config, true);

    const auto requested_default = body.value("default_model", config->agents_defaults.model_name);
    if (is_supported_model_name(requested_default)) {
      config->agents_defaults.model_name = requested_default;
      config->agents_defaults.legacy_model = requested_default;
    }
    if (body.contains("max_tool_iterations") && body["max_tool_iterations"].is_number_integer()) {
      const int requested = body["max_tool_iterations"].get<int>();
      config->agents_defaults.max_tool_iterations = std::max(0, std::min(100000, requested));
    }
    if (body.contains("max_history_messages") && body["max_history_messages"].is_number_integer()) {
      const int requested = body["max_history_messages"].get<int>();
      config->agents_defaults.max_history_messages = std::max(4, std::min(800, requested));
    }

    if (body.contains("workspace_path") && body["workspace_path"].is_string()) {
      const std::string workspace_path = trim(body["workspace_path"].get<std::string>());
      if (!workspace_path.empty()) {
        config->agents_defaults.workspace = workspace_path;
      }
    }
    if (body.contains("restrict_to_workspace") && body["restrict_to_workspace"].is_boolean()) {
      config->agents_defaults.restrict_to_workspace = body["restrict_to_workspace"].get<bool>();
    }

    if (body.contains("models") && body["models"].is_array()) {
      auto update_existing_model = [&](const std::string& model_name,
                                       const std::string& api_key,
                                       const std::optional<std::string>& api_base,
                                       const std::optional<std::string>& proxy) {
        for (auto& model : config->model_list) {
          if (model.model_name != model_name) {
            continue;
          }
          model.api_key = api_key;
          if (api_base.has_value() && !api_base->empty()) {
            model.api_base = *api_base;
          }
          if (proxy.has_value()) {
            model.proxy = *proxy;
          }
          break;
        }
      };

      for (const auto& item : body["models"]) {
        if (!item.is_object()) {
          continue;
        }
        const std::string model_name = item.value("model_name", "");
        if (!is_supported_model_name(model_name)) {
          continue;
        }
        const std::string api_key = item.value("api_key", "");
        const auto api_base = item.contains("api_base") && item["api_base"].is_string()
                                  ? std::optional<std::string>(item["api_base"].get<std::string>())
                                  : std::nullopt;
        const auto proxy = item.contains("proxy") && item["proxy"].is_string()
                               ? std::optional<std::string>(item["proxy"].get<std::string>())
                               : std::nullopt;
        update_existing_model(model_name, api_key, api_base, proxy);
      }
    }

    if (body.contains("feishu") && body["feishu"].is_object()) {
      auto& channels = config->raw["channels"];
      if (!channels.is_object()) {
        channels = nlohmann::json::object();
      }
      auto& feishu = channels["feishu"];
      if (!feishu.is_object()) {
        feishu = nlohmann::json::object();
      }

      const auto input = body["feishu"];
      feishu["app_id"] = "";
      feishu["app_secret"] = "";
      if (input.contains("proxy") && input["proxy"].is_string()) {
        feishu["proxy"] = trim(input["proxy"].get<std::string>());
      } else if (!feishu.contains("proxy")) {
        feishu["proxy"] = "";
      }
      if (input.contains("default_chat_id") && input["default_chat_id"].is_string()) {
        feishu["default_chat_id"] = trim(input["default_chat_id"].get<std::string>());
      } else if (!feishu.contains("default_chat_id")) {
        feishu["default_chat_id"] = "";
      }
      feishu["mode"] = "long_connection";
      if (input.contains("api_base") && input["api_base"].is_string()) {
        const std::string api_base = trim(input["api_base"].get<std::string>());
        if (!api_base.empty()) {
          feishu["api_base"] = api_base;
        }
      }
      if (!feishu.contains("api_base")) {
        feishu["api_base"] = "https://open.feishu.cn";
      }
      if (!feishu.contains("reconnect_count")) {
        feishu["reconnect_count"] = -1;
      }
      if (!feishu.contains("reconnect_interval_seconds")) {
        feishu["reconnect_interval_seconds"] = 120;
      }
      if (!feishu.contains("reconnect_nonce_seconds")) {
        feishu["reconnect_nonce_seconds"] = 30;
      }
      if (!feishu.contains("ping_interval_seconds")) {
        feishu["ping_interval_seconds"] = 120;
      }
      if (!feishu.contains("allow_from")) {
        feishu["allow_from"] = nlohmann::json::array();
      }
      feishu["enabled"] = false;
    }

    if (body.contains("provider_models") && body["provider_models"].is_object()) {
      auto& providers = config->raw["providers"];
      if (!providers.is_object()) {
        providers = nlohmann::json::object();
      }
      for (const auto& provider : {"zhipu", "deepseek", "qwen", "minmax"}) {
        if (!body["provider_models"].contains(provider) || !body["provider_models"][provider].is_array()) {
          continue;
        }
        auto& section = providers[provider];
        if (!section.is_object()) {
          section = nlohmann::json::object();
        }
        std::unordered_set<std::string> seen;
        nlohmann::json models = nlohmann::json::array();
        for (const auto& item : body["provider_models"][provider]) {
          if (!item.is_string()) {
            continue;
          }
          const auto model = trim(item.get<std::string>());
          if (model.empty() || seen.find(model) != seen.end()) {
            continue;
          }
          seen.insert(model);
          models.push_back(model);
        }
        section["models"] = models;
      }
    }

    apply_supported_model_preset(config, true);
    if (!config->save(default_config_path())) {
      res.status = 500;
      res.set_content(nlohmann::json{{"ok", false}, {"error", "failed to save config"}}.dump(),
                      "application/json");
      return;
    }

    const auto payload = current_config_payload(config);
    const auto dumped = payload.dump();
    {
      std::lock_guard<std::mutex> cache_lock(config_payload_cache_mutex);
      config_payload_cache_json = dumped;
    }
    res.status = 200;
    res.set_content(dumped, "application/json");
  });

  server.Post("/api/chat/stop", [&](const httplib::Request& req, httplib::Response& res) {
    if (!is_authorized(req)) {
      res.status = 401;
      res.set_content(nlohmann::json{{"ok", false}, {"error", "unauthorized"}}.dump(), "application/json");
      return;
    }

    std::string session_key = "web:default";
    if (!trim(req.body).empty()) {
      std::string parse_error;
      const auto body = parse_json_or_error(req.body, &parse_error);
      if (body.is_null() || !parse_error.empty()) {
        res.status = 400;
        res.set_content(
            nlohmann::json{{"ok", false}, {"error", "invalid json: " + parse_error}}.dump(),
            "application/json");
        return;
      }
      if (body.contains("session_key") && body["session_key"].is_string()) {
        session_key = body["session_key"].get<std::string>();
      }
    }
    session_key = trim(session_key);
    if (session_key.empty()) {
      session_key = "web:default";
    }

    {
      std::lock_guard<std::mutex> lock(cancelled_sessions_mutex);
      cancelled_sessions[session_key] = true;
    }

    res.status = 200;
    res.set_content(nlohmann::json{{"ok", true}, {"session_key", session_key}}.dump(), "application/json");
  });

  server.Post("/api/chat", [&](const httplib::Request& req, httplib::Response& res) {
    if (!is_authorized(req)) {
      res.status = 401;
      res.set_content(nlohmann::json{{"ok", false}, {"error", "unauthorized"}}.dump(), "application/json");
      return;
    }

    std::string parse_error;
    const auto body = parse_json_or_error(req.body, &parse_error);
    if (body.is_null() || !parse_error.empty()) {
      res.status = 400;
      res.set_content(
          nlohmann::json{{"ok", false}, {"error", "invalid json: " + parse_error}}.dump(),
          "application/json");
      return;
    }

    const std::string message = sanitize_utf8_lossy(body.value("message", ""));
    const std::string session_key = sanitize_utf8_lossy(body.value("session_key", "web:default"));
    const std::string model_name = sanitize_utf8_lossy(body.value("model_name", ""));
    const std::string trimmed_model_name = trim(model_name);
    const std::string normalized_session_key = trim(session_key).empty() ? "web:default" : trim(session_key);
    FeishuRuntimeConfig feishu_cfg;
    {
      std::lock_guard<std::mutex> lock(agent_mutex);
      feishu_cfg = read_feishu_config(*config);
    }
    std::string target_chat_id;
    if (feishu_cfg.enabled) {
      target_chat_id = feishu_chat_id_from_session_key(normalized_session_key);
      if (target_chat_id.empty()) {
        target_chat_id = trim(feishu_cfg.default_chat_id);
      }
      if (target_chat_id.empty()) {
        std::lock_guard<std::mutex> lock(latest_feishu_chat_id_mutex);
        target_chat_id = trim(latest_feishu_chat_id);
      }
      if (!target_chat_id.empty()) {
        std::lock_guard<std::mutex> lock(latest_feishu_chat_id_mutex);
        latest_feishu_chat_id = target_chat_id;
      }
    }
    const bool is_web_session = normalized_session_key.rfind("web:", 0) == 0;
    const std::string effective_session_key =
        (feishu_cfg.enabled && is_web_session && !target_chat_id.empty())
            ? feishu_session_key_from_chat_id(target_chat_id)
            : normalized_session_key;
    if (trim(message).empty()) {
      res.status = 400;
      res.set_content(nlohmann::json{{"ok", false}, {"error", "message is empty"}}.dump(),
                      "application/json");
      return;
    }

    if (!trimmed_model_name.empty()) {
      std::lock_guard<std::mutex> lock(active_console_model_mutex);
      active_console_model = trimmed_model_name;
    }

    {
      std::lock_guard<std::mutex> lock(cancelled_sessions_mutex);
      cancelled_sessions[effective_session_key] = false;
    }
    set_gateway_session_running(&running_sessions, &running_sessions_mutex, effective_session_key, true);
    push_gateway_chat_event(&gateway_chat_events,
                            &gateway_chat_events_mutex,
                            &next_gateway_chat_event_id,
                            "message",
                            "user",
                            "web",
                            effective_session_key,
                            trimmed_model_name,
                            trim(message));
    push_gateway_chat_event(&gateway_chat_events,
                            &gateway_chat_events_mutex,
                            &next_gateway_chat_event_id,
                            "task_start",
                            "system",
                            "web",
                            effective_session_key,
                            trimmed_model_name,
                            "");

    const auto is_cancelled = [&cancelled_sessions, &cancelled_sessions_mutex, effective_session_key]() -> bool {
      std::lock_guard<std::mutex> lock(cancelled_sessions_mutex);
      const auto it = cancelled_sessions.find(effective_session_key);
      if (it == cancelled_sessions.end()) {
        return false;
      }
      return it->second;
    };

    AgentResponse response;
    try {
      std::lock_guard<std::mutex> lock(agent_mutex);
      response =
          agent.process_direct(message,
                               effective_session_key,
                               trimmed_model_name.empty() ? std::nullopt : std::optional<std::string>(trimmed_model_name),
                               false,
                               is_cancelled);
    } catch (const std::exception& ex) {
      response.error = std::string("internal error: ") + ex.what();
    } catch (...) {
      response.error = "internal error: unknown exception";
    }
    push_gateway_chat_event(&gateway_chat_events,
                            &gateway_chat_events_mutex,
                            &next_gateway_chat_event_id,
                            "message",
                            "assistant",
                            "gateway",
                            effective_session_key,
                            trimmed_model_name,
                            response.ok() ? trim(response.content) : ("Error: " + trim(response.error)));
    if (response.ok() && !trim(response.usage_summary).empty()) {
      push_gateway_chat_event(&gateway_chat_events,
                              &gateway_chat_events_mutex,
                              &next_gateway_chat_event_id,
                              "message",
                              "system",
                              "runtime",
                              effective_session_key,
                              trimmed_model_name,
                              trim(response.usage_summary));
    }
    set_gateway_session_running(&running_sessions, &running_sessions_mutex, effective_session_key, false);
    push_gateway_chat_event(&gateway_chat_events,
                            &gateway_chat_events_mutex,
                            &next_gateway_chat_event_id,
                            "task_end",
                            "system",
                            "web",
                            effective_session_key,
                            trimmed_model_name,
                            "");

    const auto sync_web_chat_to_feishu =
        [&](const std::string& assistant_text, const bool is_error, const std::string& usage_summary) {
      if (!feishu_cfg.enabled) {
        return;
      }
      if (target_chat_id.empty()) {
        push_gateway_chat_event(&gateway_chat_events,
                                &gateway_chat_events_mutex,
                                &next_gateway_chat_event_id,
                                "message",
                                "system",
                                "runtime",
                                effective_session_key,
                                trimmed_model_name,
                                "Feishu sync skipped: target chat_id is empty. Set Feishu default Chat ID in config.");
        std::cerr << "[feishu] sync web chat skipped: target chat_id is empty\n";
        return;
      }

      const std::string user_line = "Web user: " + trim(message);
      std::string reply_line = trim(assistant_text);
      if (is_error && !reply_line.empty()) {
        reply_line = "Error: " + reply_line;
      }
      if (reply_line.empty()) {
        reply_line = is_error ? "Error: empty response." : "Done.";
      }
      if (!trim(usage_summary).empty()) {
        reply_line += "\n\n" + trim(usage_summary);
      }

      std::thread([feishu_cfg, target_chat_id, user_line, reply_line]() {
        std::string send_error;
        bool user_sent = true;
        if (!trim(user_line).empty()) {
          if (!feishu_send_text_message(feishu_cfg, target_chat_id, user_line, &send_error)) {
            user_sent = false;
            std::cerr << "[feishu] sync web user message failed: " << send_error << "\n";
          }
        }

        std::string final_reply = reply_line;
        if (!user_sent && !trim(user_line).empty() && final_reply.find(user_line) == std::string::npos) {
          final_reply = user_line + "\n" + final_reply;
        }

        send_error.clear();
        if (!feishu_send_text_message(feishu_cfg, target_chat_id, final_reply, &send_error)) {
          std::cerr << "[feishu] sync web assistant message failed: " << send_error << "\n";
        }
      }).detach();
    };

    sync_web_chat_to_feishu(response.ok() ? response.content : response.error,
                            !response.ok(),
                            response.ok() ? response.usage_summary : "");
    if (!response.ok()) {
      res.status = 400;
      res.set_content(dump_json_lossy(nlohmann::json{{"ok", false}, {"error", sanitize_utf8_lossy(response.error)}}),
                      "application/json");
      return;
    }

    res.status = 200;
    res.set_content(dump_json_lossy(nlohmann::json{{"ok", true},
                                                   {"response", sanitize_utf8_lossy(response.content)},
                                                   {"usage_summary", sanitize_utf8_lossy(response.usage_summary)}}),
                    "application/json");
  });

  g_stop_requested.store(false);
  std::signal(SIGINT, signal_handler);
#ifndef _WIN32
  std::signal(SIGTERM, signal_handler);
#endif

  std::thread feishu_thread([&]() {
    feishu_long_connection_loop(config,
                                &agent,
                                &agent_mutex,
                                &active_console_model,
                                &active_console_model_mutex,
                                &gateway_chat_events,
                                &gateway_chat_events_mutex,
                                &next_gateway_chat_event_id,
                                &feishu_seen_events,
                                &feishu_seen_events_mutex,
                                &cancelled_sessions,
                                &cancelled_sessions_mutex,
                                &running_sessions,
                                &running_sessions_mutex,
                                &latest_feishu_chat_id,
                                &latest_feishu_chat_id_mutex);
  });

  std::thread server_thread([&]() {
    if (!server.listen(host.c_str(), port)) {
      std::cerr << "failed to start health server on " << host << ":" << port << "\n";
      g_stop_requested.store(true);
    }
  });

  std::cout << version_text(false) << "\n";
  std::cout << "Gateway started on " << host << ":" << port << "\n";
  std::cout << "Web console: http://" << host << ":" << port << "/\n";
  std::cout << "Health endpoints: /health and /ready\n";
  const auto startup_feishu = read_feishu_config(*config);
  std::cout << "Feishu event mode: " << startup_feishu.mode << "\n";
  if (startup_feishu.enabled) {
    std::cout << "Feishu channel: enabled\n";
  } else {
    std::cout << "Feishu channel: disabled\n";
  }
  if (std::getenv("qinglongclaw_WEB_TOKEN") != nullptr) {
    std::cout << "Web API auth enabled via qinglongclaw_WEB_TOKEN\n";
  }
  std::cout << "Press Ctrl+C to stop\n";

  while (!g_stop_requested.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }

  server.stop();
  if (server_thread.joinable()) {
    server_thread.join();
  }
  if (feishu_thread.joinable()) {
    feishu_thread.detach();
  }
  cron_service.stop();

  std::cout << "Gateway stopped\n";
  return 0;
}

}  // namespace QingLongClaw








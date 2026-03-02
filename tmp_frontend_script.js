
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
    const builtInCodexModels = ["gpt-5.3-codex", "gpt-5.1-codex", "gpt-5-codex", "gpt-5-mini-codex", "o4-mini", "o4-mini-high", "o3"];

    const I18N = {
      zh: {
        title: "CppTaskClaw \u804a\u5929\u63a7\u5236\u53f0",
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
        adminTokenLabel: "\u7ba1\u7406\u4ee4\u724c\uff08\u4ec5\u5f53\u670d\u52a1\u7aef\u8bbe\u7f6e PIPIXIA_WEB_TOKEN \u65f6\u9700\u8981\uff09",
        adminTokenPlaceholder: "\u53ef\u9009",
        defaultModelLabel: "\u9ed8\u8ba4\u6a21\u578b",
        maxToolIterationsLabel: "\u6700\u5927\u5de5\u5177\u8fed\u4ee3\u6b21\u6570\uff080=\u4e0d\u9650\u5236\uff09",
        maxHistoryMessagesLabel: "\u4e0a\u4e0b\u6587\u5386\u53f2\u6d88\u606f\u6761\u6570",
        workspacePathLabel: "\u5de5\u4f5c\u7a7a\u95f4\u8def\u5f84",
        workspacePathPlaceholder: "/userdata/cpptaskclaw",
        restrictWorkspaceLabel: "\u4ec5\u5141\u8bb8\u5de5\u5177\u8bbf\u95ee\u5de5\u4f5c\u7a7a\u95f4",
        codexPrimaryLabel: "\u672a\u624b\u52a8\u6307\u5b9a\u6a21\u578b\u65f6\u4f18\u5148\u4f7f\u7528 Codex",
        codexFallbackOnErrorLabel: "Codex \u8bf7\u6c42\u5931\u8d25\u65f6\u56de\u9000\u5230\u975e Codex \u6a21\u578b",
        groupGlm: "GLM (Zhipu)",
        groupDeepseek: "DeepSeek",
        groupQwen: "Qwen",
        groupMinmax: "MinMax",
        groupCodex: "JingFlow Codex",
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
        codexBasePlaceholder: "https://api.jingflowai.com/api/codex/codex",
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
        title: "CppTaskClaw Chat Console",
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
        adminTokenLabel: "Admin token (required only when PIPIXIA_WEB_TOKEN is set on server)",
        adminTokenPlaceholder: "Optional",
        defaultModelLabel: "Default model",
        maxToolIterationsLabel: "Max tool iterations (0 = unlimited)",
        maxHistoryMessagesLabel: "Context history messages",
        workspacePathLabel: "Workspace Path",
        workspacePathPlaceholder: "/userdata/cpptaskclaw",
        restrictWorkspaceLabel: "Only allow tools to access workspace",
        codexPrimaryLabel: "Codex priority when model is not manually selected",
        codexFallbackOnErrorLabel: "Fallback to non-codex model if Codex request fails",
        groupGlm: "GLM (Zhipu)",
        groupDeepseek: "DeepSeek",
        groupQwen: "Qwen",
        groupMinmax: "MinMax",
        groupCodex: "JingFlow Codex",
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
        codexBasePlaceholder: "https://api.jingflowai.com/api/codex/codex",
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
      const stored = (localStorage.getItem("pipixia.lang") || "").toLowerCase();
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
        codex_primary: !!el("codexPrimary").checked,
        codex_fallback_on_error: !!el("codexFallbackOnError").checked,
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
        codex: {
          api_key: el("codexKey").value || "",
          api_base: el("codexBase").value || "https://api.jingflowai.com/api/codex/codex",
          proxy: el("codexProxy").value || "",
          models: codexModelsCatalog
        },
        provider_models: {
          zhipu: glmModelsCatalog,
          deepseek: deepseekModelsCatalog,
          qwen: qwenModelsCatalog,
          minmax: minmaxModelsCatalog,
          codex: codexModelsCatalog
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
        el("codexPrimary").checked = data.codex_primary !== false;
        el("codexFallbackOnError").checked = data.codex_fallback_on_error !== false;
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
        const codex = data.codex || {};
        el("codexKey").value = codex.api_key || "";
        el("codexBase").value = codex.api_base || "https://api.jingflowai.com/api/codex/codex";
        el("codexProxy").value = codex.proxy || "";
        const providerModels = data.provider_models || {};
        glmModelsCatalog = Array.isArray(providerModels.zhipu) ?
          providerModels.zhipu.filter((m) => typeof m === "string" && m.trim()) : [];
        deepseekModelsCatalog = Array.isArray(providerModels.deepseek) ?
          providerModels.deepseek.filter((m) => typeof m === "string" && m.trim()) : [];
        qwenModelsCatalog = Array.isArray(providerModels.qwen) ?
          providerModels.qwen.filter((m) => typeof m === "string" && m.trim()) : [];
        minmaxModelsCatalog = Array.isArray(providerModels.minmax) ?
          providerModels.minmax.filter((m) => typeof m === "string" && m.trim()) : [];
        codexModelsCatalog = Array.isArray(providerModels.codex) ?
          providerModels.codex.filter((m) => typeof m === "string" && m.trim()) :
          (Array.isArray(codex.models) ? codex.models.filter((m) => typeof m === "string" && m.trim()) : []);
        minmaxModelsCatalog = mergeCatalogModels(minmaxModelsCatalog, builtInMinmaxModels);
        codexModelsCatalog = mergeCatalogModels(codexModelsCatalog, builtInCodexModels);
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
        if (!codexModelsCatalog.length && (el("codexKey").value || "").trim()) {
          await loadProviderModels("codex", true);
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
          const displayName = data.project_display_name || "CppTaskClaw";
          appendMessage("system", tr("welcomeBanner", { displayName, version: (data.pipixia || "unknown"), toolApi }));
          return;
        }
      } catch (e) {
      }
      appendMessage("system", tr("welcomeBanner", { displayName: "CppTaskClaw", version: "unknown", toolApi: "unknown" }));
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
      localStorage.setItem("pipixia.lang", nextLang);
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
  
#!/usr/bin/env bash
set -euo pipefail

CONFIG_PATH="${1:-$HOME/.qinglongclaw/config.json}"
AUTH_PATH="${2:-$HOME/.qinglongclaw/auth.json}"

if ! command -v jq >/dev/null 2>&1; then
  echo "error: jq is required. install jq first." >&2
  exit 1
fi

if [[ -f "$CONFIG_PATH" ]]; then
  tmp_cfg="${CONFIG_PATH}.tmp.$$"
  jq '
    .model_list = ((.model_list // []) | map(.api_key="" | .auth_method="")) |
    .providers = ((.providers // {})
      | with_entries(.value = ((.value // {}) | .api_key="" | .auth_method=""))) |
    .channels.feishu = ((.channels.feishu // {})
      | .enabled=false
      | .app_id=""
      | .app_secret=""
      | .default_chat_id=(.default_chat_id // ""))
  ' "$CONFIG_PATH" > "$tmp_cfg"
  mv "$tmp_cfg" "$CONFIG_PATH"
  echo "sanitized: $CONFIG_PATH"
else
  echo "skip: config not found: $CONFIG_PATH"
fi

if [[ -f "$AUTH_PATH" ]]; then
  tmp_auth="${AUTH_PATH}.tmp.$$"
  jq '[]' "$AUTH_PATH" > "$tmp_auth"
  mv "$tmp_auth" "$AUTH_PATH"
  echo "sanitized: $AUTH_PATH"
else
  echo "skip: auth not found: $AUTH_PATH"
fi

echo "done"

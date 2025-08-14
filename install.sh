#!/usr/bin/env bash
set -euo pipefail

# codetags installer:
# - Builds and installs codetags
# - Creates a systemd --user service that runs `codetags watch`
# - Enables it to start at login (user session boot) and starts it now

SERVICE_NAME="codetags.service"
USER_SYSTEMD_DIR="${HOME}/.config/systemd/user"
LOG_DIR="${HOME}/.cache/codetags"
LOG_FILE="${LOG_DIR}/watch.log"
BIN_PATH="/usr/local/bin/codetags"

# Detect the right codetags path (use installed binary); fallback to current build if not found after install
detect_codetags_bin() {
  if command -v codetags >/dev/null 2>&1; then
    command -v codetags
  elif [ -x "$BIN_PATH" ]; then
    echo "$BIN_PATH"
  elif [ -x "./codetags" ]; then
    # local build fallback (not preferred)
    echo "$(pwd)/codetags"
  else
    echo ""
  fi
}

# 1) Build
echo "[1/5] Building codetags (make)..."
make

# 2) Install system-wide
echo "[2/5] Installing codetags (sudo make install)..."
sudo make install

# 3) Prepare systemd user service
echo "[3/5] Configuring systemd user service..."
mkdir -p "${USER_SYSTEMD_DIR}"
mkdir -p "${LOG_DIR}"

# Resolve installed codetags path
CODETAGS_BIN="$(detect_codetags_bin)"
if [ -z "${CODETAGS_BIN}" ]; then
  echo "Error: codetags binary not found after install." >&2
  exit 1
fi

# Create the service unit file
SERVICE_UNIT_PATH="${USER_SYSTEMD_DIR}/${SERVICE_NAME}"
cat > "${SERVICE_UNIT_PATH}" <<EOF
[Unit]
Description=Codetags system-wide watcher (user)
After=default.target

[Service]
Type=simple
ExecStart=${CODETAGS_BIN} watch
Restart=always
RestartSec=2
# Ensure HOME is set for user services
Environment=HOME=%h
# Optional: log to file
StandardOutput=append:${LOG_FILE}
StandardError=append:${LOG_FILE}

[Install]
WantedBy=default.target
EOF

echo "Created user service: ${SERVICE_UNIT_PATH}"

# 4) Enable lingering (optional but recommended for truly background user services even without login)
# This allows the user unit to run at system boot without an active graphical/login session.
if command -v loginctl >/dev/null 2>&1; then
  if loginctl show-user "$USER" | grep -q "^Linger=no"; then
    echo "Enabling user lingering so the service can run at boot without an active session..."
    sudo loginctl enable-linger "$USER" || true
  fi
else
  echo "Note: loginctl not found; the service will start on user session/login but may not persist across reboots without an auto-login session."
fi

# 5) Enable and start the service
echo "[4/5] Enabling and starting the codetags user service..."
systemctl --user daemon-reload
systemctl --user enable --now "${SERVICE_NAME}"

# 6) Status and guidance
echo "[5/5] Verifying service status..."
sleep 1
systemctl --user --no-pager --full status "${SERVICE_NAME}" || true

echo
echo "Installation complete."
echo "- Binary: ${CODETAGS_BIN}"
echo "- Service: ${SERVICE_UNIT_PATH}"
echo "- Logs: ${LOG_FILE}"
echo
echo "Usage:"
echo "- Initialize a repo: cd /path/to/repo && codetags init"
echo "- The watcher runs system-wide and will pick up any initialized repos automatically."
echo "- Manage the service:"
echo "  systemctl --user status ${SERVICE_NAME}"
echo "  systemctl --user restart ${SERVICE_NAME}"
echo "  systemctl --user stop ${SERVICE_NAME}"
echo
echo "If you installed just now, you may need to re-login for user services to auto-start at session boot."
echo "With lingering enabled, the service should start at system boot even without an active session."

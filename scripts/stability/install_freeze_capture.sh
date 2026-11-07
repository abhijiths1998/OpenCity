#!/usr/bin/env bash
# Make a hard freeze leave evidence. Run with sudo before the M0 stability gate.
#   sudo scripts/stability/install_freeze_capture.sh           install
#   sudo scripts/stability/install_freeze_capture.sh --remove  undo
# What it does:
#   - kernel lockups and long-hung tasks panic instead of hanging silently,
#     the oops goes to EFI pstore, and the machine reboots after 15 s
#   - journald syncs to disk every 5 s, so the last logs survive
#   - SysRq fully enabled, so Alt+SysRq+REISUB works during a freeze
# It does NOT edit the bootloader; if pstore is disabled it prints what to add.
set -euo pipefail
[[ $EUID -eq 0 ]] || { echo "run with sudo" >&2; exit 1; }

SYSCTL=/etc/sysctl.d/99-freeze-capture.conf
JOURNALD=/etc/systemd/journald.conf.d/99-freeze-capture.conf

if [[ "${1:-}" == "--remove" ]]; then
  rm -f "$SYSCTL" "$JOURNALD"
  sysctl --system >/dev/null
  systemctl restart systemd-journald
  echo "Removed. Panic settings return to defaults on next boot."
  exit 0
fi

cat > "$SYSCTL" <<'CONF'
# OpenCity M0 stability gate: turn silent freezes into captured panics.
kernel.sysrq = 1
kernel.softlockup_panic = 1
kernel.hardlockup_panic = 1
kernel.hung_task_timeout_secs = 180
kernel.hung_task_panic = 1
kernel.panic_on_oops = 1
kernel.panic = 15
CONF

mkdir -p "$(dirname "$JOURNALD")"
cat > "$JOURNALD" <<'CONF'
[Journal]
SyncIntervalSec=5s
CONF

sysctl --system >/dev/null
systemctl restart systemd-journald
systemctl enable systemd-pstore.service >/dev/null 2>&1 || true

echo "Installed $SYSCTL and $JOURNALD"
P=/sys/module/efi_pstore/parameters/pstore_disable
if [[ -r $P && "$(cat $P)" == "Y" ]]; then
  echo "NOTE: EFI pstore is disabled. To keep panic logs across reboots, add"
  echo "      efi_pstore.pstore_disable=0  to GRUB_CMDLINE_LINUX_DEFAULT in /etc/default/grub,"
  echo "      then run: grub-mkconfig -o /boot/grub/grub.cfg   and reboot."
elif [[ -r $P ]]; then
  echo "EFI pstore is enabled; panic logs will appear in /var/lib/systemd/pstore after reboot."
else
  echo "NOTE: efi_pstore module not loaded; panic logs may not survive a reboot."
fi

#!/bin/sh
# One-time setup for a Linux (Raspberry Pi OS Lite / Ubuntu) frame-rate bench
# target, so it can be compared against QNX on the same board.
#
# Run ON THE TARGET with root:   sudo ./setup_linux_bench_host.sh
#
# Deliberately minimal: an X server and nothing else. No desktop, no window
# manager, no compositor — the closest Linux analogue to running on QNX Screen
# with the window manager bypassed. A compositor would add a copy per frame and
# make the comparison measure the compositor rather than JUCE.
set -eu

NTP_SERVER=${NTP_SERVER:-time.rim.net}

echo "=== 1/4  Clock ==="
# The Pi has no RTC, and this network blocks public NTP pools, so an unsynced
# clock drifts far enough to break apt signature verification ("Not live until
# ..."), which then silently serves stale indexes and 404s on packages.
if [ -f /etc/systemd/timesyncd.conf ]; then
    if grep -qE '^#?NTP=' /etc/systemd/timesyncd.conf; then
        sed -i "s|^#\?NTP=.*|NTP=${NTP_SERVER}|" /etc/systemd/timesyncd.conf
    else
        printf 'NTP=%s\n' "$NTP_SERVER" >> /etc/systemd/timesyncd.conf
    fi
    timedatectl set-ntp false || true
    timedatectl set-ntp true  || true
    echo "NTP server set to ${NTP_SERVER}"
else
    echo "timesyncd.conf not found — skipping (set the clock by hand)"
fi

echo "=== 2/4  Packages ==="
apt-get update
DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    xserver-xorg \
    xserver-xorg-legacy \
    xinit \
    fonts-dejavu-core      # Lite ships no fonts at all; without one the FPS overlay is blank

echo "=== 3/4  Allow X to start from a non-console session ==="
# The bench is driven over SSH, and Debian's default (allowed_users=console)
# rejects that with "only console users are allowed to run the X server".
#
# needs_root_rights alone is not enough: modern Debian ships a rootless Xorg
# that is not setuid, so it cannot open a VT and dies with
#   xf86OpenConsole: Cannot open virtual console N (Permission denied)
# xserver-xorg-legacy (installed above) provides the setuid wrapper that makes
# needs_root_rights meaningful. A deliberate loosening, appropriate for a
# dedicated test board and not for a general-purpose machine.
printf 'allowed_users=anybody\nneeds_root_rights=yes\n' > /etc/X11/Xwrapper.config

echo "=== 4/4  Verify ==="
echo "xinit:  $(command -v xinit  || echo MISSING)"
echo "Xorg:   $(command -v Xorg   || echo MISSING)"
echo "fonts:  $(fc-list 2>/dev/null | wc -l)"
echo "arch:   $(uname -m)"
timedatectl 2>/dev/null | grep -iE 'System clock|NTP service' || true

echo ""
echo "Done. Clock sync can take a minute; re-check with:  timedatectl"

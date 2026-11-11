#!/system/bin/sh
# Read-only device audit for Fahim OS Mobile.
# Intentionally excludes serial numbers, IMEI, phone numbers, SIM IDs and accounts.

set -u

read_prop() {
    key="$1"
    value="$(getprop "$key" 2>/dev/null || true)"
    [ -n "$value" ] || value="<empty>"
    printf '%-34s %s\n' "$key" "$value"
}

echo "=== FAHIM OS MOBILE: TARGET AUDIT ==="
echo "This report intentionally omits personal hardware/account identifiers."
echo

echo "[Product identity]"
read_prop ro.product.model
read_prop ro.product.device
read_prop ro.product.name
read_prop ro.product.mod_device
read_prop ro.product.vendor.device
read_prop ro.product.vendor.name
read_prop ro.product.product.device
read_prop ro.product.odm.device

echo
echo "[Hardware / ABI]"
read_prop ro.hardware
read_prop ro.boot.hardware
read_prop ro.board.platform
read_prop ro.soc.model
read_prop ro.product.cpu.abi
read_prop ro.product.cpu.abilist

echo
echo "[Build / firmware]"
read_prop ro.build.version.release
read_prop ro.build.version.sdk
read_prop ro.build.display.id
read_prop ro.build.version.incremental
read_prop ro.vendor.build.version.release
read_prop ro.vendor.build.version.sdk
read_prop ro.boot.slot_suffix
read_prop ro.boot.dynamic_partitions

echo
echo "[Kernel]"
uname -m 2>/dev/null || true
uname -r 2>/dev/null || true

echo
echo "=== END OF REPORT ==="

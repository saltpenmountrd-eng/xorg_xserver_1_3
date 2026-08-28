#!/bin/sh
#
# penmount-setup -- install/uninstall the "penmount" Xorg input driver's
# configuration in /etc/X11/xorg.conf, and (on install) copy
# penmount_drv.so into the Xorg input module directory.
#
# Usage:
#   penmount-setup.sh install
#   penmount-setup.sh uninstall
#
# install:
#   1) Refuses to run if a penmount_drv.so is already present in the
#      Xorg module directory -- overwriting a module Xorg may already
#      have loaded can corrupt a running server. Run "uninstall" (which
#      removes that file) and reboot first, then "install" again.
#   2) Copies penmount_drv.so (expected next to this script) into the
#      Xorg input module directory.
#   3) In /etc/X11/xorg.conf:
#      - ServerLayout: if the "PenMount" InputDevice line is missing, adds
#        it; if present but commented out, uncomments it; if present and
#        already active, leaves it alone.
#      - InputDevice section: if a section with Identifier "PenMount" is
#        missing, appends one (Driver "penmount", Device
#        "/dev/input/event*", vendor "0x14e1", product "0x6000"); if one
#        already exists, leaves it untouched.
#   4) Prints a reminder to reboot.
#
# uninstall:
#   1) In /etc/X11/xorg.conf: removes the "PenMount" ServerLayout line and
#      the whole "PenMount" InputDevice section.
#   2) Removes penmount_drv.so from the Xorg module directory (unlinking a
#      file a running process still has mapped is safe on Linux -- it's
#      only *overwriting it in place* that risks corrupting a live X
#      server, which is why install refuses to do that instead).
#   3) Prints a reminder to reboot before running "install" again.
#
# Every edit to xorg.conf is preceded by a timestamped backup next to the
# original file.

set -eu

XORG_CONF="${XORG_CONF:-/etc/X11/xorg.conf}"

PENMOUNT_ID="PenMount"
PENMOUNT_DRIVER="penmount"
PENMOUNT_DEVICE_GLOB="/dev/input/event*"
PENMOUNT_VENDOR="0x14e1"
PENMOUNT_PRODUCT="0x6000"

DRV_SO_NAME="penmount_drv.so"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DRV_SO_SRC="${SCRIPT_DIR}/${DRV_SO_NAME}"

MODULE_DIR_CANDIDATES="/usr/lib/xorg/modules/input /usr/lib64/xorg/modules/input"

die() {
    echo "penmount-setup: $*" >&2
    exit 1
}

find_module_dir() {
    for d in $MODULE_DIR_CANDIDATES; do
        if [ -d "$d" ]; then
            echo "$d"
            return 0
        fi
    done
    return 1
}

require_conf() {
    [ -f "$XORG_CONF" ] || die "找不到 $XORG_CONF"
}

backup_conf() {
    ts="$(date +%Y%m%d%H%M%S)"
    cp -p "$XORG_CONF" "${XORG_CONF}.bak.${ts}"
    echo "已備份現有設定到 ${XORG_CONF}.bak.${ts}"
}

# ---------------------------------------------------------------------
# xorg.conf editing (awk does the real work; these are thin wrappers)
# ---------------------------------------------------------------------

edit_conf_install() {
    tmp="$(mktemp)"
    awk -v id="$PENMOUNT_ID" -v drv="$PENMOUNT_DRIVER" \
        -v dev="$PENMOUNT_DEVICE_GLOB" -v vendor="$PENMOUNT_VENDOR" \
        -v product="$PENMOUNT_PRODUCT" '
    BEGIN {
        in_sl = 0
        sl_found = 0
        in_id_section = 0
        id_is_penmount = 0
        id_section_found = 0
        buf_n = 0
    }

    # --- ServerLayout: add/uncomment the PenMount InputDevice line ----
    /^[ \t]*Section[ \t]+"ServerLayout"/ {
        in_sl = 1
        print
        next
    }
    in_sl && /^[ \t]*EndSection/ {
        if (!sl_found) {
            printf("\tInputDevice\t\"%s\"\t\"AlwaysCore\"\n", id)
        }
        in_sl = 0
        print
        next
    }
    in_sl {
        stripped = $0
        sub(/^[ \t]*#?[ \t]*/, "", stripped)
        if (stripped ~ ("InputDevice[ \t]+\"" id "\"")) {
            sl_found = 1
            print "\t" stripped
            next
        }
        print
        next
    }

    # --- InputDevice sections: detect whether a PenMount one exists ---
    /^[ \t]*Section[ \t]+"InputDevice"/ {
        in_id_section = 1
        id_is_penmount = 0
        buf_n = 0
        buf[buf_n++] = $0
        next
    }
    in_id_section {
        buf[buf_n++] = $0
        if ($0 ~ ("Identifier[ \t]+\"" id "\"")) {
            id_is_penmount = 1
        }
        if ($0 ~ /^[ \t]*EndSection/) {
            in_id_section = 0
            if (id_is_penmount) id_section_found = 1
            for (i = 0; i < buf_n; i++) print buf[i]
            next
        }
        next
    }

    { print }

    END {
        if (!id_section_found) {
            printf("\nSection \"InputDevice\"\n")
            printf("\tIdentifier\t\"%s\"\n", id)
            printf("\tDriver\t\t\"%s\"\n", drv)
            printf("\tOption\t\t\"Device\"\t\"%s\"\n", dev)
            printf("\tOption\t\t\"vendor\"\t\"%s\"\n", vendor)
            printf("\tOption\t\t\"product\"\t\"%s\"\n", product)
            printf("EndSection\n")
        }
    }
    ' "$XORG_CONF" > "$tmp"
    mv "$tmp" "$XORG_CONF"
    chmod 644 "$XORG_CONF"
}

edit_conf_uninstall() {
    tmp="$(mktemp)"
    awk -v id="$PENMOUNT_ID" '
    BEGIN { in_sl = 0; in_id_section = 0; id_is_penmount = 0; buf_n = 0 }

    /^[ \t]*Section[ \t]+"ServerLayout"/ { in_sl = 1; print; next }
    in_sl && /^[ \t]*EndSection/ { in_sl = 0; print; next }
    in_sl {
        stripped = $0
        sub(/^[ \t]*#?[ \t]*/, "", stripped)
        if (stripped ~ ("InputDevice[ \t]+\"" id "\"")) next
        print
        next
    }

    /^[ \t]*Section[ \t]+"InputDevice"/ {
        in_id_section = 1
        id_is_penmount = 0
        buf_n = 0
        buf[buf_n++] = $0
        next
    }
    in_id_section {
        buf[buf_n++] = $0
        if ($0 ~ ("Identifier[ \t]+\"" id "\"")) id_is_penmount = 1
        if ($0 ~ /^[ \t]*EndSection/) {
            in_id_section = 0
            if (!id_is_penmount) {
                for (i = 0; i < buf_n; i++) print buf[i]
            }
            next
        }
        next
    }

    { print }
    ' "$XORG_CONF" > "$tmp"
    mv "$tmp" "$XORG_CONF"
    chmod 644 "$XORG_CONF"
}

# ---------------------------------------------------------------------
# commands
# ---------------------------------------------------------------------

cmd_install() {
    require_conf

    moddir="$(find_module_dir)" || die "找不到 Xorg input 模組目錄(試過:$MODULE_DIR_CANDIDATES)"
    [ -f "$DRV_SO_SRC" ] || die "找不到 $DRV_SO_SRC,請先在同一個目錄下 make 產生 $DRV_SO_NAME"

    if [ -f "${moddir}/${DRV_SO_NAME}" ]; then
        die "偵測到 ${moddir}/${DRV_SO_NAME} 已存在。由於驅動載入機制的限制,若 Xorg 已經載入過舊版,直接覆蓋可能無法正確套用新版本(甚至讓執行中的 X 不穩定)。請先執行:$0 uninstall,重新開機後再執行 $0 install。"
    fi

    cp -p "$DRV_SO_SRC" "${moddir}/${DRV_SO_NAME}"
    echo "已安裝 ${moddir}/${DRV_SO_NAME}"

    backup_conf
    edit_conf_install
    echo "已更新 $XORG_CONF"

    echo
    echo "設定完成,請重新開機以套用變更。"
}

cmd_uninstall() {
    require_conf

    backup_conf
    edit_conf_uninstall
    echo "已從 $XORG_CONF 移除 PenMount 設定"

    if moddir="$(find_module_dir)" && [ -f "${moddir}/${DRV_SO_NAME}" ]; then
        rm -f "${moddir}/${DRV_SO_NAME}"
        echo "已移除 ${moddir}/${DRV_SO_NAME}"
    fi

    echo
    echo "解除安裝完成。由於驅動載入機制的限制,請務必重新開機後,再執行 $0 install 安裝新版本。"
}

case "${1:-}" in
    install)   cmd_install ;;
    uninstall) cmd_uninstall ;;
    *)
        echo "usage: $0 {install|uninstall}" >&2
        exit 1
        ;;
esac

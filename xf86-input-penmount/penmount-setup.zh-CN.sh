#!/bin/sh
#
# penmount-setup -- 安装/卸载 "penmount" Xorg 输入驱动在 /etc/X11/xorg.conf
# 中的相关配置,并在安装时把 penmount_drv.so 复制到 Xorg 的 input 模块目录。
#
# 用法:
#   penmount-setup.sh install
#   penmount-setup.sh uninstall
#
# install(安装):
#   1) 如果 Xorg 模块目录下已经存在 penmount_drv.so,会直接拒绝执行——
#      直接覆盖一个 Xorg 可能已经加载过的模块文件,有可能导致正在运行
#      的 X 不稳定甚至崩溃。请先执行 "uninstall"(会移除该文件),重启
#      系统后再执行一次 "install"。
#   2) 把 penmount_drv.so(预期跟本脚本放在同一目录下)复制到 Xorg 的
#      input 模块目录。
#   3) 在 /etc/X11/xorg.conf 中:
#      - ServerLayout 区段:如果缺少 "PenMount" 这一行 InputDevice 设置,
#        会自动加上;如果存在但被注释掉,会自动取消注释;如果已经存在
#        且生效中,不会做任何改动。
#      - InputDevice 区段:如果没有 Identifier 为 "PenMount" 的区段,会
#        自动新增一段(Driver "penmount"、Device "/dev/input/event*"、
#        vendor "0x14e1"、product "0x6000");如果已经存在,不会改动。
#   4) 结束时提示需要重启系统。
#
# uninstall(卸载):
#   1) 在 /etc/X11/xorg.conf 中,移除 "PenMount" 的 ServerLayout 那一行,
#      以及整个 "PenMount" 的 InputDevice 区段。
#   2) 移除 Xorg 模块目录下的 penmount_drv.so(在 Linux 上,删除一个正在
#      被进程使用中的文件是安全的;真正有风险的是"直接覆盖文件内容",
#      这也是为什么 install 遇到文件已存在时会拒绝执行,而不是卸载时)。
#   3) 结束时提示务必重启系统后,再执行一次 "install"。
#
# 每次修改 xorg.conf 之前,都会先在同一目录下备份一份带时间戳的原始文件。

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
    echo "已备份现有配置到 ${XORG_CONF}.bak.${ts}"
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
}

# ---------------------------------------------------------------------
# commands
# ---------------------------------------------------------------------

cmd_install() {
    require_conf

    moddir="$(find_module_dir)" || die "找不到 Xorg input 模块目录(尝试过:$MODULE_DIR_CANDIDATES)"
    [ -f "$DRV_SO_SRC" ] || die "找不到 $DRV_SO_SRC,请先在同一个目录下执行 make 生成 $DRV_SO_NAME"

    if [ -f "${moddir}/${DRV_SO_NAME}" ]; then
        die "检测到 ${moddir}/${DRV_SO_NAME} 已存在。由于驱动加载机制的限制,若 Xorg 已经加载过旧版本,直接覆盖可能无法正确应用新版本(甚至导致正在运行的 X 不稳定)。请先执行:$0 uninstall,重启系统后再执行 $0 install。"
    fi

    cp -p "$DRV_SO_SRC" "${moddir}/${DRV_SO_NAME}"
    echo "已安装 ${moddir}/${DRV_SO_NAME}"

    backup_conf
    edit_conf_install
    echo "已更新 $XORG_CONF"

    echo
    echo "配置完成,请重启系统以应用更改。"
}

cmd_uninstall() {
    require_conf

    backup_conf
    edit_conf_uninstall
    echo "已从 $XORG_CONF 移除 PenMount 配置"

    if moddir="$(find_module_dir)" && [ -f "${moddir}/${DRV_SO_NAME}" ]; then
        rm -f "${moddir}/${DRV_SO_NAME}"
        echo "已移除 ${moddir}/${DRV_SO_NAME}"
    fi

    echo
    echo "卸载完成。由于驱动加载机制的限制,请务必重启系统后,再执行 $0 install 安装新版本。"
}

case "${1:-}" in
    install)   cmd_install ;;
    uninstall) cmd_uninstall ;;
    *)
        echo "用法: $0 {install|uninstall}" >&2
        exit 1
        ;;
esac

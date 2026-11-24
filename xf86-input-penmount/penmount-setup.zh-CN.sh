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
#        vendor "0x14e1"、product "0x6000"、Debug "off");如果已经存在,
#        不会改动。把 InputDevice 区段里的 Debug 选项改成 "on",可以让
#        驱动在 Xorg.log 中输出详细的逐笔触控坐标调试信息([calib-debug])。
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
PENMOUNT_DEVICE_GLOB="${PENMOUNT_DEVICE_GLOB:-/dev/input/event*}"
PENMOUNT_VENDOR="${PENMOUNT_VENDOR:-0x14e1}"
PENMOUNT_PRODUCT="${PENMOUNT_PRODUCT:-0x6000}"
PENMOUNT_DEBUG="${PENMOUNT_DEBUG:-off}"
PENMOUNT_CLOSEDELAY="${PENMOUNT_CLOSEDELAY:-0}"

DRV_SO_NAME="penmount_drv.so"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DRV_SO_SRC="${SCRIPT_DIR}/${DRV_SO_NAME}"

MODULE_DIR_CANDIDATES="${MODULE_DIR_CANDIDATES:-/usr/lib/xorg/modules/input /usr/lib64/xorg/modules/input}"

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
# 设备枚举与配置文件读取
# ---------------------------------------------------------------------

detect_penmount_pid() {
    # 1. 枚举 /proc/bus/input/devices
    if [ -f /proc/bus/input/devices ]; then
        pid="$(awk '
        /^[ \t]*I:[ \t]+/ {
            vendor = ""; product = ""
            for (i = 2; i <= NF; i++) {
                if ($i ~ /^[Vv][Ee][Nn][Dd][Oo][Rr]=/) {
                    split($i, a, "="); vendor = tolower(a[2])
                }
                if ($i ~ /^[Pp][Rr][Oo][Dd][Uu][Cc][Tt]=/) {
                    split($i, a, "="); product = tolower(a[2])
                }
            }
            if (vendor ~ /^(0x)?14e1$/ && product != "") {
                sub(/^0x/, "", product)
                printf("0x%s\n", product)
                exit
            }
        }
        ' /proc/bus/input/devices)"
        if [ -n "$pid" ]; then
            echo "$pid"
            return 0
        fi
    fi

    # 2. 枚举 /sys/class/input/input*/id/
    for vpath in /sys/class/input/input*/id/vendor; do
        [ -f "$vpath" ] || continue
        vid="$(cat "$vpath" 2>/dev/null | tr '[:upper:]' '[:lower:]' | sed 's/^0x//')"
        if [ "$vid" = "14e1" ]; then
            ppath="${vpath%vendor}product"
            if [ -f "$ppath" ]; then
                pid="$(cat "$ppath" 2>/dev/null | tr '[:upper:]' '[:lower:]' | sed 's/^0x//')"
                if [ -n "$pid" ]; then
                    echo "0x$pid"
                    return 0
                fi
            fi
        fi
    done

    # 3. 枚举 /proc/bus/usb/devices
    if [ -f /proc/bus/usb/devices ]; then
        pid="$(awk '
        /^P:[ \t]+/ {
            vendor = ""; product = ""
            for (i = 2; i <= NF; i++) {
                if ($i ~ /^[Vv]endor=/) { split($i, a, "="); vendor = tolower(a[2]); }
                if ($i ~ /^[Pp]rodID=/) { split($i, a, "="); product = tolower(a[2]); }
            }
            if (vendor ~ /^(0x)?14e1$/ && product != "") {
                sub(/^0x/, "", product)
                printf("0x%s\n", product)
                exit
            }
        }
        ' /proc/bus/usb/devices)"
        if [ -n "$pid" ]; then
            echo "$pid"
            return 0
        fi
    fi

    # 4. 枚举 lsusb (若存在)
    if command -v lsusb >/dev/null 2>&1; then
        pid="$(lsusb 2>/dev/null | grep -i "14e1:" | head -n 1 | sed -n 's/.*14e1:\([0-9a-fA-F]\{4\}\).*/\1/p' | tr '[:upper:]' '[:lower:]')"
        if [ -n "$pid" ]; then
            echo "0x$pid"
            return 0
        fi
    fi

    return 1
}

parse_ini() {
    ini_file="$1"
    [ -f "$ini_file" ] || return 0
    while IFS='=' read -r key val || [ -n "$key" ]; do
        key="$(echo "$key" | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//')"
        case "$key" in
            \#*|\;*|\[*|"") continue ;;
        esac
        val="$(echo "$val" | sed -e 's/[[:space:]]*[#;].*$//' -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//')"
        case "$val" in
            \"*\") val="${val#\"}"; val="${val%\"}" ;;
            \'*\') val="${val#\'}"; val="${val%\'}" ;;
        esac
        norm_key="$(echo "$key" | tr '[:upper:]' '[:lower:]')"
        case "$norm_key" in
            debug)
                norm_val="$(echo "$val" | tr '[:upper:]' '[:lower:]')"
                case "$norm_val" in
                    1|on|true|yes)  CFG_DEBUG="on" ;;
                    0|off|false|no) CFG_DEBUG="off" ;;
                    *)              CFG_DEBUG="$val" ;;
                esac
                ;;
            closedelay|close_delay)
                CFG_CLOSEDELAY="$val"
                ;;
            product|pid)
                case "$val" in
                    0x*|0X*) CFG_PRODUCT="$(echo "$val" | tr '[:upper:]' '[:lower:]')" ;;
                    *)       CFG_PRODUCT="0x$(echo "$val" | tr '[:upper:]' '[:lower:]')" ;;
                esac
                ;;
            vendor|vid)
                case "$val" in
                    0x*|0X*) CFG_VENDOR="$(echo "$val" | tr '[:upper:]' '[:lower:]')" ;;
                    *)       CFG_VENDOR="0x$(echo "$val" | tr '[:upper:]' '[:lower:]')" ;;
                esac
                ;;
            device)
                CFG_DEVICE="$val"
                ;;
        esac
    done < "$ini_file"
}

# ---------------------------------------------------------------------
# xorg.conf editing (awk does the real work; these are thin wrappers)
# ---------------------------------------------------------------------

edit_conf_install() {
    tmp="$(mktemp)"
    awk -v id="$PENMOUNT_ID" -v drv="$PENMOUNT_DRIVER" \
        -v dev="$PENMOUNT_DEVICE_GLOB" -v vendor="$PENMOUNT_VENDOR" \
        -v product="$PENMOUNT_PRODUCT" -v debug="$PENMOUNT_DEBUG" \
        -v closedelay="$PENMOUNT_CLOSEDELAY" '
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
            if (id_is_penmount) {
                id_section_found = 1
                has_product = 0; has_debug = 0; has_closedelay = 0
                for (i = 0; i < buf_n - 1; i++) {
                    line = buf[i]
                    if (line ~ /^[ \t]*Option[ \t]+"product"/) {
                        printf("\tOption\t\t\"product\"\t\"%s\"\n", product)
                        has_product = 1
                    } else if (line ~ /^[ \t]*Option[ \t]+"Debug"/) {
                        printf("\tOption\t\t\"Debug\"\t\t\"%s\"\n", debug)
                        has_debug = 1
                    } else if (line ~ /^[ \t]*Option[ \t]+"CloseDelay"/) {
                        printf("\tOption\t\t\"CloseDelay\"\t\"%s\"\n", closedelay)
                        has_closedelay = 1
                    } else {
                        print line
                    }
                }
                if (!has_product) printf("\tOption\t\t\"product\"\t\"%s\"\n", product)
                if (!has_debug) printf("\tOption\t\t\"Debug\"\t\t\"%s\"\n", debug)
                if (!has_closedelay) printf("\tOption\t\t\"CloseDelay\"\t\"%s\"\n", closedelay)
                print buf[buf_n - 1]
            } else {
                for (i = 0; i < buf_n; i++) print buf[i]
            }
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
            printf("\tOption\t\t\"Debug\"\t\t\"%s\"\n", debug)
            printf("\tOption\t\t\"CloseDelay\"\t\"%s\"\n", closedelay)
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

    moddir="$(find_module_dir)" || die "找不到 Xorg input 模块目录(尝试过:$MODULE_DIR_CANDIDATES)"
    [ -f "$DRV_SO_SRC" ] || die "找不到 $DRV_SO_SRC,请先在同一个目录下执行 make 生成 $DRV_SO_NAME"

    if [ -f "${moddir}/${DRV_SO_NAME}" ]; then
        die "检测到 ${moddir}/${DRV_SO_NAME} 已存在。由于驱动加载机制的限制,若 Xorg 已经加载过旧版本,直接覆盖可能无法正确应用新版本(甚至导致正在运行的 X 不稳定)。请先执行:$0 uninstall,重启系统后再执行 $0 install。"
    fi

    # 1. 扫描输入设备，检测首个 VID=14e1 设备的 PID
    detected_pid=""
    if detected_pid="$(detect_penmount_pid)"; then
        echo "检测到 PenMount 触摸设备: VID=0x14e1, PID=$detected_pid"
        PENMOUNT_PRODUCT="$detected_pid"
    else
        echo "未在当前系统检测到已连接的 PenMount (VID=0x14e1) 设备，使用默认 PID: $PENMOUNT_PRODUCT"
    fi

    # 2. 读取配置文件 (若存在 config.ini)
    CONFIG_INI="${PENMOUNT_CONFIG:-}"
    if [ -z "$CONFIG_INI" ]; then
        if [ -f "${SCRIPT_DIR}/config.ini" ]; then
            CONFIG_INI="${SCRIPT_DIR}/config.ini"
        elif [ -f "./config.ini" ]; then
            CONFIG_INI="./config.ini"
        fi
    fi

    CFG_DEBUG=""
    CFG_CLOSEDELAY=""
    CFG_PRODUCT=""
    CFG_VENDOR=""
    CFG_DEVICE=""

    if [ -n "$CONFIG_INI" ] && [ -f "$CONFIG_INI" ]; then
        echo "读取配置文件: $CONFIG_INI"
        parse_ini "$CONFIG_INI"
        [ -n "$CFG_DEBUG" ] && PENMOUNT_DEBUG="$CFG_DEBUG" && echo "  - 预设 Debug: $PENMOUNT_DEBUG"
        [ -n "$CFG_CLOSEDELAY" ] && PENMOUNT_CLOSEDELAY="$CFG_CLOSEDELAY" && echo "  - 预设 CloseDelay: $PENMOUNT_CLOSEDELAY"
        [ -n "$CFG_PRODUCT" ] && PENMOUNT_PRODUCT="$CFG_PRODUCT" && echo "  - 预设 product (PID): $PENMOUNT_PRODUCT"
        [ -n "$CFG_VENDOR" ] && PENMOUNT_VENDOR="$CFG_VENDOR" && echo "  - 预设 vendor (VID): $PENMOUNT_VENDOR"
        [ -n "$CFG_DEVICE" ] && PENMOUNT_DEVICE_GLOB="$CFG_DEVICE" && echo "  - 预设 Device: $PENMOUNT_DEVICE_GLOB"
    fi

    echo "写入 xorg.conf 的配置:"
    echo "  Driver:     $PENMOUNT_DRIVER"
    echo "  Device:     $PENMOUNT_DEVICE_GLOB"
    echo "  vendor:     $PENMOUNT_VENDOR"
    echo "  product:    $PENMOUNT_PRODUCT"
    echo "  Debug:      $PENMOUNT_DEBUG"
    echo "  CloseDelay: $PENMOUNT_CLOSEDELAY"

    cp -p "$DRV_SO_SRC" "${moddir}/${DRV_SO_NAME}"
    echo "已安装 ${moddir}/${DRV_SO_NAME}"

    backup_conf
    edit_conf_install
    rm -f /etc/penmount/CalibOK /etc/penmount/CalibStart
    echo "已更新 $XORG_CONF"

    echo
    echo "配置完成,请重启系统以应用更改。"
}

cmd_uninstall() {
    require_conf

    backup_conf
    edit_conf_uninstall
    rm -f /etc/penmount/CalibOK /etc/penmount/CalibStart
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

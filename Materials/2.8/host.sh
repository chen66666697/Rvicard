#!/bin/bash
# 功能：USB 设备树（dr_mode=host）永久生效
# 临时文件路径：/tmp/，Boot 分区：/dev/mtdblock3

# ============================== 1. 全局配置（可修改）==============================
# USB 设备树配置（修复：删除嵌套注释，仅保留单行注释）
export USB_FDT_CONTENT="
/dts-v1/;
/plugin/;

&{/usbdrd/usb@ffb00000}{
    dr_mode = \"host\"; /* 设为 USB 主机模式 */
};
"

# 临时文件（全部放 /tmp/）
HDR_DTB="/tmp/fdt_hdr.dtb"
RAW_DTB="/tmp/fdt_raw.dtb"
OVERLAY_DTS="/tmp/fdt_overlay.dts"
OVERLAY_DTBO="/tmp/fdt_overlay.dtbo"
HDR_OVERLAY_DTS="/tmp/fdt_hdr_overlay.dts"
HDR_OVERLAY_DTBO="/tmp/fdt_hdr_overlay.dtbo"

# Boot 分区（已适配你的 /dev/mtdblock3）
BOOT_PART="/dev/mtdblock3"


# ============================== 2. 前置检查 ===============================
echo -e "==================================== 前置检查 ====================================\n"

# 检查 root 权限
if [ "$(id -u)" -ne 0 ]; then
    echo "[错误] 需 root 权限，执行：sudo -i 后重试"
    exit 1
fi

# 检查核心工具（嵌入式系统常用工具集）
needed_tools=("dtc" "fdtdump" "fdtoverlay" "dd" "sha256sum" "ls" "wc")
for tool in "${needed_tools[@]}"; do
    if ! command -v "$tool" &>/dev/null; then
        echo "[错误] 缺少工具：$tool（需安装 device-tree-compiler 等）"
        exit 1
    fi
done

# 检查 Boot 分区是否存在
if [ ! -b "$BOOT_PART" ]; then
    echo "[错误] Boot 分区 $BOOT_PART 不存在！"
    ls -l /dev/mtdblock* # 显示所有 mtd 设备，方便确认
    exit 1
fi

echo "[成功] 前置检查通过"


# ============================== 3. 提取原始 DTB（不用 stat，用 ls + wc 替代）==============================
echo -e "\n==================================== 提取原始 DTB ===================================="

# 步骤1：提取 DTB 头部（前 2048 字节）
echo "[1/3] 提取 DTB 头部到 $HDR_DTB"
dd if="$BOOT_PART" of="$HDR_DTB" bs=1 skip=0 count=2048 >/dev/null 2>&1

# 检查头部文件是否有效（用 ls -l 取大小，替代 stat）
hdr_size=$(ls -la "$HDR_DTB" | awk '{print $5}')
if [ "$hdr_size" -ne 2048 ]; then
    echo "[错误] 头部提取失败，大小应为 2048 字节，实际：$hdr_size"
    exit 1
fi

# 步骤2：解析原始 DTB 大小（从头部获取）
echo "[2/3] 解析原始 DTB 大小"
raw_size_hex=$(fdtdump "$HDR_DTB" | grep -A 5 "fdt {" | grep "data-size" | awk '{print $3}' | tr -d ';<>')
raw_size=$(printf "%d\n" "$raw_size_hex")
if [ -z "$raw_size" ] || [ "$raw_size" -eq 0 ]; then
    echo "[错误] 无法解析 DTB 大小"
    exit 1
fi
echo "[信息] 原始 DTB 大小：$raw_size 字节"

# 步骤3：提取完整 DTB
echo "[3/3] 提取完整 DTB 到 $RAW_DTB"
dd if="$BOOT_PART" of="$RAW_DTB" bs=1 skip=2048 count="$raw_size" >/dev/null 2>&1

# 检查完整 DTB 大小（用 ls -l 取大小）
extracted_size=$(ls -la "$RAW_DTB" | awk '{print $5}')
if [ "$extracted_size" -ne "$raw_size" ]; then
    echo "[错误] DTB 提取不完整，应为 $raw_size 字节，实际：$extracted_size"
    exit 1
fi

echo "[成功] 原始 DTB 提取完成"


# ============================== 4. 编译并叠加 USB 配置 ===============================
echo -e "\n==================================== 叠加 USB 配置 ===================================="

# 步骤1：写入 DTS 配置
echo "[1/5] 写入 USB 配置到 $OVERLAY_DTS"
echo "$USB_FDT_CONTENT" >"$OVERLAY_DTS"
if [ ! -f "$OVERLAY_DTS" ]; then
    echo "[错误] 无法写入 DTS 文件，检查 /tmp/ 权限"
    exit 1
fi

# 步骤2：编译 DTS 为 DTBO（核心步骤，解决之前语法错误）
echo "[2/5] 编译 DTS 为 DTBO"
dtc -I dts -O dtb "$OVERLAY_DTS" -o "$OVERLAY_DTBO"
if [ $? -ne 0 ] || [ ! -f "$OVERLAY_DTBO" ]; then
    echo "[错误] DTS 编译失败！当前配置内容："
    cat "$OVERLAY_DTS" # 显示配置，方便排查
    exit 1
fi

# 步骤3：叠加 DTBO 到原始 DTB
echo "[3/5] 叠加 DTBO 到原始 DTB"
fdtoverlay -i "$RAW_DTB" -o "$RAW_DTB" "$OVERLAY_DTBO" >/dev/null 2>&1
if [ $? -ne 0 ]; then
    echo "[错误] 叠加失败！可能节点不存在"
    echo "查看原始 DTB 中的 USB 节点：fdtdump $RAW_DTB | grep -A 5 'usbdrd'"
    exit 1
fi

# 步骤4：校验叠加后大小（避免覆盖内核）
echo "[4/5] 校验 DTB 大小"
overlayed_size=$(ls -la "$RAW_DTB" | awk '{print $5}')
# 解析内核偏移地址（判断是否会覆盖）
kernel_offset_hex=$(fdtdump "$HDR_DTB" | grep -A 2 "kernel {" | grep "data-position" | sed -n 's/.*<\(0x[0-9a-fA-F]*\)>.*/\1/p')
fdt_offset_hex=$(fdtdump "$HDR_DTB" | grep -A 2 "fdt {" | grep "data-position" | sed -n 's/.*<\(0x[0-9a-fA-F]*\)>.*/\1/p')
# 十六进制转十进制
kernel_offset=$((kernel_offset_hex))
fdt_offset=$((fdt_offset_hex))
available_space=$((kernel_offset - fdt_offset))

if [ "$overlayed_size" -gt "$available_space" ]; then
    echo "[错误] DTB 过大！可用空间：$available_space 字节，当前：$overlayed_size"
    exit 1
fi
echo "[信息] 叠加后大小：$overlayed_size 字节，可用空间充足"

# 步骤5：写回 Boot 分区
echo "[5/5] 写回 DTB 到 $BOOT_PART"
dd if="$RAW_DTB" of="$BOOT_PART" bs=1 seek=2048 count="$overlayed_size" >/dev/null 2>&1
if [ $? -ne 0 ]; then
    echo "[错误] 写回失败！尝试重新挂载 Boot 分区为可写："
    echo "mount -o remount,rw $BOOT_PART"
    exit 1
fi

echo "[成功] USB 配置叠加完成"


# ============================== 5. 更新 DTB 头部（含校验值）==============================
echo -e "\n==================================== 更新 DTB 头部 ================================"

# 步骤1：计算大小和 SHA256
echo "[1/3] 计算 DTB 信息"
fdt_size=$overlayed_size
fdt_size_hex=$(printf "%x\n" "$fdt_size")
# 计算 SHA256 并转换格式
sha_raw=$(sha256sum "$RAW_DTB" | awk '{print $1}')
fdt_hash=$(echo "$sha_raw" | sed -E 's/(..)/0x\1 /g')
echo "[信息] 大小（十六进制）：0x$fdt_size_hex，SHA256：$sha_raw"

# 步骤2：生成头部更新 DTS
echo "[2/3] 生成头部配置"
hdr_content="
/dts-v1/;
/plugin/;

&{/images/fdt}{
    data-size=<0x$fdt_size_hex>;
    hash{
        value=<$fdt_hash>;
    };
};
"
echo "$hdr_content" >"$HDR_OVERLAY_DTS"
if [ ! -f "$HDR_OVERLAY_DTS" ]; then
    echo "[错误] 无法生成头部 DTS"
    exit 1
fi

# 步骤3：编译并写回头部
echo "[3/3] 编译头部并写回"
dtc -I dts -O dtb "$HDR_OVERLAY_DTS" -o "$HDR_OVERLAY_DTBO"
if [ $? -ne 0 ]; then
    echo "[错误] 头部 DTS 编译失败"
    exit 1
fi
# 叠加头部
fdtoverlay -i "$HDR_DTB" -o "$HDR_DTB" "$HDR_OVERLAY_DTBO" >/dev/null 2>&1
# 写回 Boot 分区（前 2048 字节）
dd if="$HDR_DTB" of="$BOOT_PART" bs=1 seek=0 count=2048 >/dev/null 2>&1
if [ $? -ne 0 ]; then
    echo "[错误] 头部写回失败"
    exit 1
fi

echo "[成功] 头部更新完成"


# ============================== 6. 清理临时文件 ===============================
echo -e "\n==================================== 清理临时文件 ================================"
temp_files=("$HDR_DTB" "$RAW_DTB" "$OVERLAY_DTS" "$OVERLAY_DTBO" "$HDR_OVERLAY_DTS" "$HDR_OVERLAY_DTBO")
for file in "${temp_files[@]}"; do
    if [ -f "$file" ]; then
        rm -f "$file"
        echo "[已清理] $file"
    fi
done


# ============================== 7. 完成提示 ===============================
echo -e "\n==================================== 操作完成！==============================="

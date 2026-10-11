# Magpie 0.6.10 Beta7

日期：2026-10-11。本地测试版本，继承 Beta6 `556d4ac2` 的全部修复。

## 中文

- 帧率与刷新增加全局简单／高级模式，分组显示内容帧率、静态画面、光标与 VRR；保留高级偏好和已有显式重复检测设置。新配置的全局重复检测默认关闭。
- 启用 RTSS 管理后，自动准备 RTSS 与 Magpie 自身配置，再应用当前目标的限帧；失败、取消、重试和结束恢复按会话处理。MSI Afterburner 为可选工具。
- 更新刷新页面：四处帧率控件同行排列、补齐分组图标，简单／高级切换与恢复默认置于标题区。简单模式恢复可见设置，同时保留隐藏的高级偏好。
- RTSS 使用独立状态标题、说明和行内管理区，区分当前目标、工具状态与手动操作结果；重新检测只读检查，应用限帧仍由当前会话重试执行。
- 原 DLSS FG 效果接入固定版本的社区兼容运行库，保持 2×～4×与旧配置；增加文件身份校验和失败升级回滚。
- 新增实验性 FSR 3 FG：SDR、固定 2×，默认外部运动估计与重复帧过滤关闭。HDR 与不支持的几何组合明确拒绝。
- 同步 XeSS FG／VFG 审阅记录与 Beta6 已有默认值；VFG 保留性能／均衡两档。

各 GPU 架构的 DLSS 社区路由、FSR3 生成画质／成本、真实游戏和 HDR／VRR 显示仍需逐项验收。首次集成结果见工作区 `reports/0610-beta7-integration-20261011`，本次刷新／RTSS 更新的构建与回归结果见 `reports/0610-beta7-refresh-update-20261011`。实际 WinUI 布局、DPI 和 RTSS 目标帧率仍需原生验收。

## English

- Adds global Simple/Advanced refresh settings with grouped controls and preserved advanced preferences. New configurations default to no global duplicate detection.
- Prepares RTSS automatically when session management is enabled, with bounded startup, cancellation, retry and restoration. Afterburner remains optional.
- Aligns the four rate controls in rows, adds group icons, and places the mode switch and reset action in the header. Simple-mode reset preserves hidden advanced preferences.
- Separates the active RTSS target status from tool inventory and manual management results. Rechecking is read-only; applying a frame cap still requires the current session's retry action.
- Integrates the pinned community runtime into the existing DLSS FG effect, retaining 2x–4x and saved settings, with file verification and update rollback.
- Adds experimental SDR-only, fixed-2x FSR 3 FG. External motion estimation and duplicate filtering default to off. Unsupported HDR or geometry combinations are rejected.
- Retains all Beta6 fixes and updates stale XeSS FG/VFG review metadata.

GPU architecture coverage, generated-image quality, performance, real games and display behavior remain separate acceptance work. This is a local test release.

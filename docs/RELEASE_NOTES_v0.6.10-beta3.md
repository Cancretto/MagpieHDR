# Magpie Experimental 0.6.10 Beta3

本地测试版本，继承完整 Beta2（`6e50812a`），新增截至 2026-10-05 已提交的 RTSS 目标应用限帧开发。公开更新入口仍为 0.6.9。

## 中文

### RTSS 目标应用限帧（实验）

- “配置文件 → 帧率与刷新”在内容帧率之前新增目标窗口帧率限制。默认“不管理 RTSS 配置”；可选择管理 Global 或目标应用配置。同一可执行文件共享 RTSS 应用配置。
- 自定义限帧采用 15–360 的整数；同步内容目标使用 Renderer 当前解析的基础帧率并四舍五入，补帧倍率不直接作为源应用 FPS。跟随源时同步选项不可用，保留已保存的偏好。
- 工具菜单提供 RTSS 和 MSI Afterburner 的显式启动、状态与重新检测；不会自动安装或启动工具。RTSS 接口异常或配置失败不阻止正常缩放。
- 管理在缩放开始时生效。页面修改供下次开始使用；切换为不管理会释放当前管理。默认在真正结束时有界尝试一次恢复原限帧表达，也可选择保留修改。
- 保存后核对接口和磁盘读回；已有目标保留其他字段，新建目标使用 Low 检测级别；外部修改发生冲突时停止覆盖。崩溃或强制结束不能保证恢复。
- UWP 根据实际窗口和进程身份查找目标；身份不能可靠确认时跳过写入。状态“已写入”只表示配置核验，不表示 RTSS 已建立 Hook 或游戏实际限帧成功。

### 继承 Beta2

- UWP 窗口协作与参数焦点、输入所有权及会话退役保护。
- 一个默认关闭的 VRR 开关及普通 DXGI、DLSS 前端 DXGI、XeSS D3D12 代理的 NVIDIA 前台查询兼容。
- VRR 继续使用配置页面的右对齐紧凑模板和简体“关／开”状态文字。
- 性能分组常驻渲染显卡选择，保留暂时缺失设备的选择意图。
- 继承既有 DLSS／XeSS／FSR、Reflex、效果组、快捷键、工具栏及配置迁移功能。

## English

This local test version retains the complete Beta2 (`6e50812a`) and integrates the committed RTSS target-application frame-rate feature (`f5f534c2`). The public update entry remains at 0.6.9.

- **RTSS profile management:** add a section before Content Frame Rate under Frame Rate and Refresh. Leave management off by default; optionally manage Global or an executable-specific profile. Windows of the same executable share the RTSS application profile.
- **Rate selection:** accept custom integers from 15 to 360 FPS or round the renderer's effective base content target. Preserve the unavailable synchronization preference while following the source. Frame-generation output FPS is not the source-process limit.
- **Tools and status:** offer explicit RTSS/Afterburner launch and recheck commands without automatic installation or launch. RTSS failures do not prevent normal scaling.
- **Session lifecycle:** apply on scaling start, retain the session's original preference, and either keep changes or make one bounded restoration attempt when the session ends. Leaving management releases the active session. API and disk readback verify writes; conflicting external edits stop further overwrites. Forced termination or a crash may leave the last written value.
- **Target identity:** resolve the actual process, including supported UWP hosts, and skip unreliable targets. A verified configuration write does not establish hook injection or an effective in-game frame-rate limit.
- **Beta2 retained:** keep UWP/parameter input coordination, unified VRR/FG foreground compatibility, the aligned compact VRR switch, persistent rendering GPU selection and all previously integrated effects and controls.

## 来源与验收 / Source and acceptance

Beta3 基于 `release/0.6.10-beta2`，合入 RTSS `f5f534c26fddd0168ef453dfa6c041eb6f6c5993`。合并同时保留 VRR 与 RTSS 通知、源窗口协作头文件和两组多语言资源；未带入日常主目录的未提交工作，历史暂停 VRR 分支继续排除。

准确源码、构建选项和逐文件哈希以包内 `build-manifest.json` 为准。分支快照、C++ 回归、完整构建和打包检查记录位于工作区 `reports/0610-beta3-20261005/`。功能分支中的“未编译”说明保留原开发阶段记录。

Software tests and the full build are recorded separately from native whole-page/DPI acceptance, real RTSS write/restore transactions, hook injection, target-game FPS, GPU/input compatibility and physical G-SYNC/VRR activation. Those runtime and hardware checks remain pending for this local test version.

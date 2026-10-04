# Magpie Experimental 0.6.10 Beta2

本地测试版本，继承完整 Beta1，并整合 2026-10-04 已提交的三项新增开发。公开更新入口仍为 0.6.9。

## 中文

### 参数焦点与窗口协作

- 捕获窗口、已校验根窗口、参数宿主使用统一会话身份规则；独立根窗口不再被参数层当成外部程序。
- 编辑输入同时验证 RunId、真实前台和线程焦点，处理激活失败、外部失焦、源弹窗、旧会话及宿主销毁／重开。
- 参数宿主跟随输出实际层级与可见性；正常持有输入仍等待完整收尾，外部取消和源退役使用取消路径。
- 焦点交接保留内容历史、停止和重建边界。游戏失焦导致的暂停或后台限帧仍需按游戏实测。

### NVIDIA G-SYNC 前台兼容（实验）

- “帧率与刷新”新增独立、默认关闭的 NVIDIA 兼容选项，重新启用缩放后应用；标准 VRR 关闭时该偏好不生效。
- 首版限定 NVIDIA 呈现适配器、单输出无边框 DXGI、标准 tearing 路径有效且无补帧。目标驱动／图形模块的查询可见输出窗口，Magpie 自身业务继续取得真实前台。
- 参数交接、切出、隐藏和停止暂停或退休兼容状态；初始化、API Hook 和呈现失败采用会话回退。新增条件 MinHook 静态依赖与许可证，ARM64 排除该依赖。
- 诊断支持查询来源记录及短时真实前台参考对照。普通兼容选项保留源应用真实前台，不向源游戏注入。
- 该功能仍是待硬件验证的原型。Hook 命中、软件回归和构建成功均不表示 G-SYNC 已激活；FG、HDR、多输出与最终输出预算继续单列后续工作。

### 渲染显卡选择

- “配置文件 → 性能 → 渲染显卡”常驻显示，单卡也可选择“自动”或指定设备。
- 指定设备暂时缺失时保留选择意图并显示提示；主动选自动才清除指定身份，设备恢复后沿用原匹配规则。
- 无候选时禁用控件并给出提示；保留既有仅显示适配器过滤、配置结构与实际设备创建回退。

## English

This local test version retains Beta1 and integrates three committed developments from 2026-10-04. The public update entry remains at 0.6.9.

- **Parameter focus:** share validated capture/root/session identities, verify input ownership using the current run, real foreground and thread focus, follow output visibility/layering, and handle cancellation, host destruction and restart while retaining content-history and retirement boundaries.
- **NVIDIA G-SYNC foreground compatibility:** add a separate default-off option requiring standard VRR. The initial prototype targets a single borderless NVIDIA DXGI output without frame generation. Selected driver/graphics queries may see the output HWND while Magpie business logic retains the real foreground. Session suspension, retirement, failure fallback and diagnostic comparisons are included. A conditional static MinHook dependency and license are included; ARM64 excludes it. This remains a prototype awaiting hardware activation evidence.
- **Rendering GPU selection:** keep the card in the Performance group even with one GPU; retain explicit device intent during temporary absence, distinguish automatic selection, disable the zero-device control, and preserve existing filtering/configuration/runtime fallback behavior.

## 来源与验收 / Source and acceptance

继承 `release/0.6.10-beta1`（`e6b82596`）；整合 UWP `ba1130a5`、VRR `f1ee5e84`、显卡选择 `293b694c` 及本地主线中对应的已提交入口修订记录 `13adddb6`。两项前台规则的冲突按真实业务查询与新输入所有权共同保留，测试夹具随接口同步。

The package's `build-manifest.json` records its exact source commit, feature options and file hashes. Local integration audits record regression, full-build and package checks. Native product input, actual application/GPU compatibility and physical G-SYNC/VRR activation still require testing; branch handoff documents retain their historical development-stage results.

# RTSS 界面文案精简与控件样式对齐 TODO

日期：2026-10-09。分支：`feature/069-rtss-target-frame-rate`，源码基线 `30995270`。

将帧率选项统一命名为“与内容帧率同步”，移除相关防御性描述，并按现有配置页风格检查 RTSS 全部控件。本 TODO 承接[原功能文档](../design/20261005-v0.6.9-rtss-target-frame-rate-plan.md)和[开发清单](20261005-v0.6.9-rtss-target-frame-rate-TODO.md)，以下文案要求作为后续界面调整依据。

## 文案调整

- [ ] **T01** 简体中文 `Rtss_Rate_Sync.Content` 从“与内容帧率自动同步”改为“与内容帧率同步”。英文现值 `Sync with content frame rate` 已符合该命名；同步实际行为保持一致。
- [ ] **T02** 删除恢复策略说明中的“只尽力尝试一次”“修改可能保留”等防御性描述；删除说明后不留下空白说明行。
- [ ] **T03** 状态只表达当前结果，移除“实际限帧效果未确认”“缩放可以继续”“清理已结束”“本次不再调用”等额外解释。
- [ ] **T04** 同步说明只保留设置用途、必要的不可用原因及当前 FPS；删除重复保存提示和内部解析过程描述。
- [ ] **T05** 全局范围、已有独立配置和检测关闭使用简短事实描述；移除“可能包括 Magpie 及插帧输出”“全局上限可能不作用”等推测性长句。管理范围选项继续明确表达是否管理 RTSS。
- [ ] **T06** 错误文字精简为具体失败原因或必要的处理动作；接口状态、同步、恢复和冲突处理逻辑沿用现实现。
- [ ] **T07** 检查 20 份资源文件中的相关键和可访问名称，简体中文与英文同步精简；其他语言按现有翻译或英文回退策略处理。同步更新功能文档中的界面选项名称。

拟采用的主要文案：

| 资源键 | 当前文字 | 调整目标 |
| --- | --- | --- |
| `Rtss_Rate_Sync.Content` | 与内容帧率自动同步 | 与内容帧率同步 |
| `Rtss_TargetLimit.Description` | 开始缩放时管理 RTSS 配置；应用配置作用于同一可执行文件。 | 开始缩放时应用 RTSS 帧率设置。 |
| `Rtss_ExitPolicy.Description` | 恢复只尽力尝试一次；无法完成清理时修改可能保留。 | 移除说明 |
| `Rtss_Status_Written` | 已写入并请求重载配置，实际限帧效果未确认。 | 配置已更新。 |
| `Rtss_Status_Failed` | RTSS 操作失败，缩放可以继续。 | RTSS 操作失败。 |
| `Rtss_Status_RestoreFailed` | 已尝试恢复但未完成，清理已结束。 | 恢复失败。 |
| `Rtss_Status_Restored` | 已恢复原限帧状态或全局继承关系。 | 已恢复原帧率设置。 |
| `Rtss_Status_Kept` | 已按保留修改策略结束，不执行恢复。 | 已保留修改。 |
| `Rtss_Status_Conflict` | 配置已被外部修改，Magpie 不再覆盖。 | 配置已被外部修改。 |
| `Rtss_Status_NotRunning` | RTSS 未运行；本次请求等待工具启动。 | RTSS 未运行。 |
| `Rtss_Status_WaitingContent` | 没有固定的内容基础目标，本次不更新 RTSS 配置。 | 等待固定内容帧率。 |
| `Rtss_SyncAtStart` | 开始缩放时解析实际内容基础目标，不使用插帧后的帧率。 | 与当前内容帧率同步。 |
| `Rtss_CustomAtStart` | 开始缩放时自动应用；编辑保存到下次开始缩放。 | 开始缩放时应用。 |
| `Rtss_SyncUnavailable` | 内容跟随源，没有固定目标；仍可自定义 RTSS 上限。 | 内容跟随源时无法同步。 |
| `Rtss_ContentConstrained` | 采用当前捕获或兼容限制后的内容基础目标。 | 移除该额外说明，继续显示实际同步 FPS |
| `Rtss_GlobalNotice.Text` | 影响使用 RTSS 全局配置的应用，可能包括 Magpie 及插帧输出。 | 使用全局配置的应用共用此上限。 |
| `Rtss_TargetOverridesGlobal` | 目标已有独立配置，全局上限可能不作用于此目标。 | 目标使用独立配置。 |
| `Rtss_DetectionDisabled` | 已有配置的 RTSS 应用检测已关闭。 | RTSS 应用检测已关闭。 |

失败详情拟精简为：

| 资源键 | 调整目标 |
| --- | --- |
| `Rtss_Failure_Access` | RTSS 配置接口不可用。 |
| `Rtss_Failure_Target` | 无法获取目标应用。 |
| `Rtss_Failure_Read` | 读取配置失败。 |
| `Rtss_Failure_Write` | 保存配置失败。 |
| `Rtss_Failure_Restore` | 恢复配置失败。 |
| `Rtss_Failure_Interface` | RTSS 接口异常，请重启 Magpie。 |
| `Rtss_Failure_StartTimeout` | 工具启动超时。 |
| `Rtss_Failure_LibraryMissing` | RTSS 配置接口库缺失。 |
| `Rtss_Failure_Permission` | 访问被拒绝。 |

## 控件样式 review

参照同页的内容帧率、光标刷新、帧率与刷新高级选项，以及共享 SettingsCard、SettingsExpander 和普通操作按钮。保留现有四部分位置，复用现有主题与尺寸资源。

| 控件或区域 | 当前源码结论 | 待处理 |
| --- | --- | --- |
| 三个下拉框：管理范围、帧率模式、停止策略 | 均复用 `FrameRefreshComboBoxStyle`，225 DIP、最小宽度 0、垂直居中，接入现有下拉辅助 | 核对展开后的左右对齐、短文案及中英文选项显示 |
| 整数输入与 FPS | 复用 `FrameRefreshNumberBoxStyle` 和 225 DIP 整列；整数、行内步进、FPS 间距 8 DIP，与内容帧率输入一致 | 核对输入高度、基线、步进按钮、焦点和禁用状态 |
| 状态右侧工具入口与重新检测 | 两个按钮在 225 DIP Grid 中，间距 8 DIP；启动入口使用 DropDownButton，未声明与相邻输入一致的垂直对齐，也未声明局部普通按钮样式 | 重点核对高度、内边距、圆角、中性填充、边框、图标尺寸及两按钮对齐；需要修正时沿用现有普通操作按钮风格 |
| 工具弹出菜单 | 使用原生 MenuFlyout；项目页面中没有另一个 DropDownButton 作为直接参照 | 与配置列表的现有菜单对照字体、间距、明暗主题、位置和禁用反馈 |
| RTSS 主展开区与各设置行 | 使用共享 SettingsExpander/SettingsCard；设置卡片会由展开器应用子项样式 | 保留现有背景、分隔线和圆角规则，检查展开折叠后的行间距 |
| 嵌套的 RTSS 高级选项 | 是 SettingsExpander，而外层自动应用子项样式的分支仅处理 SettingsCard；内层未声明标题图标 | 重点核对与同页高级选项的层级、标题图标、左缩进、右侧箭头预留及内部下拉列；源码差异需要原生测量确认 |
| 状态说明与全局范围说明 | 状态文字来自卡片 Description；全局说明采用 Caption、次级文字色和 `Margin=16,4`，与同页条件提示一致 | 精简后检查多行行高、次级文字颜色、说明缩进；去掉说明的卡片不能残留空白 |
| 窄窗口换行 | RTSS 各行启用 IsWrapEnabled，并沿用整个刷新分组统一的换行阈值 | 检查 225 DIP 输入列与按钮组合在最小窗口和高 DPI 下不挤压、不裁切 |

- [x] **R01** 完成源码层面对照，确认下拉框、整数输入、FPS 标签和条件文字已采用既有刷新区域资源。
- [x] **R02** 记录重点复查项：工具按钮组合、弹出菜单、嵌套高级选项的样式继承与缩进。
- [ ] **R03** 在产品界面或原生控件测量中确认上述差异，并实施必要的局部对齐。
- [ ] **R04** 检查正常、悬停、按下、键盘焦点和禁用状态，保留输入名称及操作可访问名称。

## 验收与交接

- [ ] **V01** 检查浅色/深色，100%/125%/150%/200% DPI，最小/普通/最大化窗口，以及中文和英文文案。
- [ ] **V02** 检查默认配置和应用配置；三种管理范围、自定义/同步、内容跟随源、高级选项展开折叠及工具菜单。
- [ ] **V03** 检查成功、失败、外部修改和恢复状态；移除防御性描述后，仍能辨认实际操作结果。
- [ ] **V04** 复用 RTSS 资源回归、界面绑定和原有刷新/GPU 位置检查，更新精确文案断言；保持四个动态文字 getter 使用 `Magpie/Resources`。
- [ ] **V05** 后续编译会话完成产品编译及界面验收，再处理对应部署。

源码参照：[配置页面](../../../src/Magpie/ProfilePage.xaml)、[动态文字](../../../src/Magpie/ProfileViewModel.cpp)、[简体资源](../../../src/Magpie/Resources.language-zh-Hans.resw)、[SettingsCard 样式](../../../src/Magpie/SettingsCard.Resource.xaml)、[SettingsExpander 样式](../../../src/Magpie/SettingsExpander.Resource.xaml)及[子项样式应用](../../../src/Magpie/SettingsExpander.cpp)。样式基准沿用[刷新控件样式对齐记录](20261002-v0.6.9-beta2-ui-style-alignment-TODO.md)。

# 效果器说明维护规范 / Effect description guide

适用于内置效果器的简体中文／英文选择器摘要、详情、分类、子分类、家族、子家族，以及相关参数提示。新增效果、改变能力或修正说明时一并维护；不要求为了统一句式重写准确的旧文案。

## 内容分层

| 层级 | 需要回答 | 写法 |
| --- | --- | --- |
| 显示名 | 这是什么效果？ | 使用稳定、可识别的算法／产品名称；与 EffectHelper 保持一致，不带实现阶段记录 |
| 摘要 | 做什么，有什么关键区别？ | 一句话，中文通常 20–35 字；固定倍率、仅 SDR、实验性等关键限制优先。两种语言均不得换行或超过现有 90 字符检查；不为凑字数填充 |
| 详情 | 适合什么素材，如何使用，有哪些限制？ | 用途与变体差异 → 用法与链顺序 → 必要条件与副作用。普通效果几句话，复杂效果分短段，不强制套满所有栏目 |
| 分类／子分类 | 如何按用途选择？ | 说明共同用途、选择方向和共同限制；不重复全部条目 |
| 家族／子家族 | 不同版本怎么选？ | 解释 DN、Soft、RGB、AR、NoJitter、倍率和模型规格。更大模型、更高档位不代表实测画质排名 |
| 参数提示 | 调整后会怎样，何时有效？ | 用界面实际名称说明作用、取舍、适用条件和生效方式。范围／默认值／步进由现有控件补充，通常不在描述中再列一遍 |
| 自定义通用说明 | 哪里获取未收录效果的可靠说明？ | 引导查看作者的用途、尺寸、链顺序、HDR、运行库和硬件要求，不替第三方承诺能力 |

所有可影响是否使用该效果的条件必须留在该效果的详情中。用户可以通过搜索直接进入叶子条目，不能假定其已阅读分类／家族说明。共用限制允许重复，但应简短；不要为消除重复而删除必要事实。

## 事实与术语

- 描述本项目当前实现，而非厂商 SDK 的能力全集。原生 `.hlsl` 标记不能证明功能；核对后端、能力检查、参数策略、颜色边界与效果链策略。
- 硬件条件以所选**渲染 GPU**为准。只写可核对的架构／运行库限制，不根据品牌名推断支持，也不无依据增加驱动最低版本。
- 分清实时生效、重新启动缩放、完整重启喜鹊。参数面板与运行面板必须使用相同语义；初始化错误要求重启进程，不代表普通参数变更也如此。
- 分清输入尺寸、原生处理尺寸、最终输出尺寸；固定倍率、每轴倍率和补帧倍率不能混用。自动适配需要说明何时有额外缩放。
- HDR 原生／浮点路径、SDR 兼容转换、显式 HDR → SDR／SDR → HDR、特殊回退是不同能力。核对 `Renderer.cpp`、`GroupAHdrRoutes.cpp`、`GroupBHdrRoutes.cpp`、`EffectProtocolCatalogC.h` 以及具体后端。自动边界仍存在，不因显式组件更新而批量删除兼容说明。
- 有支持列表就列出实际可用项；“支持已列出的效果”不能引用不存在的列表。链位置说明必须与实际执行分界一致，例如 VFG 的后置段由首个 TrueHDR 划分。
- 帧率分别指真实输入、内容提交、生成输出或显示实测；补帧不提高游戏逻辑帧率，不承诺降低延迟。开销、画质和延迟的具体收益只能来自匹配版本及条件的测量。
- 移出队列结构、时间戳回退、内部像素格式、旧配置迁移和发布历史。确实影响操作的迁移限制保留简短提示，详细记录链接到开发／迁移文档。
- 中英文逐项核对倍率、单位、条件、默认值和生效语义。英文按自然语序组织，不把中文短语逐字拼接。

| 中文 | 英文 | 注意 |
| --- | --- | --- |
| 超分／放大 | super resolution / upscaling | 不把普通插值全部称为 AI 超分 |
| 补帧／帧数倍率 | frame generation / frame multiplier | 不与空间放大倍率混淆 |
| 光流 | optical flow | 区分外部估计与游戏真实运动信息 |
| 采样抖动 | sampling jitter | 不译为画面抖动；NoJitter 仍可能使用帧历史 |
| 重新启动缩放 | restart scaling | 与 restart Magpie 区分 |
| SDR 兼容转换 | SDR-compatible conversion | 不等于原生 HDR，也不保证无损 |
| 尼特 | nits | 中文与英文的数值、单位必须一致 |

推荐标签沿用当前审查策略：`first_try` 为易入门起点，`situational` 为素材或条件相关，`advanced` 为实验接入或需要主动设置的流程，`diagnostic` 为诊断。计算开销大不自动意味着实验性；“进阶”快捷入口是一份独立清单，不能由推荐标签推断。

## 正反例

| 避免 | 使用 | 原因 |
| --- | --- | --- |
| “常规、高码率、流媒体、降噪、去模糊，两级选择。” | “AI 视频增强，可放大、降噪或去模糊。” | 摘要先说明用途，控件枚举留给参数区域 |
| “分类改变重启应用。” | “修改分类需重新启动缩放；质量档位可实时切换。” | 准确区分参数生效边界 |
| “仅支持已列出的矩形缩放效果链。” | “可搭配：…；列表外效果不支持。” | 用户能够直接作出选择 |
| “首帧仅输出一次，有界队列逐张消费。” | “保持输入尺寸，在连续帧之间生成中间帧。” | 产品说明不讲与用户决策无关的队列实现 |
| “最高档带来最佳画质。” | “较高档位增加开销，请比较纹理与伪影。” | 档位不是测量结论 |
| “NoJitter 是空间抗锯齿。” | “NoJitter 取消额外采样抖动，仍使用帧历史。” | 名称不能代替源码事实 |

## 修改入口与生成

当前生成入口是 [Read-EffectCatalogReview.ps1](../../scripts/Read-EffectCatalogReview.ps1)，按以下顺序合并：

1. [基础审查](reviews/20261002-effect-description-review.json)：既有效果及全部分类／家族。
2. [VFX 补充](reviews/20261009-nvidia-vfx-suite-effect-description-review.json)：覆盖 VSR／TrueHDR，并新增 Upscale／VFG。
3. [FSR3 FG 补充](reviews/20261011-fsr3fg-effect-description-review.json)：新增 FSR3 FG。

同一 ID 以合并后的有效条目为准；修改其当前来源，不把被覆盖的旧条目当成实际显示内容。补充审查是现有组织方式，无需为一次文案调整再加一层覆盖。

运行 [Generate-EffectCatalog.ps1](../../scripts/Generate-EffectCatalog.ps1) 生成 `src/Magpie/EffectCatalog/{zh-Hans,en-US,hdr-components}.json`。**不能只改生成产物**。效果 ID 和参数键属于配置兼容协议，改说明不能随意改 ID。

参数提示位于 `src/Magpie/Resources.language-{zh-Hans,en-US}.resw` 的 `EffectParam_<效果键>_<参数键>_Description`。效果键里的非字母数字字符变为 `_`。现有 `EffectParameterLocalization::Tooltip` 供参数页与运行面板共用，不要为相同文字增加第二套入口。查不到说明时按现有标签／数字提示回退。

`search` 收纳当前名称、用途词、参数词、必要别名与当前说明；更新文字时同步刷新，不不断追加整段旧说明。保留有效旧文件名、RTX 档位后缀、FSR3FG 等检索入口。分类有子分类时，每个效果须落入其中一个；简体中文与英文应对应同一子分类位置。

## 审查证据

每个效果保留 `source`（着色器路径、SHA-256、尺寸／参数声明）和 `review`。新增或行为变更时核对当前文件，不把旧审查状态直接等同于当前验收。

- `status: source-reviewed` 仅表示源码审查。
- `copyReviewedOn`、`copyDisposition`、`copyScope` 记录说明复核日期、修改／保留结论及边界。
- `runtimeEvidence` 是历史字段名，实际内容为后端／策略等源码路径，**不是实机运行证据**。
- 原生效果除标记着色器外，还应列出与说明有关的后端、参数生效和链限制文件，并在 `runtimeEvidenceSha256` 记录对应 SHA-256。哈希按 UTF-8 文件字节归一化 CRLF 为 LF 后计算。现有检查要求记录的路径与哈希一一对应，并强制 VFX／RTX／FSR3FG 有后端依据；其他原生效果也应按此维护。
- 后端哈希变化时，先核对受影响的事实，再更新证据；不要仅重算哈希绕过审查。哈希没有变化也不自动证明说明正确。
- `externalReferences` 记录具体版本的一手文档。SDK 默认值与产品默认值不同，以项目参数声明和实际策略为准。
- `notVerified` 明确未做的构建、真实界面、SDK、GPU、颜色、性能或延迟验证。审查报告分别记录这些状态。

## 验证与交付

从源码仓库根目录执行：

```powershell
pwsh -NoProfile -File scripts/Generate-EffectCatalog.ps1
pwsh -NoProfile -File tests/Validate-EffectCatalog.ps1
python -X utf8 tests/validate_effect_descriptions.py
git diff --check
```

这些检查覆盖生成一致性、安装 ID、双语结构／子分类、长度、旧名检索、新效果入口、相关提示资源和审查依据。它们不能判断全部语义正确，也不能证明界面可读或 SDK 可执行。

如改变选择器模型或链策略，更新相应 `EffectPickerTests.cpp`／`NvidiaVfxPolicyTests.cpp` 的行为用例。在允许编译的会话执行原生测试；静态匹配不能代替执行。变更后仅运行相关检查，不因规范要求而无条件全项目构建。

使用包含本轮提交的程序，分别在中英文下检查摘要截断、长详情滚动、家族展开、分类／进阶入口、旧名搜索、参数页及运行面板提示。原生效果还需按改变的能力做实际启动和 GPU 验证。未构建时保留为待验收，交接基线、分支、测试命令与限制。

## English essentials

Describe the behaviour implemented in this repository, not every feature in a vendor SDK. Lead with purpose and variant differences; keep hardware, dimensions, color domain, chain restrictions and application timing where users can find them. Distinguish live changes, restarting scaling and restarting Magpie. Avoid unsupported quality/performance claims and development history in product copy.

Update the effective review JSON and both languages, regenerate the catalogs, then run the checks above. Keep IDs and useful legacy search aliases stable. Native marker shaders need backend and policy evidence; `runtimeEvidence` names source files, not runtime tests. Refresh recorded hashes only after reviewing the change. Report source checks, native test execution, product build, deployment, UI and GPU acceptance separately.

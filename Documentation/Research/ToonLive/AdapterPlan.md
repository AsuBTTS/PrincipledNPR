# Generic Toon Backend Adapter 后续实施草案

日期：2026-10-28。本文是下一阶段设计，**没有实现正式 Adapter，也没有修改 NPR Core、Runtime、Editor UI 或既有契约**。依据是[数学审阅](MathReview.md)、[源码机制](SourceMechanism.md)与[隔离实验](Report.md)。

## 技术决策

建议 **Editor 的私有预览采用 A：曲线数据更新和 GPU Profile 刷新；Runtime 继续采用 D：不可变后端包切换**。这解决了“拖动阈值/宽度时频繁编译材质”的主要疑问，同时保持现有 Runtime 契约。B 的均匀偏移可作为受限阈值快速预览候选；C 的偏移纹理上传没有提供完整 Ramp 替换入口，不作为 Generic 主路线。

底层导出的 `AddOrUpdateProfile` 已在 Editor 队列实验中驱动资源更新，但这不构成游戏发行包的稳定功能承诺。引擎注释与项目当前契约都约束 Runtime 不修改 Profile；若未来需要游戏内连续编辑，须单独立项修改能力契约，并实测 Cook、资源寿命、线程、平台与维护成本。

## 逐参数能力和更新时间

“Exact”只用于数学坐标和明确数据转换；“Approximate”用于实际 GPU 表现。下表没有把一个局部实验升格为参数全硬域的视觉等价证明。

| Principled ID / 单位 / 默认 | 数学参考 | UE 接口与资源 | 更新时间类别 | 等级、误差和适用域 | 依据 |
| --- | --- | --- | --- | --- | --- |
| TerminatorDotCenter / signed dot / 0 | (3.3)、(4.2)：t=(tau+1)/2 | DiffuseRamp 的中心；受限 B 可写 LiveUV 偏移 | Editor：实时 Profile；Runtime：预制包 | Coordinate Exact；Appearance Approximate。新批阈值扫 tau=0.15–0.70、w=0.08；A 最大中心误差约 0.00016。负阈值带自影保留限制 | 新 A/D/Natural；旧 Step0/StepLow |
| TerminatorDotHalfWidth / signed dot half-width / 0.05 | (3.3)、(4.2)：h=w/2；形状需明确 | DiffuseRamp 的支持区间与插值 | Editor：实时 Profile；Runtime：预制包 | Coordinate Exact；Appearance Approximate。新批 tau=0.5、w=0.02–0.17；w=0 有有限 LUT 过渡，越界要限制参考函数而不是钳键 | 新 A_W/D_W、ZeroWidth、Boundary；旧 SoftPositive |
| SpecHalfAngleRad / rad / 8°约0.139626 | (7.3)、(7.4)；角域半峰 | Roughness + SpecularRamp 的待拟合组合 | 候选 MID + Profile；Runtime 预制资源 | NotVerified：还没有跨视角/灯型的可逆标定，不用 1-roughness | 旧 GPU 只证明面积随 roughness 变化 |
| SpecEdgeSoftness / 无量纲 / 0.15 | delta=0.95s min(beta,pi/2-beta) | SpecularRamp 的待拟合形状 | 候选实时 Profile | NotVerified：原生高光曲线柔化不等于角域柔度完全等价；s=0 必须有显式极限 | 完整 TeX 审阅；旧 GPU 柔化对照 |
| SpecPeakScale / 线性场景倍率 / 1 | (7.3) 的峰值乘子 | 候选 SpecularRamp 响应缩放或标定后倍率 | 候选实时 Profile/MID | 仅 Approximate 候选；新批未标定。旧非金属固定视角下 Specular 0.25/0.5/1 峰值近比例，不能直接视为独立艺术峰值旋钮 | 旧 SpecIntensity；源码 F0/曲线分工 |
| Shading Model / 离散 / 本轮不新增默认 | (1.1)、(8.8) 的 m | BackendId → MaterialFamily/预编译材质包 | 离散资源选择、预载 | 已有 Lit/Toon 切换的局部 GPU 证据；正式模型选择器未实现，Custom 不可宣布支持 | 旧 SwitchLit/SwitchToon；当前 Surface 家族 |

色阶 RGB、插值形状、include-shadow、消光、间接尺度和质量条件暂放在**版本化的风格资源/后端配置**中设计；它们没有因本轮研究而获得新全局语义 ID。BaseColor、Normal 等原始表面数据仍保留，不以 Toon 参数替换。

## 建议的数据与缓存职责

1. **Resolver** 解析既有继承和稀疏覆盖，保留显式 0；Reset 删除本层值。输出统一语义值、模型选择和资产事实，尚不触碰 GPU。
2. **Adapter** 先验证硬域、有限值、形状版本、边界策略和能力前提，再生成 CanonicalCurve。线性情况用域端点和可达转折点建立有序键；Hermite 用明确的函数/切线或经误差门槛约束的采样，不能静默换成线性。
3. **Editor Preview Owner** 按 Mesh/Slot/后端实例分配私有 Profile、材质/MID 和更新版本号。选定模板时完成一次必要的图创建/初次编译，随后拖动只改私有曲线和通知。新 MID 不意味着私有 Profile，必须显式检查资源身份。
4. **Preview Scheduler** 合并同一帧的更新，只提交最新快照；建议先按 30–60Hz 节流并测量交互，再冻结预算。后台可异步排队，不在正常拖动路径每次 ReadPixels/Flush。当前实验的同步读回不是 UI 实现方式。
5. **Commit/Bake** 把艺术值存入既有风格资产约定，并生成有版本与来源指纹的不可变 Profile/MIC/后端包。Editor 预览对象与保存后的 Runtime 资源分离；不改共享生产 Profile 作为临时预览。
6. **Runtime Binder** 使用既有 ApplyPackage 与 MID 参数入口；模型选择解析成预载资源，Profile 继续不可变。新的字段与保存流程须在未来实施变更中明确兼容，当前不添加。

缓存键至少包含 BackendId、引擎/能力版本、响应形状版本、颜色空间约定、有效曲线参数、阴影/GI 策略和资源质量。不要把渲染中的 Profile ID 当作稳定的资产 ID，也不要把原始浮点拖动值无限制加入永久缓存。

## 四条路线的取舍

| 路线 | 可用范围 | 成本与风险 | 决策 |
| --- | --- | --- | --- |
| A 修改私有 Profile 并通知 | Editor 下阈值、半宽、曲线形状连续更新 | 当前 native 更新会使全局图集失效，成本随条目数/分辨率增加；共享对象会联动 | Generic Editor 主候选，需明确资源所有权和节流 |
| B MID 重映射 | 经固定偏移图和 PatternUV 的均匀平移，阈值近似可调 | 没有原生通用 Ramp 输入缩放 pin，不能独立控制宽度；12-bit UV、图集过滤和 frac 接缝 | 受限快速预览候选；不能伪装成完整 Adapter |
| C 动态纹理/LUT | 本轮验证的是偏移纹理上传并通知 Profile | 同尺寸上传不会自动重烘焙；偏移纹理不是任意 DiffuseRamp LUT 入口，仍承担图集更新 | 不作为完整曲线主路线；通用 LUT 方案需另立明确扩展 |
| D 预制 Profile/材质包 | 预编译资源离散切换、Runtime 风格选择 | 组合数量膨胀、预载/PSO/内存；新组合仍可能初次编译 | Runtime 基线与 Editor 兜底 |

例如要求 tau 最近邻量化误差不超过 0.02，需要步长不大于 0.04，在 [-1,1] 上至少 51 档；w 同精度在 [0,0.5] 上约需 14 档，两者笛卡尔积约 714 份。原生 Profile ID 为 8-bit 编码且默认/其他资源也占用空间，不能假定这 714 份可同时活跃。本轮 D 测的是 12 份阈值及 6 份半宽的资源集合，并未验证巨大组合库。

## 实施顺序与退出门槛

1. **Generic Diffuse 契约收口。** 确认规范响应形状和版本、零宽、越界、颜色系数及重叠规则；保留原语义 ID。CPU 函数和生成曲线在有效域内对应，失败返回诊断。
2. **私有 Editor Profile 实时编辑。** 验证累计新增 Shader jobs、材质翻译与 ShaderMap 身份，检查首个提交帧、误差、共享/独立资源；把图集规模和 CPU 预算纳入门槛。
3. **Style Profile 持久化与离散模型切换。** 增加完整保存、撤销、重开、预载、失败原子性、Cook 和 PSO 验证；不把本轮“原生研究资产可保存”当成产品 Style 工作流通过。
4. **Stylized Specular。** 按 beta/s/I_s 在多视角与灯型扫描，建立可测误差的拟合；零半向量、s=0、峰值 0 与 HDR 域独立处理。
5. **Face/Hair。** 根据资产输入与逐灯能力缺口分别设计，不能从 Generic 成功推出专用语义成立。

持续保留 Rim 的独立自发光贡献与 Outline 的独立阶段；不能以默认关闭角色自影换取阈值“通过”。若要求负阈值在真实遮挡下仍保持指定外观，需先明确艺术策略，再评估局部扩展。任何引擎修改、独立 Shader Pass 或正式 Runtime 可变 Profile 都不属于本轮已实施范围。

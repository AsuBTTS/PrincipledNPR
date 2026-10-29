# 原生窗口验证的失败记录

2026-10-29（北京时间），首次自动化窗口截图能显示参数，但角色区域为空，随后 Slate 输入路由断言退出。`Wide.png` 是该次真实窗口导出，`Failure.log` 保留断言摘要；它们不是通过证据。

后续源码检查发现测试场景中 ASkeletalMeshActor 的默认网格组件继承了 `RF_Transient`，因此被现有材质清单正确排除。测试夹具仅清除自身组件的这个标记，未放宽生产清单过滤。第二次运行已能显示角色，并通过真实 Hit Proxy 拾取材质槽 6 和 2，但输入路由仍断言。

输入断言来自测试构造的路径：UE 的 `GeneratePathToWidgetUnchecked` 只填充几何信息，事件路由还会索引每层的 `VirtualPointerPositions`；测试现已补齐对应的空可选值。最终是否通过，以独立的完整 Automation 记录为准，不用截图存在代替事件成功。

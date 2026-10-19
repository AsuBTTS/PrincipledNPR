# 参数契约与校验

Schema（数据格式约定）说明哪些字段合法；Contract（参数契约）进一步定义字段的含义、单位和使用边界。本项目已有两者：

- `Documentation/Schemas/NPRParameterContract.v1.json`：视觉语义、默认值、硬范围、推荐范围、材质适用性和 S58 候选映射。
- `Documentation/Schemas/NPRStyleProfile.schema.v1.json`：Draft 风格 JSON 的结构和可用 ID。
- `NPRSemanticTypes.h`：C++ 稀疏覆盖与映射质量类型。质量分为尚未评估、精确、近似和不支持；必须连同适用范围解释。

## 自动参数参考

`Tools/docs.mjs` 不修改引擎资源。它从 JSON 和 `NPROutlinePreset.h` 读取字段，并记录相关实现源码的 SHA256（文件内容指纹），生成[参数参考](../principled/parameters.md)。

```powershell
npm run docs:generate
npm run docs:check
npm run docs:build
```

`check` 会重算参数页，若与已保存页面不同则失败，要求重新生成和人工审查；还检查文档本地文件链接与功能状态证据是否存在。VitePress 构建检查站内页面链接。该机制不执行 C++，只解析当前有限的 UPROPERTY 声明形式；不识别的属性必须报错并扩展解析器。

生成器不会推断运行时实际限制或验证材质二进制。`Validate()` 中的组合约束、后处理时序、预设迁移和图像结果需人工检查。源码指纹变化是复核提醒，不等于自动证明页面语义正确。

## 既有草案校验

`python Tools/verify_semantic_drafts.py` 检查参数 ID、有限数值、硬域、Draft 状态和报告来源指纹；它不是完整 JSON Schema 验证器，也不验证画面。范围中的 null 代表无界；不得改成 0。参数 ID、默认值、语义发生变化时，先设计迁移并保留旧输入兼容方案。

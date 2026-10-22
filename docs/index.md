---
layout: home
aside: false
hero:
  name: Principled NPR
  text: 用看得见的效果，调整角色风格
  tagline: 我们正在为泛二次元角色制作一套 UE5 材质渲染编辑工具。我们希望先按画面想要的变化调整明暗、反光和轮廓，再由系统处理底层渲染参数。
  image:
    src: /images/miku-hero.png
    alt: 青绿色调的初音未来插画，手边托着一颗青绿色光球
  actions:
    - theme: brand
      text: 开始使用
      link: /guide/quick-start
    - theme: alt
      text: 查看描边功能
      link: /features/outline
---

## 从左侧目录开始

我们把文档按用途放在左侧分组里。第一次打开工程，可以先读[系统介绍](./guide/introduction.md)、[安装与环境](./guide/installation.md)和[快速开始](./guide/quick-start.md)；需要调整某种画面效果时，直接打开“功能”下对应的说明；想了解编辑器和插件实现，再看“编辑器”和“开发者”。点击分组标题可以展开或收起，点击页面名称即可跳转。

## 我们做到哪一步了

我们已经接通 Outline 的主要使用流程，可以对整角色和局部区域调整描边，并提供左右对比预览、预设保存和图片导出。[Surface Master](./features/surface-material.md) 已让基础颜色、法线、自发光与 Rim 在同一个材质槽中组合，并通过原生 Toon + Rim 的球体验证。完整 Principled 参数转换还在开发；Surface 和 Rim 的调制窗口与角色工作流仍待接入，MatCap 仍在规划中。每个功能页分别说明当前能力和验证范围。

[查看实现状态与验证记录](./reference/status.md) · [查看更新记录](./changelog.md)

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

我们已经接通 Outline 的主要使用流程，可以对整角色和局部区域调整描边，并提供左右对比预览、预设保存和图片导出。[统一效果工作区](./guides/npr-workspace.md) 增加总览分类、各 Mesh/材质槽的效果清单，以及 Rim 的 Profile、实时角色预览、隔离和保存。[Surface Master](./features/surface-material.md) 保留基础颜色、法线和自发光，并通过同槽 Toon + Rim 的受控球体验证。完整 Principled 参数转换和任意角色自动转换仍在开发，MatCap 仍在规划；角色动画、Cook 与性能需要各自验收。

[查看实现状态与验证记录](./reference/status.md) · [查看更新记录](./changelog.md)

## 五份当前说明

- [项目架构设计](./architecture/项目架构设计.md)：先理解系统怎样分工。
- [项目开发说明与约束](./developer/项目开发说明与约束.md)：开发前核对各模块要求。
- [NPR调制插件使用说明](./guides/NPR调制插件使用说明.md)：按当前界面操作。
- [outline_introduction](./outline/outline_introduction.md)：描边算法、引擎公式与使用。
- [rim_light_introduction](./rim/rim_light_introduction.md)：角度 Rim 算法、节点公式与使用。

旧说明的合并和迁移位置见[历史归档索引](./reference/legacy.md)。

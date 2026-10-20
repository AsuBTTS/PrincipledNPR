import { defineConfig } from 'vitepress'
import { withMermaid } from 'vitepress-plugin-mermaid'
const docsBase = process.env.DOCS_BASE || '/'
const docsBasePath = docsBase.endsWith('/') ? docsBase : `${docsBase}/`
const section = (text: string, items: [string, string][]) => ({ text, collapsed: !['开始使用', '功能'].includes(text), items: items.map(([text, link]) => ({ text, link })) })
export default withMermaid(defineConfig({
  lang: 'zh-CN', title: 'Principled NPR', description: '面向泛二次元角色的风格化材质渲染编辑系统',
  base: docsBasePath, lastUpdated: true,
  head: [['link', { rel: 'icon', type: 'image/svg+xml', href: `${docsBasePath}npr-mark.svg` }]],
  mermaid: {
    securityLevel: 'strict', startOnLoad: false, theme: 'base',
    themeVariables: {
      primaryColor: '#e8f4f1', primaryTextColor: '#173d37', primaryBorderColor: '#14685f',
      lineColor: '#1b8c7d', secondaryColor: '#edf5f2', tertiaryColor: '#f8fbfa'
    }
  },
  themeConfig: {
    logo: '/npr-mark.svg',
    nav: [{ text: '使用手册', link: '/guide/introduction' }, { text: '开发者', link: '/developer/architecture' }, { text: '更新记录', link: '/changelog' }],
    sidebar: [
      section('开始使用', [['系统介绍','/guide/introduction'],['安装与环境','/guide/installation'],['快速开始','/guide/quick-start']]),
      section('Principled 参数', [['设计与状态','/principled/overview'],['参数参考（自动生成）','/principled/parameters'],['映射与控制粒度','/principled/mapping']]),
      section('功能', [['Outline 描边','/features/outline'],['Rim Light 边缘光','/features/rim-light'],['基础颜色','/features/base-color'],['Toon 明暗','/features/toon-shading'],['MatCap','/features/matcap']]),
      section('编辑器', [['参数编辑与双视口','/editor/parameter-editor'],['预设与保存','/editor/presets']]),
      section('开发者', [['模块架构','/developer/architecture'],['渲染管线','/developer/shader-pipeline'],['参数契约与校验','/developer/parameter-schema'],['扩展与维护','/developer/extending']]),
      section('参考资料', [['lilToon 源码对照','/reference/liltoon-comparison'],['既有技术资料','/reference/legacy'],['实现状态与证据','/reference/status'],['文档开发','/reference/documentation']])
    ],
    search: { provider: 'local', options: { locales: { root: { translations: { button: { buttonText: '搜索文档', buttonAriaLabel: '搜索文档' }, modal: { noResultsText: '没有找到结果', resetButtonTitle: '清除搜索', footer: { selectText: '选择', navigateText: '切换', closeText: '关闭' } } } } } } },
    outline: { label: '本页目录', level: [2,3] }, docFooter: { prev: '上一页', next: '下一页' },
    lastUpdated: { text: '最近更新' }, footer: { message: '实现状态以源码和有界验证证据为准。第三方参考见技术对照。' }
  }
}))

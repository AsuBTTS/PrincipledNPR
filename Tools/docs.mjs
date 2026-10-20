// No UE mutation. Generate objective reference data; keep explanations in authored pages.
import fs from 'node:fs'
import path from 'node:path'
import crypto from 'node:crypto'
import { fileURLToPath } from 'node:url'
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const read = p => fs.readFileSync(path.join(root, p), 'utf8').replace(/\r\n/g, '\n')
const json = p => JSON.parse(read(p))
const hash = p => crypto.createHash('sha256').update(read(p)).digest('hex')
const write = (p, data) => { fs.mkdirSync(path.dirname(path.join(root,p)), {recursive:true}); fs.writeFileSync(path.join(root,p), data) }
const esc = v => String(v ?? '未声明').replaceAll('|','&#124;').replaceAll('<','&lt;').replaceAll('>','&gt;').replaceAll('\n',' ')
const code = v => '`' + esc(v) + '`'
const contractPath = 'Documentation/Schemas/NPRParameterContract.v1.json'
const headerPath = 'Plugins/PrincipledNPR/Source/NPRRuntime/Public/NPROutlinePreset.h'
const cppPath = 'Plugins/PrincipledNPR/Source/NPRRuntime/Private/NPROutlinePreset.cpp'
const builderPath = 'Tools/create_outline_backend_assets.py'
const contract = json(contractPath)
const sourcePaths = [contractPath, headerPath, cppPath, builderPath,
  'Plugins/PrincipledNPR/Source/NPREditor/Private/NPROutlineMeshCustomization.h',
  'Plugins/PrincipledNPR/Source/NPRRuntime/Public/NPROutlineActorComponent.h',
  'Plugins/PrincipledNPR/Source/NPRRuntime/Private/NPROutlineActorComponent.cpp',
  'Plugins/PrincipledNPR/Source/NPRRuntime/Private/NPROutlineComponent.cpp',
  'Plugins/PrincipledNPR/Source/NPRRuntime/Public/NPRSemanticTypes.h',
  'Plugins/PrincipledNPR/Source/NPRRuntime/Public/NPRStyleProfile.h',
  'Plugins/PrincipledNPR/Source/NPRRuntime/Public/NPRMaterialProfile.h',
  'Plugins/PrincipledNPR/Source/NPRRuntime/Public/NPRBackendStylePackage.h',
  'Plugins/PrincipledNPR/Source/NPRRuntime/Private/NPRStyleBindingComponent.cpp',
  'Plugins/PrincipledNPR/Source/NPREditor/Private/NPROutlineEditorUI.cpp',
  'Plugins/PrincipledNPR/Source/NPREditor/Private/NPROutlineEditorLibrary.cpp']
function outlineRows() {
  const h=read(headerPath), structs=[...h.matchAll(/struct NPRRUNTIME_API (FNPROutlineEdgeWidthScales|FNPROutlineSettings)\s*\{([\s\S]*?)\n\};/g)]
  if(structs.length!==2) throw Error('Outline struct parser needs review')
  const rows=[]
  for(const [,name,body] of structs) {
    const declarations=[...body.matchAll(/UPROPERTY\(([\s\S]*?)\)\s*([\w:<>]+)\s+(\w+)\s*(?:=\s*([^;]+))?;/g)]
    if(declarations.length!==(body.match(/UPROPERTY\(/g)||[]).length) throw Error('Unparsed UPROPERTY: '+name)
    for(const [,meta,type,field,initial] of declarations) {
      const value = key => meta.match(new RegExp(key+'\\s*=\\s*"([^"]*)"'))?.[1]
      rows.push([name+'.'+field, type, initial?.trim() || (field==='EdgeWidthScales'?'Silhouette=1, FeatureBoundary=1':field==='WidthMask'?'nullptr（UObject 零初始化）':'未显式初始化'), [value('ClampMin'),value('ClampMax')].some(Boolean)?`[${value('ClampMin')??'未声明'}, ${value('ClampMax')??'未声明'}]`:'见 Validate / 类型约束',value('EditCondition')||'—'])
    }
  }
  return rows
}
const range = a => a ? '['+a.map(v=>v===null?'无界':v).join(', ')+']' : '无硬域'
const parameterNotes = {
  TerminatorDotCenter: ['明暗交界位置', '调整明暗交界相对受光方向的位置；这里比较的是表面朝向和光线方向的点积，不是阴影贴图偏移。'],
  TerminatorDotHalfWidth: ['明暗过渡半宽', '控制明暗交界从亮到暗的过渡宽度；设为 0 时仍保留像素级抗锯齿。'],
  SpecHalfAngleRad: ['高光覆盖角', '从光线方向与观察方向的中间方向起，量到高光降为峰值一半的角度，用来描述高光大小；头发材质按发束方向计算。'],
  SpecEdgeSoftness: ['高光边缘柔度', '控制高光边缘从清晰到柔和的变化；实际过渡角度会随观察和受光角度改变。'],
  SpecPeakScale: ['高光亮度倍率', '调整高光峰值的线性亮度；改变峰值不保证高光总能量保持不变。'],
  NormalDetailStrength: ['细节法线强度', '控制法线贴图带来的细小凹凸对光照的影响；它与大形状法线融合是两项独立控制。'],
  MacroNormalBlend: ['形体法线融合', '把表面朝向逐渐混合到资产制作的引导法线，用来简化大形状受光；没有这份引导数据时不能使用。'],
  RimGrazingAngleRad: ['边缘光延伸角', '控制边缘光从角色轮廓向表面内部延伸的角度；这里用角度衡量，不是屏幕像素宽度。'],
  RimEdgeHalfWidthRad: ['边缘光过渡半宽', '控制边缘光从亮到暗的过渡带宽度，单位是角度。'],
  RimPeakScale: ['边缘光亮度倍率', '调整边缘光的线性亮度；边缘光是否被遮挡还需要单独定义规则。'],
  FaceAngularBiasRad: ['面部明暗位置偏移', '沿面部角度数据移动明暗交界；正值会让面部变暗的位置向后推迟。'],
  FaceAngularHalfWidthRad: ['面部明暗过渡半宽', '控制面部明暗交界的角度过渡范围；它不是屏幕上的模糊或羽化宽度。'],
  HairShiftAngleRad: ['发丝高光偏转角', '在发束切线与法线组成的平面内转动高光方向；正负方向取决于资产记录的发根到发梢方向。'],
  StrandAnisotropy: ['发束高光拉伸', '让高光沿发丝方向变成长条；这是统一参数的视觉控制，不等同于 UE 原生各向异性设置。'],
  OutlineWidthPx: ['描边宽度（像素）', '描述最终输出画面中的描边像素宽度，需要独立描边实现；目前尚未自动连接到现有 Outline 控制。'],
  MatCapWeight: ['MatCap 混合权重', '控制 MatCap（用贴图模拟环境反光）占最终材质颜色的比例；还需要指定贴图和混合方式。'],
  ChromaScale: ['颜色饱和度倍率', '控制颜色鲜艳程度的倍率；完整实现前还要确定颜色计算方式和如何保持亮度，这项草案目前关闭。'],
}
const unitNames = {
  unitless_signed_dot: '无单位的有符号受光点积',
  unitless_signed_dot_half_width: '无单位的受光点积半宽',
  radian: '弧度（rad）',
  linear_scene_multiplier: '场景线性亮度倍率',
  unitless: '无单位比例',
  output_resolution_pixel: '最终输出画面的像素（px）',
}
const materialNames = {Generic:'通用',Face:'面部',Hair:'头发'}
const degrees = value => {
  const d=value*180/Math.PI
  return (Number.isInteger(d)?String(d):d.toFixed(1).replace(/\.0$/,''))+'°'
}
const parameterValue = (value,p) => value===null?'无界':p.unit==='radian'?`${value} rad（约 ${degrees(value)}）`:value
const parameterRange = (values,p) => values ? '['+values.map(v=>parameterValue(v,p)).join(', ')+']' : '无硬域'
const mappingNotes = {
  TerminatorDotCenter: '明暗交界位置：在特定单光源条件下可以换算到后端色阶中心坐标；坐标换算准确，不代表最终画面已经匹配。',
  TerminatorDotHalfWidth: '明暗过渡半宽：需要继续校准查找表、过滤方式和抗锯齿，当前只能近似对应。',
  SpecHalfAngleRad: '高光覆盖角：需要拟合后端的粗糙度和高光色阶；不能简单用“1 减粗糙度”代替高光大小。',
  SpecEdgeSoftness: '高光边缘柔度：观察方向、灯光方向和粗糙度会一起影响结果，需要通过测量校准。',
  SpecPeakScale: '高光亮度倍率：现有反射率或 F0 不是独立的高光峰值控制，需要额外标定。',
  MacroNormalBlend: '形体法线融合：只有资产确实提供预先制作的引导法线时，才能通过预处理实现。',
  FaceCriticalAzimuthTerminatorPerLight: '逐灯面部明暗交界：当前后端没有公开接口，让我们按每盏灯分别替换面部遮罩或受光点积。',
  FaceKeyLightOffsetBridge: '面部主光偏移：这是近似候选方案，还需要校准有效范围、反向换算和贴图坐标接缝。',
  HairShiftAngleRad: '发丝高光偏转角：改共享法线会同时改变漫反射和间接光；只改切线也不能独立移动这项高光。',
  StrandAnisotropy: '发束高光拉伸：后端原生高光和统一参数描述不同，需要按发丝流向测量校准。',
  HairAngularLobeAllViews: '所有视角的发丝高光：目前无法证明后端原生高光与色阶可以在所有视角重现参考响应。',
  RimGrazingAngleRad: '边缘光延伸角：当前后端没有独立对应字段；单独加亮边缘也不会自动得到正确的遮挡效果。',
  OutlineWidthPx: '描边宽度：需要使用独立几何或屏幕描边模块，不能直接当作 ToonProfile 的表面参数。',
}
const qualityNames = {Exact:'限定条件内准确',Approximate:'近似',Unsupported:'当前接口不支持'}
const scopeNames = {CoordinateOnly:'只对应坐标',Appearance:'最终外观',DataPreprocessing:'数据预处理',NativePublicInterface:'引擎公开接口',CandidateOnly:'候选方案',IndependentNativeLobeShift:'独立高光方向',ExactReferenceReproduction:'完整复现参考效果',ToonProfileField:'ToonProfile 参数字段'}
const implementationNames = {NotImplemented:'未实现',NotRun:'未运行验证'}
let generated = '# 参数参考（自动生成）\n\n<!-- Generated by Tools/docs.mjs; do not edit. -->\n\n'
if(Object.keys(parameterNotes).length!==contract.parameterDefinitions.length || contract.parameterDefinitions.some(p=>!parameterNotes[p.id])) throw Error('Chinese parameter annotations must cover every semantic parameter')
generated += '本参数契约仍处于草案、未标定、未接入渲染状态。表中英文 ID 和原始取值保留以便对照源码；弧度值后附角度近似值。推荐范围用于设计参考，不会自动限制输入。\n\n| 参数 ID / 中文名称 | 类型 / 单位 | 默认值 | 硬域 | 推荐范围 | 适用材质 | 中文解释（附原契约语义） |\n| --- | --- | --- | --- | --- | --- | --- |\n'
for(const p of contract.parameterDefinitions) {
  const note=parameterNotes[p.id]
  const name=code(p.id)+'<br><strong>'+esc(note[0])+'</strong>'
  const unit=`number / ${unitNames[p.unit]??p.unit} (${code(p.unit)})`
  const def=parameterValue(p.schemaDefault,p)
  const materials=p.materialProfiles.map(m=>materialNames[m]??m).join('、')+' ('+p.materialProfiles.join(', ')+')'
  const explanation=esc(note[1])+'<br><small>原契约：'+esc(p.semantic)+'</small>'
  generated += '| '+[name,unit,def,parameterRange(p.hardDomain,p),parameterRange(p.recommendedRange,p),materials,explanation].join(' | ')+' |\n'
}
generated += '\n单位说明：`N·L` 表示表面朝向和光线方向的点积，数值越大越朝向光源；弧度是角度的一种单位，表内原值保留弧度并附常见角度近似；“场景线性倍率”是在画面亮度处理前乘上的数值；“最终输出像素”指目标图像分辨率中的像素。\n\n词语说明：法线表示表面朝向，切线表示表面上的方向；Fresnel（菲涅耳效应）指反光随观看角度变化，F0 是正对表面时的反射基准；GI 是环境反弹形成的间接光；GGX 是常用的表面反光计算方法；LUT 是把数值对应关系预先存好的查找表；AA 是减少边缘锯齿的抗锯齿处理。\n'
const outlineNotes={Silhouette:['外轮廓线宽倍率','角色最外侧轮廓的线宽倍率。'],FeatureBoundary:['区域边界线宽倍率','人工指定区域边界的线宽倍率；不会自动查找折痕。'],Method:['描边方法','选择关闭、反向壳或屏幕空间描边方法。'],Color:['描边颜色','描边颜色，按线性 RGBA 数值传给材质。'],WidthPixels:['屏幕像素宽度','屏幕空间方法使用的描边宽度，单位为输出像素。'],HullWidthCm:['模型外扩宽度','反向壳方法使用的模型外扩宽度，单位为厘米。'],WidthEdgeSource:['宽度来源','指定当前线宽采用哪类边缘来源。'],EdgeWidthScales:['边缘宽度倍率','分别调整轮廓线和人工区域边界的宽度倍率。'],LocalWidthScale:['局部宽度倍率','当前控制层级的局部倍率；最深层已勾选的覆写生效。'],DepthBiasCm:['深度容差','屏幕空间描边的深度容差，用于抑制被其他物体挡住的线。'],bUseVertexColorWidth:['使用顶点色控制宽度','是否使用模型顶点颜色的 R 通道控制局部线宽。'],bUseWidthMask:['使用贴图遮罩宽度','是否启用贴图来控制局部线宽。'],WidthMask:['线宽遮罩贴图','启用线宽遮罩时读取的纹理资产。']}
const outline=outlineRows()
if(outline.length!==Object.keys(outlineNotes).length || outline.some(row=>!outlineNotes[row[0].split('.').at(-1)])) throw Error('Chinese Outline annotations must cover every Outline field')
generated += '\n## Outline 强类型设置\n\n这些字段已接入独立模块；不是 JSON 语义 ID 的自动 Resolver。范围列来自 UPROPERTY，实际组合约束见 [Outline](../features/outline.md)。\n\n| C++ 字段 | 中文名称 | 类型 | 声明默认值 | 声明范围 | 编辑依赖 | 中文用途 |\n| --- | --- | --- | --- | --- | --- | --- |\n'
for(const row of outline) { const note=outlineNotes[row[0].split('.').at(-1)]; generated+='| '+[row[0],note[0],...row.slice(1),note[1]].map(esc).join(' | ')+' |\n' }
generated+='\n## S58 候选映射\n\n这是尚未实现的后端候选方案。表中的中文说明帮助我们理解限制；`Exact` 表示仅在列出的条件和范围内准确，`Approximate` 表示近似对应，`Unsupported` 表示当前接口不支持。`NotImplemented / NotRun` 分别表示尚未实现、尚未运行验证。原始条件保留在最后一列供开发核对。\n\n| 语义 ID | 中文说明 | 目标 | 对应程度 / 范围 | 实现 / 验证 | 原始条件 |\n| --- | --- | --- | --- | --- | --- |\n'
if(Object.keys(mappingNotes).length!==contract.backends.S58.mappingEntries.length || contract.backends.S58.mappingEntries.some(m=>!mappingNotes[m.semanticId])) throw Error('Chinese mapping annotations must cover every S58 mapping entry')
for(const m of contract.backends.S58.mappingEntries) {
  const quality=qualityNames[m.level]??m.level
  const scope=scopeNames[m.scope]??m.scope
  const implementation=implementationNames[m.implementation]??m.implementation
  const validation=implementationNames[m.renderValidation]??m.renderValidation
  generated+='| '+[code(m.semanticId),mappingNotes[m.semanticId],m.target??'无',`${quality} / ${scope} (${m.level} / ${m.scope})`,`${implementation} / ${validation} (${m.implementation} / ${m.renderValidation})`,m.condition].map(esc).join(' | ')+' |\n'
}
generated+='\n## 来源指纹\n\n构建检查覆盖以下文本源；改变后需重新生成并人工审查相关说明。不能证明 .uasset 与脚本一致。\n\n| 来源 | SHA256（UTF8 LF） |\n| --- | --- |\n'
const assetPath = p => /\.(json|png|jpe?g)$/i.test(p) ? p : p+'.txt'
for(const p of sourcePaths) generated+=`| [${esc(p)}](/generated/source/${assetPath(p)}) | ${hash(p)} |\n`
const target='docs/principled/parameters.md'
if(process.argv[2]==='generate') write(target,generated)
else if(!fs.existsSync(path.join(root,target)) || read(target)!==generated) throw Error('参数页或源码指纹过期：运行 npm run docs:generate，人工审查映射/状态/说明后再构建。')

// Recreate a small set of original evidence/source files for offline static hosting.
const resources=[...sourcePaths,
 'Documentation/Outline/MaterialSlotUI.md',
 'Documentation/Outline/MaterialSlotUI/Validation.json',
 'Documentation/Outline/MaterialSlotUI/Readbacks.json',
 'Documentation/Outline/MaterialSlotUI/IsolatedCloth.png',
 'Documentation/Outline/MaterialSlotUI/FullCharacter.png',
 'Documentation/Outline/MaterialSlotUI/NPRModulation-Outline-UE5.8.3.jpg',
 'Documentation/Outline/Guide/01-overview.png', 'Documentation/Outline/Guide/README.md',
 'Documentation/Outline/Temporal/Validation/Candidate1/Screen_Static100/000/Original.png',
 'Documentation/Outline/Temporal/Validation/Candidate1/Screen_Static100/000/Outline.png',
 'Documentation/Outline/WidthLayers/Implementation.json',
 'Documentation/Outline/Temporal/Acceptance.json', 'Documentation/Outline/Temporal/TemporalMetrics.json']
for(const p of resources) {
 const dest=path.join(root,'docs/public/generated/source',assetPath(p))
 fs.mkdirSync(path.dirname(dest),{recursive:true}); fs.copyFileSync(path.join(root,p),dest)
}
function files(dir) { return fs.readdirSync(dir,{withFileTypes:true}).flatMap(e=>e.name==='node_modules'||e.name==='public'||e.name==='.vitepress'?[]:e.isDirectory()?files(path.join(dir,e.name)):[path.join(dir,e.name)]) }
const docs=files(path.join(root,'docs')).filter(p=>p.endsWith('.md'))
const errors=[]
for(const file of docs) {
 const text=fs.readFileSync(file,'utf8')
 for(const [,url] of text.matchAll(/\]\(([^\s)]+)(?:\s+"[^"]*")?\)/g)) {
  if(/^(https?:|mailto:|#)/.test(url)) continue
  const clean=decodeURIComponent(url.split('#')[0])
  const candidates=clean.startsWith('/generated/')?[path.join(root,'docs/public',clean)]:clean.startsWith('/')?[path.join(root,'docs',clean),path.join(root,'docs/public',clean.slice(1))]:[path.resolve(path.dirname(file),clean)]
  const targets=candidates.flatMap(dest=>[dest,dest+'.md',path.join(dest,'index.md')])
  if(!targets.some(p=>fs.existsSync(p)&&fs.statSync(p).isFile())) errors.push(path.relative(root,file)+': '+url)
 }
}
const statuses=json('docs/feature-status.json')
for(const item of statuses) {
 if(!['已实现并验证','已实现但待验证','开发中','规划中'].includes(item.status)) errors.push('Unknown status '+item.id)
 if(!read('docs/'+item.page).includes(item.status)) errors.push('Status missing in '+item.page)
 for(const p of item.evidence) if(!fs.existsSync(path.join(root,p))) errors.push('Missing evidence '+p)
}
if(errors.length) throw Error(errors.join('\n'))
console.log(`Docs ${process.argv[2]} PASS: ${contract.parameterDefinitions.length} semantic parameters, ${outlineRows().length} outline fields, ${docs.length} pages, ${statuses.length} status entries; local file links checked. No UE rendering validation.`)

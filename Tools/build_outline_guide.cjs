// Documentation compositor: original screenshot bytes remain intact in an SVG annotation layer.
// Run with Node. Optional argument: absolute bundled node_modules path for SVG export.
const fs = require('node:fs');
const path = require('node:path');
const root = path.resolve(__dirname, '..');
const output = path.join(root, 'Documentation/Outline/Guide');
const esc = value => String(value).replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&apos;'}[c]));
const images = {
  overview: {file:'Raw/01-overview.jpg',w:1707,h:1067},
  width: {file:'Raw/02-width.jpg',w:1707,h:1067},
  regions: {file:'Raw/03-regions.jpg',w:1707,h:1067},
  export: {file:'Raw/05-export.jpg',w:1707,h:1067},
  disabled: {file:'Raw/06-disabled.jpg',w:1707,h:1067},
};
const sheets = [
  {id:'01-overview',title:'01｜读取角色与界面总览',image:'overview',crop:[0,0,1707,1067],overview:true,marks:[
    {box:[10,140,472,54],n:1,lines:['读取角色 / 打开配置']},
    {box:[10,344,472,84],n:2,lines:['基础参数与 Scope']},
    {box:[10,480,472,29],n:3,lines:['Meshes 局部区域入口']},
    {box:[500,87,1197,961],n:4,lines:['左原材质 / 右描边预览'],right:true},
    {box:[10,549,472,217],n:5,lines:['预设应用、保存与导出']},
  ]},
  {id:'02-width',title:'02｜调整基础宽度',image:'width',crop:[0,305,475,490],marks:[
    {box:[25,413,418,81],n:1,lines:['基础宽度','Screen Space 使用像素宽度。','截图当前为 1.25 px。','Hull 使用厘米宽度。']},
    {box:[25,520,418,104],n:2,lines:['边缘来源与倍率','Silhouette 对应轮廓倍率。','Feature Boundary 只用于','手工划区的屏幕空间边界。']},
    {box:[25,626,418,28],n:3,lines:['区域宽度倍率','与基础宽度、边类型倍率相乘。','1.0 表示保持原宽度。']},
    {box:[25,681,418,108],n:4,lines:['灰色不是按钮故障','当前 Screen 不支持连续','Vertex Color / UV Mask 宽度。']},
  ]},
  {id:'03-regions',title:'03｜局部覆写：开关与数值',image:'regions',crop:[0,394,475,520],marks:[
    {box:[10,395,450,110],n:1,lines:['找到源组件','Meshes → 索引 [0]。','每项对应一个源网格组件。']},
    {box:[65,722,164,26],n:2,lines:['先勾选覆写开关','要覆盖区域倍率，请勾选','“覆写区域宽度倍率”。','截图未勾选，当前仍继承父级。']},
    {box:[60,829,383,27],n:3,lines:['再展开 Values','修改其中的 Local Scale。','只修改 Values 而不勾开关，','不会替换继承值。']},
    {box:[43,858,417,53],n:4,lines:['更细的区域入口','Materials 按材质槽名划区。','Sections 仅支持单 LOD、','独占材质槽的分区。']},
  ]},
  {id:'04-preset-save',title:'04｜应用预设与保存',image:'overview',crop:[0,414,490,328],marks:[
    {box:[10,415,472,65],n:1,lines:['只读的来源记录','“已应用预设”用于查看来源。','应用操作使用下方选择器。']},
    {box:[10,549,472,67],n:2,lines:['选择并应用 / 重新应用','新选择会立即应用基础风格。','同一资产用“重新应用”恢复。']},
    {box:[10,625,472,76],n:3,lines:['指定路径后保存','基础风格写入预设资产。','区域规则和 Scope 保存在关卡。']},
    {box:[10,708,472,26],n:4,lines:['重新读取当前角色绑定','重建当前内存配置的预览，','不会从磁盘重开关卡。']},
  ]},
  {id:'05-export',title:'05｜导出画面并找到文件',image:'export',crop:[0,0,490,892],marks:[
    {box:[10,38,472,47],n:1,lines:['查看顶部操作结果','成功后会显示文件名和目录。','长路径可对照原始截图。']},
    {box:[10,737,472,25],n:2,lines:['导出双视口 PNG','生成 Original.png / Outline.png。','每次创建独立时间目录。']},
    {box:[10,768,472,25],n:3,lines:['打开上次导出目录','只在本次窗口导出成功后显示。','文件位于项目 Saved/NPR/','Outline/Previews/<时间戳>/。']},
    {box:[10,800,472,25],n:4,lines:['描边总开关','整角色模式可关闭 / 启用。','关闭会保留区域配置。']},
  ]},
  {id:'06-disabled',title:'06｜关闭描边后的正常状态',image:'disabled',crop:[0,304,490,538],marks:[
    {box:[10,329,472,31],n:1,lines:['Enabled 已关闭','右侧仍然显示角色的原表面，','但不叠加角色描边。']},
    {box:[10,752,472,27],n:2,lines:['点击即可重新启用','按钮名称随启用状态变化。','配置没有被删除。']},
  ]},
];
function rect(x,y,w,h,extra='') {return `<rect x="${x}" y="${y}" width="${w}" height="${h}" rx="5" ${extra}/>`;}
function text(x,y,label,size=20,color='#f0f5fb') {return `<text x="${x}" y="${y}" font-size="${size}" fill="${color}">${esc(label)}</text>`;}
function badge(x,y,n){return `<circle cx="${x}" cy="${y}" r="17" fill="#ffad38" stroke="#162333" stroke-width="2"/><text x="${x}" y="${y+6}" text-anchor="middle" font-size="21" font-weight="700" fill="#101926">${n}</text>`;}
function compose(sheet) {
  const img=images[sheet.image], [cx,cy,cw,ch]=sheet.crop;
  const x=55,y=83, panel=sheet.overview?0:395;
  const width=cw+110+panel, height=Math.max(ch,sheet.overview?0:sheet.marks.reduce((n,m)=>n+72+m.lines.length*27,0))+(sheet.overview?218:168);
  const data=fs.readFileSync(path.join(output,img.file)).toString('base64');
  const parts=[`<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="${width}" height="${height}" viewBox="0 0 ${width} ${height}"><style>text{font-family:'Microsoft YaHei','Noto Sans CJK SC',sans-serif}</style>`,rect(0,0,width,height,'fill="#101a28"'),text(28,44,sheet.title,28),`<svg x="${x}" y="${y}" width="${cw}" height="${ch}" viewBox="${cx} ${cy} ${cw} ${ch}"><image width="${img.w}" height="${img.h}" xlink:href="data:image/jpeg;base64,${data}"/></svg>`];
  let panelY=115;
  for(const m of sheet.marks) {
    const [bx,by,bw,bh]=m.box;
    const rx=x+bx-cx,ry=y+by-cy;
    parts.push(rect(rx,ry,bw,bh,'fill="none" stroke="#ffad38" stroke-width="3"'));
    const badgeX=m.right?x+cw+27:27, badgeY=ry+bh/2;
    parts.push(`<path d="M ${badgeX} ${badgeY} L ${m.right?rx+bw:rx} ${badgeY}" fill="none" stroke="#ffad38" stroke-width="2"/>`,badge(badgeX,badgeY,m.n));
    if(!sheet.overview){
      const px=x+cw+38;
      parts.push(badge(px+5,panelY-7,m.n));
      m.lines.forEach((line,i)=>parts.push(text(px+33,panelY+i*27,line,i?18:22,i?'#cad7e6':'#ffffff')));
      panelY+=72+m.lines.length*27;
    }
  }
  if(sheet.overview){
    sheet.marks.forEach((m,i)=>{
      const lx=35+i*355;
      parts.push(badge(lx,y+ch+36,m.n),text(lx+28,y+ch+43,m.lines[0],19));
    });
  }
  parts.push(text(28,height-23,'橙色为文档标注层；原始截图未重绘。可打开 Raw 目录查看未标注原图。',17,'#a8bdd4'),'</svg>');
  return {svg:parts.join('\n'),width,height};
}
async function main(){
  fs.mkdirSync(output,{recursive:true});
  const metadata=[];
  for(const sheet of sheets){
    const result=compose(sheet);
    fs.writeFileSync(path.join(output,sheet.id+'.svg'),result.svg);
    metadata.push({id:sheet.id,title:sheet.title,source:images[sheet.image].file,crop:sheet.crop,marks:sheet.marks,width:result.width,height:result.height});
  }
  fs.writeFileSync(path.join(output,'annotations.json'),JSON.stringify(metadata,null,2)+'\n');
  if(process.argv[2]){
    const sharp=require(path.join(process.argv[2],'sharp'));
    for(const sheet of metadata){
      // Rasterize the code-native SVG document; source screenshot bytes inside it are unchanged.
      await sharp(path.join(output,sheet.id+'.svg')).png().toFile(path.join(output,sheet.id+'.png'));
    }
  }
  console.log(JSON.stringify(metadata.map(s=>({id:s.id,width:s.width,height:s.height}))));
}
main().catch(e=>{console.error(e);process.exitCode=1;});

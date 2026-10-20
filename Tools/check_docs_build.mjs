// Check emitted static pages, assets and anchors, including nav links from config.
import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../docs/.vitepress/dist')
const base=(process.env.DOCS_BASE||'/').replace(/\/$/,'')
const walk=p=>fs.readdirSync(p,{withFileTypes:true}).flatMap(e=>e.isDirectory()?walk(path.join(p,e.name)):[path.join(p,e.name)])
const pages=walk(root).filter(p=>p.endsWith('.html'))
const failures=[]
let links=0
const ids=new Map()
for(const p of pages) ids.set(p,new Set([...fs.readFileSync(p,'utf8').matchAll(/\bid="([^"]+)"/g)].map(m=>m[1])))
for(const page of pages) {
 const html=fs.readFileSync(page,'utf8')
 for(const [,raw] of html.matchAll(/\b(?:href|src)="([^"]+)"/g)) {
  if(/^(?:[a-z]+:|\/\/)/i.test(raw)) continue
  const [url,fragment]=raw.replaceAll('&amp;','&').split('#')
  let local=decodeURIComponent(url.split('?')[0])
  if(base && local.startsWith(base+'/')) local=local.slice(base.length)
  let target=local ? (local.startsWith('/')?path.join(root,local):path.resolve(path.dirname(page),local)) : page
  if(fs.existsSync(target)&&fs.statSync(target).isDirectory()) target=path.join(target,'index.html')
  if(!fs.existsSync(target)&&!path.extname(target)) target+='.html'
  links++
  if(!fs.existsSync(target)) failures.push(`${path.relative(root,page)}: missing ${raw}`)
  else if(fragment && target.endsWith('.html') && !ids.get(target)?.has(decodeURIComponent(fragment))) failures.push(`${path.relative(root,page)}: missing anchor ${raw}`)
  else if(target.endsWith('.png')) {
   const signature=fs.readFileSync(target).subarray(0,8)
   if(!signature.equals(Buffer.from([137,80,78,71,13,10,26,10]))) failures.push(`${path.relative(root,page)}: invalid PNG asset ${raw}`)
  }
 }
}
if(failures.length) throw Error(failures.join('\n'))
console.log(`Static output PASS: ${pages.length} HTML pages; ${links} local links/assets/anchors.`)

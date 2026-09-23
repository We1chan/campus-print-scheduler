// Run from repository root. Requires the Codex bundled @oai/artifact-tool runtime.
import fs from 'node:fs/promises';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
import {Presentation,PresentationFile,FileBlob} from '@oai/artifact-tool';
import {diagrams} from './diagrams.mjs';
const root=process.cwd(),out=path.join(root,'docs/week3'),tmp=path.join(root,'runtime/week3-build');
const SKILL_DIR=process.env.CPS_PRESENTATION_SKILL;
const PYTHON=process.env.CPS_RUNTIME_PYTHON;
if(!SKILL_DIR||!PYTHON)throw Error('Set CPS_PRESENTATION_SKILL and CPS_RUNTIME_PYTHON');
const {finalizePresentation}=await import(pathToFileURL(path.join(SKILL_DIR,'container_tools/artifact_tool_utils.mjs')).href);
await fs.mkdir(out,{recursive:true});await fs.mkdir(path.join(tmp,'previews'),{recursive:true});
const P=Presentation.create({slideSize:{width:1280,height:720}});
const C={ink:'#19334A',muted:'#63778B',blue:'#245EA7',teal:'#16877E',bg:'#F7F9FC',line:'#B7C9D9',white:'#FFFFFF'};
const font='Hiragino Sans GB',notes=[];let uid=0;
function text(sl,x,y,w,h,value,size=28,color=C.ink,bold=false,align='left'){
 const t=sl.shapes.add({geometry:'textbox',name:'text-'+(++uid),position:{left:x,top:y,width:w,height:h},fill:'none',line:{fill:'none',width:0}});
 t.text=value;t.text.style={typeface:font,fontSize:size,color,bold,alignment:align,verticalAlignment:'top',autoFit:'none',wrap:'square',insets:{left:0,right:0,top:0,bottom:0}};return t;
}
function slide(title,note,dark=false){const s=P.slides.add();s.background.fill=dark?C.ink:C.bg;const i=P.slides.items.length;
 if(title)text(s,60,37,1150,64,title,44,dark?C.white:C.ink,true);
 text(s,1165,674,55,26,String(i).padStart(2,'0'),18,dark?'#ACC2D6':C.muted,false,'right');
 s.speakerNotes.textFrame.setText(note);notes.push({slide:i,title:title||'校园共享打印任务调度与故障恢复系统',note});return s;}
function rule(sl,x,y,w){sl.shapes.add({geometry:'line',position:{left:x,top:y,width:w,height:1},line:{fill:C.line,width:1}});}
function point(s,x,y){return s.shapes.add({geometry:'rect',name:'point-'+(++uid),position:{left:x-.1,top:y-.1,width:.2,height:.2},fill:'none',line:{fill:'none',width:0}});}
function pathEdge(s,pts,opts={}){for(let i=1;i<pts.length;i++){const a=point(s,...pts[i-1]),b=point(s,...pts[i]);s.shapes.connect(a,b,{kind:'straight',line:{fill:opts.color||C.line,width:opts.width||2,style:opts.dashed?'dashed':'solid'},tail:{type:i===pts.length-1&&opts.arrow!==false?'arrow':'none',width:'sm',length:'sm'}}).bringToFront();}}
function draw(s,d,ox=60,oy=145,scale=1){for(const q of d.items){if(q.type==='edge'){pathEdge(s,q.points.map(([x,y])=>[ox+x*scale,oy+y*scale]),q);continue;}
 const p={left:ox+q.x*scale,top:oy+q.y*scale,width:q.w*scale,height:q.h*scale};
 if(q.type==='text'){text(s,p.left,p.top,p.width,p.height,q.text,(q.font||23)*scale,q.color||C.ink,!!q.bold,q.align||'left');continue;}
 const sh=s.shapes.add({geometry:q.shape||'rect',name:d.id+'-'+q.id,position:p,fill:q.fill||C.white,line:{fill:q.stroke||C.line,width:1.5}});
 if(q.text){sh.text=q.text;sh.text.style={typeface:font,fontSize:(q.font||23)*scale,color:q.color||C.ink,bold:!!q.bold,alignment:q.align||'center',verticalAlignment:'middle',autoFit:'none',wrap:'square',insets:{left:6,right:6,top:4,bottom:4}};}}
}
const diagram=id=>diagrams.find(d=>d.id===id);
function diagramSlide(title,id,note,caption){const s=slide(title,note);draw(s,diagram(id));if(caption)text(s,60,642,1110,28,caption,19,C.muted);return s;}
const src='来源：《操作系统原理课程设计.pdf》第13–14页。';
let s=slide('',`${src}本次是课程第三周的小组实验设计汇报，说明选题、需求、概要设计、OS原理、小组分工和预计难点。按三人已确定分工制作。课件写4–6人一组，提交前由小组向老师确认人数安排。预计讲解8–10分钟。`,true);
text(s,66,94,1130,60,'操作系统原理课程设计',26,'#9EC9DE');
text(s,66,207,1150,184,'校园共享打印任务调度\n与故障恢复系统',61,'#FFFFFF',true);
text(s,66,458,1110,50,'第三周小组实验设计汇报',30,'#D8E7F0');
text(s,66,584,1140,36,'A  We1chan       B  YI1990442038       C  korwave',24,'#D8E7F0');

s=slide('需求背景与研发目标',`${src}本选题把校园共享打印抽象成多个任务竞争有限设备。这里的需求是基于典型场景提出的假设，尚未完成正式问卷或访谈，不把假设写成调研统计。希望解决等待过程不可见、不同任务调度难比较、执行异常后状态难追踪的问题。课设采用模拟打印，便于安全复现进程故障。`);
text(s,60,153,480,65,'共享设备带来的问题',30,C.blue,true);
text(s,60,237,505,267,'多人连续提交，任务需要排队\n\n短任务与大任务争用设备\n\n工作进程异常后需要确认结果',28);
text(s,676,153,510,65,'本项目的目标',30,C.teal,true);
text(s,676,237,520,267,'让队列、进度和结果可见\n\n比较调度策略的效率与公平性\n\n保存任务状态并支持有限恢复',28);
rule(s,60,586,1150);text(s,60,609,1130,38,'需求依据：校园打印场景假设，后续通过试用和访谈确认优先级',21,C.muted);

s=slide('需求范围与验收方式',`${src}第一版聚焦单台主机上的两台模拟打印设备。输入是任务名称、页数、份数和优先级。用户能够提交、查询和取消任务，操作员能够切换策略并查看运行记录。关键验收不是功能按钮是否存在，而是同任务不会重复领取、运行取消收到确认、服务重启可以对账恢复。真实打印、支付、登录权限和网络共享不纳入本轮。`);
const req=[['任务管理','提交、查询、取消任务','非法输入有提示，取消有明确结果'],['调度执行','两台设备，三种非抢占策略','同一任务不会被两个设备重复领取'],['恢复与观察','状态持久化、进度和日志','重启后对账，重连后获取完整快照']];
for(let i=0;i<req.length;i++){const y=163+i*130;text(s,60,y,220,45,req[i][0],29,C.blue,true);text(s,310,y,835,45,req[i][1],28);text(s,310,y+47,850,43,req[i][2],23,C.muted);rule(s,60,y+104,1140);}
text(s,60,595,1140,46,'边界：本机模拟输出，不接真实打印机，不实现支付与账号权限',23,C.muted);

diagramSlide('项目思维导图','mindmap',`${src}思维导图从需求、调度、后台、交互、验证和协作六个分支梳理工作。每条分支都对应代码或验收材料。A负责策略和统计，B负责进程与持久化，C负责界面与跨平台集成。各分支同时约束功能范围与验收方式。`);

diagramSlide('UML 用例图','usecase',`${src}用例图强调系统对外提供的能力。提交者关注提交、查看和取消，演示操作员关注策略、统计和故障演示。两个角色只是需求视角，不代表已实现身份认证。用例边界不包括真实打印机、支付系统或远程共享服务。`);

diagramSlide('目标架构与进程边界','architecture',`来源：docs/architecture.md、src/coordinator/main.cpp、src/gui/print_controller.cpp，代码基线fb7de0b。图展示目标集成架构。后台coordinator持有调度器和状态文件，监督进程启动worker执行模拟打印，GUI通过本地IPC查询和控制。B的后台已实现，但GUI当前仍使用内存Scheduler并直接启动supervisor，接入目标后台是待完成工作。不能将目标设计说成全部已集成。`);

diagramSlide('UML 核心类图','class',`来源：include/cps/core/scheduler.hpp、include/cps/metrics/statistics.hpp、src/core/replay.cpp、src/coordinator/main.cpp。这是一张简化核心关系图，不罗列所有Qt类和辅助函数。Scheduler保存Job，Job保存每次执行的RunInterval。Coordinator调用核心并负责外部进程和磁盘。Statistics为快照统计结果，replay用模拟时间驱动相同核心。接口通过副本和明确的状态方法隔离UI与核心。`);

diagramSlide('UML 任务提交与执行时序','sequence',`来源：src/coordinator/main.cpp。提交后先保存等待状态，派发时在互斥临界区领取任务并产生attempt，先持久化Running状态，再启动监督和工作进程。若持久化失败，就不能继续启动新执行。worker的进度和结束事件返回后台，后台核对结果并更新快照。为突出先持久化再执行，图略去GUI快照刷新返回线以及详细异常分支，异常状态见下一页。`);

diagramSlide('UML 取消、重试与终态','state',`来源：src/core/scheduler.cpp。Queued任务可以直接取消，Running任务进入Cancelling后等待执行器停止确认。当前attempt未达上限时可以重新入队；超过上限进入Failed。取消与成功输出可能竞争，若当前执行的原子输出已经确认提交，Cancelling仍可能转Completed。核心拒绝过期attempt回调。图中的Completed、Failed、Cancelled是业务终态，重试只从仍活动的Running发生。`);

s=slide('操作系统原理在项目中的应用',`来源：《操作系统原理课程设计.pdf》第3–9页，docs/architecture.md。课程明确要求守护进程，不能只用后台线程替代。POSIX通过double-fork、setsid、工作目录和描述符处理脱离终端，随后exec初始化Qt。Windows原生使用前台监督服务模式，传统daemon验收在macOS或Linux进行。其余原理分别体现在调度、同步、进程通信和文件一致性中。`);
for(const [i,label,body] of [[0,'调度与互斥','非抢占策略选择，std::mutex 保证领取与状态更新原子化'],[1,'多进程与 IPC','独立 coordinator / supervisor / worker，JSONL 消息分帧'],[2,'守护与恢复','POSIX 脱离终端，监督异常执行，有限重试与重启对账'],[3,'文件 I/O 与锁','QLockFile 防重复实例，QSaveFile 原子更新状态与输出']]){const y=153+i*113;text(s,60,y,260,45,label,29,i%2?C.teal:C.blue,true);text(s,350,y,840,70,body,25);if(i<3)rule(s,60,y+88,1150);}
text(s,60,629,1140,33,'跨平台：Windows 前台监督；macOS / Linux 演示传统 daemon',22,C.muted);

s=slide('原型工具与界面流程',`来源：https://www.drawio.com/docs/tutorials/mockups-in-jira/ 和 https://help.figma.com/hc/en-us/articles/360040314193-Guide-to-prototyping-in-Figma 。原型工具用来提前表达界面和操作过程，让团队在完整实现前讨论需求。Figma等支持页面连接和交互预览，diagrams.net可以编辑线框和流程。本次交付diagrams.net格式图稿及离线HTML交互原型。原型使用示例数据，不连接正式C++服务。`);
text(s,60,135,1150,73,'先确认页面怎么用，再核对后台接口如何支撑交互',31,C.blue,true);
draw(s,diagram('wireframe'),60,225,.82);
text(s,1026,273,182,230,'工具与产物\n\ndiagrams.net\n可编辑线框\n\n浏览器\n可点击 HTML',23,C.muted);

s=slide('界面原型：打印队列',`来源：本项目 docs/week3/prototype.html。截图为设计原型，数据是明确标识的课堂示例，不是实际后台运行证据。队列页同时展示两台设备、等待任务、状态和取消入口。选择策略只影响下一次派发，已经执行的任务不抢占。现场可打开同目录HTML，演示提交任务并在队列中找到它。`);
s.images.add({blob:new Uint8Array(await fs.readFile(path.join(tmp,'prototype-queue.png'))),contentType:'image/png',alt:'打印队列交互原型，示例数据',fit:'contain',position:{left:60,top:135,width:1160,height:485}});
text(s,60,637,1130,30,'交互原型，示例数据。双击 prototype.html 可离线打开',22,C.muted);

s=slide('界面原型：取消确认与重连',`来源：本项目 docs/week3/prototype.html，目标接口语义见docs/interfaces.md。点击运行任务的取消按钮后先弹出确认，再显示取消中，直到执行器确认停止。断线时明确显示连接状态并停用需要后台确认的控制，后台继续执行的目标行为通过原型模拟。重连后重新获取快照，避免仅靠旧增量事件猜测真实状态。演示数据与正式恢复逻辑分开。`);
s.images.add({blob:new Uint8Array(await fs.readFile(path.join(tmp,'prototype-cancel.png'))),contentType:'image/png',alt:'取消确认原型对话框',fit:'contain',position:{left:60,top:170,width:570,height:330}});
text(s,717,151,490,51,'取消过程',30,C.blue,true);text(s,717,212,490,135,'确认取消\n显示“取消中”\n收到停止确认后结束',27);
text(s,717,397,490,47,'连接中断',30,C.teal,true);text(s,717,458,490,145,'显示连接状态，停用控制\n恢复连接并获取完整快照\n后台任务按目标设计继续执行',26);
text(s,60,619,555,51,'示例数据用于确认交互，实际执行由独立后台负责',22,C.muted);

s=slide('实现难点与解决思路',`${src}难点集中在并发和故障下的正确性。领取必须与状态改变在同一个临界区；旧进程回调需要attempt隔离；崩溃后先对账输出和进程，再决定完成或重排队；大任务公平性用老化与尾部指标观察。跨平台通信必须处理半条消息、多条消息和中文路径。每项难点都配对应测试，避免只演示正常流程。`);
for(const [i,title,body]of [[0,'重复领取与旧回调','互斥锁覆盖选取和更新；attempt 识别执行轮次'],[1,'取消与输出竞争','区分请求收到、停止确认和原子输出完成'],[2,'崩溃一致性','先写状态再执行，重启核对输出与锁后恢复'],[3,'效率与公平性','比较平均和最长等待，用老化改善长时间等待'],[4,'跨平台通信','JSONL 缓冲分帧，隔离 POSIX API，覆盖中文路径']]){const y=143+i*96;text(s,60,y,300,42,title,27,i%2?C.teal:C.blue,true);text(s,405,y,800,66,body,25);if(i<4)rule(s,60,y+73,1140);}

s=slide('三人分工与接口约定',`${src}分工沿用已经确认的成员：A是We1chan，B是YI1990442038，C是korwave。三人都承担实现和相应验证，不把某人仅安排成写文档。A提供状态与统计接口，B提供持久化和进程事件，C使用这些接口做界面与集成。公共字段或取消语义修改时先更新接口文档，直接在main同步、提交、推送。课件人数要求与当前三人安排需向老师确认。`);
for(const [i,name,role,deliver]of [[0,'A  We1chan','调度与性能分析','策略、状态接口、统计、负载回放与实验'],[1,'B  YI1990442038','进程与持久后台','守护、IPC、文件锁、快照与故障恢复'],[2,'C  korwave','界面与跨平台集成','任务界面、事件处理、后台接入与发布包']]){const y=154+i*146;text(s,60,y,400,44,name,29,i===1?C.teal:C.blue,true);text(s,520,y,650,44,role,30,C.ink,true);text(s,520,y+53,680,55,deliver,25,C.muted);rule(s,60,y+120,1140);}
text(s,60,628,1140,32,'共同契约：Job / attempt / JSONL / 取消确认 / 快照版本',23,C.muted);

s=slide('阶段计划与当前集成边界',`${src}课程第三周交付本次设计汇报、图稿和原型。第四、五周按计划集中完成GUI与持久后台统一、异常路径验证和跨平台发布，第六周展示可运行系统。现有仓库已完成核心、后台和GUI各自的实现，也有26个A专项场景与33组策略实验。当前仍待完成的是GUI接入独立coordinator以及最终整体验收。这一页区分已完成与计划，不把原型截图当成最终系统验收证据。`);
text(s,60,143,250,42,'第 3 周',31,C.blue,true);text(s,330,143,865,52,'设计汇报、UML、思维导图、交互原型',29);rule(s,60,211,1140);
text(s,60,243,250,42,'第 4–5 周',31,C.blue,true);text(s,330,243,865,82,'GUI 接入持久后台，核对取消、重连与故障恢复\n完善 Windows / macOS 发布与演示步骤',27);rule(s,60,346,1140);
text(s,60,381,250,42,'第 6 周',31,C.blue,true);text(s,330,381,865,53,'展示可运行系统，提交代码与运行证据',29);rule(s,60,455,1140);
text(s,60,491,1140,45,'已有基础：核心调度、后台、界面及各自测试已实现',27,C.teal,true);
text(s,60,550,1140,55,'下一验收：界面与 coordinator 的统一链路，以及最终部署包',27,C.ink);
text(s,60,625,1140,34,'计划按课程周次安排；本次代码基线 fb7de0b',21,C.muted);

if(P.slides.items.length!==16)throw Error('Expected 16 slides');
// Export editable diagrams and SVG from the same definitions used on the slides.
const esc=v=>String(v).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&apos;'}[c]));
let pages=[];
for(const d of diagrams){let svg=`<svg xmlns="http://www.w3.org/2000/svg" width="1160" height="565" viewBox="0 0 1160 565"><rect width="1160" height="565" fill="#F7F9FC"/><defs><marker id="arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M 0 0 L 10 5 L 0 10" fill="none" stroke="#63778B" stroke-width="1.5"/></marker></defs><text x="15" y="37" font-family="Hiragino Sans GB,Microsoft YaHei,sans-serif" font-size="30" font-weight="bold" fill="${C.ink}">${esc(d.title)}</text><g transform="translate(0,60)">`;
let xml='<mxCell id="0"/><mxCell id="1" parent="0"/>';let n=2;
for(const q of d.items){if(q.type==='edge'){svg+=`<polyline points="${q.points.map(p=>p.join(',')).join(' ')}" fill="none" stroke="${q.color||C.line}" stroke-width="2" ${q.dashed?'stroke-dasharray="6 4"':''} ${q.arrow!==false?'marker-end="url(#arrow)"':''}/>`;
const pts=q.points;xml+=`<mxCell id="${n++}" value="" style="endArrow=${q.arrow===false?'none':'open'};strokeColor=${q.color||C.line};strokeWidth=2;${q.dashed?'dashed=1;':''}" edge="1" parent="1"><mxGeometry relative="1" as="geometry"><mxPoint x="${pts[0][0]}" y="${pts[0][1]}" as="sourcePoint"/><mxPoint x="${pts.at(-1)[0]}" y="${pts.at(-1)[1]}" as="targetPoint"/>${pts.length>2?'<Array as="points">'+pts.slice(1,-1).map(([x,y])=>`<mxPoint x="${x}" y="${y}"/>`).join('')+'</Array>':''}</mxGeometry></mxCell>`;continue;}
const f=q.font||23,lines=q.text.split('\n');if(q.type==='box'){svg+=q.shape==='ellipse'?`<ellipse cx="${q.x+q.w/2}" cy="${q.y+q.h/2}" rx="${q.w/2}" ry="${q.h/2}" fill="${q.fill||C.white}" stroke="${q.stroke||C.line}" stroke-width="1.5"/>`:`<rect x="${q.x}" y="${q.y}" width="${q.w}" height="${q.h}" rx="${q.shape==='roundRect'?12:0}" fill="${q.fill||C.white}" stroke="${q.stroke||C.line}" stroke-width="1.5"/>`;}
const align=q.type==='box'?(q.align||'center'):(q.align||'left'),tx=q.x+(align==='center'?q.w/2:3),ty=q.type==='box'?q.y+(q.h-lines.length*f*1.3)/2+f:q.y+f;
svg+=`<text x="${tx}" y="${ty}" font-family="Hiragino Sans GB,Microsoft YaHei,sans-serif" font-size="${f}" fill="${q.color||C.ink}" text-anchor="${align==='center'?'middle':'start'}" ${q.bold?'font-weight="bold"':''}>${lines.map((t,i)=>`<tspan x="${tx}" dy="${i?f*1.3:0}">${esc(t)}</tspan>`).join('')}</text>`;
xml+=`<mxCell id="${n++}" value="${esc(q.text).replace(/\n/g,'&#xa;')}" style="${q.type==='text'?'text;strokeColor=none;fillColor=none;':q.shape==='ellipse'?'ellipse;':q.shape==='roundRect'?'rounded=1;':''}whiteSpace=wrap;html=0;align=${align};verticalAlign=${q.type==='text'?'top':'middle'};fontSize=${f};fontFamily=Helvetica;${q.bold?'fontStyle=1;':''}fontColor=${q.color||C.ink};${q.type==='box'?`fillColor=${q.fill||C.white};strokeColor=${q.stroke||C.line};`:''}" vertex="1" parent="1"><mxGeometry x="${q.x}" y="${q.y}" width="${q.w}" height="${q.h}" as="geometry"/></mxCell>`;
}
svg+='</g></svg>';await fs.writeFile(path.join(out,'diagrams',d.id+'.svg'),svg+'\n');pages.push(`<diagram id="${d.id}" name="${esc(d.title)}"><mxGraphModel dx="1160" dy="490" grid="1" gridSize="10" page="1" pageWidth="1160" pageHeight="490"><root>${xml}</root></mxGraphModel></diagram>`);
}
await fs.writeFile(path.join(out,'校园共享打印-设计图稿.drawio'),`<mxfile host="app.diagrams.net" version="24.7.17">${pages.join('')}</mxfile>\n`);
await fs.writeFile(path.join(out,'讲稿.md'),'# 第三周小组汇报讲稿\n\n建议总时长 8–10 分钟，含约 1 分钟原型演示。可由 A 讲需求与调度，B 讲后台与恢复，C 讲原型和集成计划。\n\n'+notes.map(n=>`## ${String(n.slide).padStart(2,'0')} ${n.title}\n\n${n.note}\n`).join('\n'));
await fs.writeFile(path.join(tmp,'deck-source.json'),JSON.stringify(P.toProto()));
const candidate=path.join(tmp,'candidate.pptx');await(await PresentationFile.exportPptx(P)).save(candidate);
for(let i=0;i<P.slides.items.length;i++){const b=await P.export({slide:P.slides.items[i],format:'png',scale:1});await fs.writeFile(path.join(tmp,'previews',`slide-${i+1}.png`),new Uint8Array(await b.arrayBuffer()));}
const finalPath=process.env.CPS_FINAL_PPTX || path.join(out,'第三周小组设计汇报.pptx');
const result=await finalizePresentation({workspaceDir:root,candidatePath:candidate,finalPath,pythonExecutable:PYTHON,
 integrityValidatorPath:path.join(SKILL_DIR,'container_tools/inspect_presentation_package_integrity.py'),layoutValidatorPath:path.join(SKILL_DIR,'container_tools/inspect_presentation_layout_geometry.py'),layoutArgs:['--expected-slide-size-emu','12192000,6858000','--validate-bullet-geometry','--validate-heading-fit'],
 explicitTotalSlideCount:16,requiredNativeTableOwnerSlides:[],requiredNativeChartOwnerSlides:[],fontPolicy:{basis:'design',families:[font]},verifyArtifactToolImport:true,receiptPath:path.join(tmp,path.basename(finalPath)+'.validation.json')});
console.log(JSON.stringify({finalPath,result}));

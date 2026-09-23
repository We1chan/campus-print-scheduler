// Shared diagram definitions: editable PowerPoint objects, SVG and draw.io pages.
export const diagrams=[];
const scene=(id,title)=>{const d={id,title,width:1160,height:490,items:[]};diagrams.push(d);return d;};
const box=(d,id,x,y,w,h,text,opts={})=>d.items.push({type:'box',id,x,y,w,h,text,...opts});
const text=(d,x,y,w,h,value,opts={})=>d.items.push({type:'text',x,y,w,h,text:value,...opts});
const edge=(d,points,opts={})=>d.items.push({type:'edge',points,...opts});
const ink='#19334A',blue='#245EA7',teal='#16877E';
let d=scene('mindmap','项目思维导图');
box(d,'root',440,202,280,82,'校园共享打印\n调度与故障恢复',{fill:ink,color:'#FFFFFF',bold:true,font:25});
const branches=[
 ['需求目标',25,15,'排队可见、双设备共享\n异常后恢复未完成任务',blue],
 ['调度与状态',25,202,'FCFS / SJF / 优先级老化\n原子领取、取消、有限重试',blue],
 ['进程与存储',25,389,'协调 / 监督 / 工作进程\nIPC、文件锁、原子快照',blue],
 ['界面交互',850,15,'任务提交与队列展示\n进度、取消确认、断线重连',teal],
 ['实验验证',850,202,'相同负载比较策略\n并发、故障、跨平台验收',teal],
 ['协作交付',850,389,'A 调度、B 后台、C 界面\nC++17 + Qt 6 + CMake',teal]];
for(let i=0;i<branches.length;i++){const [label,x,y,body,c]=branches[i];box(d,'b'+i,x,y,285,45,label,{fill:c,color:'#FFF',font:24});text(d,x,y+49,295,55,body,{font:21});edge(d,i<3?[[440,243],[365,243],[365,y+22],[310,y+22]]:[[720,243],[790,243],[790,y+22],[850,y+22]],{color:c,arrow:false});}

d=scene('usecase','UML 用例图');
box(d,'system',240,0,680,480,'',{fill:'#FFFFFF',stroke:'#B7C9D9'});text(d,260,10,620,35,'校园共享打印系统',{font:23,bold:true});
box(d,'student',5,165,190,78,'«actor»\n提交者',{fill:'#E9F0FA',font:23});box(d,'operator',965,165,190,78,'«actor»\n演示操作员',{fill:'#E5F2EF',font:23});
for(const [id,x,y,label] of [['submit',285,75,'提交打印任务'],['view',285,205,'查看队列与进度'],['cancel',285,335,'取消任务'],['policy',615,75,'选择调度策略'],['stats',615,205,'查看统计与日志'],['fault',615,335,'故障与恢复演示']])box(d,id,x,y,270,76,label,{shape:'ellipse',font:23,fill:'#F7FAFD'});
for(const y of [113,243,373])edge(d,[[195,204],[218,204],[218,y],[285,y]],{arrow:false});
for(const y of [113,243,373])edge(d,[[965,204],[942,204],[942,y],[885,y]],{arrow:false,color:teal});
text(d,285,434,590,34,'角色用于需求划分，本阶段不实现登录与权限系统',{font:18,color:'#63778B'});

d=scene('architecture','目标进程与持久化结构');
box(d,'gui',0,172,205,100,'Qt 图形界面\n本地 IPC 客户端',{fill:'#E9F0FA',font:24});
box(d,'coord',335,172,270,100,'print-coordinator\n持久队列与设备槽位',{fill:ink,color:'#FFF',font:24});
box(d,'scheduler',345,5,250,80,'Scheduler\n策略与状态机',{font:23});
box(d,'snapshot',345,380,250,84,'state.json\n原子快照 + 单实例锁',{font:23});
box(d,'supervisor',755,125,205,90,'supervisor × 2\n监督与有限重试',{font:22,fill:'#E5F2EF'});
box(d,'worker',755,330,205,90,'worker × 2\n模拟打印',{font:23,fill:'#E5F2EF'});
box(d,'output',1005,330,150,90,'输出文件\n原子提交',{font:21});
edge(d,[[205,222],[335,222]],{color:blue});text(d,205,161,128,55,'JSONL\n双向通信',{font:19,align:'center'});
edge(d,[[470,172],[470,85]]);edge(d,[[470,272],[470,380]]);text(d,490,302,200,28,'先持久化再执行',{font:18});
edge(d,[[605,208],[680,208],[680,170],[755,170]],{color:teal});
edge(d,[[857,215],[857,330]],{color:teal});text(d,875,261,180,30,'管道 JSONL',{font:19});edge(d,[[960,375],[1005,375]],{color:teal});
text(d,0,375,270,90,'后台与界面生命周期分离\nGUI 接入仍待联调',{font:21,color:blue});

d=scene('class','UML 核心类图');
function cls(id,x,y,w,title,attrs,methods){box(d,id,x,y,w,210,'',{fill:'#FFFFFF'});text(d,x+12,y+8,w-24,33,title,{font:24,bold:true,align:'center'});edge(d,[[x,y+50],[x+w,y+50]],{arrow:false});text(d,x+15,y+61,w-30,71,attrs,{font:20});edge(d,[[x,y+139],[x+w,y+139]],{arrow:false});text(d,x+15,y+150,w-30,56,methods,{font:19});}
cls('sched',420,5,320,'Scheduler','- mutex_ : mutex\n- jobs_ : vector<Job>','+ claim(policy, now) : Job?\n+ cancel(id, now) : bool');
cls('job',20,5,315,'Job','+ id : string\n+ pages, copies, priority : int','+ sheets() : int');
cls('run',825,5,315,'RunInterval','startedMs : TimeMs\nstoppedMs : optional<TimeMs>','记录单次执行区间');
box(d,'stats',50,337,295,100,'«data» Statistics\n等待、周转、吞吐、利用率',{font:22,fill:'#E9F0FA'});
box(d,'coordinator',420,337,320,100,'Coordinator\n持久化、派发与恢复对账',{font:22,fill:'#E5F2EF'});
box(d,'replay',825,337,315,100,'«function» replay()\n模拟到达、完成与设备槽位',{font:22,fill:'#E9F0FA'});
edge(d,[[420,96],[335,96]],{arrow:false});text(d,339,53,46,34,'0..*',{font:19});text(d,394,53,23,34,'1',{font:19});text(d,337,114,83,34,'持有',{font:19});
edge(d,[[174,215],[174,277],[982,277],[982,215]],{arrow:false});text(d,815,244,163,27,'Job 的 runs：0..*',{font:18});
edge(d,[[580,337],[580,215]],{dashed:true});text(d,593,292,175,28,'调用',{font:19});
edge(d,[[826,387],[774,387],[774,176],[740,176]],{dashed:true});
edge(d,[[420,175],[375,175],[375,308],[197,308],[197,337]],{dashed:true});text(d,210,280,160,28,'快照统计',{font:19});
text(d,0,453,1140,32,'展示核心关系与代表字段，省略 Qt 事件、协议及辅助成员',{font:18,color:'#63778B'});

d=scene('sequence','UML 任务执行时序图');
const lifelines=[['gui','GUI',80],['coord','Coordinator',315],['state','StateStore',550],['sup','Supervisor',785],['worker','Worker',1020]];
for(const [id,name,x]of lifelines){box(d,id,x-75,0,150,48,name,{font:20,fill:id==='coord'?ink:'#E9F0FA',color:id==='coord'?'#FFF':ink});edge(d,[[x,48],[x,477]],{arrow:false,dashed:true,color:'#AFBECD'});}
const msg=(a,b,y,label,opts={})=>{edge(d,[[a,y],[b,y]],opts);text(d,Math.min(a,b)+7,y-31,Math.abs(b-a)-14,29,label,{font:18,align:'center'});};
msg(80,315,90,'submit(job)');msg(315,550,145,'保存 Queued');msg(550,315,187,'保存成功',{dashed:true});
text(d,245,204,215,31,'claim()：原子领取',{font:19,align:'center',bold:true});msg(315,550,270,'保存 Running / attempt');
msg(315,785,328,'持久化成功后启动');msg(785,1020,373,'启动 worker');msg(1020,785,417,'进度 / 最终输出',{dashed:true});msg(785,315,461,'完成事件，核对后持久化',{dashed:true});
text(d,11,190,173,94,'GUI 收到快照\n或事件后刷新\n（返回消息略）',{font:18,color:'#63778B'});

d=scene('state','UML 任务状态图');
const nodes=[['queued',40,170,215,73,'Queued\n等待中'],['running',430,170,215,73,'Running\n执行中'],['completed',925,20,220,73,'Completed\n完成'],['failed',40,382,215,73,'Failed\n失败'],['cancelling',430,382,215,73,'Cancelling\n取消中'],['cancelled',925,382,220,73,'Cancelled\n已取消']];
for(const[id,x,y,w,h,label]of nodes)box(d,id,x,y,w,h,label,{shape:'roundRect',font:24,fill:['completed','cancelled'].includes(id)?'#E5F2EF':'#E9F0FA'});
box(d,'start',4,193,24,24,'',{shape:'ellipse',fill:ink,stroke:ink});edge(d,[[28,205],[40,205]]);
edge(d,[[255,206],[430,206]]);text(d,275,163,140,30,'claim()',{font:21});
edge(d,[[537,170],[537,58],[925,58]]);text(d,665,18,240,30,'确认成功输出',{font:20});
edge(d,[[485,243],[485,325],[147,325],[147,382]]);text(d,175,286,300,32,'失败 / 重试耗尽',{font:20});
edge(d,[[470,170],[470,101],[150,101],[150,170]]);text(d,177,62,265,30,'retry() [未达上限]',{font:19});
edge(d,[[537,243],[537,382]]);text(d,551,302,194,30,'cancel()',{font:20});
edge(d,[[645,418],[925,418]]);text(d,660,437,258,30,'acknowledgeCancel()',{font:19});
edge(d,[[40,220],[18,220],[18,484],[1020,484],[1020,455]]);text(d,65,457,310,26,'等待中直接 cancel()',{font:18});
edge(d,[[645,395],[855,395],[855,92],[925,92]],{dashed:true});text(d,650,117,196,50,'取消期间确认输出\n成功完成仍可生效',{font:18,color:teal});

d=scene('wireframe','原型页面线框与交互');
box(d,'queue',0,20,350,405,'',{fill:'#F7FAFD'});text(d,15,32,320,36,'打印队列',{font:26,bold:true});text(d,15,90,320,60,'设备 A：打印中\n设备 B：打印中',{font:22});box(d,'new',15,166,320,46,'提交任务',{fill:blue,color:'#FFF',font:23});text(d,15,237,320,120,'任务列表\n名称 / 状态 / 进度\n详情 / 取消',{font:23});
box(d,'form',405,20,350,405,'',{fill:'#F7FAFD'});text(d,425,32,310,36,'提交任务',{font:26,bold:true});for(const[t,y]of[['任务名称',105],['页数 / 份数',179],['优先级',253]])box(d,'field'+y,425,y,310,47,t,{font:22});box(d,'send',425,346,310,47,'确认提交',{fill:blue,color:'#FFF',font:23});
box(d,'confirm',810,100,350,246,'',{fill:'#F7FAFD'});text(d,830,118,310,40,'取消确认',{font:26,bold:true});text(d,830,180,310,58,'运行任务先显示“取消中”\n收到停止确认后结束',{font:21});box(d,'cancel-button',830,276,145,45,'确认取消',{font:21,fill:blue,color:'#FFF'});box(d,'keep-button',995,276,145,45,'保留任务',{font:21});
edge(d,[[350,190],[405,190]],{color:blue});edge(d,[[350,300],[380,300],[380,443],[985,443],[985,346]],{dashed:true,color:teal});text(d,490,415,255,28,'列表中点击取消',{font:20,color:teal});text(d,18,463,1120,27,'页面布局与交互规格，可点击版本见 prototype.html',{font:20,color:'#63778B'});

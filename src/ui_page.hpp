#pragma once

// The interface, as a single self-contained page. Kept in its own header so
// ui.cpp stays readable.

namespace tsuzuki::ui {

inline constexpr const char* kIndexHtml = R"HTMLPAGE(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Tsuzuki</title>
<style>
  :root{
    --bg:#0e0d17; --panel:#171528; --panel-2:#1f1c34; --line:#2b2745;
    --ink:#ece9f7; --dim:#9a94bd; --pink:#ff5c8d; --pink-soft:#ff8fb3;
    --cyan:#5be9e9; --gold:#ffd66b; --ok:#53da33; --accent:var(--pink);
  }
  *{box-sizing:border-box}
  body{
    margin:0;background:radial-gradient(1200px 600px at 78% -12%,#2a2350 0%,var(--bg) 60%);
    color:var(--ink);font:15px/1.5 "Segoe UI",system-ui,sans-serif;min-height:100vh;
  }
  header{
    display:flex;align-items:center;gap:14px;padding:16px 26px;
    border-bottom:1px solid var(--line);position:sticky;top:0;
    background:rgba(14,13,23,.9);backdrop-filter:blur(10px);z-index:20;
  }
  header .logo{width:34px;height:34px;flex:none}
  header h1{font-size:18px;margin:0;letter-spacing:.3px}
  header h1 span{color:var(--dim);font-weight:400;font-size:12.5px;margin-left:8px}
  main{max-width:1100px;margin:0 auto;padding:22px 26px 60px}
  .searchbar{display:flex;gap:10px;margin-bottom:8px;flex-wrap:wrap}
  input,select{
    background:var(--panel);border:1px solid var(--line);color:var(--ink);
    border-radius:10px;padding:12px 14px;font:inherit;outline:none;
  }
  input:focus,select:focus{border-color:var(--pink);box-shadow:0 0 0 3px rgba(255,92,141,.15)}
  #q{flex:1;min-width:240px}
  button{
    background:linear-gradient(180deg,var(--pink-soft),var(--pink));border:0;color:#2a0d18;
    font:600 14px/1 "Segoe UI",system-ui,sans-serif;padding:11px 18px;border-radius:9px;
    cursor:pointer;transition:transform .08s ease,filter .15s ease;
  }
  button:hover{filter:brightness(1.08)}
  button:active{transform:translateY(1px)}
  .hint{color:var(--dim);font-size:13px;margin:2px 0 18px}
  .back{background:transparent;border:1px solid var(--line);color:var(--dim);font-weight:500;margin-bottom:14px}
  .back:hover{color:var(--ink);border-color:var(--pink)}

  /* ---- search results ---- */
  .card{
    background:var(--panel);border:1px solid var(--line);border-radius:12px;
    padding:14px 16px;margin-bottom:10px;display:flex;gap:14px;align-items:center;
    cursor:pointer;transition:border-color .15s ease,background .15s ease;
  }
  .card:hover{border-color:var(--pink);background:var(--panel-2)}
  .card .title{flex:1;min-width:0}
  .card .title b{display:block;font-weight:600;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
  .badge{font:600 11px/1 "Segoe UI",sans-serif;padding:5px 8px;border-radius:6px;letter-spacing:.4px;flex:none}
  .b-best{background:rgba(255,214,107,.14);color:var(--gold);border:1px solid rgba(255,214,107,.4)}
  .b-high{background:rgba(83,218,51,.12);color:var(--ok);border:1px solid rgba(83,218,51,.32)}
  .b-med{background:rgba(154,148,189,.12);color:var(--dim);border:1px solid var(--line)}
  .seed{color:var(--ok);font-variant-numeric:tabular-nums;font-size:13px;flex:none;min-width:64px;text-align:right}
  .seed.none{color:var(--dim)}
  .size{color:var(--dim);font-size:13px;flex:none;min-width:62px;text-align:right;font-variant-numeric:tabular-nums}
  .src{color:var(--cyan);font-size:11px;opacity:.75}

  /* ---- show hero ---- */
  .hero{
    position:relative;border-radius:14px;overflow:hidden;margin-bottom:18px;
    border:1px solid var(--line);background:var(--panel);
  }
  .hero .bg{position:absolute;inset:0;background-size:cover;background-position:center 30%;opacity:.34;filter:saturate(1.1)}
  .hero .veil{position:absolute;inset:0;background:linear-gradient(90deg,var(--panel) 12%,rgba(23,21,40,.72) 55%,rgba(23,21,40,.35))}
  .hero .inner{position:relative;display:flex;gap:18px;padding:20px}
  .hero img.cover{width:104px;height:150px;object-fit:cover;border-radius:10px;flex:none;box-shadow:0 8px 26px rgba(0,0,0,.5)}
  .hero .who{min-width:0;display:flex;flex-direction:column;justify-content:center}
  .hero h2{margin:0 0 6px;font-size:22px;line-height:1.25}
  .hero .facts{color:var(--dim);font-size:13px;margin-bottom:9px}
  .hero .facts b{color:var(--accent);font-weight:600}
  .hero p{
    margin:0;color:var(--dim);font-size:13px;line-height:1.55;
    display:-webkit-box;-webkit-line-clamp:3;-webkit-box-orient:vertical;overflow:hidden;
  }

  /* ---- episode cards ---- */
  .eps{display:grid;grid-template-columns:1fr;gap:9px}
  .ep{
    display:flex;gap:14px;align-items:center;background:var(--panel);
    border:1px solid var(--line);border-radius:12px;padding:10px 14px 10px 10px;
    transition:border-color .15s ease,background .15s ease;
  }
  .ep:hover{border-color:var(--accent);background:var(--panel-2)}
  .ep .thumb{
    width:112px;height:63px;border-radius:8px;flex:none;object-fit:cover;
    background:var(--panel-2) center/cover;position:relative;overflow:hidden;
    display:flex;align-items:center;justify-content:center;
  }
  .ep .thumb span{
    font:700 19px/1 "Segoe UI",sans-serif;color:#fff;text-shadow:0 2px 10px rgba(0,0,0,.85);
  }
  .ep .body{flex:1;min-width:0}
  .ep .name{font-weight:600;font-size:14.5px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
  .ep .file{color:var(--dim);font-size:11.5px;margin-top:2px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
  .chips{display:flex;gap:5px;margin-top:6px;flex-wrap:wrap}
  .chip{
    font:600 10px/1 "Segoe UI",sans-serif;padding:4px 7px;border-radius:5px;letter-spacing:.3px;
    background:rgba(91,233,233,.09);color:var(--cyan);border:1px solid rgba(91,233,233,.24);
  }
  .chip.q{background:rgba(255,92,141,.1);color:var(--pink-soft);border-color:rgba(255,92,141,.28)}
  .ep .right{display:flex;align-items:center;gap:12px;flex:none}

  .extras{margin-top:20px}
  .extras h4{color:var(--dim);font-size:12px;text-transform:uppercase;letter-spacing:1px;margin:0 0 8px}
  .extras .ep{opacity:.45}
  .extras .ep:hover{opacity:.7}

  .panel{background:var(--panel);border:1px solid var(--line);border-radius:12px;overflow:hidden;margin-top:16px}
  .panel h3{margin:0;padding:13px 16px;border-bottom:1px solid var(--line);font-size:14px;font-weight:600}
  .bar{height:6px;background:var(--panel-2);border-radius:99px;overflow:hidden;margin-top:10px}
  .bar i{display:block;height:100%;background:linear-gradient(90deg,var(--accent),var(--cyan));width:0;transition:width .4s ease}
  .note{padding:14px 16px;color:var(--dim);font-size:13px}
  .warn{
    background:rgba(255,214,107,.08);border:1px solid rgba(255,214,107,.3);color:var(--gold);
    border-radius:10px;padding:12px 14px;font-size:13px;margin-bottom:14px;
  }
  .empty{text-align:center;padding:46px 20px;color:var(--dim)}
  .empty svg{width:150px;height:auto;opacity:.95;margin-bottom:4px}
  .spinner{
    width:15px;height:15px;border:2px solid var(--line);border-top-color:var(--pink);
    border-radius:50%;animation:spin .7s linear infinite;display:inline-block;vertical-align:-3px;margin-right:8px;
  }
  @keyframes spin{to{transform:rotate(360deg)}}

  /* ---- player control strip ---- */
  #playerbar{display:none}
  body.player{background:#0b0a12;min-height:0}
  body.player header,body.player .searchbar,body.player .hint,body.player #out{display:none}
  body.player main{padding:0;max-width:none}
  body.player #playerbar{
    display:flex;align-items:center;gap:14px;padding:0 18px;height:92px;
    background:linear-gradient(180deg,#15131f,#0f0e18);border-top:1px solid var(--line);
  }
  #playerbar .pb{
    background:var(--panel-2);border:1px solid var(--line);color:var(--ink);
    border-radius:9px;padding:9px 13px;font:600 13px/1 "Segoe UI",sans-serif;cursor:pointer;
  }
  #playerbar .pb:hover{border-color:var(--accent);color:var(--accent)}
  #playerbar .pb.primary{
    background:linear-gradient(180deg,var(--pink-soft),var(--pink));color:#2a0d18;border:0;
    min-width:52px;font-size:15px;
  }
  #playerbar .grow{flex:1;display:flex;flex-direction:column;gap:5px;min-width:120px}
  #playerbar .times{display:flex;justify-content:space-between;color:var(--dim);font-size:11.5px;
    font-variant-numeric:tabular-nums}
  #seek{
    -webkit-appearance:none;appearance:none;width:100%;height:6px;border-radius:99px;
    background:var(--panel-2);outline:none;cursor:pointer;padding:0;border:0;
  }
  #seek::-webkit-slider-thumb{
    -webkit-appearance:none;width:14px;height:14px;border-radius:50%;
    background:var(--accent);cursor:pointer;box-shadow:0 0 0 3px rgba(255,92,141,.2);
  }
  #playerbar select{padding:8px 10px;font-size:12.5px;max-width:190px}
  #playerbar .vol{width:88px}
  #playerbar .lbl{color:var(--dim);font-size:11px;text-transform:uppercase;letter-spacing:.8px}
  #playerbar .stack{display:flex;flex-direction:column;gap:3px}
  #buffered{color:var(--cyan);font-size:11.5px;white-space:nowrap}

  /* ---- settings ---- */
  .setrow{
    display:flex;align-items:center;gap:16px;padding:15px 16px;border-bottom:1px solid var(--line);
  }
  .setrow:last-child{border-bottom:0}
  .setrow .info{flex:1;min-width:0}
  .setrow .info b{display:block;font-weight:600;font-size:14px}
  .setrow .info span{color:var(--dim);font-size:12.5px}
  .setrow input[type=text],.setrow input[type=number],.setrow select{min-width:190px}
  .toggle{
    width:46px;height:26px;border-radius:99px;background:var(--panel-2);border:1px solid var(--line);
    position:relative;cursor:pointer;flex:none;transition:background .18s ease;
  }
  .toggle::after{
    content:"";position:absolute;top:3px;left:3px;width:18px;height:18px;border-radius:50%;
    background:var(--dim);transition:transform .18s ease,background .18s ease;
  }
  .toggle.on{background:rgba(255,92,141,.22);border-color:var(--accent)}
  .toggle.on::after{transform:translateX(20px);background:var(--accent)}
  .gear{
    background:transparent;border:1px solid var(--line);color:var(--dim);
    margin-left:auto;font-weight:500;
  }
  .gear:hover{color:var(--ink);border-color:var(--pink)}
</style>
)HTMLPAGE"
    R"HTMLPAGE(</head>
<body>
<header>
  <svg class="logo" viewBox="0 0 128 128"><defs>
    <linearGradient id="lg" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#453A7A"/><stop offset="1" stop-color="#1B1B2F"/></linearGradient>
    <linearGradient id="la" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#FF8FB3"/><stop offset="1" stop-color="#FF5C8D"/></linearGradient>
  </defs>
    <rect width="128" height="128" rx="30" fill="url(#lg)"/>
    <path d="M28 38 L28 90 L68 64 Z" fill="url(#la)"/>
    <circle cx="82" cy="64" r="5.5" fill="#5BE9E9"/><circle cx="96" cy="64" r="5.5" fill="#5BE9E9" opacity=".72"/><circle cx="110" cy="64" r="5.5" fill="#5BE9E9" opacity=".45"/>
  </svg>
  <h1>Tsuzuki <span>&#32154;&#12365; &middot; to be continued</span></h1>
  <button class="gear" id="gear">Settings</button>
</header>

<main>
  <div class="searchbar">
    <input id="q" placeholder="Search an anime, or paste a magnet link" autofocus>
    <input id="ep" type="number" min="1" placeholder="Ep #" style="width:92px">
    <select id="res">
      <option value="">Any quality</option>
      <option value="1080">1080p</option>
      <option value="720">720p</option>
      <option value="480">480p</option>
    </select>
    <button id="go">Search</button>
  </div>
  <div class="hint">Downloads are deleted after you finish watching. Playback opens in mpv.</div>
  <div id="out"></div>

  <div id="playerbar">
    <button class="pb" id="pStop" title="Stop and go back">&#9632;</button>
    <button class="pb primary" id="pPlay" title="Play / pause (space)">&#9208;</button>
    <button class="pb" id="pBack" title="Back 10s">&#8630; 10</button>
    <button class="pb" id="pFwd" title="Forward 30s">30 &#8631;</button>
    <div class="grow">
      <input type="range" id="seek" min="0" max="1000" value="0">
      <div class="times"><span id="tNow">0:00</span><span id="buffered"></span><span id="tEnd">0:00</span></div>
    </div>
    <div class="stack"><span class="lbl">Audio</span><select id="aTrack"></select></div>
    <div class="stack"><span class="lbl">Subtitles</span><select id="sTrack"></select></div>
    <div class="stack"><span class="lbl">Volume</span><input type="range" class="vol" id="vol" min="0" max="130" value="100"></div>
  </div>
</main>

<script>
const $ = s => document.querySelector(s);
const out = $('#out');
const esc = s => String(s==null?'':s).replace(/[&<>"]/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
const size = b => { if(!b) return '-'; const u=['B','K','M','G','T']; let i=0,v=b; while(v>=1024&&i<4){v/=1024;i++;} return v.toFixed(v<10&&i>0?1:0)+u[i]; };

let lastResults = null, lastAnilistId = null;

function busy(msg){ out.innerHTML = '<div class="empty"><span class="spinner"></span><span id="busytext">'+esc(msg)+'</span></div>'; }
function mascot(msg){ out.innerHTML = '<div class="empty">'+MASCOT+'<p>'+esc(msg)+'</p></div>'; }

// Release tags pulled straight out of the filename. This is information the
// torrent already carries and most clients throw away.
function chipsFor(name){
  const tests = [
    [/\b(2160p|4k)\b/i,'4K','q'], [/\b1080p\b/i,'1080p','q'], [/\b720p\b/i,'720p','q'], [/\b480p\b/i,'480p','q'],
    [/\bremux\b/i,'REMUX',''], [/\b(bd|bdrip|blu-?ray)\b/i,'BD',''], [/\b(web-?dl|webrip)\b/i,'WEB',''],
    [/\b(hevc|x265|h\.?265)\b/i,'HEVC',''], [/\b(avc|x264|h\.?264)\b/i,'H.264',''], [/\bav1\b/i,'AV1',''],
    [/\bflac\b/i,'FLAC',''], [/\bopus\b/i,'Opus',''], [/\b(e-?ac-?3|ddp|aac)\b/i,'AAC',''],
    [/dual[- .]?audio/i,'Dual Audio',''], [/\b10-?bits?\b/i,'10-bit',''],
  ];
  const seen = new Set(), out = [];
  for(const [re,label,cls] of tests){
    if(re.test(name) && !seen.has(label)){ seen.add(label); out.push('<span class="chip '+cls+'">'+label+'</span>'); }
  }
  return out.slice(0,6).join('');
}

)HTMLPAGE"
    R"HTMLPAGE(async function search(){
  const q = $('#q').value.trim();
  if(!q) return;
  if(q.startsWith('magnet:')) return openTorrent(q);
  busy('Searching Nyaa, AnimeTosho, SubsPlease and SeaDex...');
  let j;
  try { j = await (await fetch('/api/search?q='+encodeURIComponent(q)+'&res='+encodeURIComponent($('#res').value))).json(); }
  catch(err){ return mascot('Search failed: '+err.message); }
  if(j.error) return mascot(j.error);
  if(!j.results || !j.results.length) return mascot('Nothing found for "'+q+'".');

  lastResults = j; lastAnilistId = j.anilist ? j.anilist.id : null;
  renderResults();
}

function renderResults(){
  const j = lastResults;
  let html = '';
  if(j.anilist) html += '<div class="hint">Matched <b>'+esc(j.anilist.title)+'</b> on AniList &middot; '+
    (j.anilist.episodes ? j.anilist.episodes+' episodes' : 'ongoing')+'</div>';
  for(const x of j.results){
    const cls = x.best ? 'b-best' : (x.accuracy==='high' ? 'b-high' : 'b-med');
    html += '<div class="card" data-magnet="'+esc(x.magnet)+'">'+
      '<span class="badge '+cls+'">'+(x.best?'BEST':x.accuracy.toUpperCase())+'</span>'+
      '<div class="title"><b>'+esc(x.title)+'</b><div class="src">'+esc(x.source)+'</div></div>'+
      '<div class="seed'+(x.seeders?'':' none')+'">'+(x.seeders? x.seeders+' &#9650;':'&ndash;')+'</div>'+
      '<div class="size">'+size(x.size)+'</div></div>';
  }
  out.innerHTML = html;
  document.querySelectorAll('.card').forEach(c => c.onclick = () => openTorrent(c.dataset.magnet));
}

// While /api/open blocks on the DHT, poll the engine for live status so the
// spinner reports peers and elapsed time instead of looking hung.
let openPoll = null;
function startOpenPoll(){
  stopOpenPoll();
  openPoll = setInterval(async () => {
    try {
      const s = await (await fetch('/api/status')).json();
      const t = $('#busytext');
      if(t && s.message) t.textContent = s.message;
    } catch(e){}
  }, 900);
}
function stopOpenPoll(){ if(openPoll){ clearInterval(openPoll); openPoll = null; } }

async function openTorrent(magnet){
  busy('Contacting peers for torrent metadata...');
  startOpenPoll();
  const want = $('#ep').value.trim();
  let j;
  try {
    j = await (await fetch('/api/open', {method:'POST',headers:{'Content-Type':'application/json'},
      body: JSON.stringify({magnet, episode: want? parseInt(want,10):null, anilistId: lastAnilistId})})).json();
  } catch(err){ stopOpenPoll(); return mascot('Could not open that torrent: '+err.message); }
  stopOpenPoll();
  if(j.error) return mascot(j.error);
  renderEpisodes(magnet, j);
}

function renderEpisodes(magnet, j){
  const show = j.show || {};
  const info = j.episodeInfo || {};
  if(show.color) document.documentElement.style.setProperty('--accent', show.color);

  let html = '<button class="back" id="back">&larr; Back to results</button>';

  if(show.title){
    html += '<div class="hero">'+
      (show.banner? '<div class="bg" style="background-image:url('+esc(show.banner)+')"></div>':'')+
      '<div class="veil"></div><div class="inner">'+
      (show.cover? '<img class="cover" src="'+esc(show.cover)+'" alt="">':'')+
      '<div class="who"><h2>'+esc(show.title)+'</h2><div class="facts">'+
        (show.episodes? '<b>'+show.episodes+'</b> episodes':'ongoing')+
        (show.duration? ' &middot; ~'+show.duration+' min':'')+
        ' &middot; '+esc(j.name)+
      '</div>'+(show.description? '<p>'+esc(show.description)+'</p>':'')+'</div></div></div>';
  } else {
    html += '<div class="panel"><h3>'+esc(j.name)+'</h3></div>';
  }

  if(j.refused){
    html += '<div class="warn">Episode '+esc(j.wanted)+' is not clearly in this torrent, so nothing '+
            'was played. Pick a file below instead.</div>';
  }

  const eps = j.files.filter(f => !f.skipped);
  const extras = j.files.filter(f => f.skipped);

  html += '<div class="eps">';
  for(const f of eps){
    const num = (f.episode||'').replace(/^EP\s*/,'');
    const meta = info[num] || {};
    const thumbStyle = meta.thumb ? 'background-image:url('+esc(meta.thumb)+')'
                     : (show.cover ? 'background-image:url('+esc(show.cover)+');background-position:center 22%' : '');
    const label = meta.title ? meta.title : (f.episode==='??' ? 'Unrecognised' : 'Episode '+num);
    const hot = (j.target === f.index) ? ' style="border-color:var(--accent)"' : '';
    html += '<div class="ep"'+hot+'>'+
      '<div class="thumb" style="'+thumbStyle+'"><span>'+esc(f.episode||'?')+'</span></div>'+
      '<div class="body"><div class="name">'+esc(label)+'</div>'+
        '<div class="file">'+esc(f.name)+'</div>'+
        '<div class="chips">'+chipsFor(f.name)+'</div></div>'+
      '<div class="right"><span class="size">'+size(f.size)+'</span>'+
        '<button data-i="'+f.index+'">Play</button></div></div>';
  }
  html += '</div>';

  if(extras.length){
    html += '<div class="extras"><h4>Openings, endings and extras &mdash; not episodes</h4><div class="eps">';
    for(const f of extras){
      html += '<div class="ep"><div class="thumb"><span>'+esc(f.reason||'--')+'</span></div>'+
        '<div class="body"><div class="name">'+esc(f.reason||'Extra')+'</div>'+
        '<div class="file">'+esc(f.name)+'</div></div>'+
        '<div class="right"><span class="size">'+size(f.size)+'</span>'+
        '<button data-i="'+f.index+'">Play</button></div></div>';
    }
    html += '</div></div>';
  }

  html += '<div class="panel" id="status" style="display:none"><h3>Streaming</h3>'+
          '<div class="note"><span id="stext">Starting...</span><div class="bar"><i id="sbar"></i></div></div></div>';

  out.innerHTML = html;
  $('#back').onclick = () => { if(lastResults) renderResults(); else mascot('Search for something to watch.'); };
  document.querySelectorAll('.ep button').forEach(b => b.onclick = () => play(magnet, parseInt(b.dataset.i,10)));
}

async function play(magnet, index){
  $('#status').style.display = '';
  $('#stext').textContent = 'Buffering...';
  $('#status').scrollIntoView({behavior:'smooth', block:'nearest'});
  await fetch('/api/play', {method:'POST',headers:{'Content-Type':'application/json'},
    body: JSON.stringify({magnet, index})});
  poll();
}

async function poll(){
  let j;
  try { j = await (await fetch('/api/status')).json(); } catch(e){ return; }
  if($('#stext')){
    $('#stext').textContent = j.message || '';
    $('#sbar').style.width = (j.progress||0)+'%';
  }
  if(!j.done) setTimeout(poll, 700);
}

$('#go').onclick = search;
$('#q').addEventListener('keydown', e => { if(e.key==='Enter') search(); });

)HTMLPAGE"
    R"HTMLPAGE(
/* ---------------------------------------------------------- player bar */

const fmt = t => {
  if(!isFinite(t) || t < 0) t = 0;
  const m = Math.floor(t/60), sec = Math.floor(t%60);
  return m + ':' + String(sec).padStart(2,'0');
};

let seeking = false, inPlayer = false;

async function playerCmd(action, value){
  try{
    await fetch('/api/player/command', {method:'POST',headers:{'Content-Type':'application/json'},
      body: JSON.stringify({action, value})});
  }catch(e){}
}

function fillTracks(sel, tracks, type, noneLabel){
  const want = tracks.filter(t => t.type === type);
  const cur = want.find(t => t.selected);
  const key = want.map(t => t.id+':'+t.selected).join(',');
  if(sel.dataset.key === key) return;
  sel.dataset.key = key;
  sel.innerHTML = '';
  if(noneLabel){
    const o = document.createElement('option');
    o.value = '0'; o.textContent = noneLabel;
    sel.appendChild(o);
  }
  for(const t of want){
    const o = document.createElement('option');
    o.value = String(t.id);
    const bits = [];
    if(t.lang) bits.push(t.lang.toUpperCase());
    if(t.title) bits.push(t.title);
    if(!bits.length && t.codec) bits.push(t.codec);
    o.textContent = bits.join(' - ') || ('Track ' + t.id);
    sel.appendChild(o);
  }
  sel.value = cur ? String(cur.id) : '0';
}

async function refreshPlayer(){
  let st;
  try{ st = await (await fetch('/api/player/state')).json(); }catch(e){ return; }
  if(!st.running) return;

  $('#pPlay').innerHTML = st.paused ? '&#9654;' : '&#9208;';
  if(!seeking && st.duration > 0){
    $('#seek').value = String(Math.round(st.position / st.duration * 1000));
  }
  $('#tNow').textContent = fmt(st.position);
  $('#tEnd').textContent = fmt(st.duration);
  $('#buffered').textContent = st.buffered || '';
  fillTracks($('#aTrack'), st.tracks, 'audio', null);
  fillTracks($('#sTrack'), st.tracks, 'sub', 'Off');
  if(document.activeElement !== $('#vol')) $('#vol').value = String(st.volume);
}

function enterPlayer(){
  if(inPlayer) return;
  inPlayer = true;
  document.body.classList.add('player');
}
function exitPlayer(){
  if(!inPlayer) return;
  inPlayer = false;
  document.body.classList.remove('player');
}

$('#pStop').onclick = () => { playerCmd('stop'); exitPlayer(); };
$('#pPlay').onclick = () => playerCmd('pause');
$('#pBack').onclick = () => playerCmd('seek', -10);
$('#pFwd').onclick  = () => playerCmd('seek', 30);
$('#seek').addEventListener('input', () => { seeking = true; });
$('#seek').addEventListener('change', async () => {
  let st;
  try{ st = await (await fetch('/api/player/state')).json(); }catch(e){ seeking=false; return; }
  if(st.duration > 0) playerCmd('seekTo', $('#seek').value / 1000 * st.duration);
  seeking = false;
});
$('#aTrack').onchange = () => playerCmd('audio', parseInt($('#aTrack').value,10));
$('#sTrack').onchange = () => playerCmd('sub', parseInt($('#sTrack').value,10));
$('#vol').onchange = () => playerCmd('volume', parseInt($('#vol').value,10));

// Heartbeat: the app swaps the window layout when playback starts, so the page
// has to notice on its own rather than only when it started the playback.
setInterval(async () => {
  let s;
  try{ s = await (await fetch('/api/status')).json(); }catch(e){ return; }
  if(s.playing){ enterPlayer(); refreshPlayer(); }
  else { exitPlayer(); }
}, 900);

/* ------------------------------------------------------------ settings */

const SETTING_ROWS = [
  ['savePath','text','Download folder','Where episodes are stored while you watch them.'],
  ['speedLimit','number','Speed limit (Mb/s)','0 means unlimited.'],
  ['maxConnections','number','Max connections','Peers per torrent. Higher is faster but noisier.'],
  ['quality','select','Preferred quality','Used as the default in search.','|1080|720|480'],
  ['audioLang','text','Preferred audio language','Track code to select automatically, e.g. jpn or eng.'],
  ['subLang','text','Preferred subtitle language','Track code to select automatically, e.g. eng.'],
  ['bufferSeconds','number','Extra buffer (seconds)','0 lets Tsuzuki size the buffer from measured speed.'],
  ['deleteAfter','toggle','Delete after watching','Remove each episode once you finish it.'],
  ['subsOn','toggle','Subtitles on by default','Applies when a subtitle track exists.'],
];

async function showSettings(){
  let cfg = {};
  try{ cfg = await (await fetch('/api/settings')).json(); }catch(e){}
  let html = '<button class="back" id="sBack">&larr; Back</button>' +
             '<div class="panel"><h3>Settings</h3>';
  for(const [key,type,label,desc,opts] of SETTING_ROWS){
    let control;
    if(type === 'toggle'){
      control = '<div class="toggle'+(cfg[key]?' on':'')+'" data-k="'+key+'"></div>';
    } else if(type === 'select'){
      const choices = opts.split('|');
      control = '<select data-k="'+key+'">' + choices.map(v =>
        '<option value="'+v+'"'+(String(cfg[key]||'')===v?' selected':'')+'>'+(v||'Any')+(v?'p':'')+'</option>').join('') + '</select>';
    } else {
      control = '<input type="'+type+'" data-k="'+key+'" value="'+esc(cfg[key]==null?'':cfg[key])+'">';
    }
    html += '<div class="setrow"><div class="info"><b>'+esc(label)+'</b><span>'+esc(desc)+'</span></div>'+control+'</div>';
  }
  html += '</div><div class="hint" id="sSaved" style="margin-top:12px"></div>';
  out.innerHTML = html;

  $('#sBack').onclick = () => mascot('Search for something to watch.');
  const save = async () => {
    const next = {};
    document.querySelectorAll('[data-k]').forEach(el => {
      const k = el.dataset.k;
      if(el.classList.contains('toggle')) next[k] = el.classList.contains('on');
      else if(el.type === 'number') next[k] = parseFloat(el.value) || 0;
      else next[k] = el.value;
    });
    await fetch('/api/settings', {method:'POST',headers:{'Content-Type':'application/json'},
      body: JSON.stringify(next)});
    $('#sSaved').textContent = 'Saved.';
    setTimeout(() => { if($('#sSaved')) $('#sSaved').textContent = ''; }, 1600);
  };
  document.querySelectorAll('.toggle[data-k]').forEach(t =>
    t.onclick = () => { t.classList.toggle('on'); save(); });
  document.querySelectorAll('input[data-k],select[data-k]').forEach(el =>
    el.onchange = save);
}

$('#gear').onclick = showSettings;

const MASCOT = `)HTMLPAGE"
    R"HTMLPAGE(<svg viewBox="0 0 200 250" xmlns="http://www.w3.org/2000/svg"><defs>
<linearGradient id="mh" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#4A417F"/><stop offset="1" stop-color="#2B2650"/></linearGradient>
<linearGradient id="mo" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#4B3D86"/><stop offset="1" stop-color="#332A5E"/></linearGradient>
<linearGradient id="mt" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#FF8FB3"/><stop offset="1" stop-color="#FF5C8D"/></linearGradient></defs>
<path d="M38 108 Q30 42 100 36 Q170 42 162 108 L164 200 Q142 186 132 196 L130 128 L70 128 L68 196 Q58 186 36 200 Z" fill="url(#mh)"/>
<path d="M36 118 Q16 150 26 188 Q40 168 44 138 Z" fill="url(#mh)"/>
<path d="M164 118 Q184 150 174 188 Q160 168 156 138 Z" fill="url(#mh)"/>
<path d="M89 138 L111 138 L111 158 Q100 165 89 158 Z" fill="#EFC0AB"/>
<path d="M54 250 Q54 176 100 164 Q146 176 146 250 Z" fill="url(#mo)"/>
<path d="M100 166 Q88 172 84 182 Q94 178 100 174 Q106 178 116 182 Q112 172 100 166 Z" fill="#2A2350" opacity=".55"/>
<path d="M100 182 L93 198 L100 207 L107 198 Z" fill="#5BE9E9" opacity=".85"/>
<ellipse cx="100" cy="100" rx="47" ry="46" fill="#FFE3D4"/>
<path d="M52 96 Q50 46 100 42 Q150 46 148 96 Q142 70 120 64 Q110 84 94 64 Q70 70 60 96 Z" fill="url(#mh)"/>
<path d="M96 43 Q102 22 120 19 Q104 30 106 44 Z" fill="url(#mh)"/>
<ellipse cx="77" cy="107" rx="10.5" ry="13" fill="#241F45"/><ellipse cx="123" cy="107" rx="10.5" ry="13" fill="#241F45"/>
<ellipse cx="77" cy="110" rx="8.5" ry="9" fill="#7B6BE0"/><ellipse cx="123" cy="110" rx="8.5" ry="9" fill="#7B6BE0"/>
<circle cx="80.5" cy="102" r="4.2" fill="#fff"/><circle cx="126.5" cy="102" r="4.2" fill="#fff"/>
<path d="M67 90 Q77 86 87 89" stroke="#3B3468" stroke-width="2.6" fill="none" stroke-linecap="round"/>
<path d="M113 89 Q123 86 133 90" stroke="#3B3468" stroke-width="2.6" fill="none" stroke-linecap="round"/>
<ellipse cx="62" cy="119" rx="9" ry="5.5" fill="#FF9DBB" opacity=".55"/><ellipse cx="138" cy="119" rx="9" ry="5.5" fill="#FF9DBB" opacity=".55"/>
<path d="M93 124 Q100 131 107 124" stroke="#C2687A" stroke-width="2.6" fill="none" stroke-linecap="round"/>
<path d="M132 60 L132 76 L146 68 Z" fill="url(#mt)"/></svg>`;

// ?magnet=... opens a torrent straight away, so a specific release can be
// linked to. ?q=... runs a search on load.
const params = new URLSearchParams(location.search);
if(params.get('settings')){
  showSettings();
} else if(params.get('magnet')){
  if(params.get('ep')) $('#ep').value = params.get('ep');
  if(params.get('anilist')) lastAnilistId = parseInt(params.get('anilist'),10);
  openTorrent(params.get('magnet'));
} else if(params.get('q')){
  $('#q').value = params.get('q');
  if(params.get('ep')) $('#ep').value = params.get('ep');
  if(params.get('res')) $('#res').value = params.get('res');
  search();
} else {
  mascot('Search for something to watch.');
}
</script>
</body>
</html>
)HTMLPAGE";

}  // namespace tsuzuki::ui

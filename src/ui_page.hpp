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
    --cyan:#5be9e9; --gold:#ffd66b; --ok:#53da33;
  }
  *{box-sizing:border-box}
  body{
    margin:0;background:radial-gradient(1200px 600px at 78% -12%,#2a2350 0%,var(--bg) 60%);
    color:var(--ink);font:15px/1.5 "Segoe UI",system-ui,sans-serif;min-height:100vh;
  }
  header{
    display:flex;align-items:center;gap:14px;padding:18px 26px;
    border-bottom:1px solid var(--line);position:sticky;top:0;
    background:rgba(14,13,23,.86);backdrop-filter:blur(10px);z-index:5;
  }
  header .logo{width:38px;height:38px;flex:none}
  header h1{font-size:19px;margin:0;letter-spacing:.3px}
  header h1 span{color:var(--dim);font-weight:400;font-size:13px;margin-left:8px}
  main{max-width:1080px;margin:0 auto;padding:26px}
  .searchbar{display:flex;gap:10px;margin-bottom:8px;flex-wrap:wrap}
  input,select{
    background:var(--panel);border:1px solid var(--line);color:var(--ink);
    border-radius:10px;padding:12px 14px;font:inherit;outline:none;
  }
  input:focus,select:focus{border-color:var(--pink);box-shadow:0 0 0 3px rgba(255,92,141,.15)}
  #q{flex:1;min-width:240px}
  button{
    background:linear-gradient(180deg,var(--pink-soft),var(--pink));border:0;color:#2a0d18;
    font:600 15px/1 "Segoe UI",system-ui,sans-serif;padding:13px 20px;border-radius:10px;
    cursor:pointer;transition:transform .08s ease,filter .15s ease;
  }
  button:hover{filter:brightness(1.08)}
  button:active{transform:translateY(1px)}
  button.ghost{background:transparent;border:1px solid var(--line);color:var(--dim);font-weight:500}
  button.ghost:hover{color:var(--ink);border-color:var(--pink)}
  .hint{color:var(--dim);font-size:13px;margin:2px 0 20px}
  .card{
    background:var(--panel);border:1px solid var(--line);border-radius:12px;
    padding:14px 16px;margin-bottom:10px;display:flex;gap:14px;align-items:center;
    cursor:pointer;transition:border-color .15s ease,background .15s ease,transform .08s ease;
  }
  .card:hover{border-color:var(--pink);background:var(--panel-2)}
  .card:active{transform:scale(.996)}
  .card .title{flex:1;min-width:0}
  .card .title b{display:block;font-weight:600;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
  .card .meta{color:var(--dim);font-size:12.5px;margin-top:3px}
  .badge{
    font:600 11px/1 "Segoe UI",sans-serif;padding:5px 8px;border-radius:6px;
    letter-spacing:.4px;flex:none;
  }
  .b-best{background:rgba(255,214,107,.14);color:var(--gold);border:1px solid rgba(255,214,107,.4)}
  .b-high{background:rgba(83,218,51,.12);color:var(--ok);border:1px solid rgba(83,218,51,.32)}
  .b-med{background:rgba(154,148,189,.12);color:var(--dim);border:1px solid var(--line)}
  .seed{color:var(--ok);font-variant-numeric:tabular-nums;font-size:13px;flex:none;min-width:64px;text-align:right}
  .seed.none{color:var(--dim)}
  .size{color:var(--dim);font-size:13px;flex:none;min-width:64px;text-align:right;font-variant-numeric:tabular-nums}
  .src{color:var(--cyan);font-size:11px;opacity:.75}
  .empty{text-align:center;padding:50px 20px;color:var(--dim)}
  .empty svg{width:150px;height:auto;opacity:.95;margin-bottom:6px}
  .empty p{margin:6px 0 0}
  .row{display:flex;align-items:center;gap:12px;padding:11px 14px;border-bottom:1px solid var(--line)}
  .row:last-child{border-bottom:0}
  .row.skip{opacity:.42}
  .ep{
    font:600 12px/1 "Segoe UI",sans-serif;background:var(--panel-2);border:1px solid var(--line);
    padding:6px 9px;border-radius:6px;min-width:56px;text-align:center;flex:none;
  }
  .ep.match{background:rgba(255,92,141,.16);border-color:var(--pink);color:var(--pink-soft)}
  .fname{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;font-size:13.5px}
  .panel{background:var(--panel);border:1px solid var(--line);border-radius:12px;overflow:hidden;margin-top:14px}
  .panel h3{margin:0;padding:14px 16px;border-bottom:1px solid var(--line);font-size:14px;font-weight:600}
  .bar{height:6px;background:var(--panel-2);border-radius:99px;overflow:hidden;margin-top:10px}
  .bar i{display:block;height:100%;background:linear-gradient(90deg,var(--pink),var(--cyan));width:0;transition:width .4s ease}
  .note{padding:14px 16px;color:var(--dim);font-size:13px}
  .note.warn{color:var(--gold)}
  .spinner{
    width:15px;height:15px;border:2px solid var(--line);border-top-color:var(--pink);
    border-radius:50%;animation:spin .7s linear infinite;display:inline-block;vertical-align:-3px;margin-right:7px;
  }
  @keyframes spin{to{transform:rotate(360deg)}}
</style>
</head>
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
</header>

<main>
  <div class="searchbar">
    <input id="q" placeholder="Search an anime, or paste a magnet link" autofocus>
    <input id="ep" type="number" min="1" placeholder="Ep #" style="width:96px">
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
</main>

<script>
const $ = s => document.querySelector(s);
const out = $('#out');
const esc = s => String(s).replace(/[&<>"]/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
const size = b => { if(!b) return '-'; const u=['B','K','M','G','T']; let i=0,v=b; while(v>=1024&&i<4){v/=1024;i++;} return v.toFixed(v<10&&i>0?1:0)+u[i]; };

function busy(msg){ out.innerHTML = '<div class="empty"><span class="spinner"></span>'+esc(msg)+'</div>'; }
function mascot(msg){
  out.innerHTML = '<div class="empty">'+MASCOT+'<p>'+esc(msg)+'</p></div>';
}

async function search(){
  const q = $('#q').value.trim();
  if(!q) return;
  if(q.startsWith('magnet:')){ return openTorrent(q); }
  busy('Searching Nyaa, AnimeTosho, SubsPlease and SeaDex...');
  const r = await fetch('/api/search?q='+encodeURIComponent(q)+'&res='+encodeURIComponent($('#res').value));
  const j = await r.json();
  if(j.error){ mascot(j.error); return; }
  if(!j.results || !j.results.length){ mascot('Nothing found for "'+q+'".'); return; }

  let html = '';
  if(j.anilist) html += '<div class="hint">Matched <b>'+esc(j.anilist.title)+'</b> on AniList &middot; '+
    (j.anilist.episodes? j.anilist.episodes+' episodes':'ongoing')+'</div>';
  for(const x of j.results){
    const cls = x.best ? 'b-best' : (x.accuracy==='high' ? 'b-high' : 'b-med');
    const lbl = x.best ? 'BEST' : x.accuracy.toUpperCase();
    html += '<div class="card" data-magnet="'+esc(x.magnet)+'">'+
      '<span class="badge '+cls+'">'+lbl+'</span>'+
      '<div class="title"><b>'+esc(x.title)+'</b><div class="meta"><span class="src">'+esc(x.source)+'</span></div></div>'+
      '<div class="seed'+(x.seeders?'':' none')+'">'+(x.seeders? x.seeders+' &#9650;':'&ndash;')+'</div>'+
      '<div class="size">'+size(x.size)+'</div></div>';
  }
  out.innerHTML = html;
  document.querySelectorAll('.card').forEach(c =>
    c.onclick = () => openTorrent(c.dataset.magnet));
}

async function openTorrent(magnet){
  busy('Fetching torrent metadata...');
  const want = $('#ep').value.trim();
  const r = await fetch('/api/open', {method:'POST',headers:{'Content-Type':'application/json'},
    body: JSON.stringify({magnet, episode: want? parseInt(want,10) : null})});
  const j = await r.json();
  if(j.error){ mascot(j.error); return; }

  let html = '<div class="panel"><h3>'+esc(j.name)+'</h3>';
  if(j.refused){
    html += '<div class="note warn">Episode '+esc(j.wanted)+' is not clearly in this torrent &mdash; '+
            'nothing was played. Pick a file yourself below.</div>';
  }
  for(const f of j.files){
    const cls = f.skipped ? 'row skip' : 'row';
    const epc = (j.target === f.index) ? 'ep match' : 'ep';
    const label = f.skipped ? (f.reason||'skip') : (f.episode || '??');
    html += '<div class="'+cls+'">'+
      '<span class="'+epc+'">'+esc(label)+'</span>'+
      '<span class="fname">'+esc(f.name)+'</span>'+
      '<span class="size">'+size(f.size)+'</span>'+
      (f.skipped? '' : '<button data-i="'+f.index+'">Play</button>')+
      '</div>';
  }
  html += '</div><div class="panel" id="status" style="display:none"><h3>Streaming</h3>'+
          '<div class="note"><span id="stext">Starting...</span><div class="bar"><i id="sbar"></i></div></div></div>';
  out.innerHTML = html;
  document.querySelectorAll('.row button').forEach(b =>
    b.onclick = () => play(magnet, parseInt(b.dataset.i,10)));
}

async function play(magnet, index){
  $('#status').style.display = '';
  $('#stext').textContent = 'Buffering...';
  await fetch('/api/play', {method:'POST',headers:{'Content-Type':'application/json'},
    body: JSON.stringify({magnet, index})});
  poll();
}

async function poll(){
  const r = await fetch('/api/status');
  const j = await r.json();
  if($('#stext')){
    $('#stext').textContent = j.message || '';
    $('#sbar').style.width = (j.progress||0)+'%';
  }
  if(!j.done) setTimeout(poll, 700);
}

$('#go').onclick = search;
$('#q').addEventListener('keydown', e => { if(e.key==='Enter') search(); });

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

// ?q=... runs a search on load, so a search can be bookmarked or linked.
const params = new URLSearchParams(location.search);
if(params.get('q')){
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

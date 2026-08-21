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
  header h1,header .logo{cursor:pointer}
  header h1:hover{color:var(--pink-soft)}
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

  /* ---- themes ---- */
  html[data-theme="blackout"]{
    --bg:#000; --panel:#0b0b0b; --panel-2:#141414; --line:#242424;
    --ink:#f2f2f2; --dim:#8a8a8a; --pink:#ffffff; --pink-soft:#dcdcdc; --cyan:#bdbdbd;
  }
  html[data-theme="blackout"] button{color:#111}
  html[data-theme="whiteout"]{
    --bg:#f4f4f7; --panel:#ffffff; --panel-2:#ececf2; --line:#d6d6e0;
    --ink:#16161c; --dim:#61616e; --pink:#2b2b31; --pink-soft:#4a4a55;
    --cyan:#3a6ea5; --gold:#9a7400; --ok:#2f7d20;
  }
  html[data-theme="whiteout"] body{background:var(--bg)}
  html[data-theme="whiteout"] button{color:#fff}
  html[data-theme="catppuccin"]{
    --bg:#1e1e2e; --panel:#242438; --panel-2:#2d2d44; --line:#3b3b55;
    --ink:#cdd6f4; --dim:#9399b2; --pink:#f5c2e7; --pink-soft:#f9d3ee;
    --cyan:#94e2d5; --gold:#f9e2af; --ok:#a6e3a1;
  }
  html[data-theme="catppuccin"] button{color:#2b2033}
  html[data-theme="dracula"]{
    --bg:#282a36; --panel:#31333f; --panel-2:#3b3d4d; --line:#4a4c60;
    --ink:#f8f8f2; --dim:#9aa0b8; --pink:#bd93f9; --pink-soft:#d5b8ff;
    --cyan:#8be9fd; --gold:#f1fa8c; --ok:#50fa7b;
  }
  html[data-theme="dracula"] button{color:#241b33}
  html[data-theme="amber"]{
    --bg:#100d07; --panel:#191307; --panel-2:#241b0c; --line:#3a2c12;
    --ink:#f5ecd9; --dim:#a89778; --pink:#ffb020; --pink-soft:#ffc85c;
    --cyan:#ffd98a; --gold:#ffb020; --ok:#9bd44f;
  }
  html[data-theme="amber"] button{color:#2b1c00}
  html[data-theme="lavender"]{
    --bg:#15101f; --panel:#1e1730; --panel-2:#2a2142; --line:#3b2f5a;
    --ink:#ece6fa; --dim:#a297c4; --pink:#b794f6; --pink-soft:#d0b6ff;
    --cyan:#8fd8ff; --gold:#ffd66b; --ok:#7fe08a;
  }
  html[data-theme="lavender"] button{color:#241a38}

  /* ---- theme picker ---- */
  .themes{display:grid;grid-template-columns:repeat(auto-fill,minmax(168px,1fr));gap:10px;padding:14px 16px}
  .theme{
    border:1px solid var(--line);border-radius:10px;padding:12px;cursor:pointer;
    transition:border-color .15s ease,transform .08s ease;
  }
  .theme:hover{transform:translateY(-1px)}
  .theme.sel{border-color:var(--accent);box-shadow:0 0 0 2px rgba(255,92,141,.18)}
  .theme b{display:block;font-size:13px;margin-bottom:8px}
  .sw{display:flex;gap:5px}
  .sw i{width:22px;height:22px;border-radius:6px;display:block}

  /* ---- account ---- */
  .acct{display:flex;align-items:center;gap:14px;padding:16px}
  .acct img{width:52px;height:52px;border-radius:10px;object-fit:cover;flex:none}
  .acct .who{flex:1;min-width:0}
  .acct .who b{display:block;font-size:15px}
  .acct .who span{color:var(--dim);font-size:12.5px}
  .acct .dot{
    width:9px;height:9px;border-radius:50%;background:var(--ok);flex:none;
    box-shadow:0 0 0 3px rgba(83,218,51,.16);
  }
  .acct .dot.off{background:var(--dim);box-shadow:0 0 0 3px rgba(154,148,189,.14)}
  .steps{padding:0 16px 16px;color:var(--dim);font-size:12.5px;line-height:1.7}
  .steps ol{margin:6px 0 0;padding-left:20px}
  .steps code{
    background:var(--panel-2);border:1px solid var(--line);border-radius:5px;
    padding:2px 6px;font-size:11.5px;color:var(--cyan);
  }
  .steps a{color:var(--pink-soft)}

  /* ---- library grid ---- */
  .sect{display:flex;align-items:baseline;gap:10px;margin:22px 0 12px}
  .sect h3{margin:0;font-size:15px;font-weight:600}
  .sect span{color:var(--dim);font-size:12.5px}
  .grid{
    display:grid;grid-template-columns:repeat(auto-fill,minmax(148px,1fr));gap:14px;
  }
  .tile{cursor:pointer;transition:transform .1s ease}
  .tile:hover{transform:translateY(-3px)}
  .tile .art{
    position:relative;aspect-ratio:2/3;border-radius:10px;overflow:hidden;
    background:var(--panel-2) center/cover;border:1px solid var(--line);
  }
  .tile:hover .art{border-color:var(--accent)}
  .tile .next{
    position:absolute;left:0;right:0;bottom:0;padding:7px 9px;
    background:linear-gradient(180deg,transparent,rgba(0,0,0,.88));
    font:600 12px/1.2 "Segoe UI",sans-serif;color:#fff;
  }
  .tile .prog{position:absolute;left:0;right:0;bottom:0;height:3px;background:rgba(0,0,0,.5)}
  .tile .prog i{display:block;height:100%;background:var(--accent)}
  /* Continue watching: the resume time matters more than the artwork, so the
     card is wider than a poster tile and carries the numbers underneath. */
  .cw{display:grid;grid-template-columns:repeat(auto-fill,minmax(260px,1fr));gap:14px}
  .cwc{cursor:pointer;background:var(--card);border:1px solid var(--line);border-radius:12px;
       overflow:hidden;transition:border-color .1s ease,transform .1s ease;display:flex;flex-direction:column}
  .cwc:hover{border-color:var(--accent);transform:translateY(-2px)}
  .cwc .art{position:relative;aspect-ratio:16/9;background:#12121a center/cover no-repeat}
  .cwc .art .badge{position:absolute;left:8px;bottom:10px;font-size:12px;font-weight:600;
       background:rgba(0,0,0,.72);padding:3px 7px;border-radius:5px}
  .cwc .art .left{position:absolute;right:8px;bottom:10px;font-size:11.5px;color:#dcdce6;
       background:rgba(0,0,0,.72);padding:3px 7px;border-radius:5px}
  .cwc .bar{position:absolute;left:0;right:0;bottom:0;height:4px;background:rgba(0,0,0,.55)}
  .cwc .bar i{display:block;height:100%;background:var(--accent)}
  .cwc .meta{padding:9px 11px 11px}
  .cwc .meta .t{font-size:13.5px;font-weight:600;line-height:1.3}
  .cwc .meta .s{color:var(--dim);font-size:12px;margin-top:3px;
       display:flex;justify-content:space-between;gap:8px}
  /* Sync badge, next to the hint line. Quiet until it has something to say. */
  #sync{display:inline-flex;align-items:center;gap:6px;margin-left:10px;font-size:12px;color:var(--dim)}
  #sync i{width:7px;height:7px;border-radius:50%;background:#4ac97e;display:inline-block}
  #sync.busy i{background:#e0b341}
  #sync.bad i{background:#e05f5f}
  #sync.off i{background:#5a5a68}
  /* Resume prompt */
  .resume{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:16px 18px;margin:14px 0}
  .resume h4{margin:0 0 4px;font-size:15px}
  .resume p{margin:0 0 13px;color:var(--dim);font-size:13px}
  .resume .row{display:flex;gap:9px;flex-wrap:wrap}
  .resume button.ghost{background:transparent;border:1px solid var(--line);color:var(--fg)}
)HTMLPAGE"
    R"HTMLPAGE(
  .filters{display:flex;gap:8px;flex-wrap:wrap;margin-bottom:6px}
  .filters select,.filters input{padding:9px 11px;font-size:13px}
  .filters .yr{width:96px}
  .nav{display:flex;gap:4px;margin-left:auto;align-items:center}
  .nav button{
    background:transparent;border:1px solid transparent;color:var(--dim);
    font-weight:500;padding:9px 14px;
  }
  .nav button:hover{color:var(--ink)}
  .nav button.on{color:var(--ink);border-color:var(--line);background:var(--panel)}
  .meta{color:var(--dim);font-size:11.5px;margin-top:3px}
  .when{
    position:absolute;top:8px;left:8px;background:rgba(0,0,0,.72);
    border-radius:6px;padding:4px 7px;font:600 11px/1 "Segoe UI",sans-serif;color:#fff;
  }
  .tile .name{
    margin-top:7px;font-size:12.5px;line-height:1.35;color:var(--ink);
    display:-webkit-box;-webkit-line-clamp:2;-webkit-box-orient:vertical;overflow:hidden;
  }
  .wiz{padding:0 16px 16px}
  .wiz .step{display:flex;align-items:center;gap:12px;padding:9px 0}
  .wiz .n{
    width:22px;height:22px;border-radius:50%;background:var(--panel-2);
    border:1px solid var(--line);color:var(--dim);flex:none;
    font:600 11px/20px "Segoe UI",sans-serif;text-align:center;
  }
  .wiz .txt{flex:1;min-width:0;color:var(--dim);font-size:13px}
  .wiz input{width:100%;font-size:13px}
  .wiz .copyrow{display:flex;gap:8px;align-items:center;flex:1;min-width:0}
  .wiz .copyrow code{
    flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;
    background:var(--panel-2);border:1px solid var(--line);border-radius:6px;
    padding:8px 10px;font-size:12px;color:var(--cyan);
  }
  .wiz .small{
    background:var(--panel-2);border:1px solid var(--line);color:var(--ink);
    font:600 12px/1 "Segoe UI",sans-serif;padding:9px 12px;border-radius:7px;flex:none;
  }
  .wiz .small:hover{border-color:var(--accent);color:var(--accent)}
  .alert{
    margin:0 16px 14px;padding:10px 12px;border-radius:8px;font-size:12.5px;
    background:rgba(255,214,107,.09);border:1px solid rgba(255,214,107,.32);color:var(--gold);
  }

  /* ---- spoilers ---- */
  html[data-spoilers="hide"] .hero p,
  html[data-spoilers="hide"] .ep .name{
    filter:blur(5px);transition:filter .18s ease;
  }
  html[data-spoilers="hide"] .hero:hover p,
  html[data-spoilers="hide"] .ep:hover .name{filter:none}

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
<header id="home" title="Back to the start">
  <svg class="logo" viewBox="0 0 128 128"><defs>
    <linearGradient id="lg" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#453A7A"/><stop offset="1" stop-color="#1B1B2F"/></linearGradient>
    <linearGradient id="la" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#FF8FB3"/><stop offset="1" stop-color="#FF5C8D"/></linearGradient>
  </defs>
    <rect width="128" height="128" rx="30" fill="url(#lg)"/>
    <path d="M28 38 L28 90 L68 64 Z" fill="url(#la)"/>
    <circle cx="82" cy="64" r="5.5" fill="#5BE9E9"/><circle cx="96" cy="64" r="5.5" fill="#5BE9E9" opacity=".72"/><circle cx="110" cy="64" r="5.5" fill="#5BE9E9" opacity=".45"/>
  </svg>
  <h1>Tsuzuki <span>&#32154;&#12365; &middot; to be continued</span></h1>
  <div class="nav">
    <button id="navHome">Home</button>
    <button id="navBrowse">Browse</button>
    <button id="navSchedule">Schedule</button>
    <button class="gear" id="gear">Settings</button>
  </div>
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
  <div class="hint">Downloads are deleted after you finish watching. Playback opens in mpv.<span id="sync"><i></i><span id="synctext">Synced</span></span></div>
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
let prefs = {};

// Pull preferences once so the theme is applied before anything renders.
(async () => {
  try{
    prefs = await (await fetch('/api/settings')).json();
    applyTheme(prefs.theme);
    applyPrefs(prefs);
    if(prefs.quality && $('#res')) $('#res').value = prefs.quality;
  }catch(e){}
})();

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
  if(j.autoSelect && j.results.length){
    // Straight into the top result. The episode table still appears, so this
    // skips a click without skipping the part where you see what you got.
    return openTorrent(j.results[0].magnet);
  }
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

// Set when a Continue Watching card was clicked: the episode to open and the
// second to open it at. Cleared as soon as it is used, so a later manual pick
// of the same episode still gets the usual prompt.
let pendingResume = null;

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
        '<button data-i="'+f.index+'" data-ep="'+(parseInt(num,10)||0)+'">Play</button></div></div>';
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
  $('#back').onclick = () => { if(lastResults) renderResults(); else goHome(); };
  document.querySelectorAll('.ep button').forEach(b => b.onclick = () =>
    play(magnet, parseInt(b.dataset.i,10), parseInt(b.dataset.ep,10) || 0));

  // Arrived here from Continue Watching: go straight in at the saved second.
  if(pendingResume){
    const want = pendingResume;
    pendingResume = null;
    const btn = Array.from(document.querySelectorAll('.ep button'))
      .find(b => (parseInt(b.dataset.ep,10)||0) === want.episode);
    if(btn) play(magnet, parseInt(btn.dataset.i,10), want.episode, want.at);
  }
}

async function play(magnet, index, episode, resumeFrom){
  // No explicit instruction yet: if there is somewhere worth going back to,
  // ask rather than deciding for them. Starting over by accident is far more
  // annoying than one extra click.
  if(resumeFrom === undefined && lastAnilistId && episode){
    let r = null;
    try{
      r = await (await fetch('/api/resume?anilistId='+lastAnilistId+'&episode='+episode)).json();
    }catch(e){}
    if(r && r.resumable){
      return askResume(r, () => play(magnet, index, episode, Math.floor(r.seconds)),
                          () => play(magnet, index, episode, 0));
    }
  }

  $('#status').style.display = '';
  $('#stext').textContent = 'Buffering...';
  $('#status').scrollIntoView({behavior:'smooth', block:'nearest'});
  const body = {magnet, index};
  if(resumeFrom !== undefined) body.resumeFrom = resumeFrom;
  await fetch('/api/play', {method:'POST',headers:{'Content-Type':'application/json'},
    body: JSON.stringify(body)});
  poll();
}

function askResume(r, onResume, onRestart){
  const box = document.createElement('div');
  box.className = 'resume';
  box.innerHTML = '<h4>Resume from '+fmt(r.seconds)+'?</h4>'+
    '<p>You stopped '+Math.round(r.percent||0)+'% of the way through episode '+r.episode+
    ', with '+fmt(r.remaining||0)+' left.</p>'+
    '<div class="row"><button id="rgo">Resume from '+fmt(r.seconds)+'</button>'+
    '<button class="ghost" id="rstart">Start from the beginning</button></div>';
  out.prepend(box);
  box.scrollIntoView({behavior:'smooth', block:'nearest'});
  box.querySelector('#rgo').onclick    = () => { box.remove(); onResume(); };
  box.querySelector('#rstart').onclick = () => { box.remove(); onRestart(); };
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
)HTMLPAGE"
    R"HTMLPAGE(
/* ------------------------------------------------------------- browse */

const SEASONS = [['','Any season'],['WINTER','Winter'],['SPRING','Spring'],['SUMMER','Summer'],['FALL','Fall']];
const FORMATS = [['','Any format'],['TV','TV'],['MOVIE','Movie'],['OVA','OVA'],['ONA','ONA'],['SPECIAL','Special']];
const STATUSES = [['','Any status'],['RELEASING','Airing'],['FINISHED','Finished'],['NOT_YET_RELEASED','Upcoming']];
const SORTS = [['POPULARITY_DESC','Most popular'],['SCORE_DESC','Highest rated'],['TRENDING_DESC','Trending'],['START_DATE_DESC','Newest']];

let browseState = {genre:'', season:'', year:'', format:'', status:'', sort:'POPULARITY_DESC'};

function selectEl(id, options, current){
  return '<select id="'+id+'">' + options.map(([v,l]) =>
    '<option value="'+esc(v)+'"'+(current===v?' selected':'')+'>'+esc(l)+'</option>').join('') + '</select>';
}

function tileFor(b){
  const art = b.cover ? 'background-image:url('+esc(b.cover)+')' : '';
  const bits = [];
  if(b.format) bits.push(b.format);
  if(b.year) bits.push(b.year);
  if(b.episodes) bits.push(b.episodes+' eps');
  if(b.score) bits.push(b.score+'%');
  return '<div class="tile" data-title="'+esc(b.title)+'" data-ep="1"'+
      (b.color ? ' data-color="'+esc(b.color)+'"' : '')+'>'+
    '<div class="art" style="'+art+'"></div>'+
    '<div class="name">'+esc(b.title)+'</div>'+
    '<div class="meta">'+esc(bits.join(' · '))+'</div></div>';
}

async function showBrowse(){
  setNav('navBrowse');
  let genres = [];
  try{ genres = await (await fetch('/api/genres')).json(); }catch(e){}

  const genreOpts = [['','Any genre']].concat((genres||[]).map(g => [g,g]));
  const years = [['','Any year']];
  const thisYear = new Date().getFullYear();
  for(let y = thisYear + 1; y >= 1990; y--) years.push([String(y), String(y)]);

  out.innerHTML =
    '<div class="filters">'+
      selectEl('fGenre', genreOpts, browseState.genre)+
      selectEl('fSeason', SEASONS, browseState.season)+
      selectEl('fYear', years, browseState.year)+
      selectEl('fFormat', FORMATS, browseState.format)+
      selectEl('fStatus', STATUSES, browseState.status)+
      selectEl('fSort', SORTS, browseState.sort)+
    '</div><div id="browseOut"></div>';

  ['fGenre','fSeason','fYear','fFormat','fStatus','fSort'].forEach(id => {
    $('#'+id).onchange = () => {
      browseState = {
        genre: $('#fGenre').value, season: $('#fSeason').value, year: $('#fYear').value,
        format: $('#fFormat').value, status: $('#fStatus').value, sort: $('#fSort').value,
      };
      runBrowse();
    };
  });
  runBrowse();
}

async function runBrowse(){
  const box = $('#browseOut');
  if(!box) return;
  box.innerHTML = '<div class="empty"><span class="spinner"></span>Looking...</div>';

  const qs = new URLSearchParams();
  for(const [k,v] of Object.entries(browseState)) if(v) qs.set(k === 'genre' ? 'genre' : k, v);

  let items = [];
  try{ items = await (await fetch('/api/browse?'+qs.toString())).json(); }catch(e){}
  if(!Array.isArray(items) || !items.length){
    box.innerHTML = '<div class="empty"><p>Nothing matches those filters.</p></div>';
    return;
  }
  box.innerHTML = '<div class="grid">' + items.map(tileFor).join('') + '</div>';
  wireTiles();
}

/* ----------------------------------------------------------- schedule */

)HTMLPAGE"
    R"HTMLPAGE(async function showSchedule(){
  setNav('navSchedule');
  out.innerHTML = '<div class="empty"><span class="spinner"></span>Loading the schedule...</div>';

  let items = [];
  try{ items = await (await fetch('/api/airing?days=7')).json(); }catch(e){}
  if(!Array.isArray(items) || !items.length){
    out.innerHTML = '<div class="empty"><p>Nothing scheduled in the next week.</p></div>';
    return;
  }

  // Group by day so the week reads as a week.
  const days = {};
  for(const a of items){
    const d = new Date(a.airingAt * 1000);
    const key = d.toDateString();
    (days[key] = days[key] || []).push(a);
  }

  let html = '';
  for(const [day, list] of Object.entries(days)){
    const when = new Date(list[0].airingAt * 1000);
    const label = when.toLocaleDateString(undefined, {weekday:'long', month:'short', day:'numeric'});
    html += '<div class="sect"><h3>'+esc(label)+'</h3><span>'+list.length+' episodes</span></div><div class="grid">';
    for(const a of list){
      const art = a.cover ? 'background-image:url('+esc(a.cover)+')' : '';
      const t = new Date(a.airingAt * 1000).toLocaleTimeString(undefined, {hour:'numeric', minute:'2-digit'});
      html += '<div class="tile" data-title="'+esc(a.title)+'" data-ep="'+a.episode+'"'+
          (a.color ? ' data-color="'+esc(a.color)+'"' : '')+'>'+
        '<div class="art" style="'+art+'">'+
          '<div class="when">'+esc(t)+'</div>'+
          '<div class="next">Episode '+a.episode+'</div>'+
        '</div><div class="name">'+esc(a.title)+'</div></div>';
    }
    html += '</div>';
  }
  out.innerHTML = html;
  wireTiles();
}

function wireTiles(){
  document.querySelectorAll('.tile').forEach(t => {
    t.onclick = () => {
      if(t.dataset.color) document.documentElement.style.setProperty('--accent', t.dataset.color);
      $('#q').value = t.dataset.title;
      $('#ep').value = t.dataset.ep || '';
      search();
    };
  });
}

function setNav(id){
  ['navHome','navBrowse','navSchedule'].forEach(n => {
    const el = $('#'+n);
    if(el) el.classList.toggle('on', n === id);
  });
}

/* ------------------------------------------------------- home + history */

function goHome(){
  document.documentElement.style.removeProperty('--accent');
  lastResults = null;
  showHome();
}

async function showHome(){
  let lists = [], hist = [], cont = [];
  // All three come from the local database, so this paints without waiting on
  // AniList; the sync worker refreshes underneath and the next visit shows it.
  try{ lists = await (await fetch('/api/lists')).json(); }catch(e){}
  try{ hist  = await (await fetch('/api/history')).json(); }catch(e){}
  try{ cont  = await (await fetch('/api/continue')).json(); }catch(e){}
  if(!Array.isArray(lists)) lists = [];
  if(!Array.isArray(cont)) cont = [];

  const watching = lists.filter(e => e.status === 'CURRENT' || e.status === 'REPEATING');
  const planning = lists.filter(e => e.status === 'PLANNING');
  const paused   = lists.filter(e => e.status === 'PAUSED');

  if(!lists.length && !cont.length && (!Array.isArray(hist) || !hist.length)){
    mascot('Search for something to watch.');
    return;
  }

  let html = '';

  const section = (title, note, entries) => {
    if(!entries.length) return '';
    let h = '<div class="sect"><h3>'+esc(title)+'</h3><span>'+esc(note)+'</span></div><div class="grid">';
    for(const e of entries){
      const pct = e.episodes ? Math.round(e.progress / e.episodes * 100) : 0;
      const art = e.cover ? 'background-image:url('+esc(e.cover)+')' : '';
      h += '<div class="tile" data-title="'+esc(e.title)+'" data-ep="'+e.nextEpisode+'"'+
             (e.color ? ' data-color="'+esc(e.color)+'"' : '')+'>'+
        '<div class="art" style="'+art+'">'+
          '<div class="next">Episode '+e.nextEpisode+
            (e.episodes ? ' <span style="opacity:.7">of '+e.episodes+'</span>' : '')+'</div>'+
          (pct ? '<div class="prog"><i style="width:'+pct+'%"></i></div>' : '')+
        '</div>'+
        '<div class="name">'+esc(e.title)+'</div></div>';
    }
    return h + '</div>';
  };

  // Part-watched episodes come first: picking up where you actually stopped
  // is the reason most people open the app at all.
  if(cont.length){
    html += '<div class="sect"><h3>Continue watching</h3><span>pick up where you left off</span></div><div class="cw">';
    for(const c of cont){
      const art = c.cover ? 'background-image:url('+esc(c.cover)+')' : '';
      const pct = Math.round(c.percent||0);
      html += '<div class="cwc" data-magnet="'+esc(c.magnet||'')+'" data-al="'+(c.anilistId||0)+
                '" data-ep="'+(c.episode||0)+'" data-at="'+Math.floor(c.currentTime||0)+'">'+
        '<div class="art" style="'+art+'">'+
          '<div class="badge">Episode '+(c.episode||'?')+'</div>'+
          '<div class="left">'+fmt(c.remaining||0)+' left</div>'+
          '<div class="bar"><i style="width:'+pct+'%"></i></div>'+
        '</div>'+
        '<div class="meta"><div class="t">'+esc(c.title||'Unknown')+'</div>'+
          '<div class="s"><span>'+fmt(c.currentTime||0)+' / '+fmt(c.duration||0)+'</span>'+
            '<span>'+pct+'% watched</span></div>'+
        '</div></div>';
    }
    html += '</div>';
  }

  html += section('From your AniList', 'currently watching', watching);
  html += section('On hold', 'paused', paused);
  html += section('Planning to watch', 'not started', planning);

  if(Array.isArray(hist) && hist.length){
    html += '<div class="sect"><h3>Recently opened</h3><span>on this machine</span></div><div class="eps">';
  for(const h of hist.slice(0, 12)){
    const ep = h.episode ? 'Episode ' + h.episode : (h.file || '');
    const name = h.show || h.torrent || 'Unknown';
    const thumb = h.cover ? 'background-image:url('+esc(h.cover)+');background-position:center 22%' : '';
    html += '<div class="ep hist" data-magnet="'+esc(h.magnet)+'" data-al="'+(h.anilistId||0)+'" style="cursor:pointer">'+
      '<div class="thumb" style="'+thumb+'"><span>'+esc(h.episode ? 'EP '+h.episode : '?')+'</span></div>'+
      '<div class="body"><div class="name">'+esc(name)+'</div>'+
      '<div class="file">'+esc(ep)+'</div>'+
      '<div class="file">'+esc(h.torrent||'')+'</div></div>'+
      '<div class="right"><button>Open</button></div></div>';
  }
    html += '</div>';
  }

  out.innerHTML = html;
  setNav('navHome');

  // A tile is "find this episode and play it" - the title and the next
  // unwatched number both come from the list, so nothing has to be typed.
  wireTiles();

  document.querySelectorAll('.ep.hist').forEach(c => {
    c.onclick = () => {
      lastAnilistId = parseInt(c.dataset.al, 10) || null;
      openTorrent(c.dataset.magnet);
    };
  });

  // A Continue Watching card opens its episode and resumes, with no stop at
  // the file list and no prompt - the click already said what to do.
  document.querySelectorAll('.cwc').forEach(c => {
    c.onclick = () => {
      const magnet = c.dataset.magnet;
      if(!magnet) return mascot('That episode has no torrent saved - search for it again.');
      lastAnilistId = parseInt(c.dataset.al, 10) || null;
      pendingResume = { episode: parseInt(c.dataset.ep,10)||0, at: parseInt(c.dataset.at,10)||0 };
      $('#ep').value = pendingResume.episode || '';
      openTorrent(magnet);
    };
  });
}

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

const LANG_NAMES = {
  jpn:'Japanese', ja:'Japanese', jp:'Japanese',
  eng:'English', en:'English',
  spa:'Spanish', es:'Spanish', por:'Portuguese', pt:'Portuguese',
  fre:'French', fra:'French', fr:'French',
  ger:'German', deu:'German', de:'German',
  ita:'Italian', it:'Italian', rus:'Russian', ru:'Russian',
  chi:'Chinese', zho:'Chinese', zh:'Chinese',
  kor:'Korean', ko:'Korean', ara:'Arabic', ar:'Arabic',
};
const langName = c => !c ? '' : (LANG_NAMES[String(c).toLowerCase()] || String(c).toUpperCase());

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
  // Two tracks both labelled "JA" tells you nothing, so fall back to the
  // track title, then the codec, then the id until the labels differ.
  const label = t => {
    const bits = [];
    if(t.lang) bits.push(langName(t.lang));
    if(t.title) bits.push(t.title);
    return bits.join(' - ') || (t.codec || ('Track ' + t.id));
  };
  const labels = want.map(label);
  const finalLabels = labels.map((l, i) => {
    if(labels.filter(x => x === l).length === 1) return l;
    const t = want[i];
    return l + (t.codec ? ' (' + t.codec + ')' : ' #' + t.id);
  });
  const seen = {};
  want.forEach((t, i) => {
    const o = document.createElement('option');
    o.value = String(t.id);
    let text = finalLabels[i];
    if(seen[text]) text += ' #' + t.id;
    seen[text] = true;
    o.textContent = text;
    sel.appendChild(o);
  });
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
// mascot() is the empty state; goHome() shows history when there is any.
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
  if(s.videoActive){ enterPlayer(); refreshPlayer(); }
  else { exitPlayer(); }
}, 900);

/* ------------------------------------------------------------ settings */

const SETTING_ROWS = [
  ['savePath','text','Download folder','Where episodes are stored while you watch them.'],
  ['speedLimit','number','Speed limit (Mb/s)','0 means unlimited.'],
  ['maxConnections','number','Max connections','Peers per torrent. Higher is faster but noisier.'],
  ['quality','select','Preferred quality','Used as the default in search.','=Any|1080=1080p|720=720p|480=480p'],
  ['audioLang','select','Preferred audio language','Picked automatically when the release includes it.','=Auto (whatever the release defaults to)|jpn=Japanese|eng=English|spa=Spanish|por=Portuguese|fre=French|ger=German|ita=Italian|rus=Russian|chi=Chinese|kor=Korean|ara=Arabic'],
  ['subLang','select','Preferred subtitle language','Picked automatically when the release includes it.','=Auto (whatever the release defaults to)|jpn=Japanese|eng=English|spa=Spanish|por=Portuguese|fre=French|ger=German|ita=Italian|rus=Russian|chi=Chinese|kor=Korean|ara=Arabic'],
  ['bufferSeconds','number','Extra buffer (seconds)','0 lets Tsuzuki size the buffer from measured speed.'],
  ['deleteAfter','toggle','Delete after watching','Remove each episode once you finish it.'],
  ['subsOn','toggle','Subtitles on by default','Applies when a subtitle track exists.'],
  ['titleLanguage','select','Title language','How show titles are displayed.','romaji=Romaji|english=English|native=Native'],
  ['lookupPreference','select','Lookup preference','What to rank results by. Curated picks stay first either way.','quality=Quality|size=Smallest size|availability=Most seeders'],
  ['autoSelect','toggle','Auto-select torrents','Open the top result straight away instead of showing the list.'],
  ['streamedDownload','toggle','Streamed download','Only fetch what playback needs. Gentler on the swarm, stalls more easily.'],
  ['torrentPort','number','Forwarded torrent port','0 picks one automatically. Set this if you forwarded a port manually.'],
  ['dhtPort','number','DHT port','0 is automatic.'],
  ['disableDHT','toggle','Disable DHT and LSD','For private trackers. Greatly reduces peer discovery.'],
  ['disablePeX','toggle','Disable peer exchange','For private trackers. Greatly reduces peer discovery.'],
  ['syncProgress','toggle','Sync progress to AniList','Marks an episode watched once you pass 80% of it.'],
  ['uiScale','number','Interface scale','1.0 is normal. Try 1.2 on a high-DPI screen.'],
  ['hideSpoilers','toggle','Hide spoilers','Blurs synopses and episode titles until you hover them.'],
  ['showAdult','toggle','Show adult results','Includes titles AniList flags as adult in search.'],
  ['dohUrl','select','DNS over HTTPS','Routes lookups through a resolver, useful if your ISP blocks trackers.','=Off (system DNS)|https://cloudflare-dns.com/dns-query=Cloudflare|https://dns.google/dns-query=Google|https://dns.quad9.net/dns-query=Quad9'],
  ['discordPresence','toggle','Discord rich presence','Shows what you are watching on your Discord profile.'],
  ['discordClientId','text','Discord application id','From discord.com/developers. Only needed for rich presence.'],
];

const THEMES = [
  ['tsuzuki','Tsuzuki','#0e0d17','#ff5c8d','#5be9e9'],
  ['blackout','Blackout','#000000','#ffffff','#bdbdbd'],
  ['whiteout','Whiteout','#f4f4f7','#2b2b31','#3a6ea5'],
  ['catppuccin','Catppuccin','#1e1e2e','#f5c2e7','#94e2d5'],
  ['dracula','Dracula','#282a36','#bd93f9','#8be9fd'],
  ['amber','Amber','#100d07','#ffb020','#ffd98a'],
  ['lavender','Lavender','#15101f','#b794f6','#8fd8ff'],
];

function applyPrefs(cfg){
  const scale = parseFloat(cfg.uiScale);
  document.documentElement.style.zoom = (isFinite(scale) && scale > 0) ? scale : 1;
  if(cfg.hideSpoilers) document.documentElement.dataset.spoilers = 'hide';
  else delete document.documentElement.dataset.spoilers;
}

function applyTheme(name){
  if(name && name !== 'tsuzuki') document.documentElement.dataset.theme = name;
  else delete document.documentElement.dataset.theme;
}

)HTMLPAGE"
    R"HTMLPAGE(async function showSettings(){
  let cfg = {};
  try{ cfg = await (await fetch('/api/settings')).json(); }catch(e){}
  let acct = {};
  try{ acct = await (await fetch('/api/account')).json(); }catch(e){}

  let html = '<button class="back" id="sBack">&larr; Back</button>';

  html += '<div class="panel"><h3>Account</h3><div class="acct">';
  if(acct.linked){
    html += (acct.avatar ? '<img src="'+esc(acct.avatar)+'" alt="">' : '')+
      '<div class="who"><b>'+esc(acct.name)+'</b><span>AniList &middot; linked</span></div>'+
      '<span class="dot"></span>'+
      '<button class="pb" id="aOut" style="background:var(--panel-2);border:1px solid var(--line);color:var(--ink)">Unlink</button>';
  } else {
    html += '<div class="who"><b>Not linked</b><span>AniList</span></div>'+
      '<span class="dot off"></span>'+
      '<button id="aIn">Link AniList</button>';
  }
  html += '</div>';

  html += '<div id="aErr"></div>';

  if(!acct.linked){
    html += '<div class="wiz"><div class="txt">Press <b>Link AniList</b>. A sign-in window '+
      'opens; approve Tsuzuki and it links itself.</div></div>';
  }
  html += '</div>';

  html += '<div class="panel"><h3>Appearance</h3><div class="themes">';
  for(const [id,label,bg,accent,second] of THEMES){
    const sel = (cfg.theme||'tsuzuki') === id ? ' sel' : '';
    html += '<div class="theme'+sel+'" data-theme-id="'+id+'" style="background:'+bg+'">'+
      '<b style="color:'+(id==='whiteout'?'#16161c':'#fff')+'">'+esc(label)+'</b>'+
      '<div class="sw"><i style="background:'+accent+'"></i><i style="background:'+second+'"></i>'+
      '<i style="background:'+bg+';border:1px solid rgba(128,128,128,.4)"></i></div></div>';
  }
  html += '</div></div><div class="panel"><h3>Settings</h3>';
  for(const [key,type,label,desc,opts] of SETTING_ROWS){
    let control;
    if(type === 'toggle'){
      control = '<div class="toggle'+(cfg[key]?' on':'')+'" data-k="'+key+'"></div>';
    } else if(type === 'select'){
      const choices = opts.split('|').map(o => {
        const eq = o.indexOf('=');
        return eq < 0 ? [o, o] : [o.slice(0, eq), o.slice(eq + 1)];
      });
      const cur = String(cfg[key] == null ? '' : cfg[key]);
      // A value saved before this became a dropdown would otherwise silently
      // reset to the first option, so keep it as a choice.
      if(cur && !choices.some(c => c[0] === cur)) choices.push([cur, cur]);
      control = '<select data-k="'+key+'">' + choices.map(([v,label]) =>
        '<option value="'+esc(v)+'"'+(cur===v?' selected':'')+'>'+esc(label)+'</option>').join('') + '</select>';
    } else {
      control = '<input type="'+type+'" data-k="'+key+'" value="'+esc(cfg[key]==null?'':cfg[key])+'">';
    }
    html += '<div class="setrow"><div class="info"><b>'+esc(label)+'</b><span>'+esc(desc)+'</span></div>'+control+'</div>';
  }
  html += '</div><div class="hint" id="sSaved" style="margin-top:12px"></div>';
  out.innerHTML = html;

  $('#sBack').onclick = goHome;
  const save = async () => {
    const next = {};
    document.querySelectorAll('[data-k]').forEach(el => {
      const k = el.dataset.k;
      if(el.classList.contains('toggle')) next[k] = el.classList.contains('on');
      else if(el.type === 'number') next[k] = parseFloat(el.value) || 0;
      else next[k] = el.value;
    });
    const saved = await (await fetch('/api/settings', {method:'POST',
      headers:{'Content-Type':'application/json'}, body: JSON.stringify(next)})).json();
    prefs = saved;
    applyPrefs(saved);
    $('#sSaved').textContent = 'Saved.';
    setTimeout(() => { if($('#sSaved')) $('#sSaved').textContent = ''; }, 1600);
  };
  const showErr = m => {
    const box = $('#aErr');
    if(box) box.innerHTML = m ? '<div class="alert">'+esc(m)+'</div>' : '';
  };

  async function startLink(){
    let r = {};
    try{ r = await (await fetch('/api/account/login',{method:'POST'})).json(); }catch(e){}
    if(!r.ok){ showErr(r.error || 'Could not start linking.'); return; }
    showErr(r.inApp ? 'Sign in to AniList in the window that just opened.'
                    : 'Approve Tsuzuki in the browser tab that opened.');
    let tries = 0;
    const timer = setInterval(async () => {
      let a = {};
      try{ a = await (await fetch('/api/account')).json(); }catch(e){}
      if(a.linked){ clearInterval(timer); showSettings(); return; }
      if(++tries > 90){ clearInterval(timer); showErr('Gave up waiting. Press Link AniList to try again.'); }
    }, 2000);
  }

  if($('#aIn')) $('#aIn').onclick = async () => {
    startLink();
  };

  if($('#aOut')) $('#aOut').onclick = async () => {
    await fetch('/api/account/logout',{method:'POST'});
    showSettings();
  };

  document.querySelectorAll('.theme[data-theme-id]').forEach(el =>
    el.onclick = async () => {
      const id = el.dataset.themeId;
      applyTheme(id);
      document.querySelectorAll('.theme').forEach(x => x.classList.remove('sel'));
      el.classList.add('sel');
      await fetch('/api/settings', {method:'POST',headers:{'Content-Type':'application/json'},
        body: JSON.stringify({theme: id})});
    });

  document.querySelectorAll('.toggle[data-k]').forEach(t =>
    t.onclick = () => { t.classList.toggle('on'); save(); });
  document.querySelectorAll('input[data-k],select[data-k]').forEach(el =>
    el.onchange = save);
}

$('#gear').onclick = (e) => { e.stopPropagation(); showSettings(); };
$('#home').onclick = goHome;
$('#navHome').onclick = goHome;
$('#navBrowse').onclick = showBrowse;
$('#navSchedule').onclick = showSchedule;

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

/* --------------------------------------------------------- sync status */

// The badge is the only place the queue is visible, so it has to be honest:
// green only when there is genuinely nothing outstanding.
let lastSyncLabel = '';
async function pollSync(){
  let j;
  try{ j = await (await fetch('/api/sync')).json(); }catch(e){
    const b = $('#sync'); if(b){ b.className = 'off'; $('#synctext').textContent = 'Offline'; }
    return;
  }
  const b = $('#sync');
  if(!b) return;
  b.className = !j.linked ? 'off'
              : j.label === 'Synced' ? ''
              : /failed|Offline/i.test(j.label) ? 'bad' : 'busy';
  $('#synctext').textContent = j.label || '';
  b.title = j.lastError ? j.lastError
          : (j.lastSyncAt ? 'Last synced ' + new Date(j.lastSyncAt).toLocaleTimeString() : '');

  // The moment a queued write lands, the home screen may be out of date.
  if(lastSyncLabel && lastSyncLabel !== 'Synced' && j.label === 'Synced' && !inPlayer){
    if(document.querySelector('.cw') || document.querySelector('.grid')) showHome();
  }
  lastSyncLabel = j.label;
}
pollSync();
setInterval(pollSync, 4000);

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
  goHome();
}
</script>
</body>
</html>
)HTMLPAGE";

}  // namespace tsuzuki::ui

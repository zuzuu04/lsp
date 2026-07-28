// ================= KONFIG (SATU CHANNEL) =================
const CH = {
  id: "3403067",
  readKey: "91HT99SJN6YRO2MQ",   // Read API Key channel
  writeKey: "ALZ1HERJNTLNREOD"   // Write API Key channel
};

// ================= CHART FACTORY =================
function makeChart(ctx, label, color){
  return new Chart(ctx, {
    type:'line',
    data:{ labels:[], datasets:[{ label, data:[], borderColor:color, backgroundColor:color+'22', fill:true, tension:0.4, pointRadius:2 }] },
    options:{
      responsive:true,
      animation:{ duration:400 },
      plugins:{ legend:{ display:false } },
      scales:{
        x:{ ticks:{ color:'#6b6558', maxTicksLimit:6 }, grid:{ display:false } },
        y:{ ticks:{ color:'#6b6558' }, grid:{ color:'#eee6d3' } }
      }
    }
  });
}

const tempChart = makeChart(document.getElementById("tempChart"), 'Suhu (°C)', '#ef4444');
const humChart  = makeChart(document.getElementById("humChart"),  'Kelembapan (%)', '#2f6fd8');
const soilChart = makeChart(document.getElementById("soilChart"), 'Tanah (%)', '#b8843c');

// ================= STATUS UI =================
function setStatus(online){
  const led = document.getElementById("statusLed");
  const text = document.getElementById("statusText");
  led.className = "led " + (online ? "online" : "offline");
  text.textContent = online ? "Terhubung ke ThingSpeak" : "Gagal memuat data";
}

// ================= LOAD DATA =================
async function loadData(){
  try{
    const r = await fetch(`https://api.thingspeak.com/channels/${CH.id}/feeds.json?api_key=${CH.readKey}&results=20`);
    const d = await r.json();

    tempChart.data.labels = []; tempChart.data.datasets[0].data = [];
    humChart.data.labels  = []; humChart.data.datasets[0].data  = [];
    soilChart.data.labels = []; soilChart.data.datasets[0].data = [];

    let lastT=null, lastH=null, lastS=null;

    d.feeds.forEach(f=>{
      const time = new Date(f.created_at).toLocaleTimeString('id-ID',{hour:'2-digit',minute:'2-digit'});

      const temp = parseFloat(f.field1);
      if(!isNaN(temp)){ tempChart.data.labels.push(time); tempChart.data.datasets[0].data.push(temp); lastT = temp; }

      const hum = parseFloat(f.field2);
      if(!isNaN(hum)){ humChart.data.labels.push(time); humChart.data.datasets[0].data.push(hum); lastH = hum; }

      const soil = parseFloat(f.field3);
      if(!isNaN(soil)){ soilChart.data.labels.push(time); soilChart.data.datasets[0].data.push(soil); lastS = soil; }
    });

    tempChart.update(); humChart.update(); soilChart.update();

    if(lastT!==null) document.getElementById("lcdSuhu").innerHTML = lastT.toFixed(2)+"<small>°C</small>";
    if(lastH!==null) document.getElementById("lcdHum").innerHTML  = lastH.toFixed(2)+"<small>%</small>";
    if(lastS!==null) document.getElementById("lcdSoil").innerHTML = lastS.toFixed(0)+"<small>%</small>";

    setStatus(true);
  }catch(err){
    console.log("ERROR:", err);
    setStatus(false);
  }
}

// ================= RELAY =================
function toggleRelay(num){
  const btn = document.getElementById("relay"+num);
  btn.classList.toggle("active");
  const status = btn.classList.contains("active") ? 1 : 0;
  const fieldNum = num + 3; // field4 = relay1, field5 = relay2

  fetch(`https://api.thingspeak.com/update?api_key=${CH.writeKey}&field${fieldNum}=${status}`)
    .catch(err => console.log("Relay update error:", err));
}

// ================= AUTO UPDATE =================
loadData();
setInterval(loadData, 15000);

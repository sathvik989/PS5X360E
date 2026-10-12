// SPDX-License-Identifier: MIT
// The page the TV's Kinect QR code opens (plain HTTP, on the settings server, with the key).
// It checks whether this phone already trusts the console's camera certificate: a probe of
// the HTTPS server succeeds only then. If it does, the phone goes straight to the camera page
// (key in the address, so the page connects by itself); if not, it shows the one-time
// certificate install, then checks again.
#pragma once

namespace xbox360ps5::phone_camera {
inline constexpr char kConsoleLanding[] = R"CAMERA(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>PS5X360E Kinect</title>
<style>body{background:#0a0c11;color:#eef3f6;font:17px/1.5 system-ui,sans-serif;max-width:640px;margin:0 auto;padding:18px}
h1{font-size:24px}h2{color:#35d07f;font-size:17px;margin-top:22px}li{margin:6px 0}small{color:#7f8a96}
a.button,button{display:block;width:100%;text-align:center;background:#35d07f;color:#06210f;font:600 17px system-ui,sans-serif;padding:14px;border:0;border-radius:12px;text-decoration:none;margin:14px 0}
.secondary{background:#232a35!important;color:#eef3f6!important}#steps{display:none}</style></head>
<body><h1>PS5X360E Kinect</h1><p id="status"></p>
<div id="steps"><p data-t="why"></p>
<a class="button" href="/phone-camera-ca.crt" data-t="download"></a>
<h2>Android</h2><ol><li data-t="a1"></li><li data-t="a2"></li><li data-t="a3"></li></ol>
<h2>iPhone / iPad</h2><ol><li data-t="i1"></li><li data-t="i2"></li><li data-t="i3"></li></ol>
<button id="again" data-t="again"></button>
<a class="button secondary" id="anyway" data-t="anyway"></a><small data-t="anyway_note"></small></div>
<script>
const boot=__CAMERA_BOOT__;
const W={en:{checking:'Opening the camera...',why:'First time on this phone: install the PS5 camera certificate once. Phone browsers only open the camera on a secure page; this certificate was made by your PS5 for its own address and cannot sign anything else.',
download:'1. Download the certificate',a1:'Tap Download. If Android says it cannot install it from here, that is normal.',a2:'Open Settings > Security (or Security and privacy) > More security settings > Encryption and credentials > Install a certificate > CA certificate.',a3:'Confirm, choose the downloaded file and enter the phone PIN.',
i1:'Tap Download, then Allow.',i2:'Settings > General > VPN and Device Management > the PS5X360E profile > Install.',i3:'Settings > General > About > Certificate Trust Settings > turn on "PS5X360E Kinect camera".',
again:'2. Installed: open the camera',anyway:'Open without installing',anyway_note:'The browser will warn that the connection is not private; choosing to continue also works, but the warning returns.'},
pt:{checking:'Abrindo a câmera...',why:'Primeira vez neste celular: instale uma vez o certificado da câmera do PS5. Os navegadores do celular só abrem a câmera numa página segura; este certificado foi feito pelo seu PS5 para o endereço dele e não consegue assinar mais nada.',
download:'1. Baixar o certificado',a1:'Toque em Baixar. Se o Android disser que não pode instalar por aqui, é normal.',a2:'Abra Configurações > Segurança (ou Segurança e privacidade) > Mais configurações de segurança > Criptografia e credenciais > Instalar um certificado > Certificado de CA.',a3:'Confirme, escolha o arquivo baixado e digite o PIN do celular.',
i1:'Toque em Baixar e depois em Permitir.',i2:'Ajustes > Geral > VPN e Gerenciamento de Dispositivos > perfil PS5X360E > Instalar.',i3:'Ajustes > Geral > Sobre > Ajustes de Confiança de Certificados > ligue "PS5X360E Kinect camera".',
again:'2. Já instalei: abrir a câmera',anyway:'Abrir sem instalar',anyway_note:'O navegador vai avisar que a conexão não é particular; escolher continuar também funciona, mas o aviso volta.'},
es:{checking:'Abriendo la cámara...',why:'Primera vez en este móvil: instala una vez el certificado de la cámara de la PS5. Los navegadores del móvil solo abren la cámara en una página segura; este certificado lo creó tu PS5 para su propia dirección y no puede firmar nada más.',
download:'1. Descargar el certificado',a1:'Toca Descargar. Si Android dice que no puede instalarlo desde aquí, es normal.',a2:'Abre Ajustes > Seguridad (o Seguridad y privacidad) > Más ajustes de seguridad > Cifrado y credenciales > Instalar un certificado > Certificado de CA.',a3:'Confirma, elige el archivo descargado e introduce el PIN del móvil.',
i1:'Toca Descargar y después Permitir.',i2:'Ajustes > General > VPN y gestión de dispositivos > perfil PS5X360E > Instalar.',i3:'Ajustes > General > Información > Ajustes de confianza de certificados > activa "PS5X360E Kinect camera".',
again:'2. Ya lo instalé: abrir la cámara',anyway:'Abrir sin instalar',anyway_note:'El navegador avisará de que la conexión no es privada; continuar también funciona, pero el aviso vuelve.'}};
const w=W[(navigator.language||'en').slice(0,2).toLowerCase()]||W.en;
document.querySelectorAll('[data-t]').forEach(e=>e.textContent=w[e.dataset.t]);
const target=boot.https+'#k='+encodeURIComponent(boot.key);
document.getElementById('anyway').href=target;
async function open(){
  document.getElementById('status').textContent=w.checking;
  // Succeeds only when this phone trusts the console's certificate.
  try{await fetch(boot.https+'ping',{mode:'no-cors',cache:'no-store'});location.replace(target);}
  catch(e){document.getElementById('status').textContent='';document.getElementById('steps').style.display='block';}
}
document.getElementById('again').onclick=open;
open();
</script></body></html>)CAMERA";
}  // namespace xbox360ps5::phone_camera

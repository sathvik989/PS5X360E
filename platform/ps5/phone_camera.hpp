// SPDX-License-Identifier: MIT
#pragma once
#include <sys/socket.h>
#include <arpa/inet.h>
#include <cstdint>
#include <fstream>
#include <string>

namespace xbox360ps5::phone_camera {
#ifndef PHONE_CAMERA_CONFIG_PATH
#define PHONE_CAMERA_CONFIG_PATH "/app0/phone-camera.conf"
#endif
#ifndef PHONE_CAMERA_CA_PATH
#define PHONE_CAMERA_CA_PATH "/app0/assets/phone-camera-ca.crt"
#endif
// A local HTTPS origin, never a redirector, path, credential or key carrier.
inline std::string RelayOrigin(const std::string& line) {
  if (line.size() > 255 || line.compare(0, 8, "https://")) return {};
  std::string authority = line.substr(8);
  if (!authority.empty() && authority.back() == '/') authority.pop_back();
  if (authority.empty() || authority.find_first_of("/@?#\\") != std::string::npos) return {};
  for (unsigned char c : authority) if (!((c >= '0' && c <= '9') || c == '.' || c == ':')) return {};
  const auto colon = authority.find(':');
  const std::string host = authority.substr(0, colon);
  in_addr ip{};
  if (inet_pton(AF_INET, host.c_str(), &ip) != 1) return {};
  const uint32_t value = ntohl(ip.s_addr);
  if (!((value >> 24) == 10 || (value >> 20) == 0xAC1 || (value >> 16) == 0xC0A8)) return {};
  if (colon != std::string::npos) {
    const auto port = authority.substr(colon + 1);
    if (port.empty() || port.size() > 5 || port.front() == '0') return {};
    unsigned number = 0;
    for (char c : port) {
      if (c < '0' || c > '9') return {};
      number = number * 10 + unsigned(c - '0');
    }
    if (!number || number > 65535) return {};
  }
  return "https://" + authority + "/";
}

inline std::string ReadRelay(const char* path = PHONE_CAMERA_CONFIG_PATH) {
  std::ifstream file(path, std::ios::binary);
  std::string line;
  char c;
  while (file.get(c) && c != '\n') {
    if (line.size() >= 256) return {};
    line += c;
  }
  if (!line.empty() && line.back() == '\r') line.pop_back();
  return RelayOrigin(line);
}

inline std::string ReadPublicCA() {
  std::ifstream file(PHONE_CAMERA_CA_PATH, std::ios::binary);
  if (!file) return {};
  std::string bytes;
  char buffer[4096];
  while (file.read(buffer, sizeof(buffer)) || file.gcount()) {
    if (bytes.size() + size_t(file.gcount()) > 65536) return {};
    bytes.append(buffer, size_t(file.gcount()));
  }
  if (file.bad()) return {};
  // Serve PEM public certificate blocks only. Even a misplaced private key
  // or appended non-certificate payload is never returned by this endpoint.
  const std::string begin = "-----BEGIN CERTIFICATE-----";
  const std::string end = "-----END CERTIFICATE-----";
  size_t at = 0;
  bool found = false;
  while (at < bytes.size()) {
    while (at < bytes.size() && (bytes[at] == ' ' || bytes[at] == '\r' || bytes[at] == '\n' || bytes[at] == '\t')) ++at;
    if (at == bytes.size()) break;
    if (bytes.compare(at, begin.size(), begin)) return {};
    at += begin.size();
    const auto close = bytes.find(end, at);
    if (close == std::string::npos || close == at) return {};
    for (; at < close; ++at) {
      const unsigned char c = bytes[at];
      if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=' || c == '\r' || c == '\n')) return {};
    }
    at += end.size(); found = true;
  }
  return found ? bytes : std::string();
}

inline constexpr char kLanding[] = R"CAMERA(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="referrer" content="no-referrer"><title>PS5X360 · Phone camera</title>
<style>
:root{color-scheme:dark}body{margin:0;background:#0a0c11;color:#d5dae2;font:16px/1.5 system-ui,sans-serif}
main{max-width:640px;margin:auto;padding:24px 18px}h1{font-size:24px;color:#f3f5f8}
section{background:#1a1f28;border:1px solid #303949;border-radius:12px;padding:16px;margin:16px 0}
a{color:#35d07f}a.button{display:inline-block;background:#35d07f;color:#06130c;padding:12px 18px;border-radius:10px;font-weight:600;text-decoration:none}
code{display:block;overflow-wrap:anywhere;color:#f3f5f8;padding:8px 0}#pairing{font-size:24px;letter-spacing:.15em}
[hidden]{display:none!important}p{margin:10px 0}pre{white-space:pre-wrap;overflow-wrap:anywhere;font:13px/1.5 ui-monospace,monospace}
</style></head><body><main>
<a href="/" id="back"></a><h1 id="title"></h1><p id="intro"></p>
<section><h2 id="pairTitle"></h2><p id="addressLabel"></p><code id="console"></code><p id="keyLabel"></p><code id="pairing"></code><p id="manual"></p></section>
<section id="ready" hidden><p id="trusted"></p><code id="relayAddress"></code><a class="button" id="relay" target="_blank" rel="noopener noreferrer" referrerpolicy="no-referrer"></a><p id="certificate"></p></section>
<section><h2 id="caTitle"></h2><p id="caWhy"></p><a id="caDownload" href="/phone-camera-ca.crt" download="phone-camera-ca.crt" hidden></a><p id="caMissing" hidden></p><h3>Android</h3><p id="android"></p><h3>iPhone / iPad</h3><p id="ios"></p></section>
<section id="setup" hidden><h2 id="setupTitle"></h2><p id="setupText"></p><pre>/app0/phone-camera.conf
https://192.168.1.100:8781/</pre><p id="setupCert"></p></section>
<p id="limitations"></p><noscript>Enable JavaScript / Ative o JavaScript.</noscript>
</main><script>
const boot=__CAMERA_BOOT__;
const pt=(navigator.language||'en').toLowerCase().startsWith('pt');
const text=pt?{
 back:'← Configurações',title:'Usar câmera do celular',intro:'Use o celular e o PS5 na mesma rede. A câmera abre no serviço HTTPS local configurado.',
 pairTitle:'Pareamento manual',addressLabel:'Endereço das configurações do PS5',keyLabel:'Código atual mostrado na TV',manual:'No serviço local de câmera, informe manualmente este endereço e código. O botão não envia o código; ele aparece apenas aqui.',
 trusted:'Serviço HTTPS local configurado. Confirme que este endereço pertence ao seu computador de confiança:',relay:'Abrir câmera do celular',certificate:'O celular precisa confiar no certificado HTTPS do serviço. Depois, permita a câmera quando o navegador solicitar.',
 setupTitle:'Configure o serviço de câmera',setupText:'Nenhum endereço HTTPS local válido foi configurado. No pacote do PS5, crie phone-camera.conf com o endereço do serviço de câmera do seu computador na primeira linha, por exemplo:',setupCert:'Use um IP privado da rede, HTTPS e certificado confiável no celular. Não use túneis públicos.',
 caTitle:'Certificado da rede local',caWhy:'Antes de abrir a câmera, instale e confie nesta CA no celular. Instale apenas a CA do seu próprio serviço local. O download contém somente o certificado público.',caDownload:'Baixar certificado público da CA',caMissing:'Certificado indisponível. Inclua o certificado público do serviço em assets/phone-camera-ca.crt no pacote do PS5.',
 android:'Baixe o certificado. Em Configurações, procure “Instalar certificado” e escolha certificado de CA (Segurança / Criptografia e credenciais, conforme o aparelho). Confirme a instalação e reabra o serviço HTTPS no navegador.',
 ios:'Baixe o certificado e permita o perfil. Em Ajustes → Geral → VPN e Gerenciamento de Dispositivo, instale o perfil. Depois, em Geral → Sobre → Ajustes de Confiança de Certificados, ative a confiança total para esta CA. Reabra o serviço HTTPS no Safari.',
 limitations:'Esta página HTTP de configurações não acessa a câmera. O serviço HTTPS local é necessário; a disponibilidade da câmera e o pareamento dependem desse serviço.'
}:{
 back:'← Settings',title:'Use phone camera',intro:'Keep your phone and PS5 on the same network. The camera opens in the configured local HTTPS service.',
 pairTitle:'Manual pairing',addressLabel:'PS5 settings address',keyLabel:'Current code shown on the TV',manual:'In the local camera service, enter this address and code manually. The button does not send the code; it is only displayed here.',
 trusted:'Configured local HTTPS service. Confirm this address belongs to your trusted computer:',relay:'Open phone camera',certificate:'Your phone must trust the service’s HTTPS certificate. Then allow camera access when the browser asks.',
 setupTitle:'Set up the camera service',setupText:'No valid local HTTPS address is configured. In the PS5 package, create phone-camera.conf with your computer’s camera service address on the first line, for example:',setupCert:'Use a private LAN IP, HTTPS and a certificate trusted by your phone. Do not use public tunnels.',
 caTitle:'Local network certificate',caWhy:'Before opening the camera, install and trust this CA on your phone. Install only the CA from your own local service. The download contains only the public certificate.',caDownload:'Download public CA certificate',caMissing:'Certificate unavailable. Include your service’s public certificate as assets/phone-camera-ca.crt in the PS5 package.',
 android:'Download the certificate. In Settings, search for “Install certificate” and select CA certificate (Security / Encryption and credentials, depending on your device). Confirm installation, then reopen the HTTPS service in your browser.',
 ios:'Download the certificate and allow the profile. In Settings → General → VPN & Device Management, install the profile. Then in General → About → Certificate Trust Settings, enable full trust for this CA. Reopen the HTTPS service in Safari.',
 limitations:'This HTTP settings page does not access the camera. The local HTTPS service is required; camera availability and pairing depend on that service.'
};
document.documentElement.lang=pt?'pt-BR':'en';document.title='PS5X360 · '+text.title;
for(const [id,value] of Object.entries(text))document.getElementById(id).textContent=value;
document.getElementById('pairing').textContent=boot.key;
document.getElementById('console').textContent=location.origin+'/';
document.getElementById('back').href='/?k='+encodeURIComponent(boot.key);
sessionStorage.setItem('k',boot.key);
if(boot.relay){document.getElementById('relay').href=boot.relay;document.getElementById('relayAddress').textContent=boot.relay;document.getElementById('ready').hidden=false;}
else document.getElementById('setup').hidden=false;
document.getElementById(boot.ca?'caDownload':'caMissing').hidden=false;
</script></body></html>
)CAMERA";
}  // namespace xbox360ps5::phone_camera

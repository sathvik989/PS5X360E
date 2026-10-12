// SPDX-License-Identifier: MIT
// See phone_camera_tls.hpp. TLS 1.2 with an ECDSA P-256 certificate (mbedTLS); one thread per
// phone connection, at most six; HTTP/1.1 keep-alive so a phone sends every pose frame on one
// connection. Only the camera page's own files, a reachability probe and the two motion
// routes (key required) are served.
#include "phone_camera_tls.hpp"

#include "xbox360ps5/motion_input.hpp"

#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/oid.h"
#include "mbedtls/pk.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>

// mbedTLS asks the platform for entropy here (MBEDTLS_ENTROPY_HARDWARE_ALT); the same source
// the settings page uses for its key.
extern "C" int mbedtls_hardware_poll(void*, unsigned char* output, size_t length, size_t* produced) {
  static std::mutex lock;
  static std::random_device device;
  std::lock_guard guard(lock);
  for (size_t at = 0; at < length; at += 4) {
    const unsigned value = device();
    std::memcpy(output + at, &value, std::min<size_t>(4, length - at));
  }
  *produced = length;
  return 0;
}

namespace xbox360ps5::phone_tls {
namespace {
constexpr int kAuthorityDays = 398, kCertificateDays = 397, kMaxConnections = 6;
constexpr const char* kAssets = "/app0/assets/kinect/";
constexpr const char* kAuthorityName = "CN=PS5X360 Kinect camera (this console),O=PS5X360";

std::mutex state_mutex;           // authority_pem, url, folder, the certificate objects
std::string authority_pem, url, folder, key;
std::function<std::string()> address_of;
std::atomic<bool> started{false};
std::atomic<int> connections{0};
int listener = -1;
unsigned short port = 0;

std::mutex random_mutex;
mbedtls_entropy_context entropy;
mbedtls_ctr_drbg_context drbg;
int Random(void*, unsigned char* out, size_t length) {
  std::lock_guard guard(random_mutex);
  return mbedtls_ctr_drbg_random(&drbg, out, length);
}

// The TLS configuration with its certificate. Replaced (address change) only by the accepting
// thread while no connection is open, so a connection's configuration never goes away under it.
struct Identity {
  mbedtls_ssl_config config;
  mbedtls_x509_crt chain;
  mbedtls_pk_context key;
  Identity() { mbedtls_ssl_config_init(&config); mbedtls_x509_crt_init(&chain); mbedtls_pk_init(&key); }
  ~Identity() { mbedtls_ssl_config_free(&config); mbedtls_x509_crt_free(&chain); mbedtls_pk_free(&key); }
};
std::unique_ptr<Identity> identity;

std::string ReadFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return in ? std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()) : std::string();
}
bool WriteFile(const std::string& path, const std::string& data, bool secret = false) {
  const std::string temporary = path + ".new";
  {
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(data.data(), std::streamsize(data.size()));
    if (!out) return false;
  }
  if (secret) chmod(temporary.c_str(), 0600);
  return std::rename(temporary.c_str(), path.c_str()) == 0;
}

std::string Stamp(time_t when) {
  tm parts{};
  gmtime_r(&when, &parts);
  char text[16];
  std::strftime(text, sizeof(text), "%Y%m%d%H%M%S", &parts);
  return text;
}

struct Key {
  mbedtls_pk_context pk;
  Key() { mbedtls_pk_init(&pk); }
  ~Key() { mbedtls_pk_free(&pk); }
  bool Make() {
    return !mbedtls_pk_setup(&pk, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY)) &&
           !mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(pk), Random, nullptr);
  }
};
struct Writer {
  mbedtls_x509write_cert crt;
  Writer() { mbedtls_x509write_crt_init(&crt); }
  ~Writer() { mbedtls_x509write_crt_free(&crt); }
};

bool SetCommon(Writer& w, Key& subject, Key& issuer, const char* subject_name, time_t from, int days) {
  unsigned char serial[16];
  if (Random(nullptr, serial, sizeof(serial))) return false;
  serial[0] = (serial[0] & 0x7F) | 0x01;  // positive and non-zero
  mbedtls_x509write_crt_set_version(&w.crt, MBEDTLS_X509_CRT_VERSION_3);
  mbedtls_x509write_crt_set_md_alg(&w.crt, MBEDTLS_MD_SHA256);
  mbedtls_x509write_crt_set_subject_key(&w.crt, &subject.pk);
  mbedtls_x509write_crt_set_issuer_key(&w.crt, &issuer.pk);
  return !mbedtls_x509write_crt_set_subject_name(&w.crt, subject_name) &&
         !mbedtls_x509write_crt_set_issuer_name(&w.crt, kAuthorityName) &&
         !mbedtls_x509write_crt_set_serial_raw(&w.crt, serial, sizeof(serial)) &&
         !mbedtls_x509write_crt_set_validity(&w.crt, Stamp(from).c_str(), Stamp(from + time_t(days) * 86400).c_str()) &&
         !mbedtls_x509write_crt_set_subject_key_identifier(&w.crt) &&
         !mbedtls_x509write_crt_set_authority_key_identifier(&w.crt);
}
std::string Pem(Writer& w) {
  std::string out(4096, '\0');
  if (mbedtls_x509write_crt_pem(&w.crt, reinterpret_cast<unsigned char*>(out.data()), out.size(), Random, nullptr)) return {};
  out.resize(std::strlen(out.c_str()));
  return out;
}

// A one-use authority and a certificate for `address`; the authority's key is dropped here.
bool MakeCertificate(const std::string& address, std::string* authority, std::string* certificate, std::string* private_key) {
  in_addr ip{};
  if (inet_pton(AF_INET, address.c_str(), &ip) != 1) return false;
  Key authority_key, server_key;
  if (!authority_key.Make() || !server_key.Make()) return false;
  const time_t from = time(nullptr) - 600;

  Writer ca;
  if (!SetCommon(ca, authority_key, authority_key, kAuthorityName, from, kAuthorityDays) ||
      mbedtls_x509write_crt_set_basic_constraints(&ca.crt, 1, 0) ||
      mbedtls_x509write_crt_set_key_usage(&ca.crt, MBEDTLS_X509_KU_KEY_CERT_SIGN | MBEDTLS_X509_KU_CRL_SIGN))
    return false;
  *authority = Pem(ca);

  Writer leaf;
  const std::string subject = "CN=" + address + ",O=PS5X360";
  mbedtls_asn1_sequence server_auth{};
  server_auth.buf.tag = MBEDTLS_ASN1_OID;
  server_auth.buf.p = reinterpret_cast<unsigned char*>(const_cast<char*>(MBEDTLS_OID_SERVER_AUTH));
  server_auth.buf.len = MBEDTLS_OID_SIZE(MBEDTLS_OID_SERVER_AUTH);
  mbedtls_x509_san_list san{};
  san.node.type = MBEDTLS_X509_SAN_IP_ADDRESS;
  san.node.san.unstructured_name.p = reinterpret_cast<unsigned char*>(&ip);
  san.node.san.unstructured_name.len = 4;
  if (!SetCommon(leaf, server_key, authority_key, subject.c_str(), from, kCertificateDays) ||
      mbedtls_x509write_crt_set_basic_constraints(&leaf.crt, 0, -1) ||
      mbedtls_x509write_crt_set_key_usage(&leaf.crt, MBEDTLS_X509_KU_DIGITAL_SIGNATURE) ||
      mbedtls_x509write_crt_set_ext_key_usage(&leaf.crt, &server_auth) ||
      mbedtls_x509write_crt_set_subject_alternative_name(&leaf.crt, &san))
    return false;
  *certificate = Pem(leaf);

  std::string pem(2048, '\0');
  if (mbedtls_pk_write_key_pem(&server_key.pk, reinterpret_cast<unsigned char*>(pem.data()), pem.size())) return false;
  pem.resize(std::strlen(pem.c_str()));
  *private_key = pem;
  return !authority->empty() && !certificate->empty();
}

// Loads (or makes) the certificate for `address`. Caller holds state_mutex, no connection open.
bool UseAddress(const std::string& address) {
  const std::string info_path = folder + "/certificate.txt";
  std::string saved_address;
  long long expires = 0;
  {
    std::istringstream info(ReadFile(info_path));
    info >> saved_address >> expires;
  }
  std::string ca = ReadFile(folder + "/authority.crt"), crt = ReadFile(folder + "/server.crt"),
              pem = ReadFile(folder + "/server.key");
  const long long now = time(nullptr);
  if (saved_address != address || expires < now + 7 * 86400 || ca.empty() || crt.empty() || pem.empty()) {
    if (!MakeCertificate(address, &ca, &crt, &pem)) return false;
    mkdir(folder.c_str(), 0700);
    if (!WriteFile(folder + "/authority.crt", ca) || !WriteFile(folder + "/server.crt", crt) ||
        !WriteFile(folder + "/server.key", pem, true) ||
        !WriteFile(info_path, address + " " + std::to_string(now + (kCertificateDays - 1) * 86400LL) + "\n"))
      return false;
  }
  auto next = std::make_unique<Identity>();
  if (mbedtls_x509_crt_parse(&next->chain, reinterpret_cast<const unsigned char*>(crt.c_str()), crt.size() + 1) ||
      mbedtls_pk_parse_key(&next->key, reinterpret_cast<const unsigned char*>(pem.c_str()), pem.size() + 1, nullptr, 0,
                           Random, nullptr) ||
      mbedtls_ssl_config_defaults(&next->config, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_STREAM,
                                  MBEDTLS_SSL_PRESET_DEFAULT))
    return false;
  mbedtls_ssl_conf_rng(&next->config, Random, nullptr);
  mbedtls_ssl_conf_min_tls_version(&next->config, MBEDTLS_SSL_VERSION_TLS1_2);
  if (mbedtls_ssl_conf_own_cert(&next->config, &next->chain, &next->key)) return false;
  identity = std::move(next);
  authority_pem = ca;
  url = "https://" + address + ":" + std::to_string(port) + "/";
  return true;
}

// ---- One phone connection --------------------------------------------------------------------
int SendBytes(void* context, const unsigned char* data, size_t length) {
  const int fd = *static_cast<int*>(context);
  const ssize_t sent = send(fd, data, length, 0);
  if (sent >= 0) return int(sent);
  return errno == EINTR ? MBEDTLS_ERR_SSL_WANT_WRITE : MBEDTLS_ERR_NET_SEND_FAILED;
}
int ReceiveBytes(void* context, unsigned char* data, size_t length) {
  const int fd = *static_cast<int*>(context);
  const ssize_t got = recv(fd, data, length, 0);
  if (got > 0) return int(got);
  if (got == 0) return MBEDTLS_ERR_NET_CONN_RESET;
  return errno == EINTR ? MBEDTLS_ERR_SSL_WANT_READ : MBEDTLS_ERR_NET_RECV_FAILED;  // includes the idle timeout
}

std::string Header(const std::string& head, const char* name) {
  const std::string wanted = std::string("\r\n") + name + ":";
  for (size_t at = head.find("\r\n"); at != std::string::npos; at = head.find("\r\n", at + 2)) {
    if (head.size() < at + wanted.size()) break;
    bool same = true;
    for (size_t n = 0; n < wanted.size() && same; ++n)
      same = std::tolower(static_cast<unsigned char>(head[at + n])) == std::tolower(static_cast<unsigned char>(wanted[n]));
    if (!same) continue;
    size_t begin = at + wanted.size(), end = head.find("\r\n", begin);
    while (begin < end && head[begin] == ' ') ++begin;
    return head.substr(begin, end - begin);
  }
  return {};
}

std::string Response(const char* status, const char* type, const std::string& body, const char* extra = "") {
  std::string out = std::string("HTTP/1.1 ") + status + "\r\nContent-Type: " + type +
                    "\r\nContent-Length: " + std::to_string(body.size()) +
                    "\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\n" +
                    extra + "Connection: keep-alive\r\n\r\n";
  return out + body;
}

std::string Route(const std::string& method, std::string target, const std::string& head, const std::string& body) {
  if (const size_t mark = target.find('?'); mark != std::string::npos) target.resize(mark);
  if (method == "GET" && (target == "/" || target == "/index.html")) {
    std::string page = ReadFile(std::string(kAssets) + "index.html");
    if (page.empty()) return Response("404 Not Found", "text/plain", "camera page missing");
    // Tell the page it is served by the console (direct motion API, no PC relay).
    const std::string marker = "<meta name=\"ps5x360-host\" content=\"relay\">";
    if (const size_t at = page.find(marker); at != std::string::npos)
      page.replace(at, marker.size(), "<meta name=\"ps5x360-host\" content=\"ps5\">");
    return Response("200 OK", "text/html; charset=utf-8", page);
  }
  if (method == "GET" && (target == "/pose.mjs" || target == "/kinect_tracker.mjs" || target == "/demo_body.mjs")) {
    const std::string code = ReadFile(std::string(kAssets) + target.substr(1));
    return code.empty() ? Response("404 Not Found", "text/plain", "missing")
                        : Response("200 OK", "text/javascript; charset=utf-8", code);
  }
  if (target == "/ping") return Response("204 No Content", "text/plain", "");
  if (target == "/favicon.ico") return Response("404 Not Found", "text/plain", "");
  if (Header(head, "X-Key") != key) return Response("403 Forbidden", "application/json", "{\"error\":\"key\"}");
  if (method == "GET" && target == "/api/motion/status") return Response("200 OK", "application/json", motion::Status());
  if (method == "POST" && target == "/api/motion/frame") {
    const auto result = motion::Receive(body);
    if (result == motion::Result::accepted) return Response("200 OK", "application/json", "{\"ok\":true}");
    return Response(result == motion::Result::malformed ? "400 Bad Request" : "409 Conflict", "application/json",
                    result == motion::Result::disabled ? "{\"error\":\"disabled\"}" : "{\"error\":\"invalid motion frame\"}");
  }
  return Response("404 Not Found", "text/plain", "not found");
}

struct Connection {
  int fd;
  const mbedtls_ssl_config* config;
};
void* Serve(void* argument) {
  std::unique_ptr<Connection> connection(static_cast<Connection*>(argument));
  int fd = connection->fd;
  timeval idle{20, 0};
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &idle, sizeof(idle));
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &idle, sizeof(idle));
#ifdef SO_NOSIGPIPE
  const int yes = 1;
  setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
#endif
  mbedtls_ssl_context ssl;
  mbedtls_ssl_init(&ssl);
  if (!mbedtls_ssl_setup(&ssl, connection->config)) {
    mbedtls_ssl_set_bio(&ssl, &fd, SendBytes, ReceiveBytes, nullptr);
    int status;
    while ((status = mbedtls_ssl_handshake(&ssl)) == MBEDTLS_ERR_SSL_WANT_READ || status == MBEDTLS_ERR_SSL_WANT_WRITE) {}
    std::string data;
    unsigned char buffer[4096];
    while (status == 0) {
      size_t head_end;
      while ((head_end = data.find("\r\n\r\n")) == std::string::npos && data.size() < 16384) {
        const int got = mbedtls_ssl_read(&ssl, buffer, sizeof(buffer));
        if (got == MBEDTLS_ERR_SSL_WANT_READ || got == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
        if (got <= 0) { status = -1; break; }
        data.append(reinterpret_cast<char*>(buffer), size_t(got));
      }
      if (status || head_end == std::string::npos) break;
      const std::string head = data.substr(0, head_end + 2);
      const size_t length = std::min<size_t>(size_t(std::atoi(Header(head, "Content-Length").c_str())), 8192);
      while (data.size() < head_end + 4 + length) {
        const int got = mbedtls_ssl_read(&ssl, buffer, sizeof(buffer));
        if (got == MBEDTLS_ERR_SSL_WANT_READ || got == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
        if (got <= 0) { status = -1; break; }
        data.append(reinterpret_cast<char*>(buffer), size_t(got));
      }
      if (status) break;
      const size_t space = head.find(' '), space2 = head.find(' ', space + 1);
      if (space == std::string::npos || space2 == std::string::npos) break;
      const std::string reply = Route(head.substr(0, space), head.substr(space + 1, space2 - space - 1), head,
                                      data.substr(head_end + 4, length));
      data.erase(0, head_end + 4 + length);
      for (size_t sent = 0; sent < reply.size();) {
        const int wrote = mbedtls_ssl_write(&ssl, reinterpret_cast<const unsigned char*>(reply.data()) + sent, reply.size() - sent);
        if (wrote == MBEDTLS_ERR_SSL_WANT_READ || wrote == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
        if (wrote <= 0) { status = -1; break; }
        sent += size_t(wrote);
      }
      std::string close_header = Header(head, "Connection");
      std::transform(close_header.begin(), close_header.end(), close_header.begin(), ::tolower);
      if (close_header == "close") break;
    }
    mbedtls_ssl_close_notify(&ssl);
  }
  mbedtls_ssl_free(&ssl);
  close(fd);
  --connections;
  return nullptr;
}

void* Listen(void*) {
  // Wait for a network address and a certificate for it.
  std::string address;
  for (;;) {
    address = address_of ? address_of() : std::string();
    if (!address.empty()) {
      std::lock_guard guard(state_mutex);
      if (UseAddress(address)) break;
    }
    sleep(3);
  }
  for (;;) {
    const int client = accept(listener, nullptr, nullptr);
    if (client < 0) { usleep(100000); continue; }
    // A new network address needs a new certificate: switch while no phone is connected.
    if (connections.load() == 0) {
      const std::string now = address_of();
      if (!now.empty() && now != address) {
        std::lock_guard guard(state_mutex);
        if (UseAddress(now)) address = now;
      }
    }
    if (connections.load() >= kMaxConnections) { close(client); continue; }
    ++connections;
    pthread_t thread;
    auto* connection = new Connection{client, &identity->config};
    if (pthread_create(&thread, nullptr, Serve, connection)) {
      delete connection;
      close(client);
      --connections;
      continue;
    }
    pthread_detach(thread);
  }
  return nullptr;
}
}  // namespace

bool Start(const std::string& certificate_folder, std::function<std::string()> address, const std::string& pairing_key) {
  if (started.exchange(true)) return true;
  folder = certificate_folder;
  address_of = std::move(address);
  key = pairing_key;
  mbedtls_entropy_init(&entropy);
  mbedtls_ctr_drbg_init(&drbg);
  static const char personal[] = "PS5X360 phone camera";
  if (mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy, reinterpret_cast<const unsigned char*>(personal),
                            sizeof(personal) - 1))
    return false;
  listener = socket(AF_INET, SOCK_STREAM, 0);
  if (listener < 0) return false;
  const int yes = 1;
  setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  // Some ports are refused on the console (EACCES): try a small range.
  for (unsigned short candidate = 8370; candidate < 8378 && !port; ++candidate) {
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(candidate);
    if (!bind(listener, reinterpret_cast<sockaddr*>(&local), sizeof(local))) port = candidate;
  }
  pthread_t thread;
  if (!port || listen(listener, 8) || pthread_create(&thread, nullptr, Listen, nullptr)) {
    close(listener);
    listener = -1;
    return false;
  }
  pthread_detach(thread);
  return true;
}

std::string AuthorityPem() {
  std::lock_guard guard(state_mutex);
  return authority_pem;
}

std::string Url() {
  std::lock_guard guard(state_mutex);
  return url;
}
}  // namespace xbox360ps5::phone_tls

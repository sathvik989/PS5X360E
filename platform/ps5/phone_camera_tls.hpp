// SPDX-License-Identifier: MIT
// Phone camera served by the console itself (no PC): a small HTTPS server for the Kinect
// camera page. Phone browsers only open the camera on a secure page, so the console makes its
// own certificate: a one-use authority (its private key is never stored) signs a certificate
// for the console's network address. The phone installs that authority once from the plain
// settings page; afterwards the QR code on the TV opens the camera page directly. Poses go to
// the same motion receiver as the settings page's /api/motion/frame, with the same key.
#pragma once
#include <functional>
#include <string>

namespace xbox360ps5::phone_tls {
// Starts the server thread once. `folder` keeps the certificate between runs; `address`
// returns the console's current network address (it may only appear later); `key` is the
// settings-page key. The camera page is read from /app0/assets/kinect.
bool Start(const std::string& folder, std::function<std::string()> address, const std::string& key);
// The authority to install on the phone (PEM); empty until it has been made.
std::string AuthorityPem();
// https://ADDRESS:PORT/ while serving with a certificate for the current address; empty otherwise.
std::string Url();
}  // namespace xbox360ps5::phone_tls

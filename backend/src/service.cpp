#include "service.h"
#include <grpcpp/support/status.h>
#include <memory>
#include <tins/ip.h>
#include <tins/network_interface.h>
#include <tins/packet_sender.h>
#include <tins/pdu.h>
#include <tins/sniffer.h>
#include <tins/tcp.h>
#include <tins/tins.h>
#include <tins/utils/routing_utils.h>

using grpc::ServerContext;

using client_window = yarilo::WPA2Decrypter::client_window;
using group_window = yarilo::WPA2Decrypter::group_window;

namespace yarilo {

Service::Service(const std::filesystem::path &save_path,
                 const std::filesystem::path &sniff_path)
    : save_path(save_path), sniff_path(sniff_path) {
  logger = spdlog::stdout_color_mt("Service");
  logger->info("Created a service using save path: {} and sniff file path {}",
               save_path.string(), sniff_path.string());
}

bool Service::add_file_sniffer(const std::filesystem::path &file) {
  try {
    auto sniffer = std::make_unique<Tins::FileSniffer>(file.string());
    sniffers.push_back(std::make_unique<Sniffer>(std::move(sniffer), file));
    sniffers.back()->start();
  } catch (const Tins::pcap_error &e) {
    logger->error("Error while initializing the sniffer: {}", e.what());
    return false;
  }

  logger->info("Added new sniffer: FileSniffer ({})", file.string());
  return true;
}

bool Service::add_iface_sniffer(const std::string &iface_name) {
  std::optional<std::string> iface =
      yarilo::Sniffer::detect_interface(this->logger, iface_name);
  if (!iface.has_value()) {
    logger->error("Didn't find suitable interface");
    return false;
  }

  logger->info("Sniffing using interface: {}", iface.value());

  std::set<std::string> interfaces = Tins::Utils::network_interfaces();
  if (!interfaces.count(iface.value())) {
    logger->error("There is no available interface by that name: {}",
                  iface.value());
    return false;
  }

  try {
    auto sniffer = std::make_unique<Tins::Sniffer>(iface.value());
    sniffers.push_back(std::make_unique<Sniffer>(
        std::move(sniffer), Tins::NetworkInterface(iface.value())));
    sniffers.back()->start();
  } catch (const Tins::pcap_error &e) {
    logger->error("Error while initializing the sniffer: {}", e.what());
    return false;
  }

  logger->info("Added new sniffer: InterfaceSniffer ({})", iface.value());
  return true;
}

void Service::shutdown() {
  logger->info("Service shutdown, forcing all sniffers to stop");
  for (auto &sniffer : sniffers)
    sniffer->shutdown();
}

grpc::Status Service::SnifferCreate(grpc::ServerContext *context,
                                    const proto::SnifferCreateRequest *request,
                                    proto::SnifferID *reply) {
  if (request->is_file_based()) {
    std::filesystem::path path = sniff_path;
    path.append(request->filename());
    if (!add_file_sniffer(path))
      return grpc::Status(grpc::StatusCode::INTERNAL,
                          "Unable to create the sniffer");

    reply->set_id(sniffers.size() - 1);
    return grpc::Status::OK;
  }

  for (const auto &sniffer : sniffers) {
    std::optional<std::string> net_iface_name = sniffer->iface();
    if (net_iface_name.has_value() &&
        net_iface_name.value() == request->net_iface_name())
      return grpc::Status(grpc::StatusCode::ALREADY_EXISTS,
                          "There already exists a sniffer on this interface");
  }

  if (!add_iface_sniffer(request->net_iface_name()))
    return grpc::Status(grpc::StatusCode::INTERNAL,
                        "Unable to create the sniffer");

  reply->set_id(sniffers.size() - 1);
  return grpc::Status::OK;
}

grpc::Status Service::SnifferDestroy(grpc::ServerContext *context,
                                     const proto::SnifferID *request,
                                     proto::Empty *reply) {
  if (request->id() >= sniffers.size())
    return grpc::Status(grpc::StatusCode::NOT_FOUND, "No sniffer with this id");
  sniffers[request->id()]->shutdown();
  erased_sniffers.push_back(std::move(sniffers[request->id()]));
  sniffers.erase(sniffers.begin() + request->id());
  return grpc::Status::OK;
}

grpc::Status Service::SnifferList(grpc::ServerContext *context,
                                  const proto::Empty *request,
                                  proto::SnifferListResponse *reply) {
  for (int i = 0; i < sniffers.size(); i++) {
    Sniffer *sniffer = sniffers[i].get();
    std::string name = "";
    std::string net_iface_name = "";
    std::string filename = "";

    bool is_file_based = sniffer->file().has_value();
    if (is_file_based) {
      name = sniffer->file()->stem().string();
      filename = sniffer->file()->filename().string();
    } else {
      name = sniffer->iface().value();
      net_iface_name = sniffer->iface().value();
    }

    proto::SnifferInfo *new_info = reply->add_sniffers();
    new_info->set_id(i);
    new_info->set_name(name);
    new_info->set_is_file_based(is_file_based);
    new_info->set_filename(filename);
    new_info->set_net_iface_name(net_iface_name);
  }

  return grpc::Status::OK;
}

grpc::Status Service::SniffFileList(grpc::ServerContext *context,
                                    const proto::Empty *request,
                                    proto::SniffFileListResponse *reply) {
  for (const auto &entry : std::filesystem::directory_iterator(sniff_path)) {
    if (!entry.is_regular_file())
      continue;

    if (entry.path().extension() != ".pcap" &&
        entry.path().extension() != ".pcapng")
      continue;

    auto new_filename = reply->add_filename();
    *new_filename = std::string(entry.path().filename());
  }

  return grpc::Status::OK;
}

grpc::Status
Service::SniffInterfaceList(grpc::ServerContext *context,
                            const proto::Empty *request,
                            proto::SniffInterfaceListResponse *reply) {
  for (const auto &iface : Tins::Utils::network_interfaces()) {
    auto new_name = reply->add_net_iface_name();
    *new_name = std::string(iface);
  }
  return grpc::Status::OK;
}

grpc::Status Service::GetAllAccessPoints(grpc::ServerContext *context,
                                         const proto::SnifferID *request,
                                         proto::NetworkList *reply) {
  if (request->id() >= sniffers.size())
    return grpc::Status(grpc::StatusCode::NOT_FOUND, "No sniffer with this id");
  Sniffer *sniffer = sniffers[request->id()].get();

  std::set<Sniffer::network_name> names = sniffer->all_networks();
  for (const auto &name : names) {
    auto new_name = reply->add_names();
    *new_name = std::string(name.second);
  }

  return grpc::Status::OK;
};

grpc::Status Service::GetAccessPoint(grpc::ServerContext *context,
                                     const proto::NetworkName *request,
                                     proto::NetworkInfo *reply) {
  if (request->sniffer_id() >= sniffers.size())
    return grpc::Status(grpc::StatusCode::NOT_FOUND, "No sniffer with this id");
  Sniffer *sniffer = sniffers[request->sniffer_id()].get();

  auto ap_option = sniffer->get_network(request->ssid());
  if (!ap_option.has_value())
    return grpc::Status(grpc::StatusCode::NOT_FOUND,
                        "No network with this ssid");

  std::shared_ptr<AccessPoint> ap = ap_option.value();
  reply->set_name(ap->get_ssid());
  reply->set_bssid(ap->get_bssid().to_string());
  reply->set_channel(ap->get_wifi_channel());
  reply->set_encrypted_packet_count(ap->raw_packet_count());
  reply->set_decrypted_packet_count(ap->decrypted_packet_count());

  WPA2Decrypter &decrypter = ap->get_decrypter();
  for (const auto &client_addr : decrypter.get_clients()) {
    std::optional<std::vector<client_window>> windows =
        decrypter.get_all_client_windows(client_addr);
    if (!windows.has_value())
      continue;

    proto::ClientInfo *info = reply->add_clients();
    info->set_addr(client_addr.to_string());
    if (!windows->size()) {
      info->set_is_decrypted(false);
      info->set_handshake_num(0);
      info->set_can_decrypt(false);
      continue;
    }

    client_window latest_window = windows->back();
    info->set_is_decrypted(latest_window.decrypted);
    info->set_handshake_num(latest_window.auth_packets.size());
    info->set_can_decrypt(latest_window.auth_packets.size() == 4);
  }

  return grpc::Status::OK;
}

grpc::Status Service::FocusNetwork(grpc::ServerContext *context,
                                   const proto::NetworkName *request,
                                   proto::Empty *reply) {
  if (request->sniffer_id() >= sniffers.size())
    return grpc::Status(grpc::StatusCode::NOT_FOUND, "No sniffer with this id");
  Sniffer *sniffer = sniffers[request->sniffer_id()].get();

  if (!sniffer->get_network(request->ssid()).has_value())
    return grpc::Status(grpc::StatusCode::NOT_FOUND,
                        "No network with this ssid");

  bool success = sniffer->focus_network(request->ssid());
  if (!success)
    return grpc::Status(grpc::StatusCode::INTERNAL,
                        "Unable to scan the network");
  return grpc::Status::OK;
}

grpc::Status Service::GetFocusState(grpc::ServerContext *context,
                                    const proto::SnifferID *request,
                                    proto::FocusState *reply) {
  if (request->id() >= sniffers.size())
    return grpc::Status(grpc::StatusCode::NOT_FOUND, "No sniffer with this id");
  Sniffer *sniffer = sniffers[request->id()].get();

  auto ap = sniffer->focused_network();
  bool focused = ap.has_value();

  reply->set_focused(focused);
  if (focused) {
    auto name = new proto::NetworkName();
    name->set_ssid(ap.value()->get_ssid());
    reply->set_allocated_name(name);
  };

  return grpc::Status::OK;
}

grpc::Status Service::StopFocus(grpc::ServerContext *context,
                                const proto::SnifferID *request,
                                proto::Empty *reply) {
  if (request->id() >= sniffers.size())
    return grpc::Status(grpc::StatusCode::NOT_FOUND, "No sniffer with this id");
  Sniffer *sniffer = sniffers[request->id()].get();

  sniffer->stop_focus();
  return grpc::Status::OK;
}

grpc::Status Service::ProvidePassword(ServerContext *context,
                                      const proto::DecryptRequest *request,
                                      proto::Empty *reply) {
  if (request->sniffer_id() >= sniffers.size())
    return grpc::Status(grpc::StatusCode::NOT_FOUND, "No sniffer with this id");
  Sniffer *sniffer = sniffers[request->sniffer_id()].get();

  auto ap = sniffer->get_network(request->ssid());
  if (!ap.has_value())
    return grpc::Status(grpc::StatusCode::NOT_FOUND,
                        "No network with this ssid");

  if (ap.value()->has_working_password())
    return grpc::Status(grpc::StatusCode::ALREADY_EXISTS, "Already decrypted");

  bool success = ap.value()->add_password(request->passwd());
  if (!success)
    return grpc::Status(grpc::StatusCode::UNKNOWN,
                        "Wrong password or no data to decrypt");

  return grpc::Status::OK;
};

grpc::Status
Service::GetDecryptedPackets(grpc::ServerContext *context,
                             const proto::NetworkName *request,
                             grpc::ServerWriter<proto::Packet> *writer) {
  if (request->sniffer_id() >= sniffers.size())
    return grpc::Status(grpc::StatusCode::NOT_FOUND, "No sniffer with this id");
  Sniffer *sniffer = sniffers[request->sniffer_id()].get();

  auto ap = sniffer->get_network(request->ssid());
  if (!ap.has_value())
    return grpc::Status(grpc::StatusCode::NOT_FOUND,
                        "No network with this ssid");

  // Make sure to cancel when the user cancels!
  std::shared_ptr<PacketChannel> channel = ap.value()->get_channel();
  std::thread([context, channel]() {
    while (true) {
      if (context->IsCancelled()) {
        channel->close();
        return;
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
  }).detach();

  logger->trace("Streaming packets");
  while (!channel->is_closed()) {
    std::optional<std::unique_ptr<Tins::EthernetII>> pkt_opt =
        channel->receive();
    if (!pkt_opt.has_value()) {
      logger->trace("Stream has been cancelled");
      return grpc::Status::OK;
    }

    std::unique_ptr<Tins::EthernetII> pkt = std::move(pkt_opt.value());
    auto ip = pkt->find_pdu<Tins::IP>();
    if (!ip)
      continue;

    auto tcp = pkt->find_pdu<Tins::TCP>();
    auto udp = pkt->find_pdu<Tins::UDP>();
    if (!tcp && !udp)
      continue;

    auto from = std::make_unique<proto::User>();
    from->set_ipv4address(ip->src_addr().to_string());
    from->set_macaddress(pkt->src_addr().to_string());
    from->set_port(tcp ? tcp->sport() : udp->sport());

    auto to = std::make_unique<proto::User>();
    to->set_ipv4address(ip->dst_addr().to_string());
    to->set_macaddress(pkt->dst_addr().to_string());
    to->set_port(tcp ? tcp->dport() : udp->dport());

    auto packet = std::make_unique<proto::Packet>();
    packet->set_protocol(tcp ? "TCP" : "UDP");
    packet->set_allocated_from(from.release());
    packet->set_allocated_to(to.release());

    std::vector<uint8_t> data =
        tcp ? tcp->clone()->serialize() : udp->clone()->serialize();
    std::string to_send(data.begin(), data.end());
    packet->set_data(to_send);
    writer->Write(*packet);
  }

  return grpc::Status::OK;
};

grpc::Status Service::DeauthNetwork(grpc::ServerContext *context,
                                    const proto::DeauthRequest *request,
                                    proto::Empty *reply) {
  if (request->sniffer_id() >= sniffers.size())
    return grpc::Status(grpc::StatusCode::NOT_FOUND, "No sniffer with this id");
  Sniffer *sniffer = sniffers[request->sniffer_id()].get();

  auto ap = sniffer->get_network(request->network().ssid());
  if (!ap.has_value())
    return grpc::Status(grpc::StatusCode::NOT_FOUND,
                        "No network with this ssid");

  // TODO: Check per-client
  bool pmf = ap.value()->protected_management_supported();
  if (pmf)
    return grpc::Status(grpc::StatusCode::UNAVAILABLE,
                        "Target network uses protected management frames");

  std::optional<Tins::NetworkInterface> iface = sniffer->iface();
  if (!iface.has_value())
    return grpc::Status(
        grpc::StatusCode::UNAVAILABLE,
        "This sniffer doesn't have a network interface attached");

  bool success = ap.value()->send_deauth(iface.value(), request->user_addr());
  if (!success)
    return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION,
                        "Not enough information to send the packet");

  return grpc::Status::OK;
};

grpc::Status Service::IgnoreNetwork(grpc::ServerContext *context,
                                    const proto::NetworkName *request,
                                    proto::Empty *reply) {
  if (request->sniffer_id() >= sniffers.size())
    return grpc::Status(grpc::StatusCode::NOT_FOUND, "No sniffer with this id");
  Sniffer *sniffer = sniffers[request->sniffer_id()].get();

  auto ap = sniffer->focused_network();
  if (ap.has_value() && ap.value()->get_ssid() == request->ssid())
    sniffer->stop_focus();

  sniffer->add_ignored_network(request->ssid());
  return grpc::Status::OK;
};

grpc::Status Service::GetIgnoredNetworks(grpc::ServerContext *context,
                                         const proto::SnifferID *request,
                                         proto::NetworkList *reply) {
  if (request->id() >= sniffers.size())
    return grpc::Status(grpc::StatusCode::NOT_FOUND, "No sniffer with this id");
  Sniffer *sniffer = sniffers[request->id()].get();

  for (const auto &ssid : sniffer->ignored_network_names())
    *reply->add_names() = ssid;
  return grpc::Status::OK;
};

grpc::Status Service::SaveDecryptedTraffic(grpc::ServerContext *context,
                                           const proto::NetworkName *request,
                                           proto::Empty *reply) {
  if (request->sniffer_id() >= sniffers.size())
    return grpc::Status(grpc::StatusCode::NOT_FOUND, "No sniffer with this id");
  Sniffer *sniffer = sniffers[request->sniffer_id()].get();

  auto ap = sniffer->get_network(request->ssid());
  if (!ap.has_value())
    return grpc::Status(grpc::StatusCode::NOT_FOUND,
                        "No network with this ssid");

  bool saved = ap.value()->save_decrypted_traffic(save_path);
  if (!saved)
    return grpc::Status(grpc::StatusCode::INTERNAL,
                        "Cannot save decrypted traffic");

  return grpc::Status::OK;
};

grpc::Status Service::GetAvailableRecordings(grpc::ServerContext *context,
                                             const proto::Empty *request,
                                             proto::RecordingsList *reply) {
  // TODO
  for (const auto &recording : sniffers[0]->available_recordings(save_path)) {
    proto::File *file = reply->add_files();
    file->set_name(recording);
  }

  return grpc::Status::OK;
};

grpc::Status Service::LoadRecording(grpc::ServerContext *context,
                                    const proto::File *request,
                                    grpc::ServerWriter<proto::Packet> *writer) {
  // TODO
  if (!sniffers[0]->recording_exists(save_path, request->name()))
    return grpc::Status(grpc::StatusCode::NOT_FOUND,
                        "No recording with that name");

  auto stream = sniffers[0]->get_recording_stream(save_path, request->name());
  if (!stream.has_value())
    return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to get the stream");

  auto channel = std::move(stream.value());
  logger->debug("Got stream with {} packets", channel->len());
  int iter_count = 0;

  while (iter_count != channel->len()) {
    iter_count++;
    std::optional<std::unique_ptr<Tins::EthernetII>> pkt_opt =
        channel->receive();
    if (!pkt_opt.has_value())
      return grpc::Status::OK;

    std::unique_ptr<Tins::EthernetII> pkt = std::move(pkt_opt.value());
    auto ip = pkt->find_pdu<Tins::IP>();
    if (!ip)
      continue;

    auto tcp = pkt->find_pdu<Tins::TCP>();
    auto udp = pkt->find_pdu<Tins::UDP>();
    if (!tcp && !udp)
      continue;

    auto from = std::make_unique<proto::User>();
    from->set_ipv4address(ip->src_addr().to_string());
    from->set_macaddress(pkt->src_addr().to_string());
    from->set_port(tcp ? tcp->sport() : udp->sport());

    auto to = std::make_unique<proto::User>();
    to->set_ipv4address(ip->dst_addr().to_string());
    to->set_macaddress(pkt->dst_addr().to_string());
    to->set_port(tcp ? tcp->dport() : udp->dport());

    auto packet = std::make_unique<proto::Packet>();
    packet->set_protocol(tcp ? "TCP" : "UDP");
    packet->set_allocated_from(from.release());
    packet->set_allocated_to(to.release());

    std::vector<uint8_t> data =
        tcp ? tcp->clone()->serialize() : udp->clone()->serialize();
    std::string to_send(data.begin(), data.end());
    packet->set_data(to_send);

    writer->Write(*packet);
  }

  return grpc::Status::OK;
};

grpc::Status Service::SetMayhemMode(grpc::ServerContext *context,
                                    const proto::NewMayhemState *request,
                                    proto::Empty *reply) {
  if (request->sniffer_id() >= sniffers.size())
    return grpc::Status(grpc::StatusCode::NOT_FOUND, "No sniffer with this id");
  Sniffer *sniffer = sniffers[request->sniffer_id()].get();

  if (!sniffer->iface().has_value())
    return grpc::Status(grpc::StatusCode::UNAVAILABLE,
                        "Not listening on a live interface");
#ifndef MAYHEM
  logger->error("Tried to access rpc SetMayhemMode when mayhem is disabled!");
  return grpc::Status(grpc::StatusCode::UNAVAILABLE, "Mayhem support disabled");
#else
  logger->trace("Set mayhem hit");

  bool turn_on = request->state();
  if (turn_on) {
    if (mayhem_on.load()) {
      logger->warn("Already in mayhem");
      return grpc::Status(grpc::StatusCode::ALREADY_EXISTS,
                          "We are already in Mayhem");
    }
    mayhem_on.store(true);
    sniffer->start_mayhem();
    return grpc::Status::OK;
  }

  if (!mayhem_on.load())
    return grpc::Status(grpc::StatusCode::ALREADY_EXISTS,
                        "We are already out of Mayhem");

  mayhem_on.store(false);
  sniffer->stop_mayhem();
  return grpc::Status::OK;
#endif
};

grpc::Status Service::GetLED(grpc::ServerContext *context,
                             const proto::SnifferID *request,
                             grpc::ServerWriter<proto::LEDState> *writer) {
  if (request->id() >= sniffers.size())
    return grpc::Status(grpc::StatusCode::NOT_FOUND, "No sniffer with this id");
  Sniffer *sniffer = sniffers[request->id()].get();

  if (!sniffer->iface().has_value())
    return grpc::Status(grpc::StatusCode::UNAVAILABLE,
                        "Not listening on a live interface");
#ifndef MAYHEM
  logger->error("Tried to access rpc GetLED when mayhem is disabled!");
  return grpc::Status(grpc::StatusCode::UNAVAILABLE, "Mayhem support disabled");
#else
  logger->trace("Get led hit");
  if (led_on.load()) {
    logger->warn("Already streaming LEDs");
    return grpc::Status(grpc::StatusCode::ALREADY_EXISTS,
                        "We are already streaming LED's");
  }
  led_on.store(true);

  std::mutex led_lock;
  std::queue<LEDColor> led_queue;
  sniffer->start_led(&led_lock, &led_queue);

  int red_on = false;
  int yellow_on = false;
  int green_on = false;

  while (led_on.load() && !context->IsCancelled()) {
    led_lock.lock();
    if (led_queue.empty()) {
      led_lock.unlock();
      continue;
    }

    LEDColor color = led_queue.front();
    led_queue.pop();
    led_lock.unlock();

    proto::LEDState nls;
    switch (color) {
    case RED_LED:
      red_on = !red_on;
      nls.set_color(proto::RED);
      nls.set_state(red_on);
      break;
    case YELLOW_LED:
      yellow_on = !yellow_on;
      nls.set_color(proto::YELLOW);
      nls.set_state(yellow_on);
      break;
    case GREEN_LED:
      green_on = !green_on;
      nls.set_color(proto::GREEN);
      nls.set_state(green_on);
      break;
    }

    writer->Write(nls);
  }

  led_on.store(false);
  sniffer->stop_led();
  logger->trace("LED streaming stopped");
  return grpc::Status::OK;
#endif
};

} // namespace yarilo

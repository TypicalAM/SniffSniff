#include "service.h"
#include "access_point.h"
#include "packets.pb.h"
#include "sniffer.h"
#include <chrono>
#include <cstdint>
#include <grpcpp/support/status.h>
#include <memory>
#include <optional>
#include <thread>
#include <tins/ip.h>
#include <tins/network_interface.h>
#include <tins/packet_sender.h>
#include <tins/pdu.h>
#include <tins/tcp.h>
#include <tins/udp.h>
#include <vector>

Service::Service(Tins::BaseSniffer *sniffer, Tins::NetworkInterface iface) {
  filemode = false;
  this->iface = iface;
  this->sniffinson = new Sniffer(sniffer, iface);
  this->sniffinson->run();
}

Service::Service(Tins::BaseSniffer *sniffer) {
  sniffinson = new Sniffer(sniffer);
  sniffinson->run();
}

#ifdef MAYHEM
void Service::run_fifo() {
  std::thread([this]() { sniffinson->readloop_topgun(); }).detach();
}

bool Service::open_led_fifo(const std::string &filename) {
  return sniffinson->open_led_fifo(filename);
};

bool Service::open_topgun_fifo(const std::string &filename) {
  return sniffinson->open_topgun_fifo(filename);
}
#endif

grpc::Status Service::GetAllAccessPoints(grpc::ServerContext *context,
                                         const Empty *request,
                                         NetworkList *reply) {
  std::set<SSID> names = sniffinson->get_networks();
  for (const auto &name : names) {
    auto new_name = reply->add_names();
    *new_name = std::string(name);
  }

  return grpc::Status::OK;
};

grpc::Status Service::GetAccessPoint(grpc::ServerContext *context,
                                     const NetworkName *request,
                                     NetworkInfo *reply) {
  std::optional<AccessPoint *> ap_option = sniffinson->get_ap(request->ssid());
  if (!ap_option.has_value())
    return grpc::Status::CANCELLED;

  AccessPoint *ap = ap_option.value();
  reply->set_name(ap->get_ssid());
  reply->set_bssid(ap->get_bssid().to_string());
  reply->set_channel(ap->get_wifi_channel());
  reply->set_encrypted_packet_count(ap->raw_packet_count());
  reply->set_decrypted_packet_count(ap->decrypted_packet_count());

  std::vector<Client *> clients = ap->get_clients();
  for (const auto &client : clients) {
    ClientInfo *info = reply->add_clients();
    info->set_addr(client->get_addr().to_string());
    info->set_is_decrypted(client->is_decrypted());
    info->set_handshake_num(client->get_key_num());
    info->set_can_decrypt(client->can_decrypt());
  }

  return grpc::Status::OK;
}

grpc::Status Service::FocusNetwork(grpc::ServerContext *context,
                                   const NetworkName *request, Empty *reply) {
  bool success = sniffinson->focus_network(request->ssid());
  if (!success)
    return grpc::Status::CANCELLED;
  return grpc::Status::OK;
}

grpc::Status Service::GetFocusState(grpc::ServerContext *context,
                                    const Empty *request, FocusState *reply) {
  auto ap = sniffinson->get_focused_network();

  reply->set_focused(ap.has_value());
  if (ap.has_value()) {
    auto name = new NetworkName();
    name->set_ssid(ap.value()->get_ssid());
    reply->set_allocated_name(name);
  };

  return grpc::Status::OK;
}

grpc::Status Service::StopFocus(grpc::ServerContext *context,
                                const Empty *request, Empty *reply) {
  sniffinson->stop_focus();
  return grpc::Status::OK;
}

grpc::Status Service::ProvidePassword(grpc::ServerContext *context,
                                      const DecryptRequest *request,
                                      DecryptResponse *reply) {
  std::optional<AccessPoint *> ap = sniffinson->get_ap(request->ssid());
  if (!ap.has_value()) {
    reply->set_state(DecryptState::WRONG_NETWORK_NAME);
    return grpc::Status::OK;
  }

  if (ap.value()->is_psk_correct()) {
    reply->set_state(DecryptState::ALREADY_DECRYPTED);
    return grpc::Status::OK;
  }

  bool success = ap.value()->add_passwd(request->passwd());
  if (!success) {
    reply->set_state(DecryptState::WRONG_OR_NO_DATA);
    return grpc::Status::OK;
  }

  reply->set_state(DecryptState::SUCCESS);
  return grpc::Status::OK;
};

grpc::Status Service::GetDecryptedPackets(grpc::ServerContext *context,
                                          const NetworkName *request,
                                          grpc::ServerWriter<Packet> *writer) {
  std::optional<AccessPoint *> ap = sniffinson->get_ap(request->ssid());
  if (!ap.has_value())
    return grpc::Status::CANCELLED;

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

  while (!channel->is_closed()) {
    std::optional<std::unique_ptr<Tins::EthernetII>> pkt_opt =
        channel->receive();
    if (!pkt_opt.has_value()) {
      std::cout << "Stream has been cancelled, cool" << std::endl;
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

    auto from = std::make_unique<User>();
    from->set_ipv4address(ip->src_addr().to_string());
    from->set_macaddress(pkt->src_addr().to_string());
    from->set_port(tcp ? tcp->sport() : udp->sport());

    auto to = std::make_unique<User>();
    to->set_ipv4address(ip->dst_addr().to_string());
    to->set_macaddress(pkt->dst_addr().to_string());
    to->set_port(tcp ? tcp->dport() : udp->dport());

    auto packet = std::make_unique<Packet>();
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
                                    const DeauthRequest *request,
                                    Empty *reply) {
  std::optional<AccessPoint *> ap =
      sniffinson->get_ap(request->network().ssid());
  if (!ap.has_value()) {
    return grpc::Status::CANCELLED;
  }

  bool success = ap.value()->send_deauth(&iface, request->user_addr());
  if (!success)
    return grpc::Status::CANCELLED;
  return grpc::Status::OK;
};

grpc::Status Service::IgnoreNetwork(grpc::ServerContext *context,
                                    const NetworkName *request, Empty *reply) {
  sniffinson->add_ignored_network(request->ssid());
  return grpc::Status::OK;
};

grpc::Status Service::GetIgnoredNetworks(grpc::ServerContext *context,
                                         const Empty *request,
                                         NetworkList *reply) {
  for (const auto &ssid : sniffinson->get_ignored_networks())
    *reply->add_names() = ssid;
  return grpc::Status::OK;
};

grpc::Status Service::SaveDecryptedTraffic(grpc::ServerContext *context,
                                           const NetworkName *request,
                                           Empty *response) {
  const std::string dir_path = "/opt/sniff";
  auto ap = sniffinson->get_ap(request->ssid());
  if (!ap.has_value())
    return grpc::Status::CANCELLED;

  bool saved = ap.value()->save_decrypted_traffic(dir_path);
  if (!saved)
    return grpc::Status::CANCELLED;

  return grpc::Status::OK;
};

grpc::Status Service::GetAvailableRecordings(grpc::ServerContext *context,
                                             const Empty *request,
                                             RecordingsList *response) {
  for (const auto &recording : sniffinson->get_recordings()) {
    File *file = response->add_files();
    file->set_name(recording);
  }

  return grpc::Status::OK;
};

grpc::Status Service::LoadRecording(grpc::ServerContext *context,
                                    const File *request,
                                    grpc::ServerWriter<Packet> *writer) {
  auto [channel, count] = sniffinson->get_recording_stream(request->name());
  std::cout << "Got stream with " << count << " packets" << std::endl;
  int iter_count = 0;

  while (iter_count != count) {
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

    auto from = std::make_unique<User>();
    from->set_ipv4address(ip->src_addr().to_string());
    from->set_macaddress(pkt->src_addr().to_string());
    from->set_port(tcp ? tcp->sport() : udp->sport());

    auto to = std::make_unique<User>();
    to->set_ipv4address(ip->dst_addr().to_string());
    to->set_macaddress(pkt->dst_addr().to_string());
    to->set_port(tcp ? tcp->dport() : udp->dport());

    auto packet = std::make_unique<Packet>();
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

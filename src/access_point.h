#ifndef SNIFF_AP
#define SNIFF_AP

#include "client.h"
#include <optional>
#include <tins/dot11.h>
#include <tins/eapol.h>
#include <tins/ethernetII.h>
#include <tins/hw_address.h>
#include <tins/pdu.h>
#include <unordered_map>

typedef std::unordered_map<Tins::HWAddress<6>, Client *> client_map;

class AccessPoint {
public:
  /**
   * A constructor which creates the access point based on the Dot11Beacon
   * packet
   * @param[in] beacon A reference to the Dot11Beacon packet
   */
  AccessPoint(const Tins::Dot11Beacon &beacon);

  /**
   * A method for knowing if this packet belongs to the network
   * @param[in] dot11 A dot11data packet that you wish to inspect
   * @return True if the packet belongs to this AP
   */
  bool in_network(const Tins::Dot11Data &dot11);

  /**
   * A method for handling incoming data packets inside this network, if you
   * don't know if the packet belongs to this network use @ref in_network
   * @param[in] beacon A reference to the packet
   */
  bool handle_pkt(Tins::PDU &pkt);

  /**
   * A method for adding the wifi password key. Decryption of packets
   * requires a 4-way handshake. If the password is present, the user packets
   * will be decrypted using this key.
   * @param[in] psk network key
   */
  void add_passwd(const std::string &psk);

  /**
   * Get all the clients
   * @return A set of unique clients
   */
  std::vector<Client *> get_clients();

  /**
   * Get a specific client based on the NIC hwaddr
   * @param[in] addr The MAC address of the device
   * @return Optionally return the client if they exist
   */
  std::optional<Client *> get_client(Tins::HWAddress<6> addr);

  /**
   * Get this networks SSID
   * @return the ssid of the network
   */
  SSID get_ssid();

  /**
   * Get the converted data channel for this network
   * TODO: Add timing info
   */
  Channel<Tins::EthernetII *> *get_channel();

private:
  SSID ssid;
  Tins::HWAddress<6> bssid;
  client_map clients;
  std::string psk;
  int channel;
  data_queue raw_data;
  Tins::Crypto::WPA2Decrypter decrypter;
  Channel<Tins::EthernetII *> *converted_channel;

  /**
   * Get a specific client (sender or receiver) based on the dot11 address data
   * inside a network
   * @param[in] dot11 The Dot11Data packet to analyze
   * @return The client hardware address
   */
  Tins::HWAddress<6> determine_client(const Tins::Dot11Data &dot11);

  /**
   * Create an ethernet packet based on the decrypted dot11 packet
   * @param[in] dot11 The Dot11Data packet to analyze
   * @return The converted ethernet packet
   */
  static Tins::EthernetII make_eth_packet(const Tins::Dot11Data &dot11);
};

#endif // SNIFF_AP

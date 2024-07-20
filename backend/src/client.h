#ifndef SNIFF_CLIENT
#define SNIFF_CLIENT

#include "decrypter.h"
#include <memory>
#include <optional>
#include <queue>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <tins/crypto.h>
#include <tins/dot11.h>
#include <tins/eapol.h>
#include <tins/ethernetII.h>
#include <tins/hw_address.h>
#include <tins/snap.h>

namespace yarilo {

typedef std::queue<Tins::PDU *> data_queue;

class Client {
public:
  typedef std::pair<Tins::Crypto::WPA2Decrypter::keys_map,
                    WPA2Decrypter::gtk_type>
      decryption_keys;

  /**
   * A constructor which creates a client for a specific network
   * @param[in] bssid hwaddr of the network
   * @param[in] ssid name of the network
   * @param[in] addr hwaddr of the client
   */
  Client(const Tins::HWAddress<6> &bssid, const SSID &ssid,
         const Tins::HWAddress<6> &addr);

  /**
   * Add an EAPOL handshake packet for this client
   * @param[in] dot11 802.11 data for the RSNEAPOL packet
   */
  void add_handshake(Tins::PDU &pdu);

  /**
   * Chcek whether the client can be supplied a password and perform packet
   * decryption
   * @return True if the client has been supplied enough valid handshakes
   */
  bool can_decrypt();

  /**
   * Check whether the client has already generated correct decryption keys
   * @return True if the client already generated decryption keys
   */
  bool is_decrypted();

  /**
   * Try to decrypt the traffic using the pre-shared key, requires being
   * supplied 4 valid EAPOL handshakes beforehand
   * @param[in] psk PSK of the network
   * @return Handshake key map if successful, nullopt otherwise
   */
  std::optional<decryption_keys> try_decrypt(const std::string &psk);

  /**
   * Get the hardware address of the client
   * @return Client hardware address
   */
  Tins::HWAddress<6> get_addr();

  /**
   * Get the count of the valid EAPOL handshakes
   * @return Count of available handshakes
   */
  int get_key_num();

private:
  std::shared_ptr<spdlog::logger> logger;
  Tins::HWAddress<6> bssid;
  SSID ssid;
  Tins::HWAddress<6> addr;
  data_queue auth_data;
  bool decrypted = false;
};
} // namespace yarilo

#endif // SNIFF_CLIENT

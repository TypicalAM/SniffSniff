#ifndef SNIFF_NET_CARD_MANAGER
#define SNIFF_NET_CARD_MANAGER

#include "netlink/attr.h"
#include "netlink/handlers.h"
#include "netlink/netlink.h"
#include <linux/nl80211.h>
#include <memory>
#include <set>
#include <spdlog/logger.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <string>

enum ChannelModes {
  NO_HT,     // Channel does not support High Throughput (HT) mode.
  HT20,      // Channel does support HT mode with a channel width of 20 MHz.
  HT40MINUS, // Channel does support HT mode with a channel width of 40 MHz,
             // where the secondary channel is below the primary channel.
  HT40PLUS,  // Channel does support HT mode with a channel width of 40 MHz,
             // where the secondary channel is above the primary channel.
  VHT80, // Channel does support Very High Throughput (VHT) mode with a channel
         // width of 80 MHz
  VHT80P80, // Channel does support Very High Throughput (VHT) mode with a
            // channel width of 80 MHz and also supports an additional 80 MHz
            // channel (80+80 MHz)
  VHT160    // Channel does support VHT mode with a channel width of 160 MHz
};

enum FcsState { FCS_ALL, FCS_VALID, FCS_INVALID };

struct phy_iface {
  std::string ifname;             // Interface name
  std::set<uint32_t> frequencies; // Supported frequencies
  bool can_set_freq;  // Can it set frequencies?, we cannot jump if it doesn't
  bool can_check_fcs; // Can it check the Frame Check Sequence
  int channel_opts;   // All available channel options, see ChannelModes
  int can_monitor;    // Supports monitor mode

  bool operator<(const phy_iface &other) const { return ifname < other.ifname; }
};

struct iface_state {
  int freq;               // Current working frequency
  ChannelModes chan_type; // Current channel type
  int center_freq1;       // Primary center frequency
  int center_freq2;   // Secondary center frequency in cases of bonded channels.
  FcsState fcs_state; // Validation state of the Frame Check Sequence
};

struct iface_state_fetcher {
  iface_state *pub; // We populate this pointer before the callbacks so that it
                    // doesn't get messed with by the nlmsg api
  int type;         // (virtual) interface type, see nl80211_iftype
  int phy_idx; // Index of the phy that this virtual interface is attached to
};

class NetlinkCallback {
public:
  NetlinkCallback(nl_sock *sock) { this->sock = sock; };

  void attach(nl_recvmsg_msg_cb_t func, void *arg);
  int wait(); // wait for the result of the callback, negative numbers are
              // errors

private:
  nl_sock *sock;
  nl_cb *callback;
  int result;
};

class NetCardManager {
public:
  NetCardManager() { this->log = spdlog::stdout_color_mt("net"); }

  bool connect();
  void disconnect();

  std::set<std::string> network_interfaces();
  std::set<phy_iface> phy_interfaces();
  std::optional<iface_state> interface_details(std::string ifname);

  void test();

  ~NetCardManager() { nl_socket_free(this->sock); }

private:
  static int phy_interfaces_callback(nl_msg *msg, void *arg);
  static int interface_details_callback(nl_msg *msg, void *arg);

  std::shared_ptr<spdlog::logger> log;
  nl_sock *sock;
  int sock_id;
};

#endif // SNIFF_NET_CARD_MANAGER

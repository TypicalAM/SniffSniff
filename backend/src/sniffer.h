#ifndef SNIFF_SNIFFER
#define SNIFF_SNIFFER

#include "access_point.h"
#include <atomic>
#include <set>
#include <string>
#include <tins/crypto.h>
#include <tins/network_interface.h>
#include <tins/pdu.h>
#include <tins/sniffer.h>
#include <unordered_map>

#ifdef MAYHEM
// Commands to send/receive from pipes
enum MayhemCommands {
  GREEN_OFF = 'a',
  GREEN_ON = 'b',
  YELLOW_OFF = 'c',
  YELLOW_ON = 'd',
  RED_OFF = 'e',
  RED_ON = 'f',
  START_MAYHEM = 'x',
  STOP_MAYHEM = 'y'
};
#endif

enum ScanMode {
  FOCUSED, // We are focused on one network and following it's channel
  GENERAL  // We are hopping through the spectrum
};

class Sniffer {
public:
  Sniffer(Tins::BaseSniffer *sniffer);
  Sniffer(Tins::BaseSniffer *sniffer, Tins::NetworkInterface iface);

  void run();
  bool callback(Tins::PDU &pkt);
  std::set<SSID> get_networks();
  std::optional<AccessPoint *> get_ap(SSID ssid);
  // Ignore network and delete any ap with this name from the list
  void add_ignored_network(SSID ssid);
  std::set<SSID> get_ignored_networks();
  void end_capture();
  bool focus_network(SSID ssid); // focus network
  std::optional<AccessPoint *> get_focused_network();
  void stop_focus();
  void hopping_thread(); // to hop channels
  std::vector<std::string> get_recordings();
  std::pair<std::unique_ptr<PacketChannel>, int>
  get_recording_stream(std::string filename);

#ifdef MAYHEM
  bool open_led_fifo(const std::string &filename);
  bool open_topgun_fifo(const std::string &filename);
  void readloop_topgun();
#endif

private:
  std::atomic<ScanMode> scan_mode = GENERAL;

  SSID focused_network = "";
  bool filemode = true;
  int count = 0;
  int current_channel = 1;
  Tins::Crypto::WPA2Decrypter *decrypter;
  std::unordered_map<SSID, AccessPoint *> aps;
  Tins::NetworkInterface send_iface;
  std::set<SSID> ignored_networks;
  Tins::BaseSniffer *sniffer;
  std::atomic<bool> end;

#ifdef MAYHEM
  int topgun_fd = -1;
  int led_fd = -1;
  int yellow_led = 0;
  int red_led = 0;

  void toggle_yellow_led();
  void toggle_red_red();
#endif

  // hop every so often
  void channel_hop(int timems);
};

#endif // SNIFF_SNIFFER

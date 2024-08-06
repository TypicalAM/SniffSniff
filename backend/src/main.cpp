#include "service.h"
#include <absl/flags/flag.h>
#include <absl/flags/internal/flag.h>
#include <absl/flags/parse.h>
#include <absl/flags/usage.h>
#include <absl/strings/str_format.h>
#include <csignal>
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/server_builder.h>
#include <memory>
#include <optional>
#include <tins/utils/routing_utils.h>

static std::unique_ptr<grpc::Server> server;
static std::unique_ptr<yarilo::Service> service;
static std::atomic<bool> shutdown_required = false;
static std::mutex shutdown_mtx;
static std::condition_variable shutdown_cv;

ABSL_FLAG(std::optional<std::string>, sniff_file, std::nullopt,
          "Filename to sniff on");
ABSL_FLAG(std::optional<std::string>, iface, std::nullopt,
          "Network interface card to use when listening or emitting packets. "
          "Mutually exclusive with the filename option.");
ABSL_FLAG(uint32_t, port, 9090, "Port to serve the grpc server on");
ABSL_FLAG(std::string, save_path, "/opt/yarlilo/saves",
          "Directory that yarilo will use to save decrypted traffic");
ABSL_FLAG(std::string, sniff_files_path, "/opt/yarlilo/sniff_files",
          "Directory which will be searched for sniff files (raw montior mode "
          "recordings)");
ABSL_FLAG(std::string, log_level, "info", "Log level (debug, info, trace)");

std::optional<std::shared_ptr<spdlog::logger>> init_logger() {
  std::string log_level = absl::GetFlag(FLAGS_log_level);
  auto log = spdlog::stdout_color_mt("base");
  if (log_level == "info") {
    spdlog::set_level(spdlog::level::info);
  } else if (log_level == "debug") {
    spdlog::set_level(spdlog::level::debug);
  } else if (log_level == "trace") {
    spdlog::set_level(spdlog::level::trace);
  } else {
    spdlog::set_level(spdlog::level::info);
    log->critical("Unexpected log level: {}", log_level);
    return std::nullopt;
  }
  return log;
}

std::optional<std::filesystem::path>
init_saves(std::shared_ptr<spdlog::logger> log) {
  std::filesystem::path saves = absl::GetFlag(FLAGS_save_path);
  if (!std::filesystem::exists(saves)) {
    log->info("Saves path not found, creating");
    try {
      std::filesystem::create_directories(saves);
    } catch (const std::runtime_error &e) {
      log->critical("Cannot create saves directory at {}, {}", saves.string(),
                    e.what());
      return std::nullopt;
    }
  } else if (!std::filesystem::is_directory(saves)) {
    log->critical("Saves path {} is not a directory!", saves.string());
    return std::nullopt;
  }

  return saves;
}

std::optional<std::filesystem::path>
init_sniff_files(std::shared_ptr<spdlog::logger> log) {
  std::filesystem::path sniff_files = absl::GetFlag(FLAGS_sniff_files_path);
  if (!std::filesystem::exists(sniff_files)) {
    log->info("Sniff files path not found, creating");
    try {
      std::filesystem::create_directories(sniff_files);
    } catch (const std::runtime_error &e) {
      log->critical("Cannot create sniff files directory at {}, {}",
                    sniff_files.string(), e.what());
      return std::nullopt;
    }
  } else if (!std::filesystem::is_directory(sniff_files)) {
    log->critical("Sniff files path {} is not a directory!",
                  sniff_files.string());
    return std::nullopt;
  }

  return sniff_files;
}

bool init_first_sniffer(std::shared_ptr<spdlog::logger> log) {
  std::optional<std::string> net_iface_name = absl::GetFlag(FLAGS_iface);
  std::optional<std::string> filename = absl::GetFlag(FLAGS_sniff_file);

  if (net_iface_name.has_value() && filename.has_value()) {
    log->error("Incorrect usage, both filename and network card interface was "
               "specified");
    return false;
  }

  if (!net_iface_name.has_value() && !filename.has_value()) {
    log->info("No sniffers initialized");
    return true;
  }

  if (filename.has_value())
    return service->add_file_sniffer(filename.value());
  return service->add_iface_sniffer(net_iface_name.value());
}

void handle_signal(int sig) {
  const std::lock_guard lock(shutdown_mtx);
  shutdown_required.store(true);
  shutdown_cv.notify_one();
}

void shutdown_check() {
  std::unique_lock<std::mutex> lock(shutdown_mtx);
  shutdown_cv.wait(lock, []() { return shutdown_required.load(); });
  server->Shutdown();
  service->shutdown();
}

int main(int argc, char *argv[]) {
  absl::SetProgramUsageMessage(
      absl::StrCat("packet sniffer designed "
                   "for capturing and decrypting encrypted wireless "
                   "network traffic\n\n",
                   "Sample usage:\n  ", argv[0],
                   " --iface=wlp5s0f4u2 \\\n    "
                   "--save_path=/opt/yarilo/saves \\\n    "
                   "--log_level=trace"));
  absl::ParseCommandLine(argc, argv);

  std::optional<std::shared_ptr<spdlog::logger>> log_opt = init_logger();
  if (!log_opt.has_value())
    return 1;
  std::shared_ptr<spdlog::logger> logger = log_opt.value();

  logger->info("Starting Yarilo");

#ifdef MAYHEM
  logger->info("Mayhem enabled, use the appropriate endpoints to toggle it");
#endif

  std::optional<std::filesystem::path> saves_path = init_saves(logger);
  if (!saves_path.has_value())
    return 1;

  std::optional<std::filesystem::path> sniff_files_path =
      init_sniff_files(logger);
  if (!sniff_files_path.has_value())
    return 1;

  service = std::make_unique<yarilo::Service>(saves_path.value(),
                                              sniff_files_path.value());
  if (!init_first_sniffer(logger))
    return 1;

  std::string server_address =
      absl::StrFormat("0.0.0.0:%d", absl::GetFlag(FLAGS_port));
  grpc::EnableDefaultHealthCheckService(true);
  grpc::reflection::InitProtoReflectionServerBuilderPlugin();
  grpc::ServerBuilder builder;
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  builder.RegisterService(service.get());
  logger->info("Serving on port {}", absl::GetFlag(FLAGS_port));

  std::signal(SIGINT, handle_signal);
  std::signal(SIGQUIT, handle_signal);
  std::signal(SIGTERM, handle_signal);
  std::thread t(shutdown_check);
  server = builder.BuildAndStart();
  t.join();
  return 0;
};

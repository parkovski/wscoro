#define CATCH_CONFIG_RUNNER
#include <catch2/catch_all.hpp>

#include <spdlog/sinks/stdout_color_sinks.h>

std::shared_ptr<spdlog::logger> g_logger;
struct DestroyLogger {
  ~DestroyLogger() {
    g_logger.reset();
  }
};

int main(int argc, char *argv[]) {
  g_logger = spdlog::stdout_color_mt("wscoro");
  DestroyLogger destroy_logger;

  g_logger->info("Starting Catch session.");
  int result = Catch::Session().run(argc, argv);

  return result;
}

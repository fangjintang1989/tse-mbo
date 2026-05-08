#include "tse_mbo/app.hpp"

int main(int argc, char* argv[]) {
  const auto parsed = tse_mbo::parse_args(argc, argv);
  if (parsed.help_requested) {
    return 0;
  }
  if (!parsed.valid) {
    return argc > 1 ? 1 : 0;
  }
  return tse_mbo::run(parsed.config);
}

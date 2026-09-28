#pragma once

#include "demi/runtime/database/DBDriver.h"

namespace demi::runtime {

class SQLiteDriver final : public DBDriver {
public:
  std::unique_ptr<DBConnection>
  connect(const std::string &path, const nlohmann::json &options,
          const std::atomic<bool> &cancelled) override;
};

} // namespace demi::runtime

#include "QingLongClaw/cron_service.h"

#include <algorithm>
#include <chrono>
#include <random>

#include "croncpp.h"
#include "json.hpp"
#include "QingLongClaw/util.h"

namespace QingLongClaw {

namespace {

nlohmann::json schedule_to_json(const CronSchedule& schedule) {
  nlohmann::json item;
  item["kind"] = schedule.kind;
  if (schedule.at_ms.has_value()) {
    item["atMs"] = schedule.at_ms.value();
  }
  if (schedule.every_ms.has_value()) {
    item["everyMs"] = schedule.every_ms.value();
  }
  if (!schedule.expr.empty()) {
    item["expr"] = schedule.expr;
  }
  if (!schedule.tz.empty()) {
    item["tz"] = schedule.tz;
  }
  return item;
}

CronSchedule schedule_from_json(const nlohmann::json& item) {
  CronSchedule schedule;
  schedule.kind = item.value("kind", "every");
  if (item.contains("atMs") && item["atMs"].is_number_integer()) {
    schedule.at_ms = item["atMs"].get<std::int64_t>();
  }
  if (item.contains("everyMs") && item["everyMs"].is_number_integer()) {
    schedule.every_ms = item["everyMs"].get<std::int64_t>();
  }
  schedule.expr = item.value("expr", "");
  schedule.tz = item.value("tz", "");
  return schedule;
}

nlohmann::json payload_to_json(const CronPayload& payload) {
  return nlohmann::json{
      {"kind", payload.kind},
      {"message", payload.message},
      {"command", payload.command},
      {"deliver", payload.deliver},
      {"channel", payload.channel},
      {"to", payload.to},
  };
}

CronPayload payload_from_json(const nlohmann::json& item) {
  CronPayload payload;
  payload.kind = item.value("kind", "agent_turn");
  payload.message = item.value("message", "");
  payload.command = item.value("command", "");
  payload.deliver = item.value("deliver", false);
  payload.channel = item.value("channel", "");
  payload.to = item.value("to", "");
  return payload;
}

nlohmann::json state_to_json(const CronJobState& state) {
  nlohmann::json item;
  if (state.next_run_at_ms.has_value()) {
    item["nextRunAtMs"] = state.next_run_at_ms.value();
  }
  if (state.last_run_at_ms.has_value()) {
    item["lastRunAtMs"] = state.last_run_at_ms.value();
  }
  if (!state.last_status.empty()) {
    item["lastStatus"] = state.last_status;
  }
  if (!state.last_error.empty()) {
    item["lastError"] = state.last_error;
  }
  return item;
}

CronJobState state_from_json(const nlohmann::json& item) {
  CronJobState state;
  if (item.contains("nextRunAtMs") && item["nextRunAtMs"].is_number_integer()) {
    state.next_run_at_ms = item["nextRunAtMs"].get<std::int64_t>();
  }
  if (item.contains("lastRunAtMs") && item["lastRunAtMs"].is_number_integer()) {
    state.last_run_at_ms = item["lastRunAtMs"].get<std::int64_t>();
  }
  state.last_status = item.value("lastStatus", "");
  state.last_error = item.value("lastError", "");
  return state;
}

nlohmann::json job_to_json(const CronJob& job) {
  return nlohmann::json{
      {"id", job.id},
      {"name", job.name},
      {"enabled", job.enabled},
      {"schedule", schedule_to_json(job.schedule)},
      {"payload", payload_to_json(job.payload)},
      {"state", state_to_json(job.state)},
      {"createdAtMs", job.created_at_ms},
      {"updatedAtMs", job.updated_at_ms},
      {"deleteAfterRun", job.delete_after_run},
  };
}

CronJob job_from_json(const nlohmann::json& item) {
  CronJob job;
  job.id = item.value("id", "");
  job.name = item.value("name", "");
  job.enabled = item.value("enabled", true);
  if (item.contains("schedule") && item["schedule"].is_object()) {
    job.schedule = schedule_from_json(item["schedule"]);
  }
  if (item.contains("payload") && item["payload"].is_object()) {
    job.payload = payload_from_json(item["payload"]);
  }
  if (item.contains("state") && item["state"].is_object()) {
    job.state = state_from_json(item["state"]);
  }
  job.created_at_ms = item.value("createdAtMs", 0LL);
  job.updated_at_ms = item.value("updatedAtMs", 0LL);
  job.delete_after_run = item.value("deleteAfterRun", false);
  return job;
}

}  // namespace

CronService::CronService(std::filesystem::path store_path, JobHandler handler)
    : store_path_(std::move(store_path)), on_job_(std::move(handler)) {
  load();
}

CronService::~CronService() { stop(); }

bool CronService::load() {
  std::lock_guard<std::mutex> lock(mutex_);
  jobs_.clear();
  const std::string data = read_text_file(store_path_);
  if (data.empty()) {
    return true;
  }
  try {
    const auto root = nlohmann::json::parse(data);
    if (!root.is_object() || !root.contains("jobs") || !root["jobs"].is_array()) {
      return false;
    }
    for (const auto& item : root["jobs"]) {
      CronJob job = job_from_json(item);
      if (!job.id.empty()) {
        jobs_.push_back(std::move(job));
      }
    }
    return true;
  } catch (...) {
    return false;
  }
}

bool CronService::save() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return save_unlocked();
}

bool CronService::save_unlocked() const {
  nlohmann::json root;
  root["version"] = 1;
  root["jobs"] = nlohmann::json::array();
  for (const auto& job : jobs_) {
    root["jobs"].push_back(job_to_json(job));
  }
  return write_text_file(store_path_, root.dump(2));
}

std::vector<CronJob> CronService::list_jobs(const bool include_disabled) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (include_disabled) {
    return jobs_;
  }
  std::vector<CronJob> filtered;
  for (const auto& job : jobs_) {
    if (job.enabled) {
      filtered.push_back(job);
    }
  }
  return filtered;
}

std::optional<CronJob> CronService::add_job(const std::string& name,
                                            const CronSchedule& schedule,
                                            const std::string& message,
                                            const bool deliver,
                                            const std::string& channel,
                                            const std::string& to) {
  std::lock_guard<std::mutex> lock(mutex_);
  CronJob job;
  job.id = generate_id();
  job.name = name;
  job.enabled = true;
  job.schedule = schedule;
  job.payload.kind = "agent_turn";
  job.payload.message = message;
  job.payload.deliver = deliver;
  job.payload.channel = channel;
  job.payload.to = to;
  job.created_at_ms = unix_ms_now();
  job.updated_at_ms = job.created_at_ms;
  job.delete_after_run = (schedule.kind == "at");
  job.state.next_run_at_ms = compute_next_run(schedule, job.created_at_ms);

  jobs_.push_back(job);
  save_unlocked();
  return job;
}

bool CronService::remove_job(const std::string& job_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto old_size = jobs_.size();
  jobs_.erase(std::remove_if(jobs_.begin(), jobs_.end(),
                             [&](const CronJob& job) {
                               return job.id == job_id;
                             }),
              jobs_.end());
  const bool removed = jobs_.size() != old_size;
  if (removed) {
    save_unlocked();
  }
  return removed;
}

std::optional<CronJob> CronService::enable_job(const std::string& job_id, const bool enabled) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto& job : jobs_) {
    if (job.id != job_id) {
      continue;
    }
    job.enabled = enabled;
    job.updated_at_ms = unix_ms_now();
    if (enabled) {
      job.state.next_run_at_ms = compute_next_run(job.schedule, unix_ms_now());
    } else {
      job.state.next_run_at_ms = std::nullopt;
    }
    save_unlocked();
    return job;
  }
  return std::nullopt;
}

void CronService::set_on_job(JobHandler handler) {
  std::lock_guard<std::mutex> lock(mutex_);
  on_job_ = std::move(handler);
}

bool CronService::start() {
  if (running_.load()) {
    return true;
  }
  recompute_next_runs();
  save();
  running_.store(true);
  thread_ = std::thread([this]() { run_loop(); });
  return true;
}

void CronService::stop() {
  running_.store(false);
  if (thread_.joinable()) {
    thread_.join();
  }
}

std::optional<std::int64_t> CronService::compute_next_run(const CronSchedule& schedule,
                                                          const std::int64_t now_ms) const {
  if (schedule.kind == "at") {
    if (schedule.at_ms.has_value() && schedule.at_ms.value() > now_ms) {
      return schedule.at_ms;
    }
    return std::nullopt;
  }
  if (schedule.kind == "every") {
    if (!schedule.every_ms.has_value() || schedule.every_ms.value() <= 0) {
      return std::nullopt;
    }
    return now_ms + schedule.every_ms.value();
  }
  if (schedule.kind == "cron") {
    if (schedule.expr.empty()) {
      return std::nullopt;
    }
    try {
      const auto cron_expr = cron::make_cron(schedule.expr);
      const auto now_tp = std::chrono::system_clock::time_point(std::chrono::milliseconds(now_ms));
      const auto next_tp = cron::cron_next(cron_expr, now_tp);
      const auto next_ms = std::chrono::duration_cast<std::chrono::milliseconds>(next_tp.time_since_epoch()).count();
      return next_ms;
    } catch (...) {
      return std::nullopt;
    }
  }
  return std::nullopt;
}

void CronService::recompute_next_runs() {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto now_ms = unix_ms_now();
  for (auto& job : jobs_) {
    if (job.enabled) {
      job.state.next_run_at_ms = compute_next_run(job.schedule, now_ms);
    }
  }
}

void CronService::run_loop() {
  while (running_.load()) {
    std::this_thread::sleep_for(std::chrono::seconds(1));

    std::vector<std::string> due_job_ids;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      const auto now_ms = unix_ms_now();
      for (auto& job : jobs_) {
        if (!job.enabled || !job.state.next_run_at_ms.has_value()) {
          continue;
        }
        if (job.state.next_run_at_ms.value() <= now_ms) {
          due_job_ids.push_back(job.id);
          job.state.next_run_at_ms = std::nullopt;
        }
      }
    }

    if (!due_job_ids.empty()) {
      execute_due_jobs(due_job_ids);
    }
  }
}

void CronService::execute_due_jobs(std::vector<std::string> due_job_ids) {
  for (const auto& job_id : due_job_ids) {
    CronJob snapshot;
    bool exists = false;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      for (const auto& job : jobs_) {
        if (job.id == job_id) {
          snapshot = job;
          exists = true;
          break;
        }
      }
    }
    if (!exists) {
      continue;
    }

    std::string error;
    if (on_job_) {
      try {
        static_cast<void>(on_job_(snapshot));
      } catch (const std::exception& ex) {
        error = ex.what();
      } catch (...) {
        error = "unknown error";
      }
    }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      for (auto it = jobs_.begin(); it != jobs_.end(); ++it) {
        if (it->id != job_id) {
          continue;
        }

        it->state.last_run_at_ms = unix_ms_now();
        it->updated_at_ms = unix_ms_now();
        if (error.empty()) {
          it->state.last_status = "ok";
          it->state.last_error.clear();
        } else {
          it->state.last_status = "error";
          it->state.last_error = error;
        }

        if (it->schedule.kind == "at") {
          if (it->delete_after_run) {
            jobs_.erase(it);
          } else {
            it->enabled = false;
            it->state.next_run_at_ms = std::nullopt;
          }
        } else {
          it->state.next_run_at_ms = compute_next_run(it->schedule, unix_ms_now());
        }
        break;
      }
    }
    save();
  }
}

std::string CronService::generate_id() {
  static thread_local std::mt19937_64 rng(std::random_device{}());
  static constexpr char hex[] = "0123456789abcdef";
  std::string out(16, '0');
  for (char& c : out) {
    c = hex[rng() % 16];
  }
  return out;
}

}  // namespace QingLongClaw

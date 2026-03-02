#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace QingLongClaw {

struct CronSchedule {
  std::string kind = "every";
  std::optional<std::int64_t> at_ms;
  std::optional<std::int64_t> every_ms;
  std::string expr;
  std::string tz;
};

struct CronPayload {
  std::string kind = "agent_turn";
  std::string message;
  std::string command;
  bool deliver = false;
  std::string channel;
  std::string to;
};

struct CronJobState {
  std::optional<std::int64_t> next_run_at_ms;
  std::optional<std::int64_t> last_run_at_ms;
  std::string last_status;
  std::string last_error;
};

struct CronJob {
  std::string id;
  std::string name;
  bool enabled = true;
  CronSchedule schedule;
  CronPayload payload;
  CronJobState state;
  std::int64_t created_at_ms = 0;
  std::int64_t updated_at_ms = 0;
  bool delete_after_run = false;
};

class CronService {
 public:
  using JobHandler = std::function<std::string(const CronJob&)>;

  CronService(std::filesystem::path store_path, JobHandler handler = {});
  ~CronService();

  bool load();
  bool save() const;

  std::vector<CronJob> list_jobs(bool include_disabled) const;
  std::optional<CronJob> add_job(const std::string& name,
                                 const CronSchedule& schedule,
                                 const std::string& message,
                                 bool deliver,
                                 const std::string& channel,
                                 const std::string& to);
  bool remove_job(const std::string& job_id);
  std::optional<CronJob> enable_job(const std::string& job_id, bool enabled);

  void set_on_job(JobHandler handler);
  bool start();
  void stop();

 private:
  bool save_unlocked() const;
  std::optional<std::int64_t> compute_next_run(const CronSchedule& schedule, std::int64_t now_ms) const;
  void recompute_next_runs();
  void run_loop();
  void execute_due_jobs(std::vector<std::string> due_job_ids);
  static std::string generate_id();

  std::filesystem::path store_path_;
  JobHandler on_job_;

  mutable std::mutex mutex_;
  std::vector<CronJob> jobs_;
  std::atomic_bool running_{false};
  std::thread thread_;
};

}  // namespace QingLongClaw

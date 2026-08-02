#pragma once

#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#include "exchange/types.hpp"

namespace exchange {

// How hard the log works to survive a crash. PerEvent flushes on every append,
// which costs a syscall per order and per trade. Buffered lets the stream batch
// writes and is roughly an order of magnitude faster, at the cost of losing the
// tail of the log if the process dies. This is a real durability tradeoff, so
// it is stated explicitly rather than buried in append().
enum class Durability { PerEvent, Buffered };

class EventLog {
public:
    explicit EventLog(const std::string& path = "",
                      Durability durability = Durability::PerEvent);
    ~EventLog();

    void append(const Event& event);

    // Hot path: writes an event whose payload has already been rendered, so no
    // JSON DOM is ever built. Produces the same wire format as append().
    void append_preformatted(uint64_t sequence, uint64_t timestamp_ns, EventType type,
                             const std::string& payload_json);

    void flush();
    [[nodiscard]] std::vector<Event> read_all() const;
    [[nodiscard]] std::vector<Event> read_from(uint64_t start_sequence) const;

    [[nodiscard]] uint64_t current_sequence() const { return sequence_; }
    uint64_t next_sequence();

    // Lets callers skip building an event payload that would only be discarded.
    [[nodiscard]] bool enabled() const { return enabled_; }

private:
    std::string path_;
    std::ofstream file_;
    uint64_t sequence_ = 0;
    mutable std::mutex mutex_;
    bool enabled_ = false;
    Durability durability_ = Durability::PerEvent;
};

}  // namespace exchange

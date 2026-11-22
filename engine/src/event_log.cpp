#include "exchange/event_log.hpp"

#include <fstream>

namespace exchange {

EventLog::EventLog(const std::string& path, Durability durability)
    : path_(path), durability_(durability) {
    if (!path_.empty()) {
        // Continue numbering after whatever an earlier run already wrote.
        // Restarting at zero produced duplicate sequence numbers after a
        // restart, so a later snapshot-plus-tail recovery replayed events from
        // the earlier run a second time.
        for (const auto& e : read_all()) {
            if (e.sequence > sequence_) sequence_ = e.sequence;
        }
        file_.open(path_, std::ios::app);
        enabled_ = file_.is_open();
    }
}

EventLog::~EventLog() {
    // Buffered mode would otherwise drop the tail of the log on a clean exit.
    if (enabled_) {
        std::lock_guard<std::mutex> lock(mutex_);
        file_.flush();
    }
}

void EventLog::append(const Event& event) {
    if (!enabled_) return;
    std::lock_guard<std::mutex> lock(mutex_);
    file_ << nlohmann::json(event).dump() << "\n";
    if (durability_ == Durability::PerEvent) {
        file_.flush();
    }
}

void EventLog::append_preformatted(uint64_t sequence, uint64_t timestamp_ns, EventType type,
                                   const std::string& payload_json) {
    if (!enabled_) return;
    std::lock_guard<std::mutex> lock(mutex_);
    file_ << "{\"sequence\":" << sequence << ",\"timestamp_ns\":" << timestamp_ns << ",\"type\":\""
          << to_string(type) << "\",\"payload\":" << payload_json << "}\n";
    if (durability_ == Durability::PerEvent) {
        file_.flush();
    }
}

void EventLog::flush() {
    if (!enabled_) return;
    std::lock_guard<std::mutex> lock(mutex_);
    file_.flush();
}

std::vector<Event> EventLog::read_all() const {
    return read_from(0);
}

std::vector<Event> EventLog::read_from(uint64_t start_sequence) const {
    std::vector<Event> events;
    if (path_.empty()) return events;

    std::ifstream in(path_);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        try {
            auto j = nlohmann::json::parse(line);
            Event e = j.get<Event>();
            if (e.sequence >= start_sequence) events.push_back(e);
        } catch (...) {
        }
    }
    return events;
}

uint64_t EventLog::next_sequence() {
    return ++sequence_;
}

}  // namespace exchange

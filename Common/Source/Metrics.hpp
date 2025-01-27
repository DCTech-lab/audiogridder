/*
 * Copyright (c) 2020 Andreas Pohl
 * Licensed under MIT (https://github.com/apohl79/audiogridder/blob/master/COPYING)
 *
 * Author: Andreas Pohl
 */

#ifndef Metrics_hpp
#define Metrics_hpp

#include <JuceHeader.h>
#include <unordered_map>

#include "SharedInstance.hpp"
#include "Utils.hpp"

namespace e47 {

class BasicStatistic {
  public:
    virtual ~BasicStatistic() {}
    virtual void aggregate() = 0;
    virtual void aggregate1s() = 0;
    virtual void log(const String&) = 0;
};

class Meter : public BasicStatistic {
  public:
    Meter() : ALPHA_1min(alpha(60)) {}
    ~Meter() override {}

    inline void increment(uint32 i = 1) { m_counter += i; }
    inline double rate_1min() { return m_rate1min + getExtRate1min(); }

    inline void enableExtData(bool b) { m_hasExtRates = b; }

    inline double getExtRate1min() {
        auto rate = 0.0;
        if (m_hasExtRates) {
            std::lock_guard<std::mutex> lock(m_extRate1minMtx);
            for (auto& ext : m_extRate1min) {
                rate += ext.second;
            }
        }
        return rate;
    }

    inline void updateExtRate1min(const String& key, double val) {
        std::lock_guard<std::mutex> lock(m_extRate1minMtx);
        m_extRate1min[key] = val;
    }

    inline void removeExtRate1min(const String& key) {
        std::lock_guard<std::mutex> lock(m_extRate1minMtx);
        m_extRate1min.erase(key);
    }

    void aggregate() override {}
    void aggregate1s() override {
        auto c = m_counter.exchange(0, std::memory_order_relaxed);
        m_rate1min = m_rate1min * (1 - ALPHA_1min) + c * ALPHA_1min;
    }
    void log(const String&) override {}

  private:
    std::atomic_uint_fast64_t m_counter{0};
    double m_rate1min = 0.0;
    const double ALPHA_1min;

    bool m_hasExtRates = false;
    std::unordered_map<String, double> m_extRate1min;
    std::mutex m_extRate1minMtx;

    inline double alpha(int secs) { return 1 - std::exp(std::log(0.005) / secs); }
};

class TimeStatistic : public BasicStatistic, public LogTag {
  public:
    class Duration {
      public:
        Duration(std::shared_ptr<TimeStatistic> t = nullptr) : m_timer(t), m_start(Time::getHighResolutionTicks()) {}
        Duration(const Duration& other)
            : m_timer(other.m_timer), m_start(other.m_start), m_finished(other.m_finished) {}
        ~Duration() { update(); }

        void finish() {
            update();
            m_finished = true;
        }

        double update() {
            double ms = 0.0;
            if (!m_finished) {
                auto end = Time::getHighResolutionTicks();
                ms = Time::highResolutionTicksToSeconds(end - m_start) * 1000;
                if (nullptr != m_timer) {
                    m_timer->update(ms);
                }
                m_start = end;
            }
            return ms;
        }

        void reset() {
            m_start = Time::getHighResolutionTicks();
            m_finished = false;
        }

        void clear() { m_finished = true; }

        double getMillisecondsPassed() const {
            auto end = Time::getHighResolutionTicks();
            return Time::highResolutionTicksToSeconds(end - m_start) * 1000;
        }

      private:
        std::shared_ptr<TimeStatistic> m_timer;
        int64 m_start;
        bool m_finished = false;
    };

    class Timeout {
      public:
        Timeout(int millis) : m_milliseconds(millis) {}

        int getMillisecondsLeft() const {
            int passed = (int)lround(m_duration.getMillisecondsPassed());
            if (passed < m_milliseconds) {
                return m_milliseconds - passed;
            }
            return 0;
        }

      private:
        Duration m_duration;
        int m_milliseconds;
    };

    struct Histogram {
        double min = 0;
        double max = 0;
        double avg = 0;
        double sum = 0;
        double nintyFifth = 0;
        size_t count = 0;
        std::vector<std::pair<double, size_t>> dist;

        Histogram(size_t num_of_bins, double bin_size) {
            double lower = 0;
            for (size_t i = 0; i < num_of_bins + 1; ++i) {
                dist.emplace_back(std::make_pair(lower, 0));
                lower += bin_size;
            }
        }

        Histogram(const json& j) {
            min = jsonGetValue(j, "min", 0.0);
            max = jsonGetValue(j, "max", 0.0);
            avg = jsonGetValue(j, "avg", 0.0);
            sum = jsonGetValue(j, "sum", 0.0);
            nintyFifth = jsonGetValue(j, "95th", 0.0);
            count = jsonGetValue(j, "count", (size_t)0);
            if (j.find("dist") != j.end()) {
                for (auto& d : j["dist"]) {
                    dist.emplace_back(std::make_pair(d["lower"].get<double>(), d["count"].get<size_t>()));
                }
            }
        }

        void updateBin(size_t bin, size_t c) { dist[bin].second += c; }

        json toJson() {
            json j;
            j["min"] = min;
            j["max"] = max;
            j["avg"] = avg;
            j["sum"] = sum;
            j["count"] = count;
            j["95th"] = nintyFifth;
            json jdist = json::array();
            for (auto& d : dist) {
                jdist.push_back({{"lower", d.first}, {"count", d.second}});
            }
            j["dist"] = jdist;
            return j;
        }
    };

    TimeStatistic(size_t numOfBins = 10, double binSize = 2 /* ms */)
        : LogTag("stats"), m_numOfBins(numOfBins), m_binSize(binSize) {}
    ~TimeStatistic() override {}

    void update(double t);
    void aggregate() override;
    void aggregate1s() override;
    Histogram get1minHistogram();
    void run();
    void log(const String& name) override;
    void setShowLog(bool b) { m_showLog = b; }

    Meter& getMeter() { return m_meter; }

    static Duration getDuration(const String& name, bool show = true);

    std::vector<Histogram> get1minValues();

    inline void enableExtData(bool b) { m_hasExtValues = b; }

    inline void updateExt1minValues(const String& key, const std::vector<Histogram> values) {
        std::lock_guard<std::mutex> lock(m_ext1minValuesMtx);
        m_ext1minValues[key] = values;
    }

    inline void removeExt1minValues(const String& key) {
        std::lock_guard<std::mutex> lock(m_ext1minValuesMtx);
        m_ext1minValues.erase(key);
    }

  private:
    std::vector<double> m_times[2];
    std::mutex m_timesMtx;
    uint8 m_timesIdx = 0;
    std::vector<Histogram> m_1minValues;
    std::mutex m_1minValuesMtx;
    size_t m_numOfBins;
    double m_binSize;
    Meter m_meter;
    bool m_showLog = true;

    bool m_hasExtValues = false;
    std::unordered_map<String, std::vector<Histogram>> m_ext1minValues;
    std::mutex m_ext1minValuesMtx;
};

class Metrics : public Thread, public LogTag, public SharedInstance<Metrics> {
  public:
    using StatsMap = std::unordered_map<String, std::shared_ptr<BasicStatistic>>;

    Metrics() : Thread("Metrics"), LogTag("metrics") { startThread(); }
    ~Metrics() { stopThread(-1); }
    void run();

    void aggregateAndShow(bool show);
    void aggregate1s();

    static void cleanup();

    static StatsMap getStats();

    template <typename T>
    static std::shared_ptr<T> getStatistic(const String& name) {
        std::lock_guard<std::mutex> lock(m_statsMtx);
        std::shared_ptr<T> stat;
        auto it = m_stats.find(name);
        if (m_stats.end() == it) {
            auto itnew = m_stats.emplace(name, std::make_shared<T>());
            stat = std::dynamic_pointer_cast<T>(itnew.first->second);
        } else {
            stat = std::dynamic_pointer_cast<T>(it->second);
        }
        return stat;
    }

  private:
    static StatsMap m_stats;
    static std::mutex m_statsMtx;
};

#define TRACE_STRCPY(dst, src)                              \
    do {                                                    \
        int len = jmin((int)sizeof(dst) - 1, src.length()); \
        strncpy(dst, src.getCharPointer(), (size_t)len);    \
        dst[len] = 0;                                       \
    } while (0)

class TimeTrace {
  public:
    struct Record {
        double timeSpentMs = 0;
        char name[32] = {0};
        enum Type : uint8_t { TRACE, START_GROUP, FINISH_GROUP };
        Type type = TRACE;
    };

    struct TraceContext {
        TimeStatistic::Duration duration;
        std::vector<Record> records;
        Uuid uuid;

        void add(const String& name, Record::Type type = Record::TRACE) {
            Record r;
            r.timeSpentMs = duration.update();
            r.type = type;
            TRACE_STRCPY(r.name, name);
            records.push_back(std::move(r));
        }

        void startGroup() { add({}, Record::START_GROUP); }

        void finishGroup(const String& name) { add(name, Record::FINISH_GROUP); }

        double totalMs() const {
            double total = 0.0;
            for (auto& r : records) {
                total += r.timeSpentMs;
            }
            return total;
        }

        void summary(const LogTag* tag, const String& name, double treshold) const {
            auto ms = totalMs();
            if (ms > treshold) {
                setLogTagByRef(*tag);
                logln(name << " took " << ms << "ms (" << uuid.toDashedString() << ")");

                std::vector<double> groupLevel;

                auto getIndent = [](size_t i) {
                    String s;
                    s << std::string(2 * (i + 1), ' ');
                    return s;
                };

                for (auto& trec : records) {
                    switch (trec.type) {
                        case Record::TRACE:
                            logln(getIndent(groupLevel.size()) << (groupLevel.empty() ? "- " : "+ ") << trec.name
                                                               << ": " << trec.timeSpentMs << "ms");
                            if (!groupLevel.empty()) {
                                groupLevel.back() += trec.timeSpentMs;
                            }
                            break;
                        case Record::START_GROUP:
                            groupLevel.push_back(0.0);
                            break;
                        case Record::FINISH_GROUP:
                            auto groupMs = trec.timeSpentMs + groupLevel.back();
                            groupLevel.pop_back();
                            logln(getIndent(groupLevel.size()) << "= " << trec.name << ": " << groupMs << "ms");
                            if (!groupLevel.empty()) {
                                groupLevel.back() += groupMs;
                            }
                            break;
                    }
                }
            }
        }

        void reset(Uuid id = Uuid()) {
            duration.reset();
            records.clear();
            if (id != Uuid::null()) {
                uuid = id;
            } else {
                uuid = Uuid();
            }
        }
    };

    static std::shared_ptr<TraceContext> createTraceContext();
    static std::shared_ptr<TraceContext> getTraceContext();
    static void deleteTraceContext();

    static inline void addTracePoint(const String& name) {
        if (auto ctx = getTraceContext()) {
            ctx->add(name);
        }
    }

    static inline void startGroup() {
        if (auto ctx = getTraceContext()) {
            ctx->startGroup();
        }
    }

    static inline void finishGroup(const String& name) {
        if (auto ctx = getTraceContext()) {
            ctx->finishGroup(name);
        }
    }

    static inline Uuid getTraceId() {
        if (auto ctx = getTraceContext()) {
            return ctx->uuid;
        }
        return Uuid::null();
    }
};

}  // namespace e47

#endif /* Metrics_hpp */

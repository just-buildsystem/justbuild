#ifndef INCLUDED_SRC_BUILDTOOL_MULTITHREADING_ASYNC_MAP_CONSUMER_HPP
#define INCLUDED_SRC_BUILDTOOL_MULTITHREADING_ASYNC_MAP_CONSUMER_HPP

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "gsl-lite/gsl-lite.hpp"
#include "src/buildtool/multithreading/async_map.hpp"
#include "src/buildtool/multithreading/async_map_node.hpp"
#include "src/buildtool/multithreading/task.hpp"
#include "src/buildtool/multithreading/task_system.hpp"

using AsyncMapConsumerLogger = std::function<void(std::string const&, bool)>;
using AsyncMapConsumerLoggerPtr = std::shared_ptr<AsyncMapConsumerLogger>;

// Thread safe class that enables us to add tasks to the queue system that
// depend on values being ready. Value constructors are only queued once per key
// and tasks that depend on such values are only queued once the values are
// ready. As template parameters, it takes the type that keys will have, the
// type that their corresponding values will have and the type of the underlying
// thread-safe associative container. The default thread-safe associative
// container is AsyncMap<Key, Value> and any substite must have the same public
// interface to be used in AsyncMapConsumer.
template <typename Key, typename Value, typename Map = AsyncMap<Key, Value>>
class AsyncMapConsumer {
  public:
    using Node = typename Map::Node;
    using NodePtr = typename Map::NodePtr;

    using Setter = std::function<void(Value&&)>;
    using SetterPtr = std::shared_ptr<Setter>;

    using Logger = AsyncMapConsumerLogger;
    using LoggerPtr = AsyncMapConsumerLoggerPtr;

    using FailureFunction = std::function<void()>;
    using FailureFunctionPtr = std::shared_ptr<FailureFunction>;

    using Consumer = std::function<void(std::vector<Value const*> const&)>;
    using ConsumerPtr = std::shared_ptr<Consumer>;

    using SubCaller =
        std::function<void(std::vector<Key> const&, Consumer, LoggerPtr)>;
    using SubCallerPtr = std::shared_ptr<SubCaller>;

    using ValueCreator = std::function<void(gsl::not_null<TaskSystem*> const&,
                                            SetterPtr,
                                            LoggerPtr,
                                            SubCallerPtr,
                                            Key const&)>;

    explicit AsyncMapConsumer(ValueCreator vc, std::size_t jobs = 0)
        : value_creator_{std::make_shared<ValueCreator>(std::move(vc))},
          map_{jobs} {}

    /// \brief Makes sure that the consumer will be executed once the values for
    /// all the keys are available, and that the value creators for those keys
    /// are queued (if they weren't queued already).
    /// \param[in]  ts  task system
    /// \param[in]  keys    keys for the values that consumer requires
    /// \param[in]  consumer function-like object that takes a vector of values
    /// and returns void that will be queued to be called with the values
    /// associated to keys once they are ready
    /// \param[in]  logger  function-like object that takes a string and a bool
    /// indicating that the event was fatal and returns
    /// void. This will be passed around and can be used to report errors
    /// (possibly with side effects outside AsyncMapConsumer) in the value
    /// creator
    /// \param[in]  fail  function to call instead of the consumer if the
    /// creation of this node failed
    void ConsumeAfterKeysReady(gsl::not_null<TaskSystem*> const& ts,
                               std::vector<Key> const& keys,
                               Consumer&& consumer,
                               Logger&& logger,
                               FailureFunction&& fail) {
        ConsumeAfterKeysReady(
            ts,
            std::nullopt,
            keys,
            std::move(consumer),
            std::make_shared<Logger>(std::move(logger)),
            std::make_shared<FailureFunction>(std::move(fail)));
    }

    // Similar to the previous method, but without failure function
    void ConsumeAfterKeysReady(gsl::not_null<TaskSystem*> const& ts,
                               std::vector<Key> const& keys,
                               Consumer&& consumer,
                               Logger&& logger) {
        ConsumeAfterKeysReady(ts,
                              std::nullopt,
                              keys,
                              std::move(consumer),
                              std::make_shared<Logger>(std::move(logger)),
                              nullptr);
    }

    [[nodiscard]] auto GetPendingKeys() const -> std::vector<Key> {
        return map_.GetPendingKeys();
    }

    // Returns call order of the first cycle found in the requests map.
    [[nodiscard]] auto DetectCycle() const -> std::optional<std::vector<Key>> {
        auto const& requests = GetPendingRequests();
        std::vector<Key> calls{};
        std::unordered_set<Key> known{};
        calls.resize(requests.size() + 1, Key{});
        known.reserve(requests.size());
        for (auto const& [caller, _] : requests) {
            if (DetectCycleForCaller(&calls, &known, requests, caller)) {
                return calls;
            }
        }
        return std::nullopt;
    }

    void Clear(gsl::not_null<TaskSystem*> const& ts) { map_.Clear(ts); }

  private:
    using NodeRequests = std::unordered_map<Key, std::unordered_set<NodePtr>>;

    std::shared_ptr<ValueCreator> value_creator_{};
    Map map_{};
    mutable std::shared_mutex requests_m_{};
    std::unordered_map<std::thread::id, NodeRequests> requests_by_thread_{};

    // Similar to previous methods, but in this case the logger and failure
    // function are already std::shared_ptr type.
    void ConsumeAfterKeysReady(gsl::not_null<TaskSystem*> const& ts,
                               std::optional<Key> const& consumer_id,
                               std::vector<Key> const& keys,
                               Consumer&& consumer,
                               LoggerPtr&& logger,
                               FailureFunctionPtr&& fail) {
        auto consumerptr = std::make_shared<Consumer>(std::move(consumer));
        if (keys.empty()) {
            ts->QueueTask([consumerptr = std::move(consumerptr)]() {
                (*consumerptr)({});
            });
            return;
        }

        auto nodes = EnsureValuesEventuallyPresent(ts, keys, std::move(logger));
        auto first_node = nodes->at(0);
        if (fail) {
            first_node->QueueOnFailure(ts, [fail]() { (*fail)(); });
        }
        auto const queued = first_node->AddOrQueueAwaitingTask(
            ts,
            [ts,
             consumerptr,
             nodes = std::move(nodes),
             fail,
             this,
             consumer_id]() {
                QueueTaskWhenAllReady(
                    ts, consumer_id, consumerptr, fail, nodes, 1);
            });
        if (consumer_id and not queued) {
            RecordNodeRequest(*consumer_id, first_node);
        }
    }

    [[nodiscard]] auto EnsureValuesEventuallyPresent(
        gsl::not_null<TaskSystem*> const& ts,
        std::vector<Key> const& keys,
        LoggerPtr&& logger) -> std::shared_ptr<std::vector<NodePtr>> {
        std::vector<NodePtr> nodes{};
        nodes.reserve(keys.size());
        std::transform(std::begin(keys),
                       std::end(keys),
                       std::back_inserter(nodes),
                       [this, ts, logger](Key const& key) {
                           return EnsureValuePresent(ts, key, logger);
                       });
        return std::make_shared<std::vector<NodePtr>>(std::move(nodes));
    }

    // Retrieves node from map associated to given key and queues its processing
    // task (i.e. a task that executes the value creator) to the task system.
    // Note that the node will only queue a processing task once.
    [[nodiscard]] auto EnsureValuePresent(gsl::not_null<TaskSystem*> const& ts,
                                          Key const& key,
                                          LoggerPtr const& logger) -> NodePtr {
        auto node = map_.GetOrCreateNode(key);
        auto setterptr = std::make_shared<Setter>([ts, node](Value&& value) {
            node->SetAndQueueAwaitingTasks(ts, std::move(value));
        });
        auto failptr =
            std::make_shared<FailureFunction>([node, ts]() { node->Fail(ts); });
        auto subcallerptr = std::make_shared<SubCaller>(
            [ts, failptr = std::move(failptr), this, key](
                std::vector<Key> const& keys,
                Consumer&& consumer,
                LoggerPtr&& logger) {
                ConsumeAfterKeysReady(ts,
                                      key,
                                      keys,
                                      std::move(consumer),
                                      std::move(logger),
                                      FailureFunctionPtr{failptr});
            });
        auto wrappedLogger =
            std::make_shared<Logger>([logger, node, ts](auto msg, auto fatal) {
                if (fatal) {
                    node->Fail(ts);
                }
                (*logger)(msg, fatal);
            });
        node->QueueOnceProcessingTask(
            ts,
            [vc = value_creator_,
             ts,
             key,
             setterptr = std::move(setterptr),
             wrappedLogger = std::move(wrappedLogger),
             subcallerptr = std::move(subcallerptr)]() {
                (*vc)(ts, setterptr, wrappedLogger, subcallerptr, key);
            });
        return node;
    }

    // Queues tasks for each node making sure that the task that calls the
    // consumer on the values is only queued once all the values are ready
    void QueueTaskWhenAllReady(
        gsl::not_null<TaskSystem*> const& ts,
        std::optional<Key> const& consumer_id,
        ConsumerPtr const& consumer,
        // NOLINTNEXTLINE(performance-unnecessary-value-param)
        FailureFunctionPtr const& fail,
        std::shared_ptr<std::vector<NodePtr>> const& nodes,
        std::size_t pos) {
        if (pos == nodes->size()) {
            ts->QueueTask([nodes, consumer]() {
                std::vector<Value const*> values{};
                values.reserve(nodes->size());
                std::transform(
                    nodes->begin(),
                    nodes->end(),
                    std::back_inserter(values),
                    [](NodePtr const& node) { return &node->GetValue(); });
                (*consumer)(values);
            });
        }
        else {
            auto current = nodes->at(pos);
            if (fail) {
                current->QueueOnFailure(ts, [fail]() { (*fail)(); });
            }
            auto const queued = current->AddOrQueueAwaitingTask(
                ts, [ts, consumer, fail, nodes, pos, this, consumer_id]() {
                    QueueTaskWhenAllReady(
                        ts, consumer_id, consumer, fail, nodes, pos + 1);
                });
            if (consumer_id and not queued) {
                RecordNodeRequest(*consumer_id, current);
            }
        }
    }

    void RecordNodeRequest(Key const& consumer_id,
                           gsl::not_null<NodePtr> const& node) {
        auto tid = std::this_thread::get_id();
        std::shared_lock shared(requests_m_);
        auto local_requests_it = requests_by_thread_.find(tid);
        if (local_requests_it == requests_by_thread_.end()) {
            shared.unlock();
            std::unique_lock lock(requests_m_);
            // create new requests map for thread
            requests_by_thread_[tid] = NodeRequests{{consumer_id, {node}}};
            return;
        }
        // every thread writes to separate local requests map
        local_requests_it->second[consumer_id].emplace(node);
    }

    [[nodiscard]] auto GetPendingRequests() const -> NodeRequests {
        NodeRequests requests{};
        std::unique_lock lock(requests_m_);
        for (auto const& [_, local_requests] : requests_by_thread_) {
            requests.reserve(requests.size() + local_requests.size());
            for (auto const& [consumer, deps] : local_requests) {
                auto& nodes = requests[consumer];
                std::copy_if(  // filter out nodes that are ready by now
                    deps.begin(),
                    deps.end(),
                    std::inserter(nodes, nodes.end()),
                    [](auto const& node) { return not node->IsReady(); });
            }
        }
        return requests;
    }

    [[nodiscard]] static auto DetectCycleForCaller(
        gsl::not_null<std::vector<Key>*> const& calls,
        gsl::not_null<std::unordered_set<Key>*> const& known,
        NodeRequests const& requests,
        Key const& caller,
        std::size_t pos = 0) -> bool {
        if (not known->contains(caller)) {
            auto it = requests.find(caller);
            if (it != requests.end()) {
                (*calls)[pos++] = caller;
                for (auto const& dep : it->second) {
                    auto const& dep_key = dep->GetKey();
                    auto last = calls->begin() + static_cast<int>(pos);
                    if (std::find(calls->begin(), last, dep_key) != last) {
                        (*calls)[pos++] = dep_key;
                        calls->resize(pos);
                        return true;
                    }
                    if (DetectCycleForCaller(
                            calls, known, requests, dep_key, pos)) {
                        return true;
                    }
                }
            }
            known->emplace(caller);
        }
        return false;
    }
};

#endif  // INCLUDED_SRC_BUILDTOOL_MULTITHREADING_ASYNC_MAP_CONSUMER_HPP

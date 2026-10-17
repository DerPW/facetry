#pragma once

// Cancelling long work: between features (the regeneration engine checks its token) and inside
// long kernel operations (they check the token of the current CancelScope while they run).

#include <atomic>
#include <exception>
#include <functional>
#include <memory>
#include <utility>

namespace facetry::core {

// Cancels running work from another thread. Copies share the same flag.
class CancelToken {
  public:
    void cancel() const noexcept {
        flag_->store(true);
    }
    [[nodiscard]] bool cancelled() const noexcept {
        return flag_->load();
    }

  private:
    std::shared_ptr<std::atomic<bool>> flag_ = std::make_shared<std::atomic<bool>>(false);
};

// Thrown by a long operation that stopped because its CancelScope's token was cancelled.
class Cancelled : public std::exception {
  public:
    [[nodiscard]] const char* what() const noexcept override {
        return "Cancelled";
    }
};

// While alive, long kernel operations on this thread stop with Cancelled once `token` is
// cancelled. `poll` (optional) is called while they run, e.g. to ask the user interface whether
// to stop; it may cancel the token. Scopes nest; the innermost one counts.
class CancelScope {
  public:
    explicit CancelScope(const CancelToken& token, std::function<void()> poll = {});
    ~CancelScope();
    CancelScope(const CancelScope&) = delete;
    CancelScope& operator=(const CancelScope&) = delete;

    // Calls the innermost scope's poll function, then reports whether its token is cancelled.
    // False without a scope.
    [[nodiscard]] static bool shouldStop();

  private:
    const CancelToken& token_;
    std::function<void()> poll_;
    CancelScope* previous_;
};

}  // namespace facetry::core

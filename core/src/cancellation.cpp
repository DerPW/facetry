#include "facetry/core/cancellation.hpp"

namespace facetry::core {
namespace {

thread_local CancelScope* currentScope = nullptr;

}  // namespace

CancelScope::CancelScope(const CancelToken& token, std::function<void()> poll)
    : token_(token), poll_(std::move(poll)), previous_(currentScope) {
    currentScope = this;
}

CancelScope::~CancelScope() {
    currentScope = previous_;
}

bool CancelScope::shouldStop() {
    CancelScope* scope = currentScope;
    if (scope == nullptr) {
        return false;
    }
    if (scope->poll_) {
        scope->poll_();
    }
    return scope->token_.cancelled();
}

}  // namespace facetry::core

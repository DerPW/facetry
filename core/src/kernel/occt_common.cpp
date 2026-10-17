#include "occt_common.hpp"

#include "facetry/core/cancellation.hpp"
#include "facetry/core/log.hpp"

#include <BRepCheck_Analyzer.hxx>
#include <Message.hxx>
#include <Message_Messenger.hxx>
#include <Message_Printer.hxx>
#include <Message_PrinterOStream.hxx>
#include <TCollection_AsciiString.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp.hxx>

#include <atomic>
#include <cmath>
#include <mutex>

namespace facetry::core {

Shape::Impl::Impl(TopoDS_Shape occtShape) : shape(std::move(occtShape)) {
    TopExp::MapShapes(shape, TopAbs_FACE, faces);
    TopExp::MapShapes(shape, TopAbs_EDGE, edges);
    TopExp::MapShapes(shape, TopAbs_VERTEX, vertices);
}

namespace kernel {
namespace {

LogLevel toLogLevel(Message_Gravity gravity) {
    switch (gravity) {
        case Message_Trace:
            return LogLevel::Trace;
        case Message_Info:
            // OCCT's info messages (e.g. STEP transfer statistics) are noisy.
            return LogLevel::Debug;
        case Message_Warning:
            return LogLevel::Warning;
        case Message_Alarm:
        case Message_Fail:
            return LogLevel::Error;
    }
    return LogLevel::Info;
}

class LogPrinter : public Message_Printer {
  public:
    LogPrinter() {
        SetTraceLevel(Message_Trace);
    }

  protected:
    void send(const TCollection_AsciiString& text, const Message_Gravity gravity) const override {
        if (gravity >= GetTraceLevel()) {
            log(toLogLevel(gravity), std::string_view(text.ToCString()));
        }
    }
};

// Asks the current CancelScope whether to stop. OpenCASCADE calls UserBreak() often while an
// algorithm runs; parallel algorithms also call it from worker threads, which have no scope and
// so never stop it themselves.
class CancelIndicator : public Message_ProgressIndicator {
  public:
    bool UserBreak() override {
        if (CancelScope::shouldStop()) {
            stopped_ = true;
        }
        return stopped_;
    }
    void Show(const Message_ProgressScope& /*scope*/, const bool /*force*/) override {}

    [[nodiscard]] bool stopped() const {
        return stopped_;
    }

  private:
    std::atomic<bool> stopped_ = false;
};

}  // namespace

Progress::Progress() : indicator_(new CancelIndicator()) {}

Message_ProgressRange Progress::range() const {
    return indicator_->Start();
}

void Progress::check() const {
    if (static_cast<const CancelIndicator&>(*indicator_).stopped()) {
        throw Cancelled();
    }
}

void routeMessagesToLog() {
    static std::once_flag once;
    std::call_once(once, [] {
        const Handle(Message_Messenger) & messenger = Message::DefaultMessenger();
        messenger->RemovePrinters(STANDARD_TYPE(Message_PrinterOStream));
        messenger->AddPrinter(new LogPrinter());
    });
}

Shape wrap(const TopoDS_Shape& shape, const char* operation) {
    if (shape.IsNull()) {
        throw Error(ErrorCode::Kernel, std::string(operation) + " produced no shape");
    }
    return Shape(std::make_shared<const Shape::Impl>(shape));
}

Shape checkedResult(const TopoDS_Shape& shape, const char* operation) {
    if (!shape.IsNull() && !BRepCheck_Analyzer(shape).IsValid()) {
        throw Error(ErrorCode::Kernel, std::string(operation) + " produced an invalid shape");
    }
    return wrap(shape, operation);
}

void requirePositive(double value, const char* name) {
    if (!std::isfinite(value) || value <= 0.0) {
        throw Error(ErrorCode::InvalidArgument,
                    std::string(name) + " must be a positive number, got " + std::to_string(value));
    }
}

void requireFinite(double value, const char* name) {
    if (!std::isfinite(value)) {
        throw Error(ErrorCode::InvalidArgument, std::string(name) + " must be a finite number");
    }
}

}  // namespace kernel
}  // namespace facetry::core

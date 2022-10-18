#pragma once

#include <tuple>
#include <vector>
#include <string>
#include <sstream>
#include <optional>
#include <string_view>
#include <unordered_set>

struct Context;

template <typename T>
concept Exposable = requires(T t, Context &context) {
    t.expose(context);
};

struct ErrorMustMatchText { std::string text; };
struct ErrorRequiresSpaceAfter { std::string keyword; };
struct ErrorMissingToken { };
struct ErrorProhibitsPattern { };
struct ErrorNoMatchingPattern { };
struct ErrorMustEnd { };
struct ErrorVerifyFailure { std::string reason; };

using ErrorReason = std::variant<
    ErrorMustMatchText,
    ErrorRequiresSpaceAfter,
    ErrorMissingToken,
    ErrorProhibitsPattern,
    ErrorNoMatchingPattern,
    ErrorMustEnd,
    ErrorVerifyFailure
>;

std::string reasonText(const ErrorReason &reason);

struct Error {
    size_t index = 0;
    ErrorReason reason;

    bool matched = false;

    Error &operator=(Error &&error) = default;

    Error(size_t index, ErrorReason reason, bool matched);

    Error(const Error &other) = delete;
    Error(Error &&error) noexcept = default;
};

struct LineDetails {
    std::string line;
    std::string marker;

    size_t lineNumber = 0;

    LineDetails(const std::string &text, size_t index, bool backtrack = true);
};

template <typename T, typename ErrorT = Error>
struct Result : public std::variant<T, ErrorT> {
    using Type = T;

    [[nodiscard]]
    const T *ptr() const {
        struct {
            const T *operator()(const T &t) { return &t; }
            const T *operator()(const ErrorT &) { return nullptr; }
        } visitor { };

        return std::visit(visitor, *this);
    }

    [[nodiscard]]
    T *ptr() {
        struct {
            T *operator()(T &t) { return &t; }
            T *operator()(ErrorT &) { return nullptr; }
        } visitor { };

        return std::visit(visitor, *this);
    }

    [[nodiscard]]
    const ErrorT *error() const {
        struct {
            const ErrorT *operator()(const T &) { return nullptr; }
            const ErrorT *operator()(const ErrorT &e) { return &e; }
        } visitor { };

        return std::visit(visitor, *this);
    }

    [[nodiscard]]
    ErrorT *error() {
        struct {
            ErrorT *operator()(T &) { return nullptr; }
            ErrorT *operator()(ErrorT &e) { return &e; }
        } visitor { };

        return std::visit(visitor, *this);
    }

    Result<T, ErrorT> &operator=(Result<T, ErrorT> &&other) noexcept = default;

    Result(const Result<T, ErrorT> &other) = delete;
    Result(Result<T, ErrorT> &&other) noexcept = default;

    explicit Result(T &&t) : std::variant<T, ErrorT>(std::forward<T>(t)) { }
    explicit Result(ErrorT &&e) : std::variant<T, ErrorT>(std::move(e)) { }
};

template <typename ...Args>
struct ParserResult: public Result<std::tuple<Args...>> {
    template <typename ...OtherArgs>
    using ExtendType = ParserResult<Args..., OtherArgs...>;

    explicit ParserResult(std::tuple<Args...> &&t) : Result<std::tuple<Args...>>(std::forward<std::tuple<Args...>>(t)) { }
    explicit ParserResult(Error &&e) : Result<std::tuple<Args...>>(std::move(e)) { }
};

struct State;

struct Stoppable {
    [[nodiscard]]
    virtual bool stop(std::string_view view, State &state) const = 0;
};

struct AnyHard: public Stoppable {
    std::unordered_set<char> stopAt;

    [[nodiscard]]
    bool stop(std::string_view view, State &state) const override;

    AnyHard();
    explicit AnyHard(std::unordered_set<char> stopAt);
};

struct NotSpace: public Stoppable {
    [[nodiscard]]
    bool stop(std::string_view view, State &state) const override;
};

struct StringStops: public Stoppable {
    const std::vector<std::string_view> &stops;

    [[nodiscard]]
    bool stop(std::string_view view, State &state) const override;

    explicit StringStops(const std::vector<std::string_view> &stops);
};

struct State {
    const char *text;
    size_t index;
    size_t count;

    void push(const Stoppable &stoppable);

    void pop(size_t size, const Stoppable &stoppable);

    [[nodiscard]]
    std::string_view pull(size_t size) const;

    [[nodiscard]]
    size_t until(const Stoppable &stoppable);

    [[nodiscard]]
    bool ends(size_t size, const Stoppable &stoppable);

    explicit State(std::string_view view);
};

struct Context {
    State &state;
    const Stoppable &space;
    const Stoppable &token;

    bool matched = false;

    Context extend(const Stoppable *space, const Stoppable *token);

    void push();

    void pop(size_t size);

    [[nodiscard]]
    std::string_view pull(size_t size) const;

    [[nodiscard]]
    bool ends(size_t size);

    [[nodiscard]]
    Error rawError(ErrorReason reason) const;

    template <typename ...Args>
    [[nodiscard]]
    ParserResult<Args...> error(ErrorReason reason) const {
        return ParserResult<Args...> { rawError(std::move(reason)) };
    }

    Context(State &state, const Stoppable &space, const Stoppable &token);
};

template <typename T>
requires (!Exposable<T>)
ParserResult<T> expose(T t, Context &view) {
    return ParserResult<T> { std::make_tuple(std::move(t)) };
}

// std::invoke_result_t<decltype(&T::expose), T, Context &>
template <typename T>
requires Exposable<T>
auto expose(const T &t, Context &view) {
    return t.expose(view); // requires is Result
}

template <typename T>
struct NoAutoContext {
    using Type = T;

    T value;

    explicit NoAutoContext(T &&value) : value(std::forward<T>(value)) { }
};

template <typename T>
struct IsNoAutoContextHelper {
    constexpr static bool value = false;
};

template <typename T>
struct IsNoAutoContextHelper<NoAutoContext<T>> {
    constexpr static bool value = true;
};

template <typename T>
concept IsNoAutoContext = IsNoAutoContextHelper<T>::value;

template <typename ...Produces>
struct AnyRule {
    std::unique_ptr<void, void(*)(void *)> value;

    ParserResult<Produces...> (* func)(Context &context, void *ptr) = nullptr;

    ParserResult<Produces...> dispatch(Context &context) const {
        return func(context, value.get());
    }

#pragma clang diagnostic push
#pragma ide diagnostic ignored "google-explicit-constructor"
    template <typename T>
    requires Exposable<T>
    AnyRule(T &&t) : value { new T(std::forward<T>(t)), [](void *v) { delete static_cast<T *>(v); } } {
        func = [](Context &context, void *ptr) {
            auto sub = context.extend(nullptr, nullptr);

            return static_cast<T *>(ptr)->expose(sub);
        };
    }

    template <typename T>
    AnyRule(NoAutoContext<T> &&t) : value { new T(std::forward<T>(t)), [](void *v) { delete static_cast<T *>(v); } } {
        func = [](Context &context, void *ptr) {
            return static_cast<T *>(ptr)->expose(context);
        };
    }
#pragma clang diagnostic pop

    AnyRule(const AnyRule<Produces...> &copy) = delete;
    AnyRule(AnyRule<Produces...> &&copy) = delete;
};

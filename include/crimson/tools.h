#pragma once

#include <iostream> // Debug

#include <crimson/crimson.h>

template <typename T>
using ExposeResultType = typename std::invoke_result_t<decltype(&expose<T>), T, Context &>;

template <typename T>
using ExposeType = typename ExposeResultType<T>::Type;

template <typename T>
struct ParserResultFromTupleHelper { };

template <typename ...Args>
struct ParserResultFromTupleHelper<std::tuple<Args...>> {
    using Type = ParserResult<Args...>;
};

template <typename T>
using ParserResultFromTuple = typename ParserResultFromTupleHelper<T>::Type;

template <typename First, typename Second>
concept CanBranchWith = Exposable<First> && Exposable<Second> && std::same_as<ExposeType<First>, ExposeType<Second>>;

template <typename ConditionType, typename TrueType, typename FalseType>
requires (Exposable<ConditionType> && CanBranchWith<TrueType, FalseType>)
struct If;

template <typename T>
requires Exposable<T>
struct Fails;

template <typename T>
requires Exposable<T>
struct Peek;

template <typename T>
requires Exposable<T>
struct Many;

template <typename T>
requires Exposable<T>
struct Maybe;

template <typename T, typename K>
requires Exposable<T>
struct Map;

template <typename T>
requires Exposable<T>
struct Debug;

template <typename T, typename K>
requires Exposable<T>
struct MapInto;

template <typename T, typename K>
requires Exposable<T>
struct MapThrows;

template <typename T>
requires Exposable<T>
struct Discard;

template <typename T>
requires Exposable <T>
struct Collect;

template <typename T>
requires Exposable<T>
struct MatchContext;

template <typename T, typename Check>
requires Exposable<T> && Exposable<Check>
struct MatchOn;

template <typename T, typename StoppableType>
requires Exposable<T>
struct SetStoppable;

template <typename T, typename Tuple, std::size_t ... Is>
constexpr T makeStructFromTupleHelper(Tuple &&t, std::index_sequence<Is...>) {
    return T { std::get<Is>(std::forward<Tuple>(t))... };
}

template <typename T, typename Tuple>
constexpr T makeStructFromTuple(Tuple&& t) {
    constexpr auto tuple_size = std::tuple_size_v<std::remove_reference_t<Tuple>>;
    return makeStructFromTupleHelper<T>(std::forward<Tuple>(t), std::make_index_sequence<tuple_size> { });
}

template <typename Self>
struct RuleModifiers {
    Self &&self() {
        return static_cast<Self &&>(*this);
    }

    template <typename TrueType, typename FalseType>
    auto then(TrueType &&onTrue, FalseType &&onFalse) {
        return If<Self, TrueType, FalseType> {
            self(), std::forward<TrueType>(onTrue), std::forward<FalseType>(onFalse)
        };
    }

    auto fails() {
        return Fails<Self> { self() };
    }

    auto peek() {
        return Peek<Self> { self() };
    }

    auto collect() {
        return Map {
            self(),

            [](auto tuple) { return tuple; }
        };
    }

    auto many() {
        return Many<Self> { self() };
    }

    auto maybe() {
        return Maybe<Self> { self() };
    }

    template <typename K>
    auto map(K &&map) {
        return Map<Self, K> { self(), std::forward<K>(map) };
    }

    template <typename Dest>
    auto visitTo() {
        return Map {
            self(),

            [](auto tuple) {
                return std::visit([](auto value) {
                    return Dest(value);
                }, std::get<0>(tuple));
            }
        };
    }

    template <typename T>
    auto make() {
        return Map { self(), [](auto tuple) { return std::make_from_tuple<T>(std::move(tuple)); } };
    }

    template <typename T>
    auto makeStruct() {
        return Map { self(), [](auto tuple) { return makeStructFromTuple<T>(std::move(tuple)); } };
    }

    auto makeUnique() {
        return Map { self(), [](auto tuple) {
            auto &v = std::get<0>(tuple);

            return std::make_unique<std::remove_reference_t<decltype(v)>>(std::move(v));
        } };
    }

    template <typename K>
    auto mapInto(K &&map) {
        return MapInto<Self, K> { self(), std::forward<K>(map) };
    }

    template <typename K>
    auto mapThrows(K &&map) {
        return MapThrows<Self, K> { self(), std::forward<K>(map) };
    }

    auto discard() {
        return Discard<Self> { self() };
    }

    auto matchContext() {
        return MatchContext<Self> { self() };
    }

    auto noMatchContext() {
        return NoAutoContext<Self> { self() };
    }

    template <typename Check>
    auto matchOn(Check &&check) {
        return MatchOn<Self, Check> { self(), std::forward<Check>(check) };
    }

    template <typename StoppableType>
    auto setStoppable(StoppableType &&stoppable) {
        return SetStoppable<Self, StoppableType> { self(), std::forward<StoppableType>(stoppable) };
    }

    auto debug(std::string name) {
        return Debug<Self> { std::move(name), self() };
    }
};

struct Push: public RuleModifiers<Push> {
    ParserResult<> expose(Context &context) const { // NOLINT(readability-convert-member-functions-to-static)
        context.push();

        return ParserResult<> { std::make_tuple() };
    }
};

struct End: public RuleModifiers<End> {
    ParserResult<> expose(Context &context) const {
        if (context.state.count == context.state.index) {
            return ParserResult<> { std::make_tuple() };
        }

        return context.error<>(ErrorMustEnd { });
    }
};

struct Anchor: public RuleModifiers<Anchor> {
    ParserResult<size_t> expose(Context &context) const {
        return ParserResult<size_t> { std::make_tuple(context.state.index) };
    }
};

struct Text: public RuleModifiers<Text> {
    std::string text;

    ParserResult<> expose(Context &context) const {
        if (text != context.pull(text.size())) {
            return context.error<>(ErrorMustMatchText { text });
        }

        context.pop(text.size());

        return ParserResult<> { { } };
    }

    explicit Text(std::string text) : text(std::move(text)) { }
};

struct Keyword: public RuleModifiers<Keyword> {
    std::string text;

    ParserResult<> expose(Context &context) const {
        if (text != context.pull(text.size())) {
            return context.error<>(ErrorMustMatchText { text });
        }

        if (!context.ends(text.size())) {
            return context.error<>(ErrorRequiresSpaceAfter { text });
        }

        context.pop(text.size());

        return ParserResult<> { std::make_tuple() };
    }

    explicit Keyword(std::string text) : text(std::move(text)) { }
};

struct Token: public RuleModifiers<Token> {
    ParserResult<std::string> expose(Context &context) const { // NOLINT(readability-convert-member-functions-to-static)
        size_t size = context.state.until(context.token);

        if (size <= 0)
            return context.error<std::string>(ErrorMissingToken { });

        std::string text(context.pull(size));
        context.pop(size);

        return ParserResult<std::string> { text };
    }
};

struct Until: public RuleModifiers<Until> {
    std::vector<std::string_view> stops;

    ParserResult<std::string> expose(Context &context) const {
        StringStops stoppable { stops };

        auto size = context.state.until(stoppable);

        std::string text(context.pull(size));
        context.pop(size);

        return ParserResult<std::string> { text };
    }

    explicit Until(std::vector<std::string_view> stops) : stops(std::move(stops)) { }
};

template <typename StoppableType>
struct UntilStoppable: public RuleModifiers<UntilStoppable<StoppableType>> {
    StoppableType stoppable;

    ParserResult<std::string> expose(Context &context) const {
        auto size = context.state.until(stoppable);

        std::string text(context.pull(size));
        context.pop(size);

        return ParserResult<std::string> { text };
    }

    explicit UntilStoppable(StoppableType &&stoppable) : stoppable(std::forward<StoppableType>(stoppable)) { }
};

template <typename T, typename StoppableType>
requires Exposable<T>
struct SetStoppable: public RuleModifiers<SetStoppable<T, StoppableType>> {
    T value;
    StoppableType stoppable;

    auto expose(Context &context) const {
        auto subContext = context.extend(&stoppable, nullptr);

        auto result = value.expose(subContext);

        if (subContext.matched)
            context.matched = true;

        return result;
    }

    SetStoppable(T &&value, StoppableType &&stoppable)
        : value(std::forward<T>(value)), stoppable(std::forward<StoppableType>(stoppable)) { }
};

template <typename T>
requires Exposable<T>
struct MatchContext: public RuleModifiers<MatchContext<T>> {
    T value;

    auto expose(Context &context) const {
        auto subContext = context.extend(nullptr, nullptr);
        subContext.matched = false;

        return value.expose(subContext);
    }

    explicit MatchContext(T &&value) : value(std::forward<T>(value)) { }
};

struct Match: public RuleModifiers<Match> {
    ParserResult<> expose(Context &context) const { // NOLINT(readability-convert-member-functions-to-static)
        context.matched = true;

        return ParserResult<> { std::make_tuple() };
    }
};

template <typename T>
requires Exposable<T>
struct Discard: public RuleModifiers<Discard<T>> {
    T value;

    ParserResult<> expose(Context &state) const {
        value.expose(state);

        return ParserResult<> { std::make_tuple() };
    }

    explicit Discard(T &&value) : value(std::forward<T>(value)) { }
};

template <typename ConditionType, typename TrueType, typename FalseType>
requires (Exposable<ConditionType> && CanBranchWith<TrueType, FalseType>)
struct If: public RuleModifiers<If<ConditionType, TrueType, FalseType>> {
    ConditionType condition;

    TrueType onTrue;
    FalseType onFalse;

    ExposeResultType<TrueType> expose(Context &context) const {
        auto conditionResult = condition.expose(context);

        if (conditionResult.ptr()) {
            return onTrue.expose(context);
        } else {
            return onFalse.expose(context);
        }
    }

    If(ConditionType &&condition, TrueType &&onTrue, FalseType &&onFalse)
        : condition(std::forward<ConditionType>(condition))
        , onTrue(std::forward<TrueType>(onTrue))
        , onFalse(std::forward<FalseType>(onFalse)) { }
};

template <typename T>
struct FirstTupleHelper { };

template <>
struct FirstTupleHelper<std::tuple<>> { using Type = std::monostate; };

template <typename T, typename ...Args>
struct FirstTupleHelper<std::tuple<T, Args...>> { using Type = T; };

template <typename T>
using FirstTuple = typename FirstTupleHelper<T>::Type;

template <typename ...Args>
using ResultVariant = std::variant<ExposeType<Args>...>;

template <typename ...Args>
using FirstResultVariant = std::variant<FirstTuple<ExposeType<Args>>...>;

std::monostate getTupleFirst(std::tuple<> &&t);

template <typename Arg>
Arg &&getTupleFirst(std::tuple<Arg> &&t) {
    return std::move(std::get<0>(t));
}

template <typename T>
requires Exposable<T>
struct Maybe: public RuleModifiers<Maybe<T>> {
    T value;

    using Result = FirstTuple<ExposeType<T>>;

    ParserResult<std::optional<Result>> expose(Context &context) const {
        size_t start = context.state.index;

        auto result = value.expose(context);

        if (auto pointer = result.ptr()) {
            return ParserResult<std::optional<Result>> {
                std::optional { getTupleFirst(std::move(*pointer)) }
            };
        }

        context.state.index = start;

        return ParserResult<std::optional<Result>> { std::nullopt };
    }

    Maybe(T &&value) : value(std::forward<T>(value)) { }
};

template <typename T>
requires Exposable<T>
struct Fails: public RuleModifiers<Fails<T>> {
    T value;

    ParserResult<> expose(Context &context) const {
        auto result = value.expose(context);

        if (result.ptr()) {
            return context.error(ErrorProhibitsPattern { });
        }

        return ParserResult<> { std::make_tuple() };
    }

    explicit Fails(T &&value) : value(std::forward<T>(value)) { }
};

template <typename T, typename Check>
requires Exposable<T> && Exposable<Check>
struct MatchOn: public RuleModifiers<MatchOn<T, Check>> {
    T value;
    Check check;

    using Result = ExposeResultType<T>;

    Result expose(Context &context) const {
        auto result = value.expose(context);

        if (auto error = result.error()) {
            if (error->matched || check.expose(context).ptr()) {
                Error sub = std::move(*error);
                sub.matched = true;

                return Result { std::move(sub) };
            }
        }

        return std::move(result);
    }

    explicit MatchOn(T &&value, Check &&check) : value(std::forward<T>(value)), check(std::forward<Check>(check)) { }
};

template <typename T>
requires Exposable<T>
struct Peek: public RuleModifiers<Peek<T>> {
    T value;

    auto expose(Context &context) const {
        size_t start = context.state.index;

        auto result = value.expose(context);

        context.state.index = start;

        return result;
    }

    explicit Peek(T &&value) : value(std::forward<T>(value)) { }
};

constexpr auto toSelf = [](auto tuple) { return std::move(std::get<0>(tuple)); };
constexpr auto toStringSelf = [](auto tuple) { return std::string(std::get<0>(tuple)); };

template <typename T, typename K>
requires Exposable<T>
struct Map: public RuleModifiers<Map<T, K>> {
    T value;
    K map;

    using Result = std::invoke_result_t<K, ExposeType<T>>;

    ParserResult<Result> expose(Context &context) const {
        auto result = value.expose(context);

        struct {
            const K &map;

            ParserResult<Result> operator()(Error &error) {
                return ParserResult<Result> { std::move(error) };
            }

            ParserResult<Result> operator()(typename decltype(result)::Type &v) {
                return ParserResult<Result> { std::make_tuple(map(std::move(v))) };
            }
        } visitor { map };

        return std::visit(visitor, result);
    }

    explicit Map(T &&value, K &&map) : value(std::forward<T>(value)), map(std::forward<K>(map)) { }
};

template <typename T, typename K>
requires Exposable<T>
struct MapInto: public RuleModifiers<MapInto<T, K>> {
    T value;
    K map;

    using Result = std::invoke_result_t<K, ExposeType<T>>;

    ParserResultFromTuple<Result> expose(Context &context) const {
        auto result = value.expose(context);

        struct {
            const K &map;

            ParserResultFromTuple<Result> operator()(Error &error) {
                return ParserResultFromTuple<Result> { std::move(error) };
            }

            ParserResultFromTuple<Result> operator()(typename decltype(result)::Type &v) {
                return ParserResultFromTuple<Result> { map(std::move(v)) }; // assuming map returns a tuple
            }
        } visitor { map };

        return std::visit(visitor, result);
    }

    explicit MapInto(T &&value, K &&map) : value(std::forward<T>(value)), map(std::forward<K>(map)) { }
};

template <typename T, typename K>
requires Exposable<T>
struct MapThrows: public RuleModifiers<MapInto<T, K>> {
    T value;
    K map;

    using Result = std::invoke_result_t<K, Context &, ExposeType<T>>;

    Result expose(Context &context) const {
        auto result = value.expose(context);

        struct {
            Context &context;
            const K &map;

            Result operator()(Error &error) {
                return Result { std::move(error) };
            }

            Result operator()(typename decltype(result)::Type &v) {
                return map(context, std::move(v)); // assuming map returns a tuple
            }
        } visitor { context, map };

        return std::visit(visitor, result);
    }

    explicit MapThrows(T &&value, K &&map) : value(std::forward<T>(value)), map(std::forward<K>(map)) { }
};

template <typename T>
requires Exposable<T>
struct Many: public RuleModifiers<Many<T>> {
    T value;

    using Result = FirstTuple<ExposeType<T>>;

    ParserResult<std::vector<Result>> expose(Context &context) const {
        std::vector<Result> list;

        size_t lastIndex = context.state.index;

        ExposeResultType<T> result = value.expose(context);
        while (auto pointer = result.ptr()) {
            list.push_back(getTupleFirst(std::move(*pointer)));
            lastIndex = context.state.index;

            result = value.expose(context);
        }

        // always, since only way to exit that loop is for an error to happen
        context.state.index = lastIndex;

        auto error = result.error();
        assert(error);

        if (error->matched) {
            return ParserResult<std::vector<Result>> { std::move(*error) };
        }

        return ParserResult<std::vector<Result>> { std::move(list) };
    }

    explicit Many(T &&value) : value(std::forward<T>(value)) { }
};

template <bool self, size_t index, typename ...Args>
auto anyOfTupleSized(const std::tuple<Args ...> &value, Context &context) {
    using Type = std::conditional_t<self, FirstResultVariant<Args...>, ResultVariant<Args...>>;

    if constexpr (index >= std::tuple_size_v<std::tuple<Args ...>>) {
        return context.error<Type>(ErrorNoMatchingPattern());
    } else {
        size_t start = context.state.index;

        auto subContext = context.extend(nullptr, nullptr);
        subContext.matched = false;

        auto result = expose(std::get<index>(value), subContext);

        if (auto pointer = result.ptr()) {
            auto ptr = [pointer]() {
                if constexpr (self) {
                    return &std::get<0>(*pointer);
                } else {
                    return pointer;
                }
            };

            return ParserResult<Type>(
                std::make_tuple(Type(std::in_place_index<index>, std::move(*ptr())))
            );
        }

        context.state.index = start;

        auto error = result.error();
        assert(error);

        if (index + 1 >= std::tuple_size_v<std::tuple<Args ...>> || error->matched) {
            return ParserResult<Type> { std::move(*error) };
        }

        return anyOfTupleSized<self, index + 1, Args...>(value, context);
    }
}

template <size_t index, typename T, typename ...Args>
requires (std::same_as<ExposeType<T>, ExposeType<Args>> && ...)
auto anyOfTupleValued(const std::tuple<T, Args...> &value, Context &context) {
    using Type = ExposeType<T>;
    using ResultType = ExposeResultType<T>;

    if constexpr (index >= std::tuple_size_v<std::tuple<T, Args ...>>) {
        return ResultType { context.rawError(ErrorNoMatchingPattern()) };
    } else {
        size_t start = context.state.index;

        auto subContext = context.extend(nullptr, nullptr);
        subContext.matched = false;

        auto result = expose(std::get<index>(value), subContext);

        if (auto pointer = result.ptr()) {
            return ResultType { Type(std::move(*pointer)) };
        }

        context.state.index = start;

        auto error = result.error();
        assert(error);

        if (index + 1 >= std::tuple_size_v<std::tuple<T, Args ...>> || error->matched) {
            return ResultType { std::move(*error) };
        }

        return anyOfTupleValued<index + 1, T, Args...>(value, context);
    }
}

template <typename ...Args>
struct BranchSome: public RuleModifiers<BranchSome<Args...>> {
    std::tuple<Args...> components;

    auto expose(Context &context) const {
        return anyOfTupleSized<false, 0>(components, context);
    }

    explicit BranchSome(Args && ...args) : components(std::make_tuple(std::forward<Args>(args)...)) { }
};

template <typename ...Args>
struct Branch: public RuleModifiers<Branch<Args...>> {
    std::tuple<Args...> components;

    auto expose(Context &context) const {
        return anyOfTupleSized<true, 0>(components, context);
    }

    explicit Branch(Args && ...args) : components(std::make_tuple(std::forward<Args>(args)...)) { }
};

template <typename ...Args>
struct Pick: public RuleModifiers<Pick<Args...>> {
    std::tuple<Args...> components;

    auto expose(Context &context) const {
        return anyOfTupleValued<0>(components, context);
    }

    explicit Pick(Args && ...args) : components(std::make_tuple(std::forward<Args>(args)...)) { }
};

template <typename T>
requires Exposable<T>
struct Capture: public RuleModifiers<Capture<T>> {
    T value;

    ParserResult<std::string> expose(Context &view) const {
        auto start = view.state.index;

        auto result = value.expose(view);

        if (auto error = result.error()) {
            return ParserResult<std::string> { std::move(*error) };
        }

        auto end = view.state.index;

        return ParserResult<std::string> {
            std::string(&view.state.text[start], &view.state.text[end])
        };
    }

    explicit Capture(T &&value) : value(std::forward<T>(value)) { }
};

template <typename ...Produces>
struct Wrap: public RuleModifiers<Wrap<Produces...>> {
    const AnyRule<Produces...> *rule;

    ParserResult<Produces...> expose(Context &context) const {
        return rule->dispatch(context);
    }

    explicit Wrap(const AnyRule<Produces...> *rule) : rule(rule) { }
};

template <typename T>
requires Exposable<T>
struct Debug: public RuleModifiers<Debug<T>> {
    std::string name;

    T value;

    using Type = ExposeResultType<T>;

    Type expose(Context &context) const {
        auto start = context.state.index;

        auto val = value.expose(context);

        auto end = context.state.index;

        if (Error *error = val.error()) {
            const char *matchable = error->matched ? " matched" : "";

            LineDetails details(std::string(context.state.text), error->index, false);
            std::cout << "### DEBUG: " << name << " failed on line " << details.lineNumber;
            std::cout << " with" << matchable << " error " << reasonText(error->reason) << "\n";

            std::cout << " | " << details.line << "\n";
            std::cout << " | " << details.marker << "\n";

            std::cout << " - Text Consumed (" << start << ", " << end << "): \n";
            std::cout << std::string(context.state.text + start, context.state.text + end);

            std::cout << "\n";
        }

        return val;
    }

    explicit Debug(std::string name, T &&value) : name(std::move(name)), value(std::forward<T>(value)) { }
};

template <typename ...Args1, typename ...Args2>
ParserResult<Args1..., Args2...> concat(ParserResult<Args1...> &&first, ParserResult<Args2...> &&second) {
    if (auto error = first.error()) {
        return ParserResult<Args1..., Args2...>(std::move(*error));
    }

    if (auto error = second.error()) {
        return ParserResult<Args1..., Args2...>(std::move(*error));
    }

    auto &&tuple1 = std::move(*first.ptr());
    auto &&tuple2 = std::move(*second.ptr());

    return ParserResult<Args1..., Args2...> {
        std::tuple_cat(std::move(tuple1), std::move(tuple2))
    };
}

template <size_t index, typename ...Args>
auto exposeTupleSized(const std::tuple<Args ...> &value, Context &view) {
    using Tuple = std::tuple<Args ...>;

    if constexpr (index >= std::tuple_size_v<Tuple>) {
        return ParserResult<> { std::tuple<> { } };
    } else {
        auto result = expose(std::get<index>(value), view);

        using Out = decltype(concat(std::move(result), exposeTupleSized<index + 1, Args...>(value, view)));

        if (auto error = result.error()) {
            return Out { std::move(*error) };
        }

        return concat(std::move(result), exposeTupleSized<index + 1, Args...>(value, view));
    }
}

template <typename ...Args>
auto exposeTuple(const std::tuple<Args ...> &value, Context &view) {
    return exposeTupleSized<0, Args...>(value, view);
}

template <typename ...Args>
struct Rule: public RuleModifiers<Rule<Args...>> {
    std::tuple<Args...> components;

    auto expose(Context &context) const {
        return exposeTuple(components, context);
    }

    explicit Rule(Args && ...args) : components(std::make_tuple(std::forward<Args>(args)...)) { }
};

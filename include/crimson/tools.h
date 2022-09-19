#pragma once

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

template <typename T, typename K>
requires Exposable<T>
struct ManyMap;

template <typename T>
requires Exposable<T>
struct Maybe;

template <typename T, typename K>
requires Exposable<T>
struct MaybeMap;

template <typename T, typename K>
requires Exposable<T>
struct Map;

template <typename T, typename K>
requires Exposable<T>
struct MapInto;

template <typename T>
requires Exposable<T>
struct Discard;

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

    auto many() {
        return Many<Self> { self() };
    }

    template <typename K>
    auto manyMap(K &&map) {
        return ManyMap<Self, K> { self(), std::forward<K>(map) };
    }

    auto maybe() {
        return Maybe<Self> { self() };
    }

    template <typename K>
    auto maybeMap(K &&map) {
        return MaybeMap<Self, K> { self(), std::forward<K>(map) };
    }

    template <typename K>
    auto map(K &&map) {
        return Map<Self, K> { self(), std::forward<K>(map) };
    }

    template <typename K>
    auto mapInto(K &&map) {
        return MapInto<Self, K> { self(), std::forward<K>(map) };
    }

    auto discard() {
        return Discard<Self> { self() };
    }
};

struct Push: public RuleModifiers<Push> {
    ParserResult<> expose(Context &context) const {
        context.push();

        return ParserResult<> { std::make_tuple() };
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

        context.pop(text.size());

        if (!context.ends(text.size())) {
            return context.error<>(ErrorRequiresSpaceAfter { text });
        }

        return ParserResult<> { std::make_tuple() };
    }

    explicit Keyword(std::string text) : text(std::move(text)) { }
};

template <typename T>
struct Add: public RuleModifiers<Add<T>> {
    T value;

    ParserResult<T> expose(Context &context) const {
        return ParserResult<T> { std::make_tuple(value) };
    }

    explicit Add(T &&value) : value(std::forward<T>(value)) { }
};

struct Token: public RuleModifiers<Token> {
    ParserResult<std::string> expose(Context &context) const {
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
requires Exposable<T>
struct Maybe: public RuleModifiers<Maybe<T>> {
    T value;

    ParserResult<std::optional<ExposeType<T>>> expose(Context &context) const {
        size_t start = context.state.index;

        auto result = value.expose(context);

        if (auto pointer = result.ptr()) {
            return ParserResult<std::optional<ExposeType<T>>> { std::optional { *pointer } };
        }

        context.state.index = start;

        return ParserResult<std::optional<ExposeType<T>>> { std::nullopt };
    }

    explicit Maybe(T &&value) : value(std::forward<T>(value)) { }
};

template <typename T, typename K>
requires Exposable<T>
struct MaybeMap: public RuleModifiers<Maybe<T>> {
    T value;
    K map;

    using Result = std::invoke_result_t<K, ExposeType<T>>;

    ParserResult<std::optional<Result>> expose(Context &context) const {
        size_t start = context.state.index;

        auto result = value.expose(context);

        if (auto pointer = result.ptr()) {
            return { std::optional { map(std::move(*pointer)) } };
        }

        context.state.index = start;

        return { std::nullopt };
    }

    MaybeMap(T &&value, K &&map) : value(std::forward<T>(value)), map(std::forward<K>(map)) { }
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

auto toSelf = [](auto tuple) { return std::get<0>(tuple); };
auto toStringSelf = [](auto tuple) { return std::string(std::get<0>(tuple)); };

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
            K &map;

            ParserResultFromTuple<Result> operator()(Error &error) {
                return { error };
            }

            ParserResultFromTuple<Result> operator()(typename decltype(result)::Type &v) {
                return { map(std::move(v)) }; // assuming map returns a tuple
            }
        } visitor { map };

        return std::visit(visitor, result);
    }

    explicit MapInto(T &&value, K &&map) : value(std::forward<T>(value)), map(std::forward<K>(map)) { }
};

template <typename T>
requires Exposable<T>
struct Many: public RuleModifiers<Many<T>> {
    T value;

    ParserResult<std::vector<ExposeType<T>>> expose(Context &context) const {
        std::vector<ExposeType<T>> list;

        ExposeResultType<T> result;

        size_t lastIndex = context.state.index;

        result = value.expose(context);
        while (auto pointer = result.ptr()) {
            list.push_back(*pointer);
            lastIndex = context.state.index;

            result = value.expose(context);
        }

        // always, since only way to exit that loop is for an error to happen
        context.state.index = lastIndex;

        return { list };
    }

    explicit Many(T &&value) : value(std::forward<T>(value)) { }
};

template <typename T, typename K>
requires Exposable<T>
struct ManyMap: public RuleModifiers<ManyMap<T, K>> {
    T value;
    K map;

    using Result = std::invoke_result_t<K, ExposeType<T>>;

    ParserResult<std::vector<Result>> expose(Context &context) const {
        std::vector<Result> list;

        size_t lastIndex = context.state.index;

        ExposeResultType<T> result = value.expose(context);
        while (auto pointer = result.ptr()) {
            list.push_back(map(std::move(*pointer)));
            lastIndex = context.state.index;

            result = value.expose(context);
        }

        // always, since only way to exit that loop is for an error to happen
        context.state.index = lastIndex;

        return ParserResult<std::vector<Result>> { list };
    }

    explicit ManyMap(T &&value, K &&map) : value(std::forward<T>(value)), map(std::forward<K>(map)) { }
};

template <typename T>
struct FirstTupleHelper { };

template <typename T, typename ...Args>
struct FirstTupleHelper<std::tuple<T, Args...>> { using Type = T; };

template <typename T>
using FirstTuple = typename FirstTupleHelper<T>::Type;

template <typename ...Args>
using ResultVariant = std::variant<ExposeType<Args>...>;

template <typename ...Args>
using FirstResultVariant = std::variant<FirstTuple<ExposeType<Args>>...>;

template <bool self, size_t index, typename ...Args>
auto anyOfTupleSized(const std::tuple<Args ...> &value, Context &context) {
    using Tuple = std::tuple<Args ...>;
    using Type = std::conditional_t<self, FirstResultVariant<Args...>, ResultVariant<Args...>>;

    if constexpr (index >= std::tuple_size_v<Tuple>) {
        return context.error<Type>(ErrorNoMatchingPattern());
    } else {
        auto result = expose(std::get<index>(value), context);

        if (auto pointer = result.ptr()) {
            auto ptr = [pointer]() {
                if constexpr (self) {
                    return &std::get<0>(*pointer);
                } else {
                    return pointer;
                }
            };

            return ParserResult<Type>(
                std::make_tuple(
                    Type(
                        std::in_place_index<index>, std::move(*ptr())
                    )
                )
            );
        }

        return anyOfTupleSized<self, index + 1, Args...>(value, context);
    }
}

template <bool self, typename ...Args>
auto anyOfTuple(const std::tuple<Args ...> &value, Context &view) {
    return anyOfTupleSized<self, 0, Args...>(value, view);
}

template <typename ...Args>
struct AnyOf: public RuleModifiers<AnyOf<Args...>> {
    std::tuple<Args...> components;

    auto expose(Context &context) const {
        return anyOfTuple<false>(components, context);
    }

    explicit AnyOf(Args && ...args) : components(std::make_tuple(std::forward<Args>(args)...)) { }
};

template <typename ...Args>
struct AnyOfSelf: public RuleModifiers<AnyOfSelf<Args...>> {
    std::tuple<Args...> components;

    auto expose(Context &context) const {
        return anyOfTuple<true>(components, context);
    }

    explicit AnyOfSelf(Args && ...args) : components(std::make_tuple(std::forward<Args>(args)...)) { }
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
struct Wrap {
    AnyRule<Produces...> *rule;

    ParserResult<Produces...> expose(Context &context) const {
        return rule->dispatch(context);
    }

    explicit Wrap(AnyRule<Produces...> *rule) : rule(rule) { }
};

template <typename ...Args1, typename ...Args2>
ParserResult<Args1..., Args2...> concat(ParserResult<Args1...> &&first, ParserResult<Args2...> &&second) {
    if (auto error = std::get_if<Error>(&first)) {
        return ParserResult<Args1..., Args2...>(std::move(*error));
    }

    if (auto error = std::get_if<Error>(&second)) {
        return ParserResult<Args1..., Args2...>(std::move(*error));
    }

    auto tuple1 = std::get<std::tuple<Args1...>>(first);
    auto tuple2 = std::get<std::tuple<Args2...>>(second);

    return ParserResult<Args1..., Args2...> {
        std::tuple_cat(tuple1, tuple2)
    };
}

template <size_t index, typename ...Args>
auto exposeTupleSized(const std::tuple<Args ...> &value, Context &view) {
    using Tuple = std::tuple<Args ...>;

    if constexpr (index >= std::tuple_size_v<Tuple>) {
        return ParserResult<> { std::tuple<> { } };
    } else {
        return concat(expose(std::get<index>(value), view), exposeTupleSized<index + 1, Args...>(value, view));
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

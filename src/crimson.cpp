#include <crimson/crimson.h>

std::string reasonSubtext(const ErrorMustMatchText &reason) {
    std::stringstream stream;
    stream << "Expected " << reason.text << " but got something else.";

    return stream.str();
}

std::string reasonSubtext(const ErrorRequiresSpaceAfter &reason) {
    std::stringstream stream;
    stream << "Expected trailing space after " << reason.keyword << " but got something else.";

    return stream.str();
}

std::string reasonSubtext(const ErrorMissingToken &reason) {
    return "Expected some token here.";
}

std::string reasonSubtext(const ErrorProhibitsPattern &reason) {
    return "This pattern is explicitly prohibited here.";
}

std::string reasonSubtext(const ErrorNoMatchingPattern &reason) {
    return "Expected some subpattern here but gone none.";
}

std::string reasonText(const ErrorReason &reason) {
    return std::visit([](const auto &value) { return reasonSubtext(value); }, reason);
}

Error::Error(size_t index, ErrorReason reason, bool matched)
    : index(index), reason(std::move(reason)), matched(matched) { }

std::unordered_set<char> hardCharacters() {
    return {
        ':', ';', ',', '.', '{', '}', '+', '-',
        '=', '/', '\\', '@', '#', '$', '%', '^',
        '&', '|', '*', '(', ')', '!', '?', '<',
        '>', '~', '[', ']', '\"', '\''
    };
}

bool AnyHard::stop(std::string_view view, State &state) const {
    auto value = view[0];

    return std::isspace(value) || stopAt.find(value) != stopAt.end();
}

AnyHard::AnyHard() : stopAt(hardCharacters()) { }
AnyHard::AnyHard(std::unordered_set<char> stopAt) : stopAt(std::move(stopAt)) { }

bool NotSpace::stop(std::string_view view, State &state) const {
    return !std::isspace(view[0]);
}

bool StringStops::stop(std::string_view view, State &state) const {
    return std::any_of(stops.begin(), stops.end(), [view](auto stop) {
        return stop.size() <= view.size() && view.substr(0, stop.size()) == stop;
    });
}

StringStops::StringStops(const std::vector<std::string_view> &stops) : stops(stops) { }

void State::push(const Stoppable &stoppable) {
    while (index < count && !stoppable.stop({ &text[index], count - index }, *this)) {
        index++;
    }
}

void State::pop(size_t size, const Stoppable &stoppable) {
    index += size;

    push(stoppable);
}

std::string_view State::pull(size_t size) const {
    return { &text[index], std::min(size, count - index) };
}

size_t State::until(const Stoppable &stoppable) {
    size_t size = 0;

    while ((index + size) < count) {
        if (stoppable.stop({ &text[index + size], count - index - size }, *this)) {
            break;
        }

        size++;
    }

    return size;
}

bool State::ends(size_t size, const Stoppable &stoppable) {
    return (index + size >= count) || stoppable.stop({ &text[index + size], count - index - size }, *this);
}

State::State(std::string_view view) : text(view.data()), index(0), count(view.size()) { }

Context Context::extend(const Stoppable *s, const Stoppable *t) {
    return Context {
        state,
        s ? *s : space,
        t ? *t : token,
    };
}

void Context::push() { state.push(space); }

void Context::pop(size_t size) { state.pop(size, space); }

std::string_view Context::pull(size_t size) const { return state.pull(size); }

bool Context::ends(size_t size) { return state.ends(size, token); }

Error Context::rawError(ErrorReason reason) const {
    return Error {
        state.index,
        std::move(reason),
        matched
    };
}

Context::Context(State &state, const Stoppable &space, const Stoppable &token)
    : state(state), space(space), token(token) { }
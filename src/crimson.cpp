#include <crimson/crimson.h>

std::string reasonText(const ErrorMustMatchText &reason) {
    std::stringstream stream;
    stream << "Expected " << reason.text << " but got something else.";

    return stream.str();
}

std::string reasonText(const ErrorRequiresSpaceAfter &reason) {
    std::stringstream stream;
    stream << "Expected " << reason.keyword << " but got something else.";

    return stream.str();
}

std::string reasonText(const ErrorMissingToken &reason) {
    return "Expected some token here.";
}

std::string reasonText(const ErrorProhibitsPattern &reason) {
    return "This pattern is explicitly prohibited here.";
}

std::string reasonText(const ErrorReason &reason) {
    return std::visit([](const auto &value) { return reasonText(value); }, reason);
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

bool AnyHard::stop(std::string_view view) const {
    auto value = view[0];

    return std::isspace(value) || stopAt.find(value) != stopAt.end();
}

AnyHard::AnyHard() : stopAt(hardCharacters()) { }
AnyHard::AnyHard(std::unordered_set<char> stopAt) : stopAt(std::move(stopAt)) { }

bool NotSpace::stop(std::string_view view) const {
    return !std::isspace(view[0]);
}

bool StringStops::stop(std::string_view view) const {
    return std::any_of(stops.begin(), stops.end(), [view](auto stop) {
        return stop.size() < view.size() && view.substr(0, view.size()) == stop;
    });
}

StringStops::StringStops(const std::vector<std::string_view> &stops) : stops(stops) { }

void State::push(const Stoppable &stoppable) {
    while (index < count && !stoppable.stop({ &text[index], count - index })) {
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

size_t State::until(const Stoppable &stoppable) const {
    size_t size = 0;

    while ((index + size) < count) {
        if (stoppable.stop({ &text[index + size], count - index - size })) {
            break;
        }

        size++;
    }

    return size;
}

bool State::ends(size_t size, const Stoppable &stoppable) const {
    return (index + size >= count) || stoppable.stop({ &text[index + size], count - index - size });
}

State::State(std::string_view view) : text(view.data()), index(0), count(view.size()) { }

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

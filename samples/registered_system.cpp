#include "app/app.hpp"

#include <cstddef>
#include <print>

using namespace fei;

namespace {

struct Score {
    int value {0};
};

struct CallbackSystems {
    RegisteredSystemId print_score;
};

struct DemoState {
    std::size_t frame {0};
};

void print_score(ResRO<Score> score) {
    std::println(
        "score = {}, changed since last callback = {}",
        score->value,
        score.is_changed()
    );
}

void trigger_callback(
    Commands commands,
    ResRO<CallbackSystems> callbacks,
    ResRW<DemoState> demo,
    ResRW<Score> score,
    ResRW<AppStates> app_states
) {
    ++demo->frame;
    if (demo->frame != 2) {
        ++score->value;
    }

    commands.run_system(callbacks->print_score);

    if (demo->frame == 3) {
        app_states->should_stop = true;
    }
}

} // namespace

int main() {
    App app;
    auto print_score_id = app.register_system(print_score);

    app.add_resource(Score {})
        .add_resource(CallbackSystems {.print_score = print_score_id})
        .add_resource(DemoState {})
        .add_systems(Update, trigger_callback);

    app.run();
    return 0;
}

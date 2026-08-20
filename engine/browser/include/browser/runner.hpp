#pragma once

namespace fei {

class App;

// Transfers the App into browser-owned storage and drives it with
// requestAnimationFrame until AppStates::should_stop is set.
void run_browser_app(App&& app);

} // namespace fei

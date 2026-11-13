#pragma once
// Deck-specific pieces of the shared code, implemented in src/decks/<DECK>/app/deck_hooks.cpp.
namespace deck {

void console_status();  // board lines of the console `status` command (touch, display, power keys)
const char *console_help();  // extra help lines of this deck's commands ("" when none)
// This deck's own console commands; true when `cmd` was one of them (it answered).
bool console_command(const char *cmd, const char *rest);

}  // namespace deck

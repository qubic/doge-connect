#pragma once

#include <atomic>
#include <stop_token>

/**
* @brief Handle keyboard input, currently supported: 'q' for quitting the application.
* @param st Stop token of the thread.
* @param keepRunning Atomic flag signaling the main thread to gracefully stop the whole application.
*/
void inputThreadLoop(std::stop_token st, std::atomic<bool>& keepRunning);